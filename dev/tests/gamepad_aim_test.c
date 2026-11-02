/*
 * MW2's game pad (code/client/cl_gamepadaim.c): the stick deadzone, the
 * squared move and its diagonal stretch, the aim graph, the dominant axis
 * scale, the hip/ADS turn rates times input_viewSensitivity and the
 * turn-rate acceleration, through the real functions the client calls.
 *
 * The tuning is MW2's defaults as CL_InitInput registers them. Expectations
 * are worked out by hand from those numbers, not by the code's formula.
 */
#include "../../code/qcommon/q_shared.h"
#include "../../code/game/bg_public.h"
#include "../../code/client/cl_gamepadaim.h"
#include <stdio.h>
#include <stdlib.h>

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) <= (eps))

#define FRAME	0.008f		// com_maxfps 125

static aimTuning_t MW2Tuning( float sensitivity ) {
	aimTuning_t t;
	t.yaw = 260.0f;
	t.pitch = 90.0f;
	t.yawAds = 90.0f;
	t.pitchAds = 55.0f;
	t.sensitivity = sensitivity;
	t.accelEnabled = qtrue;
	t.accel = 1200.0f;
	t.scaleViewAxis = qtrue;
	t.graph = NULL;
	return t;
}

// Hold the stick for a time and return the speed it settles at.
static void Hold( const aimTuning_t *t, aimState_t *s, float yaw, float pitch, float ads, float seconds ) {
	float dy, dp;
	int i, frames = (int)( seconds / FRAME + 0.5f );
	for ( i = 0; i < frames; i++ ) {
		CL_AimTurn( t, s, yaw, pitch, ads, FRAME, &dy, &dp );
	}
}

static void TestDeadzone( void ) {
	float x, y;

	// Inside the inner deadzone, by length rather than per axis.
	CL_StickDeadzone( 0.1f, 0.0f, 0.2f, 0.01f, &x, &y );
	CHECK( x == 0.0f && y == 0.0f );
	CL_StickDeadzone( 0.15f, 0.1f, 0.2f, 0.01f, &x, &y );	// length 0.18
	CHECK( x == 0.0f && y == 0.0f );
	CL_StickDeadzone( 0.2f, 0.0f, 0.2f, 0.01f, &x, &y );	// at the edge: 0
	CHECK( NEAR( x, 0.0f, 1e-6 ) && y == 0.0f );

	// Rescaled over 0.2..0.99: 0.595 is halfway, 0.99 is full.
	CL_StickDeadzone( 0.595f, 0.0f, 0.2f, 0.01f, &x, &y );
	CHECK( NEAR( x, 0.5f, 1e-5 ) && y == 0.0f );
	CL_StickDeadzone( 0.0f, -0.99f, 0.2f, 0.01f, &x, &y );
	CHECK( NEAR( y, -1.0f, 1e-5 ) && x == 0.0f );
	CL_StickDeadzone( -32768.0f / 32767.0f, 0.0f, 0.2f, 0.01f, &x, &y );
	CHECK( NEAR( x, -1.0f, 1e-6 ) );

	// A diagonal keeps its direction: (0.5, 0.5) is length 0.70711, which
	// reads (0.70711 - 0.2) / 0.79 = 0.64191, so 0.45390 on each axis.
	CL_StickDeadzone( 0.5f, 0.5f, 0.2f, 0.01f, &x, &y );
	CHECK( NEAR( x, 0.45390f, 1e-4 ) && NEAR( x, y, 1e-7 ) );
	CL_StickDeadzone( 0.3f, -0.4f, 0.2f, 0.01f, &x, &y );	// length 0.5
	CHECK( NEAR( x / y, -0.75f, 1e-5 ) );
}

static void TestMove( void ) {
	int f, r;
	float threshold;

	CL_StickMove( 0.0f, 0.0f, &f, &r );
	CHECK( f == 0 && r == 0 );

	// A full push is the edge of the move square, in every direction.
	CL_StickMove( 1.0f, 0.0f, &f, &r );
	CHECK( f == 127 && r == 0 );
	CL_StickMove( -1.0f, 0.0f, &f, &r );
	CHECK( f == -127 && r == 0 );
	CL_StickMove( 0.0f, 1.0f, &f, &r );
	CHECK( f == 0 && r == 127 );
	CL_StickMove( 0.0f, -1.0f, &f, &r );
	CHECK( f == 0 && r == -127 );

	// The speed is the square of the push: half a stick is a quarter,
	// 0.25 * 127 = 31.75, floored (so -31.75 is -32, not -31).
	CL_StickMove( 0.5f, 0.0f, &f, &r );
	CHECK( f == 31 && r == 0 );
	CL_StickMove( -0.5f, 0.0f, &f, &r );
	CHECK( f == -32 && r == 0 );

	// (0.3, 0.4) is length 0.5, so (0.15, 0.2), stretched by
	// sqrt(1 + 0.75^2) = 1.25 onto the square: 23.81 and 31.75.
	CL_StickMove( 0.3f, 0.4f, &f, &r );
	CHECK( f == 23 && r == 31 );

	// A full diagonal reaches the corner, as both keys held would.
	CL_StickMove( 0.70710678f, 0.70710678f, &f, &r );
	CHECK( f == r && f >= 126 && f <= 127 );

	// Past a full stick the axis stays full (IW4x's clamp).
	CL_StickMove( 1.2f, 0.0f, &f, &r );
	CHECK( f == 127 );

	// Sprint wants more than BG_SPRINT_FORWARD_MINIMUM forward
	// (PM_CheckSprint), so about 91% of the stick past the deadzone.
	threshold = sqrt( ( BG_SPRINT_FORWARD_MINIMUM + 1 ) / 127.0 );
	CHECK( threshold > 0.91f && threshold < 0.92f );
	CL_StickMove( threshold - 0.001f, 0.0f, &f, &r );
	CHECK( f <= BG_SPRINT_FORWARD_MINIMUM );
	CL_StickMove( threshold + 0.001f, 0.0f, &f, &r );
	CHECK( f > BG_SPRINT_FORWARD_MINIMUM );
}

static void TestTurnRates( void ) {
	aimTuning_t t = MW2Tuning( 4.0f );	// "Insane", sensitivity 10
	aimState_t s = { 0, 0 };

	// Held full: 260 * 4 across, 90 * 4 up and down.
	Hold( &t, &s, 1.0f, 0.0f, 0.0f, 1.0f );
	CHECK( NEAR( s.yawSpeed, 1040.0f, 1e-3 ) && s.pitchSpeed == 0.0f );
	s.yawSpeed = s.pitchSpeed = 0.0f;
	Hold( &t, &s, 0.0f, 1.0f, 0.0f, 1.0f );
	CHECK( NEAR( s.pitchSpeed, 360.0f, 1e-3 ) && s.yawSpeed == 0.0f );

	// Fully aimed: 90 * 4 and 55 * 4. Halfway: 175 * 4 and 72.5 * 4.
	s.yawSpeed = s.pitchSpeed = 0.0f;
	Hold( &t, &s, 1.0f, 0.0f, 1.0f, 1.0f );
	CHECK( NEAR( s.yawSpeed, 360.0f, 1e-3 ) );
	s.yawSpeed = s.pitchSpeed = 0.0f;
	Hold( &t, &s, 0.0f, 1.0f, 1.0f, 1.0f );
	CHECK( NEAR( s.pitchSpeed, 220.0f, 1e-3 ) );
	s.yawSpeed = s.pitchSpeed = 0.0f;
	Hold( &t, &s, 1.0f, 0.0f, 0.5f, 1.0f );
	CHECK( NEAR( s.yawSpeed, 700.0f, 1e-3 ) );
	s.yawSpeed = s.pitchSpeed = 0.0f;
	Hold( &t, &s, 0.0f, 1.0f, 0.5f, 1.0f );
	CHECK( NEAR( s.pitchSpeed, 290.0f, 1e-3 ) );

	// The aim fraction is clamped, and garbage reads as the hip.
	s.yawSpeed = s.pitchSpeed = 0.0f;
	Hold( &t, &s, 1.0f, 0.0f, 3.0f, 1.0f );
	CHECK( NEAR( s.yawSpeed, 360.0f, 1e-3 ) );
	s.yawSpeed = s.pitchSpeed = 0.0f;
	Hold( &t, &s, 1.0f, 0.0f, sqrt( -1.0 ), 1.0f );
	CHECK( NEAR( s.yawSpeed, 1040.0f, 1e-3 ) );
}

static void TestAcceleration( void ) {
	aimTuning_t t = MW2Tuning( 4.0f );
	aimState_t s = { 0, 0 };
	float dy, dp, turned = 0.0f, seconds = 0.0f;
	int i, frames;

	// From rest, full speed takes 260 / 1200 = 0.2167 s at any sensitivity:
	// the rate and the acceleration both scale with it. At 8 ms that is 28
	// frames (27 * 38.4 = 1036.8 < 1040).
	for ( frames = 0; s.yawSpeed < 1040.0f && frames < 1000; frames++ ) {
		CL_AimTurn( &t, &s, 1.0f, 0.0f, 0.0f, FRAME, &dy, &dp );
	}
	CHECK( frames == 28 );
	t = MW2Tuning( 1.0f );
	s.yawSpeed = 0.0f;
	for ( frames = 0; s.yawSpeed < 260.0f && frames < 1000; frames++ ) {
		CL_AimTurn( &t, &s, 1.0f, 0.0f, 0.0f, FRAME, &dy, &dp );
	}
	CHECK( frames == 28 );

	// Slowing down is immediate, and so is stopping.
	t = MW2Tuning( 4.0f );
	Hold( &t, &s, 1.0f, 0.0f, 0.0f, 1.0f );
	CHECK( NEAR( s.yawSpeed, 1040.0f, 1e-3 ) );
	CL_AimTurn( &t, &s, 0.5f, 0.0f, 0.0f, FRAME, &dy, &dp );
	CHECK( NEAR( s.yawSpeed, 520.0f, 1e-3 ) );
	CL_AimTurn( &t, &s, 0.0f, 0.0f, 0.0f, FRAME, &dy, &dp );
	CHECK( s.yawSpeed == 0.0f && dy == 0.0f );

	// The speed is the stick's size: flipping direction keeps it.
	Hold( &t, &s, 1.0f, 0.0f, 0.0f, 1.0f );
	CL_AimTurn( &t, &s, -1.0f, 0.0f, 0.0f, FRAME, &dy, &dp );
	CHECK( NEAR( s.yawSpeed, 1040.0f, 1e-3 ) && NEAR( dy, -1040.0f * FRAME, 1e-4 ) );

	// A 180 at full tilt from rest: the 0.2167 s ramp covers 112.7 degrees,
	// the last 67.3 take 0.0647 s more, about 0.281 s in all.
	s.yawSpeed = 0.0f;
	for ( i = 0; turned < 180.0f && i < 1000; i++ ) {
		CL_AimTurn( &t, &s, 1.0f, 0.0f, 0.0f, FRAME, &dy, &dp );
		turned += dy;
		seconds += FRAME;
	}
	CHECK( seconds >= 0.272f && seconds <= 0.290f );

	// Without acceleration the full speed is there on the first frame.
	t.accelEnabled = qfalse;
	s.yawSpeed = 0.0f;
	CL_AimTurn( &t, &s, 1.0f, 0.0f, 0.0f, FRAME, &dy, &dp );
	CHECK( NEAR( s.yawSpeed, 1040.0f, 1e-3 ) && NEAR( dy, 1040.0f * FRAME, 1e-4 ) );

	// No time, no turn.
	CL_AimTurn( &t, &s, 1.0f, 1.0f, 0.0f, 0.0f, &dy, &dp );
	CHECK( dy == 0.0f && dp == 0.0f );
}

static void TestDominantAxis( void ) {
	aimTuning_t t = MW2Tuning( 1.0f );
	aimState_t s = { 0, 0 };
	float dy, dp;

	t.accelEnabled = qfalse;

	// Yaw leads by 0.5, so pitch keeps 1 - 0.5 of itself: 0.25 of 90.
	CL_AimTurn( &t, &s, 1.0f, 0.5f, 0.0f, FRAME, &dy, &dp );
	CHECK( NEAR( s.yawSpeed, 260.0f, 1e-3 ) && NEAR( s.pitchSpeed, 22.5f, 1e-3 ) );
	// Pitch leads: yaw keeps 1 - (0.8 - 0.2) of 0.2.
	CL_AimTurn( &t, &s, -0.2f, 0.8f, 0.0f, FRAME, &dy, &dp );
	CHECK( NEAR( s.yawSpeed, 0.08f * 260.0f, 1e-3 ) && dy < 0.0f );
	// Equal axes are both kept.
	CL_AimTurn( &t, &s, 0.6f, -0.6f, 0.0f, FRAME, &dy, &dp );
	CHECK( NEAR( s.yawSpeed, 156.0f, 1e-3 ) && NEAR( s.pitchSpeed, 54.0f, 1e-3 ) && dp < 0.0f );
	// Off, the minor axis is untouched.
	t.scaleViewAxis = qfalse;
	CL_AimTurn( &t, &s, 1.0f, 0.5f, 0.0f, FRAME, &dy, &dp );
	CHECK( NEAR( s.pitchSpeed, 45.0f, 1e-3 ) );
}

static void TestGraph( void ) {
	aimGraph_t g;
	aimTuning_t t = MW2Tuning( 1.0f );
	aimState_t s = { 0, 0 };
	float dy, dp;
	char many[1024];
	int i, n;

	CHECK( CL_ParseAimGraph( "0 0  0.5 0.25  1 1", 1.0f, &g ) && g.knotCount == 3 );
	CHECK( NEAR( CL_AimGraphValue( &g, 0.0f ), 0.0f, 1e-6 ) );
	CHECK( NEAR( CL_AimGraphValue( &g, 0.5f ), 0.25f, 1e-6 ) );
	CHECK( NEAR( CL_AimGraphValue( &g, 0.75f ), 0.625f, 1e-6 ) );
	CHECK( NEAR( CL_AimGraphValue( &g, 1.0f ), 1.0f, 1e-6 ) );

	// The graph shapes the stick by its length: half a stick is a quarter.
	t.accelEnabled = qfalse;
	t.graph = &g;
	CL_AimTurn( &t, &s, 0.5f, 0.0f, 0.0f, FRAME, &dy, &dp );
	CHECK( NEAR( s.yawSpeed, 0.125f * 260.0f, 1e-3 ) );
	CL_AimTurn( &t, &s, 1.0f, 0.0f, 0.0f, FRAME, &dy, &dp );
	CHECK( NEAR( s.yawSpeed, 260.0f, 1e-3 ) );
	// It reads the stick's length, not an axis: (0.6, 0.8) is a full stick,
	// so only the dominant-axis scale touches it (yaw keeps 1 - 0.2 of 0.6).
	CL_AimTurn( &t, &s, 0.6f, 0.8f, 0.0f, FRAME, &dy, &dp );
	CHECK( NEAR( s.yawSpeed, 0.48f * 260.0f, 1e-3 ) && NEAR( s.pitchSpeed, 0.8f * 90.0f, 1e-3 ) );

	// The scale applies to every value, and commas may separate knots.
	CHECK( CL_ParseAimGraph( "0,0, 1,2", 0.5f, &g ) && NEAR( CL_AimGraphValue( &g, 1.0f ), 1.0f, 1e-6 ) );

	// Anything MW2 could not store, or that would reach its -1, is no graph.
	CHECK( !CL_ParseAimGraph( "", 1.0f, &g ) && g.knotCount == 0 );
	CHECK( !CL_ParseAimGraph( "0 0", 1.0f, &g ) );				// one knot
	CHECK( !CL_ParseAimGraph( "0 0 1", 1.0f, &g ) );			// odd
	CHECK( !CL_ParseAimGraph( "0 0 0.8 1", 1.0f, &g ) );		// stops short of 1
	CHECK( !CL_ParseAimGraph( "0.5 0 0.4 1 1 1", 1.0f, &g ) );	// not rising
	CHECK( !CL_ParseAimGraph( "0 0 0 0.5 1 1", 1.0f, &g ) );	// repeated fraction
	CHECK( !CL_ParseAimGraph( "0 0 1.5 1", 1.0f, &g ) );		// past 1
	CHECK( !CL_ParseAimGraph( "-0.1 0 1 1", 1.0f, &g ) );		// before 0
	CHECK( !CL_ParseAimGraph( "0 0 x 1", 1.0f, &g ) );
	CHECK( !CL_ParseAimGraph( "0 0 1 1", sqrt( -1.0 ), &g ) );

	// 32 knots fit, 33 do not.
	for ( n = 32; n <= 33; n++ ) {
		many[0] = '\0';
		for ( i = 0; i < n; i++ ) {
			snprintf( many + strlen( many ), sizeof( many ) - strlen( many ), "%g %g ",
				(double)i / ( n - 1 ), (double)i / ( n - 1 ) );
		}
		CHECK( CL_ParseAimGraph( many, 1.0f, &g ) == ( n == 32 ) );
	}
}

int main( void ) {
	TestDeadzone();
	TestMove();
	TestTurnRates();
	TestAcceleration();
	TestDominantAxis();
	TestGraph();
	printf( "PASS: %d game pad aim checks\n", checks );
	return 0;
}
