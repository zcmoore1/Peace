/* Server-authored replay snapshots use the normal scene/event pipeline.
 * This layer only owns the presentation boundary and lobby-wide banner. */
#include "cg_local.h"

qboolean CG_Killcam_Active( void ) {
	return cg.snap && (cg.snap->snapFlags & SNAPFLAG_REPLAY);
}

void CG_Killcam_Transition( const snapshot_t *previous, const snapshot_t *next ) {
	int n;
	entityState_t pending;
	if ( !previous || !((previous->snapFlags ^ next->snapFlags) & SNAPFLAG_REPLAY) ) return;
	// A replay is a camera/time cut, including when the viewer is the killer.
	// Preserve the newly received states while clearing live effects, event
	// latches and animation interpolation that belong to the other timeline.
	for ( n = 0; n < MAX_GENTITIES; n++ ) {
		pending = cg_entities[n].nextState;
		memset(&cg_entities[n], 0, sizeof(cg_entities[n]));
		cg_entities[n].nextState = pending;
	}
	memset(&cg.predictedPlayerEntity, 0, sizeof(cg.predictedPlayerEntity));
	CG_InitLocalEntities();
	CG_InitMarkPolys();
	CG_ClearParticles();
	CG_WeapAnim_Reset();
	trap_S_ClearLoopingSounds(qtrue);
	cg.soundBufferIn = cg.soundBufferOut = 0;
	cg.rewardTime = cg.rewardStack = cg.centerPrintTime = 0;
	cg.damageTime = cg.v_dmg_time = 0;
	cg.stepTime = cg.landTime = cg.duckTime = 0;
	cg.adsTime = 0;
	cg.adsClientNum = -1;
	cg.zoomed = qfalse;
	cg.zoomTime = 0;
	cg.validPPS = qfalse;
	cg.hyperspace = qfalse;
	cg.predictedErrorTime = 0;
	VectorClear(cg.predictedError);
	cg.thisFrameTeleport = cg.nextFrameTeleport = qtrue;
	cg.eventSequence = next->ps.eventSequence;
	if ( !(next->snapFlags & SNAPFLAG_REPLAY) ) cg.weaponSelect = next->ps.weapon;
}

void CG_Killcam_Draw( void ) {
	const char *title = cgs.gametype == GT_SND ? "ROUND WINNING KILLCAM" : "FINAL KILLCAM";
	const char *name;
	float width;
	int killer;
	if ( !CG_Killcam_Active() ) return;
	killer = cg.snap->ps.clientNum;
	width = BIGCHAR_WIDTH * strlen(title);
	CG_DrawBigString((SCREEN_WIDTH-width)/2, 40, title, 1);
	if ( killer >= 0 && killer < cgs.maxclients ) {
		name = cgs.clientinfo[killer].name;
		width = BIGCHAR_WIDTH * Q_PrintStrlen(name);
		CG_DrawBigString((SCREEN_WIDTH-width)/2, 64, name, 1);
	}
}
