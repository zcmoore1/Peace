/* The cgame side of the skeletal pose's QVM boundary test (pose_qvm_test.c).
 * q3lcc compiles it with the real cg_anim.c, bg_pmove.c, bg_loadout.c,
 * bg_misc.c, bg_slidemove.c, q_math.c, q_shared.c and bg_lib.c (Q3_VM
 * defined, as for a shipped cgame.qvm), and q3asm links them with the real
 * cg_syscalls.asm. The engine side is the real vm.c, vm_interpreted.c,
 * CL_CgameSystemCalls and CL_AddRefEntityFromVM.
 *
 * Two kinds of command:
 * - PQ_ANIM_*: the real viewmodel animation. cg_anim.c loads the SPAS's clip
 *   table through the file traps, builds and blends cg.weaponPose through the
 *   pose traps (CG_WeapAnim_BuildPose), and points the viewmodel's
 *   refEntity_t at it (CG_WeapAnim_Apply), exactly as a shipped cgame.qvm
 *   draws the gun (cg_weapons.c).
 * - PQ_BUILD, PQ_BLEND, PQ_ADD: the same traps called directly with a pose
 *   address the engine chooses, so it can hand the QVM a pose that runs past
 *   the QVM's memory.
 * vmMain must stay the first function: a QVM starts at code 0. */
/* q3cpp looks for a quoted include under the source file's directory and
 * under each directory an earlier include named, joined as written. Naming
 * q_shared.h first gives cg_local.h's "../qcommon/" and "../game/" a base. */
#include "../../code/qcommon/q_shared.h"
#include "../../code/cgame/cg_local.h"

enum { PQ_INIT, PQ_BUILD, PQ_BLEND, PQ_ADD, PQ_ANIM_INIT, PQ_ANIM_FRAME };

cg_t cg;
static modelPose_t pqPoses[3];	/* two sampled layers and their blend */
static refEntity_t pqEntity;
static playerState_t pqPs;
static int pqAddresses[4];

Q_EXPORT intptr_t vmMain( int command, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8, int arg9, int arg10, int arg11 ) {
	switch ( command ) {
	case PQ_INIT:	/* where the poses and the entity are, for the engine to read back */
		pqAddresses[0] = (int)&pqPoses[0];
		pqAddresses[1] = (int)&pqPoses[1];
		pqAddresses[2] = (int)&pqPoses[2];
		pqAddresses[3] = (int)&pqEntity;
		return (intptr_t)pqAddresses;
	case PQ_BUILD:	/* model, frame, oldframe, backlerp in thousandths, the pose */
		return trap_R_BuildModelPose( arg0, arg1, arg2, arg3 / 1000.0f, (modelPose_t *)arg4 );
	case PQ_BLEND:	/* the result, the first pose and its weight, the second and its weight */
		trap_R_BlendModelPoses( (modelPose_t *)arg0, (const modelPose_t *)arg1, arg2 / 1000.0f,
			(const modelPose_t *)arg3, arg4 / 1000.0f );
		return 0;
	case PQ_ADD:	/* model, frame, oldframe, backlerp in thousandths, the pose (0: none) */
		memset( &pqEntity, 0, sizeof( pqEntity ) );
		pqEntity.reType = RT_MODEL;
		pqEntity.hModel = arg0;
		pqEntity.frame = arg1;
		pqEntity.oldframe = arg2;
		pqEntity.backlerp = arg3 / 1000.0f;
		pqEntity.renderfx = RF_FIRST_PERSON | RF_DEPTHHACK;
		pqEntity.pose = (const modelPose_t *)arg4;
		trap_R_AddRefEntityToScene( &pqEntity );
		return 0;
	case PQ_ANIM_INIT:	/* the SPAS's clip table, for this model; returns &cg.weaponPose */
		CG_WeapAnim_Init();
		CG_WeapAnim_RegisterClips( WP_SHOTGUN, arg0, "models/weapons2/spas12/view.cfg" );
		return (intptr_t)&cg.weaponPose;
	case PQ_ANIM_FRAME:	/* time, msec, model: one frame of the ready SPAS, drawn */
		cg.time = arg0;
		memset( &pqPs, 0, sizeof( pqPs ) );
		pqPs.weapon = WP_SHOTGUN;
		pqPs.weaponstate = WEAPON_READY;
		pqPs.stats[STAT_HEALTH] = 100;
		memset( &pqEntity, 0, sizeof( pqEntity ) );
		pqEntity.reType = RT_MODEL;
		pqEntity.hModel = arg2;
		pqEntity.renderfx = RF_FIRST_PERSON | RF_DEPTHHACK;
		if ( !CG_WeapAnim_Apply( &pqPs, &pqEntity, arg1 ) ) {
			return 0;
		}
		trap_R_AddRefEntityToScene( &pqEntity );
		return 1;
	}
	return -1;
}

/* q_shared.c's and cg_anim.c's prints and errors: through the engine, as
 * cg_main.c's */
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
void QDECL CG_Printf( const char *msg, ... ) {
	va_list argptr;
	char text[1024];
	va_start( argptr, msg );
	Q_vsnprintf( text, sizeof( text ), msg, argptr );
	va_end( argptr );
	trap_Print( text );
}
