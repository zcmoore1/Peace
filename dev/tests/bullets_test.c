/* MW2's client-side bullets (sv_clientSideBullets), end to end through the
 * real code on both sides: the server's half (bullets_server.c) runs the real
 * ClientThink, Pmove, ClientEvents, FireWeapon and G_Damage; the client's half
 * (bullets_client.c) runs the real CG_PredictPlayerState over the same
 * commands, the real playerstate and entity events and the real
 * CG_FireWeapon. Both trace the same world (bullets_world.c).
 *
 * Checked, for every hitscan gun (machinegun, SPAS, railgun, lightning gun,
 * M300, and the chaingun in missionpack):
 *  - the server sends no impact, tracer, blast or beam for a shot, and its
 *    damage is what it always was (7 a bullet, 5 in team games, 10 a pellet,
 *    100 the rail and the M300, 8 the lightning);
 *  - the shooter's own client draws every bullet, pellet and beam exactly where
 *    the server's damage traces ended, hip and aimed, standing and moving,
 *    with the aim moving every command, whether it renders every command or
 *    several commands at once, with and without lag;
 *  - with nothing predicted (a demo, a followed player, the killcam) the
 *    playerstate event draws the shot from the state it came in, which is the
 *    shot's when the shot ended the server frame, and not otherwise;
 *  - another player draws the shot from the shooter's entity event, exactly
 *    when the entity shows the shot's state; the one hit stops it in their own
 *    box, which no snapshot carries;
 *  - nothing is drawn for a projectile, and a shot into the sky draws nothing;
 *  - the fake bullet emerges: a shot the server never fires is still drawn
 *    on the shooter's screen, with no damage.
 *
 * MW2's stance (dev/STANCE.md): every bullet and pellet ends where the
 * owner's hit volumes put it (D7, BG_PlayerHitVolumes: the box, the volume
 * over it while the eye is above it, a prone player's body), and the real
 * server's damage lands on what the mark says. A prone shooter's own shots
 * come from the float eye; others draw a drop shot (stand to prone while
 * firing) from the eye the entity carries (origin2[2]) on every think of both
 * legs, and a settled prone shooter from the prone eye; a prone target's body
 * behind it stops the shooter's, a watcher's and its own drawn bullets as it
 * stops the server's; a target crouching or going prone is hit over its box
 * while its eye is still above it, on the server and every screen alike.
 */
#include "../../code/qcommon/q_shared.h"
#include "../../code/game/bg_public.h"
#include "bullets_world.h"
#include "bullets.h"

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define NEAR(a,b) (fabs((a)-(b))<0.01f)

#define CMD_MSEC	8
#define SHOOTER		0
#define VICTIM		1
#define WATCHER		2

static int Damage( int weapon, int gametype ) {
	switch ( weapon ) {
	case WP_MACHINEGUN: return gametype == GT_TEAM ? 5 : 7;
	case WP_SHOTGUN: return 10;
	case WP_RAILGUN: return 100;
	case WP_LIGHTNING: return 8;
	case WP_M40A3: return 100;
#ifdef MISSIONPACK
	case WP_CHAINGUN: return 7;
#endif
	}
	return 0;
}

static qboolean SameMark( const bwMark_t *a, const bwMark_t *b ) {
	return NEAR( a->point[0], b->point[0] ) && NEAR( a->point[1], b->point[1] ) && NEAR( a->point[2], b->point[2] ) &&
		a->flesh == b->flesh && a->entityNum == b->entityNum;
}

/* The server's marks as the client draws them: a rail's shot shows only where
 * its beam stopped (every body it passed is only damage). */
static int ServerMarks( int weapon, bwMark_t *out ) {
	int i, n = 0;
	for ( i = 0; i < bs_markCount; i++ ) {
		if ( weapon == WP_RAILGUN && bs_marks[i].flesh ) continue;
		out[n++] = bs_marks[i];
	}
	return n;
}
static int FleshOn( int entityNum ) {
	int i, n = 0;
	for ( i = 0; i < bs_markCount; i++ ) if ( bs_marks[i].flesh && bs_marks[i].entityNum == entityNum ) n++;
	return n;
}
static qboolean MarksMatch( int weapon ) {
	bwMark_t server[BW_MAX_MARKS];
	int i, n = ServerMarks( weapon, server );
	if ( getenv( "BULLETS_DEBUG" ) ) {	/* both lists, side by side */
		for ( i = 0; i < n || i < bc_markCount; i++ ) {
			const bwMark_t *a = i < n ? &server[i] : NULL, *b = i < bc_markCount ? &bc_marks[i] : NULL;
			printf( "%3d  sv %8.2f %8.2f %8.2f e%-4d f%d   cl %8.2f %8.2f %8.2f e%-4d f%d%s\n", i,
				a ? a->point[0] : 0, a ? a->point[1] : 0, a ? a->point[2] : 0, a ? a->entityNum : -1, a ? a->flesh : 0,
				b ? b->point[0] : 0, b ? b->point[1] : 0, b ? b->point[2] : 0, b ? b->entityNum : -1, b ? b->flesh : 0,
				a && b && SameMark( a, b ) ? "" : "   <<" );
		}
	}
	if ( getenv( "BULLETS_DEBUG" ) ) printf( "marks: server %d (%d shot traces, %d stopped by a hit volume), client %d\n",
		n, bs_shotTraces, bs_volumeHits, bc_markCount );
	if ( n != bc_markCount ) return qfalse;
	for ( i = 0; i < n; i++ ) if ( !SameMark( &server[i], &bc_marks[i] ) ) return qfalse;
	return qtrue;
}

typedef struct {
	int		weapon;
	float	yaw;			/* the shooter's aim */
	float	shooterY;		/* where the shooter stands across the line (0: in front of the victim) */
	int		shooterStance;	/* stance bits the shooter's commands hold */
	int		victimStance;	/* stance bits the victim holds (0: it sends no commands) */
	int		victimCycle;	/* ms on, then off, of victimStance (0: always on) */
	float	victimYaw;
	qboolean	settleFirst;	/* no trigger until the victim's stance has settled */
	int		gametype;
	int		viewer;			/* whose cgame draws */
	int		renderEvery;	/* commands made before each client frame */
	int		snapEvery;		/* commands per server frame */
	int		lag;			/* client frames a snapshot waits before it arrives */
	int		commands;
	qboolean	move, turn, ads, predicted;
	qboolean	lastOnly;	/* the trigger only on each server frame's last command */
	int		useReloadAt;	/* a use/reload tap on this command, the trigger only after it (0: none) */
	float	pitch;
} run_t;

/* the victim's stance has settled: no eye leg running (or no stance in pmove) */
static int settledAt;	/* the command the victim's stance settled on, -1 not yet */
#define SETTLE_MARGIN	24	/* commands after that before the trigger: every client's snapshots have it */
static qboolean Settled( void ) {
	const playerState_t *ps = BS_PlayerState( VICTIM );
	return !ps->viewHeightLerpTarget && ps->viewheight == ( ( ps->pm_flags & PMF_PRONE ) ? PRONE_VIEWHEIGHT :
		( ps->pm_flags & PMF_DUCKED ) ? CROUCH_VIEWHEIGHT : DEFAULT_VIEWHEIGHT );
}
static usercmd_t Cmd( const run_t *r, int k, int t ) {
	usercmd_t c;
	float yaw = r->yaw, pitch = r->pitch;
	memset( &c, 0, sizeof( c ) );
	c.serverTime = t; c.weapon = r->weapon;
	c.buttons = r->shooterStance;
	if ( r->settleFirst && ( settledAt < 0 || k < settledAt + SETTLE_MARGIN ) ) {
		;	/* hold fire while the target lies down, and until every view has it down */
	} else if ( r->useReloadAt ) {	/* a few rounds, a pause, the tap, then the trigger */
		if ( k < 24 || k > r->useReloadAt ) c.buttons |= BUTTON_ATTACK;
		if ( k == r->useReloadAt ) c.buttons |= BUTTON_USE_RELOAD;
	} else if ( !r->lastOnly || ( k + 1 ) % r->snapEvery == 0 ) c.buttons |= BUTTON_ATTACK;
	if ( r->ads ) c.buttons |= BUTTON_ADS;
	if ( r->turn ) {	/* sweep across the victim and the wall behind them */
		yaw = 6 * sin( k * 0.37 ); pitch += 2.5f * cos( k * 0.23 );
	}
	c.angles[YAW] = ANGLE2SHORT( yaw ); c.angles[PITCH] = ANGLE2SHORT( pitch );
	if ( r->move ) {
		c.forwardmove = ( k / 20 ) % 2 ? 127 : -127;
		c.rightmove = ( k / 13 ) % 2 ? 64 : -64;
	}
	return c;
}

#define MAX_QUEUE 16
static struct { int time; playerState_t ps; entityState_t ents[BS_MAX_SNAPSHOT_ENTITIES]; int count; int due; } queue[MAX_QUEUE];

/* One run: the server takes each command as it is made; every snapEvery a
 * server frame ends and a snapshot is taken for the viewer, which arrives lag
 * client frames later (never before the client has predicted the commands it
 * answers: a command is predicted the frame it is made). */
static void Run( const run_t *r ) {
	int k, t, frames = 0, head = 0, tail = 0, health = 0;
	entityState_t *ents;

	if ( getenv( "BULLETS_DEBUG" ) ) {
		printf( "run: weapon %d gametype %d viewer %d render %d snap %d lag %d move %d turn %d ads %d predicted %d lastOnly %d pitch %g"
			" yaw %g shooterStance %x victimStance %x cycle %d\n",
			r->weapon, r->gametype, r->viewer, r->renderEvery, r->snapEvery, r->lag, r->move, r->turn, r->ads,
			r->predicted, r->lastOnly, r->pitch, r->yaw, r->shooterStance, r->victimStance, r->victimCycle );
	}

	BS_Reset( r->gametype );
	BS_Place( SHOOTER, 0, r->shooterY, 24 );
	BS_Place( VICTIM, 300, 0, 24 );
	BS_Place( WATCHER, 0, 200, 24 );
	BS_Arm( SHOOTER, r->weapon );
	BS_EndFrame();
	health = BS_Health( VICTIM );

	BC_Reset( r->viewer );
	{
		static entityState_t first[BS_MAX_SNAPSHOT_ENTITIES];
		int n = BS_Snapshot( r->viewer, first, BS_MAX_SNAPSHOT_ENTITIES );
		BC_Snapshot( BS_START_TIME, BS_PlayerState( r->viewer ), first, n );
	}
	BC_ClearMarks(); BS_ClearMarks();
	settledAt = -1;

	for ( k = 0, t = BS_START_TIME; k < r->commands; k++ ) {
		usercmd_t c;
		t += CMD_MSEC;
		if ( r->victimStance ) {	/* the target's own command first, in this think */
			usercmd_t v;
			memset( &v, 0, sizeof( v ) );
			v.serverTime = t; v.weapon = BS_PlayerState( VICTIM )->weapon;
			v.angles[YAW] = ANGLE2SHORT( r->victimYaw );
			if ( !r->victimCycle || ( k * CMD_MSEC / r->victimCycle ) % 2 == 0 ) v.buttons = r->victimStance;
			BS_Command( VICTIM, &v );
			if ( r->viewer == VICTIM ) BC_StoreCommand( &v );
			if ( settledAt < 0 && k > 0 && Settled() ) settledAt = k;
		} else if ( settledAt < 0 ) {
			settledAt = 0;
		}
		c = Cmd( r, k, t );
		if ( r->viewer == SHOOTER ) BC_StoreCommand( &c );
		BS_Command( SHOOTER, &c );
		CHECK( BS_EffectsSent() == 0 );		/* no impact, tracer, blast or beam */

		if ( ( k + 1 ) % r->snapEvery == 0 ) {
			BS_EndFrame();
			CHECK( BS_EffectsSent() == 0 );
			ents = queue[head % MAX_QUEUE].ents;
			queue[head % MAX_QUEUE].count = BS_Snapshot( r->viewer, ents, BS_MAX_SNAPSHOT_ENTITIES );
			queue[head % MAX_QUEUE].ps = *BS_PlayerState( r->viewer );
			queue[head % MAX_QUEUE].time = t;
			queue[head % MAX_QUEUE].due = frames + 2 + r->lag;	/* this command's frame is frames + 1 */
			head++;
			CHECK( head - tail < MAX_QUEUE );
			BS_FreeTempEntities();
		}
		if ( ( k + 1 ) % r->renderEvery == 0 || k == r->commands - 1 ) {
			frames++;
			while ( tail < head && queue[tail % MAX_QUEUE].due <= frames ) {
				BC_Snapshot( queue[tail % MAX_QUEUE].time, &queue[tail % MAX_QUEUE].ps,
					queue[tail % MAX_QUEUE].ents, queue[tail % MAX_QUEUE].count );
				if ( !r->predicted ) BC_UnpredictedFrame( t );
				tail++;
			}
			if ( r->predicted ) BC_PredictFrame( t );
		}
	}
	/* the snapshots still on their way */
	while ( tail < head ) {
		BC_Snapshot( queue[tail % MAX_QUEUE].time, &queue[tail % MAX_QUEUE].ps,
			queue[tail % MAX_QUEUE].ents, queue[tail % MAX_QUEUE].count );
		if ( !r->predicted ) BC_UnpredictedFrame( t );
		tail++;
	}

	/* the damage is the server's and unchanged: each hit's own amount */
	CHECK( bs_markCount < BW_MAX_MARKS && bc_markCount < BW_MAX_MARKS );	/* every mark recorded */
	if ( !r->victimStance ) {	/* a target that thinks counts its health down above the maximum (ClientTimerActions) */
		CHECK( health - BS_Health( VICTIM ) == FleshOn( VICTIM ) * Damage( r->weapon, r->gametype ) );
	}
	if ( r->weapon != WP_RAILGUN && r->weapon != WP_LIGHTNING && r->weapon != WP_ROCKET_LAUNCHER ) {
		CHECK( bs_bulletHealth[VICTIM] == FleshOn( VICTIM ) * Damage( r->weapon, r->gametype ) );
	}
	/* every bullet's and pellet's damage is on what its mark says, where it
	 * says (the real bullet code's point, snapped by a bullet as tr.endpos is) */
	if ( r->weapon != WP_RAILGUN && r->weapon != WP_LIGHTNING && r->weapon != WP_ROCKET_LAUNCHER ) {
		int i, d = 0;
		for ( i = 0; i < bs_markCount; i++ ) {
			if ( !bs_marks[i].flesh ) continue;
			CHECK( d < bs_damageCount && bs_damage[d].entityNum == bs_marks[i].entityNum );
			CHECK( fabs( bs_damage[d].point[0] - bs_marks[i].point[0] ) < 1 && fabs( bs_damage[d].point[1] - bs_marks[i].point[1] ) < 1 &&
				fabs( bs_damage[d].point[2] - bs_marks[i].point[2] ) < 1 );
			d++;
		}
		CHECK( d == bs_damageCount );
	}
}

static const int guns[] = { WP_MACHINEGUN, WP_SHOTGUN, WP_RAILGUN, WP_LIGHTNING, WP_M40A3
#ifdef MISSIONPACK
	, WP_CHAINGUN
#endif
};

/* The shooter's own screen, live: every shot exactly where the server's went. */
static void TestOwnShots( void ) {
	static const int renders[] = { 1, 3 }, lags[] = { 0, 2 };
	int g, i, j, m, a;
	for ( g = 0; g < ARRAY_LEN( guns ); g++ ) for ( i = 0; i < 2; i++ ) for ( j = 0; j < 2; j++ )
	for ( m = 0; m < 2; m++ ) for ( a = 0; a < 2; a++ ) {
		run_t r;
		memset( &r, 0, sizeof( r ) );
		r.weapon = guns[g]; r.gametype = GT_FFA; r.viewer = SHOOTER; r.renderEvery = renders[i];
		r.snapEvery = 6; r.lag = lags[j]; r.commands = 240; r.move = m; r.ads = a; r.turn = qtrue;
		r.predicted = qtrue;
		Run( &r );
		CHECK( bs_markCount > 0 );
		CHECK( FleshOn( VICTIM ) > 0 );		/* it hits the victim, and the wall beside them */
		if ( guns[g] != WP_M40A3 || !a ) CHECK( bs_markCount > FleshOn( VICTIM ) );	/* the M300 aimed: every round hits */
		CHECK( MarksMatch( guns[g] ) );
		if ( guns[g] == WP_SHOTGUN ) CHECK( bc_shotgunSmokes > 0 && bs_markCount % DEFAULT_SHOTGUN_COUNT == 0 );
		if ( guns[g] == WP_RAILGUN ) CHECK( bc_rails == bc_markCount );
		if ( guns[g] != WP_RAILGUN && guns[g] != WP_LIGHTNING && guns[g] != WP_SHOTGUN ) CHECK( bc_tracers > 0 );
	}
	/* a team game: the machinegun's team damage, unchanged */
	{
		run_t r;
		memset( &r, 0, sizeof( r ) );
		r.weapon = WP_MACHINEGUN; r.gametype = GT_TEAM; r.viewer = SHOOTER; r.renderEvery = 1;
		r.snapEvery = 6; r.commands = 120; r.turn = qtrue; r.predicted = qtrue;
		Run( &r ); CHECK( FleshOn( VICTIM ) > 0 && MarksMatch( WP_MACHINEGUN ) );
	}
}

/* Into the sky: the server's shot ends unseen, and nothing is drawn. */
static void TestSky( void ) {
	int g;
	for ( g = 0; g < ARRAY_LEN( guns ); g++ ) {
		run_t r;
		if ( guns[g] == WP_LIGHTNING ) continue;	/* its beam does not reach the sky */
		memset( &r, 0, sizeof( r ) );
		r.weapon = guns[g]; r.gametype = GT_FFA; r.viewer = SHOOTER; r.renderEvery = 1; r.snapEvery = 6;
		r.commands = 120; r.predicted = qtrue; r.pitch = -80;
		Run( &r );
		CHECK( bs_shotTraces > 0 && bs_markCount == 0 && bc_markCount == 0 );
		if ( guns[g] == WP_RAILGUN ) CHECK( bc_rails > 0 );	/* the beam still shows, with no mark */
	}
}

/* Nothing predicted: the playerstate event draws from the state it came in.
 * When the shot ended its server frame that is the shot's state, exactly. */
static void TestUnpredicted( void ) {
	int g;
	run_t r;
	for ( g = 0; g < ARRAY_LEN( guns ); g++ ) {
		memset( &r, 0, sizeof( r ) );
		r.weapon = guns[g]; r.gametype = GT_FFA; r.viewer = SHOOTER; r.renderEvery = 1; r.snapEvery = 6;
		r.commands = 240; r.lastOnly = qtrue; r.predicted = qfalse;
		Run( &r );
		CHECK( bs_markCount > 0 && FleshOn( VICTIM ) > 0 && MarksMatch( guns[g] ) );
	}
	/* a shot with commands after it in its frame comes from the frame's last
	 * state: the seed is that command's, so the pattern is not the server's
	 * (the predicted shot, which records its own command, is) */
	memset( &r, 0, sizeof( r ) );
	r.weapon = WP_SHOTGUN; r.gametype = GT_FFA; r.viewer = SHOOTER; r.renderEvery = 1; r.snapEvery = 6;
	r.commands = 240; r.predicted = qfalse;
	Run( &r ); CHECK( bs_markCount > 0 && bc_markCount == bs_markCount && !MarksMatch( WP_SHOTGUN ) );
	r.predicted = qtrue;
	Run( &r ); CHECK( MarksMatch( WP_SHOTGUN ) );
}

/* Everyone else: from the shooter's entity event. Standing still, aimed along
 * whole degrees (the entity's aim is snapped to them), the shot ending its
 * server frame: the entity holds the shot's own state, and the shot is drawn
 * exactly. The one in the way is never in their own snapshot: their own box
 * stops it, as on the server. */
static void TestOthers( void ) {
	static const int viewers[] = { WATCHER, VICTIM };
	int g, v;
	for ( g = 0; g < ARRAY_LEN( guns ); g++ ) for ( v = 0; v < 2; v++ ) {
		run_t r;
		memset( &r, 0, sizeof( r ) );
		r.weapon = guns[g]; r.gametype = GT_FFA; r.viewer = viewers[v]; r.renderEvery = 1; r.snapEvery = 6;
		r.commands = 240; r.lastOnly = qtrue; r.predicted = qtrue;
		Run( &r );
		CHECK( bs_markCount > 0 && FleshOn( VICTIM ) > 0 && MarksMatch( guns[g] ) );
	}
}

/* The fake bullet (dev/FAKE_RELOAD.md), as it emerges: a use/reload tap a
 * command before the trigger. The server takes the reload first and never
 * fires; the client, which never sets the use/reload bit itself, predicts the
 * shot and draws it, blood and tracer, with no damage - as MW2's client draws
 * the bullets it predicts. Nothing names it. */
static void TestFakeBullet( void ) {
	static const int lags[] = { 0, 2 };
	int j, i;
	for ( j = 0; j < 2; j++ ) {
		run_t r;
		memset( &r, 0, sizeof( r ) );
		r.weapon = WP_MACHINEGUN; r.gametype = GT_FFA; r.viewer = SHOOTER; r.renderEvery = 1; r.snapEvery = 6;
		r.lag = lags[j]; r.commands = 120; r.predicted = qtrue; r.useReloadAt = 60;
		Run( &r );
		CHECK( bs_markCount > 0 && BS_PlayerState( SHOOTER )->weaponstate == WEAPON_RELOADING );
		/* every real shot as the server traced it, then the one it never fired */
		CHECK( bc_markCount == bs_markCount + 1 );
		for ( i = 0; i < bs_markCount; i++ ) CHECK( SameMark( &bs_marks[i], &bc_marks[i] ) );
		CHECK( bc_marks[bs_markCount].flesh && bc_marks[bs_markCount].entityNum == VICTIM );
	}
}

/* A projectile is the server's missile: nothing is traced or drawn for it. */
static void TestProjectile( void ) {
	run_t r;
	memset( &r, 0, sizeof( r ) );
	r.weapon = WP_ROCKET_LAUNCHER; r.gametype = GT_FFA; r.viewer = SHOOTER; r.renderEvery = 1; r.snapEvery = 6;
	r.commands = 60; r.predicted = qtrue;
	Run( &r );
	CHECK( BS_Missiles() > 0 && bs_shotTraces == 0 && bc_markCount == 0 && bc_tracers == 0 );
}

/* The shared generator is MW2's: BG_srand's four steps, BG_random's top 15
 * bits (0x410720, 0x44c590). */
static void TestGenerator( void ) {
	unsigned int s = 12345u; int i, seed;
	for ( i = 0; i < 4; i++ ) s = s * 214013u + 2531011u;
	CHECK( BG_BulletSeed( 12345 ) == (int)s );
	seed = (int)s; s = s * 214013u + 2531011u;
	CHECK( BG_BulletRandom( &seed ) == (float)( s >> 17 ) / 32768.0f && seed == (int)s );
	for ( i = 0; i < 100000; i++ ) { float f = BG_BulletRandom( &seed ); CHECK( f >= 0 && f < 1 ); }
	CHECK( BG_BulletSeed( 10008 ) != BG_BulletSeed( 10016 ) );
}

#define VICTIM_X	300	/* where Run puts the victim */

/* A prone target's body, which lies behind its origin (MW2's room check's
 * body, D7): the victim lies down facing +y, so its body runs back along -y,
 * and a prone shooter level with it fires along +x across the body, clear of
 * the victim's prone box. Every aim here is a whole multiple of 45 degrees,
 * the angles an entity carries exactly. The server, the shooter's own screen,
 * a watcher's and the victim's own (its box from BG_PlayerBox, its volumes
 * from its own playerstate) agree on every bullet and pellet. */
static void TestProneTarget( void ) {
	static const int bodyGuns[] = { WP_MACHINEGUN, WP_SHOTGUN, WP_M40A3 }, viewers[] = { SHOOTER, WATCHER, VICTIM };
	int g, v, volumeHits = 0;
	qboolean prone = qtrue;
	for ( g = 0; g < ARRAY_LEN( bodyGuns ); g++ ) for ( v = 0; v < ARRAY_LEN( viewers ); v++ ) {
		run_t r;
		memset( &r, 0, sizeof( r ) );
		r.weapon = bodyGuns[g]; r.gametype = GT_FFA; r.viewer = viewers[v]; r.renderEvery = 1; r.snapEvery = 6;
		r.commands = 200; r.predicted = qtrue; r.lastOnly = viewers[v] != SHOOTER;
		r.ads = viewers[v] == SHOOTER;	/* others draw the hip's spread: aiming is not on the entity */
		r.victimStance = BUTTON_PRONE; r.victimYaw = 90; r.settleFirst = qtrue;
		r.shooterStance = BUTTON_PRONE; r.shooterY = -0.6f * BG_PRONE_FEET_DIST;
		Run( &r );
		CHECK( bs_markCount > 0 && MarksMatch( r.weapon ) );
		prone = prone && ( BS_PlayerState( VICTIM )->pm_flags & PMF_PRONE );
		volumeHits += bs_volumeHits;
	}
	CHECK( prone && volumeHits > 0 );
	printf( "a prone target's body behind its box: %d bullets and pellets stopped there, on every screen\n", volumeHits );
}

/* A target crouching, then going prone, over and over: while its eye is
 * still above its new box (MW2's eye curve against the box that changes at
 * once), a bullet over the box hits it, on the server and on a watcher's
 * screen, which sees the target's entity as the shot did. Shot level from a
 * standing eye, over the crouched box (the stand-to-crouch leg), and from a
 * crouched eye, over the prone box (crouch to prone). */
static void TestOverTheBox( void ) {
	static const int shooterStances[] = { 0, BUTTON_CROUCH | BUTTON_TEMP_STANCE };
	int h, volumeHits = 0;
	qboolean down = qfalse;
	for ( h = 0; h < ARRAY_LEN( shooterStances ); h++ ) {
		run_t r;
		memset( &r, 0, sizeof( r ) );
		r.weapon = WP_MACHINEGUN; r.gametype = GT_FFA; r.viewer = WATCHER; r.renderEvery = 1; r.snapEvery = 6;
		r.shooterStance = shooterStances[h];
		r.victimStance = BUTTON_PRONE; r.victimCycle = 3 * BG_PRONE_LERP_MS; r.victimYaw = 180;
		/* four times down and up, and down again at the end */
		r.commands = 7 * r.victimCycle / CMD_MSEC - 10; r.predicted = qtrue; r.lastOnly = qtrue;
		Run( &r );
		CHECK( bs_markCount > 0 && MarksMatch( r.weapon ) );
		volumeHits += bs_volumeHits;
		down = down || ( BS_PlayerState( VICTIM )->pm_flags & ( PMF_PRONE | PMF_DUCKED ) );
	}
	CHECK( down && volumeHits > 0 );
	printf( "a target's eye above its new box: %d bullets hit it over the box, on the server and a watcher's screen\n", volumeHits );
}

/* The drop shot: firing while going prone. Others draw it from the eye the
 * shooter's entity carries (origin2[2], the server's muzzle eye) on every
 * think of both legs, and then from the prone eye; the shooter's own screen
 * from its recorded float eye, standing still and moving. */
static void TestDropShot( void ) {
	static const int viewers[] = { WATCHER, VICTIM, SHOOTER };
	int g, v, m, midLeg = 0;
	qboolean prone = qtrue;
	for ( g = 0; g < ARRAY_LEN( guns ); g++ ) for ( v = 0; v < ARRAY_LEN( viewers ); v++ ) for ( m = 0; m < 2; m++ ) {
		run_t r;
		if ( viewers[v] != SHOOTER && m ) continue;	/* others are exact standing still (above) */
		memset( &r, 0, sizeof( r ) );
		r.weapon = guns[g]; r.gametype = GT_FFA; r.viewer = viewers[v]; r.renderEvery = 1; r.snapEvery = 6;
		r.commands = 240; r.predicted = qtrue; r.lastOnly = viewers[v] != SHOOTER; r.move = m;
		r.shooterStance = BUTTON_PRONE;
		Run( &r );
		CHECK( bs_markCount > 0 && MarksMatch( r.weapon ) );
		midLeg += bs_midLegShots;
		prone = prone && BS_PlayerState( SHOOTER )->viewheight == PRONE_VIEWHEIGHT;
	}
	CHECK( prone && midLeg > 0 );
	printf( "drop shots: %d bullets fired from an eye between two stances, drawn where the server's went\n", midLeg );
}

int main( void ) {
	TestGenerator();
	TestOwnShots();
	TestSky();
	TestUnpredicted();
	TestOthers();
	TestFakeBullet();
	TestProjectile();
	TestProneTarget();
	TestOverTheBox();
	TestDropShot();
	printf( "bullets_test: %d checks passed\n", checks );
	return 0;
}
