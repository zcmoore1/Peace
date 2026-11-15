/* MW2's stance in the real cgame (dev/STANCE.md): the respawn reset in
 * the real CG_PredictPlayerState (cg_predict.c, over the real Pmove), the
 * unpredicted eye's lerp (CG_InterpolatePlayerState), and MW2's
 * invalid-command hint through the real arm (CG_SetInvalidCmdHint, cg_event.c)
 * and draw (CG_DrawInvalidCmdHint, cg_draw.c). Only the engine traps and the
 * cgame outside these files are stubbed.
 *
 * - Respawn (MW2 0x5901AF): while the predicted playerstate has
 *   PMF_RESPAWNED, every prediction pass sends _peaceSetStance 0 (MW2's
 *   CL_SetStance(STAND)), when it ran a command and when nothing moved; once
 *   prediction clears the flag, nothing; never in a demo, the killcam or a
 *   followed view, which do not predict.
 * - The unpredicted eye: a demo, the killcam or a followed view lerps
 *   viewheight between the two snapshots, as the origin, except across a
 *   teleport.
 * - The hint (0x587230, 0x58DB10): shown CG_INVALID_HINT_MS, its alpha a
 *   sawtooth of CG_INVALID_HINT_BLINK_MS from its stamp; an event on every
 *   think never restamps it while it is set, the draw clears it, and the
 *   first event after that arms it afresh, so a continuous block blinks; a
 *   menu or a spectated client hides it (the draw does not run, so it is not
 *   cleared and not restamped), the scoreboard does not. */
#include "../../code/cgame/cg_local.h"

cg_t cg;
cgs_t cgs;
centity_t cg_entities[MAX_GENTITIES];
vmCvar_t cg_showmiss, cg_nopredict, cg_synchronousClients, cg_errorDecay, cg_predictItems, pmove_fixed, pmove_msec;

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define NEAR(a,b,e) CHECK(fabs((double)(a)-(double)(b)) < (e))

void QDECL CG_Printf( const char *fmt, ... ) { (void)fmt; }
void QDECL CG_Error( const char *fmt, ... ) { va_list a; va_start( a, fmt ); vfprintf( stderr, fmt, a ); va_end( a ); exit( 1 ); }
void QDECL Com_Printf( const char *fmt, ... ) { (void)fmt; }
void QDECL Com_Error( int level, const char *fmt, ... ) {
	va_list a; (void)level; va_start( a, fmt ); vfprintf( stderr, fmt, a ); va_end( a ); exit( 1 );
}

/* ---- the console: every command cgame sends, counted ---- */
static int stanceResets, otherCommands;
void trap_SendConsoleCommand( const char *text ) {
	char expect[64];
	Com_sprintf( expect, sizeof( expect ), "_peaceSetStance %i\n", BG_STANCE_STAND );
	if ( !strcmp( text, expect ) ) stanceResets++; else otherCommands++;
}

/* ---- the client's commands ---- */
static usercmd_t cmds[CMD_BACKUP];
static int cmdNumber;
int trap_GetCurrentCmdNumber( void ) { return cmdNumber; }
qboolean trap_GetUserCmd( int n, usercmd_t *cmd ) { *cmd = cmds[n & CMD_MASK]; return n <= cmdNumber && n > cmdNumber - CMD_BACKUP; }
static void Command( int serverTime, int buttons ) {
	usercmd_t *c = &cmds[++cmdNumber & CMD_MASK];
	memset( c, 0, sizeof( *c ) ); c->serverTime = serverTime; c->buttons = buttons;
}
void trap_Cvar_Set( const char *name, const char *value ) { (void)name; (void)value; }
void trap_Cvar_Update( vmCvar_t *v ) { (void)v; }

/* ---- the world: a floor at z 0, nothing else ---- */
void trap_CM_BoxTrace( trace_t *t, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs,
	clipHandle_t model, int mask ) {
	float lo = mins ? mins[2] : 0, sz = start[2] + lo, ez = end[2] + lo, f;
	(void)maxs; (void)model; (void)mask;
	memset( t, 0, sizeof( *t ) ); t->fraction = 1; VectorCopy( end, t->endpos );
	if ( ez < 0 && sz >= 0 ) {
		f = sz / ( sz - ez ) - 0.0001f; if ( f < 0 ) f = 0;
		t->fraction = f; t->plane.normal[2] = 1;
		t->endpos[0] = start[0] + f * ( end[0] - start[0] ); t->endpos[1] = start[1] + f * ( end[1] - start[1] );
		t->endpos[2] = start[2] + f * ( end[2] - start[2] );
	}
}
void trap_CM_TransformedBoxTrace( trace_t *t, const vec3_t start, const vec3_t end, const vec3_t mins,
	const vec3_t maxs, clipHandle_t model, int mask, const vec3_t origin, const vec3_t angles ) {
	(void)start; (void)mins; (void)maxs; (void)model; (void)mask; (void)origin; (void)angles;
	memset( t, 0, sizeof( *t ) ); t->fraction = 1; VectorCopy( end, t->endpos );
}
clipHandle_t trap_CM_TempBoxModel( const vec3_t mins, const vec3_t maxs ) { (void)mins; (void)maxs; return 0; }
clipHandle_t trap_CM_InlineModel( int index ) { (void)index; return 0; }
int trap_CM_PointContents( const vec3_t p, clipHandle_t model ) { (void)p; (void)model; return 0; }
int trap_CM_TransformedPointContents( const vec3_t p, clipHandle_t model, const vec3_t origin, const vec3_t angles ) {
	(void)p; (void)model; (void)origin; (void)angles; return 0;
}
void trap_SnapVector( float *v ) { v[0] = rintf( v[0] ); v[1] = rintf( v[1] ); v[2] = rintf( v[2] ); }

/* ---- the rest of the cgame ---- */
static qboolean killcam;
qboolean CG_Killcam_Active( void ) { return killcam; }
static int transitions;
void CG_TransitionPlayerState( playerState_t *ps, playerState_t *ops ) { (void)ps; (void)ops; transitions++; }
void CG_RecordShots( const playerState_t *before, const playerState_t *after ) { (void)before; (void)after; }
void CG_AdjustPositionForMover( const vec3_t in, int moverNum, int fromTime, int toTime, vec3_t out,
	vec3_t angles_in, vec3_t angles_out ) {
	(void)moverNum; (void)fromTime; (void)toTime; VectorCopy( in, out ); VectorCopy( angles_in, angles_out );
}
void CG_PlayerHitVolumesTrace( trace_t *tr, const vec3_t start, const vec3_t end, int entityNum,
	const entityState_t *es, const vec3_t origin, const vec3_t mins, const vec3_t maxs ) {
	(void)tr; (void)start; (void)end; (void)entityNum; (void)es; (void)origin; (void)mins; (void)maxs; abort();
}
void CG_WeapPicture_Reset( void ) { }
void CG_WeapPicture_BeginReplay( const playerState_t *base ) { (void)base; }
void CG_WeapPicture_ReplayStep( const playerState_t *before, const playerState_t *after, const entityState_t *pickup ) {
	(void)before; (void)after; (void)pickup;
}
void CG_WeapPicture_Update( const playerState_t *predicted, const pmove_t *model, int firstCmd, int lastCmd,
	qboolean (*readCmd)( int cmdNumber, usercmd_t *cmd ) ) {
	(void)predicted; (void)model; (void)firstCmd; (void)lastCmd; (void)readCmd;
}

/* ---- the 2D draw: what the hint draws ---- */
static int catcher, drawnCount;
static char drawnText[64];
static float drawnAlpha;
static int drawnX, drawnY;
int trap_Key_GetCatcher( void ) { return catcher; }
int CG_DrawStrlen( const char *str ) { return (int)strlen( str ); }
void CG_DrawStringExt( int x, int y, const char *string, const float *setColor, qboolean forceColor, qboolean shadow,
	int charWidth, int charHeight, int maxChars ) {
	(void)forceColor; (void)maxChars;
	drawnCount++; Q_strncpyz( drawnText, string, sizeof( drawnText ) ); drawnAlpha = setColor[3];
	drawnX = x; drawnY = y;
	CHECK( setColor[0] == 1 && setColor[1] == 1 && setColor[2] == 1 && shadow );	/* white, MW2's textstyle 3 */
	CHECK( charWidth == SMALLCHAR_WIDTH && charHeight == SMALLCHAR_HEIGHT );
}

/* ---- a player on the floor, as a snapshot gives them ---- */
static snapshot_t snaps[2];
static void Live( int flags ) {
	playerState_t *ps = &snaps[0].ps;
	memset( &cg, 0, sizeof( cg ) ); memset( snaps, 0, sizeof( snaps ) ); memset( cmds, 0, sizeof( cmds ) );
	cmdNumber = 0; stanceResets = otherCommands = transitions = 0; killcam = qfalse; catcher = 0;
	cg.time = 20000; cg.clientNum = 3;
	ps->clientNum = 3; ps->pm_type = PM_NORMAL; ps->stats[STAT_HEALTH] = 100; ps->commandTime = cg.time;
	ps->origin[2] = -MINS_Z; ps->groundEntityNum = ENTITYNUM_WORLD; ps->gravity = DEFAULT_GRAVITY;
	ps->speed = BG_PLAYER_SPEED; ps->weapon = WP_MACHINEGUN; ps->weaponstate = WEAPON_READY; ps->weaponAnimTime = -1;
	BG_SpawnStance( ps );
	ps->pm_flags |= flags;
	snaps[0].serverTime = cg.time;
	cg.snap = &snaps[0];
	pmove_msec.integer = 8; cg_errorDecay.value = 100;
}
static void Predict( int msec, qboolean command, int buttons ) {
	cg.oldTime = cg.time; cg.time += msec;
	if ( command ) Command( cg.time, buttons );
	CG_PredictPlayerState();
}

/* MW2's respawn reset: every pass while the predicted state is RESPAWNED. */
static void TestRespawnReset( void ) {
	int k, view;
	Live( PMF_RESPAWNED );
	/* ATTACK held keeps the flag (in Q3's rule and MW2's) */
	for ( k = 1; k <= 10; k++ ) {
		Predict( 8, qtrue, BUTTON_ATTACK );
		CHECK( ( cg.predictedPlayerState.pm_flags & PMF_RESPAWNED ) && stanceResets == k );
	}
	/* the server has answered every command, and a frame makes no new one:
	 * nothing moved, and it is still sent */
	snaps[0].ps = cg.predictedPlayerState; snaps[0].serverTime = cg.time;
	Predict( 4, qfalse, 0 ); CHECK( stanceResets == 11 && transitions == 10 );
	Predict( 4, qfalse, 0 ); CHECK( stanceResets == 12 && transitions == 10 );
	/* the first command with nothing held clears it in prediction: no more */
	Predict( 8, qtrue, 0 );
	CHECK( !( cg.predictedPlayerState.pm_flags & PMF_RESPAWNED ) && stanceResets == 12 );
	for ( k = 0; k < 5; k++ ) { Predict( 8, qtrue, 0 ); Predict( 4, qfalse, 0 ); }
	CHECK( stanceResets == 12 && !otherCommands );
	/* a demo, the killcam, a followed player: no prediction, no reset */
	for ( view = 0; view < 3; view++ ) {
		Live( PMF_RESPAWNED | ( view == 2 ? PMF_FOLLOW : 0 ) );
		cg.demoPlayback = view == 0; killcam = view == 1;
		if ( view == 2 ) snaps[0].ps.clientNum = cg.clientNum + 1;
		for ( k = 0; k < 5; k++ ) Predict( 8, qtrue, BUTTON_ATTACK );
		CHECK( !stanceResets && !otherCommands && ( cg.predictedPlayerState.pm_flags & PMF_RESPAWNED ) );
	}
}

/* An unpredicted view lerps the eye between snapshots, as it lerps the origin. */
static void TestInterpolatedEye( void ) {
	static const float eyes[][2] = { { DEFAULT_VIEWHEIGHT, CROUCH_VIEWHEIGHT }, { CROUCH_VIEWHEIGHT, PRONE_VIEWHEIGHT },
		{ PRONE_VIEWHEIGHT, CROUCH_VIEWHEIGHT + 0.375f }, { DEFAULT_VIEWHEIGHT, DEAD_VIEWHEIGHT } };
	int i, t;
	for ( i = 0; i < ARRAY_LEN( eyes ); i++ ) {
		Live( 0 ); cg.demoPlayback = qtrue;
		snaps[0].ps.viewheight = eyes[i][0];
		snaps[1] = snaps[0]; snaps[1].serverTime = snaps[0].serverTime + 50; snaps[1].ps.viewheight = eyes[i][1];
		snaps[1].ps.origin[0] = snaps[0].ps.origin[0] + 10;
		cg.nextSnap = &snaps[1];
		for ( t = 0; t <= 50; t += 5 ) {
			float f = t / 50.0f;
			cg.time = snaps[0].serverTime + t;
			CG_PredictPlayerState();
			NEAR( cg.predictedPlayerState.viewheight, eyes[i][0] + f * ( eyes[i][1] - eyes[i][0] ), 1e-4 );
			NEAR( cg.predictedPlayerState.origin[0], snaps[0].ps.origin[0] + f * 10, 1e-4 );	/* with the origin */
		}
		/* across a teleport the next snapshot is not lerped to */
		cg.nextFrameTeleport = qtrue; cg.time = snaps[0].serverTime + 25;
		CG_PredictPlayerState();
		CHECK( cg.predictedPlayerState.viewheight == eyes[i][0] );
		CHECK( !stanceResets );
	}
}

/* MW2's hint, through the real arm and draw, in cgame's frame order: the
 * frame's events (prediction) first, then the 2D draw. */
#define FRAME 8	/* one 8 ms think, and one frame, per event */
static int Draw( void ) { drawnCount = 0; drawnAlpha = -1; CG_DrawInvalidCmdHint(); return drawnCount; }
static float Sawtooth( int t, int stamp ) {
	return (float)( ( t - stamp ) % CG_INVALID_HINT_BLINK_MS ) / CG_INVALID_HINT_BLINK_MS;
}
static void TestHint( void ) {
	static const struct { int parm; const char *text; } types[] = {
		{ BG_STANCE_PARM_STAND_BLOCKED, "Can't Stand Here" }, { BG_STANCE_PARM_CROUCH_BLOCKED, "Can't Crouch Here" },
		{ BG_STANCE_PARM_PRONE_BLOCKED, "Prone Blocked" } };
	int i, t, start, stamp, stamps, maxAlpha, ramps;
	float top;

	/* each type, once: shown CG_INVALID_HINT_MS, blinking, then cleared by the draw */
	for ( i = 0; i < ARRAY_LEN( types ); i++ ) {
		Live( 0 ); start = cg.time;
		CG_SetInvalidCmdHint( WP_MACHINEGUN, types[i].parm );
		CHECK( cg.invalidCmdHintType == types[i].parm + 2 && cg.invalidCmdHintTime == start );	/* MW2's types 3..5 */
		for ( t = start; t <= start + CG_INVALID_HINT_MS + 2 * FRAME; t += FRAME ) {
			cg.time = t;
			if ( t - start <= CG_INVALID_HINT_MS ) {
				CHECK( Draw() == 1 && !strcmp( drawnText, types[i].text ) );
				NEAR( drawnAlpha, Sawtooth( t, start ), 1e-6 );
				CHECK( drawnY == CG_INVALID_HINT_Y && drawnX == SCREEN_WIDTH / 2 - (int)strlen( types[i].text ) * SMALLCHAR_WIDTH / 2 );
			} else {
				CHECK( Draw() == 0 && cg.invalidCmdHintType == CG_INVALID_HINT_NONE );
			}
		}
		/* the weapon is stored; parms with no hint arm nothing */
		CHECK( cg.invalidCmdHintWeapon == WP_MACHINEGUN );
		CG_SetInvalidCmdHint( WP_SHOTGUN, BG_STANCE_PARM_FORCED ); CHECK( Draw() == 0 && cg.invalidCmdHintWeapon == WP_SHOTGUN );
	}

	/* a block held for 3000 ms raises the event on every 8 ms think: the
	 * stamp holds while the hint is set, the draw clears it after
	 * CG_INVALID_HINT_MS, and the next event arms it again. Every ramp of
	 * every showing reaches near 1: it blinks, never stuck at 0. */
	Live( 0 ); start = cg.time; stamp = -1; stamps = 0; maxAlpha = 0; ramps = 0; top = 0;
	for ( t = start; t <= start + 3000; t += FRAME ) {
		int before = cg.invalidCmdHintType;
		cg.time = t;
		CG_SetInvalidCmdHint( WP_MACHINEGUN, BG_STANCE_PARM_PRONE_BLOCKED );
		if ( !before ) {	/* cleared by the last draw (or never set): this event arms it */
			CHECK( cg.invalidCmdHintTime == t );
			/* cleared by the first draw past its time, armed by the next frame's event */
			if ( stamp >= 0 ) CHECK( t == stamp + ( CG_INVALID_HINT_MS / FRAME + 1 ) * FRAME + FRAME );
			stamp = t; stamps++;
		} else {
			CHECK( cg.invalidCmdHintTime == stamp );	/* never restamped while set */
		}
		if ( Draw() ) {
			NEAR( drawnAlpha, Sawtooth( t, stamp ), 1e-6 );
			if ( drawnAlpha < top ) { ramps++; CHECK( top > 1.0f - (float)FRAME / CG_INVALID_HINT_BLINK_MS - 1e-4f ); }
			top = drawnAlpha;
			if ( drawnAlpha > 0.9f ) maxAlpha++;
		} else {
			CHECK( t > stamp + CG_INVALID_HINT_MS && cg.invalidCmdHintType == CG_INVALID_HINT_NONE );
			top = 0;
		}
	}
	CHECK( stamps == 1 + 3000 / ( ( CG_INVALID_HINT_MS / FRAME + 2 ) * FRAME ) && maxAlpha > 0 && ramps >= 4 );
	printf( "a 3000 ms prone block: armed %d times, %d blinks drawn\n", stamps, ramps + stamps );

	/* hidden by a menu or a spectated client, not by the scoreboard. While
	 * hidden the draw does not run: past its time the type stays set, so a
	 * same-type event cannot restamp it; once shown again the draw clears it
	 * and the next event arms it. */
	for ( i = 0; i < 4; i++ ) {
		Live( 0 ); start = cg.time;
		CG_SetInvalidCmdHint( WP_MACHINEGUN, BG_STANCE_PARM_PRONE_BLOCKED );
		cg.showScores = qtrue; cg.scoreBoardShowing = qtrue;
		CHECK( Draw() == 1 );
		if ( i == 0 ) catcher = KEYCATCH_UI;
		if ( i == 1 ) snaps[0].ps.pm_flags |= PMF_FOLLOW;
		if ( i == 2 ) snaps[0].ps.persistant[PERS_TEAM] = TEAM_SPECTATOR;
		if ( i == 3 ) catcher = KEYCATCH_CONSOLE;	/* the console is not a menu: shown */
		cg.time = start + CG_INVALID_HINT_MS * 2;
		CG_SetInvalidCmdHint( WP_MACHINEGUN, BG_STANCE_PARM_PRONE_BLOCKED );
		if ( i == 3 ) {
			CHECK( Draw() == 0 && cg.invalidCmdHintType == CG_INVALID_HINT_NONE );
			continue;
		}
		CHECK( Draw() == 0 && cg.invalidCmdHintType == CG_INVALID_HINT_PRONE_BLOCKED && cg.invalidCmdHintTime == start );
		catcher = 0; snaps[0].ps.pm_flags &= ~PMF_FOLLOW; snaps[0].ps.persistant[PERS_TEAM] = TEAM_FREE;
		CHECK( Draw() == 0 && cg.invalidCmdHintType == CG_INVALID_HINT_NONE );
		cg.time += FRAME;
		CG_SetInvalidCmdHint( WP_MACHINEGUN, BG_STANCE_PARM_PRONE_BLOCKED );
		CHECK( cg.invalidCmdHintTime == cg.time && Draw() == 1 && drawnAlpha == 0 );
	}
}

int main( void ) {
	TestRespawnReset();
	TestInterpolatedEye();
	TestHint();
	printf( "PASS: MW2's stance in the real cgame: respawn reset, unpredicted eye, invalid-command hint (%d checks)\n", checks );
	return 0;
}
