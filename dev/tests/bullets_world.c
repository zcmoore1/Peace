/* The client-side bullets test's world: see bullets_world.h. */
#include "../../code/qcommon/q_shared.h"
#include "bullets_world.h"

void BW_Sweep( trace_t *t, const vec3_t s, const vec3_t e, const vec3_t mins, const vec3_t maxs,
	const vec3_t lo, const vec3_t hi, int num, int contents, int surfaceFlags ) {
	static const vec3_t zero = { 0, 0, 0 };
	float enter = -1, leave = 1;
	vec3_t normal = { 0, 0, 0 }, a, b;
	int k;
	qboolean inside = qtrue;

	if ( !mins ) mins = zero;
	if ( !maxs ) maxs = zero;
	for ( k = 0; k < 3; k++ ) {
		a[k] = lo[k] - maxs[k]; b[k] = hi[k] - mins[k];
		if ( !( s[k] > a[k] && s[k] < b[k] ) ) inside = qfalse;
	}
	if ( inside ) {
		if ( t->fraction > 0 || !t->startsolid ) {
			t->startsolid = qtrue; t->fraction = 0; t->entityNum = num; t->contents = contents;
			t->surfaceFlags = surfaceFlags; VectorClear( t->plane.normal );
			t->allsolid = e[0] > a[0] && e[0] < b[0] && e[1] > a[1] && e[1] < b[1] && e[2] > a[2] && e[2] < b[2];
		}
		return;
	}
	for ( k = 0; k < 3; k++ ) {
		float d = e[k] - s[k], t0, t1, side = -1;
		if ( d == 0 ) { if ( s[k] <= a[k] || s[k] >= b[k] ) return; continue; }
		t0 = ( a[k] - s[k] ) / d; t1 = ( b[k] - s[k] ) / d;
		if ( t0 > t1 ) { float x = t0; t0 = t1; t1 = x; side = 1; }
		if ( t0 > enter ) { enter = t0; VectorClear( normal ); normal[k] = side; }
		if ( t1 < leave ) leave = t1;
		if ( enter >= leave ) return;
	}
	if ( enter < 0 || enter > 1 ) return;
	if ( enter < t->fraction ) {
		t->fraction = enter; VectorCopy( normal, t->plane.normal ); t->entityNum = num;
		t->contents = contents; t->surfaceFlags = surfaceFlags;
	}
}

void BW_TraceWorld( trace_t *t, const vec3_t start, const vec3_t mins, const vec3_t maxs,
	const vec3_t end, int mask ) {
	static const vec3_t floorLo = { -100000, -100000, -100000 }, floorHi = { 100000, 100000, 0 };
	static const vec3_t wallLo = { BW_WALL_X, -4000, -100 }, wallHi = { BW_WALL_X + 16, 4000, 1000 };
	static const vec3_t skyLo = { -4000, -4000, BW_SKY_Z }, skyHi = { 4000, 4000, BW_SKY_Z + 16 };

	memset( t, 0, sizeof( *t ) );
	t->fraction = 1;
	t->entityNum = ENTITYNUM_NONE;
	if ( mask & ( CONTENTS_SOLID | CONTENTS_PLAYERCLIP ) ) {
		BW_Sweep( t, start, end, mins, maxs, floorLo, floorHi, ENTITYNUM_WORLD, CONTENTS_SOLID, 0 );
		BW_Sweep( t, start, end, mins, maxs, wallLo, wallHi, ENTITYNUM_WORLD, CONTENTS_SOLID, 0 );
		BW_Sweep( t, start, end, mins, maxs, skyLo, skyHi, ENTITYNUM_WORLD, CONTENTS_SOLID, SURF_NOIMPACT );
	}
}

void BW_Finish( trace_t *t, const vec3_t start, const vec3_t end ) {
	int i;
	for ( i = 0; i < 3; i++ ) t->endpos[i] = start[i] + t->fraction * ( end[i] - start[i] );
}

int BW_PointContents( const vec3_t p ) {
	if ( p[2] < 0 ) return CONTENTS_SOLID;
	if ( p[0] > BW_WALL_X && p[0] < BW_WALL_X + 16 ) return CONTENTS_SOLID;
	if ( p[2] > BW_SKY_Z && p[2] < BW_SKY_Z + 16 ) return CONTENTS_SOLID;
	return 0;
}
