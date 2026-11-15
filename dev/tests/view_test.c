/* MW2's first-person camera, through the real cgame: CG_CalcViewValues
 * (cg_view.c) with the real sights (cg_sights.c, reading the shipped .sights
 * files) and the real Pmove over a flat floor (fake_reload_pmove.c) for the
 * eye and the aim permission.
 *
 * - The FOV is MW2's (CG_GetViewFov 0x40D3C0, CG_CalcFov 0x596D50, the
 *   projection 0x47AC60): cg_fov is horizontal at 4:3, widened for the view's
 *   own aspect, held to 65-80, and aiming lerps toward the sight's zoom FOV,
 *   never wider than the hip. The mouse follows tan(fov/2) / tan(65/2).
 * - The eye is the playerstate's viewheight above the origin, whatever pmove
 *   put there: nothing in the view assumes Quake 3's 26 (50 above the feet).
 *   A stance change is shown think by think as pmove moves the eye (MW2's
 *   curve), first and third person alike, with no cgame ease; the dead eye
 *   falls at one rate.
 * Only the engine traps, the world and the weapon registry are stubbed. */
#include "../../code/cgame/cg_view.c"

cg_t cg;
cgs_t cgs;
centity_t cg_entities[MAX_GENTITIES];
vmCvar_t cg_fov, cg_zoomFov, cg_viewsize, cg_cameraOrbit, cg_cameraOrbitDelay, cg_thirdPersonAngle,
 cg_thirdPersonRange, cg_cameraMode, cg_errorDecay, cg_sight, cg_drawGun;

void FakeReloadPmove( pmove_t *move );	/* fake_reload_pmove.c: the real Pmove over a floor */
int ViewTestSightIn( int weapon );	/* view_sights.c: a sight file's aim-in time */

static int checks;
static snapshot_t snap;
static const char *configOverride;
static FILE *input;
static char cvarName[64], cvarValue[64];
static int cvarSets, underwater;

#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); \
} } while (0)
#define NEAR(a,b,e) CHECK(fabs((double)(a)-(double)(b)) < (e))

/* ---- the engine and the rest of cgame ---- */
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL CG_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level, const char *fmt, ...) {
	va_list args; (void)level;
	va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args); exit(1);
}
void QDECL CG_Error(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args); exit(1);
}
void trap_Cvar_Set(const char *name, const char *value) {
	cvarSets++;
	Q_strncpyz(cvarName, name, sizeof(cvarName));
	Q_strncpyz(cvarValue, value, sizeof(cvarValue));
}
int CG_PointContents(const vec3_t point, int passEntityNum) {
	(void)point; (void)passEntityNum;
	return underwater ? CONTENTS_WATER : 0;
}
void CG_Trace(trace_t *result, const vec3_t start, const vec3_t mins, const vec3_t maxs,
	const vec3_t end, int skipNumber, int mask) {
	(void)start; (void)mins; (void)maxs; (void)skipNumber; (void)mask;
	memset(result, 0, sizeof(*result)); result->fraction = 1; VectorCopy(end, result->endpos);
}
void CG_RegisterWeapon(int weapon) { (void)weapon; }
/* the shipped sight files, as the engine's filesystem serves them */
int trap_FS_FOpenFile(const char *path, fileHandle_t *handle, fsMode_t mode) {
	char full[256]; long size; (void)mode;
	if (configOverride) { *handle = 1; return (int)strlen(configOverride); }
	Com_sprintf(full, sizeof(full), "assets/baseq3/%s", path);
	input = fopen(full, "rb");
	if (!input) { *handle = 0; return -1; }
	fseek(input, 0, SEEK_END); size = ftell(input); rewind(input);
	*handle = 1;
	return (int)size;
}
void trap_FS_Read(void *buffer, int size, fileHandle_t handle) {
	(void)handle;
	if (configOverride) memcpy(buffer, configOverride, size);
	else CHECK(fread(buffer, 1, size, input) == (size_t)size);
}
void trap_FS_FCloseFile(fileHandle_t handle) { (void)handle; if (!configOverride) fclose(input); }
qhandle_t trap_R_RegisterModel(const char *name) { CHECK(name[0]); return 1; }
qhandle_t trap_R_RegisterShader(const char *name) { CHECK(name[0]); return 2; }

/* ---- MW2's arithmetic, written out independently of cg_view.c ---- */
static double Deg(double r) { return r * 180.0 / M_PI; }
static double Rad(double d) { return d * M_PI / 180.0; }
static double Mw2FovY(double fov) { return 2 * Deg(atan(0.75 * tan(Rad(fov) / 2))); }
static double Mw2FovX(double fov, double aspect) { return 2 * Deg(atan(0.75 * tan(Rad(fov) / 2) * aspect)); }
static double Mw2Sensitivity(double fov) { return tan(Rad(fov) / 2) / tan(Rad(65) / 2); }

/* A player standing on the floor at the origin's usual 24 (-MINS_Z) above it. */
static void Reset(int width, int height) {
	playerState_t *ps = &cg.predictedPlayerState;
	memset(&cg, 0, sizeof(cg)); memset(&cgs, 0, sizeof(cgs)); memset(&snap, 0, sizeof(snap));
	cg.snap = &snap; cg.time = 10000;
	cgs.glconfig.vidWidth = width; cgs.glconfig.vidHeight = height;
	cg_viewsize.integer = 100;
	cg_fov.value = CG_FOV_DEFAULT; cg_fov.integer = CG_FOV_DEFAULT;
	cg_zoomFov.value = 22.5f;
	cg_errorDecay.value = 0;
	underwater = 0;
	VectorSet(ps->origin, 0, 0, -MINS_Z);
	ps->groundEntityNum = ENTITYNUM_WORLD;
	ps->gravity = DEFAULT_GRAVITY;
	ps->stats[STAT_HEALTH] = 100;
	ps->weapon = WP_MACHINEGUN;
	ps->weaponstate = WEAPON_READY;
	ps->weaponAnimTime = -1;
	BG_SpawnStance(ps);	/* standing, as ClientSpawn spawns a player */
	ps->commandTime = cg.time;
	snap.ps = *ps;
}
static void View(void) {
	snap.ps = cg.predictedPlayerState;
	CG_CalcViewValues();
}

/* MW2's projection: 4:3 horizontal, widened for the screen. */
static void TestProjection(void) {
	static const int screens[][2] = { {640,480}, {1024,768}, {1680,1050}, {1920,1080}, {2560,1080}, {1280,1024} };
	static const float fovs[] = { 65, 72.5f, 80 };
	int s, f;
	for (s = 0; s < ARRAY_LEN(screens); s++) {
		for (f = 0; f < ARRAY_LEN(fovs); f++) {
			double aspect = (double)screens[s][0] / screens[s][1];
			Reset(screens[s][0], screens[s][1]);
			cg_fov.value = fovs[f];
			View();
			NEAR(cg.refdef.fov_x, Mw2FovX(fovs[f], aspect), .001);
			NEAR(cg.refdef.fov_y, Mw2FovY(fovs[f]), .001);	/* Hor+: the height never changes */
			NEAR(cg.zoomSensitivity, Mw2Sensitivity(fovs[f]), .0001);
		}
	}
	/* MW2's 65: 65 across 4:3, 74.8 at 16:10, 80.7 at 16:9, 51.1 high on all. */
	Reset(640, 480); View();
	NEAR(cg.refdef.fov_x, 65, .001); NEAR(cg.refdef.fov_y, 51.08, .01); NEAR(cg.zoomSensitivity, 1, .0001);
	Reset(1680, 1050); View(); NEAR(cg.refdef.fov_x, 74.8, .05);
	Reset(1920, 1080); View(); NEAR(cg.refdef.fov_x, 80.7, .05); NEAR(cg.refdef.fov_y, 51.1, .05);
	/* A reduced viewport keeps the screen's proportions, so its picture. */
	Reset(1920, 1080); cg_viewsize.integer = 50; View();
	NEAR(cg.refdef.fov_x, Mw2FovX(65, (double)cg.refdef.width / cg.refdef.height), .001);
	/* Quake 3's underwater wave still rides on top. */
	Reset(640, 480); underwater = 1; cg.time = 10625; View();	/* a quarter wave: +1 */
	NEAR(cg.refdef.fov_x, 66, .01); NEAR(cg.refdef.fov_y, Mw2FovY(65) - 1, .01);
}

/* 65 to 80, as MW2's dvar: the value is kept in range, and a frame that
 * still holds an out-of-range value uses the nearest end. */
static void TestRange(void) {
	Reset(640, 480);
	CHECK(!strcmp(XSTRING(CG_FOV_DEFAULT), "65"));
	cg_fov.value = 90; cvarSets = 0;
	View(); NEAR(cg.refdef.fov_x, 80, .001);
	CG_ClampFov(); CHECK(cvarSets == 1 && !strcmp(cvarName, "cg_fov") && !strcmp(cvarValue, "80"));
	cg_fov.value = 30; cvarSets = 0;
	View(); NEAR(cg.refdef.fov_x, 65, .001);
	CG_ClampFov(); CHECK(cvarSets == 1 && !strcmp(cvarValue, "65"));
	cg_fov.value = 0; cvarSets = 0;	/* not a number */
	View(); NEAR(cg.refdef.fov_x, 65, .001);
	CG_ClampFov(); CHECK(cvarSets == 1 && !strcmp(cvarValue, "65"));
	cg_fov.value = 80; cvarSets = 0; CG_ClampFov(); CHECK(cvarSets == 0);
	cg_fov.value = 65; cvarSets = 0; CG_ClampFov(); CHECK(cvarSets == 0);
	/* the fixed-FOV dmflag holds every client to the default */
	cg_fov.value = 80; cgs.dmflags = DF_FIXED_FOV;
	View(); NEAR(cg.refdef.fov_x, 65, .001);
	cgs.dmflags = 0;
	/* intermission: MW2's fixed 90, in the same projection */
	Reset(1920, 1080); cg_fov.value = 72;
	cg.predictedPlayerState.pm_type = PM_INTERMISSION;
	View(); NEAR(cg.refdef.fov_x, Mw2FovX(90, 1920.0 / 1080), .001); NEAR(cg.refdef.fov_y, Mw2FovY(90), .001);
}

/* The real Pmove takes the ADS button and sets the permission; CG_CalcViewValues
 * runs the aim clock and the FOV from it. */
static void AimWith(int weapon, pmove_t *move) {
	playerState_t *ps = &cg.predictedPlayerState;
	BG_SetSlotWeapon(ps, SLOT_PRIMARY, weapon);
	BG_SetSlotWeapon(ps, SLOT_SECONDARY, weapon == WP_SHOTGUN ? WP_M40A3 : WP_SHOTGUN);
	ps->slotFlags[SLOT_PRIMARY] = ps->slotFlags[SLOT_SECONDARY] = 0;
	ps->weapon = weapon; ps->weaponstate = WEAPON_READY; ps->weaponTime = 0; ps->weaponAnimTime = -1;
	ps->ammo[weapon] = BG_WeaponMagSize(weapon);
	memset(move, 0, sizeof(*move));
	move->ps = ps;
	move->cmd.weapon = BG_SLOT_COMMAND + SLOT_PRIMARY;
	move->cmd.buttons = BUTTON_ADS;
	move->cmd.serverTime = ps->commandTime + 8;
	FakeReloadPmove(move);
	CHECK(ps->pm_flags & PMF_ADS);
	cg.time = ps->commandTime;
	View();	/* the aim clock starts here */
}
static void Hold(int msec) {
	cg.time += msec;
	View();
}
static float ExpectedAds(float hip, float zoom) {
	float target = zoom < hip ? zoom : hip;
	return hip + CG_Sights_Fraction() * (target - hip);
}
static void TestAim(void) {
	static const float hips[] = { 65, 72, 80 };
	pmove_t move;
	int h, in, t;
	for (h = 0; h < ARRAY_LEN(hips); h++) {
		/* the SPAS: MW2's spas12_mp adsZoomFov, from its sight file */
		Reset(1920, 1080); cg_fov.value = hips[h];
		AimWith(WP_SHOTGUN, &move);
		CHECK(CG_Sights_ZoomFov(WP_SHOTGUN) == 55);
		NEAR(cg.refdef.fov_x, Mw2FovX(hips[h], 1920.0 / 1080), .001);	/* not yet moved */
		in = ViewTestSightIn(WP_SHOTGUN);
		for (t = 0; t < in; t += 10) {
			float fov;
			Hold(10);
			fov = ExpectedAds(hips[h], 55);
			NEAR(cg.refdef.fov_y, Mw2FovY(fov), .001);
			NEAR(cg.refdef.fov_x, Mw2FovX(fov, 1920.0 / 1080), .001);
			NEAR(cg.zoomSensitivity, Mw2Sensitivity(fov), .0001);
		}
		NEAR(cg.adsFrac, 1, 1e-6);
		NEAR(cg.refdef.fov_y, Mw2FovY(55), .001);	/* the same aimed picture from any hip */
		NEAR(cg.zoomSensitivity, Mw2Sensitivity(55), .0001);
		/* halfway in, f is .5: exactly halfway between the two FOVs */
		Reset(640, 480); cg_fov.value = hips[h];
		AimWith(WP_SHOTGUN, &move); Hold(in / 2);
		NEAR(CG_Sights_Fraction(), .5, 1e-6);
		NEAR(cg.refdef.fov_x, (hips[h] + 55) / 2, .001);

		/* the M300's scope: cheytac_mp's 15, and the overlay is unchanged */
		Reset(1920, 1080); cg_fov.value = hips[h];
		CG_Sights_Register(WP_M40A3, "models/weapons2/m40a3/view.sights");
		AimWith(WP_M40A3, &move);
		CHECK(CG_Sights_ZoomFov(WP_M40A3) == 15);
		Hold(ViewTestSightIn(WP_M40A3)); NEAR(cg.adsFrac, 1, 1e-6);
		NEAR(cg.refdef.fov_y, Mw2FovY(15), .001);
		CHECK(CG_Sights_HideViewModel());

		/* a gun with no sight file: MW2's commonest adsZoomFov */
		Reset(640, 480); cg_fov.value = hips[h];
		AimWith(WP_MACHINEGUN, &move);
		CHECK(CG_Sights_ZoomFov(WP_MACHINEGUN) == CG_SIGHTS_DEFAULT_ZOOM_FOV);
		for (t = 0; t < 1000 && cg.adsFrac < 1; t += 10) Hold(10);	/* no file: the default aim time */
		NEAR(cg.adsFrac, 1, 1e-6);
		NEAR(cg.refdef.fov_x, CG_SIGHTS_DEFAULT_ZOOM_FOV, .001);

		/* a sight wider than the hip never widens it: min( cg_fov, zoom ) */
		configOverride = "iron 0 0 0 0 0 0\nzoomfov 75\n";
		CG_Sights_Register(WP_ROCKET_LAUNCHER, "test");
		configOverride = NULL;
		CHECK(CG_Sights_ZoomFov(WP_ROCKET_LAUNCHER) == 75);
		Reset(640, 480); cg_fov.value = hips[h];
		AimWith(WP_ROCKET_LAUNCHER, &move);
		Hold(ViewTestSightIn(WP_ROCKET_LAUNCHER)); NEAR(cg.adsFrac, 1, 1e-6);
		NEAR(cg.refdef.fov_x, hips[h] < 75 ? hips[h] : 75, .001);
	}
	/* letting go eases back out along the same lerp */
	Reset(640, 480);
	AimWith(WP_SHOTGUN, &move); Hold(ViewTestSightIn(WP_SHOTGUN));
	cg.predictedPlayerState.pm_flags &= ~PMF_ADS;
	Hold(10);
	CHECK(cg.adsFrac > 0 && cg.adsFrac < 1);
	NEAR(cg.refdef.fov_x, ExpectedAds(65, 55), .001);
	for (t = 0; t < 1000 && cg.adsFrac > 0; t += 10) Hold(10);
	NEAR(cg.refdef.fov_x, 65, .001);
}

/* A sight file says its zoom in degrees; the old scale keyword is gone. */
static void TestSightConfig(void) {
	static const char *bad[] = {
		"iron 0 0 0 0 0 0\nzoomfov 0\n", "iron 0 0 0 0 0 0\nzoomfov 171\n",
		"iron 0 0 0 0 0 0\nzoomfov\n", "iron 0 0 0 0 0 0\nfov 0.72\n"
	};
	int i;
	for (i = 0; i < ARRAY_LEN(bad); i++) {
		configOverride = bad[i];
		CG_Sights_Register(WP_SHOTGUN, "test");
		CHECK(CG_Sights_ZoomFov(WP_SHOTGUN) == CG_SIGHTS_DEFAULT_ZOOM_FOV);	/* rejected: hipfire defaults */
	}
	configOverride = "iron 0 0 0 0 0 0\n";	/* no zoom given: the default */
	CG_Sights_Register(WP_SHOTGUN, "test");
	CHECK(CG_Sights_ZoomFov(WP_SHOTGUN) == CG_SIGHTS_DEFAULT_ZOOM_FOV);
	configOverride = NULL;
	CG_Sights_Register(WP_SHOTGUN, "models/weapons2/spas12/view.sights");
	CHECK(CG_Sights_ZoomFov(WP_SHOTGUN) == 55);
}

/* The eye is ps.viewheight above the origin, as the real Pmove sets it. */
static void Step(pmove_t *move, int buttons, int upmove) {
	playerState_t *ps = &cg.predictedPlayerState;
	memset(move, 0, sizeof(*move));
	move->ps = ps;
	move->cmd.buttons = buttons;
	move->cmd.upmove = upmove;
	move->cmd.weapon = BG_SLOT_COMMAND + SLOT_PRIMARY;
	move->cmd.serverTime = ps->commandTime + 8;
	FakeReloadPmove(move);
	cg.time = ps->commandTime;
}
static void Eye(float height) {
	playerState_t *ps = &cg.predictedPlayerState;
	View();
	NEAR(cg.refdef.vieworg[0], ps->origin[0], 1e-4);
	NEAR(cg.refdef.vieworg[1], ps->origin[1], 1e-4);
	NEAR(cg.refdef.vieworg[2], ps->origin[2] + height, 1e-4);
}
/* third person: the camera starts from the same eye (8 above it, then back by
 * the range, here 0), not from a fixed height */
static void ThirdPersonEye(float height) {
	playerState_t *ps = &cg.predictedPlayerState;
	cg.renderingThirdPerson = qtrue; cg_thirdPersonRange.value = 0; cg_thirdPersonAngle.value = 0;
	View();
	NEAR(cg.refdef.vieworg[2], ps->origin[2] + height + 8, 1e-4);
	cg.renderingThirdPerson = qfalse;
}
/* One stance change through the real Pmove, think by think until the eye
 * reaches its end: the box (the flags) changes on the first think, the eye
 * moves only one way and never past its end, and every think the first- and
 * third-person views stand exactly on the playerstate's eye (pmove's curve,
 * MW2's PM_ViewHeightAdjust; no cgame ease). Returns the thinks it took. */
static int Leg(pmove_t *move, int buttons, int upmove, float to) {
	playerState_t *ps = &cg.predictedPlayerState;
	float from = ps->viewheight, last = from;
	int thinks = 0;
	while (ps->viewheight != to) {
		CHECK(thinks * 8 <= 2 * BG_PRONE_LERP_MS);	/* a leg is never longer than prone's */
		Step(move, buttons, upmove);
		thinks++;
		CHECK(!!(ps->pm_flags & PMF_DUCKED) == (to == CROUCH_VIEWHEIGHT));
		if (to < from) CHECK(ps->viewheight <= last && ps->viewheight >= to);
		else CHECK(ps->viewheight >= last && ps->viewheight <= to);
		Eye(ps->viewheight);
		ThirdPersonEye(ps->viewheight);
		last = ps->viewheight;
	}
	return thinks;
}
static void TestEye(void) {
	static const float heights[] = { 36, 16, 31.5f, 8, 35.73f, -13, 2.0625f };	/* MW2's 60 and 40 above the feet, the curve's fractions, others */
	playerState_t *ps = &cg.predictedPlayerState;
	pmove_t move;
	float last;
	int i, down, up, dead;
	Reset(640, 480);
	BG_SetSlotWeapon(ps, SLOT_PRIMARY, WP_MACHINEGUN);
	BG_SpawnStance(ps);	/* as ClientSpawn spawns a player: standing, no leg */
	Step(&move, 0, 0);
	CHECK(ps->viewheight == DEFAULT_VIEWHEIGHT && ps->groundEntityNum == ENTITYNUM_WORLD);
	Eye(DEFAULT_VIEWHEIGHT);
	/* crouch as a held +movedown sends it: MW2's crouch level with TEMP, and
	 * Quake's upmove; pmove takes whichever it reads. The eye follows pmove
	 * over as many thinks as pmove takes. */
	down = Leg(&move, BUTTON_CROUCH | BUTTON_TEMP_STANCE, -127, CROUCH_VIEWHEIGHT);
	Eye(CROUCH_VIEWHEIGHT);
	up = Leg(&move, 0, 0, DEFAULT_VIEWHEIGHT);
	Eye(DEFAULT_VIEWHEIGHT);
	printf("eye above the feet: standing %d, crouched %d (MW2's body: 60, 40); down in %d ms, up in %d ms\n",
		DEFAULT_VIEWHEIGHT - MINS_Z, CROUCH_VIEWHEIGHT - MINS_Z, down * 8, up * 8);
	/* whatever the playerstate carries, nothing else, first and third person */
	for (i = 0; i < ARRAY_LEN(heights); i++) {
		ps->viewheight = heights[i];
		Eye(heights[i]);
		ThirdPersonEye(heights[i]);
	}
	ps->viewheight = DEFAULT_VIEWHEIGHT;
	/* dead: the dead eye, from pmove too, falling at one rate (the last think
	 * may stop short at the end) */
	ps->pm_type = PM_DEAD; ps->stats[STAT_HEALTH] = 0;
	snap.ps.stats[STAT_HEALTH] = 0;
	last = ps->viewheight; dead = 0;
	{
		float rate = 0;
		while (ps->viewheight != DEAD_VIEWHEIGHT) {
			float drop;
			CHECK(dead * 8 <= 1000);
			Step(&move, 0, 0); dead++;
			snap.ps.stats[STAT_HEALTH] = 0;
			drop = last - ps->viewheight;
			CHECK(drop > 0 && ps->viewheight >= DEAD_VIEWHEIGHT);
			if (ps->viewheight != DEAD_VIEWHEIGHT) {
				if (rate) NEAR(drop, rate, 1e-3);
				rate = drop;
			} else if (rate) CHECK(drop <= rate + 1e-3);
			Eye(ps->viewheight);
			last = ps->viewheight;
		}
	}
	Eye(DEAD_VIEWHEIGHT);
}

int main(void) {
	CG_Sights_Register(WP_SHOTGUN, "models/weapons2/spas12/view.sights");
	TestSightConfig();
	TestProjection();
	TestRange();
	TestAim();
	TestEye();
	printf("PASS: MW2's FOV (4:3 horizontal, Hor+, 65-80, aimed lerp, sensitivity) and the eye (%d checks).\n", checks);
	return 0;
}
