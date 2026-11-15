/* The viewmodel's skeletal pose across the QVM boundary. A client runs
 * cgame.qvm by default (vm_cgame 2), and always on a pure server. Its cg_anim.c
 * builds and blends cg.weaponPose through two renderer traps. It then hands
 * the engine a refEntity_t whose last field points at that pose. Every one of
 * those pointers is a QVM offset.
 *
 * This loads a QVM that q3lcc and q3asm built from pose_qvm_cgame.c, the real
 * cg_anim.c and bg files, and the real cg_syscalls.asm. The real vm.c and
 * vm_interpreted.c run it. The real CL_CgameSystemCalls and
 * CL_AddRefEntityFromVM (cl_vm.c) answer its traps, and the file traps read
 * the shipped SPAS clip table. The renderer here records what it is handed.
 *
 * The real animation: every frame of the ready SPAS, cg_anim.c builds its
 * layers in the QVM's memory and the viewmodel reaches the renderer pointing
 * at the QVM's own cg.weaponPose, as native cgame's does. For a model the
 * renderer cannot pose, it carries no pose and the clip's frames.
 *
 * The traps called directly, the renderer must get:
 * - host pointers to exactly the QVM's own poses, and the QVM's weights;
 * - the entity's frames, and its pose pointer, or NULL for none;
 * - an offset past the QVM's memory masked back into it, as the interpreter
 *   masks every load.
 * A pose that starts inside the QVM's memory but runs past its end is refused
 * with ERR_DROP before the renderer sees it, at each of the three places (both
 * traps and the entity). A pose that ends on the last byte passes. */
#include "qvm_engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* pose_qvm_cgame.c's commands */
enum { PQ_INIT, PQ_BUILD, PQ_BLEND, PQ_ADD, PQ_ANIM_INIT, PQ_ANIM_FRAME };

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)

#define PQ_MODEL	7
#define PQ_MODEL_NOPOSE	8	/* a model the renderer cannot pose (no IQM poses) */
#define PQ_JOINTS	3

static void *Host( int address );
static int memorySize;		/* the QVM's data: dataMask + 1 bytes */

/* ---- the renderer: it records what the engine hands it ---- */
static int builds, blends, adds;
static modelPose_t *builtPose;
static int builtFrame, builtOldframe;
static float builtBacklerp;
static modelPose_t *blendOut;
static const modelPose_t *blendA, *blendB;
static float blendWeightA, blendWeightB;
static refEntity_t added;

static qboolean BuildModelPose( qhandle_t model, int frame, int oldframe, float backlerp, modelPose_t *out ) {
	int j;
	builds++;
	if ( model == PQ_MODEL_NOPOSE ) {
		return qfalse;
	}
	CHECK( model == PQ_MODEL );
	builtPose = out; builtFrame = frame; builtOldframe = oldframe; builtBacklerp = backlerp;
	/* every joint's translate says which sample it is */
	out->numJoints = PQ_JOINTS;
	for ( j = 0; j < PQ_JOINTS; j++ ) {
		VectorSet( out->joints[j].translate, frame, oldframe, backlerp + j );
		out->joints[j].rotate[0] = out->joints[j].rotate[1] = out->joints[j].rotate[2] = 0;
		out->joints[j].rotate[3] = 1;
		VectorSet( out->joints[j].scale, 1, 1, 1 );
	}
	return qtrue;
}
static void BlendModelPoses( modelPose_t *out, const modelPose_t *a, float weightA, const modelPose_t *b, float weightB ) {
	blends++;
	blendOut = out; blendA = a; blendB = b; blendWeightA = weightA; blendWeightB = weightB;
	if ( a->numJoints == PQ_JOINTS && b->numJoints == PQ_JOINTS && out != a ) {
		memmove( out, a, sizeof( *out ) );	/* the joints' sample is the first pose's */
	}
}
static void AddRefEntityToScene( const refEntity_t *entity ) {
	adds++;
	added = *entity;
}

/* ---- the files: cg_anim.c's clip table, from the shipped assets ---- */
static FILE *openFile;
int FS_FOpenFileByMode( const char *qpath, fileHandle_t *f, fsMode_t mode ) {
	char path[MAX_QPATH + 16];
	long size;
	CHECK( mode == FS_READ && !openFile );
	Com_sprintf( path, sizeof( path ), "assets/baseq3/%s", qpath );
	openFile = fopen( path, "rb" );
	CHECK( openFile != NULL );
	fseek( openFile, 0, SEEK_END ); size = ftell( openFile ); rewind( openFile );
	*f = 1;
	return (int)size;
}
int FS_Read( void *buffer, int len, fileHandle_t f ) {
	CHECK( f == 1 && openFile );
	CHECK( fread( buffer, 1, len, openFile ) == (size_t)len );
	return len;
}
void FS_FCloseFile( fileHandle_t f ) {
	CHECK( f == 1 && openFile );
	fclose( openFile );
	openFile = NULL;
}

/* ---- the QVM ---- */
static int vmAddress[4];	/* pqPoses[0..2] and pqEntity, as QVM offsets */

static void *Host( int address ) {
	return cgvm->dataBase + ( address & cgvm->dataMask );
}

static void Load( void ) {
	int *table;
	cgvm = VM_Create( "cgame", CL_CgameSystemCalls, VMI_BYTECODE );
	CHECK( cgvm != NULL && !cgvm->compiled && !VM_IsNative( cgvm ) );
	memorySize = cgvm->dataMask + 1;
	table = Host( (int)VM_Call( cgvm, PQ_INIT ) );
	memcpy( vmAddress, table, sizeof( vmAddress ) );
}

/* One QVM call that must be refused with ERR_DROP: the engine drops the
 * game, which unloads the running cgame as CL_ShutdownCGame does, and a
 * fresh one is loaded for the next check. */
static void Dropped( int command, int arg0, int arg1, int arg2, int arg3, int arg4 ) {
	jmp_buf jump;
	int before = builds + blends + adds;
	qvmDropMessage[0] = '\0';
	if ( !setjmp( jump ) ) {
		qvmDrop = &jump;
		VM_Call( cgvm, command, arg0, arg1, arg2, arg3, arg4 );
		qvmDrop = NULL;
		CHECK( !"the call was not dropped" );
	}
	qvmDrop = NULL;
	CHECK( strstr( qvmDropMessage, "runs past the VM's memory" ) != NULL );
	CHECK( builds + blends + adds == before );	/* the renderer never saw it */
	VM_Forced_Unload_Start();
	VM_Free( cgvm );
	VM_Forced_Unload_Done();
	Load();
}

/* The SPAS's clips, read from the same table cg_anim.c loads: every frame
 * any clip can show lies in [0, frames). */
static int ClipFrames( void ) {
	FILE *f = fopen( "assets/baseq3/models/weapons2/spas12/view.cfg", "r" );
	char line[256], name[64];
	int first, count, frames = 0;
	CHECK( f != NULL );
	while ( fgets( line, sizeof( line ), f ) ) {
		if ( sscanf( line, "%63s %d %d", name, &first, &count ) == 3 && first + count > frames ) {
			frames = first + count;
		}
	}
	fclose( f );
	CHECK( frames > 0 );
	return frames;
}

static qboolean InsideQVM( const void *p, size_t size ) {
	const byte *b = p;
	return b >= cgvm->dataBase && b + size <= cgvm->dataBase + memorySize;
}

/* The real cg_anim.c, built as a cgame.qvm builds it: the ready SPAS, frame
 * after frame, reaches the renderer pointing at the QVM's cg.weaponPose. */
static void TestAnim( void ) {
	modelPose_t *weaponPose;
	int t = 1000, f, frames = ClipFrames(), posed = 0, built;
	weaponPose = Host( (int)VM_Call( cgvm, PQ_ANIM_INIT, PQ_MODEL ) );
	CHECK( InsideQVM( weaponPose, sizeof( *weaponPose ) ) );
	for ( f = 0; f < 120; f++ ) {
		int buildsBefore = builds, addsBefore = adds;
		t += 16;
		CHECK( VM_Call( cgvm, PQ_ANIM_FRAME, t, 16, PQ_MODEL ) == 1 );
		built = builds - buildsBefore;
		CHECK( built > 0 );	/* cg_anim.c sampled its layers through the trap */
		CHECK( InsideQVM( builtPose, sizeof( *builtPose ) ) );
		CHECK( adds == addsBefore + 1 );
		CHECK( added.hModel == PQ_MODEL && added.reType == RT_MODEL );
		CHECK( added.pose == weaponPose );	/* the QVM's own cg.weaponPose */
		CHECK( added.pose->numJoints == PQ_JOINTS );
		CHECK( added.frame >= 0 && added.frame < frames && added.oldframe >= 0 && added.oldframe < frames );
		posed++;
	}
	CHECK( posed == 120 );
	/* a model the renderer cannot pose: no pose, the clip's frames */
	t += 16;
	CHECK( VM_Call( cgvm, PQ_ANIM_FRAME, t, 16, PQ_MODEL_NOPOSE ) == 1 );
	CHECK( added.hModel == PQ_MODEL_NOPOSE && added.pose == NULL );
	CHECK( added.frame >= 0 && added.frame < frames );
	/* and posed again */
	t += 16;
	CHECK( VM_Call( cgvm, PQ_ANIM_FRAME, t, 16, PQ_MODEL ) == 1 );
	CHECK( added.pose == weaponPose );
}

int main( int argc, char **argv ) {
	modelPose_t *pose[3];
	int i, last, past;
	CHECK( argc == 2 );
	qvmPath = argv[1];
	re.BuildModelPose = BuildModelPose;
	re.BlendModelPoses = BlendModelPoses;
	re.AddRefEntityToScene = AddRefEntityToScene;
	VM_Init();
	Load();
	for ( i = 0; i < 3; i++ ) {
		pose[i] = Host( vmAddress[i] );
	}
	/* where a pose ends on the QVM's last byte, and where one runs 4 past it */
	last = memorySize - (int)sizeof( modelPose_t );
	past = last + 4;

	TestAnim();
	builds = blends = adds = 0;

	/* the traps called directly: each sample is written into the QVM's own pose */
	CHECK( VM_Call( cgvm, PQ_BUILD, PQ_MODEL, 4, 3, 250, vmAddress[0] ) == qtrue );
	CHECK( builds == 1 && builtPose == pose[0] );
	CHECK( builtFrame == 4 && builtOldframe == 3 && builtBacklerp == 250 / 1000.0f );
	CHECK( pose[0]->numJoints == PQ_JOINTS && pose[0]->joints[2].translate[0] == 4 );
	CHECK( pose[0]->joints[2].translate[2] == 250 / 1000.0f + 2 );
	CHECK( VM_Call( cgvm, PQ_BUILD, PQ_MODEL, 9, 9, 0, vmAddress[1] ) == qtrue );
	CHECK( builds == 2 && builtPose == pose[1] && pose[1]->joints[0].translate[1] == 9 );

	/* their blend into the third, at the QVM's weights */
	VM_Call( cgvm, PQ_BLEND, vmAddress[2], vmAddress[0], 600, vmAddress[1], 400 );
	CHECK( blends == 1 && blendOut == pose[2] && blendA == pose[0] && blendB == pose[1] );
	CHECK( blendWeightA == 600 / 1000.0f && blendWeightB == 400 / 1000.0f );

	/* the viewmodel: its frames, and the pose it points at */
	VM_Call( cgvm, PQ_ADD, PQ_MODEL, 4, 3, 250, vmAddress[2] );
	CHECK( adds == 1 && added.reType == RT_MODEL && added.hModel == PQ_MODEL );
	CHECK( added.frame == 4 && added.oldframe == 3 && added.backlerp == 250 / 1000.0f );
	CHECK( added.renderfx == ( RF_FIRST_PERSON | RF_DEPTHHACK ) );
	CHECK( added.pose == pose[2] );
	/* no pose: frames only */
	VM_Call( cgvm, PQ_ADD, PQ_MODEL, 4, 3, 250, 0 );
	CHECK( adds == 2 && added.pose == NULL && added.frame == 4 );
	/* past the QVM's memory: masked back into it, as every QVM load is */
	VM_Call( cgvm, PQ_ADD, PQ_MODEL, 4, 3, 250, vmAddress[2] + memorySize );
	CHECK( adds == 3 && added.pose == pose[2] );
	/* a pose ending on the QVM's last byte is inside it (read only here: the
	 * top of the QVM's memory is its program stack) */
	VM_Call( cgvm, PQ_ADD, PQ_MODEL, 4, 3, 250, last );
	CHECK( adds == 4 && added.pose == Host( last ) );
	VM_Call( cgvm, PQ_BLEND, vmAddress[2], last, 500, vmAddress[1], 500 );
	CHECK( blends == 2 && blendA == Host( last ) );

	/* one that runs past the end is refused, at each place a pose crosses */
	Dropped( PQ_BUILD, PQ_MODEL, 4, 3, 250, past );
	Dropped( PQ_BLEND, past, vmAddress[0], 500, vmAddress[1], 500 );
	Dropped( PQ_BLEND, vmAddress[2], past, 500, vmAddress[1], 500 );
	Dropped( PQ_BLEND, vmAddress[2], vmAddress[0], 500, past, 500 );
	Dropped( PQ_ADD, PQ_MODEL, 4, 3, 250, past );

	/* and the reloaded cgame still passes its poses */
	for ( i = 0; i < 3; i++ ) {
		pose[i] = Host( vmAddress[i] );
	}
	CHECK( VM_Call( cgvm, PQ_BUILD, PQ_MODEL, 1, 2, 500, vmAddress[0] ) == qtrue && builtPose == pose[0] );
	VM_Call( cgvm, PQ_ADD, PQ_MODEL, 1, 2, 500, vmAddress[0] );
	CHECK( added.pose == pose[0] && pose[0]->joints[0].translate[1] == 2 );

	VM_Free( cgvm );
	printf( "PASS: %d skeletal pose QVM boundary checks (q3lcc/q3asm cg_anim.c + cg_syscalls.asm, vm_interpreted.c, CL_CgameSystemCalls, CL_AddRefEntityFromVM)\n", checks );
	return 0;
}
