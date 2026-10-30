/* The real Pmove (sprint, ADS, PM_Weapon) over a flat floor. Only the world
 * trace is stubbed. Its own TU: Q3 headers intentionally have no guards. */
#include "../../code/game/bg_pmove.c"
static void FloorTrace(trace_t *tr, const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, int pass, int mask) {
	float lo = mins ? mins[2] : 0, sz = start[2] + lo, ez = end[2] + lo, f;
	(void)maxs; (void)pass; (void)mask;
	memset(tr, 0, sizeof(*tr)); tr->fraction = 1; VectorCopy(end, tr->endpos); tr->entityNum = ENTITYNUM_NONE;
	if (ez < 0 && sz >= 0) {
		f = sz / (sz - ez) - 0.0001f; if (f < 0) f = 0;
		tr->fraction = f;
		tr->endpos[0] = start[0] + f * (end[0] - start[0]);
		tr->endpos[1] = start[1] + f * (end[1] - start[1]);
		tr->endpos[2] = start[2] + f * (end[2] - start[2]);
		tr->plane.normal[2] = 1; tr->entityNum = ENTITYNUM_WORLD;
	}
}
static int NoContents(const vec3_t p, int pass) { (void)p; (void)pass; return 0; }
/* The engine's Q_SnapVector rounds to nearest, as MW2's does; q_shared's macro truncates. */
void trap_SnapVector(float *v) { v[0] = rintf(v[0]); v[1] = rintf(v[1]); v[2] = rintf(v[2]); }
void FakeReloadModel(pmove_t *move) {
	move->trace = FloorTrace; move->pointcontents = NoContents; move->tracemask = MASK_PLAYERSOLID;
	move->pmove_msec = 8;
}
void FakeReloadPmove(pmove_t *move) { FakeReloadModel(move); Pmove(move); }
