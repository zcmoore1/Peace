/* MW2's view kick (owner's decision: MW2's way, the kick only).
 *
 * A shot of the predicted player assigns an angular velocity
 * (BG_WeaponFireRecoil, called from MW2's CG_FireWeapon at 4fb2d7), and
 * CG_KickAngles (0x596790) runs it out every frame into kick angles. The
 * client adds those angles into the next command's aim (CL_FinishMove,
 * 5a6740-5a677b), never into its own view angles, so they come back as the
 * predicted ps.viewangles, the server aims with them and the killcam records
 * them. The view never draws the kick any other way.
 *
 * This is the one place cgame authors aim, and only the kick: it never reads
 * or writes ammo, weapon state or anything pmove owns. Pmove is unchanged. */
#include "cg_local.h"

// MW2's client holdrand (0x79cb88), as the executable starts it. The server
// draws none of it: MW2's server keeps no view kick.
unsigned int cg_kickSeed = 0x89abcdefu;

/*
=================
CG_KickPlayer

MW2 runs the kick only while the player state is the player's own
(POF_PLAYER, 59a100) and zeroes it otherwise (59a113). Peace's own state is
the one it predicts: not a demo, not the killcam and not a followed player.
=================
*/
static qboolean CG_KickPlayer( void ) {
	return cg.snap && !cg.demoPlayback && !CG_Killcam_Active() &&
		!( cg.snap->ps.pm_flags & PMF_FOLLOW );
}

/*
=================
CG_KickFire

The predicted player's own fire event (cg.predictedPlayerEntity), read
against the predicted state at that moment, as MW2's is.
=================
*/
void CG_KickFire( void ) {
	BG_WeaponFireRecoil( &cg.predictedPlayerState, cg.adsFrac, cg.kickAVel, &cg_kickSeed );
}

/*
=================
CG_KickAngles

MW2's CG_KickAngles, once a frame after the fire events: the time since the
last run in steps of at most BG_KICK_STEP_MSEC, each axis pushed back toward
0 at the centre speed of the weapon the view reads (BG_ViewmodelWeapon,
taken once for the frame), a step back toward 0 cut to BG_KICK_RETURN_SCALE,
an axis that crosses 0 stopped there, and one past BG_KICK_MAX_ANGLE held at
it with no velocity. A weapon whose centre speed is 0 never brakes the kick
it inherits. The angles go to the client every frame.
=================
*/
void CG_KickAngles( void ) {
	int msec, step, i;
	float center, dt, d, angle, next;

	msec = cg.time - cg.kickTime;
	cg.kickTime = cg.time;
	if ( !CG_KickPlayer() ) {
		VectorClear( cg.kickAVel );
		VectorClear( cg.kickAngles );
		msec = 0;
	}
	if ( msec > 0 ) {
		center = BG_WeaponViewKickCenterSpeed( BG_ViewmodelWeapon( &cg.predictedPlayerState ), cg.adsFrac );
	} else {
		center = 0.0f;
	}
	for ( ; msec > 0; msec -= BG_KICK_STEP_MSEC ) {
		// nothing to run out: the rest of the frame changes nothing either
		if ( VectorCompare( cg.kickAVel, vec3_origin ) && VectorCompare( cg.kickAngles, vec3_origin ) ) {
			break;
		}
		step = msec < BG_KICK_STEP_MSEC ? msec : BG_KICK_STEP_MSEC;
		dt = step * 0.001f;
		for ( i = 0; i < 3; i++ ) {
			angle = cg.kickAngles[i];
			if ( cg.kickAVel[i] == 0.0f && angle == 0.0f ) {
				continue;
			}
			if ( angle != 0.0f ) {
				cg.kickAVel[i] += ( angle > 0.0f ? -center : center ) * dt;
			}
			d = cg.kickAVel[i] * dt;
			if ( angle * d < 0.0f ) {
				d *= BG_KICK_RETURN_SCALE;
			}
			next = angle + d;
			if ( angle * next < 0.0f ) {
				cg.kickAngles[i] = 0.0f;
				cg.kickAVel[i] = 0.0f;
			} else if ( next == 0.0f ) {
				cg.kickAngles[i] = 0.0f;
				cg.kickAVel[i] = 0.0f;
			} else if ( fabs( next ) > BG_KICK_MAX_ANGLE ) {
				cg.kickAngles[i] = next > 0.0f ? BG_KICK_MAX_ANGLE : -BG_KICK_MAX_ANGLE;
				cg.kickAVel[i] = 0.0f;
			} else {
				cg.kickAngles[i] = next;
			}
		}
	}
	trap_SetUserCmdKickAngles( cg.kickAngles[PITCH], cg.kickAngles[YAW], cg.kickAngles[ROLL] );
}
