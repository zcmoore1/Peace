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
	RunFor( 349, step );
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
	/* Start, loop before insertion, insertion think, loop after insertion. */
	const int times[] = { 100, 600, 749, 800 };
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
	RunFor( 1000, step );
	CHECK( testPs.weaponstate == WEAPON_READY );
	CHECK( shots == 0 );
	CHECK( testPs.ammo[WP_SHOTGUN] == 3 );
	CHECK( testPs.ammoReserve[WP_SHOTGUN] == 10 );
}

static void TestNaturalEnd( int loaded, int reserve, int step ) {
	BeginReload( WP_SHOTGUN, loaded, reserve );
	RunFor( 1050, 1 );
	CHECK( testPs.weaponAnimSeq == RSEQ_END );
	RunFor( 100, step );
	testMove.cmd.buttons = BUTTON_ATTACK;
	/* Attacking during END must not restart the closing animation. */
	RunFor( 249, step );
	CHECK( shots == 0 );
	Step( 1 );
	CHECK( shots == 1 );
	CHECK( testPs.ammo[WP_SHOTGUN] == loaded );
	CHECK( testPs.ammoReserve[WP_SHOTGUN] == reserve - 1 );
}

static void TestMagazineUnchanged( int step ) {
	BeginReload( WP_MACHINEGUN, 3, 40 );
	testMove.cmd.buttons = BUTTON_ATTACK;
	RunFor( 799, 1 ); /* First think enters its zero-length START -> LOOP. */
	CHECK( testPs.weaponstate == WEAPON_RELOADING );
	CHECK( testPs.weaponAnimSeq == RSEQ_LOOP );
	CHECK( testPs.ammo[WP_MACHINEGUN] == 3 );
	CHECK( shots == 0 );
	RunFor( 200, step );
	CHECK( testPs.ammo[WP_MACHINEGUN] == 30 );
	CHECK( shots == 0 );
	Step( 1 );
	CHECK( shots == 1 );
}

static void TestSwapSprintPreserved( int sprint, int offset ) {
	int remaining;
	/* First shell seats at 750 ms, second at 1300 ms. Interrupt the second. */
	BeginReload( WP_SHOTGUN, 3, 10 );
	RunFor( 1299 + offset, 1 );
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
	RunFor( 600, 8 );
	if ( sprint ) {
		testPs.pm_flags &= ~PMF_SPRINTING;
	} else {
		testMove.cmd.weapon = WP_SHOTGUN;
	}
	RunFor( 600, 8 );
	CHECK( testPs.weaponstate == WEAPON_READY );
	remaining = testPs.ammo[WP_SHOTGUN];
	testMove.cmd.buttons = BUTTON_RELOAD;
	Step( 1 );
	CHECK( testPs.weaponAnimSeq == RSEQ_START );
	CHECK( testPs.weaponAnimTime == 0 );
	CHECK( testPs.ammo[WP_SHOTGUN] == remaining );
}

int main( void ) {
	int i, offset;
	const int steps[] = { 1, 8, 16, 33, 66 };
	for ( i = 0; i < 5; i++ ) {
		TestAttackInterrupt( steps[i] );
		TestEmptyTube( steps[i] );
		TestReleasedAttack( steps[i] );
		TestNaturalEnd( 7, 10, steps[i] );
		TestNaturalEnd( 3, 1, steps[i] );
		TestMagazineUnchanged( steps[i] );
	}
	for ( offset = -1; offset <= 1; offset++ ) {
		TestSwapSprintPreserved( 0, offset );
		TestSwapSprintPreserved( 1, offset );
	}
	printf( "PASS: shotgun reload interruption, ammo conservation, magazine reload, and swap/sprint ordering (%d checks).\n", checks );
	return 0;
}
