/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// cl_gamepadaim.c -- how MW2 moves and turns the view from a game pad's sticks
//
// PC MW2 keeps the console game's aim code: the aim_turnrate_*,
// aim_accel_turnrate_*, aim_input_graph_* and aim_scale_view_axis dvars are
// in iw4mp.exe (IW4x's controller support first looked them all up with
// Dvar_FindVar), and so are its input graphs (aaInputGraph, 0x7A2FC0). IW4x
// ported the functions that read them, AimAssist_CalcAdjustedAxis and
// AimAssist_ApplyTurnRates, the stick deadzone, GPad_ConvertStickToFloat,
// and the move, CL_GamepadMove with the console's thumbstick_default.cfg
// axis mapping (iw4x-client e49e17d6, Gamepad.cpp; iw4x-rawfiles 403ef82).
// These are those functions, as pure functions of their inputs so a harness
// drives the real rule.
//
// Not here: aim assist slowdown (Peace has no aim assist) and
// aaGlob.fovTurnRateScale, which MW2's AimAssist_UpdateTweakables (0x569950)
// computes and IW4x only ever reads; it is 1 until that function is read.

#include "../qcommon/q_shared.h"
#include "cl_gamepadaim.h"

static qboolean CL_AimFinite( float v ) {
	return v == v && v > -1e30f && v < 1e30f;
}

/*
=================
CL_StickDeadzone

GPad_ConvertStickToFloat: the deadzone is radial, so a diagonal keeps its
direction. Inside inner the stick reads nothing; past 1 - outer it reads
full; between, its length is rescaled to run 0..1.
=================
*/
void CL_StickDeadzone( float x, float y, float inner, float outer, float *outX, float *outY ) {
	float	len = sqrt( x * x + y * y );
	float	span = 1.0f - ( inner + outer );
	float	scaled;

	if ( len <= 0.0f || len < inner ) {
		*outX = *outY = 0.0f;
		return;
	}

	if ( len <= 1.0f - outer && span > 0.0f ) {
		scaled = ( len - inner ) / span;
	} else {
		scaled = 1.0f;
	}

	*outX = x / len * scaled;
	*outY = y / len * scaled;
}

/*
=================
CL_StickMove

CL_GamepadMove's move. thumbstick_default.cfg maps the move stick
MAP_SQUARED (CL_GamepadAxisValue): each axis times the stick's length, so
the speed follows the square of the push. The pair is then stretched from
the stick's circle onto the move square by sqrt(1 + (minor / major)^2), so
a full diagonal reaches the corner as a full push reaches the edge, and
floored into the command's units.
=================
*/
void CL_StickMove( float forward, float side, int *forwardMove, int *rightMove ) {
	float	length = sqrt( forward * forward + side * side );
	float	moveScale = 127.0f;

	forward *= length;
	side *= length;
	forward = forward < -1.0f ? -1.0f : forward > 1.0f ? 1.0f : forward;
	side = side < -1.0f ? -1.0f : side > 1.0f ? 1.0f : side;

	if ( fabs( side ) > 0.0f || fabs( forward ) > 0.0f ) {
		float	ratio = fabs( side ) <= fabs( forward ) ? side / forward : forward / side;

		moveScale = sqrt( ratio * ratio + 1.0f ) * moveScale;
	}

	*forwardMove = (int)floor( forward * moveScale );
	*rightMove = (int)floor( side * moveScale );
}

/*
=================
CL_ParseAimGraph

An aim graph as MW2 stores one (GraphFloat): up to 32 knots of
(fraction, value), fractions strictly increasing within 0..1, the last at 1,
and a scale applied to every value. Anything else, including an empty
string, leaves no graph.
=================
*/
qboolean CL_ParseAimGraph( const char *knots, float scale, aimGraph_t *graph ) {
	float	values[AIM_GRAPH_MAX_KNOTS * 2];
	int		count = 0;
	int		i;

	graph->knotCount = 0;
	graph->scale = 1.0f;

	if ( !knots || !CL_AimFinite( scale ) ) {
		return qfalse;
	}

	for ( ;; ) {
		char	*end;

		while ( *knots == ' ' || *knots == '\t' || *knots == ',' ) {
			knots++;
		}
		if ( !*knots ) {
			break;
		}
		if ( count == ARRAY_LEN( values ) ) {
			return qfalse;
		}
		values[count] = (float)strtod( knots, &end );
		if ( end == knots || !CL_AimFinite( values[count] ) ) {
			return qfalse;
		}
		knots = end;
		count++;
	}

	if ( count < 4 || ( count & 1 ) ) {
		return qfalse;
	}

	for ( i = 0; i < count; i += 2 ) {
		if ( values[i] < 0.0f || values[i] > 1.0f ) {
			return qfalse;
		}
		if ( i > 0 && values[i] <= values[i - 2] ) {
			return qfalse;
		}
	}
	// MW2 answers -1 past the last knot; a graph that reaches 1 never asks.
	if ( values[count - 2] < 1.0f ) {
		return qfalse;
	}

	for ( i = 0; i < count / 2; i++ ) {
		graph->knots[i][0] = values[i * 2];
		graph->knots[i][1] = values[i * 2 + 1];
	}
	graph->knotCount = count / 2;
	graph->scale = scale;
	return qtrue;
}

/*
=================
CL_AimGraphValue

GraphFloat_GetValue: the value at the first knot at or past the fraction,
interpolated from the knot before it, times the graph's scale.
=================
*/
float CL_AimGraphValue( const aimGraph_t *graph, float fraction ) {
	int		i;

	for ( i = 1; i < graph->knotCount; i++ ) {
		if ( graph->knots[i][0] >= fraction ) {
			float t = ( fraction - graph->knots[i - 1][0] ) /
				( graph->knots[i][0] - graph->knots[i - 1][0] );

			return ( ( graph->knots[i][1] - graph->knots[i - 1][1] ) * t +
				graph->knots[i - 1][1] ) * graph->scale;
		}
	}

	return -1.0f * graph->scale;
}

/*
=================
CL_LinearTrack

MW2's LinearTrack: move current toward target by rate * dt, landing on it.
=================
*/
static float CL_LinearTrack( float target, float current, float rate, float dt ) {
	float	err = target - current;
	float	step = err <= 0.0f ? -rate * dt : rate * dt;

	if ( fabs( err ) <= 0.001f ) {
		return target;
	}
	if ( fabs( step ) <= fabs( err ) ) {
		return current + step;
	}
	return target;
}

/*
=================
CL_AimTurn

AimAssist_CalcAdjustedAxis then AimAssist_ApplyTurnRates. The stick (yawAxis,
pitchAxis, already past the deadzone, each -1..1 in the direction the view
angle grows) is shaped by the graph over its length, then the smaller axis
is scaled down by how much the larger one leads. Each axis turns at its
share of the hip turn rate lerped toward the ADS one by adsLerp, times the
sensitivity. With acceleration on, a faster turn is reached at
accel * sensitivity deg/s^2, while a slower one is taken at once; the speed
is the stick's size, so flipping direction keeps it.
=================
*/
void CL_AimTurn( const aimTuning_t *tuning, aimState_t *state, float yawAxis, float pitchAxis,
		float adsLerp, float dt, float *yawDelta, float *pitchDelta ) {
	float	yawRate, pitchRate;
	float	yawTarget, pitchTarget;
	float	yawSign, pitchSign;

	if ( tuning->graph && tuning->graph->knotCount >= 2 ) {
		float	deflection = sqrt( yawAxis * yawAxis + pitchAxis * pitchAxis );
		float	response;

		if ( deflection > 1.0f ) {
			deflection = 1.0f;
		}
		response = CL_AimGraphValue( tuning->graph, deflection );
		yawAxis *= response;
		pitchAxis *= response;
	}

	if ( tuning->scaleViewAxis ) {
		float	absYaw = fabs( yawAxis );
		float	absPitch = fabs( pitchAxis );

		if ( absPitch <= absYaw ) {
			pitchAxis *= 1.0f - ( absYaw - absPitch );
		} else {
			yawAxis *= 1.0f - ( absPitch - absYaw );
		}
	}

	if ( !( adsLerp >= 0.0f ) ) {
		adsLerp = 0.0f;
	} else if ( adsLerp > 1.0f ) {
		adsLerp = 1.0f;
	}

	yawRate = ( ( tuning->yawAds - tuning->yaw ) * adsLerp + tuning->yaw ) * tuning->sensitivity;
	pitchRate = ( ( tuning->pitchAds - tuning->pitch ) * adsLerp + tuning->pitch ) * tuning->sensitivity;

	yawSign = yawAxis >= 0.0f ? 1.0f : -1.0f;
	pitchSign = pitchAxis >= 0.0f ? 1.0f : -1.0f;
	yawTarget = fabs( yawAxis ) * yawRate;
	pitchTarget = fabs( pitchAxis ) * pitchRate;

	if ( !tuning->accelEnabled ) {
		state->yawSpeed = yawTarget;
		state->pitchSpeed = pitchTarget;
	} else {
		float	accel = tuning->accel * tuning->sensitivity;

		state->yawSpeed = yawTarget <= state->yawSpeed ? yawTarget :
			CL_LinearTrack( yawTarget, state->yawSpeed, accel, dt );
		state->pitchSpeed = pitchTarget <= state->pitchSpeed ? pitchTarget :
			CL_LinearTrack( pitchTarget, state->pitchSpeed, accel, dt );
	}

	*yawDelta = state->yawSpeed * dt * yawSign;
	*pitchDelta = state->pitchSpeed * dt * pitchSign;
}
