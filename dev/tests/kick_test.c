/* MW2's view kick through the real code, end to end: the real Pmove fires and
 * pulls (kick_pmove.c), the real CG_TransitionPlayerState and CG_EntityEvent
 * deliver the player-view fire event, the real cg_kick.c assigns and runs out
 * the kick, cgame's real native trap (cg_syscalls.c) hands it to the engine,
 * and the real CL_FinishMove (kick_client.c) puts it into the next command,
 * whose angles the real PM_UpdateViewAngles turns into ps.viewangles - what
 * the server aims with and the killcam records.
 *
 * The expected kick comes from this file's own model of MW2's decoded code
 * (BG_WeaponFireRecoil 0x4a5fe0, CG_KickAngles 0x596790, the random 0x44c590)
 * and the IW4 dump's numbers, in double precision, as the research's
 * kicksim.py has them. kicksim's own printed results are checked too. */
#include "../../code/cgame/cg_local.h"
#include "../../code/game/bg_m40a3_data.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

cg_t cg;
cgs_t cgs;
centity_t cg_entities[MAX_GENTITIES];
weaponInfo_t cg_weapons[MAX_WEAPONS];
itemInfo_t cg_items[MAX_ITEMS];
vmCvar_t cg_debugEvents, cg_drawGun, cg_footsteps, cg_nopredict, cg_synchronousClients, cg_showmiss;
#ifdef MISSIONPACK
vmCvar_t cg_cameraOrbit, cg_singlePlayerActive;
#endif

static int checks, fires[MAX_WEAPONS], fireCalls, frameFired;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)

/* the engine side (kick_client.c) and the floor (kick_pmove.c) */
intptr_t QDECL KickTestSyscall( intptr_t arg, ... );
void KickTestFinishMove( usercmd_t *cmd, int serverTime, const vec3_t view, int weapon );
void KickTestClientKick( vec3_t out );
void KickTestClientView( vec3_t out );
void KickTestClientReset( void );
extern int kickTestCalls, kickTestSnaps;
void KickPmove( pmove_t *move );
Q_EXPORT void dllEntry( intptr_t (QDECL *syscallptr)( intptr_t arg,... ) );

/* What CG_EntityEvent and CG_TransitionPlayerState call besides the kick.
 * The shot's own picture and sound are not under test: count them. */
void CG_FireWeapon( centity_t *cent, int weapon ) { (void)cent; (void)weapon; fireCalls++; }
void CG_PrepOffhand( centity_t *cent, int weapon ) { (void)cent; (void)weapon; }
void QDECL CG_Printf( const char *msg, ... ) { (void)msg; }
void QDECL CG_Error( const char *msg, ... ) { fprintf( stderr, "CG_Error %s\n", msg ); exit( 1 ); }
void QDECL Com_Printf( const char *msg, ... ) { (void)msg; }
void QDECL Com_Error( int level, const char *msg, ... ) { (void)level; fprintf( stderr, "Com_Error %s\n", msg ); exit( 1 ); }
void CG_AddBufferedSound( sfxHandle_t sfx ) { (void)sfx; }
void CG_OutOfAmmoChange( void ) {}
/* The rest of what CG_EntityEvent can call is kick_stubs.c's: none of it may run. */

/* ---- MW2's decoded kick, independently, in double precision ---- */

/* The IW4 dump (weapons/mp): hip pitch min/max, yaw min/max, centre; then ADS. */
typedef struct { int weapon; const char *name; double v[10]; } dumpKick_t;
static const dumpKick_t dump[] = {
	{ WP_MACHINEGUN, "m4_mp", { -30, 50, 40, -40, 1500, -30, 50, 40, -40, 1500 } },
	{ WP_SHOTGUN, "spas12_mp", { 85, 95, -45, 45, 1000, 85, 95, -45, 45, 1000 } },
	{ WP_ROCKET_LAUNCHER, "rpg_mp/at4_mp", { 100, 150, -50, -80, 1500, 20, 95, -10, 30, 2500 } },
	{ WP_M40A3, "cheytac_mp", { 80, 100, -50, -55, 500, 30, 85, 70, -75, 500 } }
};
static const int equipment[] = { WP_FRAG, WP_FLASH, WP_SEMTEX, WP_THROWING_KNIFE, WP_TACTICAL_INSERTION };

static const dumpKick_t *Dump( int weapon ) {
	int i;
	for ( i = 0; i < ARRAY_LEN( dump ); i++ ) if ( dump[i].weapon == weapon ) return &dump[i];
	return NULL;
}
static int IsEquipment( int weapon ) {
	int i;
	for ( i = 0; i < ARRAY_LEN( equipment ); i++ ) if ( equipment[i] == weapon ) return 1;
	return 0;
}
/* MW2's centre speed for the weapon its view reads: weapon 0 (no def) 2400. */
static double RefCenter( int viewWeapon, double frac ) {
	const dumpKick_t *d = Dump( viewWeapon );
	if ( IsEquipment( viewWeapon ) ) return 0;
	if ( !d ) return 2400;
	return frac > 0.5 ? d->v[9] : d->v[4];
}
/* MW2's view weapon: the offhand once in hand, the gun under a melee (MW2's
 * knife is the gun's own), nothing from an empty hand. */
static int RefViewWeapon( const playerState_t *p, int gun ) {
	return p->weapon == WP_KNIFE ? gun : p->weapon;
}
static unsigned RefRandomSeed;
static double RefRandom( void ) {
	RefRandomSeed = RefRandomSeed * 214013u + 2531011u;
	return ( RefRandomSeed >> 17 ) / 32768.0;
}
static void RefRecoil( int weapon, double frac, double vel[3] ) {
	const dumpKick_t *d = Dump( weapon );
	const double *k = d->v + ( frac == 1.0 ? 5 : 0 );
	double r1 = RefRandom(), r2 = RefRandom();
	double pitch = k[0] + r1 * ( k[1] - k[0] ), yaw = k[2] + r2 * ( k[3] - k[2] );
	vel[0] = -pitch; vel[1] = yaw; vel[2] = -0.5 * yaw;
}
static void RefStep( double vel[3], double ang[3], double center, int msec ) {
	int i, step;
	for ( ; msec > 0; msec -= 5 ) {
		step = msec < 5 ? msec : 5;
		for ( i = 0; i < 3; i++ ) {
			double dt = step * 0.001, d, next;
			if ( vel[i] == 0 && ang[i] == 0 ) continue;
			if ( ang[i] != 0 ) vel[i] += ( ang[i] > 0 ? -1 : 1 ) * center * dt;
			d = vel[i] * dt;
			if ( ang[i] * d < 0 ) d *= 0.06;
			next = ang[i] + d;
			if ( ang[i] * next < 0 ) { ang[i] = vel[i] = 0; }
			else {
				ang[i] = next;
				if ( next == 0 ) vel[i] = 0;
				else if ( fabs( next ) > 10 ) { ang[i] = next > 0 ? 10 : -10; vel[i] = 0; }
			}
		}
	}
}
/* A seed whose next two draws are k1/32768 and k2/32768: search the second
 * state's block for one that steps into the wanted block, then step back. */
static unsigned SeedFor( unsigned k1, unsigned k2 ) {
	unsigned inverse = 214013u, s1, s2, i;
	for ( i = 0; i < 5; i++ ) inverse *= 2u - 214013u * inverse;
	CHECK( inverse * 214013u == 1u );
	for ( s1 = k1 << 17; s1 < ( ( k1 + 1 ) << 17 ) || ( k1 == 32767 && s1 >= ( k1 << 17 ) ); s1++ ) {
		s2 = s1 * 214013u + 2531011u;
		if ( ( s2 >> 17 ) == k2 ) return ( s1 - 2531011u ) * inverse;
		if ( s1 == 0xffffffffu ) break;
	}
	fprintf( stderr, "no seed for %u %u\n", k1, k2 ); exit( 1 );
}

/* ---- the frame loop: client command, Pmove, cgame frame ---- */

#define FRAME 8	/* 125 fps, one command a frame */
static playerState_t ps;
static pmove_t move;
static snapshot_t snap;
static int now, slot, gun;
static const vec3_t view = { 0, 90, 0 };
static usercmd_t lastCmd;

static void Ready( int primary, int secondary, int lethal ) {
	bg_class_t loadout = *BG_Class( 0 );
	memset( &ps, 0, sizeof( ps ) ); memset( &move, 0, sizeof( move ) ); memset( &snap, 0, sizeof( snap ) );
	memset( &cg, 0, sizeof( cg ) ); memset( fires, 0, sizeof( fires ) ); fireCalls = 0;
	loadout.slot[0] = primary; loadout.slot[1] = secondary;
	if ( lethal ) { loadout.lethal = lethal; loadout.lethalCount = BG_ClassEquipmentCount( lethal ); }
	CHECK( BG_ValidateClass( &loadout ) );
	BG_ApplyClass( &ps, &loadout );
	ps.stats[STAT_HEALTH] = 100; ps.groundEntityNum = ENTITYNUM_WORLD; ps.gravity = 800; ps.speed = 190;
	ps.commandTime = now = 100000; slot = 0; gun = primary;
	move.ps = &ps; move.pmove_msec = 8;
	KickTestClientReset();
	cg.time = cg.kickTime = now; cg.snap = &snap; snap.ps = ps; cg.predictedPlayerState = ps;
	BG_PlayerStateToEntityState( &ps, &cg.predictedPlayerEntity.currentState, qfalse );
	CHECK( ps.weapon == primary && ps.weaponstate == WEAPON_READY );
}
static void Frame( int buttons ) {
	playerState_t old = ps;
	int seq, calls = fireCalls, raised = 0;
	now += FRAME;
	KickTestFinishMove( &lastCmd, now, view, BG_SlotCommand( &ps, slot ) );	/* the client's command */
	lastCmd.buttons = buttons;
	move.cmd = lastCmd; KickPmove( &move );	/* the server's and the prediction's think */
	frameFired = -1;	/* what pmove fired this command, if anything */
	for ( seq = old.eventSequence; seq < ps.eventSequence; seq++ ) {
		if ( ps.events[seq & ( MAX_PS_EVENTS - 1 )] == EV_FIRE_WEAPON ) { frameFired = ps.weapon; fires[ps.weapon]++; raised++; }
	}
	cg.time = now; snap.ps.pm_flags = ( snap.ps.pm_flags & PMF_FOLLOW ) | ( ps.pm_flags & ~PMF_FOLLOW );
	cg.predictedPlayerState = ps;
	CG_TransitionPlayerState( &cg.predictedPlayerState, &old );	/* the fire event */
	CHECK( fireCalls - calls == raised );	/* each one reached CG_EntityEvent's fire case */
	CG_KickAngles();	/* the frame's kick, to the engine */
	/* CG_AddPacketEntities' predicted entity, which the next frame's events play on */
	BG_PlayerStateToEntityState( &cg.predictedPlayerState, &cg.predictedPlayerEntity.currentState, qfalse );
}
static float AngleError( float a, float b ) { return fabs( AngleSubtract( a, b ) ); }
/* The command just run aimed at the view plus the kick the frame before handed
 * over, and the real PM_UpdateViewAngles made that ps.viewangles. */
static void CheckAim( const double kick[3] ) {
	vec3_t clientView;
	int i;
	for ( i = 0; i < 3; i++ ) CHECK( AngleError( ps.viewangles[i], view[i] + (float)kick[i] ) < 0.006f );
	KickTestClientView( clientView );
	CHECK( VectorCompare( clientView, view ) );	/* the kick never enters cl.viewangles */
}
static void CheckKick( const double kick[3] ) {
	vec3_t engine;
	int i;
	KickTestClientKick( engine );
	for ( i = 0; i < 3; i++ ) {
		CHECK( fabs( cg.kickAngles[i] - kick[i] ) < 0.002 );
		CHECK( engine[i] == cg.kickAngles[i] );
	}
}

/* ---- the tests ---- */

/* Every weapon's data: the four MW2 guns kick at MW2's numbers (r exactly 0
 * and one half from crafted seeds), equipment brakes nothing, the knife reads
 * the gun, and the rest have no MW2 def. */
static void TestData( void ) {
	int w, i, k;
	const unsigned ks[2] = { 0, 16384 };
	playerState_t p;
	vec3_t vel;
	for ( w = 0; w < WP_NUM_WEAPONS; w++ ) {
		const dumpKick_t *d = Dump( w );
		memset( &p, 0, sizeof( p ) ); p.weapon = w;
		if ( d ) {
			for ( k = 0; k < 2; k++ ) for ( i = 0; i < 2; i++ ) {
				const double *v = d->v + ( i ? 5 : 0 );
				double r = ks[k] / 32768.0;
				unsigned seed = SeedFor( ks[k], ks[k] );
				CHECK( BG_WeaponFireRecoil( &p, i ? 1.0f : 0.999f, vel, &seed ) );
				CHECK( fabs( vel[PITCH] + ( v[0] + r * ( v[1] - v[0] ) ) ) < 1e-4 );
				CHECK( fabs( vel[YAW] - ( v[2] + r * ( v[3] - v[2] ) ) ) < 1e-4 );
				CHECK( fabs( vel[ROLL] + 0.5 * vel[YAW] ) < 1e-4 );
			}
			CHECK( BG_WeaponViewKickCenterSpeed( w, 0.5f ) == (float)d->v[4] );
			CHECK( BG_WeaponViewKickCenterSpeed( w, 0.51f ) == (float)d->v[9] );
		} else {
			unsigned seed = 1234;
			VectorSet( vel, 1, 2, 3 );
			CHECK( !BG_WeaponFireRecoil( &p, 0, vel, &seed ) && !BG_WeaponFireRecoil( &p, 1, vel, &seed ) );
			CHECK( vel[0] == 1 && vel[1] == 2 && vel[2] == 3 && seed == 1234 );	/* no def: no kick at all */
			CHECK( BG_WeaponViewKickCenterSpeed( w, 0 ) == (float)RefCenter( w, 0 ) );
			CHECK( BG_WeaponViewKickCenterSpeed( w, 1 ) == (float)RefCenter( w, 1 ) );
			CHECK( IsEquipment( w ) ? RefCenter( w, 1 ) == 0 : w == WP_KNIFE || RefCenter( w, 1 ) == 2400 );
		}
	}
	/* the M300 is generated from weapon.json, as cheytac_mp */
	CHECK( M40_HIP_VIEW_KICK_CENTER_SPEED == 500 && M40_ADS_VIEW_KICK_YAW_MAX == -75 );
	/* the knife is MW2's melee: the view reads the gun it is written over */
	memset( &p, 0, sizeof( p ) ); p.weapon = WP_KNIFE;
	p.stats[STAT_SLOT_PRIMARY] = WP_M40A3; p.stats[STAT_SLOT_SECONDARY] = WP_SHOTGUN;
	p.stats[STAT_ACTIVE_SLOT] = SLOT_SECONDARY;
	CHECK( BG_ViewmodelWeapon( &p ) == WP_SHOTGUN );
	p.weaponAction = WA_MELEE | WA_LEFT_EMPTY; CHECK( BG_ViewmodelWeapon( &p ) == WP_NONE );
	for ( i = 0; i < ARRAY_LEN( equipment ); i++ ) { p.weapon = equipment[i]; CHECK( BG_ViewmodelWeapon( &p ) == equipment[i] ); }
	p.weapon = WP_M40A3; CHECK( BG_ViewmodelWeapon( &p ) == WP_M40A3 );
}

/* The research's kicksim.py results (8 ms frames, r one half), through the
 * real CG_KickAngles: peaks and the time back to 0, with and without the
 * throwing knife in hand from 8 ms after the shot for 558 ms. */
static void TestKicksim( void ) {
	static const struct { int weapon, ads; double peak[3], back, knifePeak[3], knifeBack; } sim[] = {
		{ WP_M40A3, 0, { -8.36, -2.91, +0.77 }, 936, { -10, -10, +10 }, 1384 },
		{ WP_M40A3, 1, { -3.47, -0.02, +0.01 }, 600, { -10, -0.58, +0.01 }, 1384 },
		{ WP_SHOTGUN, 0, { -4.31, 0, 0 }, 472, { -10, 0, 0 }, 1144 },
		{ WP_MACHINEGUN, 0, { -0.07, 0, 0 }, 56, { -3.15, 0, 0 }, 840 },
	};
	/* the Intervention's hip mid trace, normal and with the knife */
	static const struct { int t; double normal[3], knife[3]; } trace[] = {
		{ 16, { -1.40, -0.80, +0.38 }, { -1.42, -0.82, +0.40 } },
		{ 104, { -6.81, -2.91, +0.73 }, { -9.21, -5.31, +2.58 } },
		{ 400, { -7.65, -1.63, 0 }, { -10, -10, +9.91 } },
		{ 600, { -5.75, 0, 0 }, { -9.98, -9.98, +9.98 } },
		{ 1000, { 0, 0, 0 }, { -7.17, -7.17, +7.17 } },
		{ 1200, { 0, 0, 0 }, { -3.97, -3.97, +3.97 } },
	};
	int s, knife, t, i, back;
	for ( s = 0; s < ARRAY_LEN( sim ); s++ ) for ( knife = 0; knife < 2; knife++ ) {
		double peak[3] = { 0, 0, 0 };
		unsigned seed = SeedFor( 16384, 16384 );
		Ready( WP_M40A3, WP_SHOTGUN, WP_THROWING_KNIFE );
		cg.predictedPlayerState.weapon = sim[s].weapon; cg.adsFrac = sim[s].ads ? 1.0f : 0.0f;
		CHECK( BG_WeaponFireRecoil( &cg.predictedPlayerState, cg.adsFrac, cg.kickAVel, &seed ) );
		back = -1;
		for ( t = FRAME; t < 2500; t += FRAME ) {
			cg.predictedPlayerState.weapon = knife && t - FRAME >= 8 && t - FRAME < 8 + 558 ? WP_THROWING_KNIFE : sim[s].weapon;
			cg.time += FRAME; CG_KickAngles();
			for ( i = 0; i < 3; i++ ) if ( fabs( cg.kickAngles[i] ) > fabs( peak[i] ) ) peak[i] = cg.kickAngles[i];
			if ( back < 0 && VectorCompare( cg.kickAngles, vec3_origin ) ) back = t;
			if ( sim[s].weapon == WP_M40A3 && !sim[s].ads ) for ( i = 0; i < ARRAY_LEN( trace ); i++ ) if ( trace[i].t == t ) {
				const double *want = knife ? trace[i].knife : trace[i].normal;
				CHECK( fabs( cg.kickAngles[0] - want[0] ) < 0.006 && fabs( cg.kickAngles[1] - want[1] ) < 0.006 &&
				       fabs( cg.kickAngles[2] - want[2] ) < 0.006 );
			}
		}
		for ( i = 0; i < 3; i++ ) CHECK( fabs( peak[i] - ( knife ? sim[s].knifePeak[i] : sim[s].peak[i] ) ) < 0.006 );
		CHECK( back == ( knife ? sim[s].knifeBack : sim[s].back ) );
	}
}

/* A shot, then optionally a quick action pressed a command later, through the
 * real Pmove and cgame, against the model of MW2. Returns the largest roll. */
static double Shot( int primary, int lethal, int ads, int second, int frames, int *viewWeapons ) {
	double vel[3] = { 0, 0, 0 }, kick[3] = { 0, 0, 0 }, roll = 0;
	int i, f, shotFrame = -1, kicked;
	Ready( primary, primary == WP_SHOTGUN ? WP_MACHINEGUN : WP_SHOTGUN, lethal );
	cg.adsFrac = ads ? 1.0f : 0.0f;
	RefRandomSeed = cg_kickSeed;
	if ( viewWeapons ) *viewWeapons = 0;
	for ( f = 0; f < frames; f++ ) {
		int buttons = ( ads ? BUTTON_ADS : 0 ) | ( f == 0 ? BUTTON_ATTACK : 0 ) | ( f == 1 ? second : 0 );
		Frame( buttons );
		CheckAim( kick );	/* the command carried last frame's kick */
		kicked = frameFired == primary;
		if ( kicked ) { CHECK( shotFrame < 0 && f == 0 ); shotFrame = f; RefRecoil( primary, cg.adsFrac, vel ); }
		if ( viewWeapons && ps.weapon != primary ) *viewWeapons |= 1 << ps.weapon;
		RefStep( vel, kick, RefCenter( RefViewWeapon( &ps, gun ), cg.adsFrac ), FRAME );
		CheckKick( kick );
		if ( fabs( kick[ROLL] ) > fabs( roll ) ) roll = kick[ROLL];
	}
	CHECK( shotFrame == 0 && kickTestCalls == frames && kickTestSnaps > 0 );
	for ( i = 0; i < 3; i++ ) CHECK( cg.kickAngles[i] == 0 );	/* it always comes back */
	return roll;
}

/* The tilt emerges: the throwing knife's centre speed is 0, so the shot's kick
 * runs to the clamp while it is in hand, in the aim the server records. */
static void TestThrowingKnifeTilt( void ) {
	int seen, f, held = 0, clamped = 0;
	double plain = Shot( WP_M40A3, WP_THROWING_KNIFE, 0, 0, 250, NULL );
	double tilt = Shot( WP_M40A3, WP_THROWING_KNIFE, 0, BUTTON_LETHAL, 300, &seen );
	CHECK( fabs( plain ) > 0.6 && fabs( plain ) < 0.9 );	/* an ordinary shot rolls under a degree */
	CHECK( tilt == BG_KICK_MAX_ANGLE );	/* with the knife the roll clamps */
	CHECK( seen & ( 1 << WP_THROWING_KNIFE ) );
	/* the recorded aim holds the clamp for the rest of the knife's time in hand */
	Ready( WP_M40A3, WP_SHOTGUN, WP_THROWING_KNIFE );
	Frame( BUTTON_ATTACK ); Frame( BUTTON_LETHAL );
	CHECK( ps.weapon == WP_THROWING_KNIFE );
	for ( f = 0; f < 200 && ps.weapon == WP_THROWING_KNIFE; f++ ) {
		Frame( 0 ); held++;
		if ( AngleError( ps.viewangles[ROLL], BG_KICK_MAX_ANGLE ) < 0.006f ) clamped++;
		else CHECK( !clamped );	/* once there it stays */
	}
	CHECK( held * FRAME >= BG_QUICK_ACTION_HOLD && clamped > 0 && AngleError( ps.viewangles[PITCH], -BG_KICK_MAX_ANGLE ) < 0.006f );
	CHECK( fires[WP_THROWING_KNIFE] == 1 );	/* the throw kicked nothing: the clamp held through it */
	/* the SPAS (pump) and the M300 aimed tilt as well */
	CHECK( fabs( Shot( WP_SHOTGUN, WP_THROWING_KNIFE, 0, BUTTON_LETHAL, 300, NULL ) ) > 5 );
	Shot( WP_M40A3, WP_THROWING_KNIFE, 1, BUTTON_LETHAL, 300, NULL );
}

/* MW2's knife is the gun's own melee: the gun's centring goes on, so a knife
 * after a shot tilts nothing, and the knife's hit kicks nothing. A frag's
 * quick drop keeps the gun's centring and only the grenade in hand stops it. */
static void TestOtherQuickActions( void ) {
	int seen;
	double roll = Shot( WP_MACHINEGUN, 0, 0, BUTTON_MELEE, 250, &seen );
	CHECK( ( seen & ( 1 << WP_KNIFE ) ) && fires[WP_KNIFE] == 1 && fabs( roll ) < 0.3 );
	roll = Shot( WP_M40A3, WP_FRAG, 0, BUTTON_LETHAL, 300, &seen );
	CHECK( ( seen & ( 1 << WP_FRAG ) ) && fabs( roll ) < 0.9 );
	/* a gun with no MW2 counterpart fires with no kick */
	Ready( WP_RAILGUN, WP_SHOTGUN, 0 );
	Frame( BUTTON_ATTACK ); CHECK( fires[WP_RAILGUN] == 1 );
	Frame( 0 ); CHECK( VectorCompare( cg.kickAngles, vec3_origin ) && VectorCompare( cg.kickAVel, vec3_origin ) );
}

/* MW2 runs the kick only for the player's own state (POF_PLAYER): a demo, the
 * killcam and a followed player get none, and one under way stops at once. */
static void TestOnlyThePlayer( void ) {
	int mode, f;
	vec3_t engine;
	for ( mode = 0; mode < 3; mode++ ) {
		Ready( WP_M40A3, WP_SHOTGUN, WP_THROWING_KNIFE );
		Frame( BUTTON_ATTACK ); Frame( 0 ); Frame( 0 );
		CHECK( !VectorCompare( cg.kickAngles, vec3_origin ) );
		if ( mode == 0 ) cg.demoPlayback = qtrue;
		if ( mode == 1 ) snap.snapFlags |= SNAPFLAG_REPLAY;
		if ( mode == 2 ) snap.ps.pm_flags |= PMF_FOLLOW;
		for ( f = 0; f < 20; f++ ) {
			Frame( f == 5 ? BUTTON_ATTACK : 0 );
			CHECK( VectorCompare( cg.kickAngles, vec3_origin ) && VectorCompare( cg.kickAVel, vec3_origin ) );
			KickTestClientKick( engine ); CHECK( VectorCompare( engine, vec3_origin ) );
			if ( f > 0 ) CheckAim( (const double[3]){ 0, 0, 0 } );
		}
	}
}

/* Another player's shot is an entity event: it plays the shot and kicks no one. */
static void TestOthersShots( void ) {
	centity_t *other = &cg_entities[3];
	Ready( WP_M40A3, WP_SHOTGUN, WP_THROWING_KNIFE );
	memset( other, 0, sizeof( *other ) );
	other->currentState.number = other->currentState.clientNum = 3;
	other->currentState.weapon = WP_M40A3; other->currentState.event = EV_FIRE_WEAPON;
	CG_EntityEvent( other, other->lerpOrigin );
	CHECK( fireCalls == 1 && VectorCompare( cg.kickAVel, vec3_origin ) );
	Frame( 0 ); CHECK( VectorCompare( cg.kickAngles, vec3_origin ) );
}

/* cgame's trap number is the QVM stub's (cg_syscalls.asm is -1 - the import). */
static void TestQvmStub( void ) {
	char line[256], name[128];
	int number, found = 0;
	FILE *f = fopen( "code/cgame/cg_syscalls.asm", "r" );
	CHECK( f != NULL );
	while ( fgets( line, sizeof( line ), f ) ) {
		if ( sscanf( line, " equ %127s %d", name, &number ) == 2 && !strcmp( name, "trap_SetUserCmdKickAngles" ) ) {
			CHECK( number == -1 - CG_SETUSERCMDKICKANGLES ); found++;
		}
	}
	fclose( f );
	CHECK( found == 1 );
}

int main( void ) {
	dllEntry( KickTestSyscall );
	TestData();
	TestKicksim();
	TestThrowingKnifeTilt();
	TestOtherQuickActions();
	TestOnlyThePlayer();
	TestOthersShots();
	TestQvmStub();
	printf( "PASS: %d view kick checks (data, kicksim, real fire/pull/Pmove/cgame/CL_FinishMove, gate, others, QVM stub)\n", checks );
	return 0;
}
