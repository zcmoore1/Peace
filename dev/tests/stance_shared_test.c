/* MW2's stance and prone: the shared bg definitions pmove, the server and cgame build on.
 * The box rule, the legs anim's stance, the spawn eye, the stance events' routing, the
 * spread placeholder, MW2's trace walkable byte, and the bullet-only hit volumes' contract
 * and ray test (BG_PlayerHitVolumes, BG_HitVolumeTrace; owner's decision D7). Every number
 * is read from the code's constants: the contract in every state, and the over-the-box and
 * prone-body volumes' own geometry. */
#include "../../code/qcommon/q_shared.h"
#include "../../code/game/bg_public.h"
#include "../../code/game/bg_local.h"

static int checks;
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void trap_SnapVector(float *v) { v[0] = rintf(v[0]); v[1] = rintf(v[1]); v[2] = rintf(v[2]); }
void QDECL Com_Error(int level, const char *fmt, ...) {
	va_list args; (void)level; va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args); exit(1);
}
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define NEAR(a, b, e) (fabs((double)(a) - (double)(b)) <= (e))

/* The one box rule: dead, prone, crouched, standing; a spectator stands. */
static void TestBox(void) {
	const int types[] = { PM_NORMAL, PM_DEAD, PM_SPECTATOR, PM_NOCLIP };
	const int flags[] = { 0, PMF_DUCKED, PMF_PRONE, PMF_DUCKED | PMF_PRONE };
	int t, f;
	for (t = 0; t < ARRAY_LEN(types); t++) for (f = 0; f < ARRAY_LEN(flags); f++) {
		playerState_t ps; vec3_t mins, maxs; float top;
		memset(&ps, 0, sizeof(ps)); ps.pm_type = types[t]; ps.pm_flags = flags[f];
		BG_PlayerBox(&ps, mins, maxs);
		CHECK(mins[0] == -PLAYER_WIDTH && mins[1] == -PLAYER_WIDTH && mins[2] == MINS_Z);
		CHECK(maxs[0] == PLAYER_WIDTH && maxs[1] == PLAYER_WIDTH);
		top = types[t] == PM_SPECTATOR ? DEFAULT_HEIGHT : types[t] == PM_DEAD ? DEAD_HEIGHT :
			(flags[f] & PMF_PRONE) ? PRONE_HEIGHT : (flags[f] & PMF_DUCKED) ? CROUCH_HEIGHT : DEFAULT_HEIGHT;
		CHECK(maxs[2] == top);
	}
	/* MW2's body: the prone box is the crouch box less MW2's 20, the prone eye 29 below the crouch's */
	CHECK(PRONE_HEIGHT < CROUCH_HEIGHT && PRONE_VIEWHEIGHT < CROUCH_VIEWHEIGHT && PRONE_VIEWHEIGHT > DEAD_VIEWHEIGHT);
	CHECK(BG_EYE_PRONE + MINS_Z == PRONE_VIEWHEIGHT && BG_EYE_DEAD + MINS_Z == DEAD_VIEWHEIGHT);
	CHECK(BG_EYE_STAND + MINS_Z == DEFAULT_VIEWHEIGHT && BG_EYE_CROUCH + MINS_Z == CROUCH_VIEWHEIGHT);
	CHECK(BG_WATER_SAMPLE_EYE == BG_EYE_STAND);
}

/* Every legs anim, with and without the toggle bit. */
static void TestLegsAnimStance(void) {
	int a, toggle;
	for (toggle = 0; toggle < 2; toggle++) for (a = 0; a < MAX_TOTALANIMATIONS; a++) {
		int want = (a == LEGS_IDLEPR || a == LEGS_CRAWL || a == LEGS_CRAWLB) ? BG_STANCE_PRONE :
			(a == LEGS_IDLECR || a == LEGS_WALKCR || a == LEGS_BACKCR) ? BG_STANCE_CROUCH : BG_STANCE_STAND;
		CHECK(BG_LegsAnimStance(a | (toggle ? ANIM_TOGGLEBIT : 0)) == want);
	}
	/* the prone anims are appended past MAX_ANIMATIONS (no animation.cfg line) and fit legsAnim's 8 bits */
	CHECK(LEGS_IDLEPR > MAX_ANIMATIONS && LEGS_CRAWLB == MAX_TOTALANIMATIONS - 1 && MAX_TOTALANIMATIONS < ANIM_TOGGLEBIT);
	CHECK(BG_STANCE_STAND == 0 && BG_STANCE_CROUCH == 1 && BG_STANCE_PRONE == 2);	/* MW2's StanceState */
}

/* MW2's spawn eye: standing, no leg; only the stance flags are cleared. */
static void TestSpawnStance(void) {
	playerState_t ps; int keep = PMF_RESPAWNED | PMF_TIME_KNOCKBACK | PMF_ADS | PMF_SPRINTING | PMF_PENDING_MAG;
	memset(&ps, 0, sizeof(ps));
	ps.pm_flags = keep | PMF_DUCKED | PMF_PRONE | PMF_PRONEMOVE_OVERRIDDEN;
	ps.viewheight = (float)PRONE_VIEWHEIGHT; ps.viewHeightTarget = BG_EYE_PRONE;
	ps.viewHeightLerpTarget = BG_EYE_PRONE; ps.viewHeightLerpTime = BG_PRONE_LERP_MS - 1; ps.viewHeightLerpDown = 1;
	ps.proneDirection = 90; ps.proneDirectionPitch = 5; ps.proneTorsoPitch = -5; ps.prevCmd = 0x1ffff;
	BG_SpawnStance(&ps);
	CHECK(ps.pm_flags == keep);
	CHECK(ps.viewheight == DEFAULT_VIEWHEIGHT && ps.viewHeightTarget == BG_EYE_STAND);
	CHECK(ps.viewHeightLerpTarget == 0 && ps.viewHeightLerpTime == 0 && ps.viewHeightLerpDown == 0);
	CHECK(ps.proneDirection == 0 && ps.proneDirectionPitch == 0 && ps.proneTorsoPitch == 0);
	CHECK(ps.prevCmd == 0x1ffff);	/* the previous command is not the spawn's */
}

/* The stance events: appended after the offhand press's, MW2's single-client list. */
static void TestEvents(void) {
	int w;
	CHECK(EV_STANCE_FORCE_STAND == EV_PREP_OFFHAND + 1 && EV_STANCE_FORCE_CROUCH == EV_STANCE_FORCE_STAND + 1);
	CHECK(EV_STANCE_FORCE_PRONE == EV_STANCE_FORCE_CROUCH + 1 && EV_STANCE_INVALID == EV_STANCE_FORCE_PRONE + 1);
	CHECK(EV_STANCE_INVALID < 256);
	CHECK(!BG_EventReachesOthers(EV_STANCE_FORCE_STAND) && !BG_EventReachesOthers(EV_STANCE_FORCE_CROUCH));
	CHECK(!BG_EventReachesOthers(EV_STANCE_FORCE_PRONE) && !BG_EventReachesOthers(EV_STANCE_INVALID));
	CHECK(BG_EventReachesOthers(EV_FIRE_WEAPON) && BG_EventReachesOthers(EV_PREP_OFFHAND));
	CHECK(BG_STANCE_PARM_FORCED == 0 && BG_STANCE_PARM_STAND_BLOCKED == 1);
	CHECK(BG_STANCE_PARM_CROUCH_BLOCKED == 2 && BG_STANCE_PARM_PRONE_BLOCKED == 3);
	for (w = 0; w < MAX_WEAPONS; w++) CHECK(!BG_WeaponBlocksProne(w));	/* only MW2's riot shield blocks prone */
}

/* The spread placeholder: prone counts as crouched, still or moving, on the ground or not. */
static void TestSpread(void) {
	const float speeds[] = { 0, 10, 19.9f, 20, BG_PLAYER_SPEED };
	int i, ground;
	for (ground = 0; ground < 2; ground++) for (i = 0; i < ARRAY_LEN(speeds); i++) {
		playerState_t ducked, prone, standing;
		memset(&ducked, 0, sizeof(ducked)); ducked.weapon = WP_MACHINEGUN; ducked.speed = BG_PLAYER_SPEED;
		ducked.velocity[0] = speeds[i]; ducked.groundEntityNum = ground ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
		prone = standing = ducked; ducked.pm_flags = PMF_DUCKED; prone.pm_flags = PMF_PRONE;
		CHECK(BG_SpreadScale(&prone) == BG_SpreadScale(&ducked));
		if (ground && speeds[i] == 0) CHECK(BG_SpreadScale(&prone) < BG_SpreadScale(&standing));
	}
}

/* MW2's trace walkable byte: never from inside, and MIN_WALK_NORMAL itself is walkable. */
static void TestTraceWalkable(void) {
	trace_t tr;
	memset(&tr, 0, sizeof(tr)); tr.plane.normal[2] = 1;
	CHECK(BG_TraceWalkable(&tr));
	tr.startsolid = qtrue; CHECK(!BG_TraceWalkable(&tr));
	tr.startsolid = qfalse; tr.plane.normal[2] = MIN_WALK_NORMAL; CHECK(BG_TraceWalkable(&tr));
	tr.plane.normal[2] = nextafterf(MIN_WALK_NORMAL, 0); CHECK(!BG_TraceWalkable(&tr));
	tr.plane.normal[2] = 0; CHECK(!BG_TraceWalkable(&tr));
}

static void VectorLerp(const vec3_t a, const vec3_t b, float t, vec3_t out) {
	out[0] = a[0] + t * (b[0] - a[0]); out[1] = a[1] + t * (b[1] - a[1]); out[2] = a[2] + t * (b[2] - a[2]);
}
/* A point in an oriented box, with a margin (positive: a little larger). */
static qboolean Inside(const bgHitVolume_t *v, const vec3_t p, float margin) {
	vec3_t rel; int i;
	VectorSubtract(p, v->center, rel);
	for (i = 0; i < 3; i++) if (fabs(DotProduct(rel, v->axis[i])) > v->halfSize[i] + margin) return qfalse;
	return qtrue;
}
static unsigned int rng = 12345u;
static float Rand(float lo, float hi) { rng = rng * 1664525u + 1013904223u; return lo + (hi - lo) * (float)(rng >> 8) / 16777216.0f; }

/* The ray test: exact entries into the move box and into turned boxes, misses, a start
 * inside, and random segments against random oriented boxes checked by sampling. */
static void TestHitVolumeTrace(void) {
	bgHitVolume_t box; vec3_t a, b, n, mins, maxs, origin = { 100, -50, 24 }, angles; float f; int i, k;
	VectorSet(mins, -PLAYER_WIDTH, -PLAYER_WIDTH, MINS_Z); VectorSet(maxs, PLAYER_WIDTH, PLAYER_WIDTH, DEFAULT_HEIGHT);
	{ entityState_t es; bgHitVolume_t vols[BG_MAX_HIT_VOLUMES]; memset(&es, 0, sizeof(es));
	  CHECK(BG_PlayerHitVolumes(&es, origin, mins, maxs, vols) >= 1); box = vols[0]; }
	CHECK(box.type == HITVOL_BOX);
	/* along each axis, from either side: the entry is at the face, its normal out of it */
	for (i = 0; i < 3; i++) for (k = -1; k <= 1; k += 2) {
		float lo = origin[i] + mins[i], hi = origin[i] + maxs[i], from = k < 0 ? lo - 100 : hi + 100;
		VectorAdd(origin, vec3_origin, a); a[2] += 0.5f * (mins[2] + maxs[2]); VectorCopy(a, b);
		a[i] = from; b[i] = k < 0 ? hi + 100 : lo - 100;
		CHECK(BG_HitVolumeTrace(&box, a, b, &f, n));
		CHECK(NEAR(f, 100.0 / (200.0 + (hi - lo)), 1e-6));
		CHECK(n[i] == (float)k && n[(i + 1) % 3] == 0 && n[(i + 2) % 3] == 0);
	}
	/* a segment that stops short, one passing beside (parallel), one beside a corner */
	VectorSet(a, origin[0] - 100, origin[1], origin[2]); VectorSet(b, origin[0] - PLAYER_WIDTH - 1, origin[1], origin[2]);
	CHECK(!BG_HitVolumeTrace(&box, a, b, &f, n));
	VectorSet(a, origin[0] - 100, origin[1] + PLAYER_WIDTH + 1, origin[2]); VectorSet(b, origin[0] + 100, origin[1] + PLAYER_WIDTH + 1, origin[2]);
	CHECK(!BG_HitVolumeTrace(&box, a, b, &f, n));
	VectorSet(a, origin[0] - 100, origin[1] - 100 + 2 * PLAYER_WIDTH + 1, origin[2]);	/* misses the corner by 1 */
	VectorSet(b, origin[0] + 100, origin[1] + 100 + 2 * PLAYER_WIDTH + 1, origin[2]);
	CHECK(!BG_HitVolumeTrace(&box, a, b, &f, n));
	VectorSet(a, origin[0] - 100, origin[1] - 100 + 2 * PLAYER_WIDTH, origin[2]);	/* touches it: a hit */
	VectorSet(b, origin[0] + 100, origin[1] + 100 + 2 * PLAYER_WIDTH, origin[2]);
	CHECK(BG_HitVolumeTrace(&box, a, b, &f, n) && NEAR(f, (100.0 - PLAYER_WIDTH) / 200.0, 1e-6));
	/* starting inside, as the engine's box (CM_TraceThroughBrush: startsolid,
	 * the fraction kept): a segment that gets out misses, along an axis or
	 * not, and from on a face, outward or inward; one that never leaves is a
	 * hit at 0, the normal back along it (the engine's all-solid) */
	VectorCopy(origin, a); VectorSet(b, origin[0] + 100, origin[1], origin[2]);
	CHECK(!BG_HitVolumeTrace(&box, a, b, &f, n));
	VectorSet(b, origin[0] - 100, origin[1] + 37, origin[2] + 60); CHECK(!BG_HitVolumeTrace(&box, a, b, &f, n));
	VectorSet(a, origin[0] + maxs[0], origin[1], origin[2]); CHECK(!BG_HitVolumeTrace(&box, a, b, &f, n));
	VectorSet(b, origin[0] + 100, origin[1], origin[2]); CHECK(!BG_HitVolumeTrace(&box, a, b, &f, n));
	VectorCopy(origin, a); VectorSet(b, origin[0] + maxs[0] - 1, origin[1], origin[2]);
	CHECK(BG_HitVolumeTrace(&box, a, b, &f, n) && f == 0 && NEAR(n[0], -1, 1e-6) && n[1] == 0 && n[2] == 0);
	VectorSet(b, origin[0] + maxs[0], origin[1], origin[2]);	/* ending on the far face: never out */
	CHECK(BG_HitVolumeTrace(&box, a, b, &f, n) && f == 0);
	CHECK(BG_HitVolumeTrace(&box, a, a, &f, n) && f == 0 && VectorLength(n) == 0);	/* no length */
	/* the prone body's own shape, turned a quarter and an eighth: along x it is
	 * BG_PRONE_TRACE_HALF deep, then BG_PRONE_TRACE_HALF * sqrt(2) */
	{ bgHitVolume_t body; vec3_t fwd, right, up; float yaw;
	  for (yaw = 90; yaw >= 45; yaw -= 45) {
		VectorSet(angles, 0, yaw, 0); AngleVectors(angles, fwd, right, up);
		body.type = HITVOL_PRONE_BODY; VectorCopy(origin, body.center);
		VectorCopy(fwd, body.axis[0]); VectorCopy(right, body.axis[1]); VectorCopy(up, body.axis[2]);
		VectorSet(body.halfSize, BG_PRONE_FEET_DIST / 2, BG_PRONE_TRACE_HALF, (PRONE_HEIGHT - MINS_Z) / 2.0f);
		VectorSet(a, origin[0] - 100, origin[1], origin[2]); VectorSet(b, origin[0] + 100, origin[1], origin[2]);
		CHECK(BG_HitVolumeTrace(&body, a, b, &f, n));
		CHECK(NEAR(f, (100.0 - BG_PRONE_TRACE_HALF / sin(DEG2RAD(yaw))) / 200.0, 1e-5));
		CHECK(NEAR(VectorLength(n), 1, 1e-5) && n[0] < 0);
	  } }
	/* random segments against random oriented boxes: one from inside misses unless it
	 * never leaves; from outside, an entry is on the box and nothing before it is
	 * inside, and a miss never passes through */
	for (k = 0, i = 0; k < 4000; k++) {
		bgHitVolume_t v; vec3_t fwd, right, up, p; int s, steps = 512;
		VectorSet(angles, Rand(-90, 90), Rand(0, 360), Rand(-180, 180)); AngleVectors(angles, fwd, right, up);
		VectorSet(v.center, Rand(-20, 20), Rand(-20, 20), Rand(-20, 20));
		VectorCopy(fwd, v.axis[0]); VectorCopy(right, v.axis[1]); VectorCopy(up, v.axis[2]);
		VectorSet(v.halfSize, Rand(1, 30), Rand(1, 30), Rand(1, 30));
		VectorSet(a, Rand(-80, 80), Rand(-80, 80), Rand(-80, 80)); VectorSet(b, Rand(-80, 80), Rand(-80, 80), Rand(-80, 80));
		if (k & 1) VectorLerp(v.center, a, Rand(0, 0.25f), a);	/* half from near the centre: many inside */
		if (Inside(&v, a, -1e-3f)) {
			i++;
			if (Inside(&v, b, -1e-3f)) CHECK(BG_HitVolumeTrace(&v, a, b, &f, n) && f == 0);
			else if (!Inside(&v, b, 1e-3f)) CHECK(!BG_HitVolumeTrace(&v, a, b, &f, n));
			continue;
		}
		if (Inside(&v, a, 1e-3f)) continue;	/* on a face, within the margin */
		if (BG_HitVolumeTrace(&v, a, b, &f, n)) {
			CHECK(f >= 0 && f <= 1);
			VectorLerp(a, b, f, p); CHECK(Inside(&v, p, 1e-3f));
			if (f > 0) CHECK(NEAR(VectorLength(n), 1, 1e-4) && DotProduct(n, b) - DotProduct(n, a) < 0);
			for (s = 0; s < steps; s++) {
				float t = (float)s / steps; if (t >= f - 1e-3f) break;
				VectorLerp(a, b, t, p); CHECK(!Inside(&v, p, -1e-3f));
			}
		} else {
			for (s = 0; s <= steps; s++) { VectorLerp(a, b, (float)s / steps, p); CHECK(!Inside(&v, p, -1e-3f)); }
		}
	}
	CHECK(i > 1000);	/* plenty started inside */
}

/* What the hit-volume contract guarantees in every state: the move box first, at most
 * BG_MAX_HIT_VOLUMES, a corpse the box alone, and a standing player whose eye is in the
 * box and who is not prone the box alone. */
static void TestHitVolumeContract(void) {
	const int types[] = { PM_NORMAL, PM_DEAD };
	const int flags[] = { 0, PMF_DUCKED, PMF_PRONE };
	const float eyes[] = { (float)DEFAULT_VIEWHEIGHT, 30.5f, (float)CROUCH_VIEWHEIGHT, 0.25f, (float)PRONE_VIEWHEIGHT };
	int t, fl, e, i, n;
	CHECK(BG_HIT_HEAD_ALLOWANCE == CROUCH_HEIGHT - CROUCH_VIEWHEIGHT);	/* the box's reach above its eye, both stances */
	CHECK(BG_HIT_HEAD_ALLOWANCE > 0 && BG_MAX_HIT_VOLUMES == HITVOL_PRONE_BODY + 1);
	CHECK(BG_PRONE_TRACE_HALF < PLAYER_WIDTH && BG_PRONE_TURN_FEET_DIST < BG_PRONE_FEET_DIST);
	for (t = 0; t < ARRAY_LEN(types); t++) for (fl = 0; fl < ARRAY_LEN(flags); fl++) for (e = 0; e < ARRAY_LEN(eyes); e++) {
		playerState_t ps; entityState_t es; bgHitVolume_t vols[BG_MAX_HIT_VOLUMES]; vec3_t mins, maxs;
		memset(&ps, 0, sizeof(ps)); memset(&es, 0, sizeof(es)); memset(vols, 0, sizeof(vols));
		ps.pm_type = types[t]; ps.stats[STAT_HEALTH] = types[t] == PM_DEAD ? 0 : 100; ps.pm_flags = flags[fl];
		ps.viewheight = eyes[e]; ps.proneDirection = 37.5f; ps.proneDirectionPitch = -4;
		VectorSet(ps.origin, -300.125f, 512.5f, 88);
		BG_PlayerStateToEntityState(&ps, &es, qtrue); BG_PlayerBox(&ps, mins, maxs);
		n = BG_PlayerHitVolumes(&es, es.pos.trBase, mins, maxs, vols);
		CHECK(n >= 1 && n <= BG_MAX_HIT_VOLUMES && vols[0].type == HITVOL_BOX);
		for (i = 0; i < 3; i++) {
			CHECK(vols[0].center[i] == es.pos.trBase[i] + 0.5f * (mins[i] + maxs[i]));
			CHECK(vols[0].halfSize[i] == 0.5f * (maxs[i] - mins[i]));
			CHECK(vols[0].axis[i][i] == 1 && vols[0].axis[i][(i + 1) % 3] == 0 && vols[0].axis[i][(i + 2) % 3] == 0);
		}
		if (es.eFlags & EF_DEAD) CHECK(n == 1);
		if (!(es.eFlags & EF_PRONE) && es.origin2[2] <= maxs[2]) CHECK(n == 1);
		for (i = 1; i < n; i++) CHECK(vols[i].type == HITVOL_OVER_BOX || vols[i].type == HITVOL_PRONE_BODY);
	}
}

/* The two extra volumes' exact geometry: over the box while the eye is
 * above it, to the eye plus BG_HIT_HEAD_ALLOWANCE on the box's footprint;
 * the prone body BG_PRONE_FEET_DIST behind along the body's yaw and pitch,
 * BG_PRONE_TRACE_HALF either side, from the feet to the prone top. */
static int VolumeOf(const bgHitVolume_t *vols, int n, int type) {
	int i; for (i = 0; i < n; i++) if (vols[i].type == type) return i; return -1;
}
static void TestHitVolumeGeometry(void) {
	const float yaws[] = { 0, 37.5f, 90, 181.25f, 300 }, pitches[] = { 0, -12, 9.5f };
	playerState_t ps; entityState_t es; bgHitVolume_t vols[BG_MAX_HIT_VOLUMES]; vec3_t mins, maxs, p, f, r, u, ang;
	int y, pi, k, n, o, b;
	/* over the box: a leg's first think reaches the old box's top exactly */
	memset(&ps, 0, sizeof(ps)); ps.pm_type = PM_NORMAL; ps.stats[STAT_HEALTH] = 100;
	VectorSet(ps.origin, 64, -32, 24);
	ps.pm_flags = PMF_DUCKED; ps.viewheight = DEFAULT_VIEWHEIGHT;
	BG_PlayerStateToEntityState(&ps, &es, qtrue); BG_PlayerBox(&ps, mins, maxs);
	n = BG_PlayerHitVolumes(&es, es.pos.trBase, mins, maxs, vols); o = VolumeOf(vols, n, HITVOL_OVER_BOX);
	CHECK(n == 2 && o == 1);
	CHECK(vols[o].center[2] + vols[o].halfSize[2] == es.pos.trBase[2] + DEFAULT_HEIGHT);
	CHECK(vols[o].center[2] - vols[o].halfSize[2] == es.pos.trBase[2] + CROUCH_HEIGHT);
	for (k = 0; k < 2; k++) {
		CHECK(vols[o].center[k] == vols[0].center[k] && vols[o].halfSize[k] == vols[0].halfSize[k]);
		CHECK(vols[o].axis[k][k] == 1);
	}
	ps.pm_flags = PMF_PRONE; ps.viewheight = CROUCH_VIEWHEIGHT;
	BG_PlayerStateToEntityState(&ps, &es, qtrue); BG_PlayerBox(&ps, mins, maxs);
	n = BG_PlayerHitVolumes(&es, es.pos.trBase, mins, maxs, vols); o = VolumeOf(vols, n, HITVOL_OVER_BOX);
	CHECK(n == 3 && o == 1 && VolumeOf(vols, n, HITVOL_PRONE_BODY) == 2);	/* all three, in the order given */
	CHECK(vols[o].center[2] + vols[o].halfSize[2] == es.pos.trBase[2] + CROUCH_HEIGHT);
	CHECK(vols[o].center[2] - vols[o].halfSize[2] == es.pos.trBase[2] + PRONE_HEIGHT);
	/* a shot over cover as tall as the prone box: it hits only while the eye is above */
	{
		vec3_t a, c; float frac; vec3_t nrm;
		VectorSet(a, es.pos.trBase[0] - 200, es.pos.trBase[1], es.pos.trBase[2] + PRONE_HEIGHT + 2);
		VectorSet(c, es.pos.trBase[0] + 200, a[1], a[2]);
		CHECK(!BG_HitVolumeTrace(&vols[0], a, c, &frac, nrm) && BG_HitVolumeTrace(&vols[o], a, c, &frac, nrm));
		ps.viewheight = PRONE_HEIGHT;	/* the eye down to the box's top: gone */
		BG_PlayerStateToEntityState(&ps, &es, qtrue);
		CHECK(VolumeOf(vols, BG_PlayerHitVolumes(&es, es.pos.trBase, mins, maxs, vols), HITVOL_OVER_BOX) < 0);
	}
	/* the prone body, turned and pitched */
	for (y = 0; y < ARRAY_LEN(yaws); y++) for (pi = 0; pi < ARRAY_LEN(pitches); pi++) {
		ps.pm_flags = PMF_PRONE; ps.viewheight = PRONE_VIEWHEIGHT;
		ps.proneDirection = yaws[y]; ps.proneDirectionPitch = pitches[pi];
		BG_PlayerStateToEntityState(&ps, &es, qtrue); BG_PlayerBox(&ps, mins, maxs);
		n = BG_PlayerHitVolumes(&es, es.pos.trBase, mins, maxs, vols); b = VolumeOf(vols, n, HITVOL_PRONE_BODY);
		CHECK(n == 2 && b == 1);
		VectorSet(ang, pitches[pi], yaws[y], 0); AngleVectors(ang, f, r, u);
		for (k = 0; k < 3; k++) {
			CHECK(NEAR(vols[b].axis[0][k], -f[k], 1e-6) && NEAR(vols[b].axis[1][k], r[k], 1e-6) && NEAR(vols[b].axis[2][k], u[k], 1e-6));
			CHECK(NEAR(vols[b].center[k], es.pos.trBase[k] - f[k] * BG_PRONE_FEET_DIST * 0.5f + u[k] * (MINS_Z + PRONE_HEIGHT) * 0.5f, 1e-3));
		}
		CHECK(vols[b].halfSize[0] == BG_PRONE_FEET_DIST * 0.5f && vols[b].halfSize[1] == BG_PRONE_TRACE_HALF &&
			vols[b].halfSize[2] == (PRONE_HEIGHT - MINS_Z) * 0.5f);
		/* in at the feet's end, out past it; in at the side's edge, out past it */
		VectorMA(es.pos.trBase, -(BG_PRONE_FEET_DIST - 1), f, p); VectorMA(p, MINS_Z + 1, u, p); CHECK(Inside(&vols[b], p, 1e-3f));
		VectorMA(es.pos.trBase, -(BG_PRONE_FEET_DIST + 1), f, p); VectorMA(p, MINS_Z + 1, u, p); CHECK(!Inside(&vols[b], p, -1e-3f));
		VectorMA(es.pos.trBase, -BG_PRONE_FEET_DIST * 0.5f, f, p); VectorMA(p, BG_PRONE_TRACE_HALF - 0.5f, r, p); CHECK(Inside(&vols[b], p, 1e-3f));
		VectorMA(p, 1, r, p); CHECK(!Inside(&vols[b], p, -1e-3f));
		VectorMA(es.pos.trBase, -BG_PRONE_FEET_DIST * 0.5f, f, p); VectorMA(p, PRONE_HEIGHT + 1, u, p); CHECK(!Inside(&vols[b], p, -1e-3f));
	}
	/* a corpse lying down is its box */
	ps.pm_type = PM_DEAD; ps.stats[STAT_HEALTH] = 0; BG_PlayerStateToEntityState(&ps, &es, qtrue);
	CHECK((es.eFlags & EF_DEAD) && (es.eFlags & EF_PRONE) && BG_PlayerHitVolumes(&es, es.pos.trBase, mins, maxs, vols) == 1);
}

int main(void) {
	TestBox();
	TestLegsAnimStance();
	TestSpawnStance();
	TestEvents();
	TestSpread();
	TestTraceWalkable();
	TestHitVolumeTrace();
	TestHitVolumeContract();
	TestHitVolumeGeometry();
	printf("PASS: %d stance interface checks\n", checks);
	return 0;
}
