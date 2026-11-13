/* The player picture, through the real CG_Player (cg_players.c), the real tag
 * chain (CG_PositionEntityOnTag / CG_PositionRotatedEntityOnTag, cg_ents.c)
 * and the real gun on the hand (CG_AddPlayerWeapon, cg_weapons.c).
 *
 * The models are Quake 3 placeholders authored for Quake 3's 56-unit box.
 * cg_players.c draws every one of them, live players, corpses and podium
 * players alike, scaled about the feet so the picture fills the standing box
 * (DEFAULT_HEIGHT above MINS_Z): x1.25 with MW2's 70-unit body. Checked here:
 * - the feet stay on the floor and the top reaches the box top;
 * - every piece hung on a tag (torso, head, gun, barrel, flash, flag pole,
 *   flag) sits exactly where the unscaled assembly would put it, scaled about
 *   the feet, so nothing comes apart; each carries the scale;
 * - a moving player's lean, a corpse, a podium player and the viewer's own
 *   body in a mirror are scaled the same way;
 * - the flag swings exactly as the unscaled one would;
 * - the sprites over the head, the shadow and the water splash follow it;
 * - picture only: the entity, its lighting origin and its state are untouched;
 * - MW2's prone body, a placeholder (owner's decision): a live player with
 *   EF_PRONE lies face down along the body's yaw and pitch (origin2[0..1]),
 *   the legs pitched 90 degrees about the yaw's right axis, the model's eye on
 *   the prone eye over the origin and the body running back behind it, scaled
 *   about the rotated feet; the torso held to the legs, the head looking
 *   ahead; the tags below PRONE_HEIGHT; a prone corpse stands for its death;
 *   the prone legs anims are built from the Quake 3 frames.
 * Only the engine traps, the world and modules outside these files are
 * stubbed; the tags are made-up but not trivial (offset and rotated). */
#include "../../code/cgame/cg_players.c"

cg_t cg;
cgs_t cgs;
centity_t cg_entities[MAX_GENTITIES];
weaponInfo_t cg_weapons[MAX_WEAPONS];
itemInfo_t cg_items[MAX_ITEMS];
vmCvar_t cg_swingSpeed, cg_animSpeed, cg_debugAnim, cg_noPlayerAnims, cg_shadows, cg_drawFriend,
 cg_cameraMode, cg_drawGun, cg_debugPosition, cg_thirdPerson, cg_gun_x, cg_gun_y, cg_gun_z,
 cg_weaponDebug, cg_buildScript, cg_deferPlayers, cg_forceModel, cg_teamChatsOnly, cg_trueLightning,
 cg_sight, cg_fov, cg_weaponBob, cg_brassTime, cg_noProjectileTrail, cg_oldPlasma;
#ifdef MISSIONPACK
vmCvar_t cg_singlePlayerActive, cg_hudFiles, cg_enableBreath, cg_enableDust;
#endif

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); \
} } while (0)
#define NEAR(a,b,e) CHECK(fabs((double)(a)-(double)(b)) < (e))

/* ---- what reaches the renderer ---- */
#define MAX_DRAWN 64
static refEntity_t drawn[MAX_DRAWN];
static int drawnCount;
static float markRadius;
static int splashPolys;

void trap_R_AddRefEntityToScene(const refEntity_t *re) {
	CHECK(drawnCount < MAX_DRAWN);
	drawn[drawnCount++] = *re;
}
void trap_R_AddPolyToScene(qhandle_t hShader, int numVerts, const polyVert_t *verts) {
	(void)hShader; (void)numVerts; (void)verts; splashPolys++;
}
void trap_R_AddLightToScene(const vec3_t org, float intensity, float r, float g, float b) {
	(void)org; (void)intensity; (void)r; (void)g; (void)b;
}
void CG_ImpactMark(qhandle_t markShader, const vec3_t origin, const vec3_t dir, float orientation,
	float r, float g, float b, float a, qboolean alphaFade, float radius, qboolean temporary) {
	(void)markShader; (void)origin; (void)dir; (void)orientation; (void)r; (void)g; (void)b; (void)a;
	(void)alphaFade; (void)temporary;
	markRadius = radius;
}

/* ---- the models and their tags ---- */
enum { M_LEGS = 11, M_TORSO, M_HEAD, M_GUN = 21, M_BARREL, M_FLASH, M_POLE = 31, M_FLAG, M_TRAIL };
typedef struct { int model; const char *name; float origin[3], angles[3]; } fakeTag_t;
static const fakeTag_t tags[] = {
	{ M_LEGS,  "tag_torso",  { 0.6f, 0.2f, 1.8f },   { 4, -6, 3 } },
	{ M_TORSO, "tag_head",   { 1.1f, 0.0f, 18.4f },  { -5, 2, 0 } },
	{ M_TORSO, "tag_weapon", { 6.3f, -7.9f, 4.2f },  { 10, 15, -20 } },
	{ M_TORSO, "tag_flag",   { -9.0f, 1.0f, 12.0f }, { 0, 0, 8 } },
	{ M_GUN,   "tag_barrel", { 14.0f, 0.3f, 2.1f },  { 0, 3, 0 } },
	{ M_GUN,   "tag_flash",  { 21.5f, 0.0f, 3.0f },  { -2, 0, 0 } },
	{ M_POLE,  "tag_flag",   { 0.0f, 0.0f, 30.0f },  { 0, 0, 0 } },
};
static const fakeTag_t *FindTag(int model, const char *name) {
	int i;
	for (i = 0; i < ARRAY_LEN(tags); i++) {
		if (tags[i].model == model && !strcmp(tags[i].name, name)) return &tags[i];
	}
	return NULL;
}
int trap_R_LerpTag(orientation_t *tag, clipHandle_t mod, int startFrame, int endFrame,
	float frac, const char *tagName) {
	const fakeTag_t *t = FindTag(mod, tagName);
	(void)startFrame; (void)endFrame; (void)frac;
	memset(tag, 0, sizeof(*tag));
	if (!t) { AxisClear(tag->axis); return qfalse; }
	VectorCopy(t->origin, tag->origin);
	AnglesToAxis(t->angles, tag->axis);
	return qtrue;
}

/* ---- the world: a floor under the player for the shadow, water up to the waist ---- */
static int waterLine = -100000;	/* world z of the water surface; none by default */
void trap_CM_BoxTrace(trace_t *results, const vec3_t start, const vec3_t end, const vec3_t mins,
	const vec3_t maxs, clipHandle_t model, int brushmask) {
	(void)mins; (void)maxs; (void)model; (void)brushmask;
	memset(results, 0, sizeof(*results));
	results->fraction = 0.25f;	/* a floor a quarter of the way down */
	results->endpos[0] = start[0]; results->endpos[1] = start[1];
	results->endpos[2] = start[2] + 0.25f * (end[2] - start[2]);
	results->plane.normal[2] = 1;
}
int CG_PointContents(const vec3_t point, int passEntityNum) {
	(void)passEntityNum;
	return point[2] < waterLine ? CONTENTS_WATER : 0;
}
void CG_Trace(trace_t *result, const vec3_t start, const vec3_t mins, const vec3_t maxs,
	const vec3_t end, int skipNumber, int mask) {
	(void)mins; (void)maxs; (void)skipNumber; (void)mask;
	memset(result, 0, sizeof(*result));
	result->fraction = 1; VectorCopy(end, result->endpos);
	if (start[2] >= waterLine && end[2] < waterLine) {
		result->fraction = (start[2] - waterLine) / (start[2] - end[2]);
		result->endpos[2] = waterLine;
	}
}

/* ---- the rest of the cgame and engine these files call ---- */
void QDECL CG_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL CG_Error(const char *fmt, ...) {
	va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a); exit(1);
}
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level, const char *fmt, ...) {
	va_list a; (void)level; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a); exit(1);
}
void trap_S_AddLoopingSound(int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx) {
	(void)entityNum; (void)origin; (void)velocity; (void)sfx;
}
void trap_S_StartSound(vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx) {
	(void)origin; (void)entityNum; (void)entchannel; (void)sfx;
}
sfxHandle_t trap_S_RegisterSound(const char *sample, qboolean compressed) { (void)sample; (void)compressed; return 0; }
localEntity_t *CG_SmokePuff(const vec3_t p, const vec3_t vel, float radius, float r, float g, float b, float a,
	float duration, int startTime, int fadeInTime, int leFlags, qhandle_t hShader) {
	(void)p; (void)vel; (void)radius; (void)r; (void)g; (void)b; (void)a; (void)duration; (void)startTime;
	(void)fadeInTime; (void)leFlags; (void)hShader;
	return NULL;
}

/* CG_RegisterWeapon's, kept by the linker for CG_AddPlayerWeapon; the test
 * registers its gun itself, so these never run */
qhandle_t trap_R_RegisterModel(const char *name) { (void)name; CHECK(0); return 0; }
qhandle_t trap_R_RegisterShader(const char *name) { (void)name; CHECK(0); return 0; }
void trap_R_ModelBounds(clipHandle_t model, vec3_t mins, vec3_t maxs) { (void)model; (void)mins; (void)maxs; CHECK(0); }
/* an animation.cfg, served only to the prone frames' check */
static const char *animConfig;
int trap_FS_FOpenFile(const char *path, fileHandle_t *f, fsMode_t mode) {
	(void)path; (void)mode; CHECK(animConfig != NULL); *f = 1; return (int)strlen(animConfig);
}
void trap_FS_Read(void *buffer, int len, fileHandle_t f) { (void)f; memcpy(buffer, animConfig, len); }
void trap_FS_FCloseFile(fileHandle_t f) { (void)f; CHECK(animConfig != NULL); }
void CG_WeapAnim_RegisterClips(int weapon, qhandle_t model, const char *path) { (void)weapon; (void)model; (void)path; CHECK(0); }
void CG_Sights_Register(int weapon, const char *path) { (void)weapon; (void)path; CHECK(0); }
localEntity_t *CG_AllocLocalEntity(void) { CHECK(0); return NULL; }
void CG_BubbleTrail(vec3_t start, vec3_t end, float spacing) { (void)start; (void)end; (void)spacing; CHECK(0); }

/* ---- the scene ---- */
#define S CG_PLAYER_MODEL_SCALE
static const vec3_t start = { 120, -40, 64 };

static void Reset(void) {
	clientInfo_t *ci = &cgs.clientinfo[0];
	int a;
	memset(&cg, 0, sizeof(cg)); memset(&cgs, 0, sizeof(cgs)); memset(cg_entities, 0, sizeof(cg_entities));
	memset(cg_weapons, 0, sizeof(cg_weapons));
	cg.snap = &cg.activeSnapshots[0];
	cg.snap->ps.clientNum = 5;	/* the viewer is someone else */
	cg.predictedPlayerState.clientNum = 5;
	cg.time = 20000; cg.frametime = 16;
	cg_swingSpeed.value = 0.3f; cg_animSpeed.integer = 1; cg_shadows.integer = 1; cg_drawGun.integer = 1;
	ci->infoValid = qtrue;
	ci->legsModel = M_LEGS; ci->torsoModel = M_TORSO; ci->headModel = M_HEAD;
	for (a = 0; a < MAX_TOTALANIMATIONS; a++) {
		ci->animations[a].numFrames = 1; ci->animations[a].frameLerp = 100;
		ci->animations[a].initialLerp = 100; ci->animations[a].loopFrames = 1;
	}
	cg_weapons[WP_MACHINEGUN].registered = qtrue;
	cg_weapons[WP_MACHINEGUN].weaponModel = M_GUN;
	cg_weapons[WP_MACHINEGUN].barrelModel = M_BARREL;
	cg_weapons[WP_MACHINEGUN].flashModel = M_FLASH;
	cgs.media.flagPoleModel = M_POLE;
	cgs.media.flagFlapModel = M_FLAG;
	cgs.media.redFlagModel = M_TRAIL;
	cgs.media.balloonShader = 77;
	waterLine = -100000;
}
static centity_t *Player(int number, int clientNum, float yaw, float pitch) {
	centity_t *cent = &cg_entities[number];
	memset(cent, 0, sizeof(*cent));
	cent->currentState.number = number;
	cent->currentState.clientNum = clientNum;
	cent->currentState.eType = ET_PLAYER;
	cent->currentState.weapon = WP_MACHINEGUN;
	cent->currentState.legsAnim = LEGS_IDLE;
	cent->currentState.torsoAnim = TORSO_STAND;
	VectorCopy(start, cent->lerpOrigin);
	VectorCopy(start, cent->currentState.pos.trBase);
	VectorSet(cent->lerpAngles, pitch, yaw, 0);
	cent->pe.legs.yawAngle = cent->pe.torso.yawAngle = yaw;
	return cent;
}
static const refEntity_t *Drawn(int model) {
	int i;
	for (i = 0; i < drawnCount; i++) if (drawn[i].hModel == model && !drawn[i].customShader) return &drawn[i];
	return NULL;
}
static void Draw(centity_t *cent) {
	centity_t before = *cent;
	drawnCount = 0; markRadius = 0; splashPolys = 0;
	CG_Player(cent);
	/* picture only: the entity's place and state are what the game sent */
	CHECK(!memcmp(&before.currentState, &cent->currentState, sizeof(before.currentState)));
	CHECK(VectorCompare(before.lerpOrigin, cent->lerpOrigin));
}

/* A drawn piece carries the scale: axes S long and at right angles. */
static void Unit(const refEntity_t *ent, vec3_t unit[3]) {
	int i, j;
	CHECK(ent->nonNormalizedAxes);
	for (i = 0; i < 3; i++) {
		NEAR(VectorLength(ent->axis[i]), S, 1e-4);
		VectorScale(ent->axis[i], 1.0f / S, unit[i]);
		for (j = 0; j < i; j++) NEAR(DotProduct(ent->axis[i], ent->axis[j]), 0, 1e-4);
	}
}
/* Where the unscaled chain hangs a child: the tag's offset along the parent's unit axes. */
static void Hang(const vec3_t parentOrigin, vec3_t parentUnit[3], int parentModel, const char *tag, vec3_t out) {
	const fakeTag_t *t = FindTag(parentModel, tag);
	int i;
	CHECK(t != NULL);
	VectorCopy(parentOrigin, out);
	for (i = 0; i < 3; i++) VectorMA(out, t->origin[i], parentUnit[i], out);
}
/* The unscaled point, as drawn: scaled about the feet. */
static void ExpectScaled(const vec3_t feet, const vec3_t unscaled, const vec3_t drawnOrigin) {
	vec3_t expect;
	int i;
	for (i = 0; i < 3; i++) expect[i] = feet[i] + S * (unscaled[i] - feet[i]);
	NEAR(Distance(expect, drawnOrigin), 0, 1e-3);
}
/* A model-space point of a drawn piece, in the world. */
static void ModelPoint(const refEntity_t *ent, const vec3_t local, vec3_t out) {
	int i;
	VectorCopy(ent->origin, out);
	for (i = 0; i < 3; i++) VectorMA(out, local[i], ent->axis[i], out);
}

/* The whole assembly, whatever the pose: the feet stay, everything else is
 * the unscaled assembly scaled about them. Returns the feet. */
static void CheckAssembly(const centity_t *cent, qboolean gun, qboolean flash, vec3_t feet) {
	const refEntity_t *legs = Drawn(M_LEGS), *torso = Drawn(M_TORSO), *head = Drawn(M_HEAD);
	const refEntity_t *g = Drawn(M_GUN), *barrel = Drawn(M_BARREL), *fl = Drawn(M_FLASH);
	vec3_t legsU[3], torsoU[3], headU[3], gunU[3], barrelU[3], flashU[3], tagAxis[3], expectAxis[3];
	vec3_t torsoAt, headAt, gunAt, at, point, local;
	const fakeTag_t *t;
	int i;

	CHECK(legs && torso && head);
	Unit(legs, legsU); Unit(torso, torsoU); Unit(head, headU);
	/* lighting is the entity's, as before: the picture does not move the light */
	CHECK(VectorCompare(legs->lightingOrigin, cent->lerpOrigin));
	CHECK(VectorCompare(torso->lightingOrigin, cent->lerpOrigin));
	CHECK(VectorCompare(head->lightingOrigin, cent->lerpOrigin));
	/* the feet: the model's MINS_Z on its own up axis, where the box's floor is */
	VectorMA(cent->lerpOrigin, MINS_Z, legsU[2], feet);
	VectorSet(local, 0, 0, MINS_Z);
	ModelPoint(legs, local, point);
	NEAR(Distance(point, feet), 0, 1e-3);
	/* the model's top (Quake 3's box top) reaches the game's box top */
	VectorSet(local, 0, 0, MINS_Z + CG_PLAYER_MODEL_HEIGHT);
	ModelPoint(legs, local, point);
	VectorMA(cent->lerpOrigin, DEFAULT_HEIGHT, legsU[2], at);
	NEAR(Distance(point, at), 0, 1e-3);
	/* the unscaled chain, scaled about the feet */
	Hang(cent->lerpOrigin, legsU, M_LEGS, "tag_torso", torsoAt);
	ExpectScaled(feet, torsoAt, torso->origin);
	Hang(torsoAt, torsoU, M_TORSO, "tag_head", headAt);
	ExpectScaled(feet, headAt, head->origin);
	if (!gun) { CHECK(!g); return; }
	CHECK(g != NULL);
	Unit(g, gunU);
	Hang(torsoAt, torsoU, M_TORSO, "tag_weapon", gunAt);
	ExpectScaled(feet, gunAt, g->origin);
	/* the gun turns with the hand's tag, exactly: tag axis x the torso's */
	t = FindTag(M_TORSO, "tag_weapon");
	AnglesToAxis(t->angles, tagAxis);
	MatrixMultiply(tagAxis, torsoU, expectAxis);
	for (i = 0; i < 3; i++) NEAR(Distance(expectAxis[i], gunU[i]), 0, 1e-4);
	CHECK(barrel != NULL);
	Unit(barrel, barrelU);
	Hang(gunAt, gunU, M_GUN, "tag_barrel", at);
	ExpectScaled(feet, at, barrel->origin);
	if (!flash) { CHECK(!fl); return; }
	CHECK(fl != NULL);
	Unit(fl, flashU);
	Hang(gunAt, gunU, M_GUN, "tag_flash", at);
	ExpectScaled(feet, at, fl->origin);
}

static void TestStanding(void) {
	centity_t *cent;
	vec3_t feet;
	Reset();
	cent = Player(0, 0, 37, -12);
	Draw(cent);
	CheckAssembly(cent, qtrue, qfalse, feet);
	NEAR(feet[2], start[2] + MINS_Z, 1e-3);	/* upright, on the floor */
	/* the shadow grows with the body */
	NEAR(markRadius, 24 * S, 1e-4);
	/* a muzzle flash on the scaled gun's muzzle */
	Reset();
	cent = Player(0, 0, -150, 20);
	cent->muzzleFlashGun = WP_MACHINEGUN;
	cent->muzzleFlashGunTime = cg.time;
	Draw(cent);
	CheckAssembly(cent, qtrue, qtrue, feet);
	printf("standing: box top %d above the origin, picture x%.4f about the feet\n", DEFAULT_HEIGHT, S);
}

/* Running sideways leans the legs (CG_PlayerAngles' roll and pitch): the
 * picture scales about the feet along the model's own up axis. */
static void TestLean(void) {
	static const float velocities[][3] = { { 0, 250, 0 }, { 300, 0, 0 }, { -180, -180, 0 } };
	centity_t *cent;
	vec3_t feet, up = { 0, 0, 1 };
	int i;
	for (i = 0; i < ARRAY_LEN(velocities); i++) {
		const refEntity_t *legs;
		vec3_t legsU[3];
		Reset();
		cent = Player(0, 0, 90, 0);
		cent->currentState.legsAnim = LEGS_RUN;
		cent->currentState.torsoAnim = TORSO_STAND;
		VectorCopy(velocities[i], cent->currentState.pos.trDelta);
		Draw(cent);
		CheckAssembly(cent, qtrue, qfalse, feet);
		legs = Drawn(M_LEGS);
		Unit(legs, legsU);
		CHECK(DotProduct(legsU[2], up) < 0.9999f);	/* really leaning */
	}
}

/* A corpse is a body-queue entity (number past MAX_CLIENTS) drawn with a
 * client's models; a podium player is one too, at its own place and yaw; the
 * viewer's own body is drawn only in mirrors. All the same picture. */
static void TestCorpsePodiumMirror(void) {
	centity_t *cent;
	vec3_t feet;
	Reset();
	cent = Player(MAX_CLIENTS + 3, 0, 200, 0);
	cent->currentState.eFlags = EF_DEAD;
	cent->currentState.legsAnim = BOTH_DEAD1;
	cent->currentState.torsoAnim = BOTH_DEAD1;
	cent->currentState.weapon = WP_NONE;
	Draw(cent);
	CheckAssembly(cent, qfalse, qfalse, feet);
	NEAR(feet[2], start[2] + MINS_Z, 1e-3);

	Reset();
	cent = Player(MAX_CLIENTS + 9, 0, -20, 0);	/* a podium player, gun in hand */
	VectorSet(cent->lerpOrigin, -300, 512, 98);
	VectorCopy(cent->lerpOrigin, cent->currentState.pos.trBase);
	Draw(cent);
	CheckAssembly(cent, qtrue, qfalse, feet);
	NEAR(feet[2], 98 + MINS_Z, 1e-3);

	Reset();
	cg.snap->ps.clientNum = 0;	/* the viewer's own body, first person */
	cent = Player(0, 0, 10, 0);
	Draw(cent);
	CHECK(Drawn(M_LEGS)->renderfx & RF_THIRD_PERSON);	/* mirrors only */
	CheckAssembly(cent, qtrue, qfalse, feet);
}

/* The flag hangs on the torso's tag and on the pole's: both carry the scale,
 * and it swings exactly as the unscaled flag would. The old-style trailing
 * flag rides on the scaled back. */
static void TestFlag(void) {
	centity_t *cent;
	const refEntity_t *pole, *flag;
	vec3_t feet, legsU[3], torsoU[3], poleU[3], flagU[3], torsoAt, poleAt, at, dir;
	float d, angle, target;
	int frame;
	Reset();
	cgs.clientinfo[0].newAnims = qtrue;
	cent = Player(0, 0, 30, 0);
	cent->currentState.powerups = 1 << PW_REDFLAG;
	cent->currentState.legsAnim = LEGS_RUN;
	VectorSet(cent->currentState.pos.trDelta, 0, 250, 0);
	for (frame = 0; frame < 400; frame++) {	/* let the swing settle */
		cg.time += 16;
		Draw(cent);
	}
	CheckAssembly(cent, qtrue, qfalse, feet);
	pole = Drawn(M_POLE); flag = Drawn(M_FLAG);
	CHECK(pole && flag);
	Unit(Drawn(M_LEGS), legsU); Unit(Drawn(M_TORSO), torsoU); Unit(pole, poleU); Unit(flag, flagU);
	/* positions: the unscaled chain, scaled about the feet */
	Hang(cent->lerpOrigin, legsU, M_LEGS, "tag_torso", torsoAt);
	Hang(torsoAt, torsoU, M_TORSO, "tag_flag", poleAt);
	ExpectScaled(feet, poleAt, pole->origin);
	Hang(poleAt, poleU, M_POLE, "tag_flag", at);
	ExpectScaled(feet, at, flag->origin);
	/* the swing's target, from the unit pole axes (CG_PlayerFlag, unscaled) */
	VectorCopy(cent->currentState.pos.trDelta, dir);
	dir[2] += 100;
	VectorNormalize(dir);
	CHECK(fabs(DotProduct(poleU[2], dir)) < 0.9f);
	d = DotProduct(poleU[0], dir);
	angle = acos(d > 1 ? 1 : d < -1 ? -1 : d) * 180 / M_PI;
	target = DotProduct(poleU[1], dir) < 0 ? 360 - angle : angle;
	NEAR(AngleSubtract(cent->pe.flag.yawAngle, target), 0, 0.5);

	/* the old trailing flag (no flag animations): on the scaled back, at scale */
	Reset();
	cent = Player(0, 0, 0, 0);
	cent->currentState.powerups = 1 << PW_REDFLAG;
	Draw(cent);
	flag = Drawn(M_TRAIL);
	CHECK(flag && flag->nonNormalizedAxes);
	NEAR(VectorLength(flag->axis[0]), S, 1e-4);
	NEAR(flag->origin[0], start[0] - 16 * S, 1e-3);
	NEAR(flag->origin[2], start[2] + MINS_Z + (16 - MINS_Z) * S, 1e-3);
}

/* What floats over the head, and the splash line, follow the scaled body. */
static void TestSpritesAndSplash(void) {
	centity_t *cent;
	int i, sprite = -1;
	Reset();
	cent = Player(0, 0, 0, 0);
	cent->currentState.eFlags = EF_TALK;
	Draw(cent);
	for (i = 0; i < drawnCount; i++) if (drawn[i].reType == RT_SPRITE && drawn[i].customShader == 77) sprite = i;
	CHECK(sprite >= 0);
	NEAR(drawn[sprite].origin[2], start[2] + CG_PlayerModelZ(48), 1e-3);
	NEAR(CG_PlayerModelZ(48) - DEFAULT_HEIGHT, (48 - (MINS_Z + CG_PLAYER_MODEL_HEIGHT)) * S, 1e-3);
	/* the splash: feet in the water, the top of the box out of it */
	Reset();
	cent = Player(0, 0, 0, 0);
	waterLine = (int)(start[2] + DEFAULT_HEIGHT - 2);
	Draw(cent);
	CHECK(splashPolys == 1);
	Reset();
	cent = Player(0, 0, 0, 0);
	waterLine = (int)(start[2] + DEFAULT_HEIGHT + 2);	/* the head under: no splash */
	Draw(cent);
	CHECK(splashPolys == 0);
}

/* MW2's prone body, the placeholder: the stated pose, not every vertex. */
static void TestProne(void) {
	static const float yaws[] = { 0, 37, -150, 270 }, pitches[] = { 0, 12, -9 }, views[] = { 0, -30, 25 };
	int y, p, v, i;
	for (y = 0; y < ARRAY_LEN(yaws); y++) for (p = 0; p < ARRAY_LEN(pitches); p++) for (v = 0; v < ARRAY_LEN(views); v++) {
		const refEntity_t *legs, *torso, *head;
		vec3_t legsU[3], torsoU[3], headU[3], f, r, u, eye, feet, point, local, top, tagAxis[3], rel[3], tmp[3], expect[3];
		vec3_t angles;
		const fakeTag_t *t;
		centity_t *cent;
		Reset();
		/* the view turned away from the body, as a prone player may look */
		cent = Player(0, 0, yaws[y] + 40, views[v]);
		cent->currentState.eFlags = EF_PRONE;
		cent->currentState.legsAnim = LEGS_IDLEPR;
		cent->currentState.origin2[0] = yaws[y];
		cent->currentState.origin2[1] = pitches[p];
		cent->currentState.origin2[2] = PRONE_VIEWHEIGHT;
		Draw(cent);
		legs = Drawn(M_LEGS); torso = Drawn(M_TORSO); head = Drawn(M_HEAD);
		CHECK(legs && torso && head && Drawn(M_GUN));
		Unit(legs, legsU); Unit(torso, torsoU); Unit(head, headU);
		/* picture only: every piece lit from the entity's own origin */
		CHECK(VectorCompare(legs->lightingOrigin, cent->lerpOrigin) && VectorCompare(torso->lightingOrigin, cent->lerpOrigin) &&
			VectorCompare(head->lightingOrigin, cent->lerpOrigin));
		/* face down, 90 degrees about the yaw's right axis: the model's up runs
		 * along the body's forward, its face down, its left the yaw's left */
		VectorSet(angles, pitches[p], yaws[y], 0);
		AngleVectors(angles, f, r, u);
		for (i = 0; i < 3; i++) {
			NEAR(legsU[2][i], f[i], 1e-4);
			NEAR(legsU[0][i], -u[i], 1e-4);
			NEAR(legsU[1][i], -r[i], 1e-4);
		}
		/* the model's eye (the standing eye up from its feet) is the prone eye,
		 * over the origin; the pivot is the rotated feet, behind it */
		VectorMA(cent->lerpOrigin, PRONE_VIEWHEIGHT, u, eye);
		VectorMA(eye, -(DEFAULT_VIEWHEIGHT - MINS_Z), f, feet);
		VectorSet(local, 0, 0, MINS_Z);
		ModelPoint(legs, local, point);
		NEAR(Distance(point, feet), 0, 1e-3);
		VectorSet(local, 0, 0, MINS_Z + (DEFAULT_VIEWHEIGHT - MINS_Z) / S);
		ModelPoint(legs, local, point);
		NEAR(Distance(point, eye), 0, 1e-3);
		/* the body runs back from the origin: the feet 60 behind, the head's
		 * top (the box's reach above its eye) just ahead */
		VectorSubtract(feet, cent->lerpOrigin, point);
		NEAR(DotProduct(point, f), -(DEFAULT_VIEWHEIGHT - MINS_Z), 1e-3);
		VectorSet(local, 0, 0, MINS_Z + CG_PLAYER_MODEL_HEIGHT);
		ModelPoint(legs, local, top);
		VectorSubtract(top, cent->lerpOrigin, point);
		NEAR(DotProduct(point, f), DEFAULT_HEIGHT - DEFAULT_VIEWHEIGHT, 1e-3);
		/* the torso held to the legs (tag_torso's own turn only), the head
		 * pitched back by 90 less the view's pitch to look ahead */
		t = FindTag(M_LEGS, "tag_torso");
		AnglesToAxis(t->angles, tagAxis);
		MatrixMultiply(tagAxis, legsU, expect);
		for (i = 0; i < 3; i++) NEAR(Distance(expect[i], torsoU[i]), 0, 1e-4);
		t = FindTag(M_TORSO, "tag_head");
		VectorSet(angles, views[v] - 90, 0, 0);
		AnglesToAxis(angles, rel);
		AnglesToAxis(t->angles, tagAxis);
		MatrixMultiply(rel, tagAxis, tmp);
		MatrixMultiply(tmp, torsoU, expect);
		for (i = 0; i < 3; i++) NEAR(Distance(expect[i], headU[i]), 0, 1e-4);
		/* lying down: the legs' root, torso and head tags are below the prone box's top */
		CHECK(legs->origin[2] < cent->lerpOrigin[2] + PRONE_HEIGHT);
		CHECK(torso->origin[2] < cent->lerpOrigin[2] + PRONE_HEIGHT);
		CHECK(head->origin[2] < cent->lerpOrigin[2] + PRONE_HEIGHT);
	}
	printf("prone: the head at the origin, the feet %d behind, lying along the body's yaw and pitch\n",
		DEFAULT_VIEWHEIGHT - MINS_Z);
	/* a prone corpse plays its death standing; a crouched player stands in the picture's sense */
	{
		centity_t *cent;
		vec3_t feet;
		Reset();
		cent = Player(MAX_CLIENTS + 3, 0, 200, 0);
		cent->currentState.eFlags = EF_DEAD | EF_PRONE;
		cent->currentState.origin2[0] = 90;
		cent->currentState.legsAnim = BOTH_DEAD1; cent->currentState.torsoAnim = BOTH_DEAD1;
		cent->currentState.weapon = WP_NONE;
		Draw(cent);
		CheckAssembly(cent, qfalse, qfalse, feet);
		NEAR(feet[2], start[2] + MINS_Z, 1e-3);
		Reset();
		cent = Player(0, 0, 10, 0);
		cent->currentState.eFlags = EF_CROUCHING; cent->currentState.legsAnim = LEGS_IDLECR;
		Draw(cent);
		CheckAssembly(cent, qtrue, qfalse, feet);
		NEAR(feet[2], start[2] + MINS_Z, 1e-3);
	}
}

/* The prone legs anims, built after an animation.cfg's parse as LEGS_BACKCR
 * is: the straight idle, and the crouch walk forward and reversed. */
static void TestProneFrames(void) {
	static char config[4096];
	clientInfo_t ci;
	int i, n = 0;
	for (i = 0; i < MAX_ANIMATIONS; i++) n += Com_sprintf(config + n, sizeof(config) - n, "%d %d %d %d\n", i * 10, 5 + i, i, 15 + i);
	animConfig = config;
	memset(&ci, 0, sizeof(ci));
	CHECK(CG_ParseAnimationFile("models/players/test/animation.cfg", &ci));
	animConfig = NULL;
	CHECK(!memcmp(&ci.animations[LEGS_IDLEPR], &ci.animations[LEGS_IDLE], sizeof(animation_t)));
	CHECK(ci.animations[LEGS_CRAWL].firstFrame == ci.animations[LEGS_WALKCR].firstFrame &&
		ci.animations[LEGS_CRAWL].numFrames == ci.animations[LEGS_WALKCR].numFrames && !ci.animations[LEGS_CRAWL].reversed);
	CHECK(ci.animations[LEGS_CRAWLB].firstFrame == ci.animations[LEGS_WALKCR].firstFrame &&
		ci.animations[LEGS_CRAWLB].numFrames == ci.animations[LEGS_WALKCR].numFrames && ci.animations[LEGS_CRAWLB].reversed);
	CHECK(ci.animations[LEGS_IDLEPR].numFrames > 0 && ci.animations[LEGS_CRAWL].numFrames > 0);
	CHECK(BG_LegsAnimStance(LEGS_CRAWLB | ANIM_TOGGLEBIT) == BG_STANCE_PRONE);
}

int main(void) {
	/* the scale fills the box the game uses */
	NEAR(S, (double)(DEFAULT_HEIGHT - MINS_Z) / CG_PLAYER_MODEL_HEIGHT, 1e-6);
	NEAR(CG_PlayerModelZ(MINS_Z), MINS_Z, 1e-6);
	NEAR(CG_PlayerModelZ(MINS_Z + CG_PLAYER_MODEL_HEIGHT), DEFAULT_HEIGHT, 1e-4);
	TestStanding();
	TestLean();
	TestCorpsePodiumMirror();
	TestFlag();
	TestSpritesAndSplash();
	TestProne();
	TestProneFrames();
	printf("PASS: every player, corpse and podium model fills the %d-unit box, x%.4f about the feet (%d checks).\n",
		DEFAULT_HEIGHT - MINS_Z, S, checks);
	return 0;
}
