/* The cgame half of the silent shot test (silent_shot_test.c): what
 * CG_DrawActiveFrame runs before it draws, through the real code - the
 * snapshot pipeline (CG_ProcessSnapshots, CG_TransitionSnapshot, the
 * killcam's cut in cg_killcam.c), prediction (CG_PredictPlayerState over the
 * client's own commands), the playerstate and entity events (cg_playerstate.c,
 * cg_event.c), CG_FireWeapon and its client-side bullets (cg_weapons.c), and
 * the view kick (cg_kick.c), which reaches the client engine's real
 * CL_SetUserCmdKickAngles. What a frame plays is read where it lands: the
 * events CG_EntityEvent names (cg_debugEvents), the sounds started, the
 * muzzle flashes, the marks, blood, tracers and smoke. Each other player's
 * third-person weapon is drawn through the real CG_AddPlayerWeapon, and the
 * flash it adds is read by its model, tag and light. Traces go against the
 * world both halves share (bullets_world.c). Only the renderer, the sound
 * system and the cgame modules outside these files are stubbed. */
#include "../../code/cgame/cg_snapshot.c"
#include "bullets_world.h"
#include "silent_shot.h"

void CL_SetUserCmdKickAngles( float pitch, float yaw, float roll );	/* cl_cgame.c's */

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
#ifdef MISSIONPACK
vmCvar_t cg_singlePlayerActive;
#endif

static ccFrame_t *frame;	/* what the frame in progress played */
static int gun;				/* the shooter's gun, whose fire sound is listened for */

/* ---- prints: CG_EntityEvent names each event it plays (cg_debugEvents 1) ---- */
static int eventEntity = -1;
void QDECL CG_Printf( const char *fmt, ... ) {
	va_list a;
	if ( !strcmp( fmt, "ent:%3i  event:%3i " ) ) {
		va_start( a, fmt ); eventEntity = va_arg( a, int ); va_end( a );
		return;
	}
	if ( !strcmp( fmt, "EV_FIRE_WEAPON\n" ) && frame && eventEntity >= 0 && eventEntity < SS_CLIENTS ) {
		/* the parm names the gun: the playerstate's slot or the entity ring's */
		const centity_t *cent = cg.eventPlayerState ? &cg.predictedPlayerEntity : &cg_entities[eventEntity];
		if ( cent->currentState.eventParm == gun ) {
			frame->gunFire[eventEntity]++;
			if ( cg.eventPlayerState ) frame->playerGunFire++;
		} else {
			int parm = cent->currentState.eventParm;
			if ( parm > WP_NONE && parm < WP_NUM_WEAPONS ) {
				CG_RegisterWeapon( parm );	/* as CG_FireWeapon is about to */
				if ( cg_weapons[parm].flashModel ) frame->otherFlashFire[eventEntity]++;
			}
			if ( parm == WP_THROWING_KNIFE ) frame->knifeThrows++;
		}
	}
	if ( !strncmp( fmt, "Event %s just for client", 24 ) ) {
		fprintf( stderr, "an event for its own client reached another\n" ); abort();
	}
}
void QDECL CG_Error( const char *fmt, ... ) {
	va_list a; va_start( a, fmt ); vfprintf( stderr, fmt, a ); va_end( a ); fputc( '\n', stderr ); abort();
}

/* ---- sounds: each name its own handle; what starts is counted ---- */
static char soundNames[256][MAX_QPATH];
static int numSoundNames;
sfxHandle_t trap_S_RegisterSound( const char *name, qboolean compressed ) {
	int i; (void)compressed;
	for ( i = 1; i <= numSoundNames; i++ ) if ( !strcmp( soundNames[i], name ) ) return i;
	if ( numSoundNames + 1 >= ARRAY_LEN( soundNames ) ) CG_Error( "too many sounds" );
	Q_strncpyz( soundNames[++numSoundNames], name, MAX_QPATH );
	return numSoundNames;
}
void trap_S_StartSound( vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx ) {
	int c;
	(void)origin; (void)entchannel;
	if ( !frame || !sfx || entityNum < 0 || entityNum >= SS_CLIENTS ) return;
	for ( c = 0; c < 4; c++ ) if ( sfx == cg_weapons[gun].flashSound[c] ) frame->gunSounds[entityNum]++;
}
void trap_S_StartLocalSound( sfxHandle_t sfx, int channelNum ) { (void)sfx; (void)channelNum; }
void trap_S_StopLoopingSound( int entityNum ) { (void)entityNum; }
void trap_S_ClearLoopingSounds( qboolean killall ) { (void)killall; }
void CG_AddBufferedSound( sfxHandle_t sfx ) { (void)sfx; }
sfxHandle_t CG_CustomSound( int clientNum, const char *soundName ) { (void)clientNum; return trap_S_RegisterSound( soundName, qfalse ); }
static char lastConsoleCommand[64];
void trap_SendConsoleCommand( const char *text ) { Q_strncpyz( lastConsoleCommand, text, sizeof( lastConsoleCommand ) ); }

/* ---- the view kick's hand-off: the client engine's own (cl_cgame.c) ---- */
void trap_SetUserCmdKickAngles( float pitch, float yaw, float roll ) {
	CL_SetUserCmdKickAngles( pitch, yaw, roll );
}

/* ---- the rest of the cgame and the engine these files call ---- */
const char *CG_ConfigString( int index ) { (void)index; return ""; }
void CG_CenterPrint( const char *str, int y, int charWidth ) { (void)str; (void)y; (void)charWidth; }
void CG_SpawnEffect( vec3_t org ) { (void)org; }
void CG_ScorePlum( int client, vec3_t org, int score ) { (void)client; (void)org; (void)score; }
void CG_GibPlayer( vec3_t playerOrigin ) { (void)playerOrigin; }
void CG_Beam( centity_t *cent ) { (void)cent; }
void CG_SetEntitySoundPosition( centity_t *cent ) { (void)cent; }
void CG_ExecuteNewServerCommands( int latestSequence ) { (void)latestSequence; }
void CG_AddLagometerSnapshotInfo( snapshot_t *snap ) { (void)snap; }
void CG_ResetPlayerEntity( centity_t *cent ) { (void)cent; }
/* the killcam's cut clears these presentation layers (cg_killcam.c) */
void CG_InitLocalEntities( void ) { }
void CG_InitMarkPolys( void ) { }
void CG_ClearParticles( void ) { }
void CG_WeapAnim_Reset( void ) { }
void CG_DrawBigString( int x, int y, const char *s, float alpha ) { (void)x; (void)y; (void)s; (void)alpha; }
#ifdef MISSIONPACK
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
/* CG_RegisterWeapon's: every model and shader a handle, no optional file
 * there. Each model name has its own handle, so a drawn model says what it is. */
static char modelNames[512][MAX_QPATH];
static int numModelNames;
qhandle_t trap_R_RegisterModel( const char *name ) {
	int i;
	for ( i = 1; i <= numModelNames; i++ ) if ( !strcmp( modelNames[i], name ) ) return i;
	if ( numModelNames + 1 >= ARRAY_LEN( modelNames ) ) CG_Error( "too many models" );
	Q_strncpyz( modelNames[++numModelNames], name, MAX_QPATH );
	return numModelNames;
}
qhandle_t trap_R_RegisterShader( const char *name ) { (void)name; return 1; }
void trap_R_ModelBounds( clipHandle_t model, vec3_t mins, vec3_t maxs ) { (void)model; VectorClear( mins ); VectorClear( maxs ); }
void CG_WeapAnim_RegisterClips( int weapon, qhandle_t model, const char *path ) { (void)weapon; (void)model; (void)path; }
void CG_Sights_Register( int weapon, const char *path ) { (void)weapon; (void)path; }
int trap_FS_FOpenFile( const char *path, fileHandle_t *f, fsMode_t mode ) { (void)path; (void)mode; *f = 0; return -1; }
void trap_FS_FCloseFile( fileHandle_t f ) { (void)f; }
/* ---- the third-person weapon: what CG_AddPlayerWeapon adds for an entity ---- */
static int drawing = -1;	/* the entity whose weapon is being added */
static int flashTagModel;	/* the model the last tag_flash was looked up on */
static int WeaponOfFlash( qhandle_t model ) {
	int w;
	for ( w = WP_GAUNTLET; w < WP_NUM_WEAPONS; w++ ) if ( model && cg_weapons[w].flashModel == model ) return w;
	return 0;
}
void trap_R_AddRefEntityToScene( const refEntity_t *re ) {
	int w;
	if ( !frame || drawing < 0 || !( w = WeaponOfFlash( re->hModel ) ) ) return;
	frame->flashDrawn[drawing] = w;
	for ( frame->flashTag[drawing] = 0, w = WP_GAUNTLET; w < WP_NUM_WEAPONS; w++ ) {
		if ( flashTagModel && cg_weapons[w].weaponModel == flashTagModel ) { frame->flashTag[drawing] = w; break; }
	}
}
void trap_R_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {
	int w;
	(void)org; (void)intensity;
	if ( !frame || drawing < 0 ) return;
	for ( w = WP_GAUNTLET; w < WP_NUM_WEAPONS; w++ ) {
		const float *c = cg_weapons[w].flashDlightColor;
		if ( ( c[0] || c[1] || c[2] ) && c[0] == r && c[1] == g && c[2] == b && w == frame->flashDrawn[drawing] ) frame->flashLight[drawing] = w;
	}
}
int trap_R_LerpTag( orientation_t *tag, clipHandle_t mod, int startFrame, int endFrame, float frac, const char *tagName ) {
	(void)mod; (void)startFrame; (void)endFrame; (void)frac; (void)tagName;
	VectorClear( tag->origin ); AxisClear( tag->axis );
	return qtrue;
}
/* cg_ents.c's, which this test does not link: the tag lookup is what is read */
void CG_PositionRotatedEntityOnTag( refEntity_t *entity, const refEntity_t *parent, qhandle_t parentModel, char *tagName ) {
	if ( !strcmp( tagName, "tag_flash" ) ) flashTagModel = parentModel;
	VectorCopy( parent->origin, entity->origin );
}
void trap_S_AddLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx ) {
	(void)entityNum; (void)origin; (void)velocity; (void)sfx;
}
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

/* ---- the client's commands (CL_CreateNewCommands' buffer) ---- */
static usercmd_t cmds[CMD_BACKUP];
static int cmdNumber;
int trap_GetCurrentCmdNumber( void ) { return cmdNumber; }
qboolean trap_GetUserCmd( int n, usercmd_t *cmd ) {
	*cmd = cmds[n & CMD_MASK]; return n <= cmdNumber && n > cmdNumber - CMD_BACKUP;
}
void CC_StoreCommand( const usercmd_t *cmd ) { cmds[++cmdNumber & CMD_MASK] = *cmd; }

/* ---- the client's received snapshots (cl.snapshots), numbered from 1 ---- */
#define PACKET_BACKUP	32	/* qcommon.h's, which cgame does not see */
#define PACKET_MASK		( PACKET_BACKUP - 1 )
static ssSnapshot_t received[PACKET_BACKUP];
static int latestSnapshot;
void CC_Snapshot( const ssSnapshot_t *snap ) { received[++latestSnapshot & PACKET_MASK] = *snap; }
void trap_GetCurrentSnapshotNumber( int *snapshotNumber, int *serverTime ) {
	*snapshotNumber = latestSnapshot;
	*serverTime = latestSnapshot ? received[latestSnapshot & PACKET_MASK].serverTime : 0;
}
qboolean trap_GetSnapshot( int snapshotNumber, snapshot_t *snapshot ) {
	const ssSnapshot_t *in;
	int i;
	if ( snapshotNumber > latestSnapshot || snapshotNumber <= latestSnapshot - PACKET_BACKUP ) return qfalse;
	in = &received[snapshotNumber & PACKET_MASK];
	memset( snapshot, 0, sizeof( *snapshot ) );
	snapshot->snapFlags = in->snapFlags;
	snapshot->serverTime = in->serverTime;
	memset( snapshot->areamask, 0xff, sizeof( snapshot->areamask ) );
	snapshot->ps = in->ps;
	snapshot->numEntities = in->numEntities;
	for ( i = 0; i < in->numEntities; i++ ) snapshot->entities[i] = in->entities[i];
	return qtrue;
}

/* ---- collision: the world, and the temp box entities trace against ---- */
static vec3_t boxMins, boxMaxs;
void trap_CM_BoxTrace( trace_t *t, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs,
	clipHandle_t model, int mask ) {
	(void)model;
	BW_TraceWorld( t, start, mins, maxs, end, mask );
	BW_Finish( t, start, end );
	if ( getenv( "SILENTSHOT_CLTRACE" ) && !mins && mask == MASK_SHOT )
		printf( "    cl world ray (%.1f %.1f %.1f)->(%.1f %.1f %.1f) frac %.4f\n", start[0], start[1], start[2], end[0], end[1], end[2], t->fraction );
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
	if ( getenv( "SILENTSHOT_CLTRACE" ) && !mins )
		printf( "    cl box at (%.1f %.1f %.1f) [%.0f..%.0f] mask %x frac %.4f\n", origin[0], origin[1], origin[2], boxMins[2], boxMaxs[2], mask, t->fraction );
}
int trap_CM_PointContents( const vec3_t p, clipHandle_t model ) { (void)model; return BW_PointContents( p ); }
int trap_CM_TransformedPointContents( const vec3_t p, clipHandle_t model, const vec3_t origin, const vec3_t angles ) {
	(void)p; (void)model; (void)origin; (void)angles; return 0;
}

/* ---- what is drawn: impacts, blood, tracers, smoke ---- */
static localEntity_t localPool[1024];
static int localUsed;
void CG_Bleed( vec3_t origin, int entityNum ) {
	(void)origin;
	if ( frame ) { frame->impacts++; frame->fleshImpacts++; }
	if ( frame && entityNum == cg.predictedPlayerState.clientNum ) frame->viewerBleeds++;
}
void CG_ImpactMark( qhandle_t markShader, const vec3_t origin, const vec3_t dir, float orientation,
	float r, float g, float b, float a, qboolean alphaFade, float radius, qboolean temporary ) {
	(void)markShader; (void)origin; (void)dir; (void)orientation; (void)r; (void)g; (void)b; (void)a;
	(void)alphaFade; (void)radius; (void)temporary;
	if ( frame ) frame->impacts++;
}
localEntity_t *CG_AllocLocalEntity( void ) {
	localEntity_t *le = &localPool[localUsed++ & ( ARRAY_LEN( localPool ) - 1 )];
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
	(void)fadeInTime; (void)leFlags;
	if ( frame && hShader == cgs.media.shotgunSmokePuffShader ) frame->smokes++;
	return CG_AllocLocalEntity();
}
void CG_BubbleTrail( vec3_t start, vec3_t end, float spacing ) { (void)start; (void)end; (void)spacing; }
void CG_ParticleExplosion( char *animStr, vec3_t origin, vec3_t vel, int duration, int sizeStart, int sizeEnd ) {
	(void)animStr; (void)origin; (void)vel; (void)duration; (void)sizeStart; (void)sizeEnd;
}
void trap_R_AddPolyToScene( qhandle_t shader, int numVerts, const polyVert_t *verts ) {
	(void)numVerts; (void)verts;
	if ( frame && shader == cgs.media.tracerShader ) frame->tracers++;
}

/* ---- where entities are drawn: CG_AddPacketEntities' positions ----
 * cg_ents.c's CG_CalcEntityLerpPositions and CG_InterpolateEntityPosition, as
 * they are (static there, and cg_ents.c is the renderer's way in): the
 * players' boxes the next frame's prediction and shot traces collide with. */
static void InterpolateEntityPosition( centity_t *cent ) {
	vec3_t current, next;
	float f = cg.frameInterpolation;
	if ( cg.nextSnap == NULL ) CG_Error( "CG_InterpoateEntityPosition: cg.nextSnap == NULL" );
	BG_EvaluateTrajectory( &cent->currentState.pos, cg.snap->serverTime, current );
	BG_EvaluateTrajectory( &cent->nextState.pos, cg.nextSnap->serverTime, next );
	cent->lerpOrigin[0] = current[0] + f * ( next[0] - current[0] );
	cent->lerpOrigin[1] = current[1] + f * ( next[1] - current[1] );
	cent->lerpOrigin[2] = current[2] + f * ( next[2] - current[2] );
	BG_EvaluateTrajectory( &cent->currentState.apos, cg.snap->serverTime, current );
	BG_EvaluateTrajectory( &cent->nextState.apos, cg.nextSnap->serverTime, next );
	cent->lerpAngles[0] = LerpAngle( current[0], next[0], f );
	cent->lerpAngles[1] = LerpAngle( current[1], next[1], f );
	cent->lerpAngles[2] = LerpAngle( current[2], next[2], f );
}
static void CalcEntityLerpPositions( centity_t *cent ) {
	if ( !cg_smoothClients.integer && cent->currentState.number < MAX_CLIENTS ) {
		cent->currentState.pos.trType = TR_INTERPOLATE;
		cent->nextState.pos.trType = TR_INTERPOLATE;
	}
	if ( cent->interpolate && ( cent->currentState.pos.trType == TR_INTERPOLATE ||
	     ( cent->currentState.pos.trType == TR_LINEAR_STOP && cent->currentState.number < MAX_CLIENTS ) ) ) {
		InterpolateEntityPosition( cent );
		return;
	}
	BG_EvaluateTrajectory( &cent->currentState.pos, cg.time, cent->lerpOrigin );
	BG_EvaluateTrajectory( &cent->currentState.apos, cg.time, cent->lerpAngles );
}
static void AddPacketEntities( void ) {
	int num;
	if ( cg.nextSnap ) {
		int delta = cg.nextSnap->serverTime - cg.snap->serverTime;
		cg.frameInterpolation = delta ? (float)( cg.time - cg.snap->serverTime ) / delta : 0;
	} else {
		cg.frameInterpolation = 0;
	}
	BG_PlayerStateToEntityState( &cg.predictedPlayerState, &cg.predictedPlayerEntity.currentState, qfalse );
	CalcEntityLerpPositions( &cg.predictedPlayerEntity );
	CalcEntityLerpPositions( &cg_entities[cg.snap->ps.clientNum] );
	for ( num = 0; num < cg.snap->numEntities; num++ ) CalcEntityLerpPositions( &cg_entities[cg.snap->entities[num].number] );
}

/* ---- the client, as the test drives it ---- */
void CC_Reset( int clientNum, int shooterGun ) {
	int w;
	memset( &cg, 0, sizeof( cg ) ); memset( &cgs, 0, sizeof( cgs ) );
	memset( cg_entities, 0, sizeof( cg_entities ) ); memset( cg_weapons, 0, sizeof( cg_weapons ) );
	memset( cmds, 0, sizeof( cmds ) ); memset( received, 0, sizeof( received ) );
	cmdNumber = latestSnapshot = 0; localUsed = 0; frame = NULL; gun = shooterGun;
	numModelNames = 0; drawing = -1;
	lastConsoleCommand[0] = 0;
	cg.clientNum = clientNum;
	cgs.gametype = GT_SND; cgs.maxclients = SS_CLIENTS;
	cgs.glconfig.hardwareType = GLHW_GENERIC;
	cgs.media.tracerShader = 11; cgs.media.shotgunSmokePuffShader = 12;
	cgs.media.bulletMarkShader = 13; cgs.media.energyMarkShader = 14; cgs.media.holeMarkShader = 15;
	cg_tracerChance.value = 1; cg_tracerLength.value = 100; cg_tracerWidth.value = 1;
	cg_errorDecay.value = 100; cg_railTrailTime.value = 400; cg_oldRail.integer = 1; cg_drawGun.integer = 1;
	cg_blood.integer = 1; cg_brassTime.integer = 0; cg_debugEvents.integer = 1; cg_smoothClients.integer = 0;
	cg_kickSeed = 0x89abcdefu;	/* every run's first shot draws the same kick */
	for ( w = WP_GAUNTLET; w < WP_NUM_WEAPONS; w++ ) CG_RegisterWeapon( w );	/* as CG_Init leaves them */
}

int CC_WeaponCommand( void ) {
	return cg.snap ? BG_SlotCommand( &cg.snap->ps, cg.weaponSelectSlot ) : BG_SLOT_COMMAND + cg.weaponSelectSlot;
}

void CC_Frame( int time, ccFrame_t *out ) {
	int flashBefore[SS_CLIENTS], playerFlashBefore, i;
	memset( out, 0, sizeof( *out ) );
	frame = out;
	for ( i = 0; i < SS_CLIENTS; i++ ) flashBefore[i] = cg_entities[i].muzzleFlashTime;
	playerFlashBefore = cg.predictedPlayerEntity.muzzleFlashTime;

	/* CG_DrawActiveFrame, up to building the scene */
	cg.time = time;
	cg.demoPlayback = qfalse;
	CG_ProcessSnapshots();
	if ( cg.snap ) {
		if ( !CG_Killcam_Active() ) CG_ValidateWeaponSelect();
		CG_PredictPlayerState();
		CG_KickAngles();
		/* CG_AddPacketEntities: the predicted entity, which the next frame's
		 * events play on, and every entity's place */
		AddPacketEntities();
		cg.frametime = cg.time - cg.oldTime;
		cg.oldTime = cg.time;
	}

	for ( i = 0; i < SS_CLIENTS; i++ ) {
		if ( cg_entities[i].muzzleFlashTime != flashBefore[i] && cg_entities[i].muzzleFlashTime == cg.time ) out->flashes++;
	}
	/* CG_Player's weapon for every other player in the snapshot, as the scene adds it */
	if ( cg.snap ) {
		int n;
		for ( n = 0; n < cg.snap->numEntities; n++ ) {
			centity_t *cent = &cg_entities[cg.snap->entities[n].number];
			refEntity_t torso;
			if ( cent->currentState.eType != ET_PLAYER || cent->currentState.number >= SS_CLIENTS ) continue;
			memset( &torso, 0, sizeof( torso ) );
			torso.hModel = trap_R_RegisterModel( "models/players/sarge/upper.md3" );
			AxisClear( torso.axis );
			VectorCopy( cent->lerpOrigin, torso.origin );
			drawing = cent->currentState.number; flashTagModel = 0;
			CG_AddPlayerWeapon( &torso, NULL, cent, TEAM_FREE );
			if ( out->flashDrawn[drawing] ) out->flashHeld[drawing] = cent->currentState.weapon;
			drawing = -1;
		}
	}
	out->playerFlash = cg.predictedPlayerEntity.muzzleFlashTime != playerFlashBefore &&
		cg.predictedPlayerEntity.muzzleFlashTime == cg.time;
	out->killcam = CG_Killcam_Active();
	VectorCopy( cg.kickAngles, out->kickAngles );
	VectorCopy( cg.kickAVel, out->kickAVel );
	if ( cg.snap ) { out->snapPs = cg.snap->ps; out->snapServerTime = cg.snap->serverTime; }
	out->predictedPs = cg.predictedPlayerState;
	frame = NULL;
}
