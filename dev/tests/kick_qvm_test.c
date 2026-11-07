/* MW2's view kick across the QVM boundary. kick_test.c drives cgame's native
 * trap, which is cg_syscalls.c's C wrapper. A cgame.qvm has no such wrapper:
 * its call goes through the equ stub in cg_syscalls.asm, and the arguments
 * are exactly the words q3lcc pushes for cg_kick.c's call. That is what a
 * client runs by default (vm_cgame 2), and always on a pure server.
 *
 * This loads a QVM that q3lcc and q3asm built from the real cg_kick.c and
 * cg_syscalls.asm (kick_qvm_cgame.c is its driver). The real vm.c and
 * vm_interpreted.c run it. The real CL_CgameSystemCalls answers its traps,
 * and the real CL_FinishMove builds the command. Each frame, the engine
 * must receive exactly the angles cgame holds in VM memory. That covers a
 * frame that runs the kick out, a frame with no time to run (msec 0), and a
 * frame that zeroes the kick (follow, killcam). Each command's angles must
 * then be the view plus those angles. */
#include "../../code/client/client.h"
#include "../../code/botlib/botlib.h"
#include "../../code/qcommon/vm_local.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* kick_qvm_cgame.c's commands and flags */
enum { KQ_INIT, KQ_FRAME };
#define KQ_FIRE			1
#define KQ_FOLLOW		2
#define KQ_KILLCAM		4

void CL_FinishMove( usercmd_t *cmd );	/* cl_input.c's */
intptr_t CL_CgameSystemCalls( intptr_t *args );	/* cl_cgame.c's */

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)

/* ---- the engine state cl_cgame.c and cl_input.c name ---- */
clientActive_t cl;
clientConnection_t clc;
clientStatic_t cls;
vm_t *cgvm;
refexport_t re;
botlib_export_t *botlib_export;
static cvar_t engineCvars[32];
static int engineCvarCount;
cvar_t *com_developer = &engineCvars[0], *com_sv_running = &engineCvars[1];

/* ---- what vm.c needs to load one QVM from disk ---- */
static const char *qvmPath;
static int traps;
void QDECL Com_Printf( const char *fmt, ... ) { (void)fmt; }
void QDECL Com_DPrintf( const char *fmt, ... ) { (void)fmt; }
void QDECL Com_Error( int level, const char *fmt, ... ) {
	va_list ap;
	(void)level;
	va_start( ap, fmt ); fprintf( stderr, "Com_Error: " ); vfprintf( stderr, fmt, ap ); fprintf( stderr, "\n" ); va_end( ap );
	exit( 1 );
}
cvar_t *Cvar_Get( const char *name, const char *value, int flags ) {
	cvar_t *cv = &engineCvars[2 + engineCvarCount++ % ( ARRAY_LEN( engineCvars ) - 2 )];
	(void)name; (void)flags;
	cv->integer = atoi( value ); cv->value = atof( value );
	return cv;
}
void Cmd_AddCommand( const char *name, xcommand_t fn ) { (void)name; (void)fn; }
int FS_FindVM( void **startSearch, char *found, int foundlen, const char *name, int enableDll ) {
	(void)name; (void)enableDll;
	if ( *startSearch ) return -1;
	*startSearch = (void *)1;
	Q_strncpyz( found, qvmPath, foundlen );
	return VMI_COMPILED;	/* "a .qvm was found", as the engine reports one */
}
long FS_ReadFileDir( const char *path, void *searchPath, qboolean unpure, void **buffer ) {
	FILE *f = fopen( qvmPath, "rb" );
	long n;
	(void)path; (void)searchPath; (void)unpure;
	CHECK( f != NULL );
	fseek( f, 0, SEEK_END ); n = ftell( f ); rewind( f );
	*buffer = malloc( n + 1 );
	CHECK( fread( *buffer, 1, n, f ) == (size_t)n );
	fclose( f );
	return n;
}
long FS_ReadFile( const char *path, void **buffer ) { (void)path; if ( buffer ) *buffer = NULL; return -1; }
void FS_FreeFile( void *buffer ) { free( buffer ); }
qboolean FS_Which( const char *filename, void *searchPath ) { (void)filename; (void)searchPath; return qtrue; }
#ifdef HUNK_DEBUG
void *Hunk_AllocDebug( int size, ha_pref pref, char *label, char *file, int line ) { (void)pref; (void)label; (void)file; (void)line; return calloc( 1, size ); }
#else
void *Hunk_Alloc( int size, ha_pref pref ) { (void)pref; return calloc( 1, size ); }
#endif
#ifdef ZONE_DEBUG
void *Z_MallocDebug( int size, char *label, char *file, int line ) { (void)label; (void)file; (void)line; return calloc( 1, size ); }
#else
void *Z_Malloc( int size ) { return calloc( 1, size ); }
#endif
void Z_Free( void *ptr ) { free( ptr ); }
int Hunk_MemoryRemaining( void ) { return 1 << 30; }
void *QDECL Sys_LoadGameDll( const char *name, vmMainProc *entryPoint, intptr_t (QDECL *systemcalls)(intptr_t, ...) ) {
	(void)name; (void)entryPoint; (void)systemcalls; return NULL;
}
void Sys_UnloadDll( void *handle ) { (void)handle; }
/* the interpreter runs it: no JIT here */
void VM_Compile( vm_t *vm, vmHeader_t *header ) { (void)vm; (void)header; CHECK( 0 ); }
int VM_CallCompiled( vm_t *vm, int *args ) { (void)vm; (void)args; CHECK( 0 ); return 0; }
#if idx64 || id386
long qftolsse( float f ) { return lrintf( f ); }
#endif

/* Every trap the QVM makes goes through the engine's own dispatcher. */
static intptr_t CountedSystemCalls( intptr_t *args ) {
	if ( args[0] == CG_SETUSERCMDKICKANGLES ) traps++;
	return CL_CgameSystemCalls( args );
}

/* ---- the checks ---- */
static float *vmKick;	/* cg.kickAngles, in the QVM's memory */
static const vec3_t view = { 12.5f, 271.25f, 0.0f };

static int Bits( float f ) { int i; memcpy( &i, &f, sizeof( i ) ); return i; }

/* One client frame: cgame's CG_KickAngles, then the command CL_CreateCmd
 * finishes with the real CL_FinishMove. Returns whether cgame held a kick. */
static int Frame( int time, int flags ) {
	usercmd_t cmd;
	int i, before = traps, kicked = 0;
	VM_Call( cgvm, KQ_FRAME, time, flags );
	CHECK( traps == before + 1 );	/* the kick goes to the engine every frame */
	for ( i = 0; i < 3; i++ ) {
		CHECK( Bits( cl.cgameKickAngles[i] ) == Bits( vmKick[i] ) );	/* exactly what cgame holds */
		if ( vmKick[i] != 0.0f ) kicked = 1;
	}
	memset( &cmd, 0, sizeof( cmd ) );
	VectorCopy( view, cl.viewangles );
	CL_FinishMove( &cmd );
	for ( i = 0; i < 3; i++ ) {
		CHECK( cmd.angles[i] == ANGLE2SHORT( view[i] + vmKick[i] ) );
	}
	CHECK( VectorCompare( cl.viewangles, view ) );	/* never into the client's own view */
	return kicked;
}

int main( int argc, char **argv ) {
	int t = 1000, f, kickedFrames = 0, allAxes = 0, back = 0;
	CHECK( argc == 2 );
	qvmPath = argv[1];
	VM_Init();
	cgvm = VM_Create( "cgame", CountedSystemCalls, VMI_BYTECODE );
	CHECK( cgvm != NULL && !cgvm->compiled );
	vmKick = (float *)( cgvm->dataBase + ( (int)VM_Call( cgvm, KQ_INIT, t ) & cgvm->dataMask ) );

	/* a shot kicks, and the kick runs out over the following frames */
	for ( f = 0; f < 200; f++ ) {
		t += 16;
		if ( Frame( t, f == 0 ? KQ_FIRE : 0 ) ) {
			kickedFrames++;
			if ( vmKick[PITCH] != 0.0f && vmKick[YAW] != 0.0f && vmKick[ROLL] != 0.0f ) allAxes++;
			/* a frame with no time to run passes the same angles again */
			if ( f == 3 ) { vec3_t held; VectorCopy( vmKick, held ); CHECK( Frame( t, 0 ) && VectorCompare( vmKick, held ) ); }
		} else if ( f > 0 ) {
			back = 1;
			break;
		}
	}
	CHECK( kickedFrames > 4 && allAxes > 0 && back );	/* it kicked on all three axes, and came back to 0 */

	/* a frame that is not the player's own zeroes it, and the engine gets 0 */
	t += 16; CHECK( Frame( t, KQ_FIRE ) );
	t += 16; CHECK( !Frame( t, KQ_FOLLOW ) );
	t += 16; CHECK( !Frame( t, KQ_FOLLOW ) );
	t += 16; CHECK( Frame( t, KQ_FIRE ) );
	t += 16; CHECK( !Frame( t, KQ_KILLCAM ) );
	t += 16; CHECK( !Frame( t, 0 ) );

	VM_Free( cgvm );
	printf( "PASS: %d view kick QVM boundary checks (q3lcc/q3asm cg_kick.c + cg_syscalls.asm, vm_interpreted.c, CL_CgameSystemCalls, CL_FinishMove)\n", checks );
	return 0;
}
