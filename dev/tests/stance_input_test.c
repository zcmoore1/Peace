/* MW2's stance button through the real client (dev/STANCE.md): the real
 * command handlers and command builder (cl_input.c: CL_InitInput's commands,
 * CL_CreateNewCommands, CL_CmdButtons, CL_KeyMove), the real command buffer
 * and tokenizer (cmd.c), the real cvars (cvar.c, so cl_stanceHoldTime's range
 * is the real check) and the real key path (cl_keys.c: Key_SetBinding,
 * CL_KeyEvent, CL_ParseBinding, Key_SetCatcher and Key_ClearStates). Only the
 * rest of the engine is stubbed.
 *
 * A frame is MW2's and ioquake3's: com_frameTime moves on, that frame's key
 * events queue their binds, Cbuf_Execute runs them, and one command is built.
 * Every time below is read from cl_stanceHoldTime and the frame length; none
 * is written as a literal.
 *
 * Checked (MW2's handlers, 0x5A5690-0x5A5FD3): the tap/hold matrix from every
 * stance at 8, 16 and 33 ms frames; the hold at exactly cl_stanceHoldTime and
 * one ms short, 0, and the range; a release in the hold's own frame; double
 * taps, two keys on +stance, a press after a hold fired; the held stance keys
 * (+prone, +movedown) and what they block, freeze and lose; every toggle and
 * step from every stance and the connection gates; +gostand and +moveup
 * (stand, then jump; flying for a spectator; the jump under a held stance
 * key, which DOWN never cancels); _peaceSetStance (cgame's
 * CL_SetStance); a hold across death; the real key path; and a console opened
 * mid-hold. */
#include "../../code/client/client.h"

void CL_InitInput( void );
void CL_CreateNewCommands( void );
extern kbutton_t in_up, in_down, in_jump, in_buttons[32];

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
	*com_cl_running, *com_sv_running;

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

/* ---- the rest of the engine ---- */
void QDECL Com_Printf( const char *fmt, ... ) { (void)fmt; }
void QDECL Com_DPrintf( const char *fmt, ... ) { (void)fmt; }
void QDECL Com_Error( int code, const char *fmt, ... ) {
	va_list a; (void)code; va_start( a, fmt ); vfprintf( stderr, fmt, a ); va_end( a ); exit( 1 );
}
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
/* no command here may fall through to the game or the server */
static int unknownCommands;
qboolean CL_GameCommand( void ) { return qfalse; }
qboolean SV_GameCommand( void ) { return qfalse; }
qboolean UI_GameCommand( void ) { return qfalse; }
void CL_ForwardCommandToServer( const char *string ) { (void)string; unknownCommands++; }
/* cl_keys.c's neighbours: the menus and console are not open here, except
 * the console, which the console key toggles as Con_ToggleConsole_f does */
vm_t *cgvm, *uivm;
int g_console_field_width = 78;
cvar_t *con_autochat = &zero;
qboolean com_errorEntered;
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

/* ---- the frame: the clock moves, the frame's events queue their binds,
 * Cbuf_Execute runs them, one command is built (Com_Frame's order) ---- */
#define STAND	BG_STANCE_STAND
#define CROUCH	BG_STANCE_CROUCH
#define PRONE	BG_STANCE_PRONE
static int frameMsec;
static usercmd_t lastCmd;
static void Reset( int msec ) {
	memset( &cl, 0, sizeof( cl ) );	/* CL_ClearState's memset */
	memset( in_buttons, 0, sizeof( kbutton_t ) * 32 );
	memset( &in_up, 0, sizeof( in_up ) ); memset( &in_down, 0, sizeof( in_down ) ); memset( &in_jump, 0, sizeof( in_jump ) );
	Key_SetCatcher( 0 );
	clc.state = CA_ACTIVE;
	frameMsec = msec;
	Cvar_Reset( "cl_stanceHoldTime" );	/* its default, MW2's */
	Cbuf_Execute();
	com_frameTime += 1000;	/* a fresh time, well clear of the last test */
	CL_CreateNewCommands();
}
/* the next frame starts: its time; then the caller's events for it */
static void Begin( void ) { com_frameTime += frameMsec; }
/* the frame's binds run, and its command is built */
static int End( void ) {
	Cbuf_Execute();
	CL_CreateNewCommands();
	lastCmd = cl.cmds[cl.cmdNumber & CMD_MASK];
	return lastCmd.buttons & BUTTON_STANCE_MASK;
}
static int Frame( void ) { Begin(); return End(); }
static void Run( const char *text ) { Cbuf_AddText( text ); Cbuf_AddText( "\n" ); }
static int Level( int stance ) {	/* the bits a command carries for the client's stance */
	return stance == CROUCH ? BUTTON_CROUCH : stance == PRONE ? BUTTON_PRONE : 0;
}
/* a pad button bound as the shipped config binds RS, pressed and released
 * through the engine's key event (the time argument is the event's) */
#define RS K_PAD0_RIGHTSTICK_CLICK
static void Press( int key ) { CL_KeyEvent( key, qtrue, com_frameTime ); }
static void Release( int key ) { CL_KeyEvent( key, qfalse, com_frameTime ); }
static void From( int stance ) {	/* put the client in a stance, as a toggle would */
	Run( stance == CROUCH ? "gocrouch" : stance == PRONE ? "goprone" : "_peaceSetStance 0" );
	CHECK( Frame() == Level( stance ) && cl.stance == stance );
}
static int HoldTime( void ) { return Cvar_VariableIntegerValue( "cl_stanceHoldTime" ); }

/* 1, 3: MW2's matrix (CL_KeyMove 0x5A5F4B, +stance/-stance 0x5A5A30/0x5A5A80).
 * The press crouches (from prone too); a hold still down on the first command
 * at least cl_stanceHoldTime later goes prone, or stands if armed from prone;
 * a tap from a crouch stands, any other tap leaves the crouch. */
static void TestMatrix( void ) {
	static const int frames[] = { 8, 16, 33 };
	int f, from, hold, k, fired;
	for ( f = 0; f < ARRAY_LEN( frames ); f++ ) for ( from = STAND; from <= PRONE; from++ ) for ( hold = 0; hold < 2; hold++ ) {
		int releaseFrame = ( 100 + frames[f] - 1 ) / frames[f];	/* the first frame at or after 100 ms */
		int fireFrame = ( HoldTime() + frames[f] - 1 ) / frames[f];	/* the first command at least the hold time on */
		int tapEnd = from == CROUCH ? STAND : CROUCH, holdEnd = from == PRONE ? STAND : PRONE;
		Reset( frames[f] ); Key_SetBinding( RS, "+stance" );
		From( from );
		Begin(); Press( RS ); CHECK( End() == BUTTON_CROUCH );	/* frame 0: crouched at once, from every stance */
		fired = -1;
		for ( k = 1; k * frames[f] <= 2 * HoldTime(); k++ ) {
			int bits;
			Begin();
			if ( !hold && k == releaseFrame ) Release( RS );
			bits = End();
			if ( hold ) {
				CHECK( bits == ( k < fireFrame ? BUTTON_CROUCH : Level( holdEnd ) ) );
				if ( fired < 0 && bits != BUTTON_CROUCH ) fired = k * frames[f];
			} else {
				CHECK( bits == ( k < releaseFrame ? BUTTON_CROUCH : Level( tapEnd ) ) );
			}
		}
		if ( hold ) {
			/* MW2's quantised hold: 304 at 8 and 16 ms frames, 330 at 33 */
			CHECK( fired == fireFrame * frames[f] && fired >= HoldTime() && fired - frames[f] < HoldTime() );
			if ( from != STAND ) printf( "hold from %s at %2i ms frames: %s on the command at %i ms\n",
				from == CROUCH ? "crouch" : "prone ", frames[f], from == PRONE ? "stand" : "prone", fired );
			/* the release after it changes nothing */
			Begin(); Release( RS ); CHECK( End() == Level( holdEnd ) );
		}
		CHECK( !cl.stanceHeld && !unknownCommands );
	}
	/* 3: let go in the very frame the hold would fire: the binds run before
	 * the command, so it is a tap */
	for ( from = STAND; from <= PRONE; from++ ) {
		Reset( 12 ); Key_SetBinding( RS, "+stance" ); From( from );
		Begin(); Press( RS ); End();
		for ( k = 1; k * frameMsec < HoldTime(); k++ ) CHECK( Frame() == BUTTON_CROUCH );
		CHECK( k * frameMsec == HoldTime() );
		Begin(); Release( RS ); CHECK( End() == Level( from == CROUCH ? STAND : CROUCH ) );
	}
}

/* 2: exactly cl_stanceHoldTime fires (MW2's jl skips), one ms short waits a
 * frame; 0 fires on the press's own command, so its crouch never shows; the
 * range is MW2's 0..1000. */
static void TestBoundary( void ) {
	static const int frames[] = { 10, 12 };
	char value[16];
	int f, k, extra;
	for ( f = 0; f < ARRAY_LEN( frames ); f++ ) for ( extra = 0; extra < 2; extra++ ) {
		int hold;
		Reset( frames[f] );
		hold = HoldTime() + extra;
		Com_sprintf( value, sizeof( value ), "%i", hold );
		Cvar_Set( "cl_stanceHoldTime", value );
		CHECK( HoldTime() == hold && HoldTime() % frames[f] == extra );	/* 300 is a whole number of both */
		Run( "+stance" ); CHECK( Frame() == BUTTON_CROUCH );
		for ( k = 1; k * frames[f] < HoldTime(); k++ ) CHECK( Frame() == BUTTON_CROUCH );
		CHECK( Frame() == BUTTON_PRONE );
		CHECK( ( k * frames[f] == HoldTime() ) == !extra );	/* at the time exactly, or the frame after */
	}
	Reset( 8 ); Cvar_Set( "cl_stanceHoldTime", "0" );
	Run( "+stance" ); CHECK( Frame() == BUTTON_PRONE );
	Run( "-stance" ); CHECK( Frame() == BUTTON_PRONE );
	Cvar_Set( "cl_stanceHoldTime", "2000" ); CHECK( HoldTime() == 1000 );
	Cvar_Set( "cl_stanceHoldTime", "-5" ); CHECK( HoldTime() == 0 );
	Cvar_Reset( "cl_stanceHoldTime" ); CHECK( HoldTime() == atoi( Cvar_VariableString( "cl_stanceHoldTime" ) ) );
	CHECK( !( Cvar_Flags( "cl_stanceHoldTime" ) & CVAR_ARCHIVE ) );	/* MW2's flags 0 */
}

/* 4: a double tap from standing ends standing. Two keys on +stance: the
 * second press re-arms the timer from its own frame, either release resolves
 * it, the other's release does nothing. A press after a fired hold re-arms
 * from the new stance. */
static void TestPresses( void ) {
	int k, second;
	Reset( 8 ); Key_SetBinding( RS, "+stance" );
	Begin(); Press( RS ); CHECK( End() == BUTTON_CROUCH );
	Begin(); Release( RS ); CHECK( End() == BUTTON_CROUCH );
	CHECK( Frame() == BUTTON_CROUCH );
	Begin(); Press( RS ); CHECK( End() == BUTTON_CROUCH );
	Begin(); Release( RS ); CHECK( End() == 0 );
	/* two keys */
	Reset( 8 ); Key_SetBinding( RS, "+stance" ); Key_SetBinding( 'v', "+stance" );
	Begin(); Press( RS ); End();
	for ( k = 0; k < 10; k++ ) CHECK( Frame() == BUTTON_CROUCH );
	Begin(); Press( 'v' ); CHECK( End() == BUTTON_CROUCH ); second = com_frameTime;
	CHECK( cl.stanceTime == second && cl.stancePosition == CROUCH );
	while ( com_frameTime + frameMsec - second < HoldTime() ) CHECK( Frame() == BUTTON_CROUCH );
	CHECK( Frame() == BUTTON_PRONE );	/* timed from the second press */
	Begin(); Release( RS ); CHECK( End() == BUTTON_PRONE );
	Begin(); Release( 'v' ); CHECK( End() == BUTTON_PRONE );
	/* released early by the other key: armed from a crouch, so it stands */
	Reset( 8 ); Key_SetBinding( RS, "+stance" ); Key_SetBinding( 'v', "+stance" );
	Begin(); Press( RS ); End(); Frame();
	Begin(); Press( 'v' ); End(); Frame();
	Begin(); Release( RS ); CHECK( End() == 0 );
	Begin(); Release( 'v' ); CHECK( End() == 0 );
	/* after a hold fired (prone), a new press re-arms from prone: crouch, then stand */
	Reset( 16 ); Key_SetBinding( RS, "+stance" );
	Begin(); Press( RS ); End();
	while ( Frame() != BUTTON_PRONE ) ;
	Begin(); Release( RS ); End();
	Begin(); Press( RS ); CHECK( End() == BUTTON_CROUCH ); second = com_frameTime;
	while ( com_frameTime + frameMsec - second < HoldTime() ) CHECK( Frame() == BUTTON_CROUCH );
	CHECK( Frame() == 0 );
}

/* 5: the held stance keys. +prone sends PRONE|TEMP, +movedown CROUCH|TEMP
 * (prone wins with both), and while either is down every stance command,
 * toggle and reset is ignored and a pending hold waits: it fires on the first
 * command after the key comes up, if it is due. A +prone tap inside one frame
 * is lost. */
static void TestTempKeys( void ) {
	static const char *ignored[] = { "+stance", "-stance", "togglecrouch", "toggleprone", "goprone", "gocrouch",
		"lowerstance", "raisestance", "+gostand", "+moveup", "_peaceSetStance 2", "_peaceSetStance 1" };
	int i, k, armed;
	for ( i = 0; i < ARRAY_LEN( ignored ); i++ ) {
		Reset( 8 );
		Run( "+movedown 200 0" ); CHECK( Frame() == ( BUTTON_CROUCH | BUTTON_TEMP_STANCE ) );
		Run( ignored[i] ); CHECK( Frame() == ( BUTTON_CROUCH | BUTTON_TEMP_STANCE ) );
		CHECK( cl.stance == STAND && !cl.stanceHeld );
		Run( "+prone 201 0" ); CHECK( Frame() == ( BUTTON_PRONE | BUTTON_TEMP_STANCE ) );	/* both: prone */
		Run( "-movedown 200 0" ); CHECK( Frame() == ( BUTTON_PRONE | BUTTON_TEMP_STANCE ) );
		Run( ignored[i] ); CHECK( Frame() == ( BUTTON_PRONE | BUTTON_TEMP_STANCE ) );
		CHECK( cl.stance == STAND && !cl.stanceHeld );
		Run( "-prone 201 0" ); CHECK( Frame() == 0 );
		Run( "-gostand" ); Run( "-moveup" ); Frame();
	}
	/* a pending hold freezes under a held key and fires when it comes up */
	Reset( 8 );
	Run( "+stance" ); CHECK( Frame() == BUTTON_CROUCH ); armed = com_frameTime;
	Run( "+movedown 200 0" ); CHECK( Frame() == ( BUTTON_CROUCH | BUTTON_TEMP_STANCE ) );
	Run( "-stance" );	/* ignored: the hold is still armed */
	for ( k = 0; com_frameTime - armed < 2 * HoldTime(); k++ ) CHECK( Frame() == ( BUTTON_CROUCH | BUTTON_TEMP_STANCE ) );
	CHECK( cl.stanceHeld );
	Run( "-movedown 200 0" ); CHECK( Frame() == BUTTON_PRONE && !cl.stanceHeld );
	/* not due yet when the key comes up: it fires on time after */
	Reset( 8 );
	Run( "+stance" ); Frame(); armed = com_frameTime;
	Run( "+prone 201 0" ); Frame(); Frame();
	Run( "-prone 201 0" ); CHECK( Frame() == BUTTON_CROUCH );
	while ( com_frameTime + frameMsec - armed < HoldTime() ) CHECK( Frame() == BUTTON_CROUCH );
	CHECK( Frame() == BUTTON_PRONE );
	/* a +prone tap inside a frame: CL_CmdButtons sees it, the stance level overwrites it */
	Reset( 8 );
	Run( "+prone 201 0" ); Run( "-prone 201 0" ); CHECK( Frame() == 0 );
	Run( "goprone" ); Frame();
	Run( "+movedown 200 0" ); Run( "-movedown 200 0" ); CHECK( Frame() == BUTTON_PRONE );
}

/* 6: every toggle and step from every stance, and MW2's connection gates. */
static void TestToggles( void ) {
	static const struct { const char *cmd; int to[3]; qboolean connected; } table[] = {
		{ "togglecrouch", { CROUCH, STAND, CROUCH }, qfalse },
		{ "toggleprone",  { PRONE, PRONE, STAND }, qfalse },
		{ "goprone",      { PRONE, PRONE, PRONE }, qfalse },
		{ "gocrouch",     { CROUCH, CROUCH, CROUCH }, qfalse },
		{ "lowerstance",  { CROUCH, PRONE, PRONE }, qtrue },
		{ "raisestance",  { STAND, STAND, CROUCH }, qtrue },
		{ "+gostand",     { STAND, STAND, STAND }, qtrue },
		{ "+moveup",      { STAND, STAND, CROUCH }, qtrue },
		{ "+stance",      { CROUCH, CROUCH, CROUCH }, qtrue },
	};
	int i, from;
	for ( i = 0; i < ARRAY_LEN( table ); i++ ) for ( from = STAND; from <= PRONE; from++ ) {
		Reset( 8 ); From( from );
		Run( table[i].cmd ); CHECK( Frame() == Level( table[i].to[from] ) );
		/* below CA_CONNECTED (no command is built there: the state is read) */
		Reset( 8 ); From( from ); clc.state = CA_CHALLENGING;
		Run( table[i].cmd ); Cbuf_Execute();
		CHECK( cl.stance == ( table[i].connected ? from : table[i].to[from] ) );
		Run( "-gostand 77 0" ); Run( "-moveup 77 0" ); Run( "-stance" ); Cbuf_Execute();
	}
	/* -stance below CA_CONNECTED leaves the hold armed */
	Reset( 8 ); Run( "+stance" ); Frame(); clc.state = CA_CHALLENGING;
	Run( "-stance" ); Cbuf_Execute(); CHECK( cl.stanceHeld && cl.stance == CROUCH );
	/* +gostand and -gostand press and release nothing unconnected; +moveup
	 * presses UP (and neither stands nor jumps), -moveup always releases */
	Reset( 8 ); clc.state = CA_CHALLENGING;
	Run( "+gostand 77 0" ); Cbuf_Execute(); CHECK( !in_up.active && !in_jump.active );
	Run( "+moveup 78 0" ); Cbuf_Execute(); CHECK( in_up.active && !in_jump.active );
	Run( "-moveup 78 0" ); Cbuf_Execute(); CHECK( !in_up.active && !in_jump.active );
	clc.state = CA_ACTIVE; Run( "+gostand 77 0" ); Cbuf_Execute(); CHECK( in_up.active && in_jump.active );
	clc.state = CA_CHALLENGING; Run( "-gostand 77 0" ); Cbuf_Execute(); CHECK( in_up.active && in_jump.active );
	clc.state = CA_ACTIVE; Run( "-gostand 77 0" ); Cbuf_Execute(); CHECK( !in_up.active && !in_jump.active );
}

/* 7: MW2's jump is MOVEUP alone: +gostand and +moveup press it only from a
 * standing stance, so a walker's upmove > 0 is only ever a jump. UP flies a
 * spectator or noclip. +movedown alone still sends upmove < 0, never against
 * a jump. */
static void TestJump( void ) {
	int from;
	for ( from = STAND; from <= PRONE; from++ ) {
		Reset( 8 ); From( from );
		Run( "+gostand 57 0" ); CHECK( Frame() == 0 );
		CHECK( from == STAND ? lastCmd.upmove > 0 : lastCmd.upmove == 0 );
		Run( "-gostand 57 0" ); Frame(); Frame(); CHECK( lastCmd.upmove == 0 );	/* the release frame counts its share */
		Reset( 8 ); From( from );
		Run( "+moveup 58 0" ); CHECK( Frame() == ( from == PRONE ? BUTTON_CROUCH : 0 ) );
		CHECK( from == STAND ? lastCmd.upmove > 0 : lastCmd.upmove == 0 );
		Run( "-moveup 58 0" ); Frame(); Frame(); CHECK( lastCmd.upmove == 0 );
	}
	/* held jump keys keep jumping from stand; the next stance change is a level */
	Reset( 8 ); Run( "+gostand 57 0" ); Frame(); CHECK( lastCmd.upmove > 0 && Frame() == 0 && lastCmd.upmove > 0 );
	/* a spectator: UP flies, and the toggle stands */
	Reset( 8 ); cl.snap.ps.pm_type = PM_SPECTATOR; From( CROUCH );
	Run( "+gostand 57 0" ); CHECK( Frame() == 0 && lastCmd.upmove > 0 && cl.stance == STAND );
	Run( "-gostand 57 0" ); Frame(); Frame(); CHECK( lastCmd.upmove == 0 );
	cl.snap.ps.pm_type = PM_NOCLIP; From( CROUCH );
	Run( "+gostand 57 0" ); CHECK( Frame() == 0 && lastCmd.upmove > 0 && !in_jump.active );
	Run( "togglecrouch" ); Frame(); CHECK( lastCmd.upmove > 0 );
	cl.snap.ps.pm_type = PM_NORMAL;
	/* a walker in a crouch with UP held sends nothing up */
	CHECK( Frame() == BUTTON_CROUCH && lastCmd.upmove == 0 && in_up.active );
	Run( "-gostand 57 0" ); Frame();
	Reset( 8 ); Run( "+movedown 59 0" ); CHECK( Frame() == ( BUTTON_CROUCH | BUTTON_TEMP_STANCE ) && lastCmd.upmove < 0 );
	/* the held stance keys never cancel the jump (MW2's 0x400 is its own bit,
	 * which DOWN, kb[11], never touches): +gostand from a standing stance under
	 * a held +movedown sends the jump, as under a held +prone, on every command
	 * and through the key's release; +moveup under either presses no jump */
	{
		static const char *keys[][2] = { { "+movedown 59 0", "-movedown 59 0" }, { "+prone 201 0", "-prone 201 0" } };
		int k, n;
		for ( k = 0; k < ARRAY_LEN( keys ); k++ ) {
			int temp = ( k ? BUTTON_PRONE : BUTTON_CROUCH ) | BUTTON_TEMP_STANCE;
			Reset( 8 ); Run( keys[k][0] ); CHECK( Frame() == temp && ( k ? lastCmd.upmove == 0 : lastCmd.upmove < 0 ) );
			Run( "+gostand 57 0" ); CHECK( Frame() == temp && lastCmd.upmove > 0 && in_jump.active );
			for ( n = 0; n < 5; n++ ) CHECK( Frame() == temp && lastCmd.upmove > 0 );
			Run( keys[k][1] ); CHECK( Frame() == 0 && lastCmd.upmove > 0 );
			CHECK( Frame() == 0 && lastCmd.upmove > 0 && cl.stance == STAND );
			Run( "-gostand 57 0" ); Frame(); Frame(); CHECK( lastCmd.upmove == 0 );
			Reset( 8 ); Run( keys[k][0] ); Frame();
			Run( "+moveup 58 0" ); CHECK( Frame() == temp && !in_jump.active && ( k ? lastCmd.upmove == 0 : lastCmd.upmove < 0 ) );
			Run( "-moveup 58 0" ); Run( keys[k][1] ); Frame();
		}
	}
}

/* 8, 9: cgame's reset writes the stance only: a pending hold still fires
 * (prone, or stand if it was armed from prone); a bad stance is refused. The
 * client never reads the playerstate for the hold: dead, it fires on time. */
static void TestSetStance( void ) {
	int armed, dead;
	Reset( 8 ); Run( "_peaceSetStance 2" ); CHECK( Frame() == BUTTON_PRONE );
	Run( "_peaceSetStance 1" ); CHECK( Frame() == BUTTON_CROUCH );
	Run( "_peaceSetStance 3" ); Run( "_peaceSetStance -1" ); Run( "_peaceSetStance" ); CHECK( Frame() == BUTTON_CROUCH );
	Run( "_peaceSetStance 0" ); CHECK( Frame() == 0 );
	for ( dead = 0; dead < 2; dead++ ) {
		int from;
		for ( from = STAND; from <= PRONE; from += PRONE ) {
			Reset( 8 ); From( from );
			if ( dead ) { cl.snap.ps.pm_type = PM_DEAD; cl.snap.ps.stats[STAT_HEALTH] = 0; }
			Run( "+stance" ); Frame(); armed = com_frameTime;
			Run( "_peaceSetStance 0" ); CHECK( Frame() == 0 );
			CHECK( cl.stanceHeld && cl.stanceTime == armed && cl.stancePosition == from );
			while ( com_frameTime + frameMsec - armed < HoldTime() ) Frame();
			CHECK( Frame() == ( from == PRONE ? 0 : BUTTON_PRONE ) );
		}
	}
	CHECK( !unknownCommands );
}

/* 11: the real key path. A press runs "+stance <key> <time>" once and the
 * release "-stance <key> <time>"; +stance reads neither argument (MW2's takes
 * no kbutton): its time is the frame's. A key held sends nothing more (the
 * engine passes no repeats in game, and a pad button has none). */
static void TestKeyPath( void ) {
	int k;
	Reset( 8 ); Key_SetBinding( RS, "+stance" );
	Begin(); CL_KeyEvent( RS, qtrue, com_frameTime - 5 ); CHECK( End() == BUTTON_CROUCH );
	CHECK( cl.stanceHeld && cl.stanceTime == com_frameTime );
	for ( k = 0; k < 10; k++ ) { CHECK( Frame() == BUTTON_CROUCH ); CHECK( cl.stanceTime == com_frameTime - ( k + 1 ) * frameMsec ); }
	Begin(); CL_KeyEvent( RS, qfalse, com_frameTime - 3 ); CHECK( End() == BUTTON_CROUCH && !cl.stanceHeld );
	CHECK( !unknownCommands );
}

/* 12: opening the console mid-hold clears the keys (Key_SetCatcher's
 * Key_ClearStates), which sends the key-up: -stance runs and the hold
 * resolves as a tap (Peace's, from the Quake engine; MW2's menu was not
 * traced). Armed from a crouch it stands; from standing it stays crouched.
 * The stance still goes out with the console up (no stand-up on a menu). */
static void TestConsoleMidHold( void ) {
	int from;
	for ( from = STAND; from <= CROUCH; from++ ) {
		Reset( 8 ); Key_SetBinding( RS, "+stance" ); From( from );
		Begin(); Press( RS ); End(); Frame(); Frame();
		Begin(); Press( K_CONSOLE ); CHECK( End() == Level( from == CROUCH ? STAND : CROUCH ) );
		CHECK( Key_GetCatcher() & KEYCATCH_CONSOLE );
		CHECK( !cl.stanceHeld && ( lastCmd.buttons & BUTTON_TALK ) );
		while ( com_frameTime - cl.stanceTime <= 2 * HoldTime() ) CHECK( Frame() == Level( from == CROUCH ? STAND : CROUCH ) );
		Release( K_CONSOLE ); Release( RS );
		Key_SetCatcher( 0 );
	}
}

int main( void ) {
	extern cvar_t *cl_run, *cl_yawspeed, *cl_pitchspeed, *cl_anglespeedkey;
	Cbuf_Init();
	cl_run = Cvar_Get( "cl_run", "1", 0 );	/* cl_main.c's, which CL_KeyMove and CL_AdjustAngles read */
	cl_yawspeed = cl_pitchspeed = cl_anglespeedkey = &zero;
	CL_InitInput();
	CHECK( HoldTime() == 300 );	/* MW2's default, the dvar's (0x45E9FD), pinned once */
	TestMatrix();
	TestBoundary();
	TestPresses();
	TestTempKeys();
	TestToggles();
	TestJump();
	TestSetStance();
	TestKeyPath();
	TestConsoleMidHold();
	printf( "PASS: MW2's stance input through the real client (%d checks)\n", checks );
	return 0;
}
