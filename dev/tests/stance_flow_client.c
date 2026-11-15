/* The client half of the stance integration test (stance_flow_test.c): the
 * real cl_input.c (MW2's stance commands and command builder), cmd.c's buffer,
 * cvar.c and cl_keys.c's key path, as stance_input_test.c links them. cgame's
 * console commands arrive here through the real Cbuf_AddText and run at the
 * next frame's Cbuf_Execute. Its own TU: client.h and cg_local.h cannot share
 * one. */
#include "../../code/client/client.h"

void CL_InitInput( void );
void CL_CreateNewCommands( void );

clientActive_t cl;
clientConnection_t clc;
clientStatic_t cls;
int com_frameTime;
static cvar_t zero;
cvar_t *cl_freelook = &zero, *cl_sensitivity = &zero, *cl_mouseAccel = &zero, *cl_mouseAccelStyle = &zero,
	*cl_mouseAccelOffset = &zero, *cl_showMouseRate = &zero, *m_pitch = &zero, *m_yaw = &zero,
	*m_forward = &zero, *m_side = &zero, *m_filter = &zero, *j_pitch = &zero, *j_yaw = &zero,
	*j_forward = &zero, *j_side = &zero, *j_up = &zero, *j_pitch_axis = &zero, *j_yaw_axis = &zero,
	*j_forward_axis = &zero, *j_side_axis = &zero, *j_up_axis = &zero, *cl_debugMove, *cl_nodelta,
	*com_cl_running, *com_sv_running, *con_autochat = &zero;
vm_t *cgvm, *uivm;
int g_console_field_width = 78;
qboolean com_errorEntered;
static int unknownCommands;

#ifdef ZONE_DEBUG
void *Z_MallocDebug( int size, char *label, char *file, int line ) { (void)label; (void)file; (void)line; return calloc( 1, size ); }
void *S_MallocDebug( int size, char *label, char *file, int line ) { (void)label; (void)file; (void)line; return calloc( 1, size ); }
#else
void *Z_Malloc( int size ) { return calloc( 1, size ); }
void *S_Malloc( int size ) { return calloc( 1, size ); }
#endif
void Z_Free( void *p ) { free( p ); }
char *CopyString( const char *in ) { char *out = malloc( strlen( in ) + 1 ); strcpy( out, in ); return out; }
void SCR_DebugGraph( float value ) { (void)value; }
qboolean CL_GameCommand( void ) { return qfalse; }
qboolean SV_GameCommand( void ) { return qfalse; }
qboolean UI_GameCommand( void ) { return qfalse; }
void CL_ForwardCommandToServer( const char *string ) { (void)string; unknownCommands++; }
intptr_t QDECL VM_Call( vm_t *vm, int callNum, ... ) { (void)vm; (void)callNum; return 0; }
void Con_ToggleConsole_f( void ) { Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_CONSOLE ); }
void Con_Bottom( void ) { } void Con_Top( void ) { } void Con_PageDown( void ) { } void Con_PageUp( void ) { }
void CL_Disconnect_f( void ) { abort(); }
void CL_AddReliableCommand( const char *cmd, qboolean isDisconnectCmd ) { (void)cmd; (void)isDisconnectCmd; abort(); }
void S_StopAllSounds( void ) { }
void SCR_UpdateScreen( void ) { }
void Field_Clear( field_t *edit ) { memset( edit, 0, sizeof( *edit ) ); }
void Field_AutoComplete( field_t *edit ) { (void)edit; }
char *Sys_GetClipboardData( void ) { return NULL; }
fileHandle_t FS_FOpenFileWrite_HomeState( const char *path ) { (void)path; return 0; }
int FS_Write( const void *buffer, int len, fileHandle_t f ) { (void)buffer; (void)f; return len; }
void FS_FCloseFile( fileHandle_t f ) { (void)f; }

/* ---- what the test drives ---- */
void FlowClientInit( void ) {
	extern cvar_t *cl_run, *cl_yawspeed, *cl_pitchspeed, *cl_anglespeedkey;
	static qboolean once;
	if ( !once ) {
		Cbuf_Init();
		cl_run = Cvar_Get( "cl_run", "1", 0 );
		cl_yawspeed = cl_pitchspeed = cl_anglespeedkey = &zero;
		CL_InitInput();
		once = qtrue;
	}
	memset( &cl, 0, sizeof( cl ) );	/* CL_ClearState's memset */
	Cbuf_Execute();	/* nothing queued from a last run survives */
	clc.state = CA_ACTIVE;
	com_frameTime += 1000;
	CL_CreateNewCommands();
	Key_SetBinding( K_PAD0_RIGHTSTICK_CLICK, "+stance" );	/* the shipped pad's RS */
	Key_SetBinding( K_PAD0_LEFTSTICK_CLICK, "+sprint" );	/* and LS */
	Key_SetBinding( K_PAD0_LEFTSTICK_UP, "+forward" );
}
/* a frame: its time, then the caller's events, then its binds and its command */
void FlowClientBegin( int msec ) { com_frameTime += msec; }
void FlowKey( int key, qboolean down ) { CL_KeyEvent( key, down, com_frameTime ); }
void FlowClientCommand( const char *text ) { Cbuf_AddText( text ); }	/* the console: cgame's trap_SendConsoleCommand */
void FlowClientEnd( usercmd_t *cmd, int serverTime ) {
	Cbuf_Execute();
	cl.serverTime = serverTime;
	CL_CreateNewCommands();
	*cmd = cl.cmds[cl.cmdNumber & CMD_MASK];
}
void FlowSetSnapType( int pmType ) { cl.snap.ps.pm_type = pmType; }
void FlowTurn( float yaw ) { cl.viewangles[YAW] += yaw; }
int FlowStance( void ) { return cl.stance; }
qboolean FlowStanceHeld( void ) { return cl.stanceHeld; }
int FlowUnknownCommands( void ) { return unknownCommands; }
/* cgame's command reads, as CL_GetUserCmd answers them */
int FlowCurrentCmdNumber( void ) { return cl.cmdNumber; }
qboolean FlowGetUserCmd( int cmdNumber, usercmd_t *ucmd ) {
	if ( cmdNumber > cl.cmdNumber || cmdNumber <= cl.cmdNumber - CMD_BACKUP ) {
		return qfalse;
	}
	*ucmd = cl.cmds[cmdNumber & CMD_MASK];
	return qtrue;
}
