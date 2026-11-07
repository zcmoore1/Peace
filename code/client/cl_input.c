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
// cl.input.c  -- builds an intended movement command to send to the server

#include "client.h"

unsigned	frame_msec;
int			old_com_frameTime;

/*
===============================================================================

KEY BUTTONS

Continuous button event tracking is complicated by the fact that two different
input sources (say, mouse button 1 and the control key) can both press the
same button, but the button should only be released when both of the
pressing key have been released.

When a key event issues a button command (+forward, +attack, etc), it appends
its key number as argv(1) so it can be matched up with the release.

argv(2) will be set to the time the event happened, which allows exact
control even at low framerates when the down and up events may both get qued
at the same time.

===============================================================================
*/


kbutton_t	in_left, in_right, in_forward, in_back;
kbutton_t	in_lookup, in_lookdown, in_moveleft, in_moveright;
kbutton_t	in_strafe, in_speed;
kbutton_t	in_up, in_down;

#ifdef USE_VOIP
kbutton_t	in_voiprecord;
#endif

kbutton_t	in_buttons[32];


qboolean	in_mlooking;


void IN_MLookDown( void ) {
	in_mlooking = qtrue;
}

void IN_MLookUp( void ) {
	in_mlooking = qfalse;
	if ( !cl_freelook->integer ) {
		IN_CenterView ();
	}
}

void IN_KeyDown( kbutton_t *b ) {
	int		k;
	char	*c;
	
	c = Cmd_Argv(1);
	if ( c[0] ) {
		k = atoi(c);
	} else {
		k = -1;		// typed manually at the console for continuous down
	}

	if ( k == b->down[0] || k == b->down[1] ) {
		return;		// repeating key
	}
	
	if ( !b->down[0] ) {
		b->down[0] = k;
	} else if ( !b->down[1] ) {
		b->down[1] = k;
	} else {
		Com_Printf ("Three keys down for a button!\n");
		return;
	}
	
	if ( b->active ) {
		return;		// still down
	}

	// save timestamp for partial frame summing
	c = Cmd_Argv(2);
	b->downtime = atoi(c);

	b->active = qtrue;
	b->wasPressed = qtrue;
}

void IN_KeyUp( kbutton_t *b ) {
	int		k;
	char	*c;
	unsigned	uptime;

	c = Cmd_Argv(1);
	if ( c[0] ) {
		k = atoi(c);
	} else {
		// typed manually at the console, assume for unsticking, so clear all
		b->down[0] = b->down[1] = 0;
		b->active = qfalse;
		return;
	}

	if ( b->down[0] == k ) {
		b->down[0] = 0;
	} else if ( b->down[1] == k ) {
		b->down[1] = 0;
	} else {
		return;		// key up without coresponding down (menu pass through)
	}
	if ( b->down[0] || b->down[1] ) {
		return;		// some other key is still holding it down
	}

	b->active = qfalse;

	// save timestamp for partial frame summing
	c = Cmd_Argv(2);
	uptime = atoi(c);
	if ( uptime ) {
		b->msec += uptime - b->downtime;
	} else {
		b->msec += frame_msec / 2;
	}

	b->active = qfalse;
}



/*
===============
CL_KeyState

Returns the fraction of the frame that the key was down
===============
*/
float CL_KeyState( kbutton_t *key ) {
	float		val;
	int			msec;

	msec = key->msec;
	key->msec = 0;

	if ( key->active ) {
		// still down
		if ( !key->downtime ) {
			msec = com_frameTime;
		} else {
			msec += com_frameTime - key->downtime;
		}
		key->downtime = com_frameTime;
	}

#if 0
	if (msec) {
		Com_Printf ("%i ", msec);
	}
#endif

	val = (float)msec / frame_msec;
	if ( val < 0 ) {
		val = 0;
	}
	if ( val > 1 ) {
		val = 1;
	}

	return val;
}



void IN_UpDown(void) {IN_KeyDown(&in_up);}
void IN_UpUp(void) {IN_KeyUp(&in_up);}
void IN_DownDown(void) {IN_KeyDown(&in_down);}
void IN_DownUp(void) {IN_KeyUp(&in_down);}
void IN_LeftDown(void) {IN_KeyDown(&in_left);}
void IN_LeftUp(void) {IN_KeyUp(&in_left);}
void IN_RightDown(void) {IN_KeyDown(&in_right);}
void IN_RightUp(void) {IN_KeyUp(&in_right);}
void IN_ForwardDown(void) {IN_KeyDown(&in_forward);}
void IN_ForwardUp(void) {IN_KeyUp(&in_forward);}
void IN_BackDown(void) {IN_KeyDown(&in_back);}
void IN_BackUp(void) {IN_KeyUp(&in_back);}
void IN_LookupDown(void) {IN_KeyDown(&in_lookup);}
void IN_LookupUp(void) {IN_KeyUp(&in_lookup);}
void IN_LookdownDown(void) {IN_KeyDown(&in_lookdown);}
void IN_LookdownUp(void) {IN_KeyUp(&in_lookdown);}
void IN_MoveleftDown(void) {IN_KeyDown(&in_moveleft);}
void IN_MoveleftUp(void) {IN_KeyUp(&in_moveleft);}
void IN_MoverightDown(void) {IN_KeyDown(&in_moveright);}
void IN_MoverightUp(void) {IN_KeyUp(&in_moveright);}

void IN_SpeedDown(void) {IN_KeyDown(&in_speed);}
void IN_SpeedUp(void) {IN_KeyUp(&in_speed);}
void IN_StrafeDown(void) {IN_KeyDown(&in_strafe);}
void IN_StrafeUp(void) {IN_KeyUp(&in_strafe);}

#ifdef USE_VOIP
void IN_VoipRecordDown(void)
{
	IN_KeyDown(&in_voiprecord);
	Cvar_Set("cl_voipSend", "1");
}

void IN_VoipRecordUp(void)
{
	IN_KeyUp(&in_voiprecord);
	Cvar_Set("cl_voipSend", "0");
}
#endif

void IN_Button0Down(void) {IN_KeyDown(&in_buttons[0]);}
void IN_Button0Up(void) {IN_KeyUp(&in_buttons[0]);}
void IN_Button1Down(void) {IN_KeyDown(&in_buttons[1]);}
void IN_Button1Up(void) {IN_KeyUp(&in_buttons[1]);}
void IN_Button2Down(void) {IN_KeyDown(&in_buttons[2]);}
void IN_Button2Up(void) {IN_KeyUp(&in_buttons[2]);}
void IN_Button3Down(void) {IN_KeyDown(&in_buttons[3]);}
void IN_Button3Up(void) {IN_KeyUp(&in_buttons[3]);}
void IN_Button4Down(void) {IN_KeyDown(&in_buttons[4]);}
void IN_Button4Up(void) {IN_KeyUp(&in_buttons[4]);}
void IN_Button5Down(void) {IN_KeyDown(&in_buttons[5]);}
void IN_Button5Up(void) {IN_KeyUp(&in_buttons[5]);}
void IN_Button6Down(void) {IN_KeyDown(&in_buttons[6]);}
void IN_Button6Up(void) {IN_KeyUp(&in_buttons[6]);}
void IN_Button7Down(void) {IN_KeyDown(&in_buttons[7]);}
void IN_Button7Up(void) {IN_KeyUp(&in_buttons[7]);}
void IN_Button8Down(void) {IN_KeyDown(&in_buttons[8]);}
void IN_Button8Up(void) {IN_KeyUp(&in_buttons[8]);}
void IN_Button9Down(void) {IN_KeyDown(&in_buttons[9]);}
void IN_Button9Up(void) {IN_KeyUp(&in_buttons[9]);}
void IN_Button10Down(void) {IN_KeyDown(&in_buttons[10]);}
void IN_Button10Up(void) {IN_KeyUp(&in_buttons[10]);}
void IN_Button11Down(void) {IN_KeyDown(&in_buttons[11]);}
void IN_Button11Up(void) {IN_KeyUp(&in_buttons[11]);}
void IN_Button12Down(void) {IN_KeyDown(&in_buttons[12]);}
void IN_Button12Up(void) {IN_KeyUp(&in_buttons[12]);}
void IN_Button13Down(void) {IN_KeyDown(&in_buttons[13]);}
void IN_Button13Up(void) {IN_KeyUp(&in_buttons[13]);}
void IN_Button14Down(void) {IN_KeyDown(&in_buttons[14]);}
void IN_Button14Up(void) {IN_KeyUp(&in_buttons[14]);}
// MW2's toggleads and leaveads (5a5ac0, 5a5ae0), on cl.usingAds
static void IN_ToggleADS(void) {cl.usingAds = !cl.usingAds;}
static void IN_LeaveADS(void) {cl.usingAds = qfalse;}
void IN_Button15Down(void) {IN_KeyDown(&in_buttons[15]);}
void IN_Button15Up(void) {IN_KeyUp(&in_buttons[15]);}
void IN_Button16Down(void) {IN_KeyDown(&in_buttons[16]);}
void IN_Button16Up(void) {IN_KeyUp(&in_buttons[16]);}
void IN_Button17Down(void) {IN_KeyDown(&in_buttons[17]);}
void IN_Button17Up(void) {IN_KeyUp(&in_buttons[17]);}
void IN_Button18Down(void) {IN_KeyDown(&in_buttons[18]);}
void IN_Button18Up(void) {IN_KeyUp(&in_buttons[18]);}
void IN_Button19Down(void) {IN_KeyDown(&in_buttons[19]);}
void IN_Button19Up(void) {IN_KeyUp(&in_buttons[19]);}
void IN_Button21Down(void) {IN_KeyDown(&in_buttons[21]);}
void IN_Button21Up(void) {IN_KeyUp(&in_buttons[21]);}
// cgame reports the swap press as well as the desired slot. Equipment can
// consume that intent without having to manufacture a different selection.
static void IN_WeaponSwap(void) { in_buttons[20].wasPressed = qtrue; }

void IN_CenterView (void) {
	cl.viewangles[PITCH] = -SHORT2ANGLE(cl.snap.ps.delta_angles[PITCH]);
}


//==========================================================================

cvar_t	*cl_yawspeed;
cvar_t	*cl_pitchspeed;

cvar_t	*cl_run;

cvar_t	*cl_anglespeedkey;

// MW2's game pad look (CL_JoystickAim) and move (CL_JoystickWalk)
static cvar_t	*j_mw2Aim;
static cvar_t	*j_mw2Move;
static cvar_t	*input_viewSensitivity;
static cvar_t	*aim_turnrate_yaw;
static cvar_t	*aim_turnrate_pitch;
static cvar_t	*aim_turnrate_yaw_ads;
static cvar_t	*aim_turnrate_pitch_ads;
static cvar_t	*aim_accel_turnrate_enabled;
static cvar_t	*aim_accel_turnrate_lerp;
static cvar_t	*aim_scale_view_axis;
static cvar_t	*aim_input_graph_enabled;
static cvar_t	*aim_input_graph;
static cvar_t	*aim_input_graph_scale;


/*
================
CL_AdjustAngles

Moves the local angle positions
================
*/
void CL_AdjustAngles( void ) {
	float	speed;
	
	if ( in_speed.active ) {
		speed = 0.001 * cls.frametime * cl_anglespeedkey->value;
	} else {
		speed = 0.001 * cls.frametime;
	}

	if ( !in_strafe.active ) {
		cl.viewangles[YAW] -= speed*cl_yawspeed->value*CL_KeyState (&in_right);
		cl.viewangles[YAW] += speed*cl_yawspeed->value*CL_KeyState (&in_left);
	}

	cl.viewangles[PITCH] -= speed*cl_pitchspeed->value * CL_KeyState (&in_lookup);
	cl.viewangles[PITCH] += speed*cl_pitchspeed->value * CL_KeyState (&in_lookdown);
}

/*
================
CL_KeyMove

Sets the usercmd_t based on key states
================
*/
void CL_KeyMove( usercmd_t *cmd ) {
	int		movespeed;
	int		forward, side, up;

	//
	// adjust for speed key / running
	// the walking flag is to keep animations consistent
	// even during acceleration and develeration
	//
	if ( in_speed.active ^ cl_run->integer ) {
		movespeed = 127;
		cmd->buttons &= ~BUTTON_WALKING;
	} else {
		cmd->buttons |= BUTTON_WALKING;
		movespeed = 64;
	}

	forward = 0;
	side = 0;
	up = 0;
	if ( in_strafe.active ) {
		side += movespeed * CL_KeyState (&in_right);
		side -= movespeed * CL_KeyState (&in_left);
	}

	side += movespeed * CL_KeyState (&in_moveright);
	side -= movespeed * CL_KeyState (&in_moveleft);


	up += movespeed * CL_KeyState (&in_up);
	up -= movespeed * CL_KeyState (&in_down);

	forward += movespeed * CL_KeyState (&in_forward);
	forward -= movespeed * CL_KeyState (&in_back);

	cmd->forwardmove = ClampChar( forward );
	cmd->rightmove = ClampChar( side );
	cmd->upmove = ClampChar( up );
}

/*
=================
CL_MouseEvent
=================
*/
void CL_MouseEvent( int dx, int dy, int time ) {
	if ( dx || dy ) {
		CL_NoteInputDevice( qfalse );
	}
	if ( Key_GetCatcher( ) & KEYCATCH_UI ) {
		VM_Call( uivm, UI_MOUSE_EVENT, dx, dy );
	} else if (Key_GetCatcher( ) & KEYCATCH_CGAME) {
		VM_Call (cgvm, CG_MOUSE_EVENT, dx, dy);
	} else {
		cl.mouseDx[cl.mouseIndex] += dx;
		cl.mouseDy[cl.mouseIndex] += dy;
	}
}

/*
=================
CL_JoystickEvent

Joystick values stay set until changed
=================
*/
void CL_JoystickEvent( int axis, int value, int time ) {
	if ( axis < 0 || axis >= MAX_JOYSTICK_AXIS ) {
		Com_Error( ERR_DROP, "CL_JoystickEvent: bad axis %i", axis );
	}
	if ( value ) {
		CL_NoteInputDevice( qtrue );	// a stick past its dead zone
	}
	cl.joystickAxis[axis] = value;
}

/*
=================
CL_JoystickAim

MW2's game pad look (CL_GamepadMove into AimAssist_ApplyTurnRates, see
cl_gamepadaim.c): the look stick turns the view at the aim_turnrate_* rates,
leaned toward the ADS rates by cgame's aim clock, times
input_viewSensitivity. The signs of j_yaw and j_pitch still give each axis
its direction, so a negative j_pitch inverts the look; their size is unused.
=================
*/
static void CL_JoystickAim( void ) {
	static aimState_t	state;
	static aimGraph_t	graph;
	static int			graphCount = -1, scaleCount = -1;
	aimTuning_t	tuning;
	float	yawAxis, pitchAxis, yawDelta, pitchDelta;

	if ( aim_input_graph->modificationCount != graphCount ||
		aim_input_graph_scale->modificationCount != scaleCount ) {
		graphCount = aim_input_graph->modificationCount;
		scaleCount = aim_input_graph_scale->modificationCount;
		if ( !CL_ParseAimGraph( aim_input_graph->string, aim_input_graph_scale->value, &graph ) &&
			aim_input_graph->string[0] ) {
			Com_Printf( S_COLOR_YELLOW "aim_input_graph needs 2 to 32 \"fraction value\" knots, "
				"fractions rising within 0..1 and ending at 1; turning without a graph\n" );
		}
	}

	// MW2 turns the view from the pad only while nothing catches keys
	// (CG_ShouldUpdateViewAngles), and a turn held through starts over.
	if ( Key_GetCatcher() ) {
		state.yawSpeed = state.pitchSpeed = 0.0f;
		return;
	}

	yawAxis = cl.joystickAxis[j_yaw_axis->integer] / 32767.0f;
	pitchAxis = cl.joystickAxis[j_pitch_axis->integer] / 32767.0f;
	yawAxis = j_yaw->value > 0.0f ? yawAxis : j_yaw->value < 0.0f ? -yawAxis : 0.0f;
	pitchAxis = j_pitch->value > 0.0f ? pitchAxis : j_pitch->value < 0.0f ? -pitchAxis : 0.0f;

	tuning.yaw = aim_turnrate_yaw->value;
	tuning.pitch = aim_turnrate_pitch->value;
	tuning.yawAds = aim_turnrate_yaw_ads->value;
	tuning.pitchAds = aim_turnrate_pitch_ads->value;
	tuning.sensitivity = input_viewSensitivity->value;
	tuning.accelEnabled = aim_accel_turnrate_enabled->integer != 0;
	tuning.accel = aim_accel_turnrate_lerp->value;
	tuning.scaleViewAxis = aim_scale_view_axis->integer != 0;
	tuning.graph = aim_input_graph_enabled->integer && graph.knotCount ? &graph : NULL;

	CL_AimTurn( &tuning, &state, yawAxis, pitchAxis, cl.cgameAimFraction,
		0.001f * cls.frametime, &yawDelta, &pitchDelta );

	cl.viewangles[YAW] += yawDelta;
	cl.viewangles[PITCH] += pitchDelta;
}

/*
=================
CL_JoystickWalk

MW2's game pad move (CL_GamepadMove, see CL_StickMove): the speed follows
the square of the push, and a full diagonal reaches the corner of the move
square. Like MW2 it moves while a menu is up; only the look waits. The signs
of j_forward and j_side still give each axis its direction; their size is
unused.
=================
*/
static void CL_JoystickWalk( usercmd_t *cmd ) {
	float	forward = cl.joystickAxis[j_forward_axis->integer] / 32767.0f;
	float	side = cl.joystickAxis[j_side_axis->integer] / 32767.0f;
	int		forwardMove, rightMove;

	forward = j_forward->value > 0.0f ? forward : j_forward->value < 0.0f ? -forward : 0.0f;
	side = j_side->value > 0.0f ? side : j_side->value < 0.0f ? -side : 0.0f;

	CL_StickMove( forward, side, &forwardMove, &rightMove );

	cmd->forwardmove = ClampChar( cmd->forwardmove + forwardMove );
	cmd->rightmove = ClampChar( cmd->rightmove + rightMove );
}

/*
=================
CL_JoystickMove
=================
*/
void CL_JoystickMove( usercmd_t *cmd ) {
	float	anglespeed;

	float yaw     = j_yaw->value     * cl.joystickAxis[j_yaw_axis->integer];
	float right   = j_side->value    * cl.joystickAxis[j_side_axis->integer];
	float forward = j_forward->value * cl.joystickAxis[j_forward_axis->integer];
	float pitch   = j_pitch->value   * cl.joystickAxis[j_pitch_axis->integer];
	float up      = j_up->value      * cl.joystickAxis[j_up_axis->integer];

	if ( !(in_speed.active ^ cl_run->integer) ) {
		cmd->buttons |= BUTTON_WALKING;
	}

	if ( in_speed.active ) {
		anglespeed = 0.001 * cls.frametime * cl_anglespeedkey->value;
	} else {
		anglespeed = 0.001 * cls.frametime;
	}

	// MW2 has no strafe or mlook modifier on the pad: each stick keeps its
	// own job. It turns (CL_JoystickAim) and walks (CL_JoystickWalk) as MW2's
	// CL_GamepadMove does; either one alone leaves the other Quake's.
	if ( j_mw2Aim->integer || j_mw2Move->integer ) {
		if ( j_mw2Aim->integer ) {
			CL_JoystickAim();
		} else {
			cl.viewangles[YAW] += anglespeed * yaw;
			cl.viewangles[PITCH] += anglespeed * pitch;
		}
		if ( j_mw2Move->integer ) {
			CL_JoystickWalk( cmd );
		} else {
			cmd->rightmove = ClampChar( cmd->rightmove + (int)right );
			cmd->forwardmove = ClampChar( cmd->forwardmove + (int)forward );
		}
		cmd->upmove = ClampChar( cmd->upmove + (int)up );
		return;
	}

	if ( !in_strafe.active ) {
		cl.viewangles[YAW] += anglespeed * yaw;
		cmd->rightmove = ClampChar( cmd->rightmove + (int)right );
	} else {
		cl.viewangles[YAW] += anglespeed * right;
		cmd->rightmove = ClampChar( cmd->rightmove + (int)yaw );
	}

	if ( in_mlooking ) {
		cl.viewangles[PITCH] += anglespeed * forward;
		cmd->forwardmove = ClampChar( cmd->forwardmove + (int)pitch );
	} else {
		cl.viewangles[PITCH] += anglespeed * pitch;
		cmd->forwardmove = ClampChar( cmd->forwardmove + (int)forward );
	}

	cmd->upmove = ClampChar( cmd->upmove + (int)up );
}

/*
=================
CL_MouseMove
=================
*/

void CL_MouseMove(usercmd_t *cmd)
{
	float mx, my;

	// allow mouse smoothing
	if (m_filter->integer)
	{
		mx = (cl.mouseDx[0] + cl.mouseDx[1]) * 0.5f;
		my = (cl.mouseDy[0] + cl.mouseDy[1]) * 0.5f;
	}
	else
	{
		mx = cl.mouseDx[cl.mouseIndex];
		my = cl.mouseDy[cl.mouseIndex];
	}
	
	cl.mouseIndex ^= 1;
	cl.mouseDx[cl.mouseIndex] = 0;
	cl.mouseDy[cl.mouseIndex] = 0;

	if (mx == 0.0f && my == 0.0f)
		return;
	
	if (cl_mouseAccel->value != 0.0f)
	{
		if(cl_mouseAccelStyle->integer == 0)
		{
			float accelSensitivity;
			float rate;
			
			rate = sqrt(mx * mx + my * my) / (float) frame_msec;

			accelSensitivity = cl_sensitivity->value + rate * cl_mouseAccel->value;
			mx *= accelSensitivity;
			my *= accelSensitivity;
			
			if(cl_showMouseRate->integer)
				Com_Printf("rate: %f, accelSensitivity: %f\n", rate, accelSensitivity);
		}
		else
		{
			float rate[2];
			float power[2];

			// sensitivity remains pretty much unchanged at low speeds
			// cl_mouseAccel is a power value to how the acceleration is shaped
			// cl_mouseAccelOffset is the rate for which the acceleration will have doubled the non accelerated amplification
			// NOTE: decouple the config cvars for independent acceleration setup along X and Y?

			rate[0] = fabs(mx) / (float) frame_msec;
			rate[1] = fabs(my) / (float) frame_msec;
			power[0] = powf(rate[0] / cl_mouseAccelOffset->value, cl_mouseAccel->value);
			power[1] = powf(rate[1] / cl_mouseAccelOffset->value, cl_mouseAccel->value);

			mx = cl_sensitivity->value * (mx + ((mx < 0) ? -power[0] : power[0]) * cl_mouseAccelOffset->value);
			my = cl_sensitivity->value * (my + ((my < 0) ? -power[1] : power[1]) * cl_mouseAccelOffset->value);

			if(cl_showMouseRate->integer)
				Com_Printf("ratex: %f, ratey: %f, powx: %f, powy: %f\n", rate[0], rate[1], power[0], power[1]);
		}
	}
	else
	{
		mx *= cl_sensitivity->value;
		my *= cl_sensitivity->value;
	}

	// ingame FOV
	mx *= cl.cgameSensitivity;
	my *= cl.cgameSensitivity;

	// add mouse X/Y movement to cmd
	if(in_strafe.active)
		cmd->rightmove = ClampChar(cmd->rightmove + m_side->value * mx);
	else
		cl.viewangles[YAW] -= m_yaw->value * mx;

	if ((in_mlooking || cl_freelook->integer) && !in_strafe.active)
		cl.viewangles[PITCH] += m_pitch->value * my;
	else
		cmd->forwardmove = ClampChar(cmd->forwardmove - m_forward->value * my);
}


/*
==============
CL_CmdButtons
==============
*/
void CL_CmdButtons( usercmd_t *cmd ) {
	int		i;

	//
	// figure button bits
	// send a button bit even if the key was pressed and released in
	// less than a frame
	//	
	for (i = 0 ; i < 32 ; i++) {
		// MW2's CL_KeyMove sends no sprint bit while +back is held, and keeps
		// a sprint tap made then for when it is let go. So backpedalling with
		// sprint held aims (PM_CheckADS refuses aim only to a held sprint bit).
		if ( ( 1 << i ) == BUTTON_SPRINT && in_back.active ) {
			continue;
		}
		if ( in_buttons[i].active || in_buttons[i].wasPressed ) {
			cmd->buttons |= 1 << i;
		}
		in_buttons[i].wasPressed = qfalse;
	}
	if ( cl.usingAds ) {
		cmd->buttons |= BUTTON_ADS;
	}

	if ( Key_GetCatcher( ) ) {
		cmd->buttons |= BUTTON_TALK;
	}

	// allow the game to know if any key at all is
	// currently pressed, even if it isn't bound to anything
	if ( anykeydown && Key_GetCatcher( ) == 0 ) {
		cmd->buttons |= BUTTON_ANY;
	}
}


/*
==============
CL_FinishMove
==============
*/
void CL_FinishMove( usercmd_t *cmd ) {
	int		i;

	// copy the state that the cgame is currently sending
	cmd->weapon = cl.cgameUserCmdValue;

	// send the current server time so the amount of movement
	// can be determined without allowing cheating
	cmd->serverTime = cl.serverTime;

	// MW2's CL_FinishMove (5a6740-5a677b): the aim is the player's view
	// angles plus cgame's view kick, all three axes. The kick is never added
	// into cl.viewangles, so it runs out without moving the player's own aim.
	for (i=0 ; i<3 ; i++) {
		cmd->angles[i] = ANGLE2SHORT(cl.viewangles[i] + cl.cgameKickAngles[i]);
	}
}


/*
=================
CL_DebugButtons

cl_debugButtons 1 prints, for each X (+usereload) and RT (+attack) press, the
command it went out in and how far apart the two were. It only reports; it
changes nothing.

Why it matters: the server asks for X's reload after it has run the command
X went out in, and the gun takes it on the next command (MW2's
Player_UpdateActivate). So X reloads before a shot only when X went out at
least one command before RT. In the same command the server fires first. One
command is one client frame: 16ms at com_maxfps 60, 11ms at MW2's 85.
=================
*/
static cvar_t *cl_debugButtons;

static void CL_DebugButtons( const usercmd_t *cmd ) {
	static int lastButtons;
	static int xCmd = -1, rtCmd = -1;
	static unsigned xTime, rtTime;
	int risen = cmd->buttons & ~lastButtons;

	lastButtons = cmd->buttons;
	if ( !cl_debugButtons || !cl_debugButtons->integer ) {
		return;
	}
	// cl.cmdNumber starts again at each connection
	if ( xCmd > cl.cmdNumber ) xCmd = -1;
	if ( rtCmd > cl.cmdNumber ) rtCmd = -1;

	if ( risen & BUTTON_USE_RELOAD ) {
		xCmd = cl.cmdNumber;
		xTime = in_buttons[21].downtime;
		if ( !( risen & BUTTON_ATTACK ) && rtCmd >= 0 && xTime - rtTime <= 100 ) {
			Com_Printf( "^3buttons:^7 cmd %i (frame %ims): X down, %i command(s) / %ims AFTER RT\n",
				cl.cmdNumber, (int)frame_msec, cl.cmdNumber - rtCmd, (int)( xTime - rtTime ) );
		} else if ( !( risen & BUTTON_ATTACK ) ) {
			Com_Printf( "^3buttons:^7 cmd %i (frame %ims): X down\n", cl.cmdNumber, (int)frame_msec );
		}
	}

	if ( risen & BUTTON_ATTACK ) {
		rtCmd = cl.cmdNumber;
		rtTime = in_buttons[0].downtime;
		if ( risen & BUTTON_USE_RELOAD ) {
			Com_Printf( "^3buttons:^7 cmd %i (frame %ims): X and RT in the SAME command "
				"(input %ims apart): the server fires before it sees X\n",
				cl.cmdNumber, (int)frame_msec, (int)( rtTime - xTime ) );
		} else if ( xCmd >= 0 && ( ( cmd->buttons & BUTTON_USE_RELOAD ) || rtTime - xTime <= 250 ) ) {
			Com_Printf( "^3buttons:^7 cmd %i (frame %ims): RT down, X went %i command(s) / %ims "
				"earlier%s: the server sees X first\n",
				cl.cmdNumber, (int)frame_msec, cl.cmdNumber - xCmd, (int)( rtTime - xTime ),
				( cmd->buttons & BUTTON_USE_RELOAD ) ? " and is held" : " (released)" );
		} else {
			Com_Printf( "^3buttons:^7 cmd %i (frame %ims): RT down, no X\n", cl.cmdNumber, (int)frame_msec );
		}
	}
}

/*
=================
CL_CreateCmd
=================
*/
usercmd_t CL_CreateCmd( void ) {
	usercmd_t	cmd;
	vec3_t		oldAngles;

	VectorCopy( cl.viewangles, oldAngles );

	// keyboard angle adjustment
	CL_AdjustAngles ();
	
	Com_Memset( &cmd, 0, sizeof( cmd ) );

	CL_CmdButtons( &cmd );

	// get basic movement from keyboard
	CL_KeyMove( &cmd );

	// get basic movement from mouse
	CL_MouseMove( &cmd );

	// get basic movement from joystick
	CL_JoystickMove( &cmd );

	// check to make sure the angles haven't wrapped
	if ( cl.viewangles[PITCH] - oldAngles[PITCH] > 90 ) {
		cl.viewangles[PITCH] = oldAngles[PITCH] + 90;
	} else if ( oldAngles[PITCH] - cl.viewangles[PITCH] > 90 ) {
		cl.viewangles[PITCH] = oldAngles[PITCH] - 90;
	} 

	// store out the final values
	CL_FinishMove( &cmd );

	CL_DebugButtons( &cmd );

	// draw debug graphs of turning for mouse testing
	if ( cl_debugMove->integer ) {
		if ( cl_debugMove->integer == 1 ) {
			SCR_DebugGraph( fabs(cl.viewangles[YAW] - oldAngles[YAW]) );
		}
		if ( cl_debugMove->integer == 2 ) {
			SCR_DebugGraph( fabs(cl.viewangles[PITCH] - oldAngles[PITCH]) );
		}
	}

	return cmd;
}


/*
=================
CL_CreateNewCommands

Create a new usercmd_t structure for this frame
=================
*/
void CL_CreateNewCommands( void ) {
	int			cmdNum;

	// no need to create usercmds until we have a gamestate
	if ( clc.state < CA_PRIMED ) {
		return;
	}

	frame_msec = com_frameTime - old_com_frameTime;

	// if running over 1000fps, act as if each frame is 1ms
	// prevents divisions by zero
	if ( frame_msec < 1 ) {
		frame_msec = 1;
	}

	// if running less than 5fps, truncate the extra time to prevent
	// unexpected moves after a hitch
	if ( frame_msec > 200 ) {
		frame_msec = 200;
	}
	old_com_frameTime = com_frameTime;


	// generate a command for this frame
	cl.cmdNumber++;
	cmdNum = cl.cmdNumber & CMD_MASK;
	cl.cmds[cmdNum] = CL_CreateCmd ();
}

/*
=================
CL_ReadyToSendPacket

Returns qfalse if we are over the maxpackets limit
and should choke back the bandwidth a bit by not sending
a packet this frame.  All the commands will still get
delivered in the next packet, but saving a header and
getting more delta compression will reduce total bandwidth.
=================
*/
qboolean CL_ReadyToSendPacket( void ) {
	int		oldPacketNum;
	int		delta;

	// don't send anything if playing back a demo
	if ( clc.demoplaying || clc.state == CA_CINEMATIC ) {
		return qfalse;
	}

	// If we are downloading, we send no less than 50ms between packets
	if ( *clc.downloadTempName &&
		cls.realtime - clc.lastPacketSentTime < 50 ) {
		return qfalse;
	}

	// if we don't have a valid gamestate yet, only send
	// one packet a second
	if ( clc.state != CA_ACTIVE && 
		clc.state != CA_PRIMED && 
		!*clc.downloadTempName &&
		cls.realtime - clc.lastPacketSentTime < 1000 ) {
		return qfalse;
	}

	// send every frame for loopbacks
	if ( clc.netchan.remoteAddress.type == NA_LOOPBACK ) {
		return qtrue;
	}

	// send every frame for LAN
	if ( cl_lanForcePackets->integer && Sys_IsLANAddress( clc.netchan.remoteAddress ) ) {
		return qtrue;
	}

	// check for exceeding cl_maxpackets
	if ( cl_maxpackets->integer < 15 ) {
		Cvar_Set( "cl_maxpackets", "15" );
	} else if ( cl_maxpackets->integer > 125 ) {
		Cvar_Set( "cl_maxpackets", "125" );
	}
	oldPacketNum = (clc.netchan.outgoingSequence - 1) & PACKET_MASK;
	delta = cls.realtime -  cl.outPackets[ oldPacketNum ].p_realtime;
	if ( delta < 1000 / cl_maxpackets->integer ) {
		// the accumulated commands will go out in the next packet
		return qfalse;
	}

	return qtrue;
}

/*
===================
CL_WritePacket

Create and send the command packet to the server
Including both the reliable commands and the usercmds

During normal gameplay, a client packet will contain something like:

4	sequence number
2	qport
4	serverid
4	acknowledged sequence number
4	clc.serverCommandSequence
<optional reliable commands>
1	clc_move or clc_moveNoDelta
1	command count
<count * usercmds>

===================
*/
void CL_WritePacket( void ) {
	msg_t		buf;
	byte		data[MAX_MSGLEN];
	int			i, j;
	usercmd_t	*cmd, *oldcmd;
	usercmd_t	nullcmd;
	int			packetNum;
	int			oldPacketNum;
	int			count, key;

	// don't send anything if playing back a demo
	if ( clc.demoplaying || clc.state == CA_CINEMATIC ) {
		return;
	}

	Com_Memset( &nullcmd, 0, sizeof(nullcmd) );
	oldcmd = &nullcmd;

	MSG_Init( &buf, data, sizeof(data) );

	MSG_Bitstream( &buf );
	// write the current serverId so the server
	// can tell if this is from the current gameState
	MSG_WriteLong( &buf, cl.serverId );

	// write the last message we received, which can
	// be used for delta compression, and is also used
	// to tell if we dropped a gamestate
	MSG_WriteLong( &buf, clc.serverMessageSequence );

	// write the last reliable message we received
	MSG_WriteLong( &buf, clc.serverCommandSequence );

	// write any unacknowledged clientCommands
	for ( i = clc.reliableAcknowledge + 1 ; i <= clc.reliableSequence ; i++ ) {
		MSG_WriteByte( &buf, clc_clientCommand );
		MSG_WriteLong( &buf, i );
		MSG_WriteString( &buf, clc.reliableCommands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
	}

	// we want to send all the usercmds that were generated in the last
	// few packet, so even if a couple packets are dropped in a row,
	// all the cmds will make it to the server
	if ( cl_packetdup->integer < 0 ) {
		Cvar_Set( "cl_packetdup", "0" );
	} else if ( cl_packetdup->integer > 5 ) {
		Cvar_Set( "cl_packetdup", "5" );
	}
	oldPacketNum = (clc.netchan.outgoingSequence - 1 - cl_packetdup->integer) & PACKET_MASK;
	count = cl.cmdNumber - cl.outPackets[ oldPacketNum ].p_cmdNumber;
	if ( count > MAX_PACKET_USERCMDS ) {
		count = MAX_PACKET_USERCMDS;
		Com_Printf("MAX_PACKET_USERCMDS\n");
	}

#ifdef USE_VOIP
	if (clc.voipOutgoingDataSize > 0)
	{
		if((clc.voipFlags & VOIP_SPATIAL) || Com_IsVoipTarget(clc.voipTargets, sizeof(clc.voipTargets), -1))
		{
			MSG_WriteByte (&buf, clc_voipOpus);
			MSG_WriteByte (&buf, clc.voipOutgoingGeneration);
			MSG_WriteLong (&buf, clc.voipOutgoingSequence);
			MSG_WriteByte (&buf, clc.voipOutgoingDataFrames);
			MSG_WriteData (&buf, clc.voipTargets, sizeof(clc.voipTargets));
			MSG_WriteByte(&buf, clc.voipFlags);
			MSG_WriteShort (&buf, clc.voipOutgoingDataSize);
			MSG_WriteData (&buf, clc.voipOutgoingData, clc.voipOutgoingDataSize);

			// If we're recording a demo, we have to fake a server packet with
			//  this VoIP data so it gets to disk; the server doesn't send it
			//  back to us, and we might as well eliminate concerns about dropped
			//  and misordered packets here.
			if(clc.demorecording && !clc.demowaiting)
			{
				const int voipSize = clc.voipOutgoingDataSize;
				msg_t fakemsg;
				byte fakedata[MAX_MSGLEN];
				MSG_Init (&fakemsg, fakedata, sizeof (fakedata));
				MSG_Bitstream (&fakemsg);
				MSG_WriteLong (&fakemsg, clc.reliableAcknowledge);
				MSG_WriteByte (&fakemsg, svc_voipOpus);
				MSG_WriteShort (&fakemsg, clc.clientNum);
				MSG_WriteByte (&fakemsg, clc.voipOutgoingGeneration);
				MSG_WriteLong (&fakemsg, clc.voipOutgoingSequence);
				MSG_WriteByte (&fakemsg, clc.voipOutgoingDataFrames);
				MSG_WriteShort (&fakemsg, clc.voipOutgoingDataSize );
				MSG_WriteBits (&fakemsg, clc.voipFlags, VOIP_FLAGCNT);
				MSG_WriteData (&fakemsg, clc.voipOutgoingData, voipSize);
				MSG_WriteByte (&fakemsg, svc_EOF);
				CL_WriteDemoMessage (&fakemsg, 0);
			}

			clc.voipOutgoingSequence += clc.voipOutgoingDataFrames;
			clc.voipOutgoingDataSize = 0;
			clc.voipOutgoingDataFrames = 0;
		}
		else
		{
			// We have data, but no targets. Silently discard all data
			clc.voipOutgoingDataSize = 0;
			clc.voipOutgoingDataFrames = 0;
		}
	}
#endif

	if ( count >= 1 ) {
		if ( cl_showSend->integer ) {
			Com_Printf( "(%i)", count );
		}

		// begin a client move command
		if ( cl_nodelta->integer || !cl.snap.valid || clc.demowaiting
			|| clc.serverMessageSequence != cl.snap.messageNum ) {
			MSG_WriteByte (&buf, clc_moveNoDelta);
		} else {
			MSG_WriteByte (&buf, clc_move);
		}

		// write the command count
		MSG_WriteByte( &buf, count );

		// use the checksum feed in the key
		key = clc.checksumFeed;
		// also use the message acknowledge
		key ^= clc.serverMessageSequence;
		// also use the last acknowledged server command in the key
		key ^= MSG_HashKey(clc.serverCommands[ clc.serverCommandSequence & (MAX_RELIABLE_COMMANDS-1) ], 32);

		// write all the commands, including the predicted command
		for ( i = 0 ; i < count ; i++ ) {
			j = (cl.cmdNumber - count + i + 1) & CMD_MASK;
			cmd = &cl.cmds[j];
			MSG_WriteDeltaUsercmdKey (&buf, key, oldcmd, cmd);
			oldcmd = cmd;
		}
	}

	//
	// deliver the message
	//
	packetNum = clc.netchan.outgoingSequence & PACKET_MASK;
	cl.outPackets[ packetNum ].p_realtime = cls.realtime;
	cl.outPackets[ packetNum ].p_serverTime = oldcmd->serverTime;
	cl.outPackets[ packetNum ].p_cmdNumber = cl.cmdNumber;
	clc.lastPacketSentTime = cls.realtime;

	if ( cl_showSend->integer ) {
		Com_Printf( "%i ", buf.cursize );
	}

	CL_Netchan_Transmit (&clc.netchan, &buf);	
}

/*
=================
CL_SendCmd

Called every frame to builds and sends a command packet to the server.
=================
*/
void CL_SendCmd( void ) {
	// don't send any message if not connected
	if ( clc.state < CA_CONNECTED ) {
		return;
	}

	// don't send commands if paused
	if ( com_sv_running->integer && sv_paused->integer && cl_paused->integer ) {
		return;
	}

	// we create commands even if a demo is playing,
	CL_CreateNewCommands();

	// don't send a packet if the last packet was sent too recently
	if ( !CL_ReadyToSendPacket() ) {
		if ( cl_showSend->integer ) {
			Com_Printf( ". " );
		}
		return;
	}

	CL_WritePacket();
}

/*
============
CL_InitInput
============
*/
void CL_InitInput( void ) {
	Cmd_AddCommand ("centerview",IN_CenterView);

	Cmd_AddCommand ("+moveup",IN_UpDown);
	Cmd_AddCommand ("-moveup",IN_UpUp);
	Cmd_AddCommand ("+movedown",IN_DownDown);
	Cmd_AddCommand ("-movedown",IN_DownUp);
	Cmd_AddCommand ("+left",IN_LeftDown);
	Cmd_AddCommand ("-left",IN_LeftUp);
	Cmd_AddCommand ("+right",IN_RightDown);
	Cmd_AddCommand ("-right",IN_RightUp);
	Cmd_AddCommand ("+forward",IN_ForwardDown);
	Cmd_AddCommand ("-forward",IN_ForwardUp);
	Cmd_AddCommand ("+back",IN_BackDown);
	Cmd_AddCommand ("-back",IN_BackUp);
	Cmd_AddCommand ("+lookup", IN_LookupDown);
	Cmd_AddCommand ("-lookup", IN_LookupUp);
	Cmd_AddCommand ("+lookdown", IN_LookdownDown);
	Cmd_AddCommand ("-lookdown", IN_LookdownUp);
	Cmd_AddCommand ("+strafe", IN_StrafeDown);
	Cmd_AddCommand ("-strafe", IN_StrafeUp);
	Cmd_AddCommand ("+moveleft", IN_MoveleftDown);
	Cmd_AddCommand ("-moveleft", IN_MoveleftUp);
	Cmd_AddCommand ("+moveright", IN_MoverightDown);
	Cmd_AddCommand ("-moveright", IN_MoverightUp);
	Cmd_AddCommand ("+speed", IN_SpeedDown);
	Cmd_AddCommand ("-speed", IN_SpeedUp);
	Cmd_AddCommand ("+attack", IN_Button0Down);
	Cmd_AddCommand ("-attack", IN_Button0Up);
	Cmd_AddCommand ("+reload", IN_Button12Down);	// BUTTON_RELOAD = bit 12
	Cmd_AddCommand ("-reload", IN_Button12Up);
	Cmd_AddCommand ("+sprint", IN_Button13Down);	// BUTTON_SPRINT = bit 13
	Cmd_AddCommand ("-sprint", IN_Button13Up);
	Cmd_AddCommand ("+ads", IN_Button14Down);		// BUTTON_ADS = bit 14
	Cmd_AddCommand ("-ads", IN_Button14Up);
	Cmd_AddCommand ("toggleads", IN_ToggleADS);
	Cmd_AddCommand ("leaveads", IN_LeaveADS);
	Cmd_AddCommand ("+melee", IN_Button15Down);		// BUTTON_MELEE = bit 15
	Cmd_AddCommand ("-melee", IN_Button15Up);
	Cmd_AddCommand ("+lethal", IN_Button16Down);	// BUTTON_LETHAL = bit 16
	Cmd_AddCommand ("-lethal", IN_Button16Up);
	Cmd_AddCommand ("+tactical", IN_Button17Down);	// BUTTON_TACTICAL = bit 17
	Cmd_AddCommand ("-tactical", IN_Button17Up);
	Cmd_AddCommand ("+use", IN_Button18Down);		// BUTTON_USE = bit 18
	Cmd_AddCommand ("-use", IN_Button18Up);
	Cmd_AddCommand ("+usereload", IN_Button21Down);	// BUTTON_USE_RELOAD = bit 21 (MW2's use/reload)
	Cmd_AddCommand ("-usereload", IN_Button21Up);
	Cmd_AddCommand ("+altfire", IN_Button19Down);	// BUTTON_ALTFIRE = bit 19
	Cmd_AddCommand ("-altfire", IN_Button19Up);
	Cmd_AddCommand ("_peaceWeaponSwap", IN_WeaponSwap);
	Cmd_AddCommand ("+button0", IN_Button0Down);
	Cmd_AddCommand ("-button0", IN_Button0Up);
	Cmd_AddCommand ("+button1", IN_Button1Down);
	Cmd_AddCommand ("-button1", IN_Button1Up);
	Cmd_AddCommand ("+button2", IN_Button2Down);
	Cmd_AddCommand ("-button2", IN_Button2Up);
	Cmd_AddCommand ("+button3", IN_Button3Down);
	Cmd_AddCommand ("-button3", IN_Button3Up);
	Cmd_AddCommand ("+button4", IN_Button4Down);
	Cmd_AddCommand ("-button4", IN_Button4Up);
	Cmd_AddCommand ("+button5", IN_Button5Down);
	Cmd_AddCommand ("-button5", IN_Button5Up);
	Cmd_AddCommand ("+button6", IN_Button6Down);
	Cmd_AddCommand ("-button6", IN_Button6Up);
	Cmd_AddCommand ("+button7", IN_Button7Down);
	Cmd_AddCommand ("-button7", IN_Button7Up);
	Cmd_AddCommand ("+button8", IN_Button8Down);
	Cmd_AddCommand ("-button8", IN_Button8Up);
	Cmd_AddCommand ("+button9", IN_Button9Down);
	Cmd_AddCommand ("-button9", IN_Button9Up);
	Cmd_AddCommand ("+button10", IN_Button10Down);
	Cmd_AddCommand ("-button10", IN_Button10Up);
	Cmd_AddCommand ("+button11", IN_Button11Down);
	Cmd_AddCommand ("-button11", IN_Button11Up);
	Cmd_AddCommand ("+button12", IN_Button12Down);
	Cmd_AddCommand ("-button12", IN_Button12Up);
	Cmd_AddCommand ("+button13", IN_Button13Down);
	Cmd_AddCommand ("-button13", IN_Button13Up);
	Cmd_AddCommand ("+button14", IN_Button14Down);
	Cmd_AddCommand ("-button14", IN_Button14Up);
	Cmd_AddCommand ("+mlook", IN_MLookDown);
	Cmd_AddCommand ("-mlook", IN_MLookUp);

#ifdef USE_VOIP
	Cmd_AddCommand ("+voiprecord", IN_VoipRecordDown);
	Cmd_AddCommand ("-voiprecord", IN_VoipRecordUp);
#endif

	cl_nodelta = Cvar_Get ("cl_nodelta", "0", 0);
	cl_debugMove = Cvar_Get ("cl_debugMove", "0", 0);
	cl_debugButtons = Cvar_Get ("cl_debugButtons", "0", 0);

	// MW2's game pad look and move (j_mw2Aim, j_mw2Move; 0 is Quake's
	// joystick for that stick). The aim_* defaults and ranges are MW2's own dvars
	// as IW4x registers them; input_viewSensitivity is the console menu's
	// multiplier: 1 at "Medium" (2), 4 at "Insane" (10). aim_input_graph holds
	// MW2's graph knots once read from the game ("fraction value" pairs, with
	// GraphFloat's scale in aim_input_graph_scale); empty turns without one.
	j_mw2Aim = Cvar_Get( "j_mw2Aim", "1", CVAR_ARCHIVE );
	j_mw2Move = Cvar_Get( "j_mw2Move", "1", CVAR_ARCHIVE );
	input_viewSensitivity = Cvar_Get( "input_viewSensitivity", "1", CVAR_ARCHIVE );
	aim_turnrate_yaw = Cvar_Get( "aim_turnrate_yaw", "260", CVAR_ARCHIVE );
	aim_turnrate_pitch = Cvar_Get( "aim_turnrate_pitch", "90", CVAR_ARCHIVE );
	aim_turnrate_yaw_ads = Cvar_Get( "aim_turnrate_yaw_ads", "90", CVAR_ARCHIVE );
	aim_turnrate_pitch_ads = Cvar_Get( "aim_turnrate_pitch_ads", "55", CVAR_ARCHIVE );
	aim_accel_turnrate_enabled = Cvar_Get( "aim_accel_turnrate_enabled", "1", CVAR_ARCHIVE );
	aim_accel_turnrate_lerp = Cvar_Get( "aim_accel_turnrate_lerp", "1200", CVAR_ARCHIVE );
	aim_scale_view_axis = Cvar_Get( "aim_scale_view_axis", "1", CVAR_ARCHIVE );
	aim_input_graph_enabled = Cvar_Get( "aim_input_graph_enabled", "1", CVAR_ARCHIVE );
	aim_input_graph = Cvar_Get( "aim_input_graph", "", CVAR_ARCHIVE );
	aim_input_graph_scale = Cvar_Get( "aim_input_graph_scale", "1", CVAR_ARCHIVE );
	Cvar_CheckRange( input_viewSensitivity, 0.0001f, 5.0f, qfalse );
	Cvar_CheckRange( aim_turnrate_yaw, 0.0f, 1080.0f, qfalse );
	Cvar_CheckRange( aim_turnrate_pitch, 0.0f, 1080.0f, qfalse );
	Cvar_CheckRange( aim_turnrate_yaw_ads, 0.0f, 1080.0f, qfalse );
	Cvar_CheckRange( aim_turnrate_pitch_ads, 0.0f, 1080.0f, qfalse );
	Cvar_CheckRange( aim_accel_turnrate_lerp, 0.0f, 4000.0f, qfalse );
}

/*
============
CL_ShutdownInput
============
*/
void CL_ShutdownInput(void)
{
	Cmd_RemoveCommand("centerview");

	Cmd_RemoveCommand("+moveup");
	Cmd_RemoveCommand("-moveup");
	Cmd_RemoveCommand("+movedown");
	Cmd_RemoveCommand("-movedown");
	Cmd_RemoveCommand("+left");
	Cmd_RemoveCommand("-left");
	Cmd_RemoveCommand("+right");
	Cmd_RemoveCommand("-right");
	Cmd_RemoveCommand("+forward");
	Cmd_RemoveCommand("-forward");
	Cmd_RemoveCommand("+back");
	Cmd_RemoveCommand("-back");
	Cmd_RemoveCommand("+lookup");
	Cmd_RemoveCommand("-lookup");
	Cmd_RemoveCommand("+lookdown");
	Cmd_RemoveCommand("-lookdown");
	Cmd_RemoveCommand("+strafe");
	Cmd_RemoveCommand("-strafe");
	Cmd_RemoveCommand("+moveleft");
	Cmd_RemoveCommand("-moveleft");
	Cmd_RemoveCommand("+moveright");
	Cmd_RemoveCommand("-moveright");
	Cmd_RemoveCommand("+speed");
	Cmd_RemoveCommand("-speed");
	Cmd_RemoveCommand("+attack");
	Cmd_RemoveCommand("-attack");
	Cmd_RemoveCommand("+reload");
	Cmd_RemoveCommand("-reload");
	Cmd_RemoveCommand("+sprint");
	Cmd_RemoveCommand("-sprint");
	Cmd_RemoveCommand("+ads");
	Cmd_RemoveCommand("-ads");
	Cmd_RemoveCommand("toggleads");
	Cmd_RemoveCommand("leaveads");
	Cmd_RemoveCommand("+melee");
	Cmd_RemoveCommand("-melee");
	Cmd_RemoveCommand("+lethal");
	Cmd_RemoveCommand("-lethal");
	Cmd_RemoveCommand("+tactical");
	Cmd_RemoveCommand("-tactical");
	Cmd_RemoveCommand("+use");
	Cmd_RemoveCommand("-use");
	Cmd_RemoveCommand("+usereload");
	Cmd_RemoveCommand("-usereload");
	Cmd_RemoveCommand("+altfire");
	Cmd_RemoveCommand("-altfire");
	Cmd_RemoveCommand("_peaceWeaponSwap");
	Cmd_RemoveCommand("+button0");
	Cmd_RemoveCommand("-button0");
	Cmd_RemoveCommand("+button1");
	Cmd_RemoveCommand("-button1");
	Cmd_RemoveCommand("+button2");
	Cmd_RemoveCommand("-button2");
	Cmd_RemoveCommand("+button3");
	Cmd_RemoveCommand("-button3");
	Cmd_RemoveCommand("+button4");
	Cmd_RemoveCommand("-button4");
	Cmd_RemoveCommand("+button5");
	Cmd_RemoveCommand("-button5");
	Cmd_RemoveCommand("+button6");
	Cmd_RemoveCommand("-button6");
	Cmd_RemoveCommand("+button7");
	Cmd_RemoveCommand("-button7");
	Cmd_RemoveCommand("+button8");
	Cmd_RemoveCommand("-button8");
	Cmd_RemoveCommand("+button9");
	Cmd_RemoveCommand("-button9");
	Cmd_RemoveCommand("+button10");
	Cmd_RemoveCommand("-button10");
	Cmd_RemoveCommand("+button11");
	Cmd_RemoveCommand("-button11");
	Cmd_RemoveCommand("+button12");
	Cmd_RemoveCommand("-button12");
	Cmd_RemoveCommand("+button13");
	Cmd_RemoveCommand("-button13");
	Cmd_RemoveCommand("+button14");
	Cmd_RemoveCommand("-button14");
	Cmd_RemoveCommand("+mlook");
	Cmd_RemoveCommand("-mlook");

#ifdef USE_VOIP
	Cmd_RemoveCommand("+voiprecord");
	Cmd_RemoveCommand("-voiprecord");
#endif
}
