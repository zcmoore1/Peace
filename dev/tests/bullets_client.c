/* The client half of the client-side bullets test (bullets_test.c): the real
 * prediction (CG_PredictPlayerState, cg_predict.c) over the client's own
 * commands, the real playerstate and entity events (cg_playerstate.c,
 * cg_event.c) and the real CG_FireWeapon and its bullets (cg_weapons.c),
 * tracing with the real CG_Trace against the same world the server's half
 * uses (bullets_world.c). What it draws is recorded where it lands: blood
 * (CG_Bleed) and marks (CG_ImpactMark), the rail's core, the shotgun's smoke
 * and tracers. Only engine traps and modules outside these files are
 * stubbed. */
#include "../../code/cgame/cg_local.h"
#include "bullets_world.h"
#include "bullets.h"

cg_t cg;
cgs_t cgs;
centity_t cg_entities[MAX_GENTITIES];
weaponInfo_t cg_weapons[MAX_WEAPONS];
itemInfo_t cg_items[MAX_ITEMS];
vmCvar_t cg_tracerChance, cg_tracerLength, cg_tracerWidth, cg_errorDecay, cg_showmiss, cg_nopredict,
 cg_synchronousClients, cg_railTrailTime, cg_oldRail, cg_drawGun, cg_brassTime, cg_debugEvents,
 cg_footsteps, cg_blood, cg_noProjectileTrail, cg_oldPlasma, cg_oldRocket, cg_weaponDebug, cg_smoothClients,
 cg_buildScript, cg_paused, cg_gibs, cg_noVoiceChats, cg_noVoiceText, cg_teamChatsOnly, cg_thirdPerson,
 cg_autoswitch, cg_drawFriend, cg_ignore, cg_simpleItems, cg_shadows, cg_viewsize, cg_fov, cg_zoomFov,
 cg_gun_x, cg_gun_y, cg_gun_z, cg_centertime, cg_runpitch, cg_runroll, cg_bobup, cg_bobpitch, cg_bobroll,
 cg_swingSpeed, cg_animSpeed, cg_lagometer, cg_stats, cg_draw2D, cg_drawStatus, cg_drawTimer, cg_drawFPS,
 cg_drawSnapshot, cg_drawAmmoWarning, cg_drawCrosshair, cg_drawCrosshairNames, cg_drawRewards,
 cg_crosshairSize, cg_crosshairHealth, cg_crosshairX, cg_crosshairY, cg_drawIcons, cg_teamOverlayUserinfo,
 cg_addMarks, cg_predictItems, cg_deferPlayers, cg_drawTeamOverlay, cg_scorePlum, cg_timescale,
 cg_timescaleFadeEnd, cg_timescaleFadeSpeed, cg_cameraOrbit, cg_cameraOrbitDelay, cg_hudFiles,
 cg_trueLightning, cg_sight, cg_drawAttacker, cg_weaponBob;

/* ---- the rest of the cgame and engine these files call ---- */
void QDECL CG_Printf( const char *fmt, ... ) { (void)fmt; }
void QDECL CG_Error( const char *fmt, ... ) {
	va_list a; va_start( a, fmt ); vfprintf( stderr, fmt, a ); va_end( a ); abort();
}
const char *CG_ConfigString( int index ) { (void)index; return ""; }
void CG_CenterPrint( const char *str, int y, int charWidth ) { (void)str; (void)y; (void)charWidth; }
qboolean CG_Killcam_Active( void ) { return qfalse; }
sfxHandle_t CG_CustomSound( int clientNum, const char *soundName ) { (void)clientNum; (void)soundName; return 0; }
void CG_AddBufferedSound( sfxHandle_t sfx ) { (void)sfx; }
void trap_S_StartSound( vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx ) {
	(void)origin; (void)entityNum; (void)entchannel; (void)sfx;
}
void trap_S_StartLocalSound( sfxHandle_t sfx, int channelNum ) { (void)sfx; (void)channelNum; }
void trap_S_StopLoopingSound( int entityNum ) { (void)entityNum; }
sfxHandle_t trap_S_RegisterSound( const char *sample, qboolean compressed ) { (void)sample; (void)compressed; return 0; }
void CG_SpawnEffect( vec3_t org ) { (void)org; }
void CG_ScorePlum( int client, vec3_t org, int score ) { (void)client; (void)org; (void)score; }
void CG_GibPlayer( vec3_t playerOrigin ) { (void)playerOrigin; }
void CG_Beam( centity_t *cent ) { (void)cent; }
void CG_SetEntitySoundPosition( centity_t *cent ) { (void)cent; }
/* the view kick's hand-off to the client (cg_kick.c): bullets never read it */
void trap_SetUserCmdKickAngles( float pitch, float yaw, float roll ) { (void)pitch; (void)yaw; (void)roll; }
#ifdef MISSIONPACK
vmCvar_t cg_singlePlayerActive;
void CG_KamikazeEffect( vec3_t org ) { (void)org; }
void CG_ObeliskExplode( vec3_t org, int entityNum ) { (void)org; (void)entityNum; }
void CG_ObeliskPain( vec3_t org ) { (void)org; }
void CG_InvulnerabilityImpact( vec3_t org, vec3_t angles ) { (void)org; (void)angles; }
void CG_InvulnerabilityJuiced( vec3_t org ) { (void)org; }
void CG_LightningBoltBeam( vec3_t start, vec3_t end ) { (void)start; (void)end; }
void CG_VoiceChatLocal( int mode, qboolean voiceOnly, int clientNum, int color, const char *cmd ) {
	(void)mode; (void)voiceOnly; (void)clientNum; (void)color; (void)cmd;
}
#endif
/* CG_RegisterWeapon's (a fire event may be the first sight of a gun): every
 * name a handle, no optional file there */
qhandle_t trap_R_RegisterModel( const char *name ) { (void)name; return 1; }
qhandle_t trap_R_RegisterShader( const char *name ) { (void)name; return 1; }
void trap_R_ModelBounds( clipHandle_t model, vec3_t mins, vec3_t maxs ) { (void)model; VectorClear( mins ); VectorClear( maxs ); }
void CG_WeapAnim_RegisterClips( int weapon, qhandle_t model, const char *path ) { (void)weapon; (void)model; (void)path; }
void CG_Sights_Register( int weapon, const char *path ) { (void)weapon; (void)path; }
int trap_FS_FOpenFile( const char *path, fileHandle_t *f, fsMode_t mode ) { (void)path; (void)mode; *f = 0; return -1; }
void trap_FS_FCloseFile( fileHandle_t f ) { (void)f; }
void trap_R_AddRefEntityToScene( const refEntity_t *re ) { (void)re; }
/* no mover carries anyone here */
void CG_AdjustPositionForMover( const vec3_t in, int moverNum, int fromTime, int toTime, vec3_t out,
	vec3_t angles_in, vec3_t angles_out ) {
	(void)moverNum; (void)fromTime; (void)toTime;
	VectorCopy( in, out ); VectorCopy( angles_in, angles_out );
}
/* the viewmodel's own run (cg_weappicture.c) is picture only */
void CG_WeapPicture_Reset( void ) { }
void CG_WeapPicture_BeginReplay( const playerState_t *base ) { (void)base; }
void CG_WeapPicture_ReplayStep( const playerState_t *before, const playerState_t *after, const entityState_t *pickup ) {
	(void)before; (void)after; (void)pickup;
}
void CG_WeapPicture_Update( const playerState_t *predicted, const pmove_t *model, int firstCmd, int lastCmd,
	qboolean (*readCmd)( int cmdNumber, usercmd_t *cmd ) ) {
	(void)predicted; (void)model; (void)firstCmd; (void)lastCmd; (void)readCmd;
}

/* ---- the client's command buffer ---- */
static usercmd_t cmds[CMD_BACKUP];
static int cmdNumber;
int trap_GetCurrentCmdNumber( void ) { return cmdNumber; }
qboolean trap_GetUserCmd( int n, usercmd_t *cmd ) { *cmd = cmds[n & CMD_MASK]; return n <= cmdNumber && n > cmdNumber - CMD_BACKUP; }
void BC_StoreCommand( const usercmd_t *cmd ) { cmds[++cmdNumber & CMD_MASK] = *cmd; }

/* ---- collision: the world, and the one temp box entities trace against ---- */
static vec3_t boxMins, boxMaxs;
void trap_CM_BoxTrace( trace_t *t, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs,
	clipHandle_t model, int mask ) {
	(void)model;
	BW_TraceWorld( t, start, mins, maxs, end, mask );
	BW_Finish( t, start, end );
}
clipHandle_t trap_CM_TempBoxModel( const vec3_t mins, const vec3_t maxs ) {
	VectorCopy( mins, boxMins ); VectorCopy( maxs, boxMaxs ); return 255;
}
clipHandle_t trap_CM_InlineModel( int index ) { (void)index; return 0; }
void trap_CM_TransformedBoxTrace( trace_t *t, const vec3_t start, const vec3_t end, const vec3_t mins,
	const vec3_t maxs, clipHandle_t model, int mask, const vec3_t origin, const vec3_t angles ) {
	vec3_t lo, hi;
	(void)angles;
	memset( t, 0, sizeof( *t ) ); t->fraction = 1; t->entityNum = ENTITYNUM_NONE;
	if ( model == 255 && ( mask & CONTENTS_BODY ) ) {
		VectorAdd( origin, boxMins, lo ); VectorAdd( origin, boxMaxs, hi );
		BW_Sweep( t, start, end, mins, maxs, lo, hi, ENTITYNUM_NONE, CONTENTS_BODY, 0 );
	}
	BW_Finish( t, start, end );
}
int trap_CM_PointContents( const vec3_t p, clipHandle_t model ) { (void)model; return BW_PointContents( p ); }
int trap_CM_TransformedPointContents( const vec3_t p, clipHandle_t model, const vec3_t origin, const vec3_t angles ) {
	(void)p; (void)model; (void)origin; (void)angles; return 0;
}

/* ---- what is drawn ---- */
bwMark_t bc_marks[BW_MAX_MARKS];
int bc_markCount, bc_rails, bc_shotgunSmokes, bc_tracers;
vec3_t bc_railEnd;
static localEntity_t localPool[4096];
static int localUsed;
static void Mark( const vec3_t point, int entityNum, int flesh ) {
	bwMark_t *m;
	if ( bc_markCount >= BW_MAX_MARKS ) return;
	m = &bc_marks[bc_markCount++];
	VectorCopy( point, m->point ); m->entityNum = entityNum; m->flesh = flesh;
}
void CG_Bleed( vec3_t origin, int entityNum ) { Mark( origin, entityNum, 1 ); }
void CG_ImpactMark( qhandle_t markShader, const vec3_t origin, const vec3_t dir, float orientation,
	float r, float g, float b, float a, qboolean alphaFade, float radius, qboolean temporary ) {
	(void)markShader; (void)dir; (void)orientation; (void)r; (void)g; (void)b; (void)a;
	(void)alphaFade; (void)radius; (void)temporary;
	Mark( origin, ENTITYNUM_WORLD, 0 );
}
localEntity_t *CG_AllocLocalEntity( void ) {
	localEntity_t *le = &localPool[localUsed++ & 4095];
	memset( le, 0, sizeof( *le ) );
	return le;
}
localEntity_t *CG_MakeExplosion( vec3_t origin, vec3_t dir, qhandle_t hModel, qhandle_t shader, int msec, qboolean isSprite ) {
	(void)origin; (void)dir; (void)hModel; (void)shader; (void)msec; (void)isSprite;
	return CG_AllocLocalEntity();
}
localEntity_t *CG_SmokePuff( const vec3_t p, const vec3_t vel, float radius, float r, float g, float b, float a,
	float duration, int startTime, int fadeInTime, int leFlags, qhandle_t hShader ) {
	(void)p; (void)vel; (void)radius; (void)r; (void)g; (void)b; (void)a; (void)duration; (void)startTime;
	(void)fadeInTime; (void)leFlags; (void)hShader;
	if ( hShader == cgs.media.shotgunSmokePuffShader ) bc_shotgunSmokes++;
	return CG_AllocLocalEntity();
}
void CG_BubbleTrail( vec3_t start, vec3_t end, float spacing ) { (void)start; (void)end; (void)spacing; }
void CG_ParticleExplosion( char *animStr, vec3_t origin, vec3_t vel, int duration, int sizeStart, int sizeEnd ) {
	(void)animStr; (void)origin; (void)vel; (void)duration; (void)sizeStart; (void)sizeEnd;
}
void trap_R_AddPolyToScene( qhandle_t shader, int numVerts, const polyVert_t *verts ) {
	(void)numVerts; (void)verts;
	if ( shader == cgs.media.tracerShader ) bc_tracers++;
}
void BC_ClearMarks( void ) { bc_markCount = 0; bc_rails = 0; bc_shotgunSmokes = 0; bc_tracers = 0; localUsed = 0; }
/* the rail's core (CG_RailTrail's first local entity, cg_oldRail 1 sets it 8 down) */
static void FindRails( int from ) {
	int i;
	for ( i = from; i < localUsed && i < 4096; i++ ) {
		if ( localPool[i].refEntity.reType == RT_RAIL_CORE ) {
			bc_rails++;
			VectorCopy( localPool[i].refEntity.oldorigin, bc_railEnd );
			bc_railEnd[2] += 8;
		}
	}
}

/* ---- snapshots and frames ---- */
static snapshot_t snaps[2];
static int snapCurrent;
static playerState_t previousSnapPs;
static qboolean haveSnap;

void BC_Reset( int clientNum ) {
	memset( &cg, 0, sizeof( cg ) ); memset( &cgs, 0, sizeof( cgs ) );
	memset( cg_entities, 0, sizeof( cg_entities ) ); memset( cg_weapons, 0, sizeof( cg_weapons ) );
	memset( cmds, 0, sizeof( cmds ) ); memset( snaps, 0, sizeof( snaps ) );
	cmdNumber = 0; haveSnap = qfalse;
	cg.clientNum = clientNum;
	cgs.glconfig.hardwareType = GLHW_GENERIC;
	cgs.media.tracerShader = 11; cgs.media.shotgunSmokePuffShader = 12;
	cgs.media.bulletMarkShader = 13; cgs.media.energyMarkShader = 14; cgs.media.holeMarkShader = 15;
	cg_tracerChance.value = 1; cg_tracerLength.value = 100; cg_tracerWidth.value = 1;
	cg_errorDecay.value = 100; cg_railTrailTime.value = 400; cg_oldRail.integer = 1; cg_drawGun.integer = 1;
	cg_blood.integer = 1;
	pmove_msec.integer = 8; pmove_fixed.integer = 0;
	BC_ClearMarks();
	{	/* as CG_Init leaves them: every gun registered */
		int w;
		for ( w = WP_GAUNTLET; w < WP_NUM_WEAPONS; w++ ) CG_RegisterWeapon( w );
	}
}

/* A new snapshot: CG_TransitionSnapshot's part this test needs. Each entity
 * takes the snapshot's state and its events are checked (CG_TransitionEntity);
 * a temp entity is new every time (the server frees it once sent). */
void BC_Snapshot( int serverTime, const playerState_t *ps, const entityState_t *ents, int count ) {
	snapshot_t *snap;
	int i, from = localUsed;

	if ( haveSnap ) previousSnapPs = cg.snap->ps;
	snapCurrent ^= 1;
	snap = &snaps[snapCurrent];
	memset( snap, 0, sizeof( *snap ) );
	snap->serverTime = serverTime; snap->ps = *ps; snap->numEntities = count;
	for ( i = 0; i < count; i++ ) snap->entities[i] = ents[i];
	cg.snap = snap; cg.nextSnap = NULL;
	if ( !haveSnap ) {	/* as the first frame's CG_AddPacketEntities leaves them */
		previousSnapPs = *ps;
		cg.predictedPlayerState = *ps;
		BG_PlayerStateToEntityState( &cg.predictedPlayerState, &cg.predictedPlayerEntity.currentState, qfalse );
	}
	haveSnap = qtrue;

	for ( i = 0; i < count; i++ ) {
		centity_t *cent = &cg_entities[ents[i].number];
		cent->currentState = cent->nextState = ents[i];
		cent->currentValid = qtrue;
		if ( ents[i].eType > ET_EVENTS ) cent->previousEvent = 0;
		VectorCopy( ents[i].pos.trBase, cent->lerpOrigin );
		VectorCopy( ents[i].apos.trBase, cent->lerpAngles );
		cent->snapShotTime = serverTime;
	}
	CG_BuildSolidList();
	for ( i = 0; i < count; i++ ) CG_CheckEvents( &cg_entities[ents[i].number] );
	FindRails( from );
}

/* A live frame: the real prediction fires the player's own events; then the
 * predicted entity is refreshed, as CG_AddPacketEntities does. */
void BC_PredictFrame( int time ) {
	int from = localUsed;
	cg.oldTime = cg.time; cg.time = time;
	cg.demoPlayback = qfalse;
	{ int before=cg.predictedPlayerState.eventSequence, marks=bc_markCount;
	CG_PredictPlayerState();
	if ( getenv( "BULLETS_TRACE" ) ) printf( "cl frame t=%d snap=%d seq %d->%d ct=%d marks+%d ev=%d,%d\n", time, cg.snap->ps.commandTime, before,
		cg.predictedPlayerState.eventSequence, cg.predictedPlayerState.commandTime, bc_markCount-marks,
		cg.predictedPlayerState.events[0], cg.predictedPlayerState.events[1] ); }
	BG_PlayerStateToEntityState( &cg.predictedPlayerState, &cg.predictedPlayerEntity.currentState, qfalse );
	FindRails( from );
}

/* Nothing predicted (a demo, a followed player, the killcam): the snapshot's
 * own playerstate events, as cg_snapshot.c plays them. */
void BC_UnpredictedFrame( int time ) {
	int from = localUsed;
	cg.oldTime = cg.time; cg.time = time;
	cg.demoPlayback = qtrue;
	cg.predictedPlayerState = cg.snap->ps;
	CG_TransitionPlayerState( &cg.snap->ps, &previousSnapPs );
	BG_PlayerStateToEntityState( &cg.predictedPlayerState, &cg.predictedPlayerEntity.currentState, qfalse );
	FindRails( from );
}
