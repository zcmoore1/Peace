/* MW2's stance button and prone through the real Pmove (dev/STANCE.md).
 *
 * The world is a set of brushes traced as the engine traces a brush (the
 * clip of ladder_test.c's sloped boxes): floors, walls, ceilings, steps,
 * slopes, water and other players' bodies. Every think is
 * serverTime = commandTime + msec through the real Pmove, every player is
 * spawned through BG_SpawnStance as ClientSpawn does, and every number is
 * read from the code: the eye tables and leg lengths (BG_EyeLegTime,
 * bg_eye*), BG_EYE_*, the BG_PRONE_* constants, BG_PLAYER_SPEED and the
 * scales. Times are counted in thinks: a leg of L ms started on think t0 ends
 * on think t0 + ceil(L / msec), whatever msec is. */
#include "../../code/game/bg_pmove.c"
#include "../../code/qcommon/cm_local.h"	/* SURFACE_CLIP_EPSILON: the test traces as the engine clips */

static playerState_t ps;
static pmove_t move;
static int checks;

#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "FAIL %s:%d: %s (eye %.4f target %d leg %d/%d/%d flags 0x%x org %.3f %.3f %.3f vel %.2f %.2f yaw %.3f body %.3f)\n", \
	__FILE__, __LINE__, #x, ps.viewheight, ps.viewHeightTarget, ps.viewHeightLerpTarget, ps.viewHeightLerpTime, \
	ps.viewHeightLerpDown, ps.pm_flags, ps.origin[0], ps.origin[1], ps.origin[2], ps.velocity[0], ps.velocity[1], \
	ps.viewangles[YAW], ps.proneDirection); exit(1); } } while (0)
#define NEAR(a, b, e) ( fabs( (double)(a) - (double)(b) ) <= (e) )

void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level, const char *fmt, ...) {
	va_list args;
	(void)level;
	va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args);
	exit(1);
}
/* The engine's Q_SnapVector rounds to nearest, as MW2's does. */
void trap_SnapVector(float *v) { v[0] = rintf(v[0]); v[1] = rintf(v[1]); v[2] = rintf(v[2]); }

/* ---------------------------------------------------------------- world */

/* A brush: an axial box, optionally cut by one more plane (solid below the
 * plane n.p <= d), traced as CM_TraceThroughBrush traces a brush whose six
 * axial planes are its bevels. */
typedef struct { vec3_t mn, mx, n; float d; int ent, contents, surfaceFlags; qboolean sloped; } brush_t;
static brush_t brushes[64];
static int numBrushes, traceCount;

static brush_t *AddBrush(float x0, float y0, float z0, float x1, float y1, float z1, int contents) {
	brush_t *b = &brushes[numBrushes++];
	CHECK(numBrushes <= ARRAY_LEN(brushes));
	memset(b, 0, sizeof(*b));
	VectorSet(b->mn, x0, y0, z0); VectorSet(b->mx, x1, y1, z1);
	b->ent = ENTITYNUM_WORLD; b->contents = contents;
	return b;
}
static void AddBox(float x0, float y0, float z0, float x1, float y1, float z1) {
	AddBrush(x0, y0, z0, x1, y1, z1, CONTENTS_SOLID);
}
/* solid below the plane through point p with normal n (normalised here) */
static void AddSlope(float x0, float y0, float z0, float x1, float y1, float z1,
	float nx, float ny, float nz, const vec3_t p) {
	brush_t *b = AddBrush(x0, y0, z0, x1, y1, z1, CONTENTS_SOLID);
	VectorSet(b->n, nx, ny, nz); VectorNormalize(b->n);
	b->d = DotProduct(b->n, p); b->sloped = qtrue;
}
static void BrushTrace(trace_t *t, const brush_t *b, const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end) {
	float enter = -1, leave = 1; int i, k, clip = -1; qboolean startout = qfalse, getout = qfalse;
	memset(t, 0, sizeof(*t)); t->fraction = 1;
	for (i = 0; i < 7; i++) {
		vec3_t n = { 0, 0, 0 }; float d, off = 0, d1, d2, f;
		if (i < 6) { n[i >> 1] = (i & 1) ? -1 : 1; d = (i & 1) ? -b->mn[i >> 1] : b->mx[i >> 1]; }
		else if (b->sloped) { VectorCopy(b->n, n); d = b->d; }
		else continue;
		for (k = 0; k < 3; k++) off += n[k] * (n[k] < 0 ? maxs[k] : mins[k]);
		d1 = DotProduct(start, n) - (d - off); d2 = DotProduct(end, n) - (d - off);
		if (d2 > 0) getout = qtrue;
		if (d1 > 0) startout = qtrue;
		if (d1 > 0 && (d2 >= SURFACE_CLIP_EPSILON || d2 >= d1)) return;
		if (d1 <= 0 && d2 <= 0) continue;
		if (d1 > d2) {
			f = (d1 - SURFACE_CLIP_EPSILON) / (d1 - d2); if (f < 0) f = 0;
			if (f > enter) { enter = f; clip = i; VectorCopy(n, t->plane.normal); }
		} else {
			f = (d1 + SURFACE_CLIP_EPSILON) / (d1 - d2); if (f > 1) f = 1;
			if (f < leave) leave = f;
		}
	}
	if (!startout) { t->startsolid = qtrue; if (!getout) { t->allsolid = qtrue; t->fraction = 0; } return; }
	if (enter < leave && enter > -1 && clip >= 0) t->fraction = enter < 0 ? 0 : enter;
}
static void WorldTrace(trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs,
	const vec3_t end, int pass, int mask) {
	int i, k, ent = ENTITYNUM_NONE, contents = 0, surface = 0; trace_t t;
	(void)pass;
	traceCount++;
	memset(tr, 0, sizeof(*tr)); tr->fraction = 1;
	for (i = 0; i < numBrushes; i++) {
		if (!(brushes[i].contents & mask)) continue;
		BrushTrace(&t, &brushes[i], start, mins, maxs, end);
		if (t.allsolid) tr->allsolid = qtrue;
		if (t.startsolid) tr->startsolid = qtrue;
		if (t.fraction < tr->fraction) {
			qboolean a = tr->allsolid, st = tr->startsolid;
			*tr = t; tr->allsolid = a; tr->startsolid = st;
			ent = brushes[i].ent; contents = brushes[i].contents; surface = brushes[i].surfaceFlags;
		}
	}
	for (k = 0; k < 3; k++) tr->endpos[k] = start[k] + tr->fraction * (end[k] - start[k]);
	tr->entityNum = tr->fraction < 1 ? ent : tr->startsolid ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
	if (tr->fraction < 1) { tr->contents = contents; tr->surfaceFlags = surface; }
}
static int WorldContents(const vec3_t p, int pass) {
	int i, c = 0; (void)pass;
	for (i = 0; i < numBrushes; i++) {
		const brush_t *b = &brushes[i];
		if (p[0] >= b->mn[0] && p[0] <= b->mx[0] && p[1] >= b->mn[1] && p[1] <= b->mx[1] &&
		    p[2] >= b->mn[2] && p[2] <= b->mx[2] && (!b->sloped || DotProduct(p, b->n) <= b->d)) c |= b->contents;
	}
	return c;
}
/* a flat floor at z 0 everywhere */
static void Floor(void) { numBrushes = 0; AddBox(-4000, -4000, -1000, 4000, 4000, 0); }
static const float feetGap = 0.125f;	/* the origin's rest above a floor, as the suites stand */
#define STAND_Z (-MINS_Z + feetGap)

/* --------------------------------------------------------------- player */

static int msecs[] = { 8, 16, 33 };

static void Loadout(int primary, int secondary) {
	bg_class_t cl = *BG_Class(0);
	cl.slot[SLOT_PRIMARY] = primary; cl.slot[SLOT_SECONDARY] = secondary;
	BG_ApplyClass(&ps, &cl);
	ps.ammo[WP_FRAG] = 2; ps.ammo[WP_FLASH] = 2;
	move.cmd.weapon = BG_SlotCommand(&ps, SLOT_PRIMARY);
}
static void Spawn(float x, float y, float z, float yaw) {
	memset(&ps, 0, sizeof(ps)); memset(&move, 0, sizeof(move)); memset(&pml, 0, sizeof(pml));
	move.ps = &ps; move.trace = WorldTrace; move.pointcontents = WorldContents;
	move.tracemask = MASK_PLAYERSOLID;
	ps.pm_type = PM_NORMAL; ps.speed = BG_PLAYER_SPEED; ps.gravity = DEFAULT_GRAVITY;
	ps.groundEntityNum = ENTITYNUM_NONE; ps.clientNum = 0;
	ps.stats[STAT_HEALTH] = ps.stats[STAT_MAX_HEALTH] = 100;
	Loadout(WP_MACHINEGUN, WP_SHOTGUN);
	VectorSet(ps.origin, x, y, z);
	BG_SpawnStance(&ps);
	move.cmd.angles[YAW] = 0;
	ps.delta_angles[YAW] = ANGLE2SHORT(yaw);
	ps.viewangles[YAW] = SHORT2ANGLE((short)ANGLE2SHORT(yaw));
	pm = &move;
}
/* the gun in hand, ready, as the primary */
static void Gun(int weapon) { Loadout(weapon, weapon == WP_SHOTGUN ? WP_MACHINEGUN : WP_SHOTGUN); }

/* one command of msec: the stance bits and moves given, the rest held */
static int evSeq;
static void Cmd(int msec, int buttons, int forward, int right) {
	evSeq = ps.eventSequence;
	move.cmd.buttons = buttons; move.cmd.forwardmove = forward; move.cmd.rightmove = right;
	move.cmd.upmove = 0;
	move.cmd.serverTime = ps.commandTime + msec;
	Pmove(&move);
}
static void Hold(int ms, int msec, int buttons, int forward, int right) {
	while (ms > 0) { Cmd(msec, buttons, forward, right); ms -= msec; }
}
/* events of the last command, by kind and parm */
static int Raised(int ev, int parm) {
	int i, n = 0;
	for (i = evSeq; i < ps.eventSequence; i++) {
		if (i < ps.eventSequence - MAX_PS_EVENTS) continue;
		if ((ps.events[i & (MAX_PS_EVENTS - 1)] & ~EV_EVENT_BITS) == ev &&
		    (parm < 0 || ps.eventParms[i & (MAX_PS_EVENTS - 1)] == parm)) n++;
	}
	return n;
}
static int StanceEvents(void) {
	return Raised(EV_STANCE_FORCE_STAND, -1) + Raised(EV_STANCE_FORCE_CROUCH, -1) +
		Raised(EV_STANCE_FORCE_PRONE, -1) + Raised(EV_STANCE_INVALID, -1);
}
static float Eye(void) { return ps.viewheight - MINS_Z; }
static int Ceil(int a, int b) { return (a + b - 1) / b; }
static float FlatSpeed(void) { return sqrt(ps.velocity[0] * ps.velocity[0] + ps.velocity[1] * ps.velocity[1]); }

/* stand up and settle on the ground: one quiet command, PMF_RESPAWNED cleared */
static void Settle(int msec) {
	Cmd(msec, 0, 0, 0);
	CHECK(ps.groundEntityNum != ENTITYNUM_NONE && !(ps.pm_flags & PMF_RESPAWNED));
}
/* lie down on open flat ground and let both legs finish */
static void GoProne(int msec) {
	int t;
	for (t = 0; t < 400 && !(ps.viewHeightTarget == BG_EYE_PRONE && !ps.viewHeightLerpTarget); t++)
		Cmd(msec, BUTTON_PRONE, 0, 0);
	CHECK(ps.viewHeightTarget == BG_EYE_PRONE && !ps.viewHeightLerpTarget && (ps.pm_flags & PMF_PRONE));
	CHECK(Eye() == BG_EYE_PRONE && move.maxs[2] == PRONE_HEIGHT);
}
static void GoCrouch(int msec) {
	int t;
	for (t = 0; t < 400 && !(ps.viewHeightTarget == BG_EYE_CROUCH && !ps.viewHeightLerpTarget); t++)
		Cmd(msec, BUTTON_CROUCH, 0, 0);
	CHECK((ps.pm_flags & PMF_DUCKED) && Eye() == BG_EYE_CROUCH && !ps.viewHeightLerpTarget);
}

/* ------------------------------------------------------- the eye's curve */

/* An independent reading of a waypoint table: linear between waypoints. */
static float TableAt(const bgEyeWaypoint_t *t, int frac) {
	int i;
	for (i = 0; t[i + 1].frac != -1; i++) {
		if (frac == t[i].frac) return t[i].height;
		if (frac < t[i + 1].frac)
			return t[i].height + (t[i + 1].height - t[i].height) * (frac - t[i].frac) / (float)(t[i + 1].frac - t[i].frac);
	}
	return t[i].height;
}
/* the eye a running leg shows after elapsed ms */
static float LegEye(int lerpTarget, int down, int elapsed) {
	int ms = BG_EyeLegTime(lerpTarget, down), frac = 100 * elapsed / ms;
	return frac >= 100 ? (float)lerpTarget : TableAt(BG_EyeLegTable(lerpTarget, down), frac);
}

static void TestTables(void) {
	const bgEyeWaypoint_t *tables[4] = { bg_eyeStandCrouch, bg_eyeCrouchStand, bg_eyeCrouchProne, bg_eyeProneCrouch };
	const int from[4] = { BG_EYE_STAND, BG_EYE_CROUCH, BG_EYE_CROUCH, BG_EYE_PRONE };
	const int to[4] = { BG_EYE_CROUCH, BG_EYE_STAND, BG_EYE_PRONE, BG_EYE_CROUCH };
	int i, f, n;
	for (i = 0; i < 4; i++) {
		CHECK(tables[i][0].frac == 0 && tables[i][0].height == from[i]);
		for (n = 0; tables[i][n].frac != -1; n++) ;
		CHECK(tables[i][n - 1].frac == 100 && tables[i][n - 1].height == to[i]);
		for (f = 0; f <= 100; f++) CHECK(NEAR(BG_EyeTableLerp(tables[i], f), TableAt(tables[i], f), 1e-4));
		for (n = 0; tables[i][n].frac != -1; n++) CHECK(BG_EyeTableLerp(tables[i], tables[i][n].frac) == tables[i][n].height);
	}
	/* MW2's leg lengths (0x4BFAE0) and the tables they pick */
	CHECK(BG_EyeLegTime(BG_EYE_PRONE, 1) == BG_PRONE_LERP_MS && BG_EyeLegTable(BG_EYE_PRONE, 1) == bg_eyeCrouchProne);
	CHECK(BG_EyeLegTime(BG_EYE_CROUCH, 1) == BG_EYE_LEG_MS && BG_EyeLegTable(BG_EYE_CROUCH, 1) == bg_eyeStandCrouch);
	CHECK(BG_EyeLegTime(BG_EYE_CROUCH, 0) == BG_PRONE_LERP_MS && BG_EyeLegTable(BG_EYE_CROUCH, 0) == bg_eyeProneCrouch);
	CHECK(BG_EyeLegTime(BG_EYE_STAND, 0) == BG_EYE_LEG_MS && BG_EyeLegTable(BG_EYE_STAND, 0) == bg_eyeCrouchStand);
	CHECK(BG_PRONE_LERP_MS < (1 << PS_EYE_LEG_BITS));
}

/* Test 1: every think of each leg shows the table at 100 * elapsed / ms;
 * the start think does not move the eye, and the box changes on it. */
static void LegCurve(int msec, int fromButtons, int toButtons, int lerpTarget, int down, float fromEye, int box) {
	int t, ms = BG_EyeLegTime(lerpTarget, down), end = Ceil(ms, msec);
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(msec);
	if (fromButtons & BUTTON_PRONE) GoProne(msec);
	else if (fromButtons & BUTTON_CROUCH) GoCrouch(msec);
	CHECK(Eye() == fromEye);
	Cmd(msec, toButtons, 0, 0);
	CHECK(ps.viewHeightLerpTarget == lerpTarget && ps.viewHeightLerpDown == down && ps.viewHeightLerpTime == 0);
	CHECK(Eye() == fromEye && move.maxs[2] == box);
	for (t = 1; t <= end; t++) {
		Cmd(msec, toButtons, 0, 0);
		CHECK(NEAR(Eye(), LegEye(lerpTarget, down, t * msec), 1e-4) && move.maxs[2] == box);
		CHECK(t < end ? ps.viewHeightLerpTime == t * msec : !ps.viewHeightLerpTarget);
	}
	CHECK(Eye() == lerpTarget);
}
static void TestEyeCurve(void) {
	int i;
	for (i = 0; i < ARRAY_LEN(msecs); i++) {
		LegCurve(msecs[i], 0, BUTTON_CROUCH, BG_EYE_CROUCH, 1, BG_EYE_STAND, CROUCH_HEIGHT);
		LegCurve(msecs[i], BUTTON_CROUCH, 0, BG_EYE_STAND, 0, BG_EYE_CROUCH, DEFAULT_HEIGHT);
		LegCurve(msecs[i], BUTTON_CROUCH, BUTTON_PRONE, BG_EYE_PRONE, 1, BG_EYE_CROUCH, PRONE_HEIGHT);
		LegCurve(msecs[i], BUTTON_PRONE, BUTTON_CROUCH, BG_EYE_CROUCH, 0, BG_EYE_PRONE, CROUCH_HEIGHT);
	}
}

/* Tests 2 and 3: stand to prone and prone to stand, both legs, in thinks. */
static void TestStandProneStand(void) {
	int i, t;
	for (i = 0; i < ARRAY_LEN(msecs); i++) {
		int msec = msecs[i], leg1 = Ceil(BG_EYE_LEG_MS, msec), leg2 = Ceil(BG_PRONE_LERP_MS, msec);
		Floor(); Spawn(0, 0, STAND_Z, 0); Settle(msec);
		Cmd(msec, BUTTON_PRONE, 0, 0);	/* the press: crouch box, leg 1 */
		CHECK(ps.viewHeightTarget == BG_EYE_CROUCH && ps.viewHeightLerpTarget == BG_EYE_CROUCH &&
			move.maxs[2] == CROUCH_HEIGHT && (ps.pm_flags & PMF_DUCKED) && !(ps.pm_flags & PMF_PRONE));
		for (t = 1; t < leg1; t++) { Cmd(msec, BUTTON_PRONE, 0, 0); CHECK(ps.viewHeightLerpTarget == BG_EYE_CROUCH && move.maxs[2] == CROUCH_HEIGHT); }
		Cmd(msec, BUTTON_PRONE, 0, 0);	/* leg 1's last think: the eye at the crouch, no leg */
		CHECK(Eye() == BG_EYE_CROUCH && !ps.viewHeightLerpTarget && move.maxs[2] == CROUCH_HEIGHT);
		Cmd(msec, BUTTON_PRONE, 0, 0);	/* the next think: leg 2, the prone box, prone */
		CHECK(ps.viewHeightTarget == BG_EYE_PRONE && ps.viewHeightLerpTarget == BG_EYE_PRONE &&
			move.maxs[2] == PRONE_HEIGHT && (ps.pm_flags & PMF_PRONE) && Eye() == BG_EYE_CROUCH);
		for (t = 1; t < leg2; t++) { Cmd(msec, BUTTON_PRONE, 0, 0); CHECK(ps.viewHeightLerpTarget == BG_EYE_PRONE); }
		Cmd(msec, BUTTON_PRONE, 0, 0);
		CHECK(Eye() == BG_EYE_PRONE && !ps.viewHeightLerpTarget);

		/* prone to stand: 11 -> 40 (crouch box on the press), then 40 -> 60 */
		Cmd(msec, 0, 0, 0);
		CHECK(ps.viewHeightTarget == BG_EYE_CROUCH && ps.viewHeightLerpTarget == BG_EYE_CROUCH && !ps.viewHeightLerpDown &&
			move.maxs[2] == CROUCH_HEIGHT && (ps.pm_flags & PMF_DUCKED));
		for (t = 1; t < leg2; t++) { Cmd(msec, 0, 0, 0); CHECK(move.maxs[2] == CROUCH_HEIGHT); }
		Cmd(msec, 0, 0, 0);
		CHECK(Eye() == BG_EYE_CROUCH && !ps.viewHeightLerpTarget && move.maxs[2] == CROUCH_HEIGHT);
		Cmd(msec, 0, 0, 0);
		CHECK(ps.viewHeightTarget == BG_EYE_STAND && ps.viewHeightLerpTarget == BG_EYE_STAND &&
			move.maxs[2] == DEFAULT_HEIGHT && !(ps.pm_flags & (PMF_DUCKED | PMF_PRONE)));
		for (t = 1; t <= leg1; t++) Cmd(msec, 0, 0, 0);
		CHECK(Eye() == BG_EYE_STAND && !ps.viewHeightLerpTarget);
	}
}

/* Test 4: a request mid-leg waits for the leg's end. */
static void TestRequestWaits(void) {
	int msec = 8, t, leg = Ceil(BG_EYE_LEG_MS, msec);
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(msec);
	Cmd(msec, BUTTON_CROUCH, 0, 0);
	for (t = 1; t < leg; t++) {
		Cmd(msec, 0, 0, 0);	/* asks to stand: the target stays */
		CHECK(ps.viewHeightTarget == BG_EYE_CROUCH && (ps.pm_flags & PMF_DUCKED) && move.maxs[2] == CROUCH_HEIGHT);
	}
	Cmd(msec, 0, 0, 0);
	CHECK(Eye() == BG_EYE_CROUCH && !ps.viewHeightLerpTarget);
	Cmd(msec, 0, 0, 0);
	CHECK(ps.viewHeightTarget == BG_EYE_STAND && ps.viewHeightLerpTarget == BG_EYE_STAND && move.maxs[2] == DEFAULT_HEIGHT);
}

/* Test 5: a sprint mid stand-to-crouch reverses the leg; the eye that think
 * is the old leg's (MW2 discards the new table's value). */
static void TestReversal(void) {
	int i, k;
	for (i = 0; i < ARRAY_LEN(msecs); i++) {
		int msec = msecs[i];
		for (k = 1; k < Ceil(BG_EYE_LEG_MS, msec); k++) {
			int frac, elapsed;
			Floor(); Spawn(0, 0, STAND_Z, 0); Settle(msec);
			Cmd(msec, BUTTON_CROUCH, 0, 0);
			for (elapsed = 1; elapsed < k; elapsed++) Cmd(msec, BUTTON_CROUCH, 0, 0);
			elapsed = k * msec;
			frac = 100 * elapsed / BG_EyeLegTime(BG_EYE_CROUCH, 1);
			Cmd(msec, BUTTON_SPRINT, 127, 0);
			CHECK(ps.pm_flags & PMF_SPRINTING);
			CHECK(Raised(EV_STANCE_FORCE_STAND, BG_STANCE_PARM_FORCED) == 1);
			CHECK(ps.viewHeightTarget == BG_EYE_STAND && ps.viewHeightLerpTarget == BG_EYE_STAND && !ps.viewHeightLerpDown);
			CHECK(ps.viewHeightLerpTime == (100 - frac) * BG_EyeLegTime(BG_EYE_STAND, 0) / 100);
			CHECK(NEAR(Eye(), TableAt(bg_eyeStandCrouch, frac), 1e-4));
			CHECK(move.maxs[2] == DEFAULT_HEIGHT && !(ps.pm_flags & (PMF_DUCKED | PMF_PRONE)));
			/* MW2's truncation of frac * 0.01 * ms (x87, the double 0.01 a hair over) */
			CHECK(ps.viewHeightLerpTime == (int)((100 - frac) * (long double)0.01 * BG_EyeLegTime(BG_EYE_STAND, 0)));
		}
	}
}

/* Test 6: the dead eye falls linearly; the flags stay. */
static void TestDeathEye(void) {
	int msec;
	for (msec = 8; msec <= 33; msec += 25) {
		float eye, want;
		Floor(); Spawn(0, 0, STAND_Z, 0); Settle(msec); GoCrouch(msec);
		ps.pm_type = PM_DEAD; ps.stats[STAT_HEALTH] = 0;
		eye = Eye();
		do {
			Cmd(msec, BUTTON_CROUCH, 0, 0);
			want = eye - msec * 0.001f * BG_EYE_DEAD_RATE;
			if (want < BG_EYE_DEAD) want = BG_EYE_DEAD;
			CHECK(NEAR(Eye(), want, 1e-4) && (ps.pm_flags & PMF_DUCKED) && move.maxs[2] == DEAD_HEIGHT);
			CHECK(StanceEvents() == 0 && !ps.viewHeightLerpTarget);
			eye = Eye();
		} while (eye > BG_EYE_DEAD);
		CHECK(ps.viewheight == DEAD_VIEWHEIGHT);
	}
}

/* Test 7: the spawn eye. */
static void TestSpawn(void) {
	Floor(); Spawn(0, 0, STAND_Z, 0);
	CHECK(ps.viewheight == DEFAULT_VIEWHEIGHT && ps.viewHeightTarget == BG_EYE_STAND && !ps.viewHeightLerpTarget);
	ps.pm_flags |= PMF_RESPAWNED | PMF_TIME_KNOCKBACK | PMF_DUCKED | PMF_PRONE | PMF_PRONEMOVE_OVERRIDDEN;
	BG_SpawnStance(&ps);
	CHECK((ps.pm_flags & (PMF_RESPAWNED | PMF_TIME_KNOCKBACK)) == (PMF_RESPAWNED | PMF_TIME_KNOCKBACK));
	CHECK(!(ps.pm_flags & (PMF_DUCKED | PMF_PRONE | PMF_PRONEMOVE_OVERRIDDEN)));
	Cmd(8, 0, 0, 0);
	CHECK(!ps.viewHeightLerpTarget && ps.viewheight == DEFAULT_VIEWHEIGHT);
	/* without it a memset playerstate's eye is 24 above the feet: a leg */
	Spawn(0, 0, STAND_Z, 0); ps.viewheight = 0; ps.viewHeightTarget = 0;
	Cmd(8, 0, 0, 0);
	CHECK(ps.viewHeightLerpTarget != 0);
}

/* ------------------------------------------------------------- speeds */

static float RampDown(int elapsed) {	/* MW2's crouch-to-prone scale */
	float f = (float)elapsed / BG_PRONE_LERP_MS;
	return (1.0f - f) * BG_CROUCH_SPEED_SCALE + f * BG_PRONE_SPEED_SCALE;
}
static float RampUp(int elapsed) {	/* ... and prone-to-crouch */
	float f = (float)elapsed / BG_PRONE_LERP_MS;
	return (1.0f - f) * BG_PRONE_SPEED_SCALE + f * BG_CROUCH_SPEED_SCALE;
}
/* the walk's scale this think, as PM_WalkMove takes it */
static float WalkSpeedNow(int forward, int right) { pm = &move; return PM_WalkScale(forward, right) * 127; }

/* Test 8: the stance scale on every think of both prone legs; the walk and
 * the bob's most take it. */
static void TestStanceScale(void) {
	int i, t;
	for (i = 0; i < ARRAY_LEN(msecs); i++) {
		int msec = msecs[i], end = Ceil(BG_PRONE_LERP_MS, msec);
		Floor(); Spawn(0, 0, STAND_Z, 0); Settle(msec);
		CHECK(BG_StanceScale(&ps) == 1.0f);
		GoCrouch(msec);
		CHECK(BG_StanceScale(&ps) == BG_CROUCH_SPEED_SCALE);
		Cmd(msec, BUTTON_PRONE, 0, 0);	/* the leg's first think: the target's flat scale */
		CHECK(ps.viewHeightLerpTarget == BG_EYE_PRONE && BG_StanceScale(&ps) == BG_PRONE_SPEED_SCALE);
		for (t = 1; t < end; t++) {
			Cmd(msec, BUTTON_PRONE, 0, 0);
			CHECK(BG_StanceScale(&ps) == RampDown(t * msec));
			CHECK(NEAR(WalkSpeedNow(127, 0), BG_PLAYER_SPEED * RampDown(t * msec) * BG_MoveClassScale(&ps), 1e-3));
		}
		Cmd(msec, BUTTON_PRONE, 0, 0);
		CHECK(!ps.viewHeightLerpTarget && BG_StanceScale(&ps) == BG_PRONE_SPEED_SCALE);
		Cmd(msec, BUTTON_CROUCH, 0, 0);	/* up: the first think is the crouch's */
		CHECK(ps.viewHeightLerpTarget == BG_EYE_CROUCH && !ps.viewHeightLerpDown && BG_StanceScale(&ps) == BG_CROUCH_SPEED_SCALE);
		for (t = 1; t < end; t++) {
			Cmd(msec, BUTTON_CROUCH, 0, 0);
			CHECK(BG_StanceScale(&ps) == RampUp(t * msec));
		}
		Cmd(msec, BUTTON_CROUCH, 0, 0);
		CHECK(!ps.viewHeightLerpTarget && BG_StanceScale(&ps) == BG_CROUCH_SPEED_SCALE);
		Cmd(msec, 0, 0, 0);	/* the stand leg: full speed at once */
		CHECK(ps.viewHeightLerpTarget == BG_EYE_STAND && BG_StanceScale(&ps) == 1.0f);
	}
}

/* lie down and wait out the slowdown lying down starts */
static void ProneRested(int msec) {
	int t;
	GoProne(msec);
	for (t = 0; t < 1000 && (ps.jumpLand || FlatSpeed()); t++) Cmd(msec, BUTTON_PRONE, 0, 0);
	CHECK(ps.jumpLand == 0 && FlatSpeed() == 0);
}
static float Crawl(int ms, int msec, int buttons, int forward, int right) {
	Hold(ms, msec, buttons | BUTTON_PRONE, forward, right);
	CHECK(ps.groundEntityNum != ENTITYNUM_NONE && (ps.pm_flags & PMF_PRONE));
	return FlatSpeed();
}
/* the engine snaps each think's speed to a whole unit: MW2's to within half */
static int NearSpeed(float v, float want) { return fabs(v - want) <= 0.5f + 1e-3f; }

/* Test 9: the steady crawls. */
static void TestCrawlSpeeds(void) {
	const float prone = BG_PLAYER_SPEED * BG_PRONE_SPEED_SCALE;
	int t;
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); ProneRested(8);
	CHECK(NearSpeed(Crawl(2000, 8, 0, 127, 0), prone * BG_MoveClassScale(&ps)));
	ProneRested(8);
	CHECK(NearSpeed(Crawl(2000, 8, 0, 0, 127), prone * BG_STRAFE_SPEED_SCALE * BG_MoveClassScale(&ps)));
	ProneRested(8);
	CHECK(NearSpeed(Crawl(2000, 8, 0, -127, 0), prone * BG_BACK_SPEED_SCALE * BG_MoveClassScale(&ps)));
	/* a scoped gun crawls aimed: aim still, then crawl */
	Floor(); Spawn(0, 0, STAND_Z, 0); Gun(WP_M40A3); Settle(8); ProneRested(8);
	for (t = 0; t < 5; t++) Cmd(8, BUTTON_PRONE | BUTTON_ADS, 0, 0);
	CHECK(ps.pm_flags & PMF_ADS);
	CHECK(NearSpeed(Crawl(2000, 8, BUTTON_ADS, 127, 0),
		prone * BG_ADS_SPEED_SCALE * BG_WeaponAdsMoveScale(WP_M40A3) * BG_MoveClassScale(&ps)));
	CHECK((ps.pm_flags & PMF_ADS) && !(ps.pm_flags & PMF_PRONEMOVE_OVERRIDDEN));
}

/* Test 10: prone's accel 19 from rest and the stop, against MW2's walk
 * arithmetic (friction with the stopspeed floor, accel at least as hard as
 * at stopspeed), snapped each think as the engine snaps. */
static float ModelThinkRaw(float v, float wish, float dt) {
	float speed = fabs(v), drop, newspeed;
	if (speed < 1) v = 0;
	else {
		drop = (speed < pm_stopspeed ? pm_stopspeed : speed) * pm_friction * dt;
		newspeed = speed - drop; if (newspeed < 0) newspeed = 0;
		v *= newspeed / speed;
	}
	if (wish > 0 && wish - v > 0) {
		float add = wish - v, acc = pm_proneaccelerate * dt * (wish < pm_stopspeed ? pm_stopspeed : wish);
		v += acc > add ? add : acc;
	}
	return v;
}
static float ModelThink(float v, float wish, float dt) { return rintf(ModelThinkRaw(v, wish, dt)); }
static void TestCrawlAccel(void) {
	int i;
	for (i = 0; i < ARRAY_LEN(msecs); i++) {
		int msec = msecs[i], t, t90 = -1, t99 = -1, m90 = -1, m99 = -1;
		float v = 0, wish, dt = msec * 0.001f, steady;
		Floor(); Spawn(0, 0, STAND_Z, 0); Settle(msec); ProneRested(msec);
		move.cmd.forwardmove = 127; wish = WalkSpeedNow(127, 0);
		CHECK(NEAR(wish, BG_PLAYER_SPEED * BG_PRONE_SPEED_SCALE * BG_MoveClassScale(&ps), 1e-3));
		CHECK(pm_proneaccelerate == 19.0f);
		for (t = 0; t * msec < 1000; t++) {
			Cmd(msec, BUTTON_PRONE, 127, 0);
			v = ModelThink(v, wish, dt);
			CHECK(ps.velocity[0] == v && ps.velocity[1] == 0);
		}
		steady = v;
		/* the time to 90% and 99% is the model's (MW2's), think for think */
		Spawn(0, 0, STAND_Z, 0); Settle(msec); ProneRested(msec); v = 0;
		for (t = 0; t * msec < 1000; t++) {
			Cmd(msec, BUTTON_PRONE, 127, 0); v = ModelThink(v, wish, dt);
			if (t90 < 0 && FlatSpeed() >= 0.9f * steady) t90 = t;
			if (t99 < 0 && FlatSpeed() >= 0.99f * steady) t99 = t;
			if (m90 < 0 && v >= 0.9f * steady) m90 = t;
			if (m99 < 0 && v >= 0.99f * steady) m99 = t;
		}
		CHECK(t90 == m90 && t99 == m99 && t90 >= 0);
		/* the stop */
		for (t = 0; t * msec < 1000 && (v || FlatSpeed()); t++) {
			Cmd(msec, BUTTON_PRONE, 0, 0); v = ModelThink(v, 0, dt);
			CHECK(ps.velocity[0] == v);
		}
		CHECK(v == 0 && FlatSpeed() == 0);
	}
}

/* Test 11: one 132ms command (two 66ms steps) is two 66ms commands: the
 * leg's clock, the eye, the scale and the move never compound; prevCmd is
 * the command before for both steps; a call that runs no step keeps it. */
static void SameState(const playerState_t *a, const playerState_t *b) {
	CHECK(VectorCompare(a->origin, b->origin) && VectorCompare(a->velocity, b->velocity));
	CHECK(a->viewheight == b->viewheight && a->viewHeightLerpTime == b->viewHeightLerpTime &&
		a->viewHeightLerpTarget == b->viewHeightLerpTarget && a->viewHeightTarget == b->viewHeightTarget &&
		a->pm_flags == b->pm_flags && a->prevCmd == b->prevCmd);
}
static void TestChoppedCommand(void) {
	playerState_t start, one;
	int prev, running;
	for (running = 0; running < 2; running++) {
		Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8);
		if (running) { GoCrouch(8); Cmd(8, BUTTON_PRONE, 0, 0); Cmd(8, BUTTON_PRONE, 127, 0); }
		else GoCrouch(8);
		start = ps;
		prev = ps.prevCmd;
		Cmd(132, BUTTON_PRONE, 127, 0);	/* Pmove chops it into 66 + 66 */
		one = ps;
		if (running) {
			CHECK(one.viewHeightLerpTime == start.viewHeightLerpTime + 132);
			CHECK(NEAR(one.viewheight - MINS_Z, LegEye(BG_EYE_PRONE, 1, one.viewHeightLerpTime), 1e-4));
		} else {
			CHECK(one.viewHeightLerpTarget == BG_EYE_PRONE && one.viewHeightLerpTime == 66);
			CHECK(NEAR(one.viewheight - MINS_Z, LegEye(BG_EYE_PRONE, 1, 66), 1e-4));
		}
		CHECK(one.prevCmd == BG_PrevCmdPack(&move.cmd));
		ps = start; Cmd(66, BUTTON_PRONE, 127, 0); Cmd(66, BUTTON_PRONE, 127, 0);
		SameState(&one, &ps);
	}
	/* a call with no time runs no step and keeps the command before */
	prev = ps.prevCmd;
	move.cmd.forwardmove = -64; move.cmd.buttons = BUTTON_ADS; move.cmd.serverTime = ps.commandTime;
	Pmove(&move);
	CHECK(ps.prevCmd == prev);
}

/* Test 12: the bob turns at 0.25 of the posture's most, aimed or not,
 * forward or back, and prone makes no footsteps. */
static void TestProneBob(void) {
	const int dirs[2] = { 127, -127 };
	int d, aimed, t;
	for (aimed = 0; aimed < 2; aimed++) for (d = 0; d < 2; d++) {
		Floor(); Spawn(0, 0, STAND_Z, 0); Gun(WP_M40A3); Settle(8); ProneRested(8);
		if (aimed) { for (t = 0; t < 5; t++) Cmd(8, BUTTON_PRONE | BUTTON_ADS, 0, 0); CHECK(ps.pm_flags & PMF_ADS); }
		for (t = 0; t < 250; t++) {
			int old = ps.bobCycle, want;
			Cmd(8, BUTTON_PRONE | (aimed ? BUTTON_ADS : 0), dirs[d], 0);
			CHECK(Raised(EV_FOOTSTEP, -1) + Raised(EV_FOOTSTEP_METAL, -1) == 0);
			CHECK((ps.legsAnim & ~ANIM_TOGGLEBIT) == (d ? LEGS_CRAWLB : LEGS_CRAWL));
			want = move.xyspeed < 10 ? old : (int)(old + BG_PRONE_BOB_RATE * move.xyspeed / PM_BobMaxSpeed() * 8) & 255;
			CHECK(ps.bobCycle == want);
		}
		/* an aimed backward crawl is too slow to bob at all (MW2's 10) */
		CHECK(!!(ps.pm_flags & PMF_ADS) == aimed && (move.xyspeed >= 10) == !(aimed && d));
	}
	/* lying still: the prone idle */
	Cmd(8, BUTTON_PRONE, 0, 0); Cmd(8, BUTTON_PRONE, 0, 0);
	CHECK((ps.legsAnim & ~ANIM_TOGGLEBIT) == LEGS_IDLEPR);
	/* MW2's prone view bob: (0.02, 0.005) with no aim blend */
	CHECK(BG_ViewBobAmplitude(&ps, 0, 100, 0) == bg_viewBobProne[0] * 100 &&
		BG_ViewBobAmplitude(&ps, 1, 100, 1) == bg_viewBobProne[1] * 100);
}

/* ------------------------------------------------------ room and events */

static float Feet(void) { return ps.origin[2] + MINS_Z; }
/* a ceiling whose underside is top above the feet */
static void Ceiling(float top) { AddBox(-4000, -4000, Feet() + top, 4000, 4000, Feet() + top + 100); }
static void DropLast(void) { numBrushes--; }

/* Tests 13-15: standing and crouching need room; refusals unless held. */
static void TestRoom(void) {
	/* prone under 69: up to a crouch silently; the next think says stand is blocked */
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoProne(8);
	Ceiling(DEFAULT_HEIGHT - MINS_Z - 1);
	Cmd(8, 0, 0, 0);
	CHECK(StanceEvents() == 0 && (ps.pm_flags & PMF_DUCKED) && ps.viewHeightTarget == BG_EYE_CROUCH);
	Cmd(8, 0, 0, 0);
	CHECK(Raised(EV_STANCE_FORCE_CROUCH, BG_STANCE_PARM_STAND_BLOCKED) == 1 && StanceEvents() == 1);
	/* prone under 49: neither fits, every think; never with a held key */
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoProne(8);
	Ceiling(CROUCH_HEIGHT - MINS_Z - 1);
	Cmd(8, 0, 0, 0); CHECK(Raised(EV_STANCE_FORCE_PRONE, BG_STANCE_PARM_STAND_BLOCKED) == 1 && (ps.pm_flags & PMF_PRONE));
	Cmd(8, 0, 0, 0); CHECK(Raised(EV_STANCE_FORCE_PRONE, BG_STANCE_PARM_STAND_BLOCKED) == 1 && (ps.pm_flags & PMF_PRONE));
	Cmd(8, BUTTON_TEMP_STANCE, 0, 0); CHECK(StanceEvents() == 0 && (ps.pm_flags & PMF_PRONE));
	/* Test 14: a crouch from prone under 49 */
	Cmd(8, BUTTON_CROUCH, 0, 0); CHECK(Raised(EV_STANCE_FORCE_PRONE, BG_STANCE_PARM_CROUCH_BLOCKED) == 1 && (ps.pm_flags & PMF_PRONE));
	Cmd(8, BUTTON_CROUCH, 0, 0); CHECK(Raised(EV_STANCE_FORCE_PRONE, BG_STANCE_PARM_CROUCH_BLOCKED) == 1);
	Cmd(8, BUTTON_CROUCH | BUTTON_TEMP_STANCE, 0, 0); CHECK(StanceEvents() == 0 && (ps.pm_flags & PMF_PRONE));
	/* Test 15: stand from a crouch under 69, every think; a held key says nothing */
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoCrouch(8);
	Ceiling(DEFAULT_HEIGHT - MINS_Z - 1);
	Cmd(8, 0, 0, 0); CHECK(Raised(EV_STANCE_FORCE_CROUCH, BG_STANCE_PARM_STAND_BLOCKED) == 1 && (ps.pm_flags & PMF_DUCKED));
	Cmd(8, 0, 0, 0); CHECK(Raised(EV_STANCE_FORCE_CROUCH, BG_STANCE_PARM_STAND_BLOCKED) == 1);
	Cmd(8, BUTTON_TEMP_STANCE, 0, 0); CHECK(StanceEvents() == 0 && (ps.pm_flags & PMF_DUCKED));
	/* a crouch is never refused, and a ceiling exactly 70 up lets you stand */
	DropLast(); Ceiling(DEFAULT_HEIGHT - MINS_Z + 0.5f);
	Cmd(8, 0, 0, 0); CHECK(StanceEvents() == 0 && !(ps.pm_flags & PMF_DUCKED));
	/* another player on your head does not stop a stand */
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoCrouch(8);
	AddBrush(-PLAYER_WIDTH, -PLAYER_WIDTH, Feet() + CROUCH_HEIGHT - MINS_Z + 1, PLAYER_WIDTH, PLAYER_WIDTH,
		Feet() + CROUCH_HEIGHT - MINS_Z + 71, CONTENTS_BODY)->ent = 1;
	Cmd(8, 0, 0, 0); CHECK(StanceEvents() == 0 && !(ps.pm_flags & PMF_DUCKED) && ps.viewHeightTarget == BG_EYE_STAND);
}

/* a trace log for BG_CheckProne's paths */
typedef struct { vec3_t start, end; float fraction; } traceRec_t;
static traceRec_t traceLog[64];
static void LoggedTrace(trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs,
	const vec3_t end, int pass, int mask) {
	WorldTrace(tr, start, mins, maxs, end, pass, mask);
	if (traceCount <= ARRAY_LEN(traceLog)) {
		traceRec_t *r = &traceLog[traceCount - 1];
		VectorCopy(start, r->start); VectorCopy(end, r->end); r->fraction = tr->fraction;
	}
}
static qboolean Check(qboolean already, qboolean onGround, qboolean walkable, float *torso, float *waist) {
	vec3_t feet = { 0, 0, 0 };
	feet[2] = feetGap;	/* where a player's feet rest on a floor at 0 */
	move.trace = LoggedTrace; traceCount = 0; pm = &move;
	return BG_CheckProne(feet, PLAYER_WIDTH, PRONE_HEIGHT - MINS_Z, 0, torso, waist, already, onGround,
		walkable, BG_PRONE_FEET_DIST);
}
/* a wall across the body's line, its near face d behind the feet, h high */
static void WallBehind(float d, float h) { AddBox(-d - 200, -4000, -1000, -d, 4000, h); }

/* Test 16: MW2's room check, path by path. */
static void TestCheckProne(void) {
	const float th = PRONE_HEIGHT - MINS_Z - BG_PRONE_TRACE_HALF, retryUnder = th * 0.7f + 18;
	float torso, waist;
	trace_t tr;
	Spawn(0, 0, STAND_Z, 0);
	/* flat ground: room, the body flat */
	Floor(); torso = waist = -1;
	CHECK(Check(qtrue, qtrue, qtrue, &torso, &waist) && NEAR(torso, 0, 1e-3) && NEAR(waist, 0, 1e-3));
	CHECK(!Check(qfalse, qtrue, qfalse, NULL, NULL) && traceCount == 1);	/* the box, then the ground's word */
	CHECK(!Check(qtrue, qtrue, qfalse, NULL, NULL) && traceCount == 0);
	CHECK(Check(qtrue, qfalse, qfalse, NULL, NULL));	/* in the air the word means nothing */
	/* a wall closer than size + 2: no, on the legs' first trace */
	Floor(); WallBehind(PLAYER_WIDTH + 1, 200);
	CHECK(!Check(qtrue, qtrue, qtrue, NULL, NULL) && traceCount == 1);
	/* closer than the retry's reach: one more trace, 22 higher; a tall wall still says no */
	Floor(); WallBehind(retryUnder - 4, 200);
	CHECK(!Check(qtrue, qtrue, qtrue, NULL, NULL) && traceCount == 2);
	CHECK(traceLog[1].end[2] == traceLog[0].end[2] + 22 && traceLog[1].fraction < 1);
	/* past it: no retry */
	Floor(); WallBehind(retryUnder + 2, 200);
	Check(qtrue, qtrue, qtrue, NULL, NULL);
	CHECK(traceCount >= 2 && traceLog[1].start[0] != traceLog[0].start[0]);	/* the second is the waist's */
	/* a low step close behind: the retry clears it and the check goes on */
	Floor(); WallBehind(retryUnder - 4, th - 4);
	Check(qtrue, qtrue, qtrue, NULL, NULL);
	CHECK(traceCount > 2 && traceLog[1].end[2] == traceLog[0].end[2] + 22 && traceLog[1].fraction == 1);
	/* no ground under the waist: no on the ground, yes in the air with flat pitches */
	numBrushes = 0; AddBox(-10, -4000, -1000, 4000, 4000, 0);
	CHECK(!Check(qtrue, qtrue, qtrue, NULL, NULL));
	torso = waist = 1;
	CHECK(Check(qtrue, qfalse, qtrue, &torso, &waist) && torso == 0 && waist == 0);
	/* in the air the legs' trace may not hit */
	Floor(); WallBehind(retryUnder + 2, 200);
	CHECK(!Check(qtrue, qfalse, qtrue, NULL, NULL) && traceCount == 1);
	/* a steep plane under the waist: no, through the trace's own walkable,
	 * whatever the caller's word for the ground */
	{
		vec3_t p = { -12, 0, 0 };
		numBrushes = 0; AddBox(-10, -4000, -1000, 4000, 4000, 0);
		AddSlope(-4000, -4000, -1000, -10, 4000, 1000, 1, 0, 0.5f, p);	/* rising behind, 60 degrees */
		CHECK(!Check(qtrue, qtrue, qtrue, NULL, NULL));
		CHECK(!Check(qtrue, qfalse, qtrue, NULL, NULL));
	}
	/* MW2's trace walkable byte */
	memset(&tr, 0, sizeof(tr)); tr.plane.normal[2] = MIN_WALK_NORMAL; CHECK(BG_TraceWalkable(&tr));
	tr.plane.normal[2] = nextafterf(MIN_WALK_NORMAL, 0); CHECK(!BG_TraceWalkable(&tr));
	tr.plane.normal[2] = 1; tr.startsolid = qtrue; CHECK(!BG_TraceWalkable(&tr));
	/* the bend, for a body lying behind (MW2's 0x4218DE, 0x42198D: both
	 * lines run forward, pitch down positive): legs raised are held at the
	 * torso's + PRONE_WAIST_HOLD and pass (a valley at the waist); legs
	 * hanging down fail once the bend passes PRONE_BEND_MAX (a ridge), and
	 * pass short of it */
	{
		vec3_t p0 = { 0, 0, 0 }, p1 = { -18, 0, -18 };
		numBrushes = 0;
		AddSlope(-18, -4000, -1000, 4000, 4000, 1000, -0.7071f, 0, 0.7071f, p0);	/* down 45 behind */
		AddSlope(-4000, -4000, -1000, -18, 4000, 1000, 0.7071f, 0, 0.7071f, p1);	/* then up 45 */
		AddBox(0, -4000, -1000, 4000, 4000, 0);
		torso = waist = 0;
		CHECK(Check(qtrue, qtrue, qtrue, &torso, &waist));
		CHECK(torso < 0 && NEAR(BG_AngleDelta(torso, waist), -PRONE_WAIST_HOLD, 1e-3));	/* held */
	}
	{
		static const float ridges[][2] = { { 10, 40 }, { 20, 40 } };	/* up behind, then down 40: degrees */
		int r, passed = 0, failed = 0;
		for (r = 0; r < ARRAY_LEN(ridges); r++) {
			vec3_t p0 = { 0, 0, 0 }, p1 = { -18, 0, 0 };
			float rise = (float)tan(DEG2RAD(ridges[r][0])), fall = (float)tan(DEG2RAD(ridges[r][1]));
			qboolean ok;
			p1[2] = 18 * rise;
			numBrushes = 0;
			AddSlope(-18, -4000, -1000, 4000, 4000, 1000, rise, 0, 1, p0);	/* up behind */
			AddSlope(-4000, -4000, -1000, -18, 4000, 1000, -fall, 0, 1, p1);	/* then down */
			AddBox(0, -4000, -1000, 4000, 4000, 0);
			torso = waist = 0;
			ok = Check(qtrue, qtrue, qtrue, &torso, &waist);
			CHECK(torso > 0 && waist < 0 && waist < torso + PRONE_WAIST_HOLD);	/* unclamped: the legs hang */
			CHECK(ok == (BG_AngleDelta(torso, waist) <= PRONE_BEND_MAX));
			if (ok) passed++; else failed++;
		}
		CHECK(passed == 1 && failed == 1);	/* the same legs, the torso steeper: one bend each side */
	}
	move.trace = WorldTrace;
}

/* Test 17: prone refused on the ground, and asked in the air. */
static void TestProneRefused(void) {
	int t, landed;
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8);
	WallBehind(PLAYER_WIDTH + 1, 200);
	Cmd(8, BUTTON_PRONE, 0, 0);
	CHECK(Raised(EV_STANCE_FORCE_STAND, BG_STANCE_PARM_PRONE_BLOCKED) == 1 && !(ps.pm_flags & (PMF_PRONE | PMF_DUCKED)));
	Cmd(8, BUTTON_PRONE | BUTTON_TEMP_STANCE, 0, 0);
	CHECK(StanceEvents() == 0 && !(ps.pm_flags & (PMF_PRONE | PMF_DUCKED)) && ps.viewHeightTarget == BG_EYE_STAND);
	GoCrouch(8);
	Cmd(8, BUTTON_PRONE, 0, 0);
	CHECK(Raised(EV_STANCE_FORCE_CROUCH, BG_STANCE_PARM_PRONE_BLOCKED) == 1 && (ps.pm_flags & PMF_DUCKED));
	Cmd(8, BUTTON_PRONE | BUTTON_TEMP_STANCE, 0, 0);
	CHECK(StanceEvents() == 0 && (ps.pm_flags & PMF_DUCKED));
	/* asked in the air: nothing until a think whose stance check sees the
	 * ground, the one after the think whose move lands */
	Floor(); Spawn(0, 0, STAND_Z + 30, 0);
	Cmd(8, 0, 0, 0); CHECK(ps.groundEntityNum == ENTITYNUM_NONE);
	for (t = 0, landed = 0; t < 200 && !landed; t++) {
		Cmd(8, BUTTON_PRONE, 0, 0);
		CHECK(StanceEvents() == 0 && ps.viewHeightTarget == BG_EYE_STAND && !ps.viewHeightLerpTarget);
		landed = ps.groundEntityNum != ENTITYNUM_NONE;
	}
	CHECK(landed);
	Cmd(8, BUTTON_PRONE, 0, 0);
	CHECK(StanceEvents() == 0 && ps.viewHeightTarget == BG_EYE_CROUCH && ps.viewHeightLerpTarget == BG_EYE_CROUCH);
}

/* Test 18: prone onto ground too steep, and off a ledge. */
static void TestProneSteep(void) {
	vec3_t p = { -PLAYER_WIDTH, 0, 0 };
	int t, events;
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoProne(8);
	/* the ground turns to a 60-degree slope rising behind, under the body */
	numBrushes = 0;
	AddSlope(-4000, -4000, -1000, 4000, 4000, 1000, 0.866f, 0, 0.5f, p);
	Cmd(8, BUTTON_PRONE, 0, 0);
	CHECK(ps.groundEntityNum == ENTITYNUM_NONE);
	/* the pitch's air check (the legs hit the slope), then the air move's crouch */
	CHECK(Raised(EV_STANCE_FORCE_CROUCH, BG_STANCE_PARM_PRONE_BLOCKED) == 1);
	CHECK(Raised(EV_STANCE_FORCE_CROUCH, BG_STANCE_PARM_FORCED) == 1);
	CHECK((ps.pm_flags & PMF_DUCKED) && !(ps.pm_flags & PMF_PRONE) && ps.viewHeightTarget == BG_EYE_CROUCH);
	CHECK(move.maxs[2] == CROUCH_HEIGHT);
	/* off a ledge forward, the body behind on the ledge: prone in the air
	 * unless the air checks say no, and then only their two events */
	numBrushes = 0; AddBox(-4000, -4000, -1000, 40, 4000, 0); AddBox(-4000, -4000, -2000, 4000, 4000, -400);
	Spawn(0, 0, STAND_Z, 0); Settle(8); ProneRested(8);
	for (t = 0, events = 0; t < 400 && ps.origin[2] > -300; t++) {
		Cmd(8, BUTTON_PRONE, 127, 0);
		events += StanceEvents();
		CHECK(StanceEvents() == Raised(EV_STANCE_FORCE_CROUCH, BG_STANCE_PARM_PRONE_BLOCKED) +
			Raised(EV_STANCE_FORCE_CROUCH, BG_STANCE_PARM_FORCED));
		if (Raised(EV_STANCE_FORCE_CROUCH, BG_STANCE_PARM_FORCED)) CHECK(ps.pm_flags & PMF_DUCKED);
		if (ps.groundEntityNum != ENTITYNUM_NONE && ps.origin[2] < -300) break;
	}
	printf("  off a ledge prone: %s in the air, %d stance events\n", events ? "crouched" : "stayed prone", events);
}

/* Test 19: crawling back into a wall stops at the legs' reach, undone each
 * think (origin and velocity), still pushing; forward is the box's slide. */
static void TestCrawlIntoWall(void) {
	float x, w = BG_PRONE_FEET_DIST + PLAYER_WIDTH;
	int t;
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); ProneRested(8);
	WallBehind(w, 200);
	for (t = 0; t < 400; t++) Cmd(8, BUTTON_PRONE, -127, 0);
	x = ps.origin[0];
	CHECK(x < 0 && x + MINS_Z > -w);	/* moved back, short of the wall... */
	CHECK(x + w > PLAYER_WIDTH + 2 - 0.5f && x + w < BG_PRONE_FEET_DIST + 1);	/* ...by the legs' reach */
	Cmd(8, BUTTON_PRONE, -127, 0);
	CHECK(ps.origin[0] == x && ps.velocity[0] < 0);	/* undone, pushing */
	/* forward into a wall: the box's ordinary slide */
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); ProneRested(8);
	AddBox(60, -4000, -1000, 400, 4000, 200);
	for (t = 0; t < 600; t++) Cmd(8, BUTTON_PRONE, 127, 0);
	CHECK(ps.origin[0] > 60 - PLAYER_WIDTH - 0.5f && ps.origin[0] < 60 - PLAYER_WIDTH && (ps.pm_flags & PMF_PRONE));
}

/* Test 20: the prone step is 10 and the push down past it 9; a step costs
 * speed by its height over the prone step. */
static void TestProneStep(void) {
	int h, t;
	for (h = STEPSIZE_PRONE; h <= STEPSIZE_PRONE + 1; h++) {
		Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); ProneRested(8);
		AddBox(40, -4000, -1000, 4000, 4000, h);
		for (t = 0; t < 600; t++) Cmd(8, BUTTON_PRONE, 127, 0);
		if (h == STEPSIZE_PRONE) {
			/* up it; the body then bends over the edge until the line 5 up
			 * from the waist to the feet point meets it (MW2's check) */
			CHECK(Feet() > h && ps.origin[0] > 40 - PLAYER_WIDTH);
			printf("  a %d step prone: climbed, the crawl held %.1f past its edge, the waist below\n", h, ps.origin[0] - 40);
		} else CHECK(Feet() < 1 && ps.origin[0] < 40 - PLAYER_WIDTH);
	}
	/* a step of half the prone step keeps 0.6 of the speed */
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); ProneRested(8);
	AddBox(40, -4000, -1000, 4000, 4000, STEPSIZE_PRONE / 2);
	for (t = 0; t < 600; t++) {
		float z = ps.origin[2], v0 = ps.velocity[0], v, keep;
		Cmd(8, BUTTON_PRONE, 127, 0);
		if (ps.origin[2] - z > 1) {
			v = ModelThinkRaw(v0, WalkSpeedNow(127, 0), 0.008f);
			keep = STEP_SPEED_KEEP + (1 - STEP_SPEED_KEEP) * (1 - (ps.origin[2] - z) / STEPSIZE_PRONE);
			CHECK(NEAR(keep, 0.6f, 0.02f) && fabs(ps.velocity[0] - v * keep) <= 0.5f + 1e-3f);
			break;
		}
	}
	CHECK(t < 600);
	/* the push down past a ground move is STEPSIZE_DOWN prone as standing:
	 * a drop within it is walked, one past it is a fall */
	for (h = STEPSIZE_DOWN - 1; h <= STEPSIZE_DOWN + 1; h += 2) {
		int air = 0;
		numBrushes = 0; AddBox(-4000, -4000, -1000, 40, 4000, 0); AddBox(40, -4000, -1000, 4000, 4000, -h);
		Spawn(0, 0, STAND_Z, 0); Settle(8); ProneRested(8);
		for (t = 0; t < 600; t++) { Cmd(8, BUTTON_PRONE, 127, 0); air |= ps.groundEntityNum == ENTITYNUM_NONE; }
		CHECK(Feet() < -h + 1 && air == (h > STEPSIZE_DOWN));
	}
}

/* -------------------------------------------------------------- the view */

/* the mouse: aim the command so the view would be yaw (before any clamp) */
static void Look(float yaw) {
	move.cmd.angles[YAW] = ( ANGLE2SHORT(yaw) - ps.delta_angles[YAW] ) & 65535;
}
static void LookPitch(float pitch) {
	move.cmd.angles[PITCH] = ( ANGLE2SHORT(pitch) - ps.delta_angles[PITCH] ) & 65535;
}
static float ViewFromCmd(int i) { return SHORT2ANGLE((short)(move.cmd.angles[i] + ps.delta_angles[i])); }
static const float quantum = 360.0f / 65536;

/* Test 21: the body follows the view at BG_PRONE_TURN_RATE while the
 * command carries the prone bit; a flick holds at the cap; without the bit,
 * still and under the cap, the body stays. */
static void TestProneTurn(void) {
	int i, t;
	for (i = 0; i < ARRAY_LEN(msecs); i++) {
		int msec = msecs[i];
		float step = (float)(msec * BG_PRONE_TURN_RATE * 0.001), body, view;
		Floor(); Spawn(0, 0, STAND_Z, 0); Settle(msec); GoProne(msec);
		CHECK(ps.proneDirection == 0);
		Look(30); view = ViewFromCmd(YAW);
		for (t = 0; t < 200 && ps.proneDirection != view; t++) {
			body = ps.proneDirection;
			Cmd(msec, BUTTON_PRONE, 0, 0);
			CHECK(ps.proneDirection == (view - body < step ? view : body + step));
			CHECK(ps.viewangles[YAW] == view && StanceEvents() == 0);
		}
		CHECK(ps.proneDirection == view);
		/* a flick past the cap: the view holds at the cap, delta_angles takes the rest */
		Look(view + 150);
		Cmd(msec, BUTTON_PRONE, 0, 0);
		CHECK(NEAR(BG_AngleDelta(ps.viewangles[YAW], ps.proneDirection + BG_PRONE_YAW_CAP), 0, quantum));
		CHECK(ps.viewangles[YAW] == ViewFromCmd(YAW) && StanceEvents() == 0);
		for (t = 0; t < 400 && fabs(BG_AngleDelta(ps.proneDirection, ps.viewangles[YAW])) > step; t++) Cmd(msec, BUTTON_PRONE, 0, 0);
		CHECK(t < 400);
	}
	/* no prone bit (a held crouch refused under a ceiling), still, under the cap */
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoProne(8);
	Ceiling(CROUCH_HEIGHT - MINS_Z - 1);
	Look(40);
	for (t = 0; t < 50; t++) {
		Cmd(8, BUTTON_CROUCH | BUTTON_TEMP_STANCE, 0, 0);
		CHECK(ps.proneDirection == 0 && (ps.pm_flags & PMF_PRONE) && StanceEvents() == 0);
	}
	/* ...but moving, or past the cap less 5, it turns */
	Cmd(8, BUTTON_CROUCH | BUTTON_TEMP_STANCE, 64, 0);
	CHECK(ps.proneDirection > 0);
}

/* lying in a slot just wider than the legs' trace box: no turn has room */
static void Slot(void) {
	Floor();
	AddBox(-4000, BG_PRONE_TRACE_HALF + 1, -1000, -PLAYER_WIDTH - 3, 4000, 200);
	AddBox(-4000, -4000, -1000, -PLAYER_WIDTH - 3, -BG_PRONE_TRACE_HALF - 1, 200);
}

/* walls closing on the legs: no yaw has room for the body */
static void Pinch(void) {
	Floor();
	AddBox(-4000, BG_PRONE_TRACE_HALF - 1, -1000, -PLAYER_WIDTH - 3, 4000, 200);
	AddBox(-4000, -4000, -1000, -PLAYER_WIDTH - 3, -BG_PRONE_TRACE_HALF + 1, 200);
}

/* Test 22 and 50: a turn into a wall stops the body and says so every think
 * it stays blocked; the view is pushed toward the body within the think,
 * delta_angles taking all of it. */
static void TestTurnBlocked(void) {
	int t, n;
	float body;
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoProne(8);
	Slot();
	Cmd(8, BUTTON_PRONE, 0, 0);
	CHECK(StanceEvents() == 0);	/* lying straight in it fits */
	body = ps.proneDirection;
	Look(30);
	for (t = 0, n = 0; t * 8 < 2000; t++) {
		Cmd(8, BUTTON_PRONE, 0, 0);
		n += Raised(EV_STANCE_INVALID, BG_STANCE_PARM_PRONE_BLOCKED);
		CHECK(Raised(EV_STANCE_INVALID, BG_STANCE_PARM_PRONE_BLOCKED) == 1 && StanceEvents() == 1);
		CHECK(fabs(BG_AngleDelta(ps.proneDirection, body)) < 2);
		CHECK(ps.viewangles[YAW] == ViewFromCmd(YAW));	/* delta_angles holds the push */
		CHECK(fabs(BG_AngleDelta(ps.viewangles[YAW], ps.proneDirection)) < 30);
		Look(ps.viewangles[YAW] + 30);	/* keep turning into it */
	}
	CHECK(n == t);
	printf("  a turn held into a wall for %dms: EV_STANCE_INVALID on all %d thinks\n", t * 8, n);
}

/* Test 22a: an unblocked small turn is kept whole; a blocked one's last
 * small turn the same way is undone by BG_PRONE_YAW_UNDO of it. */
static void TestSmallTurns(void) {
	float view, a, b, d1;
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoProne(8);
	Look(20); Cmd(8, BUTTON_PRONE, 0, 0);
	view = ps.viewangles[YAW];
	Look(view + 0.5f); Cmd(8, BUTTON_PRONE, 0, 0);
	CHECK(NEAR(BG_AngleDelta(ps.viewangles[YAW], view), 0.5f, quantum));
	/* blocked: the same think from the same state, run with two old views -
	 * one too far for the undo, one half a degree back toward the turn */
	Slot(); Spawn(0, 0, STAND_Z, 0); GoProne(8);
	Look(25); Cmd(8, BUTTON_PRONE, 0, 0);
	CHECK(Raised(EV_STANCE_INVALID, -1) == 1);
	{
		playerState_t s0; float start;
		Look(ps.viewangles[YAW] + 25);
		move.cmd.buttons = BUTTON_PRONE; move.cmd.forwardmove = move.cmd.rightmove = 0; pm = &move;
		PM_UpdateViewAngles(&ps, &move.cmd); start = ps.viewangles[YAW];
		s0 = ps;
		memset(&pml, 0, sizeof(pml)); pml.msec = 8;
		PM_ProneViewAngles(start + 10); a = ps.viewangles[YAW];
		CHECK(BG_AngleDelta(a, start) != 0 && ps.eventSequence == s0.eventSequence + 1);	/* blocked */
		d1 = BG_AngleDelta(start, a) > 0 ? 0.5f : -0.5f;	/* the last turn, the way this one came */
		ps = s0; PM_ProneViewAngles(a + d1); b = ps.viewangles[YAW];
		CHECK(NEAR(BG_AngleDelta(b, a), BG_PRONE_YAW_UNDO * d1, 2 * quantum));
		CHECK(ps.viewangles[YAW] == ViewFromCmd(YAW));
		ps = s0; PM_ProneViewAngles(a - d1); b = ps.viewangles[YAW];
		CHECK(NEAR(BG_AngleDelta(b, a), 0, quantum));	/* the other way: kept */
		ps = s0; PM_ProneViewAngles(a + 3 * d1); b = ps.viewangles[YAW];
		CHECK(NEAR(BG_AngleDelta(b, a), 0, quantum));	/* more than a degree: kept */
	}
}

/* Test 23: the pitch keeps within BG_PRONE_PITCH_CAP of proneTorsoPitch,
 * which follows the ground at BG_PRONE_PITCH_RATE. */
static void TestPronePitch(void) {
	vec3_t p = { PLAYER_WIDTH, 0, 0 };
	float last, rate = 8 * 0.001f * BG_PRONE_PITCH_RATE, want;
	int t;
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoProne(8);
	CHECK(ps.proneTorsoPitch == 0 && ps.proneDirectionPitch == 0);
	LookPitch(80); Cmd(8, BUTTON_PRONE, 0, 0);
	CHECK(NEAR(ps.viewangles[PITCH], BG_PRONE_PITCH_CAP, quantum) && ps.viewangles[PITCH] == ViewFromCmd(PITCH));
	LookPitch(-80); Cmd(8, BUTTON_PRONE, 0, 0);
	CHECK(NEAR(ps.viewangles[PITCH], -BG_PRONE_PITCH_CAP, quantum));
	LookPitch(0); Cmd(8, BUTTON_PRONE, 0, 0);
	/* the ground under the player tilts to 20 degrees rising ahead */
	p[2] = Feet() - 0.1f;
	numBrushes = 0;
	AddSlope(-4000, -4000, -1000, 4000, 4000, 1000, -(float)sin(DEG2RAD(20)), 0, (float)cos(DEG2RAD(20)), p);
	want = BG_PitchForYawOnNormal(0, brushes[0].n);
	CHECK(want < -19 && want > -21);	/* up ahead: negative, MW2's down-positive pitch */
	last = ps.proneTorsoPitch;
	for (t = 0; t < 100 && ps.proneTorsoPitch != want; t++) {
		Cmd(8, BUTTON_PRONE, 0, 0);
		if (ps.groundEntityNum == ENTITYNUM_NONE) continue;
		CHECK(NEAR(ps.proneTorsoPitch, last - rate, 1e-4) || NEAR(ps.proneTorsoPitch, want, 1e-4));
		last = ps.proneTorsoPitch;
	}
	CHECK(NEAR(ps.proneTorsoPitch, want, 1e-4) && NEAR(ps.proneDirectionPitch, want, 1e-4));
	LookPitch(80); Cmd(8, BUTTON_PRONE, 0, 0);
	CHECK(NEAR(ps.viewangles[PITCH], want + BG_PRONE_PITCH_CAP, quantum));
	LookPitch(-80); Cmd(8, BUTTON_PRONE, 0, 0);
	CHECK(NEAR(ps.viewangles[PITCH], want - BG_PRONE_PITCH_CAP, quantum) || ps.viewangles[PITCH] >= -BG_VIEW_PITCH_UP - quantum);
}

/* Test 24: another view update in the same think, or the freeze, changes
 * nothing of the prone view. */
static void TestViewIdempotent(void) {
	vec3_t v; int delta[3];
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoProne(8);
	Look(150); LookPitch(70);
	Cmd(8, BUTTON_PRONE, 0, 0);
	VectorCopy(ps.viewangles, v); delta[0] = ps.delta_angles[0]; delta[1] = ps.delta_angles[1]; delta[2] = ps.delta_angles[2];
	PM_UpdateViewAngles(&ps, &move.cmd);
	CHECK(VectorCompare(v, ps.viewangles) && delta[YAW] == ps.delta_angles[YAW] && delta[PITCH] == ps.delta_angles[PITCH]);
	ps.pm_type = PM_FREEZE;
	{ float body = ps.proneDirection; Look(10); Cmd(8, BUTTON_PRONE, 0, 0); CHECK(ps.proneDirection == body && StanceEvents() == 0); }
}

/* ------------------------------------------------------------- weapons */

static qboolean Stopped(void) { return pml.forwardmove == 0 && pml.rightmove == 0; }
static qboolean Overridden(void) { return (ps.pm_flags & PMF_PRONEMOVE_OVERRIDDEN) != 0; }
static void Toggle(void) { move.cmd.weapon = BG_ToggleSlotWeapon(&ps, move.cmd.weapon); }

/* Test 26: a burst holds the crawl, a push in the shot does not crawl, the
 * crawl comes back with the gun ready; a pump stops it until a push, which
 * crawls while the pump finishes. */
static void TestCrawlBurst(void) {
	int t, shots = 0;
	Floor(); Spawn(0, 0, STAND_Z, 0); Gun(WP_MACHINEGUN); Settle(8); ProneRested(8);
	Hold(200, 8, BUTTON_PRONE, 64, 0);
	CHECK(!Stopped() && FlatSpeed() > 0);
	for (t = 0; t < 40; t++) {
		Cmd(8, BUTTON_PRONE | BUTTON_ATTACK, t < 20 ? 64 : 127, 0);	/* a push half way */
		shots += Raised(EV_FIRE_WEAPON, -1);
		if (t) CHECK(Stopped() && Overridden() && ps.weaponstate == WEAPON_FIRING);
	}
	CHECK(shots > 2);
	for (t = 0; t < 100 && ps.weaponstate != WEAPON_READY; t++) Cmd(8, BUTTON_PRONE, 127, 0);
	Cmd(8, BUTTON_PRONE, 127, 0);
	CHECK(!Stopped() && !Overridden());	/* no push needed */
	/* the SPAS: the shot, then the pump */
	Floor(); Spawn(0, 0, STAND_Z, 0); Gun(WP_SHOTGUN); Settle(8); ProneRested(8);
	Hold(100, 8, BUTTON_PRONE, 64, 0);
	Cmd(8, BUTTON_PRONE | BUTTON_ATTACK, 64, 0); CHECK(Raised(EV_FIRE_WEAPON, -1) == 1);
	for (t = 0; t < 200 && ps.weaponstate != WEAPON_BOLTING; t++) { Cmd(8, BUTTON_PRONE, 64, 0); CHECK(Stopped()); }
	CHECK(ps.weaponstate == WEAPON_BOLTING);
	Cmd(8, BUTTON_PRONE, 64, 0); CHECK(Stopped() && Overridden());	/* the pump holds it */
	Cmd(8, BUTTON_PRONE, 127, 0); CHECK(!Stopped() && !Overridden() && ps.weaponstate == WEAPON_BOLTING);
	for (t = 0; t < 200 && ps.weaponstate == WEAPON_BOLTING; t++) { Cmd(8, BUTTON_PRONE, 127, 0); CHECK(!Stopped()); }
}

/* Test 27: a magazine reload never stops the crawl; a shell reload taken out
 * of the shot holds the shot's stop through its start until a push. */
static void TestCrawlReload(void) {
	int t;
	Floor(); Spawn(0, 0, STAND_Z, 0); Gun(WP_MACHINEGUN); Settle(8); ProneRested(8);
	ps.ammo[WP_MACHINEGUN] = 1;
	Hold(100, 8, BUTTON_PRONE, 64, 0);
	Cmd(8, BUTTON_PRONE | BUTTON_RELOAD, 64, 0);
	CHECK(ps.weaponstate == WEAPON_RELOADING);
	for (t = 0; t < 400 && ps.weaponstate == WEAPON_RELOADING; t++) { Cmd(8, BUTTON_PRONE, 64, 0); CHECK(!Stopped() && !Overridden()); }
	CHECK(ps.weaponstate == WEAPON_READY);
	Floor(); Spawn(0, 0, STAND_Z, 0); Gun(WP_SHOTGUN); Settle(8); ProneRested(8);
	Hold(100, 8, BUTTON_PRONE, 64, 0);
	Cmd(8, BUTTON_PRONE | BUTTON_ATTACK, 64, 0);
	Cmd(8, BUTTON_PRONE | BUTTON_RELOAD, 64, 0);
	CHECK(ps.weaponstate == WEAPON_RELOADING && ps.weaponAnimSeq == RSEQ_START && BG_WeaponReloadSegLength(WP_SHOTGUN, RSEQ_START) > 0);
	for (t = 0; t < 200 && ps.weaponAnimSeq == RSEQ_START; t++) { Cmd(8, BUTTON_PRONE, 64, 0); if (ps.weaponAnimSeq == RSEQ_START) CHECK(Stopped() && Overridden()); }
	CHECK(ps.weaponAnimSeq == RSEQ_LOOP);
	Cmd(8, BUTTON_PRONE, 64, 0); CHECK(!Stopped() && !Overridden());	/* the loop is quiet */
	/* a push in the start crawls at once */
	Floor(); Spawn(0, 0, STAND_Z, 0); Gun(WP_SHOTGUN); Settle(8); ProneRested(8);
	Hold(100, 8, BUTTON_PRONE, 64, 0);
	Cmd(8, BUTTON_PRONE | BUTTON_ATTACK, 64, 0); Cmd(8, BUTTON_PRONE | BUTTON_RELOAD, 64, 0);
	Cmd(8, BUTTON_PRONE, 64, 0); CHECK(Stopped());
	Cmd(8, BUTTON_PRONE, 127, 0); CHECK(!Stopped() && ps.weaponAnimSeq == RSEQ_START);
}

/* Test 28: the knife and every offhand stop the crawl through the gun's
 * quick raise; a swap does not. */
static void TestCrawlQuickActions(void) {
	const int buttons[] = { BUTTON_MELEE, BUTTON_LETHAL, BUTTON_TACTICAL };
	int i, t;
	for (i = 0; i < ARRAY_LEN(buttons); i++) {
		Floor(); Spawn(0, 0, STAND_Z, 0); Gun(WP_MACHINEGUN); Settle(8); ProneRested(8);
		Hold(100, 8, BUTTON_PRONE, 64, 0);
		Cmd(8, BUTTON_PRONE | buttons[i], 64, 0);
		CHECK(ps.weaponAction & WA_TYPE_MASK);
		for (t = 0; t < 500 && (ps.weaponAction & WA_TYPE_MASK); t++) {
			Cmd(8, BUTTON_PRONE, t < 50 ? 64 : 127, 0);	/* a push meanwhile changes nothing */
			if (ps.weaponAction & WA_TYPE_MASK) CHECK(Stopped() && Overridden());
		}
		CHECK(t < 500 && ps.weapon == WP_MACHINEGUN && ps.weaponstate == WEAPON_READY);
		Cmd(8, BUTTON_PRONE, 127, 0); CHECK(!Stopped() && !Overridden());
	}
	Floor(); Spawn(0, 0, STAND_Z, 0); Gun(WP_MACHINEGUN); Settle(8); ProneRested(8);
	Hold(100, 8, BUTTON_PRONE, 64, 0);
	Toggle();
	for (t = 0; t < 500 && !(ps.weapon == WP_SHOTGUN && ps.weaponstate == WEAPON_READY); t++) {
		Cmd(8, BUTTON_PRONE, 64, 0);
		CHECK(!Stopped());
	}
	CHECK(ps.weapon == WP_SHOTGUN);
}

/* Test 29: aiming prone. */
static void TestProneAim(void) {
	int t;
	Floor(); Spawn(0, 0, STAND_Z, 0); Gun(WP_MACHINEGUN); Settle(8); ProneRested(8);
	/* a fresh aim while crawling aims, and the crawl stops */
	Hold(200, 8, BUTTON_PRONE, 64, 0);
	Cmd(8, BUTTON_PRONE | BUTTON_ADS, 64, 0); CHECK((ps.pm_flags & PMF_ADS) && Overridden());
	Cmd(8, BUTTON_PRONE | BUTTON_ADS, 64, 0); CHECK((ps.pm_flags & PMF_ADS) && Stopped());
	/* a push drops the aim (and tells the client) and crawls; the held aim waits */
	Cmd(8, BUTTON_PRONE | BUTTON_ADS, 127, 0);
	CHECK(Raised(EV_RESET_ADS, -1) == 1 && !(ps.pm_flags & PMF_ADS) && !Stopped());
	for (t = 0; t < 20; t++) { Cmd(8, BUTTON_PRONE | BUTTON_ADS, 127, 0); CHECK(!(ps.pm_flags & PMF_ADS) && !Stopped()); }
	/* still again, the held aim comes back */
	Cmd(8, BUTTON_PRONE | BUTTON_ADS, 0, 0); CHECK(ps.pm_flags & PMF_ADS);
	/* a fresh aim on the second step of a chopped command still aims */
	Floor(); Spawn(0, 0, STAND_Z, 0); Gun(WP_MACHINEGUN); Settle(8); ProneRested(8);
	Hold(200, 8, BUTTON_PRONE, 64, 0);
	move.pmove_fixed = 1; move.pmove_msec = 8;
	Cmd(16, BUTTON_PRONE | BUTTON_ADS, 64, 0);
	CHECK((ps.pm_flags & PMF_ADS) && Overridden());
	Cmd(16, BUTTON_PRONE | BUTTON_ADS, 64, 0);
	CHECK((ps.pm_flags & PMF_ADS) && Stopped());
	move.pmove_fixed = 0;
}

/* Test 30: lying down while moving drops a held aim; lying still keeps it. */
static void TestProneEntryAim(void) {
	int moving;
	for (moving = 0; moving < 2; moving++) {
		Floor(); Spawn(0, 0, STAND_Z, 0); Gun(WP_MACHINEGUN); Settle(8); GoCrouch(8);
		Hold(100, 8, BUTTON_CROUCH | BUTTON_ADS, moving ? 64 : 0, 0);
		CHECK(ps.pm_flags & PMF_ADS);
		Cmd(8, BUTTON_PRONE | BUTTON_ADS, moving ? 64 : 0, 0);	/* the prone leg starts: the entry */
		CHECK(ps.pm_flags & PMF_PRONE);
		CHECK(Raised(EV_RESET_ADS, -1) == moving && !!(ps.pm_flags & PMF_ADS) == !moving);
	}
}

/* Test 31: lying down starts a landing's slowdown in jumpLand, never
 * pm_time, unless knockback's timer runs; a jump soon after standing is
 * lower. */
static void TestProneSlowdown(void) {
	int t;
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoCrouch(8);
	Cmd(8, BUTTON_PRONE, 0, 0);
	CHECK(ps.viewHeightTarget == BG_EYE_PRONE && ps.jumpLand == BG_JUMP_LAND_TIME - 8 && ps.pm_time == 0);
	CHECK(BG_JumpLandFactor(ps.jumpLand) == BG_JumpLandFactor(BG_JUMP_LAND_TIME - 8) && BG_JumpLandFactor(BG_JUMP_LAND_TIME) == 2.5f);
	/* a running knockback: none */
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoCrouch(8);
	ps.pm_time = 100; ps.pm_flags |= PMF_TIME_KNOCKBACK;
	Cmd(8, BUTTON_PRONE, 0, 0);
	CHECK(ps.viewHeightTarget == BG_EYE_PRONE && ps.jumpLand == 0);
	/* up again and a jump while it runs: lower */
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoProne(8);
	for (t = 0; t < 200 && !(ps.viewHeightTarget == BG_EYE_STAND && !(ps.pm_flags & PMF_DUCKED)); t++) Cmd(8, 0, 0, 0);
	CHECK(ps.jumpLand > 0);
	{
		float want = BG_JumpVelocity(&ps) / sqrt(BG_JumpLandFactor(ps.jumpLand - 8));
		move.cmd.upmove = 127; move.cmd.buttons = 0; move.cmd.serverTime = ps.commandTime + 8; Pmove(&move);
		CHECK(ps.groundEntityNum == ENTITYNUM_NONE && ps.velocity[2] < BG_JumpVelocity(&ps));
		CHECK(fabs(ps.velocity[2] - rintf(want - DEFAULT_GRAVITY * 0.008f)) <= 1);
	}
}

/* ----------------------------------------------------------- interplay */

static void CmdUp(int msec, int buttons, int forward, int up) {
	evSeq = ps.eventSequence;
	move.cmd.buttons = buttons; move.cmd.forwardmove = forward; move.cmd.rightmove = 0; move.cmd.upmove = up;
	move.cmd.serverTime = ps.commandTime + msec;
	Pmove(&move);
}

/* Tests 32, 33 and 51: sprint and the stance. */
static void TestSprintStance(void) {
	int t;
	/* out of a crouch with room: standing on the start think, the reset sent */
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoCrouch(8);
	Cmd(8, BUTTON_CROUCH | BUTTON_SPRINT, 127, 0);
	CHECK((ps.pm_flags & PMF_SPRINTING) && !(ps.pm_flags & (PMF_DUCKED | PMF_PRONE)));
	CHECK(move.maxs[2] == DEFAULT_HEIGHT && ps.viewHeightTarget == BG_EYE_STAND);
	CHECK(Raised(EV_STANCE_FORCE_STAND, BG_STANCE_PARM_FORCED) == 1 && StanceEvents() == 1);
	for (t = 0; t < 20; t++) { Cmd(8, BUTTON_SPRINT, 127, 0); CHECK(ps.pm_flags & PMF_SPRINTING); }
	/* a held crouch key still asking: the sprint ends; the crouch waits for
	 * the stand leg the sprint started */
	Cmd(8, BUTTON_CROUCH | BUTTON_TEMP_STANCE, 127, 0);
	CHECK(!(ps.pm_flags & PMF_SPRINTING) && StanceEvents() == 0);
	for (t = 0; t < 100 && !(ps.pm_flags & PMF_DUCKED); t++) Cmd(8, BUTTON_CROUCH | BUTTON_TEMP_STANCE, 127, 0);
	CHECK((ps.pm_flags & PMF_DUCKED) && !(ps.pm_flags & PMF_SPRINTING));
	/* out of prone too */
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoProne(8);
	Cmd(8, BUTTON_PRONE | BUTTON_SPRINT, 127, 0);
	CHECK((ps.pm_flags & PMF_SPRINTING) && !(ps.pm_flags & (PMF_DUCKED | PMF_PRONE)) && move.maxs[2] == DEFAULT_HEIGHT);
	CHECK(Raised(EV_STANCE_FORCE_STAND, BG_STANCE_PARM_FORCED) == 1 && ps.viewHeightTarget == BG_EYE_STAND);
	/* under a ceiling: no sprint, nothing said */
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoCrouch(8);
	Ceiling(DEFAULT_HEIGHT - MINS_Z - 1);
	Cmd(8, BUTTON_CROUCH | BUTTON_SPRINT, 127, 0);
	CHECK(!(ps.pm_flags & PMF_SPRINTING) && (ps.pm_flags & PMF_DUCKED) && StanceEvents() == 0);
	/* Test 33: a crouch or prone bit ends a running sprint and crouches that think */
	for (t = 0; t < 2; t++) {
		int k;
		Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8);
		for (k = 0; k < 5; k++) Cmd(8, BUTTON_SPRINT, 127, 0);
		CHECK(ps.pm_flags & PMF_SPRINTING);
		Cmd(8, t ? BUTTON_PRONE : BUTTON_CROUCH, 127, 0);
		CHECK(!(ps.pm_flags & PMF_SPRINTING) && (ps.pm_flags & PMF_DUCKED) && ps.viewHeightTarget == BG_EYE_CROUCH);
	}
	/* Test 51: crouch and sprint pressed on one command from standing: Peace
	 * stands and sprints, the reset sent (MW2 ends crouched: its sprint is
	 * decided before the move, MW2_DIFFERENCES "When sprint and aim are
	 * decided") */
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8);
	Cmd(8, BUTTON_CROUCH | BUTTON_SPRINT, 127, 0);
	CHECK((ps.pm_flags & PMF_SPRINTING) && !(ps.pm_flags & PMF_DUCKED) && Raised(EV_STANCE_FORCE_STAND, BG_STANCE_PARM_FORCED) == 1);
}

/* Test 34: no jump crouched or prone, the press spent; one on the first think standing. */
static void TestStanceJump(void) {
	int prone;
	for (prone = 0; prone < 2; prone++) {
		Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8);
		if (prone) GoProne(8); else GoCrouch(8);
		CmdUp(8, prone ? BUTTON_PRONE : BUTTON_CROUCH, 0, 127);
		CHECK(ps.groundEntityNum != ENTITYNUM_NONE && (ps.pm_flags & PMF_JUMP_HELD) && Raised(EV_JUMP, -1) == 0);
		CmdUp(8, prone ? BUTTON_PRONE : BUTTON_CROUCH, 0, 127);
		CHECK(ps.groundEntityNum != ENTITYNUM_NONE);
	}
	/* crouched, a fresh press on the command that stands: up and jumping at once */
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoCrouch(8);
	CmdUp(8, 0, 0, 127);
	CHECK(!(ps.pm_flags & PMF_DUCKED) && ps.groundEntityNum == ENTITYNUM_NONE && Raised(EV_JUMP, -1) == 1);
	/* a held crouch or prone key, then the jump held under it (the client
	 * never lets the key cancel it: MW2's 0x400 is its own bit), then the key
	 * let go with the jump still held: spent lowered, and the stand-up under
	 * it is no fresh press (MW2's Jump_Check refuses a jump down on the
	 * command before), through every leg up; a fresh press then jumps */
	for (prone = 0; prone < 2; prone++) {
		int key = (prone ? BUTTON_PRONE : BUTTON_CROUCH) | BUTTON_TEMP_STANCE, t;
		Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8);
		for (t = 0; t < 400 && !(ps.viewHeightTarget == (prone ? BG_EYE_PRONE : BG_EYE_CROUCH) && !ps.viewHeightLerpTarget); t++)
			CmdUp(8, key, 0, prone ? 0 : -127);
		CHECK(ps.pm_flags & (prone ? PMF_PRONE : PMF_DUCKED));
		for (t = 0; t < 10; t++) { CmdUp(8, key, 0, 127); CHECK(ps.groundEntityNum != ENTITYNUM_NONE && Raised(EV_JUMP, -1) == 0); }
		for (t = 0; t < 400 && !(ps.viewHeightTarget == BG_EYE_STAND && !ps.viewHeightLerpTarget); t++) {
			CmdUp(8, 0, 0, 127);
			CHECK(ps.groundEntityNum != ENTITYNUM_NONE && Raised(EV_JUMP, -1) == 0);
		}
		CHECK(!(ps.pm_flags & (PMF_DUCKED | PMF_PRONE)) && Eye() == BG_EYE_STAND);
		CmdUp(8, 0, 0, 127); CHECK(ps.groundEntityNum != ENTITYNUM_NONE && Raised(EV_JUMP, -1) == 0);
		CmdUp(8, 0, 0, 0);
		CmdUp(8, 0, 0, 127); CHECK(ps.groundEntityNum == ENTITYNUM_NONE && Raised(EV_JUMP, -1) == 1);
	}
}

/* a ladder face at x = 40, facing -x */
static void Ladder(void) {
	brush_t *b;
	Floor();
	b = AddBrush(40, -4000, -1000, 80, 4000, 4000, CONTENTS_SOLID);
	b->surfaceFlags = SURF_LADDER;
}
static qboolean OnLadder(void) { return (ps.stats[STAT_LADDER] & LADDER_ATTACHED) != 0; }

/* Tests 35 and 49: no ladder prone; a crouched grab is stood up, and a held
 * crouch on the ladder is told every think. */
static void TestStanceLadder(void) {
	int t, n;
	Ladder(); Spawn(40 - PLAYER_WIDTH - 1, 0, STAND_Z, 0); Settle(8); ProneRested(8);
	for (t = 0; t < 100; t++) { Cmd(8, BUTTON_PRONE, 127, 0); CHECK(!OnLadder()); }
	Ladder(); Spawn(40 - PLAYER_WIDTH - 1, 0, STAND_Z, 0); Settle(8); GoCrouch(8);
	Ladder(); ps.origin[0] = 40 - PLAYER_WIDTH - 1;
	for (t = 0; t < 50 && !OnLadder(); t++) Cmd(8, BUTTON_CROUCH, 127, 0);
	CHECK(OnLadder());
	Cmd(8, BUTTON_CROUCH, 127, 0);
	CHECK(Raised(EV_STANCE_FORCE_STAND, BG_STANCE_PARM_FORCED) == 1 && !(ps.pm_flags & PMF_DUCKED) && ps.viewHeightTarget == BG_EYE_STAND);
	/* +movedown held on the ladder: every think, the held key notwithstanding */
	for (t = 0, n = 0; t < 50 && OnLadder(); t++) {
		Cmd(8, BUTTON_CROUCH | BUTTON_TEMP_STANCE, 127, 0);
		if (OnLadder()) { CHECK(Raised(EV_STANCE_FORCE_STAND, BG_STANCE_PARM_FORCED) == 1); n++; }
	}
	CHECK(n > 10 && !(ps.pm_flags & PMF_DUCKED));
	/* the held crouch still held, a fresh jump (MW2's 0x400, which the
	 * client's DOWN never cancels): the strip has stood the player, and the
	 * jump pushes off the ladder */
	Ladder(); Spawn(40 - PLAYER_WIDTH - 1, 0, STAND_Z, 0); Settle(8); GoCrouch(8);
	Ladder(); ps.origin[0] = 40 - PLAYER_WIDTH - 1;
	for (t = 0; t < 50 && !OnLadder(); t++) Cmd(8, BUTTON_CROUCH | BUTTON_TEMP_STANCE, 127, 0);
	CHECK(OnLadder());
	for (t = 0; t < 10; t++) {
		CmdUp(8, BUTTON_CROUCH | BUTTON_TEMP_STANCE, 127, -127);
		CHECK(OnLadder() && !(ps.pm_flags & PMF_DUCKED) && Raised(EV_JUMP, -1) == 0);
	}
	CmdUp(8, BUTTON_CROUCH | BUTTON_TEMP_STANCE, 127, 127);
	CHECK(!OnLadder() && (ps.stats[STAT_LADDER] & LADDER_JUMP_OFF) && Raised(EV_JUMP, -1) == 1);
	CHECK(Raised(EV_STANCE_FORCE_STAND, BG_STANCE_PARM_FORCED) == 1 && ps.velocity[2] > 0);
}

/* Test 36: the talk strip keeps the stance. */
static void TestTalkKeepsStance(void) {
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8);
	Cmd(8, BUTTON_TALK | BUTTON_CROUCH | BUTTON_ATTACK, 127, 0);
	CHECK((ps.pm_flags & PMF_DUCKED) && ps.viewHeightTarget == BG_EYE_CROUCH && Raised(EV_FIRE_WEAPON, -1) == 0);
}

/* Test 37: PMF_RESPAWNED holds while the trigger or prone is held, and no
 * stance is asked for meanwhile. */
static void TestRespawned(void) {
	Floor(); Spawn(0, 0, STAND_Z, 0);
	ps.pm_flags |= PMF_RESPAWNED;
	Cmd(8, BUTTON_PRONE, 0, 0);
	CHECK((ps.pm_flags & PMF_RESPAWNED) && ps.viewHeightTarget == BG_EYE_STAND && !ps.viewHeightLerpTarget && StanceEvents() == 0);
	Cmd(8, BUTTON_ATTACK | BUTTON_CROUCH, 0, 0);
	CHECK((ps.pm_flags & PMF_RESPAWNED) && ps.viewHeightTarget == BG_EYE_STAND);
	Cmd(8, BUTTON_CROUCH, 0, 0);
	CHECK(!(ps.pm_flags & PMF_RESPAWNED) && ps.viewHeightTarget == BG_EYE_CROUCH);
	Floor(); Spawn(0, 0, STAND_Z, 0); ps.pm_flags |= PMF_RESPAWNED;
	Cmd(8, BUTTON_USE_HOLDABLE, 0, 0);	/* MW2's mask drops Q3's use */
	CHECK(!(ps.pm_flags & PMF_RESPAWNED));
}

/* ------------------------------------------------- revision 2's tests */

/* Test 41: the first leg of stand to prone asks for room every think. */
static void TestFirstLegRecheck(void) {
	int i;
	for (i = 0; i < ARRAY_LEN(msecs); i++) {
		int msec = msecs[i];
		Floor(); Spawn(0, 0, STAND_Z, 0); Settle(msec);
		Cmd(msec, BUTTON_PRONE, 0, 0);
		Cmd(msec, BUTTON_PRONE, 0, 0);
		CHECK(ps.viewHeightLerpTarget == BG_EYE_CROUCH && StanceEvents() == 0);
		WallBehind(PLAYER_WIDTH + 1, 200);
		Cmd(msec, BUTTON_PRONE, 0, 0);
		CHECK(Raised(EV_STANCE_FORCE_CROUCH, BG_STANCE_PARM_PRONE_BLOCKED) == 1 && (ps.pm_flags & PMF_DUCKED));
	}
}

/* Test 42: the air move's crouch mid 40 -> 11: the target goes to 40 mid
 * leg and the next think reverses it. */
static void TestAirFallbackMidLeg(void) {
	int frac, elapsed, msec = 8, i;
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(msec); GoCrouch(msec);
	Cmd(msec, BUTTON_PRONE, 0, 0); for (i = 0; i < 10; i++) Cmd(msec, BUTTON_PRONE, 0, 0);
	CHECK(ps.viewHeightLerpTarget == BG_EYE_PRONE && ps.viewHeightLerpDown);
	/* the floor goes, a wall behind for the legs */
	numBrushes = 0; AddBox(-4000, -4000, -3000, 4000, 4000, -2000); WallBehind(PLAYER_WIDTH + 10, 400);
	Cmd(msec, BUTTON_PRONE, 0, 0);
	CHECK(Raised(EV_STANCE_FORCE_CROUCH, BG_STANCE_PARM_FORCED) == 1 &&
		Raised(EV_STANCE_FORCE_CROUCH, BG_STANCE_PARM_PRONE_BLOCKED) == 1);
	CHECK(ps.viewHeightTarget == BG_EYE_CROUCH && ps.viewHeightLerpTarget == BG_EYE_PRONE);	/* mid leg */
	elapsed = ps.viewHeightLerpTime + msec;
	frac = 100 * elapsed / BG_PRONE_LERP_MS;
	Cmd(msec, BUTTON_PRONE, 0, 0);
	CHECK(ps.viewHeightLerpTarget == BG_EYE_CROUCH && !ps.viewHeightLerpDown);
	CHECK(ps.viewHeightLerpTime == (100 - frac) * BG_PRONE_LERP_MS / 100 && ps.viewHeightLerpTime < BG_PRONE_LERP_MS);
	CHECK(Raised(EV_STANCE_FORCE_CROUCH, BG_STANCE_PARM_FORCED) == 0);
}

/* Test 43: a prone landing is not doubled (Q3's doubling reads only the
 * crouch); a crouched one is. */
static int FallEvent(int stance) {
	int t, ev = 0;
	numBrushes = 0; AddBox(-4000, -4000, -1000, 4000, 4000, -200);
	Spawn(0, 0, STAND_Z, 0);
	if (stance == 2) {	/* prone in the air, the body fitting: no ledge near */
		ps.pm_flags |= PMF_PRONE; ps.viewHeightTarget = BG_EYE_PRONE; ps.viewheight = PRONE_VIEWHEIGHT;
	} else if (stance == 1) {
		ps.pm_flags |= PMF_DUCKED; ps.viewHeightTarget = BG_EYE_CROUCH; ps.viewheight = CROUCH_VIEWHEIGHT;
	}
	for (t = 0; t < 500; t++) {
		Cmd(8, stance == 2 ? BUTTON_PRONE : stance == 1 ? BUTTON_CROUCH : 0, 0, 0);
		if (Raised(EV_FALL_FAR, -1)) ev = EV_FALL_FAR;
		else if (Raised(EV_FALL_MEDIUM, -1)) ev = EV_FALL_MEDIUM;
		else if (Raised(EV_FALL_SHORT, -1)) ev = EV_FALL_SHORT;
		if (ps.groundEntityNum != ENTITYNUM_NONE) break;
		CHECK(stance != 2 || (ps.pm_flags & PMF_PRONE));
	}
	CHECK(ps.groundEntityNum != ENTITYNUM_NONE);
	return ev;
}
static void TestProneFall(void) {
	int stand = FallEvent(0), crouch = FallEvent(1), prone = FallEvent(2);
	CHECK(stand == EV_FALL_SHORT && crouch == EV_FALL_MEDIUM && prone == stand);
}

/* Test 44 and D7: the over-the-box windows, in thinks from the tables, and
 * the hit volume that covers them. */
static void OverBoxWindow(int msec, int fromButtons, int toButtons, int lerpTarget, float boxTop) {
	int t, ms = BG_EyeLegTime(lerpTarget, 1), over = 0, want = 0, first = -1, last = -1;
	float muzzle0 = 0;
	entityState_t es;
	bgHitVolume_t vols[BG_MAX_HIT_VOLUMES];
	vec3_t muzzle, fwd = { 1, 0, 0 };
	for (t = 0; t * msec < ms; t++) if (LegEye(lerpTarget, 1, t * msec) > boxTop - MINS_Z) want++;
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(msec);
	if (fromButtons) GoCrouch(msec);
	for (t = 0; t * msec <= ms + msec; t++) {
		int n, k, hasOver = 0;
		Cmd(msec, toButtons, 0, 0);
		BG_PlayerStateToEntityState(&ps, &es, qtrue);
		n = BG_PlayerHitVolumes(&es, ps.origin, move.mins, move.maxs, vols);
		for (k = 1; k < n; k++) hasOver += vols[k].type == HITVOL_OVER_BOX;
		BG_ShotMuzzle(es.pos.trBase, ps.viewheight, fwd, muzzle);
		CHECK(move.maxs[2] == boxTop);
		CHECK(hasOver == (ps.viewheight > boxTop));
		if (ps.viewheight > boxTop) {
			over++; if (first < 0) first = t; last = t;
			/* the muzzle is the snapped base plus the eye, snapped: at or over the box */
			CHECK(muzzle[2] >= es.pos.trBase[2] + boxTop);
			if (t == 0) muzzle0 = muzzle[2] - es.pos.trBase[2];
		}
	}
	CHECK(over == want && first == 0);
	printf("  %3dms thinks, %s: eye over the %.0f box top on thinks 0..%d (%dms of the %dms leg), muzzle %.0f over the base at first\n",
		msec, lerpTarget == BG_EYE_CROUCH ? "stand to crouch" : "crouch to prone", boxTop - MINS_Z, last, over * msec, ms,
		muzzle0 - MINS_Z);
}
static void TestOverBoxWindows(void) {
	int i;
	for (i = 0; i < ARRAY_LEN(msecs); i++) {
		OverBoxWindow(msecs[i], 0, BUTTON_CROUCH, BG_EYE_CROUCH, CROUCH_HEIGHT);
		OverBoxWindow(msecs[i], BUTTON_CROUCH, BUTTON_PRONE, BG_EYE_PRONE, PRONE_HEIGHT);
	}
}

/* Test 45: the ring's four slots against stance events every think: a shot
 * on a command's first think, then a prone turn blocked for the rest. Only
 * measured: the ring is MW2's and nothing special-cases it. The server reads
 * a command's last MAX_PS_EVENTS (ClientEvents), so a first shot pushed out
 * of them deals no damage. */
static void TestRingBudget(void) {
	const int cmdMs[] = { 8, 16, 33, 132 };
	int fixed, i, m;
	printf("  a command's first shot among its last %d events, blocked prone (fixed msec: command ms)\n", MAX_PS_EVENTS);
	for (fixed = 0; fixed < 2; fixed++) for (m = 0; m < ARRAY_LEN(msecs); m++) {
		printf("   pmove_fixed %d msec %2d:", fixed, msecs[m]);
		for (i = 0; i < ARRAY_LEN(cmdMs); i++) {
			int k, seq, firstShot = -1, kept;
			Floor(); Spawn(0, 0, STAND_Z, 0); Gun(WP_MACHINEGUN); Settle(8); GoProne(8); Pinch();
			Cmd(8, BUTTON_PRONE, 0, 0);
			CHECK(Raised(EV_STANCE_INVALID, BG_STANCE_PARM_PRONE_BLOCKED) == 1);	/* blocked every think */
			move.pmove_fixed = fixed; move.pmove_msec = msecs[m];
			seq = ps.eventSequence;
			Cmd(cmdMs[i], BUTTON_PRONE | BUTTON_ATTACK, 0, 0);
			move.pmove_fixed = 0;
			CHECK(ps.ammo[WP_MACHINEGUN] < BG_WeaponMagSize(WP_MACHINEGUN));
			/* the first think's events: the blocked turn (the view runs first), the shot */
			CHECK(ps.eventSequence - seq >= 2);
			for (k = seq; k < ps.eventSequence && firstShot < 0; k++)
				if (k >= ps.eventSequence - MAX_PS_EVENTS &&
				    (ps.events[k & (MAX_PS_EVENTS - 1)] & ~EV_EVENT_BITS) == EV_FIRE_WEAPON) firstShot = k;
			kept = firstShot == seq + 1;
			if (ps.eventSequence - seq <= MAX_PS_EVENTS) CHECK(kept);
			printf(" %3d:%s", cmdMs[i], kept ? "kept" : "LOST");
		}
		printf("\n");
	}
}

/* Test 46: the prone turn's cost in traces. */
static void TestTurnCost(void) {
	int worst = 0, steady, t;
	float flick;
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8); GoProne(8); Slot();
	Cmd(8, BUTTON_PRONE, 0, 0);
	for (flick = 90; flick < 180; flick += 10) {
		Look(ps.proneDirection + flick);
		traceCount = 0; Cmd(8, BUTTON_PRONE, 0, 0);
		if (traceCount > worst) worst = traceCount;
		Look(ps.proneDirection); for (t = 0; t < 5; t++) Cmd(8, BUTTON_PRONE, 0, 0);
	}
	Look(ps.proneDirection + 30);
	for (t = 0; t < 10; t++) Cmd(8, BUTTON_PRONE, 0, 0);
	traceCount = 0; Cmd(8, BUTTON_PRONE, 0, 0); steady = traceCount;
	printf("  prone turn cost: a flick into a wall %d traces a think (%d a second at 125fps), "
		"a turn held blocked %d (%d a second)\n", worst, worst * 125, steady, steady * 125);
	CHECK(worst <= 1500 && steady <= 25);	/* the bounds set from the first run (1199, 17) */
}

/* Test 48: water from the standing eye in every stance. */
static void TestWater(void) {
	const float depths[] = { 6, 12, 25, 35, 65 };
	int d, stance, levels[3];
	for (d = 0; d < ARRAY_LEN(depths); d++) {
		for (stance = 0; stance < 3; stance++) {
			Floor(); AddBrush(-4000, -4000, 0, 4000, 4000, depths[d], CONTENTS_WATER);
			Spawn(0, 0, STAND_Z, 0); Settle(8);
			if (stance == 1) GoCrouch(8); else if (stance == 2) GoProne(8);
			Cmd(8, stance == 2 ? BUTTON_PRONE : stance == 1 ? BUTTON_CROUCH : 0, 0, 0);
			levels[stance] = move.waterlevel;
		}
		CHECK(levels[0] == levels[1] && levels[1] == levels[2]);
		CHECK(levels[0] == (depths[d] > BG_WATER_SAMPLE_EYE + feetGap ? 3 : depths[d] > BG_WATER_SAMPLE_EYE / 2 + feetGap ? 2 :
			depths[d] > 1 + feetGap ? 1 : 0));
	}
	/* 6 deep: prone crawls at the prone scale, never swims; 12 deep never drowns */
	Floor(); AddBrush(-4000, -4000, 0, 4000, 4000, 6, CONTENTS_WATER);
	Spawn(0, 0, STAND_Z, 0); Settle(8); ProneRested(8);
	CHECK(NearSpeed(Crawl(2000, 8, 0, 127, 0), BG_PLAYER_SPEED * BG_PRONE_SPEED_SCALE * BG_MoveClassScale(&ps)) ||
		FlatSpeed() < BG_PLAYER_SPEED * BG_PRONE_SPEED_SCALE);
	CHECK(move.waterlevel == 1);
	Floor(); AddBrush(-4000, -4000, 0, 4000, 4000, 12, CONTENTS_WATER);
	Spawn(0, 0, STAND_Z, 0); Settle(8); GoProne(8);
	CHECK(move.waterlevel < 3);
}

/* Every eye-based reach reads ps.viewheight: lying down, a gun on the floor
 * beyond a standing player's use radius is in reach, and the shot leaves
 * from the prone eye. */
static void TestReaches(void) {
	entityState_t item;
	vec3_t fwd = { 1, 0, 0 }, muzzle, base;
	float d;
	Floor(); Spawn(0, 0, STAND_Z, 0); Settle(8);
	memset(&item, 0, sizeof(item));
	item.eType = ET_ITEM; item.modelindex = BG_FindItemForWeapon(WP_RAILGUN) - bg_itemlist;
	/* on the floor, just out of the standing eye's reach */
	d = (float)sqrt(BG_PICKUP_RANGE * BG_PICKUP_RANGE - (ps.viewheight + feetGap) * (ps.viewheight + feetGap)) + 1;
	VectorSet(item.pos.trBase, d, 0, Feet());
	CHECK(BG_WeaponPickupDistance(&ps, &item, 0) < 0);
	GoProne(8);
	CHECK(BG_WeaponPickupDistance(&ps, &item, 0) >= 0);
	VectorCopy(ps.origin, base); SnapVector(base);
	BG_ShotMuzzle(base, ps.viewheight, fwd, muzzle);
	CHECK(muzzle[2] == (int)(base[2] + PRONE_VIEWHEIGHT));
}

int main(void) {
	TestTables();
	TestEyeCurve();
	TestStandProneStand();
	TestRequestWaits();
	TestReversal();
	TestDeathEye();
	TestSpawn();
	TestStanceScale();
	TestCrawlSpeeds();
	TestCrawlAccel();
	TestChoppedCommand();
	TestProneBob();
	TestRoom();
	TestCheckProne();
	TestProneRefused();
	TestProneSteep();
	TestCrawlIntoWall();
	TestProneStep();
	TestProneTurn();
	TestTurnBlocked();
	TestSmallTurns();
	TestPronePitch();
	TestViewIdempotent();
	TestCrawlBurst();
	TestCrawlReload();
	TestCrawlQuickActions();
	TestProneAim();
	TestProneEntryAim();
	TestProneSlowdown();
	TestSprintStance();
	TestStanceJump();
	TestStanceLadder();
	TestTalkKeepsStance();
	TestRespawned();
	TestFirstLegRecheck();
	TestAirFallbackMidLeg();
	TestProneFall();
	TestOverBoxWindows();
	TestRingBudget();
	TestTurnCost();
	TestWater();
	TestReaches();
	printf("PASS: %d stance checks\n", checks);
	return 0;
}
