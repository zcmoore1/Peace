/* MW2's stance end to end (dev/STANCE.md): client -> Pmove -> cgame -> client.
 * Each 8 ms frame, as the engine's: the frame's key events and binds, then
 * the queued console commands (cgame's from the frame before) at the real
 * Cbuf_Execute, the real command builder (stance_flow_client.c); the real
 * CG_PredictPlayerState over that command from the server's last state, its
 * events through the real CG_EntityEvent and its respawn reset, the hint's
 * real draw (stance_flow_cgame.c); and the server's real Pmove over the same
 * command, whose state is the next frame's snapshot. The world is a floor and
 * the boxes a test puts up.
 *
 * Checked: a sprint from a crouch toggle stands, and FORCE_STAND resets the
 * toggle so the next command carries no crouch and the sprint lives (a held
 * +movedown ends it instead); a stand-up under a low ceiling bounces the
 * toggle back to crouch (FORCE_CROUCH 1); a +stance hold refused prone by a
 * wall behind bounces back to crouch (FORCE_CROUCH 3) with no hint; a hold
 * across death fires while dead, and at the respawn the reset stands the
 * toggle before PMF_RESPAWNED lets go, so the player spawns standing; a prone
 * turn held into a wall raises EV_STANCE_INVALID every blocked think and
 * "Prone Blocked" blinks for as long as it lasts; +stance pressed while
 * sprinting crouches and ends the sprint, and its hold reaches prone; a held
 * +movedown never cancels the jump, as a held +prone does not: +gostand under
 * either is spent crouched or prone, letting go of the key under the held
 * jump stands with no jump until a fresh press, and on a ladder the held
 * crouch is stripped and +gostand pushes off. (Others drawing a drop shot
 * from the shooter's entity is run-bullets.sh's TestDropShot.) */
#include "../../code/qcommon/q_shared.h"
#include "../../code/game/bg_public.h"
#include "../../code/client/keycodes.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* the client (stance_flow_client.c) */
void FlowClientInit( void );
void FlowClientBegin( int msec );
void FlowKey( int key, qboolean down );
void FlowClientCommand( const char *text );
void FlowClientEnd( usercmd_t *cmd, int serverTime );
void FlowSetSnapType( int pmType );
void FlowTurn( float yaw );
int FlowStance( void );
qboolean FlowStanceHeld( void );
int FlowUnknownCommands( void );
int Cvar_VariableIntegerValue( const char *name );	/* cvar.c's: cl_stanceHoldTime */
/* the cgame (stance_flow_cgame.c) */
void FlowCgameInit( const playerState_t *ps, int time );
void FlowCgameFrame( const playerState_t *serverPs, int serverTime, int time );
const playerState_t *FlowPredicted( void );
int FlowSentCommands( void );
int FlowHintType( void );
int FlowHintStamp( void );
int FlowHintDrawn( float *alpha, const char **text );
int FlowHintMs( void );	/* cg_local.h's CG_INVALID_HINT_MS */
int FlowHintProneType( void );	/* and CG_INVALID_HINT_PRONE_BLOCKED */

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
void QDECL Com_Printf( const char *fmt, ... ) { (void)fmt; }
void QDECL Com_DPrintf( const char *fmt, ... ) { (void)fmt; }
void QDECL Com_Error( int code, const char *fmt, ... ) {
	va_list a; (void)code; va_start( a, fmt ); vfprintf( stderr, fmt, a ); va_end( a ); exit( 1 );
}
/* the engine's Q_SnapVector rounds to nearest, as MW2's does */
void trap_SnapVector( float *v ) { v[0] = rintf( v[0] ); v[1] = rintf( v[1] ); v[2] = rintf( v[2] ); }

/* ---- the world: a floor at the feet (z 0) and up to four boxes, each with
 * its surface flags (a ladder's SURF_LADDER) ---- */
static vec3_t boxLo[4], boxHi[4];
static int boxSurface[4], boxes;
static void Sweep( trace_t *t, const vec3_t s, const vec3_t e, const vec3_t mins, const vec3_t maxs,
	const vec3_t lo, const vec3_t hi, int surfaceFlags ) {
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
		t->startsolid = qtrue; t->fraction = 0; t->entityNum = ENTITYNUM_WORLD; VectorClear( t->plane.normal );
		if ( e[0] > a[0] && e[0] < b[0] && e[1] > a[1] && e[1] < b[1] && e[2] > a[2] && e[2] < b[2] ) t->allsolid = qtrue;
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
	enter -= 0.125f / ( Distance( s, e ) > 1 ? Distance( s, e ) : 1 );	/* the engine's SURFACE_CLIP_EPSILON */
	if ( enter < 0 ) enter = 0;
	if ( enter < t->fraction ) {
		t->fraction = enter; VectorCopy( normal, t->plane.normal ); t->entityNum = ENTITYNUM_WORLD;
		t->surfaceFlags = surfaceFlags;
	}
}
void FlowWorldTrace( trace_t *t, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int mask ) {
	static const vec3_t floorLo = { -100000, -100000, -1000 }, floorHi = { 100000, 100000, 0 };
	int i;
	memset( t, 0, sizeof( *t ) ); t->fraction = 1; t->entityNum = ENTITYNUM_NONE;
	if ( mask & ( CONTENTS_SOLID | CONTENTS_PLAYERCLIP ) ) {
		Sweep( t, start, end, mins, maxs, floorLo, floorHi, 0 );
		for ( i = 0; i < boxes; i++ ) Sweep( t, start, end, mins, maxs, boxLo[i], boxHi[i], boxSurface[i] );
	}
	for ( i = 0; i < 3; i++ ) t->endpos[i] = start[i] + t->fraction * ( end[i] - start[i] );
}
static void ServerTrace( trace_t *t, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
	int pass, int mask ) {
	(void)pass; FlowWorldTrace( t, start, mins, maxs, end, mask );
}
static int NoContents( const vec3_t p, int pass ) { (void)p; (void)pass; return 0; }
static void Box( float x0, float y0, float z0, float x1, float y1, float z1 ) {
	VectorSet( boxLo[boxes], x0, y0, z0 ); VectorSet( boxHi[boxes], x1, y1, z1 ); boxSurface[boxes] = 0; boxes++;
}

/* ---- the server's player, and the frame ---- */
#define FRAME 8
static playerState_t sv;
static usercmd_t cmd;
static int now, events[EV_STANCE_INVALID + 1][4], invalidThinks, jumps;
static void Spawn( void ) {
	bg_class_t loadout = *BG_Class( 0 );
	int count = sv.persistant[PERS_SPAWN_COUNT];
	memset( &sv, 0, sizeof( sv ) );
	CHECK( BG_ValidateClass( &loadout ) );
	BG_ApplyClass( &sv, &loadout );
	sv.persistant[PERS_SPAWN_COUNT] = count + 1;
	sv.stats[STAT_HEALTH] = sv.stats[STAT_MAX_HEALTH] = 100; sv.pm_type = PM_NORMAL;
	sv.origin[2] = -MINS_Z; sv.groundEntityNum = ENTITYNUM_WORLD; sv.gravity = DEFAULT_GRAVITY; sv.speed = BG_PLAYER_SPEED;
	sv.weaponstate = WEAPON_READY; sv.weaponAnimTime = -1; sv.commandTime = now;
	sv.pm_flags = PMF_RESPAWNED;	/* ClientSpawn's, beside its spawn eye */
	BG_SpawnStance( &sv );
}
static void Start( void ) {
	boxes = 0; now = 100000; jumps = 0;
	memset( &sv, 0, sizeof( sv ) );
	memset( events, 0, sizeof( events ) );
	FlowClientInit();
	Spawn();
	FlowCgameInit( &sv, now );
}
/* the stance events pmove raised on the server this command, by parm */
static void CountEvents( const playerState_t *before ) {
	int i, invalid = 0;
	for ( i = before->eventSequence; i < sv.eventSequence; i++ ) {
		int ev = sv.events[i & ( MAX_PS_EVENTS - 1 )], parm = sv.eventParms[i & ( MAX_PS_EVENTS - 1 )];
		if ( ev >= EV_STANCE_FORCE_STAND && ev <= EV_STANCE_INVALID && parm >= 0 && parm < 4 ) events[ev][parm]++;
		if ( ev == EV_STANCE_INVALID ) invalid++;
		if ( ev == EV_JUMP ) jumps++;
	}
	if ( invalid ) invalidThinks++;
}
/* one frame: the caller has queued its events between Begin and Frame */
static void Begin( void ) { now += FRAME; FlowClientBegin( FRAME ); }
static int Frame( void ) {
	playerState_t before = sv;
	pmove_t pm;
	FlowSetSnapType( sv.pm_type );	/* the client's cl.snap, for UP's fly */
	FlowClientEnd( &cmd, now );
	FlowCgameFrame( &sv, sv.commandTime, now );
	memset( &pm, 0, sizeof( pm ) );
	pm.ps = &sv; pm.cmd = cmd; pm.trace = ServerTrace; pm.pointcontents = NoContents;
	pm.tracemask = sv.pm_type == PM_DEAD ? MASK_PLAYERSOLID & ~CONTENTS_BODY : MASK_PLAYERSOLID; pm.pmove_msec = FRAME;
	Pmove( &pm );
	CountEvents( &before );
	/* the prediction ran the same command from the same state */
	CHECK( FlowPredicted()->commandTime == sv.commandTime && FlowPredicted()->pm_flags == sv.pm_flags &&
		FlowPredicted()->viewheight == sv.viewheight );
	return cmd.buttons & BUTTON_STANCE_MASK;
}
static int Next( void ) { Begin(); return Frame(); }
/* the eye at a stance's end, no leg running */
static qboolean Settled( float eye ) { return sv.viewheight == eye && !sv.viewHeightLerpTarget; }
static int Settle( float eye ) {
	int t;
	for ( t = 0; t < 4 * BG_PRONE_LERP_MS && !Settled( eye ); t += FRAME ) Next();
	return Settled( eye );
}

static void Settle0( void ) { int i; for ( i = 0; i < 10; i++ ) Next(); }	/* a spawned player's first commands */

/* 1: sprint from a crouch toggle. */
static void TestSprintFromCrouch( void ) {
	int held, k;
	for ( held = 0; held < 2; held++ ) {
		Start(); Settle0();
		Begin(); FlowClientCommand( held ? "+movedown 99 0\n" : "togglecrouch\n" );
		CHECK( Frame() == ( held ? BUTTON_CROUCH | BUTTON_TEMP_STANCE : BUTTON_CROUCH ) );
		CHECK( Settle( CROUCH_VIEWHEIGHT ) && ( sv.pm_flags & PMF_DUCKED ) );
		Begin(); FlowKey( K_PAD0_LEFTSTICK_UP, qtrue ); Frame();
		Begin(); FlowKey( K_PAD0_LEFTSTICK_CLICK, qtrue ); Frame();
		/* the sprint's command still carries the crouch; pmove stands it up to sprint */
		CHECK( ( cmd.buttons & BUTTON_SPRINT ) && ( cmd.buttons & BUTTON_CROUCH ) );
		CHECK( ( sv.pm_flags & PMF_SPRINTING ) && !( sv.pm_flags & ( PMF_DUCKED | PMF_PRONE ) ) );
		CHECK( events[EV_STANCE_FORCE_STAND][BG_STANCE_PARM_FORCED] == 1 );
		Begin(); FlowKey( K_PAD0_LEFTSTICK_CLICK, qfalse );
		if ( !held ) {
			/* FORCE_STAND reset the toggle: the next command has no crouch, and the sprint lives */
			CHECK( Frame() == 0 && FlowStance() == BG_STANCE_STAND );
			for ( k = 0; k < 20; k++ ) CHECK( Next() == 0 && ( sv.pm_flags & PMF_SPRINTING ) );
		} else {
			/* a held key refuses the reset: the crouch level ends the sprint on
			 * the next command, and the player crouches once the stand leg the
			 * sprint started has run out (MW2 moves the stance's target only
			 * between legs): on that think the crouch leg starts from the
			 * standing eye */
			CHECK( Frame() == ( BUTTON_CROUCH | BUTTON_TEMP_STANCE ) );
			CHECK( !( sv.pm_flags & ( PMF_SPRINTING | PMF_DUCKED ) ) && sv.viewHeightLerpTarget == BG_EYE_STAND );
			for ( k = 0; k < 2 * BG_PRONE_LERP_MS / FRAME && !( sv.pm_flags & PMF_DUCKED ); k++ ) {
				CHECK( Next() == ( BUTTON_CROUCH | BUTTON_TEMP_STANCE ) );
			}
			CHECK( !( sv.pm_flags & PMF_SPRINTING ) && ( sv.pm_flags & PMF_DUCKED ) );
			CHECK( sv.viewHeightLerpTarget == BG_EYE_CROUCH && sv.viewheight == DEFAULT_VIEWHEIGHT );
			Begin(); FlowClientCommand( "-movedown 99 0\n" ); Frame();
		}
		Begin(); FlowKey( K_PAD0_LEFTSTICK_UP, qfalse ); Frame();
	}
}

/* 2: a stand-up under a 69 ceiling: FORCE_CROUCH 1 puts the toggle back to
 * crouch and the commands carry the crouch again. */
static void TestStandBlocked( void ) {
	int k;
	Start(); Settle0();
	Begin(); FlowClientCommand( "togglecrouch\n" ); Frame();
	CHECK( Settle( CROUCH_VIEWHEIGHT ) );
	Box( -100, -100, DEFAULT_HEIGHT - MINS_Z - 1, 100, 100, 200 );	/* MW2's 69: under a standing box */
	Begin(); FlowClientCommand( "togglecrouch\n" ); CHECK( Frame() == 0 );
	CHECK( ( sv.pm_flags & PMF_DUCKED ) && events[EV_STANCE_FORCE_CROUCH][BG_STANCE_PARM_STAND_BLOCKED] == 1 );
	CHECK( Next() == BUTTON_CROUCH && FlowStance() == BG_STANCE_CROUCH );
	for ( k = 0; k < 10; k++ ) CHECK( Next() == BUTTON_CROUCH && ( sv.pm_flags & PMF_DUCKED ) );
	CHECK( events[EV_STANCE_FORCE_CROUCH][BG_STANCE_PARM_STAND_BLOCKED] == 1 && !FlowHintType() );
}

/* 3: a +stance hold from standing, prone refused by a wall behind: the hold
 * crouches, its prone is refused (FORCE_CROUCH 3, from the crouch), the toggle
 * goes back to crouch, and no hint shows (MW2's hint is for EV_STANCE_INVALID). */
static void TestProneBlocked( void ) {
	int t, bounced = 0;
	Start(); Settle0();
	Box( -( BG_PRONE_FEET_DIST * 0.6f ) - 5, -100, 0, -( BG_PRONE_FEET_DIST * 0.6f ), 100, 200 );	/* 30 behind, tall: MW2's retry 22 up fails too */
	Begin(); FlowKey( K_PAD0_RIGHTSTICK_CLICK, qtrue ); CHECK( Frame() == BUTTON_CROUCH );
	for ( t = FRAME; t < 2 * BG_PRONE_LERP_MS; t += FRAME ) {
		int bits = Next();
		if ( bits == BUTTON_PRONE ) bounced++;	/* the hold's own command, once */
		else CHECK( bits == BUTTON_CROUCH );
	}
	CHECK( events[EV_STANCE_FORCE_CROUCH][BG_STANCE_PARM_PRONE_BLOCKED] > 0 );
	CHECK( bounced == 1 && events[EV_STANCE_FORCE_CROUCH][BG_STANCE_PARM_PRONE_BLOCKED] == 1 );
	CHECK( FlowStance() == BG_STANCE_CROUCH && !FlowStanceHeld() && !( sv.pm_flags & PMF_PRONE ) && ( sv.pm_flags & PMF_DUCKED ) );
	CHECK( !FlowHintType() );
	Begin(); FlowKey( K_PAD0_RIGHTSTICK_CLICK, qfalse ); CHECK( Frame() == BUTTON_CROUCH );
}

/* 4: +stance held from standing 100 ms before death and on past the respawn.
 * The hold fires while dead (the client never reads the playerstate for it);
 * at the respawn PMF_RESPAWNED holds while the commands carry PRONE, cgame's
 * reset stands the toggle, and the player spawns standing. */
static void TestDeathRespawn( void ) {
	int t, firedDead = 0, resets = 0;
	Start(); Settle0();
	Begin(); FlowKey( K_PAD0_RIGHTSTICK_CLICK, qtrue ); Frame();
	for ( t = FRAME; t < 100; t += FRAME ) Next();
	sv.pm_type = PM_DEAD; sv.stats[STAT_HEALTH] = 0;	/* player_die */
	for ( t = 0; t < 3 * BG_PRONE_LERP_MS; t += FRAME ) if ( Next() == BUTTON_PRONE ) firedDead++;
	CHECK( firedDead > 0 && !FlowStanceHeld() && FlowStance() == BG_STANCE_PRONE );	/* fired while dead */
	CHECK( !( sv.pm_flags & PMF_PRONE ) );	/* a corpse never goes prone */
	Spawn();	/* ClientSpawn: standing, PMF_RESPAWNED; the key still down */
	{
		int bits[20];
		qboolean kept;
		for ( t = 0; t < 20; t++ ) {
			bits[t] = Next();
			if ( sv.pm_flags & PMF_RESPAWNED ) resets++;
			CHECK( !( sv.pm_flags & ( PMF_PRONE | PMF_DUCKED ) ) && sv.viewheight == DEFAULT_VIEWHEIGHT );
		}
		/* MW2's PMF_RESPAWNED holds while a command carries PRONE (pmove's half) */
		kept = bits[0] == BUTTON_PRONE && resets > 0;
		CHECK( kept );
		/* the first command carried PRONE and kept the flag; cgame's reset made the next one stand */
		CHECK( resets == 1 && FlowStance() == BG_STANCE_STAND && !( sv.pm_flags & PMF_RESPAWNED ) );
		for ( t = 1; t < 20; t++ ) CHECK( bits[t] == 0 );
	}
	Begin(); FlowKey( K_PAD0_RIGHTSTICK_CLICK, qfalse ); CHECK( Frame() == 0 );
}

/* 5: a prone turn held into a wall for more than the hint's time. Prone
 * between two walls just wider than the body behind it, the stick turns the
 * view a degree a frame: once the body can turn no further, every think is
 * blocked and raises EV_STANCE_INVALID 3; the hint arms on the first, keeps
 * its stamp while shown, and is armed again by the first event after the draw
 * clears it, so "Prone Blocked" blinks for as long as the block lasts. */
static void TestTurnBlocked( void ) {
	int t, first = -1, stamps = 0, stamp = -1, shownFrames = 0, blockedFrames = 0;
	float alpha; const char *text;
	Start(); Settle0();
	Box( -BG_PRONE_FEET_DIST - 20, BG_PRONE_TRACE_HALF + 1, 0, -PLAYER_WIDTH - 1, 100, 200 );
	Box( -BG_PRONE_FEET_DIST - 20, -100, 0, -PLAYER_WIDTH - 1, -BG_PRONE_TRACE_HALF - 1, 200 );
	Begin(); FlowClientCommand( "toggleprone\n" ); CHECK( Frame() == BUTTON_PRONE );
	CHECK( Settle( PRONE_VIEWHEIGHT ) && ( sv.pm_flags & PMF_PRONE ) );
	invalidThinks = 0;
	for ( t = 0; t < 3 * FlowHintMs(); t += FRAME ) {
		int before = invalidThinks;
		Begin(); FlowTurn( -1 ); Frame();
		if ( invalidThinks > before ) {
			if ( first < 0 ) first = t;
			blockedFrames++;
		} else if ( first >= 0 ) {
			CHECK( 0 );	/* once blocked, every think stays blocked while the stick pushes */
		}
		if ( first >= 0 ) {
			if ( FlowHintStamp() != stamp && FlowHintType() ) { stamp = FlowHintStamp(); stamps++; CHECK( stamp == now ); }
			if ( FlowHintDrawn( &alpha, &text ) ) {
				shownFrames++;
				CHECK( !strcmp( text, "Prone Blocked" ) && now - stamp <= FlowHintMs() );
			}
		}
	}
	CHECK( first >= 0 && blockedFrames * FRAME > FlowHintMs() && FlowHintType() == FlowHintProneType() );
	CHECK( stamps >= 2 && shownFrames > 0 && sv.pm_flags & PMF_PRONE );
	printf( "a prone turn blocked for %d ms: EV_STANCE_INVALID on every think, the hint armed %d times\n",
		blockedFrames * FRAME, stamps );
}

/* 6: +stance pressed while sprinting: the toggle crouches, the crouch level
 * ends the sprint and crouches; held, it goes prone on the first command at
 * cl_stanceHoldTime. */
static void TestStanceWhileSprinting( void ) {
	int t, pressed;
	Start(); Settle0();
	Begin(); FlowKey( K_PAD0_LEFTSTICK_UP, qtrue ); Frame();
	Begin(); FlowKey( K_PAD0_LEFTSTICK_CLICK, qtrue ); Frame();
	Begin(); FlowKey( K_PAD0_LEFTSTICK_CLICK, qfalse ); Frame();
	CHECK( sv.pm_flags & PMF_SPRINTING );
	Begin(); FlowKey( K_PAD0_RIGHTSTICK_CLICK, qtrue ); CHECK( Frame() == BUTTON_CROUCH ); pressed = now;
	CHECK( ( sv.pm_flags & PMF_DUCKED ) != 0 );
	CHECK( !( sv.pm_flags & PMF_SPRINTING ) );
	for ( t = 0; t < 2 * BG_PRONE_LERP_MS && !( sv.pm_flags & PMF_PRONE ); t += FRAME ) {
		int bits = Next();
		CHECK( bits == ( now - pressed < Cvar_VariableIntegerValue( "cl_stanceHoldTime" ) ? BUTTON_CROUCH : BUTTON_PRONE ) );
	}
	CHECK( !( sv.pm_flags & PMF_SPRINTING ) );
	CHECK( Settle( PRONE_VIEWHEIGHT ) && ( sv.pm_flags & PMF_PRONE ) );
	Begin(); FlowKey( K_PAD0_RIGHTSTICK_CLICK, qfalse ); FlowKey( K_PAD0_LEFTSTICK_UP, qfalse ); Frame();
}

/* 7: a held stance key never cancels the jump. MW2's jump is its own bit
 * (0x400, from MOVEUP), which DOWN never touches, so +movedown behaves as
 * +prone does: +gostand from a standing client stance under the held key
 * sends the jump on every command, and pmove spends it crouched or prone
 * (MW2's Jump_Check refuses a lowered stance first). Letting go of the key
 * under the held jump stands with no jump, as Jump_Check refuses a jump
 * already down on the command before; a fresh press then jumps. On a ladder
 * the held crouch is stripped (PM_CheckDuck's ladder strip) and +gostand
 * pushes off. */
static void TestHeldStanceKeyJump( void ) {
	static const char *keys[2][2] = { { "+movedown 99 0\n", "-movedown 99 0\n" }, { "+prone 98 0\n", "-prone 98 0\n" } };
	int k, t;
	for ( k = 0; k < ARRAY_LEN( keys ); k++ ) {
		int temp = ( k ? BUTTON_PRONE : BUTTON_CROUCH ) | BUTTON_TEMP_STANCE;
		Start(); Settle0();
		Begin(); FlowClientCommand( keys[k][0] ); CHECK( Frame() == temp );
		CHECK( Settle( k ? PRONE_VIEWHEIGHT : CROUCH_VIEWHEIGHT ) );
		Begin(); FlowClientCommand( "+gostand 97 0\n" ); CHECK( Frame() == temp && cmd.upmove > 0 );
		CHECK( FlowStance() == BG_STANCE_STAND && ( sv.pm_flags & PMF_JUMP_HELD ) );
		for ( t = 0; t < 10; t++ ) CHECK( Next() == temp && cmd.upmove > 0 && sv.groundEntityNum != ENTITYNUM_NONE );
		/* the key let go under the held jump: up through the legs, no jump */
		Begin(); FlowClientCommand( keys[k][1] ); CHECK( Frame() == 0 && cmd.upmove > 0 );
		CHECK( Settle( DEFAULT_VIEWHEIGHT ) && !( sv.pm_flags & ( PMF_DUCKED | PMF_PRONE ) ) );
		CHECK( cmd.upmove > 0 && jumps == 0 && sv.groundEntityNum != ENTITYNUM_NONE );
		/* a fresh press jumps */
		Begin(); FlowClientCommand( "-gostand 97 0\n" ); Frame();
		CHECK( Next() == 0 && cmd.upmove == 0 && jumps == 0 );
		Begin(); FlowClientCommand( "+gostand 97 0\n" ); CHECK( Frame() == 0 && cmd.upmove > 0 );
		CHECK( jumps == 1 && sv.groundEntityNum == ENTITYNUM_NONE );
		Begin(); FlowClientCommand( "-gostand 97 0\n" ); Frame();
	}
	/* a ladder face just ahead: crouched by the held +movedown, forward grabs
	 * it, and from the next think the strip stands the player every think
	 * (FORCE_STAND, which the held key refuses), climbing */
	Start(); Settle0();
	Box( PLAYER_WIDTH + 1, -100, 0, PLAYER_WIDTH + 41, 100, 1000 ); boxSurface[boxes - 1] = SURF_LADDER;
	Begin(); FlowClientCommand( "+movedown 99 0\n" ); CHECK( Frame() == ( BUTTON_CROUCH | BUTTON_TEMP_STANCE ) );
	CHECK( Settle( CROUCH_VIEWHEIGHT ) && !( sv.stats[STAT_LADDER] & LADDER_ATTACHED ) );
	Begin(); FlowKey( K_PAD0_LEFTSTICK_UP, qtrue ); Frame();
	for ( t = 0; t < 50 && !( sv.stats[STAT_LADDER] & LADDER_ATTACHED ); t++ ) Next();
	CHECK( sv.stats[STAT_LADDER] & LADDER_ATTACHED );
	for ( t = 0; t < 20; t++ ) {
		CHECK( Next() == ( BUTTON_CROUCH | BUTTON_TEMP_STANCE ) && cmd.upmove < 0 );
		CHECK( ( sv.stats[STAT_LADDER] & LADDER_ATTACHED ) && !( sv.pm_flags & ( PMF_DUCKED | PMF_PRONE ) ) && jumps == 0 );
	}
	CHECK( events[EV_STANCE_FORCE_STAND][BG_STANCE_PARM_FORCED] > 0 && sv.origin[2] > -MINS_Z );
	/* +gostand, the crouch still held: the jump pushes off */
	Begin(); FlowClientCommand( "+gostand 97 0\n" ); CHECK( Frame() == ( BUTTON_CROUCH | BUTTON_TEMP_STANCE ) && cmd.upmove > 0 );
	CHECK( !( sv.stats[STAT_LADDER] & LADDER_ATTACHED ) && ( sv.stats[STAT_LADDER] & LADDER_JUMP_OFF ) && jumps == 1 );
	Begin(); FlowClientCommand( "-gostand 97 0\n-movedown 99 0\n" ); FlowKey( K_PAD0_LEFTSTICK_UP, qfalse ); Frame();
}

int main( void ) {
	TestSprintFromCrouch();
	TestStandBlocked();
	TestProneBlocked();
	TestDeathRespawn();
	TestTurnBlocked();
	TestStanceWhileSprinting();
	TestHeldStanceKeyJump();
	CHECK( !FlowUnknownCommands() );
	printf( "PASS: MW2's stance end to end, client -> Pmove -> cgame -> client (%d checks)\n", checks );
	return 0;
}
