/* The one world both halves of the client-side bullets test trace (the
 * server's trap_Trace in bullets_server.c, the client's trap_CM_* in
 * bullets_client.c): a floor at z = 0, a wall across +x, and a patch of sky
 * (SURF_NOIMPACT) overhead. Only q_shared.h types cross between the halves. */
#ifndef BULLETS_WORLD_H
#define BULLETS_WORLD_H

#define BW_WALL_X		600		/* the wall's near face */
#define BW_SKY_Z		400		/* the sky patch's underside */

/* a box (mins/maxs, may be NULL) swept from s to e against [lo, hi] */
void BW_Sweep( trace_t *t, const vec3_t s, const vec3_t e, const vec3_t mins, const vec3_t maxs,
	const vec3_t lo, const vec3_t hi, int num, int contents, int surfaceFlags );
/* the world alone: a fresh trace, ENTITYNUM_WORLD on a hit */
void BW_TraceWorld( trace_t *t, const vec3_t start, const vec3_t mins, const vec3_t maxs,
	const vec3_t end, int mask );
void BW_Finish( trace_t *t, const vec3_t start, const vec3_t end );
int BW_PointContents( const vec3_t p );

/* What the two halves record, for the test to compare */
#define BW_MAX_MARKS	256
typedef struct {
	vec3_t	point;
	int		entityNum;		/* what it ended in (ENTITYNUM_WORLD for a wall) */
	int		flesh;
} bwMark_t;

#endif
