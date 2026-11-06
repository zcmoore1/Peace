/* The engine side of the view kick test: the real CL_FinishMove (cl_input.c)
 * and CL_SetUserCmdKickAngles (cl_cgame.c), and a native syscall entry made as
 * the engine's VM_DllSyscall makes it (every argument copied into args[]),
 * which answers cgame's kick call exactly as CL_CgameSystemCalls does. Its own
 * TU: client.h and cg_local.h cannot share one. */
#include "../../code/client/client.h"
#include <math.h>
#include <stdlib.h>

#define MAX_VMSYSCALL_ARGS 16	/* vm_local.h's, as VM_DllSyscall copies them */
void CL_FinishMove( usercmd_t *cmd );	/* cl_input.c's, which client.h does not declare */

clientActive_t cl;
int kickTestCalls, kickTestSnaps;

intptr_t QDECL KickTestSyscall( intptr_t arg, ... ) {
	intptr_t args[MAX_VMSYSCALL_ARGS];
	va_list ap;
	int i;
	args[0] = arg;
	va_start( ap, arg );
	for ( i = 1; i < ARRAY_LEN( args ); i++ ) args[i] = va_arg( ap, intptr_t );
	va_end( ap );
	switch ( args[0] ) {
	case CG_SETUSERCMDKICKANGLES:	/* CL_CgameSystemCalls' own case */
		kickTestCalls++;
		CL_SetUserCmdKickAngles( VMF(1), VMF(2), VMF(3) );
		return 0;
	case CG_SNAPVECTOR: {	/* the engine's Q_SnapVector (its SSE form is not linked): round to nearest */
		float *v = (float *)args[1];
		kickTestSnaps++;
		v[0] = rintf( v[0] ); v[1] = rintf( v[1] ); v[2] = rintf( v[2] );
		return 0;
	}
	case CG_ERROR:
		fprintf( stderr, "CG_Error: %s\n", (const char *)args[1] );
		exit( 1 );
	default:	/* sounds and the rest draw nothing here */
		return 0;
	}
}

/* One command as CL_CreateCmd finishes it: the selection and time, then the
 * real CL_FinishMove for the angles. */
void KickTestFinishMove( usercmd_t *cmd, int serverTime, const vec3_t view, int weapon ) {
	memset( cmd, 0, sizeof( *cmd ) );
	VectorCopy( view, cl.viewangles );
	cl.serverTime = serverTime;
	cl.cgameUserCmdValue = weapon;
	CL_FinishMove( cmd );
}
void KickTestClientKick( vec3_t out ) { VectorCopy( cl.cgameKickAngles, out ); }
void KickTestClientView( vec3_t out ) { VectorCopy( cl.viewangles, out ); }
void KickTestClientReset( void ) { memset( &cl, 0, sizeof( cl ) ); kickTestCalls = kickTestSnaps = 0; }
