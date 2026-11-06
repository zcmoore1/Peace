/*
===========================================================================
MW2's minimap radar and the UAV's pings.

The minimap sits top left and turns with the view (compassRotation): the player
at its centre facing up, compassMaxRange (BG_COMPASS_MAX_RANGE) from the centre
to its edge, anything farther held at the edge (compassClampIcons). Teammates
the snapshot carries are drawn on it always. While a UAV covers this player the
server sends a sweep of enemy positions every compassRadarUpdateTime
(compassFastRadarUpdateTime with two or more up); each ping shows where that
enemy was at the sweep and fades over compassRadarPingFadeTime. Picture only:
nothing here is gameplay.
===========================================================================
*/
#include "cg_local.h"

#define RADAR_X        8
#define RADAR_Y        8
#define RADAR_SIZE     120
#define RADAR_DOT      5

/*
==============
CG_ParseUAV

"uav <interval> [x y yaw]...": a sweep's pings; "uav 0": no radar any more.
==============
*/
void CG_ParseUAV( void ) {
	int i, n;
	cg.radarInterval = atoi( CG_Argv( 1 ) );
	if ( cg.radarInterval <= 0 ) {
		cg.radarSweepTime = 0;
		cg.radarPingCount = 0;
		return;
	}
	n = ( trap_Argc() - 2 ) / 3;
	if ( n > ARRAY_LEN( cg.radarPings ) ) n = ARRAY_LEN( cg.radarPings );
	for ( i = 0; i < n; i++ ) {
		cg.radarPings[i][0] = atof( CG_Argv( 2 + i * 3 ) );
		cg.radarPings[i][1] = atof( CG_Argv( 3 + i * 3 ) );
		cg.radarPings[i][2] = atof( CG_Argv( 4 + i * 3 ) );
	}
	cg.radarPingCount = n;
	cg.radarSweepTime = cg.time;
}

// Where a world position falls on the minimap, the view's yaw pointing up.
// Returns qfalse if it had to be held at the edge.
static qboolean CG_RadarPoint( const vec3_t origin, float viewYaw, float *x, float *y ) {
	float dx = origin[0] - cg.refdef.vieworg[0], dy = origin[1] - cg.refdef.vieworg[1];
	float yaw = DEG2RAD( viewYaw ), half = RADAR_SIZE * 0.5f - RADAR_DOT;
	float forward = dx * cos( yaw ) + dy * sin( yaw );
	float right = dx * sin( yaw ) - dy * cos( yaw );
	float scale = half / BG_COMPASS_MAX_RANGE, big;
	float px = right * scale, py = -forward * scale;
	qboolean inside = qtrue;
	big = fabs( px ) > fabs( py ) ? fabs( px ) : fabs( py );
	if ( big > half ) {
		px *= half / big;
		py *= half / big;
		inside = qfalse;
	}
	*x = RADAR_X + RADAR_SIZE * 0.5f + px;
	*y = RADAR_Y + RADAR_SIZE * 0.5f + py;
	return inside;
}

static void CG_RadarDot( float x, float y, float size, const float *color ) {
	CG_FillRect( x - size * 0.5f, y - size * 0.5f, size, size, color );
}

/*
==============
CG_DrawRadar
==============
*/
void CG_DrawRadar( void ) {
	static const float back[4] = { 0, 0, 0, 0.45f };
	static const float edge[4] = { 0.6f, 0.7f, 0.6f, 0.6f };
	static const float self[4] = { 1, 1, 1, 0.95f };
	static const float friendly[4] = { 0.3f, 0.95f, 0.3f, 0.9f };
	float enemy[4] = { 1, 0.15f, 0.1f, 1 };
	const playerState_t *ps = &cg.predictedPlayerState;
	float viewYaw = cg.refdefViewAngles[YAW], x, y, cx, cy;
	int i;

	if ( !cg_drawStatus.integer ) {
		return;
	}
	CG_FillRect( RADAR_X, RADAR_Y, RADAR_SIZE, RADAR_SIZE, back );
	CG_DrawRect( RADAR_X, RADAR_Y, RADAR_SIZE, RADAR_SIZE, 1, edge );
	cx = RADAR_X + RADAR_SIZE * 0.5f;
	cy = RADAR_Y + RADAR_SIZE * 0.5f;

	// teammates the snapshot carries
	if ( cgs.gametype >= GT_TEAM ) {
		for ( i = 0; i < MAX_CLIENTS; i++ ) {
			centity_t *cent = &cg_entities[i];
			if ( i == cg.snap->ps.clientNum || !cent->currentValid || !cgs.clientinfo[i].infoValid ) continue;
			if ( cgs.clientinfo[i].team != ps->persistant[PERS_TEAM] ) continue;
			if ( cent->currentState.eFlags & EF_DEAD ) continue;
			CG_RadarPoint( cent->lerpOrigin, viewYaw, &x, &y );
			CG_RadarDot( x, y, RADAR_DOT, friendly );
		}
	}

	// the UAV's pings, each fading from its sweep
	if ( cg.radarSweepTime && cg.radarPingCount ) {
		float age = (float)( cg.time - cg.radarSweepTime );
		enemy[3] = 1.0f - age / BG_RADAR_PING_FADE_TIME;
		if ( enemy[3] > 0 ) {
			for ( i = 0; i < cg.radarPingCount; i++ ) {
				vec3_t at;
				VectorSet( at, cg.radarPings[i][0], cg.radarPings[i][1], 0 );
				CG_RadarPoint( at, viewYaw, &x, &y );
				CG_RadarDot( x, y, RADAR_DOT + 1, enemy );
			}
		}
	}

	// the player, facing up
	CG_RadarDot( cx, cy, RADAR_DOT, self );
	CG_FillRect( cx - 1, cy - RADAR_DOT - 3, 2, 4, self );

	if ( cg.radarSweepTime && cg.time < cg.radarSweepTime + cg.radarInterval + 1000 ) {
		CG_DrawSmallStringColor( RADAR_X + 4, RADAR_Y + 4, cg.radarInterval < BG_RADAR_UPDATE_TIME ? "UAV x2" : "UAV",
			(float *)friendly );
	}

	// an earned UAV waiting to be called in, and the key that does it
	if ( ps->ammo[WP_KILLSTREAK_UAV] > 0 ) {
		char name[64], label[32], text[64], device[16];
		qboolean gamepad;
		trap_Cvar_VariableStringBuffer( "cl_gamepadInUse", device, sizeof( device ) );
		gamepad = atoi( device ) != 0;
		label[0] = '\0';
		if ( trap_Key_GetBindingName( "killstreak", gamepad, name, sizeof( name ) ) >= 0 ||
		     trap_Key_GetBindingName( "killstreak", !gamepad, name, sizeof( name ) ) >= 0 ) {
			CG_ReadableKeyName( name, label, sizeof( label ) );
			Com_sprintf( text, sizeof( text ), "UAV x%i [%s]", ps->ammo[WP_KILLSTREAK_UAV], label );
		} else {
			Com_sprintf( text, sizeof( text ), "UAV x%i: bind killstreak", ps->ammo[WP_KILLSTREAK_UAV] );
		}
		CG_DrawSmallStringColor( RADAR_X, RADAR_Y + RADAR_SIZE + 4, text, colorYellow );
	}
}
