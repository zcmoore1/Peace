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
// cl_gamepadaim.h -- MW2's game pad: stick deadzone, move and turn rates

#ifndef CL_GAMEPADAIM_H
#define CL_GAMEPADAIM_H

// MW2's GraphFloat holds at most 32 knots (iw4x Structs.hpp GraphFloat).
#define	AIM_GRAPH_MAX_KNOTS		32

typedef struct {
	int			knotCount;		// 0: no graph
	float		knots[AIM_GRAPH_MAX_KNOTS][2];	// (fraction, value) pairs
	float		scale;			// GraphFloat.scale, applied to every value
} aimGraph_t;

// The dvars MW2's AimAssist_ApplyTurnRates reads, as one frame's tuning.
typedef struct {
	float		yaw, pitch;			// aim_turnrate_yaw/pitch, deg/s at the hip
	float		yawAds, pitchAds;	// aim_turnrate_yaw_ads/pitch_ads
	float		sensitivity;		// input_viewSensitivity
	qboolean	accelEnabled;		// aim_accel_turnrate_enabled
	float		accel;				// aim_accel_turnrate_lerp, deg/s^2 before sensitivity
	qboolean	scaleViewAxis;		// aim_scale_view_axis
	const aimGraph_t *graph;		// the aim input graph, or NULL for none
} aimTuning_t;

// The turn speed MW2 carries between frames (aaGlob.yawDelta/pitchDelta).
typedef struct {
	float		yawSpeed, pitchSpeed;	// deg/s, never negative
} aimState_t;

void		CL_StickDeadzone( float x, float y, float inner, float outer, float *outX, float *outY );
void		CL_StickMove( float forward, float side, int *forwardMove, int *rightMove );
qboolean	CL_ParseAimGraph( const char *knots, float scale, aimGraph_t *graph );
float		CL_AimGraphValue( const aimGraph_t *graph, float fraction );
void		CL_AimTurn( const aimTuning_t *tuning, aimState_t *state, float yawAxis, float pitchAxis,
				float adsLerp, float dt, float *yawDelta, float *pitchDelta );

#endif
