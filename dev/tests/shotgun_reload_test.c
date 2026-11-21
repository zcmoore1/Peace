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

/* MW2's fire cycle, counted in thinks of 'step' after the shot's own think
 * (think 0). The shot's clock stops at the open note, and its last think ends
 * READY owing the action (ShouldBeFiring, iw4x 0x576D99-0x576DA3); the action
 * starts from READY on the next think, at the open note (CheckForRechamber,
 * 0x575B77-0x575BA9: rechamberTime, the fire clip after its open note), is
 * bolted on the think the clock crosses the close note (the bolt delay,
 * 0x575BD8-0x575C03, taken at 0x575ABD-0x575ACC) and ends READY on the think
 * it reaches the clip's length. */
static int Thinks( int ms, int step ) {
	return ( ms + step - 1 ) / step;
}
static int ShotEndThink( int weapon, int step ) {
	return Thinks( NoteTime( weapon, ASEQ_FIRE, WNOTE_BOLT_OPEN ), step );
}
static int CloseThink( int weapon, int step ) {
	return ShotEndThink( weapon, step ) + 1 + Thinks( NoteTime( weapon, ASEQ_FIRE, WNOTE_BOLT_CLOSED ) -
		NoteTime( weapon, ASEQ_FIRE, WNOTE_BOLT_OPEN ), step );
}
static int CycleEndThink( int weapon, int step ) {
	return ShotEndThink( weapon, step ) + 1 + Thinks( BG_WeaponFireLength( weapon ) -
		NoteTime( weapon, ASEQ_FIRE, WNOTE_BOLT_OPEN ), step );
}
/* The action alone, worked from READY: rechamberTime. */
static int ActionLength( int weapon ) {
	return BG_WeaponFireLength( weapon ) - NoteTime( weapon, ASEQ_FIRE, WNOTE_BOLT_OPEN );
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

/* ---- the trigger and a segmented reload ----
 * MW2 asks the reload to stop and lets the segment it is in play out, its
 * round included: a fresh press asks in the loop at once and in the start only
 * past 0.4 of it (PM_Weapon_CheckForReload, iw4x 0x578750-0x5787AD), and a
 * trigger down as the start or a pass ends asks too (FinishReloadStart
 * 0x5783DE-0x5783FC, FinishReload 0x578564-0x578582). Then the end plays, and
 * a trigger still held fires on the reload's last command (the owner's
 * choice; MW2 fires the think after). Each segment starts afresh on the think
 * the one before it ended (MW2's crossing drops the overshoot, 0x578B71, and
 * PM_SetReloadingState gives the next its whole time, 0x575E20): it ends on
 * the first think at or past its start plus its length. */
static int reloadElapsed;	/* ms of reload run since BeginReload */
static void ReloadStep( int msec ) {
	Step( msec );
	reloadElapsed += msec;
}
static void ReloadTo( int elapsed ) {
	while ( reloadElapsed < elapsed ) ReloadStep( 1 );
}
/* The thinks run 1ms apart up to 'fine' and 'step' apart after it: the first
   one at or past 't'. */
static int ThinkAt( int t, int fine, int step ) {
	return t <= fine ? t : fine + ( t - fine + step - 1 ) / step * step;
}
/* Where the end begins after the start and 'passes' passes, on those thinks. */
static int EndBegins( int passes, int fine, int step ) {
	int t = ThinkAt( Seg( WP_SHOTGUN, RSEQ_START ), fine, step );
	while ( passes-- > 0 ) t = ThinkAt( t + Seg( WP_SHOTGUN, RSEQ_LOOP ), fine, step );
	return t;
}
/* The reload stops after the start and 'passes' passes with 'loaded' shells,
   plays its end, and the trigger, held from now on, fires on its last think. */
static void StopsAndFires( int passes, int loaded, int reserve, int step ) {
	const int stop = EndBegins( passes, reloadElapsed, step );
	const int end = ThinkAt( stop + Seg( WP_SHOTGUN, RSEQ_END ), reloadElapsed, step );
	testMove.cmd.buttons = BUTTON_ATTACK;
	while ( reloadElapsed < end ) {
		CHECK( shots == 0 && testPs.weaponstate == WEAPON_RELOADING );
		ReloadStep( step );
		if ( reloadElapsed < stop ) {
			CHECK( testPs.weaponstate == WEAPON_RELOADING && testPs.weaponAnimSeq != RSEQ_END );
		} else if ( reloadElapsed < end ) {
			CHECK( testPs.weaponstate == WEAPON_RELOADING && testPs.weaponAnimSeq == RSEQ_END );
			CHECK( testPs.ammo[WP_SHOTGUN] == loaded && testPs.ammoReserve[WP_SHOTGUN] == reserve );
		}
	}
	CHECK( shots == 1 && testPs.weaponstate == WEAPON_FIRING );
	CHECK( testPs.ammo[WP_SHOTGUN] == loaded - 1 && testPs.ammoReserve[WP_SHOTGUN] == reserve );
	testMove.cmd.buttons = 0;
}

/* The trigger pressed at any point of the start or the first pass and held:
   the segment it is in plays out (the pass's shell goes in), then the end. */
static void TestAttackInterrupt( int step ) {
	const int start = Seg( WP_SHOTGUN, RSEQ_START ), loop = Seg( WP_SHOTGUN, RSEQ_LOOP );
	const int seats = ShellSeats( 1 ), i0 = 1;
	/* the start's first think, its 0.4 and just past, the pass before its
	   shell, the shell's own think and after it */
	const int times[] = { i0, start * 2 / 5, start * 2 / 5 + 1, start - 1,
		( start + seats ) / 2, seats - 1, seats, seats + 1, start + loop - 1 };
	int i;
	for ( i = 0; i < ARRAY_LEN( times ); i++ ) {
		qboolean inStart = times[i] < start;
		BeginReload( WP_SHOTGUN, 3, 10 );
		reloadElapsed = 0;
		ReloadTo( times[i] - 1 );
		CHECK( testPs.weaponstate == WEAPON_RELOADING && testPs.weaponAnimSeq == ( inStart ? RSEQ_START : RSEQ_LOOP ) );
		StopsAndFires( inStart ? 0 : 1, inStart ? 3 : 4, inStart ? 10 : 9, step );
	}
}

/* An empty tube, the trigger held from the start: the start plays out, and as
   it has no round to fire, one pass (MW2's PM_SetReloadingState keeps the
   request, 0x575E3B-0x575E4A), then the end. */
static void TestEmptyTube( int step ) {
	BeginReload( WP_SHOTGUN, 0, 10 );
	reloadElapsed = 0;
	StopsAndFires( 1, 1, 9, step );
}

/* A tap, let go at once: in the start's first 0.4 it is forgotten and the
   reload fills the tube; past it, the start plays out and the end follows; in
   a pass, the pass plays out with its shell. It fires nothing. */
static void TestReleasedAttack( int step ) {
	const int start = Seg( WP_SHOTGUN, RSEQ_START ), loop = Seg( WP_SHOTGUN, RSEQ_LOOP );
	const int taps[] = { step, start * 2 / 5, start * 2 / 5 + 1, start - 1, start + 1, start + loop - 1 };
	int i;
	for ( i = 0; i < ARRAY_LEN( taps ); i++ ) {
		qboolean asked = taps[i] * 5 > start * 2;
		int passes = !asked ? BG_WeaponMagSize( WP_SHOTGUN ) - 3 : taps[i] < start ? 0 : 1;
		int loaded = !asked ? BG_WeaponMagSize( WP_SHOTGUN ) : taps[i] < start ? 3 : 4;
		int stop, end;
		BeginReload( WP_SHOTGUN, 3, 10 );
		reloadElapsed = 0;
		ReloadTo( taps[i] - 1 );
		testMove.cmd.buttons = BUTTON_ATTACK;
		ReloadStep( 1 );
		testMove.cmd.buttons = 0;
		stop = EndBegins( passes, reloadElapsed, step );
		end = ThinkAt( stop + Seg( WP_SHOTGUN, RSEQ_END ), reloadElapsed, step );
		while ( reloadElapsed < end ) {
			CHECK( testPs.weaponstate == WEAPON_RELOADING );
			CHECK( ( testPs.weaponAnimSeq == RSEQ_END ) == ( reloadElapsed >= stop ) );
			ReloadStep( step );
		}
		CHECK( testPs.weaponstate == WEAPON_READY && shots == 0 );
		CHECK( testPs.ammo[WP_SHOTGUN] == loaded && testPs.ammoReserve[WP_SHOTGUN] == 10 - ( loaded - 3 ) );
	}
	/* The start's 0.4 is of its own length: a press on its first think is
	   below it at every rate a client sends. */
	CHECK( 66 * 5 <= start * 2 );
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
	int open = FireNote( WNOTE_BOLT_OPEN ), t;
	ReadyToFire();
	testMove.cmd.buttons = 0;

	/* The shot itself, to the think before its last. */
	for ( t = 1; t < ShotEndThink( WP_SHOTGUN, step ); t++ ) Step( step );
	CHECK( testPs.weaponstate == WEAPON_FIRING && testPs.weaponAnimTime < open );

	/* Its last think ends READY, owing the action, the clock at the open
	   note (MW2's ShouldBeFiring): nothing of the action yet. */
	Step( step );
	CHECK( testPs.weaponstate == WEAPON_READY && testPs.weaponTime == 0 );
	CHECK( testPs.weaponAnimTime == open && BG_ShotAwaitsAction( &testPs ) );
	CHECK( testPs.stats[STAT_UNCHAMBERED] & (1 << WP_SHOTGUN) );

	/* Then the action starts from READY at the open note (MW2's
	   CheckForRechamber), and that is NOT firing. */
	Step( step );
	CHECK( testPs.weaponstate == WEAPON_BOLTING && !BG_ShotAwaitsAction( &testPs ) );
	CHECK( testPs.weaponAnimTime == open && testPs.weaponTime == ActionLength( WP_SHOTGUN ) );
	for ( t = ShotEndThink( WP_SHOTGUN, step ) + 2; t < CycleEndThink( WP_SHOTGUN, step ); t++ ) {
		Step( step );
		CHECK( testPs.weaponstate == WEAPON_BOLTING );
		CHECK( !!( testPs.stats[STAT_UNCHAMBERED] & (1 << WP_SHOTGUN) ) == ( t < CloseThink( WP_SHOTGUN, step ) ) );
	}

	/* Only when the whole action has played does the gun come back up. */
	Step( step );
	CHECK( testPs.weaponstate == WEAPON_READY );
	CHECK( testPs.weaponAnimTime == -1 );
}

/* A frozen player's shot (MW2's PMF_FROZEN): MW2's hand step returns before
   ShouldBeFiring (0x579505), so a shot that runs out frozen stays FIRING,
   over, and its action never starts (CheckForRechamber starts it only from
   READY, 0x575B77); the first think the freeze has lifted ends it READY, and
   the next starts the action from READY at the open note. */
static void TestFrozenShotEnd( int step ) {
	int open = FireNote( WNOTE_BOLT_OPEN ), t;
	ReadyToFire();
	testMove.cmd.buttons = 0;
	testPs.pm_flags |= PMF_FROZEN;
	for ( t = 1; t < ShotEndThink( WP_SHOTGUN, step ) + 5; t++ ) {
		Step( step );
		CHECK( testPs.weaponstate == WEAPON_FIRING && !BG_ShotAwaitsAction( &testPs ) );
		CHECK( testPs.stats[STAT_UNCHAMBERED] & (1 << WP_SHOTGUN) );
	}
	CHECK( testPs.weaponAnimTime == open );
	testPs.pm_flags &= ~PMF_FROZEN;
	Step( step );
	CHECK( testPs.weaponstate == WEAPON_READY && BG_ShotAwaitsAction( &testPs ) && testPs.weaponTime == 0 );
	Step( step );
	CHECK( testPs.weaponstate == WEAPON_BOLTING && testPs.weaponAnimTime == open );
	CHECK( testPs.weaponTime == ActionLength( WP_SHOTGUN ) && shots == 1 );

	/* The trigger down as the freeze lifts: the shot ran out frozen, where
	   MW2's command keeps no trigger, so that think's lock found it up and the
	   pull was over (WeaponTimeAdjust 0x578B51-0x578B67); unfrozen, the gun is
	   still FIRING and its time has run out, and MW2's ShouldBeFiring fires it
	   again (0x576D3F-0x576D62), the action still owed: CheckForRechamber
	   starts one only from READY (0x575B77). (PmoveSingle masks the frozen
	   command; this test drives PM_Weapon alone, so it holds the trigger up.) */
	ReadyToFire();
	testMove.cmd.buttons = 0;
	testPs.pm_flags |= PMF_FROZEN;
	for ( t = 1; t < ShotEndThink( WP_SHOTGUN, step ) + 5; t++ ) Step( step );
	CHECK( testPs.weaponstate == WEAPON_FIRING && testPs.weaponShotCount == 0 && shots == 1 );
	testPs.pm_flags &= ~PMF_FROZEN;
	testMove.cmd.buttons = BUTTON_ATTACK;
	Step( step );
	CHECK( shots == 2 && testPs.weaponstate == WEAPON_FIRING && testPs.weaponAnimTime == 0 );
	CHECK( ( testPs.stats[STAT_UNCHAMBERED] & (1 << WP_SHOTGUN) ) && testPs.weaponShotCount == 1 );
	testMove.cmd.buttons = 0;
}

static void TestPumpCannotBeShortCircuited( void ) {
	int t, heldEnd = ShotEndThink( WP_SHOTGUN, 1 ) + Thinks( ActionLength( WP_SHOTGUN ), 1 );
	/* Holding fire must not produce a second shot before the cycle is over -
	   the chamber note gives the lock away partway through, so if anything
	   were gated on the lock alone this is where it would double-fire. Nor
	   after it: the SPAS is single shot (spas12_mp), and MW2's lock holds a
	   gun whose trigger is still down from its shot READY with weaponTime 1
	   (WeaponTimeAdjust, iw4x 0x5789DD-0x578AE6), its pump started from that
	   READY on the shot's last think. The pump ends READY under the lock, and
	   only a release and a new pull fire again. */
	ReadyToFire();
	for ( t = 1; t < heldEnd + 3 * Thinks( BG_WeaponFireLength( WP_SHOTGUN ), 1 ); t++ ) {
		Step( 1 );
		CHECK( shots == 1 );
		if ( t >= heldEnd ) CHECK( testPs.weaponstate == WEAPON_READY && testPs.weaponTime == 1 );
	}
	testMove.cmd.buttons = 0;
	Step( 1 );
	CHECK( shots == 1 && testPs.weaponstate == WEAPON_READY && testPs.weaponTime == 0 );
	testMove.cmd.buttons = BUTTON_ATTACK;
	Step( 1 );
	CHECK( shots == 2 );
}

static void TestPumpCancelIsFree( void ) {
	/* Swap requested on the think the round chambers. The holster stamps its
	   normal time, the note takes that lock away, and the swap completes on
	   the same think - the identical collision that cancels a reload. */
	ReadyToFire();
	testMove.cmd.buttons = 0;
	RunFor( CloseThink( WP_SHOTGUN, 1 ) - 1, 1 );
	CHECK( testPs.weaponstate == WEAPON_BOLTING );
	CHECK( testPs.weapon == WP_SHOTGUN );
	CHECK( testPs.weaponAnimTime + 1 == FireNote( WNOTE_BOLT_CLOSED ) );

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
	RunFor( CloseThink( WP_SHOTGUN, 1 ) - 2, 1 );
	testMove.cmd.weapon = WP_MACHINEGUN;
	Step( 1 );
	CHECK( testPs.weaponstate == WEAPON_DROPPING );
	CHECK( testPs.weapon == WP_SHOTGUN );
	CHECK( testPs.weaponTime == BG_WeaponDropTime( WP_SHOTGUN ) );
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
	RunFor( CloseThink( WP_SHOTGUN, 1 ) - 2, 1 );
	CHECK( testPs.stats[STAT_UNCHAMBERED] & (1 << WP_SHOTGUN) );
	SwapAwayAndBack();
	CHECK( testPs.stats[STAT_UNCHAMBERED] & (1 << WP_SHOTGUN) );

	shots = 0;
	testMove.cmd.buttons = BUTTON_ATTACK;
	Step( 1 );
	CHECK( shots == 0 );
	CHECK( testPs.weaponstate == WEAPON_BOLTING );
	/* from the action's own top, the open note, never resumed; the shot is
	   not played again (MW2's rechamber from READY) */
	CHECK( testPs.weaponAnimTime == FireNote( WNOTE_BOLT_OPEN ) );
	CHECK( testPs.weaponTime == ActionLength( WP_SHOTGUN ) );

	/* and it is the WHOLE action, not the remainder */
	RunFor( ActionLength( WP_SHOTGUN ) - 1, 1 );
	CHECK( shots == 0 );
	Step( 1 );
	CHECK( shots == 1 );
}

static void TestExactCancelCostsTheWholeCycle( void ) {
	/* The close note releases the holster lock, but completing that holster
	   returns before the chamber commit. Coming back owes the entire cycle. */
	ReadyToFire();
	testMove.cmd.buttons = 0;
	RunFor( CloseThink( WP_SHOTGUN, 1 ) - 1, 1 );
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
	CHECK( testPs.weaponAnimTime == FireNote( WNOTE_BOLT_OPEN ) );
	RunFor( ActionLength( WP_SHOTGUN ) - 1, 1 );
	CHECK( shots == 0 );
	Step( 1 );
	CHECK( shots == 1 );
}

static void TestLateCancelKeepsTheRound( void ) {
	/* One think late, chamber completion is already committed. The outgoing
	   holster keeps its real duration and returning does not owe another pump. */
	ReadyToFire();
	testMove.cmd.buttons = 0;
	RunFor( CloseThink( WP_SHOTGUN, 1 ), 1 );
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
	RunFor( CloseThink( WP_SHOTGUN, 1 ) - 2, 1 );
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
	RunFor( CloseThink( WP_SHOTGUN, 1 ) - 2, 1 );
	CHECK(  ( testPs.stats[STAT_UNCHAMBERED] & (1 << WP_SHOTGUN)    ) );
	CHECK( !( testPs.stats[STAT_UNCHAMBERED] & (1 << WP_MACHINEGUN) ) );
}

/* Exercise both authored actions through the actual input/slot engine. A
   step is a simulation think: early, exact and late mean one whole think
   either side of the close note, including thinks longer than a millisecond.
   Mode 3 uses two instances of the same base gun, so debt cannot accidentally
   migrate to the newly selected instance when the shared working bit changes.
   Mode 1, the sprint, is not a transition out of the action at all (MW2's
   PM_Weapon_CheckForSprint returns on RECHAMBERING): the gun keeps working
   it, the close note commits on its own think, and the gun lowers only once
   the cycle is over, so nothing is owed whichever think the sprint began on. */
static void TestChamberCompletionOrdering( int weapon, int mode, int offset, int step, int perk ) {
	bg_weaponInstance_t original, other;
	int sameBase = mode == 3;
	int close = NoteTime( weapon, ASEQ_FIRE, WNOTE_BOLT_CLOSED );
	int debtAfterPress = mode == 1 ? offset < 0 : offset <= 0;
	int owesCycle = mode == 1 ? 0 : offset <= 0;
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
	CHECK( close - step > NoteTime( weapon, ASEQ_FIRE, WNOTE_BOLT_OPEN ) );
	/* 1ms thinks into the action: the shot's READY think, then the action's
	   clock from the open note, so the press think's clock ends early on the
	   think before the close note's, exact on it, late on the one after. */
	RunFor( close - (1 - offset) * step + 1, 1 );
	CHECK( testPs.weaponstate == WEAPON_BOLTING && testPs.weaponAnimTime == close - (1 - offset) * step );
	if ( mode == 1 ) testPs.pm_flags |= PMF_SPRINTING;
	else if ( mode == 2 ) testPs.stats[STAT_LADDER] = LADDER_ATTACHED;
	else testMove.cmd.weapon = BG_SLOT_COMMAND + SLOT_SECONDARY;
	Step( step );
	BG_GetSlot( &testPs, SLOT_PRIMARY, &original );
	BG_GetSlot( &testPs, SLOT_SECONDARY, &other );
	CHECK( !!(original.flags & BG_GUN_UNCHAMBERED) == debtAfterPress );
	CHECK( original.ammo == originalAmmo && original.reserve == BG_WeaponMaxReserve( weapon ) );
	CHECK( !(other.flags & BG_GUN_UNCHAMBERED) );
	CHECK( other.ammo == BG_WeaponMagSize( otherWeapon ) );
	CHECK( shots == 1 );
	CHECK( !(testPs.pm_flags & PMF_PENDING_MAG) );
	if ( mode == 1 ) {
		int t;
		CHECK( testPs.weaponstate == WEAPON_BOLTING );
		for ( t = 0; t < BG_WeaponFireLength( weapon ) && testPs.weaponstate == WEAPON_BOLTING; t += step )
			Step( step );
		/* The cycle ends READY; the next think lowers it with the whole entry. */
		CHECK( testPs.weaponstate == WEAPON_READY && testPs.weaponAnimTime == -1 );
		CHECK( !(testPs.stats[STAT_UNCHAMBERED] & (1 << weapon)) );
		Step( step );
		CHECK( testPs.weaponstate == WEAPON_SPRINT_IN );
		CHECK( testPs.weaponTime == BG_WeaponSprintInTime( weapon ) );
		RunFor( testPs.weaponTime, 1 );
		CHECK( testPs.weaponstate == WEAPON_SPRINTING );
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
		/* Only a swap or a ladder owes it here (a sprint waited for the
		   cycle). A raise reaches READY and returns, so it reaches the debt
		   gate on the next think. */
		if ( testPs.weaponstate == WEAPON_READY ) Step( 1 );
		CHECK( testPs.weaponstate == WEAPON_BOLTING &&
			testPs.weaponAnimTime == NoteTime( weapon, ASEQ_FIRE, WNOTE_BOLT_OPEN ) );
		CHECK( testPs.weaponTime == ActionLength( weapon ) );
		testMove.cmd.buttons = BUTTON_ATTACK;
		RunFor( ActionLength( weapon ) - 1, 1 );
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
	testPs.speed = BG_PLAYER_SPEED;
	testPs.groundEntityNum = ENTITYNUM_NONE;
	for ( elapsed = 0; elapsed < CloseThink( WP_SHOTGUN, 1 ) - 1 + offset; elapsed++ ) MoveTick( 1 );
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
	/* The owner's NAC frame: the aim the SPAS earned is on the incoming M300,
	   a scoped gun, so that frame is drawn through its scope. The shared rule
	   already refuses the raise, which is what clears it on the next update. */
	CHECK( BG_WeaponScopeOverlay( testPs.weapon ) && !BG_CanAimDownSights( &testPs ) );
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
   Do not add a fake reload here or relax the real reload gate. The sprint's
   weapon states allow it, as MW2's ADS check does: the sprint flag, which the
   aim button clears first, is what refuses aim. */
static void TestADSPermission( void ) {
	int i;
	const int blocked[] = { WEAPON_RELOADING, WEAPON_DROPPING, WEAPON_RAISING };
	const int allowed[] = { WEAPON_READY, WEAPON_FIRING, WEAPON_BOLTING,
		WEAPON_SPRINT_IN, WEAPON_SPRINTING, WEAPON_SPRINT_OUT };
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
	testPs.stats[STAT_SLOT_PRIMARY] = BG_BaseWeapon(weapon);
	if (BG_BaseWeapon(weapon) != weapon) testPs.slotFlags[SLOT_PRIMARY] = BG_GUN_ALTERNATE;
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
/* Reload on the very next think: it starts at once, the action still owed -
   unless the action's own close note falls on that think, which settles it
   first (a gun whose bolt is 1ms into its action, as MW2's Masterkey). */
static void ReloadAtOnce( void ) {
	int close = GunNote( bg_weaponFire[fireGun].notes, WNOTE_BOLT_CLOSED );
	qboolean owed = ( testPs.stats[STAT_UNCHAMBERED] & (1 << fireGun) ) &&
		!( testPs.weaponstate == WEAPON_BOLTING && testPs.weaponAnimSeq == ASEQ_FIRE &&
		   testPs.weaponAnimTime < close && testPs.weaponAnimTime + 1 >= close );
	testMove.cmd.buttons = BUTTON_RELOAD;
	Step( 1 );
	testMove.cmd.buttons = 0;
	CHECK( testPs.weaponstate == WEAPON_RELOADING && testPs.weaponAnimSeq == RSEQ_START );
	CHECK( testPs.weaponAnimTime == 0 );
	CHECK( testPs.ammo[fireGun] == BG_WeaponMagSize( fireGun ) - 1 );
	CHECK( !!( testPs.stats[STAT_UNCHAMBERED] & (1 << fireGun) ) == owed );
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

		/* Reload is taken during the blast, on its last think, on the READY
		   think after it and during the pump/bolt alike (MW2's reload check
		   takes FIRING, READY and RECHAMBERING, 0x575E8E-0x575EA8). */
		ShootGun( w ); RunFor( 1, step ); ReloadAtOnce();
		ShootGun( w ); RunFor( ShotEndThink( w, 1 ) - 1, 1 );
		CHECK( testPs.weaponstate == WEAPON_FIRING ); ReloadAtOnce();
		ShootGun( w ); RunFor( ShotEndThink( w, 1 ), 1 );
		CHECK( testPs.weaponstate == WEAPON_READY && BG_ShotAwaitsAction( &testPs ) ); ReloadAtOnce();
		ShootGun( w ); RunFor( ShotEndThink( w, 1 ) + 1, 1 );
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
			Step( 1 );	/* the first think at rest finds the debt and works the action from its top */
			CHECK( testPs.weapon == w && testPs.weaponstate == WEAPON_BOLTING &&
				testPs.weaponAnimTime == GunNote( bg_weaponFire[w].notes, WNOTE_BOLT_OPEN ) );
			CHECK( testPs.weaponTime == ActionLength( w ) );
			shots = 0; testMove.cmd.buttons = BUTTON_ATTACK;
			RunFor( ActionLength( w ) - 1, 1 );
			CHECK( shots == 0 );
			Step( 1 );
			CHECK( shots == 1 );
			testMove.cmd.buttons = 0;
		}

		/* One think earlier it is an ordinary swap: the window is a window.
		   (A reset on the reload's first think has no earlier think: a swap
		   already waiting when the reload starts always meets it. That is the
		   whole of MW2's Moni, and dev/tests/moni_test.c presses it.) */
		if ( reset > 1 ) {
			ShootGun( w ); ReloadAtOnce();
			RunFor( reset - 2, 1 );
			testMove.cmd.weapon = otherGun; Step( 1 );
			CHECK( testPs.weapon == w && testPs.weaponstate == WEAPON_DROPPING );
			CHECK( testPs.weaponTime == BG_WeaponDropTime( w ) );
		}

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
		if ( reset > 2 ) {
			ShootGun( w ); ReloadAtOnce();
			RunFor( reset - 3, 1 );
			testMove.cmd.weapon = otherGun; Step( 1 );
			testMove.cmd.weapon = w; Step( 1 );
			CHECK( testPs.weaponstate == WEAPON_BOLTING &&
				testPs.weaponAnimTime == GunNote( bg_weaponFire[w].notes, WNOTE_BOLT_OPEN ) );
			shots = 0; testMove.cmd.buttons = BUTTON_ATTACK;
			RunFor( ActionLength( w ) - 1, 1 );
			CHECK( shots == 0 );
			Step( 1 );
			CHECK( shots == 1 );
			testMove.cmd.buttons = 0;
		}

		/* Wait out the reset, then YY late: still ready, still no pump. */
		ShootGun( w ); ReloadAtOnce();
		RunFor( reset + Seg( w, RSEQ_START ) / 2, 1 );
		testMove.cmd.weapon = otherGun; Step( 1 );
		testMove.cmd.weapon = w; Step( 1 );
		CHECK( testPs.weaponstate == WEAPON_READY );

		/* An ordinary reload of a chambered gun gains nothing at that point:
		   no lock is released, so Y there is an ordinary holster. */
		ShootGun( w );
		RunFor( CycleEndThink( w, 1 ), 1 );
		CHECK( testPs.weaponstate == WEAPON_READY && !( testPs.stats[STAT_UNCHAMBERED] & (1 << w) ) );
		testMove.cmd.buttons = BUTTON_RELOAD; Step( 1 ); testMove.cmd.buttons = 0;
		CHECK( testPs.weaponstate == WEAPON_RELOADING );
		RunFor( reset - 1, 1 );
		testMove.cmd.weapon = otherGun; Step( 1 );
		CHECK( testPs.weapon == w && testPs.weaponstate == WEAPON_DROPPING );
		CHECK( testPs.weaponTime == BG_WeaponDropTime( w ) );

		/* Reload is taken out of the pump even after the action has closed. */
		ShootGun( w ); RunFor( CloseThink( w, 1 ), 1 );
		CHECK( testPs.weaponstate == WEAPON_BOLTING && !( testPs.stats[STAT_UNCHAMBERED] & (1 << w) ) );
		testMove.cmd.buttons = BUTTON_RELOAD; Step( 1 ); testMove.cmd.buttons = 0;
		CHECK( testPs.weaponstate == WEAPON_RELOADING && testPs.weaponAnimSeq == RSEQ_START );

		/* The last round: an empty gun reloads itself once the shot is over,
		   never out of the shot. Its last think ends READY owing the action
		   (MW2's auto reload refuses FIRING, 0x578845-0x57887A); the next
		   reloads before the action can start (CheckForReload runs before
		   CheckForRechamber), the action still owed. */
		ShootGun( w ); testPs.ammo[w] = 0;
		RunFor( ShotEndThink( w, 1 ) - 1, 1 );
		CHECK( testPs.weaponstate == WEAPON_FIRING );
		Step( 1 );
		CHECK( testPs.weaponstate == WEAPON_READY && ( testPs.stats[STAT_UNCHAMBERED] & (1 << w) ) );
		Step( 1 );
		CHECK( testPs.weaponstate == WEAPON_RELOADING && testPs.weaponAnimSeq == RSEQ_START );
		CHECK( testPs.weaponAnimTime == 0 && ( testPs.stats[STAT_UNCHAMBERED] & (1 << w) ) );

		/* Letting the reload run: the reset settles the action, the reload
		   loads, and its own close still lands - ammo is conserved. */
		ShootGun( w ); ReloadAtOnce();
		RunFor( Seg( w, RSEQ_START ) + Seg( w, RSEQ_LOOP ) + Seg( w, RSEQ_END ) + 2, 1 );
		CHECK( testPs.ammo[w] == BG_WeaponMagSize( w ) );
		CHECK( testPs.ammoReserve[w] == BG_WeaponMaxReserve( w ) - 1 );
		CHECK( !( testPs.stats[STAT_UNCHAMBERED] & (1 << w) ) );
	}
}

/* ---- a sprint over the shot and the action ----
 * MW2's PM_Weapon_CheckForSprint (iw4x 0x578060) returns while the gun is
 * FIRING or RECHAMBERING, and its sprint gates never test those states: the
 * sprint runs and the gun keeps what it is doing. MW2's shot ends READY with
 * the action owed at the end of its last think (ShouldBeFiring,
 * 0x576D99-0x576DA3), and on the next think the sprint check comes before
 * CheckForRechamber (0x575a30, which starts the action only from READY). So:
 *  - a sprint on by that READY think - begun in the shot, on its last think
 *    or on the READY think itself - takes the gun there, and the action is
 *    still owed: once the gun is back up it works ALL of it (a deferral that
 *    costs the whole action, never a cancel). The shot's last think, still
 *    FIRING, does not lower.
 *  - a sprint begun once the action is under way waits for it - in the
 *    action, on the think before the close note, on the close think (the
 *    owner's "bolt NAC into a sprint") or on the think after it - and the gun
 *    lowers chambered, owing nothing: the close note never meets a sprint lock. */
enum { SP_SHOT, SP_SHOT_END, SP_SHOT_READY, SP_ACTION, SP_BEFORE_CLOSE, SP_ON_CLOSE, SP_AFTER_CLOSE, SP_COUNT };
/* One think: PM_Weapon alone with the flag set, or the whole Pmove with a
   real sprint press, where PM_CheckSprint raises the flag itself. */
static void CycleThink( int full, int step ) {
	int sequence = testPs.eventSequence;
	if ( !full ) {
		Step( step );
		return;
	}
	MoveTick( step );
	for ( ; sequence < testPs.eventSequence; sequence++ ) {
		if ( testPs.events[sequence & (MAX_PS_EVENTS - 1)] == EV_FIRE_WEAPON ) {
			shots++;
		}
	}
}
/* The sprint ends (forward let go), the gun rises, and - if the action is
   still owed - works the whole of it from its open note before the trigger
   fires again. */
static void SprintOutAndFire( int weapon, int full, int step, int owed ) {
	int t, worked = 0;
	if ( full ) testMove.cmd.forwardmove = 0;
	else testPs.pm_flags &= ~PMF_SPRINTING;
	CycleThink( full, step );
	CHECK( !( testPs.pm_flags & PMF_SPRINTING ) );
	CHECK( testPs.weaponstate == WEAPON_SPRINT_OUT );
	CHECK( testPs.weaponTime == BG_WeaponSprintOutTime( weapon ) );
	for ( t = 0; t < BG_WeaponSprintOutTime( weapon ) && testPs.weaponstate == WEAPON_SPRINT_OUT; t += step )
		CycleThink( full, step );
	CHECK( !!( testPs.stats[STAT_UNCHAMBERED] & (1 << weapon) ) == owed );
	if ( owed ) {
		/* the sprint-out's last think hands a ready gun to the debt gate,
		   which starts the action alone: from the open note, rechamberTime */
		CHECK( testPs.weaponstate == WEAPON_BOLTING );
		CHECK( testPs.weaponAnimTime == GunNote( bg_weaponFire[weapon].notes, WNOTE_BOLT_OPEN ) );
		CHECK( testPs.weaponTime == ActionLength( weapon ) );
		testMove.cmd.buttons = BUTTON_ATTACK;
		while ( testPs.weaponstate == WEAPON_BOLTING ) {
			CHECK( shots == 1 );
			CycleThink( full, step ); worked += step;
		}
		/* the whole action, and the trigger fires on the think it ends */
		CHECK( worked >= ActionLength( weapon ) && worked < ActionLength( weapon ) + step );
		CHECK( shots == 2 && testPs.weaponstate == WEAPON_FIRING );
	} else {
		CHECK( testPs.weaponstate == WEAPON_READY );
		CycleThink( full, step );
		CHECK( testPs.weaponstate == WEAPON_READY );	/* no action worked again */
		testMove.cmd.buttons = BUTTON_ATTACK;
		CycleThink( full, step );
		CHECK( shots == 2 && testPs.weaponstate == WEAPON_FIRING );
	}
	CHECK( testPs.ammo[weapon] == BG_WeaponMagSize( weapon ) - 2 );
	testMove.cmd.buttons = 0;
}
static void TestSprintOverTheCycle( int weapon, int point, int step, int full ) {
	int open = GunNote( bg_weaponFire[weapon].notes, WNOTE_BOLT_OPEN );
	int close = GunNote( bg_weaponFire[weapon].notes, WNOTE_BOLT_CLOSED );
	int length = BG_WeaponFireLength( weapon );
	/* MW2's thinks after the shot's: its last, the READY think that starts the
	   action, the bolt and the end */
	int shotEnd = ShotEndThink( weapon, step ), start = shotEnd + 1;
	int bolt = CloseThink( weapon, step ), end = CycleEndThink( weapon, step );
	int press[SP_COUNT], lowered, think, t;
	CHECK( 0 < open && open < close && close < length );
	CHECK( start < bolt && bolt < end );
	press[SP_SHOT] = shotEnd / 2;
	press[SP_SHOT_END] = shotEnd;
	press[SP_SHOT_READY] = start;
	press[SP_ACTION] = ( start + bolt ) / 2;
	press[SP_BEFORE_CLOSE] = bolt - 1;
	press[SP_ON_CLOSE] = bolt;
	press[SP_AFTER_CLOSE] = bolt + 1;
	/* A shot of one think has no think inside it; a bolt that closes on the
	   action's first think after its start (MW2's Masterkey, 1ms in) leaves
	   no think inside the action before it. */
	if ( point == SP_SHOT && press[point] < 1 ) return;
	if ( ( point == SP_ACTION || point == SP_BEFORE_CLOSE ) && press[point] <= start ) return;
	CHECK( press[SP_AFTER_CLOSE] < end );
	lowered = press[point] <= start;

	ShootGun( weapon );
	CHECK( testPs.weaponAnimTime == 0 );
	if ( full ) {
		testMove.trace = ClearWorldTrace;
		testMove.pointcontents = NoWorldContents;
		testMove.tracemask = MASK_PLAYERSOLID;
		testPs.speed = 320;
		testPs.groundEntityNum = ENTITYNUM_NONE;	/* MW2's sprint starts in the air too */
		testMove.cmd.forwardmove = 127;
	}
	for ( think = 1; think < press[point]; think++ ) CycleThink( full, step );
	if ( full ) testMove.cmd.buttons = BUTTON_SPRINT;	/* a tap: the sprint latches */
	else testPs.pm_flags |= PMF_SPRINTING;

	/* Every think until the READY think (lowered) or the cycle's end: the gun
	   stays in the shot, ends it READY on its last think with the clock at the
	   open note, or works the action from that note, and the close note
	   settles the debt on its own think. */
	for ( think = press[point]; ; think++ ) {
		CycleThink( full, step );
		testMove.cmd.buttons = 0;
		CHECK( testPs.pm_flags & PMF_SPRINTING );
		/* The sprint meter runs from the think after the press, as it always does. */
		if ( full ) CHECK( testPs.sprintUsed == ( think - press[point] ) * step );
		CHECK( shots == 1 );
		if ( lowered ? think >= start : think >= end ) break;
		if ( think < shotEnd ) {
			CHECK( testPs.weaponstate == WEAPON_FIRING && testPs.weaponAnimTime == think * step );
		} else if ( think == shotEnd ) {
			/* the shot's last think does not lower (MW2's sprint check refuses FIRING) */
			CHECK( testPs.weaponstate == WEAPON_READY && testPs.weaponAnimTime == open );
		} else {
			CHECK( testPs.weaponstate == WEAPON_BOLTING );
			CHECK( testPs.weaponAnimTime == open + ( think - start ) * step );
		}
		CHECK( !!( testPs.stats[STAT_UNCHAMBERED] & (1 << weapon) ) == ( think < bolt ) );
	}
	if ( lowered ) {
		/* The READY think lowers the gun with the gun's whole sprint entry,
		   before the action can start: the clock stays where the shot ended,
		   and the pump or bolt is still owed. */
		CHECK( testPs.weaponstate == WEAPON_SPRINT_IN );
		CHECK( testPs.weaponTime == BG_WeaponSprintInTime( weapon ) );
		CHECK( testPs.weaponAnimTime == open );
		CHECK( testPs.stats[STAT_UNCHAMBERED] & (1 << weapon) );
	} else {
		/* The think the cycle reaches its length: READY and chambered, not lowered. */
		CHECK( testPs.weaponstate == WEAPON_READY && testPs.weaponAnimTime == -1 );
		CHECK( !( testPs.stats[STAT_UNCHAMBERED] & (1 << weapon) ) );
		/* The next think lowers it, with the gun's whole sprint entry. */
		CycleThink( full, step );
		CHECK( testPs.pm_flags & PMF_SPRINTING );
		CHECK( testPs.weaponstate == WEAPON_SPRINT_IN );
		CHECK( testPs.weaponTime == BG_WeaponSprintInTime( weapon ) );
	}
	for ( t = 0; t < BG_WeaponSprintInTime( weapon ) && testPs.weaponstate == WEAPON_SPRINT_IN; t += step )
		CycleThink( full, step );
	CHECK( testPs.weaponstate == WEAPON_SPRINTING && testPs.weaponAnimTime == -1 );
	CHECK( !!( testPs.stats[STAT_UNCHAMBERED] & (1 << weapon) ) == lowered );
	CHECK( shots == 1 );

	/* End the sprint (let go of forward). Lowered on the READY think, the gun
	   works its whole action before it fires again; lowered after the cycle,
	   it owes nothing and the next trigger fires at once. */
	SprintOutAndFire( weapon, full, step, lowered );
}

/* MW2 asks for the sprint pose before the rechamber (CheckForSprint, then
   CheckForRechamber): a gun at rest that still owes its action - here after a
   swap out before the close note - lowers first, keeps the debt through the
   whole sprint, and works the WHOLE action once it is back up. A deferral that
   costs the full cycle, never a cancel. */
static void TestSprintDefersAnOwedAction( int weapon ) {
	int close = GunNote( bg_weaponFire[weapon].notes, WNOTE_BOLT_CLOSED ), t;
	ShootGun( weapon );
	RunFor( close - 2, 1 );
	testMove.cmd.weapon = otherGun; Step( 1 );
	CHECK( testPs.weapon == weapon && testPs.weaponstate == WEAPON_DROPPING );
	for ( t = 0; t < 10000 && !( testPs.weapon == otherGun && testPs.weaponstate == WEAPON_READY ); t++ ) Step( 1 );
	testMove.cmd.weapon = weapon;
	for ( t = 0; t < 10000 && !( testPs.weapon == weapon && testPs.weaponstate == WEAPON_READY ); t++ ) Step( 1 );
	CHECK( testPs.weapon == weapon && testPs.weaponstate == WEAPON_READY );
	CHECK( testPs.stats[STAT_UNCHAMBERED] & (1 << weapon) );

	testPs.pm_flags |= PMF_SPRINTING;
	Step( 1 );
	CHECK( testPs.weaponstate == WEAPON_SPRINT_IN && testPs.weaponTime == BG_WeaponSprintInTime( weapon ) );
	RunFor( BG_WeaponSprintInTime( weapon ), 1 );
	CHECK( testPs.weaponstate == WEAPON_SPRINTING );
	CHECK( testPs.stats[STAT_UNCHAMBERED] & (1 << weapon) );
	testPs.pm_flags &= ~PMF_SPRINTING;
	Step( 1 );
	CHECK( testPs.weaponstate == WEAPON_SPRINT_OUT );
	RunFor( BG_WeaponSprintOutTime( weapon ), 1 );
	/* the sprint-out's last think hands a ready gun to the debt gate, which
	   works the action alone, from its open note */
	CHECK( testPs.weaponstate == WEAPON_BOLTING &&
		testPs.weaponAnimTime == GunNote( bg_weaponFire[weapon].notes, WNOTE_BOLT_OPEN ) );
	shots = 0; testMove.cmd.buttons = BUTTON_ATTACK;
	RunFor( ActionLength( weapon ) - 1, 1 );
	CHECK( shots == 0 );
	Step( 1 );
	CHECK( shots == 1 );
	testMove.cmd.buttons = 0;
}

/* The trigger and a reload request held together: a reload is never asked of
   a gun that is reloading, so after each reload the trigger gets its shot -
   the guns alternate and never lock in their reload. */
static void TestTriggerAndReloadHeld( int step ) {
	const int guns[] = { WP_SHOTGUN, WP_M40A3, WP_MACHINEGUN };
	int g, bit;
	for ( g = 0; g < 3; g++ ) for ( bit = 0; bit < 2; bit++ ) {
		int w = guns[g], t;
		memset( &testPs, 0, sizeof(testPs) );
		memset( &testMove, 0, sizeof(testMove) );
		memset( &pml, 0, sizeof(pml) );
		testMove.ps = &testPs; testMove.cmd.weapon = w; pm = &testMove;
		testPs.weapon = w; testPs.weaponstate = WEAPON_READY; testPs.weaponAnimTime = -1;
		testPs.stats[STAT_HEALTH] = 100;
		testPs.stats[STAT_WEAPONS] = 1 << w; testPs.stats[STAT_SLOT_PRIMARY] = w;
		testPs.ammo[w] = BG_WeaponMagSize( w ) - 2;
		testPs.ammoReserve[w] = BG_WeaponMaxReserve( w );
		shots = 0;
		for ( t = 0; t < 6000; t += step ) {
			testMove.cmd.buttons = BUTTON_ATTACK | ( bit ? 0 : BUTTON_RELOAD );
			if ( bit ) testPs.weapFlags |= PWF_USE_RELOAD;	/* use/reload held: asked every command */
			Step( step );
		}
		CHECK( shots >= 3 );
		testMove.cmd.buttons = 0; testPs.weapFlags = 0;
	}
}

int main( void ) {
	int i, offset, weapon, mode, perk, full;
	const int steps[] = { 1, 8, 16, 33, 66 };
	for ( i = 0; i < 5; i++ ) {
		TestAttackInterrupt( steps[i] );
		TestEmptyTube( steps[i] );
		TestReleasedAttack( steps[i] );
		TestNaturalEnd( 7, 10, steps[i] );
		TestNaturalEnd( 3, 1, steps[i] );
		TestMagazineUnchanged( steps[i] );
		TestPumpIsItsOwnState( steps[i] );
		TestFrozenShotEnd( steps[i] );
		TestUnpumpedWeaponUnchanged( steps[i] );
		TestShootReload( steps[i] );
		TestTriggerAndReloadHeld( steps[i] );
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
	for ( weapon = 0; weapon < 2; weapon++ )
		for ( mode = 0; mode < SP_COUNT; mode++ )
			for ( i = 0; i < ARRAY_LEN(steps); i++ )
				for ( full = 0; full < 2; full++ )
					TestSprintOverTheCycle( weapon ? WP_M40A3 : WP_SHOTGUN, mode, steps[i], full );
	TestSprintDefersAnOwedAction( WP_SHOTGUN );
	TestSprintDefersAnOwedAction( WP_M40A3 );
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
