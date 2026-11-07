/* The cgame side of the view kick's QVM boundary test (kick_qvm_test.c).
 * q3lcc compiles it with the real cg_kick.c, q_math.c and bg_lib.c, and q3asm
 * links them with the real cg_syscalls.asm, so cg_kick.c's trap call is the
 * one a shipped cgame.qvm makes. The engine side is the real vm.c,
 * vm_interpreted.c and CL_CgameSystemCalls.
 *
 * Only the weapon tables are stubbed: the kick's numbers are kick_test.c's.
 * This file checks that the angles cgame holds are the angles the engine
 * receives. vmMain must stay the first function: a QVM starts at code 0. */
/* q3cpp looks for a quoted include under the source file's directory and
 * under each directory an earlier include named, joined as written. Naming
 * q_shared.h first gives cg_local.h's "../qcommon/" and "../game/" a base. */
#include "../../code/qcommon/q_shared.h"
#include "../../code/cgame/cg_local.h"

enum { KQ_INIT, KQ_FRAME };
#define KQ_FIRE			1
#define KQ_FOLLOW		2
#define KQ_KILLCAM		4

cg_t cg;
static snapshot_t kqSnap;
static int kqKillcam;
static void KQ_Frame( int time, int flags );

Q_EXPORT intptr_t vmMain( int command, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8, int arg9, int arg10, int arg11 ) {
	switch ( command ) {
	case KQ_INIT:	// where cgame keeps the angles, for the engine to read back
		cg.snap = &kqSnap;
		kqSnap.ps.weapon = WP_M40A3;
		cg.time = cg.kickTime = arg0;
		return (intptr_t)cg.kickAngles;
	case KQ_FRAME:
		KQ_Frame( arg0, arg1 );
		return 0;
	}
	return -1;
}

static void KQ_Frame( int time, int flags ) {
	cg.time = time;
	kqKillcam = ( flags & KQ_KILLCAM ) != 0;
	kqSnap.ps.pm_flags = ( flags & KQ_FOLLOW ) ? PMF_FOLLOW : 0;
	if ( flags & KQ_FIRE ) {
		CG_KickFire();
	}
	CG_KickAngles();
}

/* cg_kick.c's other calls: the weapon tables (kick_test.c checks those) and
 * the killcam's gate. */
int BG_ViewmodelWeapon( const playerState_t *ps ) {
	return ps->weapon;
}
float BG_WeaponViewKickCenterSpeed( int weapon, float adsFrac ) {
	return weapon == WP_M40A3 ? 500.0f : BG_KICK_NO_DEF_CENTER_SPEED;
}
qboolean BG_WeaponFireRecoil( const playerState_t *ps, float adsFrac, vec3_t kickAVel, unsigned int *holdrand ) {
	*holdrand = *holdrand * 214013 + 2531011;	// one draw, so each shot differs
	kickAVel[PITCH] = -90.0f - ( *holdrand >> 28 );
	kickAVel[YAW] = -52.5f + ( ( *holdrand >> 24 ) & 15 );
	kickAVel[ROLL] = -0.5f * kickAVel[YAW];
	return qtrue;
}
qboolean CG_Killcam_Active( void ) {
	return kqKillcam;
}

/* q_shared.c's, which bg_lib.c needs: through the engine, as cg_main.c's */
void QDECL Com_Error( int level, const char *error, ... ) {
	va_list argptr;
	char text[1024];
	va_start( argptr, error );
	Q_vsnprintf( text, sizeof( text ), error, argptr );
	va_end( argptr );
	trap_Error( text );
}
void QDECL Com_Printf( const char *msg, ... ) {
	va_list argptr;
	char text[1024];
	va_start( argptr, msg );
	Q_vsnprintf( text, sizeof( text ), msg, argptr );
	va_end( argptr );
	trap_Print( text );
}
