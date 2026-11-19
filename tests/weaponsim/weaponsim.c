/*
===========================================================================
weaponsim -- offline simulator for the weapon state machine and the
first-person animation blending.

It links the real shared movement code (bg_pmove.c) and the real view
animator (cg_viewanim.c), stands a player on a flat floor, and feeds it one
usercmd per client frame, exactly as the client and server would. Nothing in
here knows what any input sequence is supposed to produce.

usage:
    weaponsim selftest
    weaponsim trace <scenario> [fps] [a b c d]     writes a JSON trace to stdout
    weaponsim summary <scenario> [fps] [a b c d]   prints one line of measurements

scenarios:
    barrelroll   the second weapon is coming up; a frames into its raise press
                 sprint, b frames later switch weapons, c frames later let go of
                 sprint, d frames later switch weapons again
    slow         the same four inputs with a full second between each
    yy           switch weapons twice with no sprint
    sprint       tap sprint on an idle weapon
===========================================================================
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include "../../code/qcommon/q_shared.h"
#include "../../code/game/bg_public.h"
#include "../../code/cgame/cg_viewanim.h"

/*
=============================================================================

what the engine would normally provide

=============================================================================
*/

void QDECL Com_Error( int level, const char *error, ... ) {
	va_list		argptr;

	va_start( argptr, error );
	vfprintf( stderr, error, argptr );
	va_end( argptr );
	fprintf( stderr, "\n" );
	exit( 2 );
}

void QDECL Com_Printf( const char *msg, ... ) {
	va_list		argptr;

	va_start( argptr, msg );
	vfprintf( stderr, msg, argptr );
	va_end( argptr );
}

void trap_SnapVector( float *v ) {
	v[0] = (float)lrintf( v[0] );
	v[1] = (float)lrintf( v[1] );
	v[2] = (float)lrintf( v[2] );
}

// an endless flat floor at z = 0
static void Sim_Trace( trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs,
		const vec3_t end, int passEntityNum, int contentMask ) {
	float	zs, ze, f;

	memset( tr, 0, sizeof( *tr ) );
	tr->fraction = 1.0f;
	tr->entityNum = ENTITYNUM_NONE;
	VectorCopy( end, tr->endpos );

	zs = start[2] + mins[2];
	ze = end[2] + mins[2];

	if ( zs < 0.0f ) {
		tr->startsolid = qtrue;
		tr->allsolid = qtrue;
		tr->fraction = 0.0f;
		tr->entityNum = ENTITYNUM_WORLD;
		VectorCopy( start, tr->endpos );
		return;
	}
	if ( ze >= 0.0f ) {
		return;
	}

	f = ( zs - 0.125f ) / ( zs - ze );
	if ( f < 0.0f ) {
		f = 0.0f;
	}
	tr->fraction = f;
	tr->endpos[0] = start[0] + ( end[0] - start[0] ) * f;
	tr->endpos[1] = start[1] + ( end[1] - start[1] ) * f;
	tr->endpos[2] = start[2] + ( end[2] - start[2] ) * f;
	tr->plane.normal[2] = 1.0f;
	tr->contents = CONTENTS_SOLID;
	tr->entityNum = ENTITYNUM_WORLD;
}

static int Sim_PointContents( const vec3_t point, int passEntityNum ) {
	return 0;
}

/*
=============================================================================

the simulated player

=============================================================================
*/

#define	PRIMARY		WP_RAILGUN		// "intervention" slot
#define	SECONDARY	WP_SHOTGUN		// "spas12" slot

#define	MAX_FRAMES	2048
#define	MAX_INPUTS	32

typedef enum {
	IN_SPRINT_DOWN,
	IN_SPRINT_UP,
	IN_FORWARD_DOWN,
	IN_FORWARD_UP,
	IN_WEAPNEXT
} inputType_t;

typedef struct {
	int			frame;
	inputType_t	type;
} input_t;

typedef struct {
	int				msec;		// time at the end of the frame
	int				weapon;
	int				weaponstate;
	int				weaponTime;
	int				weapAnim;
	qboolean		sprinting;
	float			speed;
	float			weights[WANIM_NUM];
	viewAnimPose_t	pose;
} frame_t;

typedef struct {
	playerState_t	ps;
	viewAnimator_t	va;
	int				weaponSelect;
	int				buttons;
	int				forwardmove;
	int				time;
	float			timeFrac;

	int				numFrames;
	frame_t			frames[MAX_FRAMES];
} sim_t;

static void Sim_Init( sim_t *sim, int weapon ) {
	memset( sim, 0, sizeof( *sim ) );

	sim->ps.pm_type = PM_NORMAL;
	sim->ps.origin[2] = 24.125f;		// standing on the floor
	sim->ps.gravity = 800;
	sim->ps.speed = 320;
	sim->ps.groundEntityNum = ENTITYNUM_NONE;
	sim->ps.stats[STAT_HEALTH] = 100;
	sim->ps.stats[STAT_MAX_HEALTH] = 100;
	sim->ps.stats[STAT_WEAPONS] = ( 1 << PRIMARY ) | ( 1 << SECONDARY );
	sim->ps.ammo[PRIMARY] = 20;
	sim->ps.ammo[SECONDARY] = 24;
	sim->ps.weapon = weapon;
	sim->ps.weaponstate = WEAPON_READY;
	sim->ps.commandTime = 1000;

	sim->weaponSelect = weapon;
	sim->time = 1000;
}

// one client frame: build the usercmd, run the shared movement code on it,
// then let the view animator look at the result
static void Sim_Frame( sim_t *sim, float fps ) {
	pmove_t		pm;
	frame_t		*f;
	float		exact;
	int			msec;
	int			i;

	// whole milliseconds per frame that average out to the frame rate
	exact = 1000.0f / fps + sim->timeFrac;
	msec = (int)exact;
	sim->timeFrac = exact - msec;
	sim->time += msec;

	memset( &pm, 0, sizeof( pm ) );
	pm.ps = &sim->ps;
	pm.cmd.serverTime = sim->time;
	pm.cmd.buttons = sim->buttons;
	pm.cmd.weapon = sim->weaponSelect;
	pm.cmd.forwardmove = sim->forwardmove;
	pm.tracemask = MASK_PLAYERSOLID;
	pm.trace = Sim_Trace;
	pm.pointcontents = Sim_PointContents;

	Pmove( &pm );

	ViewAnim_Update( &sim->va, sim->ps.weapon, sim->ps.weapAnim, sim->time );

	if ( sim->numFrames >= MAX_FRAMES ) {
		Com_Error( ERR_FATAL, "too many frames" );
	}
	f = &sim->frames[sim->numFrames++];
	f->msec = sim->time;
	f->weapon = sim->ps.weapon;
	f->weaponstate = sim->ps.weaponstate;
	f->weaponTime = sim->ps.weaponTime;
	f->weapAnim = sim->ps.weapAnim & ~WEAPANIM_TOGGLEBIT;
	f->sprinting = ( sim->ps.pm_flags & PMF_SPRINTING ) ? qtrue : qfalse;
	f->speed = sqrt( sim->ps.velocity[0] * sim->ps.velocity[0] + sim->ps.velocity[1] * sim->ps.velocity[1] );
	for ( i = 0 ; i < WANIM_NUM ; i++ ) {
		f->weights[i] = sim->va.slots[i].weight;
	}
	ViewAnim_Pose( &sim->va, &f->pose );
}

static void Sim_Input( sim_t *sim, inputType_t type ) {
	switch ( type ) {
	case IN_SPRINT_DOWN:
		sim->buttons |= BUTTON_SPRINT;
		break;
	case IN_SPRINT_UP:
		sim->buttons &= ~BUTTON_SPRINT;
		break;
	case IN_FORWARD_DOWN:
		sim->forwardmove = 127;
		break;
	case IN_FORWARD_UP:
		sim->forwardmove = 0;
		break;
	case IN_WEAPNEXT:
		sim->weaponSelect = ( sim->weaponSelect == PRIMARY ) ? SECONDARY : PRIMARY;
		break;
	}
}

// runs frames, applying each input at the start of its frame, until
// tailFrames frames after the last input
static void Sim_Run( sim_t *sim, float fps, const input_t *inputs, int numInputs, int tailFrames ) {
	int		frame, last, i;

	last = 0;
	for ( i = 0 ; i < numInputs ; i++ ) {
		if ( inputs[i].frame > last ) {
			last = inputs[i].frame;
		}
	}

	for ( frame = 0 ; frame <= last + tailFrames ; frame++ ) {
		for ( i = 0 ; i < numInputs ; i++ ) {
			if ( inputs[i].frame == frame ) {
				Sim_Input( sim, inputs[i].type );
			}
		}
		Sim_Frame( sim, fps );
	}
}

// first frame at or after `from` in which the given weapon is being raised
static int Sim_FindRaise( sim_t *sim, float fps, int weapon, int maxFrames ) {
	int		i;

	for ( i = 0 ; i < maxFrames ; i++ ) {
		Sim_Frame( sim, fps );
		if ( sim->ps.weapon == weapon && sim->ps.weaponstate == WEAPON_RAISING ) {
			return sim->numFrames - 1;
		}
	}
	Com_Error( ERR_FATAL, "weapon %i never started raising", weapon );
	return -1;
}

/*
=============================================================================

scenarios

=============================================================================
*/

typedef struct {
	int		mark;		// frame the interesting part starts at
} scenarioInfo_t;

static void AddInput( input_t *inputs, int *num, int frame, inputType_t type ) {
	inputs[*num].frame = frame;
	inputs[*num].type = type;
	( *num )++;
}

static void Scenario_Run( sim_t *sim, const char *name, float fps, int a, int b, int c, int d,
		scenarioInfo_t *info ) {
	input_t		inputs[MAX_INPUTS];
	int			num;
	int			base;

	num = 0;

	if ( !strcmp( name, "barrelroll" ) || !strcmp( name, "slow" ) ) {
		if ( !strcmp( name, "slow" ) ) {
			a = b = c = d = (int)fps;
		}

		// stand with the primary, pushing forward the whole time, then switch
		// so that the secondary starts coming up
		Sim_Init( sim, PRIMARY );
		sim->forwardmove = 127;
		Sim_Frame( sim, fps );
		Sim_Frame( sim, fps );
		Sim_Input( sim, IN_WEAPNEXT );
		Sim_FindRaise( sim, fps, SECONDARY, 400 );

		// the frame just simulated is frame 0 of the raise, and the next
		// frame to run is frame 1, so input frame numbers start one back
		base = -1;
		info->mark = sim->numFrames - 1;
		if ( a < 1 ) {
			a = 1;
		}

		// the four inputs
		AddInput( inputs, &num, base + a, IN_SPRINT_DOWN );
		AddInput( inputs, &num, base + a + b, IN_WEAPNEXT );
		AddInput( inputs, &num, base + a + b + c, IN_SPRINT_UP );
		AddInput( inputs, &num, base + a + b + c + d, IN_WEAPNEXT );
		Sim_Run( sim, fps, inputs, num, (int)( fps * 2.5f ) );
		return;
	}

	if ( !strcmp( name, "yy" ) ) {
		Sim_Init( sim, SECONDARY );
		Sim_Frame( sim, fps );
		Sim_Frame( sim, fps );
		info->mark = sim->numFrames - 1;
		AddInput( inputs, &num, 1, IN_WEAPNEXT );
		AddInput( inputs, &num, 1 + d, IN_WEAPNEXT );
		Sim_Run( sim, fps, inputs, num, (int)( fps * 2.5f ) );
		return;
	}

	if ( !strcmp( name, "sprint" ) ) {
		Sim_Init( sim, SECONDARY );
		sim->forwardmove = 127;
		Sim_Frame( sim, fps );
		Sim_Frame( sim, fps );
		info->mark = sim->numFrames - 1;
		AddInput( inputs, &num, 1, IN_SPRINT_DOWN );
		AddInput( inputs, &num, 1 + d, IN_SPRINT_UP );
		Sim_Run( sim, fps, inputs, num, (int)( fps * 2.5f ) );
		return;
	}

	Com_Error( ERR_FATAL, "unknown scenario '%s'", name );
}

/*
=============================================================================

measurements

=============================================================================
*/

typedef struct {
	int		maxBlended;		// most animations mixed in one frame
	int		framesOver2;	// frames with three or more animations mixed
	float	rollMin;		// roll about the barrel since the start, degrees
	float	rollMax;
	float	rollEnd;
	float	longestRun;		// most roll made in one direction without reversing, degrees
	float	turned;			// total angle the weapon turned through, degrees
	float	maxStep;		// largest turn between two frames, degrees
} measure_t;

static float QuatAngleBetween( const quat_t a, const quat_t b ) {
	float	dot;

	dot = fabs( a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] );
	if ( dot > 1.0f ) {
		dot = 1.0f;
	}
	return 2.0f * acos( dot ) * 180.0f / M_PI;
}

// How far the weapon rolled about its own barrel between two poses, in
// degrees. The turn from a to b is split into the part about the barrel and
// the rest, and only the barrel part is returned.
static float RollBetween( const quat_t a, const quat_t b ) {
	vec3_t	axis[3];
	quat_t	d;
	float	len, angle, sign;

	// d = b * conjugate( a ): the turn that takes a to b, in view space
	d[0] = -b[3] * a[0] + b[0] * a[3] - b[1] * a[2] + b[2] * a[1];
	d[1] = -b[3] * a[1] + b[0] * a[2] + b[1] * a[3] - b[2] * a[0];
	d[2] = -b[3] * a[2] - b[0] * a[1] + b[1] * a[0] + b[2] * a[3];
	d[3] =  b[3] * a[3] + b[0] * a[0] + b[1] * a[1] + b[2] * a[2];

	sign = d[3] < 0.0f ? -1.0f : 1.0f;
	len = sqrt( d[0] * d[0] + d[1] * d[1] + d[2] * d[2] );
	if ( len < 0.000001f ) {
		return 0.0f;
	}
	angle = 2.0f * atan2( len, fabs( d[3] ) ) * 180.0f / M_PI;

	// project the turn onto where the barrel points in pose a
	ViewAnim_QuatToAxis( a, axis );
	return sign * angle * ( d[0] * axis[0][0] + d[1] * axis[0][1] + d[2] * axis[0][2] ) / len;
}

static void Measure( const sim_t *sim, int from, measure_t *m ) {
	const frame_t	*f;
	float			roll, step, run, turn;
	int				i, j, n;

	memset( m, 0, sizeof( *m ) );
	roll = 0.0f;
	run = 0.0f;

	for ( i = from ; i < sim->numFrames ; i++ ) {
		f = &sim->frames[i];

		n = 0;
		for ( j = 0 ; j < WANIM_NUM ; j++ ) {
			if ( f->weights[j] > 0.01f ) {
				n++;
			}
		}
		if ( n > m->maxBlended ) {
			m->maxBlended = n;
		}
		if ( n >= 3 ) {
			m->framesOver2++;
		}

		if ( i == from ) {
			continue;
		}

		// another weapon is another view model, there is no motion between them
		if ( f->weapon != sim->frames[i - 1].weapon ) {
			run = 0.0f;
			continue;
		}

		step = RollBetween( sim->frames[i - 1].pose.rot, f->pose.rot );
		roll += step;
		if ( roll < m->rollMin ) {
			m->rollMin = roll;
		}
		if ( roll > m->rollMax ) {
			m->rollMax = roll;
		}

		// a run ends when the roll reverses by more than a few degrees
		if ( ( run >= 0.0f && step >= 0.0f ) || ( run <= 0.0f && step <= 0.0f ) || fabs( step ) < 0.05f ) {
			run += step;
		} else {
			run = step;
		}
		if ( fabs( run ) > fabs( m->longestRun ) ) {
			m->longestRun = run;
		}

		turn = QuatAngleBetween( f->pose.rot, sim->frames[i - 1].pose.rot );
		m->turned += turn;
		if ( turn > m->maxStep ) {
			m->maxStep = turn;
		}
	}
	m->rollEnd = roll;
}

/*
=============================================================================

output

=============================================================================
*/

static const char *stateNames[] = {
	"READY", "RAISING", "DROPPING", "FIRING", "RAISING_QUICK", "DROPPING_QUICK",
	"SPRINT_RAISE", "SPRINT_LOOP", "SPRINT_DROP"
};

static const char *animNames[WANIM_NUM] = {
	"none", "idle", "raise", "drop", "quick_raise", "quick_drop",
	"sprint_in", "sprint_loop", "sprint_out", "fire"
};

static void PrintTrace( const sim_t *sim, const scenarioInfo_t *info, const char *name, float fps ) {
	const frame_t	*f;
	vec3_t			axis[3];
	int				i, j;

	printf( "{\"scenario\":\"%s\",\"fps\":%g,\"mark\":%i,\n", name, fps, info->mark );
	printf( "\"anims\":[" );
	for ( j = 0 ; j < WANIM_NUM ; j++ ) {
		printf( "%s\"%s\"", j ? "," : "", animNames[j] );
	}
	printf( "],\n\"frames\":[\n" );
	for ( i = 0 ; i < sim->numFrames ; i++ ) {
		f = &sim->frames[i];
		ViewAnim_QuatToAxis( f->pose.rot, axis );
		printf( "{\"t\":%i,\"weapon\":%i,\"state\":\"%s\",\"anim\":\"%s\",\"wt\":%i,\"sprint\":%i,\"speed\":%.0f,",
			f->msec, f->weapon, stateNames[f->weaponstate], animNames[f->weapAnim], f->weaponTime,
			f->sprinting, f->speed );
		printf( "\"w\":[" );
		for ( j = 0 ; j < WANIM_NUM ; j++ ) {
			printf( "%s%.4f", j ? "," : "", f->weights[j] );
		}
		printf( "],\"o\":[%.4f,%.4f,%.4f],", f->pose.origin[0], f->pose.origin[1], f->pose.origin[2] );
		printf( "\"q\":[%.5f,%.5f,%.5f,%.5f],", f->pose.rot[0], f->pose.rot[1], f->pose.rot[2], f->pose.rot[3] );
		printf( "\"axis\":[[%.5f,%.5f,%.5f],[%.5f,%.5f,%.5f],[%.5f,%.5f,%.5f]]}%s\n",
			axis[0][0], axis[0][1], axis[0][2], axis[1][0], axis[1][1], axis[1][2],
			axis[2][0], axis[2][1], axis[2][2], i == sim->numFrames - 1 ? "" : "," );
	}
	printf( "]}\n" );
}

static void PrintSummary( const sim_t *sim, const scenarioInfo_t *info, const char *name ) {
	measure_t	m;
	int			i, last;

	Measure( sim, info->mark, &m );
	printf( "%-10s mixed=%i roll=[%7.1f,%7.1f] end=%7.1f longestRun=%7.1f turned=%6.1f maxStep=%5.1f  states:",
		name, m.maxBlended, m.rollMin, m.rollMax, m.rollEnd, m.longestRun, m.turned, m.maxStep );

	last = -1;
	for ( i = info->mark ; i < sim->numFrames ; i++ ) {
		int key = sim->frames[i].weaponstate * 100 + sim->frames[i].weapon;
		if ( key != last ) {
			printf( " %s(%i)@%i", stateNames[sim->frames[i].weaponstate], sim->frames[i].weapon, i - info->mark );
			last = key;
		}
	}
	printf( "\n" );
}

/*
=============================================================================

self test

=============================================================================
*/

static int failures;

#define	CHECK( cond, text ) \
	do { if ( !( cond ) ) { failures++; printf( "FAIL: %s  (%s:%i)\n", text, __FILE__, __LINE__ ); } \
	     else { printf( "ok:   %s\n", text ); } } while ( 0 )

static qboolean StateSeen( const sim_t *sim, int from, int state ) {
	int		i;

	for ( i = from ; i < sim->numFrames ; i++ ) {
		if ( sim->frames[i].weaponstate == state ) {
			return qtrue;
		}
	}
	return qfalse;
}

static int FirstFrameInState( const sim_t *sim, int from, int state ) {
	int		i;

	for ( i = from ; i < sim->numFrames ; i++ ) {
		if ( sim->frames[i].weaponstate == state ) {
			return i;
		}
	}
	return -1;
}

static void SelfTest( void ) {
	static sim_t	sim;
	scenarioInfo_t	info;
	measure_t		m;
	float			sum, worst;
	qboolean		weaponKept;
	int				i, j, a, b;

	// ---- the animator on its own ----
	{
		viewAnimator_t	va;
		viewAnimPose_t	pose;

		ViewAnim_Reset( &va );
		ViewAnim_Update( &va, SECONDARY, WANIM_IDLE, 0 );
		CHECK( va.slots[WANIM_IDLE].weight == 1.0f, "the first animation of a view model starts at full weight" );

		ViewAnim_Update( &va, SECONDARY, WANIM_DROP | WEAPANIM_TOGGLEBIT, 0 );
		ViewAnim_Update( &va, SECONDARY, WANIM_DROP | WEAPANIM_TOGGLEBIT, 100 );
		CHECK( fabs( va.slots[WANIM_DROP].weight - 0.5f ) < 0.001f
			&& fabs( va.slots[WANIM_IDLE].weight - 0.5f ) < 0.001f,
			"half way through a 200 msec blend both animations weigh one half" );

		ViewAnim_Update( &va, SECONDARY, WANIM_SPRINT_IN, 100 );
		ViewAnim_Update( &va, SECONDARY, WANIM_SPRINT_IN, 150 );
		CHECK( va.slots[WANIM_IDLE].weight > 0.3f && va.slots[WANIM_DROP].weight > 0.3f
			&& va.slots[WANIM_SPRINT_IN].weight > 0.2f,
			"a third animation started mid blend leaves three animations mixed" );

		ViewAnim_Update( &va, PRIMARY, WANIM_RAISE, 160 );
		sum = 0.0f;
		for ( i = 0 ; i < WANIM_NUM ; i++ ) {
			sum += va.slots[i].weight;
		}
		CHECK( va.slots[WANIM_RAISE].weight == 1.0f && sum == 1.0f,
			"changing weapon leaves nothing of the old view model to blend from" );

		ViewAnim_Reset( &va );
		ViewAnim_Pose( &va, &pose );
		CHECK( pose.rot[3] == 1.0f && pose.origin[0] == 0.0f, "an animator with nothing playing gives the idle pose" );
	}

	// ---- the state machine and the animator together ----
	Scenario_Run( &sim, "barrelroll", 57.0f, 2, 3, 3, 3, &info );
	Measure( &sim, info.mark, &m );

	worst = 0.0f;
	for ( i = 0 ; i < sim.numFrames ; i++ ) {
		sum = 0.0f;
		for ( j = 0 ; j < WANIM_NUM ; j++ ) {
			sum += sim.frames[i].weights[j];
		}
		if ( fabs( sum - 1.0f ) > worst ) {
			worst = fabs( sum - 1.0f );
		}
	}
	CHECK( worst < 0.001f, "the weights always add up to one" );

	weaponKept = qtrue;
	for ( i = info.mark ; i < sim.numFrames ; i++ ) {
		if ( sim.frames[i].weapon != SECONDARY ) {
			weaponKept = qfalse;
		}
	}
	CHECK( weaponKept, "switching twice during the drop never changes weapon, so the view model keeps its blend state" );

	a = FirstFrameInState( &sim, info.mark, WEAPON_SPRINT_RAISE );
	b = FirstFrameInState( &sim, info.mark, WEAPON_DROPPING );
	CHECK( a == info.mark + 2, "sprint takes over the raise on the frame it is pressed" );
	CHECK( b == a + 3, "the weapon switch takes over the sprint on the frame it is pressed" );
	CHECK( sim.frames[sim.numFrames - 1].weaponstate == WEAPON_READY
		&& sim.frames[sim.numFrames - 1].weapon == SECONDARY, "the sequence ends with the same weapon up and ready" );
	CHECK( !StateSeen( &sim, b, WEAPON_SPRINT_RAISE ), "letting go of sprint during the drop keeps the weapon out of sprint afterwards" );
	CHECK( m.maxBlended >= 3, "the quick sequence mixes three or more animations at once" );

	Scenario_Run( &sim, "slow", 57.0f, 0, 0, 0, 0, &info );
	Measure( &sim, info.mark, &m );
	CHECK( m.maxBlended <= 2, "the same inputs a second apart never mix more than two animations" );

	// sprinting
	Scenario_Run( &sim, "sprint", 57.0f, 0, 0, 0, 60, &info );
	CHECK( sim.frames[info.mark + 50].sprinting && sim.frames[info.mark + 50].weaponstate == WEAPON_SPRINT_LOOP,
		"holding sprint while moving forward ends up in the sprint loop" );
	CHECK( sim.frames[info.mark + 50].speed > 470.0f && sim.frames[info.mark + 50].speed < 490.0f,
		"sprinting runs at one and a half times the base speed" );
	CHECK( sim.frames[sim.numFrames - 1].weaponstate == WEAPON_READY && !sim.frames[sim.numFrames - 1].sprinting,
		"letting go of sprint brings the weapon back to ready" );

	// plain double switch
	Scenario_Run( &sim, "yy", 57.0f, 0, 0, 0, 5, &info );
	CHECK( !StateSeen( &sim, info.mark, WEAPON_SPRINT_RAISE ) && sim.frames[sim.numFrames - 1].weapon == SECONDARY,
		"a double switch on its own drops and raises the same weapon" );

	// the sequence is not tied to one frame rate
	Scenario_Run( &sim, "barrelroll", 125.0f, 4, 7, 7, 7, &info );
	CHECK( sim.frames[sim.numFrames - 1].weaponstate == WEAPON_READY, "the sequence also completes at 125 fps" );

	// ---- what comes out with the placeholder SPAS-12 animations ----
	// These describe the data in cg_viewanim.c, not the code. They are
	// expected to change when the animations are replaced.
	printf( "\nwith the placeholder SPAS-12 animations:\n" );

	Scenario_Run( &sim, "barrelroll", 57.0f, 6, 15, 8, 2, &info );
	Measure( &sim, info.mark, &m );
	CHECK( m.rollMin < -300.0f && m.maxStep < 30.0f,
		"sprint early in the raise, then switch twice: the weapon rolls most of a full turn one way without snapping" );

	Scenario_Run( &sim, "barrelroll", 57.0f, 20, 15, 8, 2, &info );
	Measure( &sim, info.mark, &m );
	CHECK( m.rollMin > -45.0f, "the same inputs once the raise is mostly done do not roll it that way" );

	Scenario_Run( &sim, "yy", 57.0f, 0, 0, 0, 5, &info );
	Measure( &sim, info.mark, &m );
	CHECK( fabs( m.longestRun ) < 130.0f, "a double switch on its own only turns it a third of a turn and back" );

	Scenario_Run( &sim, "sprint", 57.0f, 0, 0, 0, 30, &info );
	Measure( &sim, info.mark, &m );
	CHECK( fabs( m.longestRun ) < 140.0f, "sprinting on its own only turns it a third of a turn and back" );

	printf( "\n%s\n", failures ? "SELF TEST FAILED" : "self test passed" );
}

int main( int argc, char **argv ) {
	static sim_t	sim;
	scenarioInfo_t	info;
	const char		*mode, *name;
	float			fps;
	int				a, b, c, d;

	if ( argc < 2 ) {
		fprintf( stderr, "usage: weaponsim selftest | trace <scenario> [fps] [a b c d] | summary <scenario> [fps] [a b c d]\n" );
		return 1;
	}
	mode = argv[1];

	if ( !strcmp( mode, "selftest" ) ) {
		SelfTest();
		return failures ? 1 : 0;
	}

	name = argc > 2 ? argv[2] : "barrelroll";
	fps = argc > 3 ? atof( argv[3] ) : 57.0f;
	a = argc > 4 ? atoi( argv[4] ) : 2;
	b = argc > 5 ? atoi( argv[5] ) : 3;
	c = argc > 6 ? atoi( argv[6] ) : 3;
	d = argc > 7 ? atoi( argv[7] ) : 3;

	Scenario_Run( &sim, name, fps, a, b, c, d, &info );

	if ( !strcmp( mode, "trace" ) ) {
		PrintTrace( &sim, &info, name, fps );
	} else {
		PrintSummary( &sim, &info, name );
	}
	return 0;
}
