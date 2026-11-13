/* The engine a QVM boundary test runs a real cgame.qvm in (qvm_engine.h).
 * Moved out of kick_qvm_test.c, unchanged but for the ERR_DROP hook. */
#include "qvm_engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QVM_ENGINE_CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s line %d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)

const char *qvmPath;
jmp_buf *qvmDrop;
char qvmDropMessage[1024];

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
void QDECL Com_Printf( const char *fmt, ... ) { (void)fmt; }
void QDECL Com_DPrintf( const char *fmt, ... ) { (void)fmt; }
void QDECL Com_Error( int level, const char *fmt, ... ) {
	va_list ap;
	va_start( ap, fmt );
	if ( qvmDrop && level == ERR_DROP ) {
		Q_vsnprintf( qvmDropMessage, sizeof( qvmDropMessage ), fmt, ap );
		va_end( ap );
		longjmp( *qvmDrop, 1 );
	}
	fprintf( stderr, "Com_Error: " ); vfprintf( stderr, fmt, ap ); fprintf( stderr, "\n" ); va_end( ap );
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
	QVM_ENGINE_CHECK( f != NULL );
	fseek( f, 0, SEEK_END ); n = ftell( f ); rewind( f );
	*buffer = malloc( n + 1 );
	QVM_ENGINE_CHECK( fread( *buffer, 1, n, f ) == (size_t)n );
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
void VM_Compile( vm_t *vm, vmHeader_t *header ) { (void)vm; (void)header; QVM_ENGINE_CHECK( 0 ); }
int VM_CallCompiled( vm_t *vm, int *args ) { (void)vm; (void)args; QVM_ENGINE_CHECK( 0 ); return 0; }
#if idx64 || id386
long qftolsse( float f ) { return lrintf( f ); }
#endif
