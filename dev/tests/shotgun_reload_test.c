/*
 * Focused native tests of the real shared weapon logic, without a renderer or
 * world simulation. Include the implementation to exercise PM_Weapon directly;
 * production code does not expose a test-only gameplay entry point.
 */
#include "../../code/game/bg_pmove.c"

static playerState_t testPs;
static pmove_t testMove;
static int shots;
static int checks;

void QDECL Com_Printf( const char *fmt, ... ) {
	(void)fmt;
}

void QDECL Com_Error( int level, const char *fmt, ... ) {
	va_list args;
	(void)level;
	va_start( args, fmt );
	vfprintf( stderr, fmt, args );
	va_end( args );
	exit( 1 );
}

void trap_SnapVector( float *v ) {
	SnapVector( v );
}

#define CHECK(condition) do { \
	checks++; \
	if ( !(condition) ) { \
		fprintf( stderr, "FAIL line %d: %s\n", __LINE__, #condition ); \
		exit( 1 ); \
	} \
} while (0)

/*
 * Everything below is derived from the shipped tables rather than written as a
 * literal. These tests exist to pin down ORDERING - what happens on the same
 * think as what - and ordering has to keep holding when a weapon is retimed.
 * An earlier version of this file hardcoded 500/550/350 and started failing the
 * moment the real MW2 lengths landed, which said nothing about the behaviour.
 */
static int Seg( int weapon, int seq ) {
	return BG_WeaponReloadSegLength( weapon, seq );
}

/* When a note fires, measured from the start of the segment it lives on. */
static int NoteTime( int weapon, int seq, int note ) {
	const bg_weaponNote_t *n = PM_SegNotes( weapon, seq );
	for ( ; n->note != WNOTE_NONE; n++ ) {
		if ( n->note == note ) {
			return n->time;
		}
	}
	return -1;
}

/* Comfortably longer than any single transition on either test weapon, so
   "let whatever is in flight finish" needs no per-weapon arithmetic. */
static int SettleTime( void ) {
	return BG_WeaponDropTime( WP_SHOTGUN )    + BG_WeaponRaiseTime( WP_SHOTGUN )
	     + BG_WeaponDropTime( WP_MACHINEGUN ) + BG_WeaponRaiseTime( WP_MACHINEGUN )
	     + BG_WeaponSprintInTime( WP_SHOTGUN )
	     + BG_WeaponSprintOutTime( WP_SHOTGUN );
}

/* Elapsed reload time at which the Nth shell seats, counting from 1. */
static int ShellSeats( int nth ) {
	return Seg( WP_SHOTGUN, RSEQ_START )
	     + ( nth - 1 ) * Seg( WP_SHOTGUN, RSEQ_LOOP )
	     + NoteTime( WP_SHOTGUN, RSEQ_LOOP, WNOTE_MAG_IN );
}

static void BeginReload( int weapon, int loaded, int reserve ) {
	memset( &testPs, 0, sizeof(testPs) );
	memset( &testMove, 0, sizeof(testMove) );
	memset( &pml, 0, sizeof(pml) );
	testMove.ps = &testPs;
	testMove.cmd.weapon = weapon;
	testPs.weapon = weapon;
	testPs.weaponAnimTime = -1;
	testPs.stats[STAT_HEALTH] = 100;
	testPs.stats[STAT_WEAPONS] = (1 << WP_SHOTGUN) | (1 << WP_MACHINEGUN);
	testPs.stats[STAT_SLOT_PRIMARY] = WP_SHOTGUN;
	testPs.stats[STAT_SLOT_SECONDARY] = WP_MACHINEGUN;
	testPs.stats[STAT_ACTIVE_SLOT] = weapon == WP_MACHINEGUN ? SLOT_SECONDARY : SLOT_PRIMARY;
	testPs.ammo[weapon] = loaded;
	testPs.ammoReserve[weapon] = reserve;
	testPs.ammo[WP_MACHINEGUN] = weapon == WP_MACHINEGUN ? loaded : 10;
	pm = &testMove;
	shots = 0;
	PM_BeginReload();
}

static void Step( int msec ) {
	int sequence = testPs.eventSequence;
	pml.msec = msec;
	PM_Weapon();
	for ( ; sequence < testPs.eventSequence; sequence++ ) {
		if ( testPs.events[sequence & (MAX_PS_EVENTS - 1)] == EV_FIRE_WEAPON ) {
			shots++;
		}
	}
}

static void RunFor( int duration, int step ) {
	while ( duration > 0 ) {
		int dt = duration < step ? duration : step;
		Step( dt );
		duration -= dt;
	}
}

static void FinishAndFire( int loaded, int reserve, int step ) {
	CHECK( testPs.weaponstate == WEAPON_RELOADING );
	CHECK( testPs.weaponAnimSeq == RSEQ_END );
	CHECK( testPs.weaponAnimTime == 0 );
	CHECK( shots == 0 );
	CHECK( testPs.ammo[WP_SHOTGUN] == loaded );
	CHECK( testPs.ammoReserve[WP_SHOTGUN] == reserve );
	RunFor( Seg( WP_SHOTGUN, RSEQ_END ) - 1, step );
	CHECK( shots == 0 );
	CHECK( testPs.weaponstate == WEAPON_RELOADING );
	Step( 1 );
	CHECK( shots == 1 );
	CHECK( testPs.weaponstate == WEAPON_FIRING );
	CHECK( testPs.ammo[WP_SHOTGUN] == loaded - 1 );
	CHECK( testPs.ammoReserve[WP_SHOTGUN] == reserve );
}

static void TestAttackInterrupt( int step ) {
	int i;
	/* Start, loop before insertion, the insertion think itself, loop after. */
	const int seats = ShellSeats( 1 );
	const int times[] = {
		Seg( WP_SHOTGUN, RSEQ_START ) / 2,
		( Seg( WP_SHOTGUN, RSEQ_START ) + seats ) / 2,
		seats - 1,
		seats + 1
	};
	for ( i = 0; i < 4; i++ ) {
		int loaded = i >= 2 ? 4 : 3;
		int reserve = i >= 2 ? 9 : 10;
		BeginReload( WP_SHOTGUN, 3, 10 );
		RunFor( times[i], 1 );
		testMove.cmd.buttons = BUTTON_ATTACK;
		Step( 1 );
		FinishAndFire( loaded, reserve, step );
	}
}

static void TestEmptyTube( int step ) {
	int elapsed;
	BeginReload( WP_SHOTGUN, 0, 10 );
	testMove.cmd.buttons = BUTTON_ATTACK;
	for ( elapsed = 0; elapsed < 2000 && testPs.weaponAnimSeq != RSEQ_END; elapsed += step ) {
		Step( step );
		CHECK( shots == 0 );
	}
	CHECK( elapsed < 2000 );
	FinishAndFire( 1, 9, step );
}

static void TestReleasedAttack( int step ) {
	BeginReload( WP_SHOTGUN, 3, 10 );
	testMove.cmd.buttons = BUTTON_ATTACK;
	Step( step );
	testMove.cmd.buttons = 0;
	RunFor( Seg( WP_SHOTGUN, RSEQ_END ) + SettleTime(), step );
	CHECK( testPs.weaponstate == WEAPON_READY );
	CHECK( shots == 0 );
	CHECK( testPs.ammo[WP_SHOTGUN] == 3 );
	CHECK( testPs.ammoReserve[WP_SHOTGUN] == 10 );
}

static void TestNaturalEnd( int loaded, int reserve, int step ) {
	BeginReload( WP_SHOTGUN, loaded, reserve );
	RunFor( Seg( WP_SHOTGUN, RSEQ_START ) + Seg( WP_SHOTGUN, RSEQ_LOOP ), 1 );
	CHECK( testPs.weaponAnimSeq == RSEQ_END );
	RunFor( 100, step );
	testMove.cmd.buttons = BUTTON_ATTACK;
	/* Attacking during END must not restart the closing animation. */
	RunFor( Seg( WP_SHOTGUN, RSEQ_END ) - 100 - 1, step );
	CHECK( shots == 0 );
	Step( 1 );
	CHECK( shots == 1 );
	CHECK( testPs.ammo[WP_SHOTGUN] == loaded );
	CHECK( testPs.ammoReserve[WP_SHOTGUN] == reserve - 1 );
}

static void TestMagazineUnchanged( int step ) {
	BeginReload( WP_MACHINEGUN, 3, 40 );
	testMove.cmd.buttons = BUTTON_ATTACK;
	/* First think enters its zero-length START -> LOOP. */
	RunFor( NoteTime( WP_MACHINEGUN, RSEQ_LOOP, WNOTE_MAG_IN ) - 1, 1 );
	CHECK( testPs.weaponstate == WEAPON_RELOADING );
	CHECK( testPs.weaponAnimSeq == RSEQ_LOOP );
	CHECK( testPs.ammo[WP_MACHINEGUN] == 3 );
	CHECK( shots == 0 );
	RunFor( Seg( WP_MACHINEGUN, RSEQ_LOOP )
	        - NoteTime( WP_MACHINEGUN, RSEQ_LOOP, WNOTE_MAG_IN ), step );
	CHECK( testPs.ammo[WP_MACHINEGUN] == 30 );
	CHECK( shots == 0 );
	Step( 1 );
	CHECK( shots == 1 );
}

static void TestSwapSprintPreserved( int sprint, int offset ) {
	int remaining;
	/* Interrupt on the think the SECOND shell would seat. */
	BeginReload( WP_SHOTGUN, 3, 10 );
	RunFor( ShellSeats( 2 ) - 1 + offset, 1 );
	if ( sprint ) {
		testPs.pm_flags |= PMF_SPRINTING;
	} else {
		testMove.cmd.weapon = WP_MACHINEGUN;
	}
	/* Include attack to prove reload interruption cannot steal swap/sprint. */
	testMove.cmd.buttons = BUTTON_ATTACK;
	Step( 1 );
	if ( sprint ) {
		CHECK( testPs.weaponstate == (offset == 0 ? WEAPON_SPRINTING : WEAPON_SPRINT_IN) );
	} else {
		CHECK( testPs.weaponstate == (offset == 0 ? WEAPON_RAISING : WEAPON_DROPPING) );
	}
	CHECK( testPs.ammo[WP_SHOTGUN] == (offset > 0 ? 5 : 4) );
	CHECK( testPs.ammoReserve[WP_SHOTGUN] == (offset > 0 ? 8 : 9) );
	CHECK( !(testPs.pm_flags & PMF_PENDING_MAG) );
	CHECK( shots == 0 );
	testMove.cmd.buttons = 0;
	RunFor( SettleTime(), 8 );
	if ( sprint ) {
		testPs.pm_flags &= ~PMF_SPRINTING;
	} else {
		testMove.cmd.weapon = WP_SHOTGUN;
	}
	RunFor( SettleTime(), 8 );
	CHECK( testPs.weaponstate == WEAPON_READY );
	remaining = testPs.ammo[WP_SHOTGUN];
	testMove.cmd.buttons = BUTTON_RELOAD;
	Step( 1 );
	CHECK( testPs.weaponAnimSeq == RSEQ_START );
	CHECK( testPs.weaponAnimTime == 0 );
	CHECK( testPs.ammo[WP_SHOTGUN] == remaining );
}

/* --- the fire cycle -----------------------------------------------------
 *
 * A pump gun is not "firing" for its whole refire: the shot leaves at the
 * start and the rest is the action being worked. These pin down that the two
 * are distinct states, that the cycle cannot be skipped, and that it cancels
 * on exactly the same collision the reload does.
 */
static int FireNote( int note ) {
	const bg_weaponNote_t *n = bg_weaponFire[WP_SHOTGUN].notes;
	for ( ; n->note != WNOTE_NONE; n++ ) {
		if ( n->note == note ) {
			return n->time;
		}
	}
	return -1;
}

static void ReadyToFire( void ) {
	memset( &testPs, 0, sizeof(testPs) );
	memset( &testMove, 0, sizeof(testMove) );
	memset( &pml, 0, sizeof(pml) );
	testMove.ps = &testPs;
	testMove.cmd.weapon = WP_SHOTGUN;
	testPs.weapon = WP_SHOTGUN;
	testPs.weaponstate = WEAPON_READY;
	testPs.weaponAnimTime = -1;
	testPs.stats[STAT_HEALTH] = 100;
	testPs.stats[STAT_WEAPONS] = (1 << WP_SHOTGUN) | (1 << WP_MACHINEGUN);
	testPs.ammo[WP_SHOTGUN] = 8;
	testPs.stats[STAT_SLOT_PRIMARY] = WP_SHOTGUN;
	testPs.stats[STAT_SLOT_SECONDARY] = WP_MACHINEGUN;
	testPs.ammoReserve[WP_SHOTGUN] = 24;
	testPs.ammo[WP_MACHINEGUN] = 30;
	pm = &testMove;
	shots = 0;
	testMove.cmd.buttons = BUTTON_ATTACK;
	Step( 1 );
	CHECK( shots == 1 );
	CHECK( testPs.weaponstate == WEAPON_FIRING );
}

static void TestPumpIsItsOwnState( int step ) {
	ReadyToFire();
	testMove.cmd.buttons = 0;

	/* The shot itself. */
	RunFor( FireNote( WNOTE_BOLT_OPEN ) - 1, step );
	CHECK( testPs.weaponstate == WEAPON_FIRING );

	/* Then the action works, and that is NOT firing. */
	RunFor( BG_WeaponFireLength( WP_SHOTGUN ) - FireNote( WNOTE_BOLT_OPEN ), step );
	CHECK( testPs.weaponstate == WEAPON_BOLTING );

	/* Only when the whole cycle has played does the gun come back up. */
	Step( step );
	CHECK( testPs.weaponstate == WEAPON_READY );
	CHECK( testPs.weaponAnimTime == -1 );
}

static void TestPumpCannotBeShortCircuited( void ) {
	int elapsed;
	/* Holding fire must not produce a second shot before the cycle is over -
	   the chamber note gives the lock away partway through, so if anything
	   were gated on the lock alone this is where it would double-fire. */
	ReadyToFire();
	for ( elapsed = 1; elapsed < BG_WeaponFireLength( WP_SHOTGUN ); elapsed++ ) {
		Step( 1 );
		CHECK( shots == 1 );
	}
	Step( 1 );
	CHECK( shots == 2 );
}

static void TestPumpCancelIsFree( void ) {
	/* Swap requested on the think the round chambers. The holster stamps its
	   normal time, the note takes that lock away, and the swap completes on
	   the same think - the identical collision that cancels a reload. */
	ReadyToFire();
	testMove.cmd.buttons = 0;
	RunFor( FireNote( WNOTE_BOLT_CLOSED ) - 1, 1 );
	CHECK( testPs.weaponstate == WEAPON_BOLTING );
	CHECK( testPs.weapon == WP_SHOTGUN );

	testMove.cmd.weapon = WP_MACHINEGUN;
	Step( 1 );
	CHECK( testPs.weapon == WP_MACHINEGUN );
	CHECK( testPs.weaponstate == WEAPON_RAISING );
}

static void TestPumpSwapBeforeChamberCosts( void ) {
	/* Same input one think earlier is an ordinary swap: the holster keeps its
	   full time. If this ever passes instantly the window has stopped being a
	   window and the cancel has become a free swap. */
	ReadyToFire();
	testMove.cmd.buttons = 0;
	RunFor( FireNote( WNOTE_BOLT_CLOSED ) - 2, 1 );
	testMove.cmd.weapon = WP_MACHINEGUN;
	Step( 1 );
	CHECK( testPs.weaponstate == WEAPON_DROPPING );
	CHECK( testPs.weapon == WP_SHOTGUN );
	CHECK( testPs.weaponTime == BG_WeaponDropTime( WP_SHOTGUN ) );
}

static void TestSprintCancelsThePump( void ) {
	/* Sprint is a timed action with its own lock, so it collides with the
	   chamber note the same way a holster does and collapses to nothing. */
	ReadyToFire();
	testMove.cmd.buttons = 0;
	RunFor( FireNote( WNOTE_BOLT_CLOSED ) - 1, 1 );
	testPs.pm_flags |= PMF_SPRINTING;
	Step( 1 );
	CHECK( testPs.weaponstate == WEAPON_SPRINTING );
}

static void SwapAwayAndBack( void ) {
	int t;
	testMove.cmd.weapon = WP_MACHINEGUN;
	for ( t = 0; t < SettleTime() &&
	      !(testPs.weapon == WP_MACHINEGUN && testPs.weaponstate == WEAPON_READY); t++ ) Step( 1 );
	CHECK( testPs.weapon == WP_MACHINEGUN && testPs.weaponstate == WEAPON_READY );
	testMove.cmd.weapon = WP_SHOTGUN;
	for ( t = 0; t < SettleTime() &&
	      !( testPs.weapon == WP_SHOTGUN && testPs.weaponstate == WEAPON_READY ); t++ ) {
		Step( 1 );
	}
	CHECK( testPs.weapon == WP_SHOTGUN && testPs.weaponstate == WEAPON_READY );
}

static void TestEarlyCancelCostsTheWholeCycle( void ) {
	/* Swap out BEFORE the action shuts. The round never chambered, so coming
	   back has to work the whole thing again - "the bolt timer resets to full".
	   This is what stops the cancel window being a free swap at any time. */
	ReadyToFire();
	testMove.cmd.buttons = 0;
	RunFor( FireNote( WNOTE_BOLT_CLOSED ) - 2, 1 );
	CHECK( testPs.stats[STAT_UNCHAMBERED] & (1 << WP_SHOTGUN) );
	SwapAwayAndBack();
	CHECK( testPs.stats[STAT_UNCHAMBERED] & (1 << WP_SHOTGUN) );

	shots = 0;
	testMove.cmd.buttons = BUTTON_ATTACK;
	Step( 1 );
	CHECK( shots == 0 );
	CHECK( testPs.weaponstate == WEAPON_BOLTING );
	CHECK( testPs.weaponAnimTime == 0 );		/* from the top, never resumed */

	/* and it is the FULL cycle, not the remainder */
	RunFor( BG_WeaponFireLength( WP_SHOTGUN ) - 1, 1 );
	CHECK( shots == 0 );
	Step( 1 );
	CHECK( shots == 1 );
}

static void TestExactCancelCostsTheWholeCycle( void ) {
	/* The close note releases the holster lock, but completing that holster
	   returns before the chamber commit. Coming back owes the entire cycle. */
	ReadyToFire();
	testMove.cmd.buttons = 0;
	RunFor( FireNote( WNOTE_BOLT_CLOSED ) - 1, 1 );
	testMove.cmd.weapon = WP_MACHINEGUN;
	Step( 1 );
	CHECK( testPs.stats[STAT_UNCHAMBERED] & (1 << WP_SHOTGUN) );
	CHECK( testPs.weapon == WP_MACHINEGUN );	/* free: holster completed */
	CHECK( testPs.weaponstate == WEAPON_RAISING );

	testMove.cmd.weapon = WP_SHOTGUN;
	SwapAwayAndBack();
	shots = 0;
	testMove.cmd.buttons = BUTTON_ATTACK;
	Step( 1 );
	CHECK( shots == 0 && testPs.weaponstate == WEAPON_BOLTING );
	CHECK( testPs.weaponAnimTime == 0 );
	RunFor( BG_WeaponFireLength( WP_SHOTGUN ) - 1, 1 );
	CHECK( shots == 0 );
	Step( 1 );
	CHECK( shots == 1 );
}

static void TestLateCancelKeepsTheRound( void ) {
	/* One think late, chamber completion is already committed. The outgoing
	   holster keeps its real duration and returning does not owe another pump. */
	ReadyToFire();
	testMove.cmd.buttons = 0;
	RunFor( FireNote( WNOTE_BOLT_CLOSED ), 1 );
	testMove.cmd.weapon = WP_MACHINEGUN;
	Step( 1 );
	CHECK( !(testPs.stats[STAT_UNCHAMBERED] & (1 << WP_SHOTGUN)) );
	CHECK( testPs.weapon == WP_SHOTGUN && testPs.weaponstate == WEAPON_DROPPING );
	CHECK( testPs.weaponTime == BG_WeaponDropTime( WP_SHOTGUN ) );
	SwapAwayAndBack();
	shots = 0;
	testMove.cmd.buttons = BUTTON_ATTACK;
	Step( 1 );
	CHECK( shots == 1 );
}

static void TestReloadChambersTheGun( void ) {
	/* The reload's END segment carries the same close note, so finishing a
	   reload shuts the action. Cancel a bolt, reload, and the bolt debt is
	   gone - no code says so, the note just lands. */
	ReadyToFire();
	testMove.cmd.buttons = 0;
	RunFor( FireNote( WNOTE_BOLT_CLOSED ) - 2, 1 );
	CHECK( testPs.stats[STAT_UNCHAMBERED] & (1 << WP_SHOTGUN) );
	SwapAwayAndBack();

	PM_BeginReload();
	RunFor( Seg( WP_SHOTGUN, RSEQ_START ) + Seg( WP_SHOTGUN, RSEQ_LOOP )
	        + Seg( WP_SHOTGUN, RSEQ_END ) + 2, 1 );
	CHECK( !( testPs.stats[STAT_UNCHAMBERED] & (1 << WP_SHOTGUN) ) );
}

static void TestChamberDebtIsPerWeapon( void ) {
	/* Two guns, one of them mid-cycle. The other must be unaffected - the bit
	   is per weapon precisely because you carry two. */
	ReadyToFire();
	testMove.cmd.buttons = 0;
	RunFor( FireNote( WNOTE_BOLT_CLOSED ) - 2, 1 );
	CHECK(  ( testPs.stats[STAT_UNCHAMBERED] & (1 << WP_SHOTGUN)    ) );
	CHECK( !( testPs.stats[STAT_UNCHAMBERED] & (1 << WP_MACHINEGUN) ) );
}

/* Exercise both authored actions through the actual input/slot engine. A
   step is a simulation think: early, exact and late mean one whole think
   either side of the close note, including thinks longer than a millisecond.
   Mode 3 uses two instances of the same base gun, so debt cannot accidentally
   migrate to the newly selected instance when the shared working bit changes. */
static void TestChamberCompletionOrdering( int weapon, int mode, int offset, int step, int perk ) {
	bg_weaponInstance_t original, other;
	int sameBase = mode == 3;
	int close = NoteTime( weapon, ASEQ_FIRE, WNOTE_BOLT_CLOSED );
	int owesCycle = offset <= 0;
	int originalAmmo = BG_WeaponMagSize( weapon ) - 1;
	int otherWeapon = sameBase ? weapon : WP_MACHINEGUN;
	memset( &testPs, 0, sizeof(testPs) );
	memset( &testMove, 0, sizeof(testMove) );
	memset( &pml, 0, sizeof(pml) );
	BG_ApplyLoadout( &testPs, 0 );
	BG_SetSlotWeapon( &testPs, SLOT_PRIMARY, weapon );
	BG_SetSlotWeapon( &testPs, SLOT_SECONDARY, otherWeapon );
	testPs.weapon = weapon;
	testPs.stats[STAT_HEALTH] = 100;
	if ( perk != PERK_NONE ) testPs.perks |= 1 << (perk - 1);
	testMove.ps = &testPs;
	testMove.cmd.weapon = BG_SLOT_COMMAND + SLOT_PRIMARY;
	pm = &testMove;
	shots = 0;
	testMove.cmd.buttons = BUTTON_ATTACK;
	Step( 1 );
	testMove.cmd.buttons = 0;
	CHECK( shots == 1 && testPs.ammo[weapon] == originalAmmo );
	CHECK( close > 2 * step );
	RunFor( close - (1 - offset) * step, 1 );
	if ( mode == 1 ) testPs.pm_flags |= PMF_SPRINTING;
	else if ( mode == 2 ) testPs.stats[STAT_LADDER] = LADDER_ATTACHED;
	else testMove.cmd.weapon = BG_SLOT_COMMAND + SLOT_SECONDARY;
	Step( step );
	BG_GetSlot( &testPs, SLOT_PRIMARY, &original );
	BG_GetSlot( &testPs, SLOT_SECONDARY, &other );
	CHECK( !!(original.flags & BG_GUN_UNCHAMBERED) == owesCycle );
	CHECK( original.ammo == originalAmmo && original.reserve == BG_WeaponMaxReserve( weapon ) );
	CHECK( !(other.flags & BG_GUN_UNCHAMBERED) );
	CHECK( other.ammo == BG_WeaponMagSize( otherWeapon ) );
	CHECK( shots == 1 );
	CHECK( !(testPs.pm_flags & PMF_PENDING_MAG) );
	if ( mode == 1 ) {
		CHECK( testPs.weaponstate == (offset == 0 ? WEAPON_SPRINTING : WEAPON_SPRINT_IN) );
		CHECK( testPs.weaponTime == (offset == 0 ? 0 : BG_WeaponSprintInTime( weapon )) );
		if ( offset != 0 ) RunFor( testPs.weaponTime, 1 );
		testPs.pm_flags &= ~PMF_SPRINTING;
		Step( 1 );
		RunFor( BG_WeaponSprintOutTime( weapon ), 1 );
	} else if ( mode == 2 ) {
		CHECK( testPs.weaponstate == (offset == 0 ? WEAPON_LADDER : WEAPON_DROPPING) );
		CHECK( testPs.weaponTime == (offset == 0 ? 0 : BG_LADDER_DROP_TIME) );
		if ( offset != 0 ) RunFor( testPs.weaponTime, 1 );
		testPs.stats[STAT_LADDER] = 0;
		Step( 1 );
		RunFor( BG_WeaponRaiseTime( weapon ), 1 );
	} else {
		CHECK( testPs.weaponstate == (offset == 0 ? WEAPON_RAISING : WEAPON_DROPPING) );
		CHECK( testPs.weaponTime == (offset == 0 ? BG_WeaponRaiseTime( otherWeapon ) : BG_WeaponDropTime( weapon )) );
		if ( offset != 0 ) RunFor( testPs.weaponTime, 1 );
		RunFor( BG_WeaponRaiseTime( otherWeapon ), 1 );
		CHECK( testPs.weaponstate == WEAPON_READY && testPs.stats[STAT_ACTIVE_SLOT] == SLOT_SECONDARY );
		testMove.cmd.weapon = BG_SLOT_COMMAND + SLOT_PRIMARY;
		Step( 1 );
		RunFor( BG_WeaponDropTime( otherWeapon ) + BG_WeaponRaiseTime( weapon ), 1 );
	}
	CHECK( testPs.weapon == weapon && testPs.stats[STAT_ACTIVE_SLOT] == SLOT_PRIMARY );
	CHECK( testPs.ammo[weapon] == originalAmmo );
	CHECK( !!(testPs.stats[STAT_UNCHAMBERED] & (1 << weapon)) == owesCycle );
	if ( owesCycle ) {
		/* Sprint-out reaches the debt gate on its completion think; a raise
		   reaches READY and returns, so it reaches that gate next think. */
		if ( testPs.weaponstate == WEAPON_READY ) Step( 1 );
		CHECK( testPs.weaponstate == WEAPON_BOLTING && testPs.weaponAnimTime == 0 );
		testMove.cmd.buttons = BUTTON_ATTACK;
		RunFor( BG_WeaponFireLength( weapon ) - 1, 1 );
		CHECK( shots == 1 && testPs.ammo[weapon] == originalAmmo );
	} else {
		CHECK( testPs.weaponstate == WEAPON_READY );
		testMove.cmd.buttons = BUTTON_ATTACK;
	}
	Step( 1 );
	CHECK( shots == 2 && testPs.ammo[weapon] == originalAmmo - 1 );
	BG_GetSlot( &testPs, SLOT_SECONDARY, &other );
	CHECK( !(other.flags & BG_GUN_UNCHAMBERED) && other.ammo == BG_WeaponMagSize( otherWeapon ) );
}

static void ClearWorldTrace( trace_t *tr, const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, int pass, int mask ) {
	(void)start; (void)mins; (void)maxs; (void)pass; (void)mask;
	memset( tr, 0, sizeof(*tr) );
	tr->fraction = 1;
	tr->entityNum = ENTITYNUM_NONE;
	VectorCopy( end, tr->endpos );
}

static int NoWorldContents( const vec3_t point, int pass ) {
	(void)point; (void)pass;
	return 0;
}

static void MoveTick( int msec ) {
	testMove.cmd.serverTime = testPs.commandTime + msec;
	Pmove( &testMove );
}

/* Actual Pmove calls CheckADS before Weapon. Holding aim through an ordinary
   holster clears aim on the intervening thinks, but a completed handoff on
   the close-note think still observes the outgoing gun's aim permission.
   The next movement think sees the raise and clears that permission. */
static void TestAimSwapOrdering( int offset, int ads ) {
	int elapsed;
	bg_weaponInstance_t original;
	ReadyToFire();
	BG_SetSlotWeapon( &testPs, SLOT_SECONDARY, WP_M40A3 );
	testMove.cmd.buttons = ads ? BUTTON_ADS : 0;
	testMove.cmd.weapon = BG_SLOT_COMMAND + SLOT_PRIMARY;
	testMove.trace = ClearWorldTrace;
	testMove.pointcontents = NoWorldContents;
	testMove.tracemask = MASK_PLAYERSOLID;
	testPs.speed = 320;
	testPs.groundEntityNum = ENTITYNUM_NONE;
	for ( elapsed = 0; elapsed < FireNote( WNOTE_BOLT_CLOSED ) - 1 + offset; elapsed++ ) MoveTick( 1 );
	CHECK( !!(testPs.pm_flags & PMF_ADS) == ads );
	testMove.cmd.weapon = BG_SLOT_COMMAND + SLOT_SECONDARY;
	MoveTick( 1 );
	if ( offset != 0 ) {
		CHECK( testPs.weaponstate == WEAPON_DROPPING && testPs.weapon == WP_SHOTGUN );
		for ( elapsed = 1; elapsed < BG_WeaponDropTime( WP_SHOTGUN ); elapsed++ ) MoveTick( 1 );
		CHECK( testPs.weapon == WP_SHOTGUN );
		MoveTick( 1 );
	}
	CHECK( testPs.weapon == WP_M40A3 && testPs.weaponstate == WEAPON_RAISING );
	CHECK( testPs.weaponTime == BG_WeaponRaiseTime( WP_M40A3 ) );
	CHECK( !!(testPs.pm_flags & PMF_ADS) == (offset == 0 && ads) );
	BG_GetSlot( &testPs, SLOT_PRIMARY, &original );
	CHECK( !!(original.flags & BG_GUN_UNCHAMBERED) == (offset <= 0) );
	CHECK( original.ammo == BG_WeaponMagSize( WP_SHOTGUN ) - 1 );
	MoveTick( 1 );
	CHECK( !(testPs.pm_flags & PMF_ADS) );
	MoveTick( 8 );
	CHECK( !(testPs.pm_flags & PMF_ADS) );
}

static void TestUnpumpedWeaponUnchanged( int step ) {
	/* A weapon with no modelled fire animation must behave exactly as before:
	   FIRING for the whole refire, no anim clock, no pump state. */
	int elapsed;
	ReadyToFire();
	BG_SelectSlot(&testPs, SLOT_SECONDARY);
	testPs.weapon = WP_MACHINEGUN;
	testMove.cmd.weapon = WP_MACHINEGUN;
	testPs.weaponstate = WEAPON_READY;
	testPs.weaponTime = 0;
	testPs.weaponAnimTime = -1;
	shots = 0;
	Step( 1 );
	CHECK( shots == 1 );
	CHECK( testPs.weaponstate == WEAPON_FIRING );
	CHECK( testPs.weaponAnimTime == -1 );
	for ( elapsed = 0; elapsed < 90; elapsed += step ) {
		Step( step );
		CHECK( testPs.weaponstate != WEAPON_BOLTING );
	}
}


/* ADS permission belongs to weapon state, not the animation sequence/clock.
   Do not add a fake reload here or relax the real reload gate. */
static void TestADSPermission( void ) {
	int i;
	const int blocked[] = { WEAPON_RELOADING, WEAPON_DROPPING, WEAPON_RAISING,
		WEAPON_SPRINT_IN, WEAPON_SPRINTING, WEAPON_SPRINT_OUT };
	const int allowed[] = { WEAPON_READY, WEAPON_FIRING, WEAPON_BOLTING };
	BeginReload( WP_SHOTGUN, 3, 10 );
	testMove.cmd.buttons = BUTTON_ADS;
	for ( i = 0; i < sizeof(blocked) / sizeof(blocked[0]); i++ ) {
		testPs.weaponstate = blocked[i];
		testPs.pm_flags |= PMF_ADS;
		testPs.weaponTime = 0; /* An expired lock does not grant permission. */
		PM_CheckADS();
		CHECK( !(testPs.pm_flags & PMF_ADS) );
	}
	for ( i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++ ) {
		testPs.weaponstate = allowed[i];
		/* An old animation clock alone must never deny aim permission. */
		testPs.weaponAnimSeq = RSEQ_LOOP;
		testPs.weaponAnimTime = NoteTime( WP_SHOTGUN, RSEQ_LOOP, WNOTE_MAG_IN );
		PM_CheckADS();
		CHECK( testPs.pm_flags & PMF_ADS );
		CHECK( testPs.ammo[WP_SHOTGUN] == 3 );
		CHECK( testPs.ammoReserve[WP_SHOTGUN] == 10 );
	}
	testPs.pm_flags |= PMF_SPRINTING;
	PM_CheckADS();
	CHECK( !(testPs.pm_flags & PMF_ADS) );
	testPs.pm_flags &= ~PMF_SPRINTING;
	testMove.cmd.buttons = 0;
	PM_CheckADS();
	CHECK( !(testPs.pm_flags & PMF_ADS) );
}


/* ---- shoot, reload at once: the reset point, the fastest NAC and the Fayde ----
 * Run for every gun with a worked action (a fire cycle), named by none. All
 * times come from that gun's own tables. */
static int fireGun, otherGun;
static int GunNote( const bg_weaponNote_t *n, int note ) {
	for ( ; n->note != WNOTE_NONE; n++ ) if ( n->note == note ) return n->time;
	return -1;
}
/* MW2's rule, from the gun's own tables: its action length (open to close on
   the fire cycle), on the reload clock, capped by the reload start's first
   round or its end. */
static int ResetTime( int weapon ) {
	int open = GunNote( bg_weaponFire[weapon].notes, WNOTE_BOLT_OPEN );
	int close = GunNote( bg_weaponFire[weapon].notes, WNOTE_BOLT_CLOSED );
	int add = GunNote( PM_SegNotes( weapon, RSEQ_START ), WNOTE_MAG_IN );
	int cap = Seg( weapon, RSEQ_START ), t = close - ( open > 0 ? open : 0 );
	if ( add >= 0 && add < cap ) cap = add;
	return t < cap ? t : cap;
}
static void ShootGun( int weapon ) {
	memset( &testPs, 0, sizeof(testPs) );
	memset( &testMove, 0, sizeof(testMove) );
	memset( &pml, 0, sizeof(pml) );
	fireGun = weapon; otherGun = weapon == WP_MACHINEGUN ? WP_SHOTGUN : WP_MACHINEGUN;
	testMove.ps = &testPs;
	testMove.cmd.weapon = weapon;
	testPs.weapon = weapon;
	testPs.weaponstate = WEAPON_READY;
	testPs.weaponAnimTime = -1;
	testPs.stats[STAT_HEALTH] = 100;
	testPs.stats[STAT_WEAPONS] = (1 << weapon) | (1 << otherGun);
	testPs.stats[STAT_SLOT_PRIMARY] = weapon;
	testPs.stats[STAT_SLOT_SECONDARY] = otherGun;
	testPs.ammo[weapon] = BG_WeaponMagSize( weapon );
	testPs.ammoReserve[weapon] = BG_WeaponMaxReserve( weapon );
	testPs.ammo[otherGun] = BG_WeaponMagSize( otherGun );
	pm = &testMove;
	shots = 0;
	testMove.cmd.buttons = BUTTON_ATTACK;
	Step( 1 );
	CHECK( shots == 1 && testPs.weaponstate == WEAPON_FIRING );
	CHECK( testPs.stats[STAT_UNCHAMBERED] & (1 << weapon) );
	testMove.cmd.buttons = 0;
}
/* Reload on the very next think: it starts at once, the action still owed. */
static void ReloadAtOnce( void ) {
	testMove.cmd.buttons = BUTTON_RELOAD;
	Step( 1 );
	testMove.cmd.buttons = 0;
	CHECK( testPs.weaponstate == WEAPON_RELOADING && testPs.weaponAnimSeq == RSEQ_START );
	CHECK( testPs.weaponAnimTime == 0 );
	CHECK( testPs.ammo[fireGun] == BG_WeaponMagSize( fireGun ) - 1 );
	CHECK( testPs.stats[STAT_UNCHAMBERED] & (1 << fireGun) );
}
static int WeaponsWithAnAction( int *list ) {
	int w, n = 0;
	for ( w = 1; w < WP_NUM_WEAPONS; w++ )
		if ( BG_WeaponFireLength( w ) > 0 && BG_WeaponMagSize( w ) > 0 && !BG_IsActionWeapon( w ) ) list[n++] = w;
	return n;
}
static void TestShootReload( int step ) {
	int list[MAX_WEAPONS], n = WeaponsWithAnAction( list ), i;
	CHECK( n > 0 );
	for ( i = 0; i < n; i++ ) {
		int w = list[i], reset = ResetTime( w ), close = GunNote( bg_weaponFire[w].notes, WNOTE_BOLT_CLOSED );
		/* every worked action's reload carries the point that resets it, and it
		   comes before both the gun's own close and its first round */
		CHECK( reset > 0 && close > 0 );
		CHECK( 1 + reset < close );
		CHECK( reset < Seg( w, RSEQ_START ) + GunNote( PM_SegNotes( w, RSEQ_LOOP ), WNOTE_MAG_IN ) );

		/* Reload is taken during the blast and during the pump/bolt alike. */
		ShootGun( w ); RunFor( 1, step ); ReloadAtOnce();
		ShootGun( w ); RunFor( GunNote( bg_weaponFire[w].notes, WNOTE_BOLT_OPEN ), 1 );
		CHECK( testPs.weaponstate == WEAPON_BOLTING ); ReloadAtOnce();

		/* The fastest NAC: Y on the think the reload resets the action. The
		   swap is instant, but the reload never got to settle the action, so
		   coming back costs the whole pump or bolt from the top. */
		ShootGun( w ); ReloadAtOnce();
		RunFor( reset - 1, 1 );
		CHECK( testPs.stats[STAT_UNCHAMBERED] & (1 << w) );
		testMove.cmd.weapon = otherGun; Step( 1 );
		CHECK( testPs.weapon == otherGun && testPs.weaponstate == WEAPON_RAISING );
		CHECK( testPs.stats[STAT_UNCHAMBERED] & (1 << w) );
		{
			int t;
			testMove.cmd.weapon = w;
			for ( t = 0; t < 10000 && !( testPs.weapon == w && testPs.weaponstate == WEAPON_READY ); t++ ) Step( 1 );
			Step( 1 );	/* the first think at rest finds the debt and works it from the top */
			CHECK( testPs.weapon == w && testPs.weaponstate == WEAPON_BOLTING && testPs.weaponAnimTime == 0 );
			shots = 0; testMove.cmd.buttons = BUTTON_ATTACK;
			RunFor( BG_WeaponFireLength( w ) - 1, 1 );
			CHECK( shots == 0 );
			Step( 1 );
			CHECK( shots == 1 );
			testMove.cmd.buttons = 0;
		}

		/* One think earlier it is an ordinary swap: the window is a window. */
		ShootGun( w ); ReloadAtOnce();
		RunFor( reset - 2, 1 );
		testMove.cmd.weapon = otherGun; Step( 1 );
		CHECK( testPs.weapon == w && testPs.weaponstate == WEAPON_DROPPING );
		CHECK( testPs.weaponTime == BG_WeaponDropTime( w ) );

		/* The Fayde: one think late, YY - the gun is ready, no pump owed, and
		   it fires long before the pump or bolt would have let it. */
		ShootGun( w ); ReloadAtOnce();
		RunFor( reset, 1 );
		testMove.cmd.weapon = otherGun; Step( 1 );
		CHECK( testPs.weaponstate == WEAPON_DROPPING );
		testMove.cmd.weapon = w; Step( 1 );
		CHECK( testPs.weaponstate == WEAPON_READY );
		shots = 0; testMove.cmd.buttons = BUTTON_ATTACK; Step( 1 );
		CHECK( shots == 1 );
		CHECK( 1 + 1 + reset + 3 < BG_WeaponFireLength( w ) );

		/* YY before the reset: the reload is gone and the action still owed,
		   so the whole pump or bolt plays again from the top. */
		ShootGun( w ); ReloadAtOnce();
		RunFor( reset - 3, 1 );
		testMove.cmd.weapon = otherGun; Step( 1 );
		testMove.cmd.weapon = w; Step( 1 );
		CHECK( testPs.weaponstate == WEAPON_BOLTING && testPs.weaponAnimTime == 0 );
		shots = 0; testMove.cmd.buttons = BUTTON_ATTACK;
		RunFor( BG_WeaponFireLength( w ) - 1, 1 );
		CHECK( shots == 0 );
		Step( 1 );
		CHECK( shots == 1 );

		/* Wait out the reset, then YY late: still ready, still no pump. */
		ShootGun( w ); ReloadAtOnce();
		RunFor( reset + Seg( w, RSEQ_START ) / 2, 1 );
		testMove.cmd.weapon = otherGun; Step( 1 );
		testMove.cmd.weapon = w; Step( 1 );
		CHECK( testPs.weaponstate == WEAPON_READY );

		/* An ordinary reload of a chambered gun gains nothing at that point:
		   no lock is released, so Y there is an ordinary holster. */
		ShootGun( w );
		RunFor( BG_WeaponFireLength( w ) + 1, 1 );
		CHECK( testPs.weaponstate == WEAPON_READY && !( testPs.stats[STAT_UNCHAMBERED] & (1 << w) ) );
		testMove.cmd.buttons = BUTTON_RELOAD; Step( 1 ); testMove.cmd.buttons = 0;
		CHECK( testPs.weaponstate == WEAPON_RELOADING );
		RunFor( reset - 1, 1 );
		testMove.cmd.weapon = otherGun; Step( 1 );
		CHECK( testPs.weapon == w && testPs.weaponstate == WEAPON_DROPPING );
		CHECK( testPs.weaponTime == BG_WeaponDropTime( w ) );

		/* Reload is taken out of the pump even after the action has closed. */
		ShootGun( w ); RunFor( close + 1, 1 );
		CHECK( testPs.weaponstate == WEAPON_BOLTING && !( testPs.stats[STAT_UNCHAMBERED] & (1 << w) ) );
		testMove.cmd.buttons = BUTTON_RELOAD; Step( 1 ); testMove.cmd.buttons = 0;
		CHECK( testPs.weaponstate == WEAPON_RELOADING && testPs.weaponAnimSeq == RSEQ_START );

		/* The last round: an empty gun reloads itself out of the pump, not out of the shot. */
		ShootGun( w ); testPs.ammo[w] = 0;
		RunFor( GunNote( bg_weaponFire[w].notes, WNOTE_BOLT_OPEN ) - 1, 1 );
		CHECK( testPs.weaponstate == WEAPON_FIRING );
		Step( 1 );
		if ( testPs.weaponstate == WEAPON_BOLTING ) Step( 1 );
		CHECK( testPs.weaponstate == WEAPON_RELOADING );

		/* Letting the reload run: the reset settles the action, the reload
		   loads, and its own close still lands - ammo is conserved. */
		ShootGun( w ); ReloadAtOnce();
		RunFor( Seg( w, RSEQ_START ) + Seg( w, RSEQ_LOOP ) + Seg( w, RSEQ_END ) + 2, 1 );
		CHECK( testPs.ammo[w] == BG_WeaponMagSize( w ) );
		CHECK( testPs.ammoReserve[w] == BG_WeaponMaxReserve( w ) - 1 );
		CHECK( !( testPs.stats[STAT_UNCHAMBERED] & (1 << w) ) );
	}
}

int main( void ) {
	int i, offset, weapon, mode, perk;
	const int steps[] = { 1, 8, 16, 33, 66 };
	for ( i = 0; i < 5; i++ ) {
		TestAttackInterrupt( steps[i] );
		TestEmptyTube( steps[i] );
		TestReleasedAttack( steps[i] );
		TestNaturalEnd( 7, 10, steps[i] );
		TestNaturalEnd( 3, 1, steps[i] );
		TestMagazineUnchanged( steps[i] );
		TestPumpIsItsOwnState( steps[i] );
		TestUnpumpedWeaponUnchanged( steps[i] );
		TestShootReload( steps[i] );
	}
	TestADSPermission();
	TestPumpCannotBeShortCircuited();
	TestEarlyCancelCostsTheWholeCycle();
	TestExactCancelCostsTheWholeCycle();
	TestLateCancelKeepsTheRound();
	TestReloadChambersTheGun();
	TestChamberDebtIsPerWeapon();
	TestPumpCancelIsFree();
	TestPumpSwapBeforeChamberCosts();
	TestSprintCancelsThePump();
	for ( offset = -1; offset <= 1; offset++ ) {
		TestSwapSprintPreserved( 0, offset );
		TestSwapSprintPreserved( 1, offset );
		TestAimSwapOrdering( offset, 0 );
		TestAimSwapOrdering( offset, 1 );
	}
	for ( weapon = 0; weapon < 2; weapon++ )
		for ( mode = 0; mode < 4; mode++ )
			for ( offset = -1; offset <= 1; offset++ )
				for ( i = 0; i < ARRAY_LEN(steps); i++ )
					for ( perk = 0; perk < 2; perk++ )
						TestChamberCompletionOrdering( weapon ? WP_M40A3 : WP_SHOTGUN,
							mode, offset, steps[i], perk ? PERK_QUICK_RELOAD : PERK_NONE );
	printf( "PASS: reload interruption, ammo conservation, magazine reload, swap/sprint ordering, chamber debt, the fire cycle and shoot-reload resets (%d checks).\n", checks );
	return 0;
}
