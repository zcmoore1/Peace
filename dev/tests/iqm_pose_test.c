/* The renderer's half of the viewmodel's skeletal pose, on the real
 * tr_model_iqm.c of either renderer. It is built twice: plain for
 * renderergl1, and with -DIQM_POSE_GL2 for renderergl2. cgame blends its
 * animation layers into one pose (cg_anim.c) through RE_BuildModelPose and
 * RE_BlendModelPoses, and RB_IQMSurfaceAnim skins the model from that pose.
 * For the shipped viewmodels (argv[1..]), loaded by the real R_LoadIQM, it
 * checks:
 * - A pose built from an entity's frames skins exactly as the frames do (the
 *   same bits), on every surface.
 * - Two single-frame poses blended at the entity's weights skin within float
 *   rounding of the frames' own lerp, which is what cgame's layer blend is.
 * - A pose whose joint count is not the model's (one built for another
 *   model, or one a QVM filled badly) is ignored: the frames skin.
 * - RE_BuildModelPose refuses a model with no poses or no frames, and wraps a
 *   frame past either end into the model's frames (GL2's modulo, which a
 *   negative frame once took to a read before the poses).
 * - RE_BlendModelPoses never writes past MAX_MODEL_JOINTS, whatever counts it
 *   is given.
 * It prints a digest of every sample's output bytes: the frames' skin, the
 * pose RE_BuildModelPose builds from them (a lerp and a slerp), the blend of
 * the two single-frame poses and its skin. The runner requires GL1's digest
 * to be GL2's, so the two renderers' pose code must give the same bits. */
#ifdef IQM_POSE_GL2
#include "../../code/renderergl2/tr_model_iqm.c"
#else
#include "../../code/renderergl1/tr_model_iqm.c"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)

/* ---- what the real file names, outside the code under test ---- */
refimport_t ri;
trGlobals_t tr;
backEndState_t backEnd;
shaderCommands_t tess;
cvar_t *r_shadows;
#ifdef IQM_POSE_GL2
glRefConfig_t glRefConfig;	/* glslMaxAnimatedBones 0: no VAOs, the CPU skin */
glstate_t glState;
#endif
static shader_t defaultShader;
#define MAX_TEST_MODELS 4
static model_t models[MAX_TEST_MODELS];
static int numModels;

void QDECL Com_Printf( const char *fmt, ... ) { (void)fmt; }
void QDECL Com_Error( int level, const char *fmt, ... ) {
	va_list ap;
	(void)level;
	va_start( ap, fmt ); vfprintf( stderr, fmt, ap ); va_end( ap );
	exit( 1 );
}
static void QDECL TestPrintf( int level, const char *fmt, ... ) {
	va_list ap;
	if ( level == PRINT_ALL ) return;
	va_start( ap, fmt ); vfprintf( stderr, fmt, ap ); va_end( ap );
}
static void Q_NO_RETURN QDECL TestError( int level, const char *fmt, ... ) {
	va_list ap;
	(void)level;
	va_start( ap, fmt ); vfprintf( stderr, fmt, ap ); va_end( ap );
	exit( 1 );
}
#ifdef HUNK_DEBUG
static void *TestHunkAlloc( int size, ha_pref pref, char *label, char *file, int line ) { (void)pref; (void)label; (void)file; (void)line; return calloc( 1, size ); }
#else
static void *TestHunkAlloc( int size, ha_pref pref ) { (void)pref; return calloc( 1, size ); }
#endif
shader_t *R_FindShader( const char *name, int lightmapIndex, qboolean mipRawImage ) {
	(void)name; (void)lightmapIndex; (void)mipRawImage;
	return &defaultShader;
}
model_t *R_GetModelByHandle( qhandle_t index ) {
	return index >= 1 && index <= numModels ? &models[index - 1] : &models[0];
}
void RB_CheckOverflow( int verts, int indexes ) {
	(void)verts; (void)indexes;
	CHECK( !"a surface overflowed tess" );
}
#ifdef IQM_POSE_GL2
void R_VaoPackNormal( int16_t *packed, vec3_t v ) { packed[0] = packed[1] = packed[2] = packed[3] = 0; (void)v; }
void R_VaoPackTangent( int16_t *packed, vec4_t v ) { packed[0] = packed[1] = packed[2] = packed[3] = 0; (void)v; }
vao_t *R_CreateVao( const char *name, byte *vertexes, int vertexesSize, byte *indexes, int indexesSize, vaoUsage_t usage ) {
	(void)name; (void)vertexes; (void)vertexesSize; (void)indexes; (void)indexesSize; (void)usage;
	CHECK( !"no VAOs without GLSL bones" );
	return NULL;
}
void Vao_SetVertexPointers( vao_t *vao ) { (void)vao; CHECK( !"no VAOs without GLSL bones" ); }
#endif

/* ---- skinning ---- */
static trRefEntity_t entity;
static float skinned[64 * SHADER_MAX_VERTEXES * 3];	/* every surface's skinned positions */
static int skinnedCount;

/* FNV-1a over every output byte, in order */
static unsigned long long digest = 1469598103934665603ULL;
static void Digest( const void *p, size_t n ) {
	const unsigned char *b = p;
	while ( n-- ) {
		digest ^= *b++;
		digest *= 1099511628211ULL;
	}
}
static void DigestPose( const modelPose_t *pose ) {
	Digest( &pose->numJoints, sizeof( pose->numJoints ) );
	Digest( pose->joints, pose->numJoints * sizeof( pose->joints[0] ) );
}

/* Skin every surface of the model, from entity.e as it stands. */
static int Skin( const iqmData_t *data, float *into ) {
	int s, v, n = 0;
	backEnd.currentEntity = &entity;
	for ( s = 0; s < data->num_surfaces; s++ ) {
		tess.numVertexes = tess.numIndexes = 0;
		RB_IQMSurfaceAnim( (surfaceType_t *)&data->surfaces[s] );
		CHECK( tess.numVertexes == data->surfaces[s].num_vertexes );
		for ( v = 0; v < tess.numVertexes; v++, n++ ) {
			CHECK( n < 64 * SHADER_MAX_VERTEXES );
			into[3 * n + 0] = tess.xyz[v][0];
			into[3 * n + 1] = tess.xyz[v][1];
			into[3 * n + 2] = tess.xyz[v][2];
		}
	}
	return n;
}

static float poseSkin[64 * SHADER_MAX_VERTEXES * 3], blendSkin[64 * SHADER_MAX_VERTEXES * 3];

static void SetFrames( int frame, int oldframe, float backlerp, const modelPose_t *pose ) {
	entity.e.frame = frame;
	entity.e.oldframe = oldframe;
	entity.e.backlerp = backlerp;
	entity.e.pose = pose;
}

static unsigned seed = 12345;
static unsigned Rand( void ) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

static void TestModel( const char *path ) {
	model_t *mod;
	iqmData_t *data;
	FILE *f = fopen( path, "rb" );
	long size;
	void *buffer;
	qhandle_t handle;
	static modelPose_t pose, oldPose, newPose, blend;
	int n, i, k, sample;
	float maxError = 0;

	CHECK( f != NULL );
	fseek( f, 0, SEEK_END ); size = ftell( f ); rewind( f );
	buffer = malloc( size );
	CHECK( fread( buffer, 1, size, f ) == (size_t)size );
	fclose( f );
	CHECK( numModels < MAX_TEST_MODELS );
	mod = &models[numModels++];
	handle = numModels;
	memset( mod, 0, sizeof( *mod ) );
	Q_strncpyz( mod->name, path, sizeof( mod->name ) );
	CHECK( R_LoadIQM( mod, buffer, size, path ) );
	CHECK( mod->type == MOD_IQM );
	data = mod->modelData;
	CHECK( data->num_poses > 0 && data->num_poses <= MAX_MODEL_JOINTS && data->num_frames > 1 );

	for ( sample = 0; sample < 200; sample++ ) {
		int frame = Rand() % data->num_frames, oldframe = Rand() % data->num_frames;
		float backlerp = ( Rand() % 1001 ) / 1000.0f;
		if ( sample % 10 == 0 ) oldframe = frame;	/* a frame on its own */

		/* the frames */
		SetFrames( frame, oldframe, backlerp, NULL );
		n = Skin( data, skinned );
		CHECK( n > 0 );
		Digest( skinned, 3 * n * sizeof( float ) );

		/* the pose built from them: the same bits */
		CHECK( RE_BuildModelPose( handle, frame, oldframe, backlerp, &pose ) );
		CHECK( pose.numJoints == data->num_poses );
		DigestPose( &pose );
		SetFrames( frame, oldframe, backlerp, &pose );
		CHECK( Skin( data, poseSkin ) == n );
		CHECK( !memcmp( poseSkin, skinned, 3 * n * sizeof( float ) ) );
		/* the entity's frames are not read while it has a pose */
		SetFrames( ( frame + 1 ) % data->num_frames, frame, 0.5f, &pose );
		CHECK( Skin( data, poseSkin ) == n );
		CHECK( !memcmp( poseSkin, skinned, 3 * n * sizeof( float ) ) );

		/* cgame's layer blend: each frame alone, blended at the same weights */
		CHECK( RE_BuildModelPose( handle, oldframe, oldframe, 0, &oldPose ) );
		CHECK( RE_BuildModelPose( handle, frame, frame, 0, &newPose ) );
		RE_BlendModelPoses( &blend, &oldPose, backlerp, &newPose, 1.0f - backlerp );
		CHECK( blend.numJoints == data->num_poses );
		DigestPose( &blend );
		SetFrames( 0, 0, 0, &blend );
		CHECK( Skin( data, blendSkin ) == n );
		Digest( blendSkin, 3 * n * sizeof( float ) );
		for ( i = 0; i < 3 * n; i++ ) {
			float e = fabsf( blendSkin[i] - skinned[i] );
			if ( e > maxError ) maxError = e;
		}

		/* a pose that is not this model's is ignored: the frames skin (its
		 * joints moved, so skinning from it would show) */
		for ( k = 0; k < data->num_poses; k++ ) pose.joints[k].translate[0] += 10;
		pose.numJoints = data->num_poses - 1;
		SetFrames( frame, oldframe, backlerp, &pose );
		CHECK( Skin( data, poseSkin ) == n && !memcmp( poseSkin, skinned, 3 * n * sizeof( float ) ) );
		pose.numJoints = data->num_poses + 1;
		CHECK( Skin( data, poseSkin ) == n && !memcmp( poseSkin, skinned, 3 * n * sizeof( float ) ) );
		pose.numJoints = -1;
		CHECK( Skin( data, poseSkin ) == n && !memcmp( poseSkin, skinned, 3 * n * sizeof( float ) ) );
	}
	CHECK( maxError < 0.01f );	/* within float rounding, on models tens of units long */
	skinnedCount += n;

	/* a frame past either end wraps into the model's frames */
	CHECK( RE_BuildModelPose( handle, -1, data->num_frames, 0.25f, &pose ) );
	CHECK( RE_BuildModelPose( handle, data->num_frames - 1, 0, 0.25f, &oldPose ) );
	CHECK( pose.numJoints == data->num_poses && oldPose.numJoints == data->num_poses );
	CHECK( !memcmp( pose.joints, oldPose.joints, data->num_poses * sizeof( pose.joints[0] ) ) );
	/* no frames, or no poses: no pose */
	k = data->num_frames; data->num_frames = 0;
	pose.numJoints = 77;
	CHECK( !RE_BuildModelPose( handle, 0, 0, 0, &pose ) && pose.numJoints == 77 );
	data->num_frames = k;
	k = data->num_poses; data->num_poses = 0;
	CHECK( !RE_BuildModelPose( handle, 0, 0, 0, &pose ) && pose.numJoints == 77 );
	data->num_poses = k;
	mod->type = MOD_MESH;
	CHECK( !RE_BuildModelPose( handle, 0, 0, 0, &pose ) );
	mod->type = MOD_IQM;
}

int main( int argc, char **argv ) {
	static struct { modelPose_t pose; int after[64]; } guarded;
	static modelPose_t a, b;
	int i;

	ri.Printf = TestPrintf;
	ri.Error = TestError;
#ifdef HUNK_DEBUG
	ri.Hunk_AllocDebug = TestHunkAlloc;
#else
	ri.Hunk_Alloc = TestHunkAlloc;
#endif
	tr.defaultShader = &defaultShader;
	numModels = 1;	/* handle 1 is no model: a non-IQM placeholder */
	models[0].type = MOD_BAD;
	CHECK( argc > 1 );
	CHECK( !RE_BuildModelPose( 1, 0, 0, 0, &a ) );
	for ( i = 1; i < argc; i++ ) {
		TestModel( argv[i] );
	}

	/* a blend never writes past the pose's joints, whatever counts it gets */
	memset( &guarded, 0, sizeof( guarded ) );
	for ( i = 0; i < 64; i++ ) guarded.after[i] = 0x5eed;
	a.numJoints = b.numJoints = 100000;
	RE_BlendModelPoses( &guarded.pose, &a, 0.5f, &b, 0.5f );
	CHECK( guarded.pose.numJoints == MAX_MODEL_JOINTS );
	for ( i = 0; i < 64; i++ ) CHECK( guarded.after[i] == 0x5eed );
	a.numJoints = -5;
	RE_BlendModelPoses( &guarded.pose, &a, 0.5f, &b, 0.5f );
	CHECK( guarded.pose.numJoints == 0 );
	/* no weight: nothing written */
	guarded.pose.numJoints = 9;
	RE_BlendModelPoses( &guarded.pose, &a, 0, &b, 0 );
	CHECK( guarded.pose.numJoints == 9 );

	printf( "digest %016llx\n", digest );
#ifdef IQM_POSE_GL2
	printf( "PASS: %d renderergl2 IQM pose checks (%d skinned vertices per sample)\n", checks, skinnedCount );
#else
	printf( "PASS: %d renderergl1 IQM pose checks (%d skinned vertices per sample)\n", checks, skinnedCount );
#endif
	return 0;
}
