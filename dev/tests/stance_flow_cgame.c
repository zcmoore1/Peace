/* The cgame half of the stance integration test (stance_flow_test.c): the real
 * CG_PredictPlayerState (cg_predict.c, its respawn reset included) over the
 * real Pmove, the real CG_TransitionPlayerState and CG_CheckPlayerstateEvents
 * (cg_playerstate.c), the real CG_EntityEvent with its stance handlers and
 * CG_SetInvalidCmdHint (cg_event.c), and the real CG_DrawInvalidCmdHint
 * (cg_draw.c). Its commands to the client go through trap_SendConsoleCommand
 * to the client's real command buffer (stance_flow_client.c); its command
 * reads come from the client's commands. Only the renderer, sounds and the
 * rest of the cgame are stubbed. */
#include "../../code/cgame/cg_local.h"

cg_t cg;
cgs_t cgs;
centity_t cg_entities[MAX_GENTITIES];
weaponInfo_t cg_weapons[MAX_WEAPONS];
itemInfo_t cg_items[MAX_ITEMS];
vmCvar_t cg_showmiss, cg_nopredict, cg_synchronousClients, cg_errorDecay, cg_predictItems, pmove_fixed, pmove_msec,
	cg_debugEvents, cg_drawGun, cg_footsteps, cg_thirdPerson;
#ifdef MISSIONPACK
vmCvar_t cg_cameraOrbit, cg_singlePlayerActive;
#endif

/* the client (stance_flow_client.c) and the world (stance_flow_test.c) */
void FlowClientCommand( const char *text );
int FlowCurrentCmdNumber( void );
qboolean FlowGetUserCmd( int cmdNumber, usercmd_t *ucmd );
void FlowWorldTrace( trace_t *t, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int mask );

void QDECL CG_Printf( const char *fmt, ... ) { (void)fmt; }
void QDECL CG_Error( const char *fmt, ... ) { va_list a; va_start( a, fmt ); vfprintf( stderr, fmt, a ); va_end( a ); exit( 1 ); }
static int sentCommands;
void trap_SendConsoleCommand( const char *text ) { sentCommands++; FlowClientCommand( text ); }
int trap_GetCurrentCmdNumber( void ) { return FlowCurrentCmdNumber(); }
qboolean trap_GetUserCmd( int n, usercmd_t *cmd ) { return FlowGetUserCmd( n, cmd ); }
void trap_Cvar_Set( const char *name, const char *value ) { (void)name; (void)value; }
void trap_Cvar_Update( vmCvar_t *v ) { (void)v; }

/* the world, as CG_Trace's engine traps see it: no entity models here */
void trap_CM_BoxTrace( trace_t *t, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs,
	clipHandle_t model, int mask ) {
	(void)model; FlowWorldTrace( t, start, mins, maxs, end, mask );
}
void trap_CM_TransformedBoxTrace( trace_t *t, const vec3_t start, const vec3_t end, const vec3_t mins,
	const vec3_t maxs, clipHandle_t model, int mask, const vec3_t origin, const vec3_t angles ) {
	(void)start; (void)mins; (void)maxs; (void)model; (void)mask; (void)origin; (void)angles;
	memset( t, 0, sizeof( *t ) ); t->fraction = 1; VectorCopy( end, t->endpos );
}
clipHandle_t trap_CM_TempBoxModel( const vec3_t mins, const vec3_t maxs ) { (void)mins; (void)maxs; return 0; }
clipHandle_t trap_CM_InlineModel( int index ) { (void)index; return 0; }
int trap_CM_PointContents( const vec3_t p, clipHandle_t model ) { (void)p; (void)model; return 0; }
int trap_CM_TransformedPointContents( const vec3_t p, clipHandle_t model, const vec3_t origin, const vec3_t angles ) {
	(void)p; (void)model; (void)origin; (void)angles; return 0;
}

/* ---- the rest of the cgame, not under test ---- */
qboolean CG_Killcam_Active( void ) { return qfalse; }
void CG_RecordShots( const playerState_t *before, const playerState_t *after ) { (void)before; (void)after; }
void CG_AdjustPositionForMover( const vec3_t in, int moverNum, int fromTime, int toTime, vec3_t out,
	vec3_t angles_in, vec3_t angles_out ) {
	(void)moverNum; (void)fromTime; (void)toTime; VectorCopy( in, out ); VectorCopy( angles_in, angles_out );
}
void CG_PlayerHitVolumesTrace( trace_t *tr, const vec3_t start, const vec3_t end, int entityNum,
	const entityState_t *es, const vec3_t origin, const vec3_t mins, const vec3_t maxs ) {
	(void)tr; (void)start; (void)end; (void)entityNum; (void)es; (void)origin; (void)mins; (void)maxs; abort();
}
void CG_WeapPicture_Reset( void ) { }
void CG_WeapPicture_BeginReplay( const playerState_t *base ) { (void)base; }
void CG_WeapPicture_ReplayStep( const playerState_t *before, const playerState_t *after, const entityState_t *pickup ) {
	(void)before; (void)after; (void)pickup;
}
void CG_WeapPicture_Update( const playerState_t *predicted, const pmove_t *model, int firstCmd, int lastCmd,
	qboolean (*readCmd)( int cmdNumber, usercmd_t *cmd ) ) {
	(void)predicted; (void)model; (void)firstCmd; (void)lastCmd; (void)readCmd;
}
void CG_FireWeapon( centity_t *cent, int weapon ) { (void)cent; (void)weapon; }
void CG_PrepOffhand( centity_t *cent, int weapon ) { (void)cent; (void)weapon; }
void CG_KickFire( void ) { }
void CG_AddBufferedSound( sfxHandle_t sfx ) { (void)sfx; }
void CG_OutOfAmmoChange( void ) { }
void trap_S_StartSound( vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx ) {
	(void)origin; (void)entityNum; (void)entchannel; (void)sfx;
}
void trap_S_StartLocalSound( sfxHandle_t sfx, int channelNum ) { (void)sfx; (void)channelNum; }
sfxHandle_t CG_CustomSound( int clientNum, const char *soundName ) { (void)clientNum; (void)soundName; return 0; }
void CG_ViewedPlayerChanged( void ) { }
/* what CG_EntityEvent names for events this test never raises: reaching one fails it */
#define NOT_HERE( name ) { fprintf( stderr, #name " is not part of this test\n" ); exit( 1 ); }
void CG_Beam( centity_t *cent ) NOT_HERE( CG_Beam )
void CG_Bullet( vec3_t end, int sourceEntityNum, vec3_t normal, qboolean flesh, int fleshEntityNum ) NOT_HERE( CG_Bullet )
const char *CG_ConfigString( int index ) { (void)index; NOT_HERE( CG_ConfigString ) }
void CG_GibPlayer( vec3_t playerOrigin ) NOT_HERE( CG_GibPlayer )
void CG_MissileHitPlayer( int weapon, vec3_t origin, vec3_t dir, int entityNum ) NOT_HERE( CG_MissileHitPlayer )
void CG_MissileHitWall( int weapon, int clientNum, vec3_t origin, vec3_t dir, impactSound_t soundType ) NOT_HERE( CG_MissileHitWall )
void CG_RailTrail( clientInfo_t *ci, vec3_t start, vec3_t end ) NOT_HERE( CG_RailTrail )
void CG_ScorePlum( int client, vec3_t org, int score ) NOT_HERE( CG_ScorePlum )
void CG_ShotgunFire( entityState_t *es ) NOT_HERE( CG_ShotgunFire )
localEntity_t *CG_SmokePuff( const vec3_t p, const vec3_t vel, float radius, float r, float g, float b, float a,
	float duration, int startTime, int fadeInTime, int leFlags, qhandle_t hShader ) NOT_HERE( CG_SmokePuff )
void CG_SpawnEffect( vec3_t org ) NOT_HERE( CG_SpawnEffect )
sfxHandle_t trap_S_RegisterSound( const char *sample, qboolean compressed ) { (void)sample; (void)compressed; return 0; }
void trap_S_StopLoopingSound( int entityNum ) { (void)entityNum; }
#ifdef MISSIONPACK
void CG_KamikazeEffect( vec3_t org ) NOT_HERE( CG_KamikazeEffect )
void CG_ObeliskExplode( vec3_t org, int entityNum ) NOT_HERE( CG_ObeliskExplode )
void CG_ObeliskPain( vec3_t org ) NOT_HERE( CG_ObeliskPain )
void CG_InvulnerabilityImpact( vec3_t org, vec3_t angles ) NOT_HERE( CG_InvulnerabilityImpact )
void CG_InvulnerabilityJuiced( vec3_t org ) NOT_HERE( CG_InvulnerabilityJuiced )
void CG_LightningBoltBeam( vec3_t start, vec3_t end ) NOT_HERE( CG_LightningBoltBeam )
void CG_VoiceChatLocal( int mode, qboolean voiceOnly, int clientNum, int color, const char *cmd ) NOT_HERE( CG_VoiceChatLocal )
#endif

/* ---- the 2D draw: the hint ---- */
static int hintDrawn;
static float hintAlpha;
static char hintText[64];
int trap_Key_GetCatcher( void ) { return 0; }
int CG_DrawStrlen( const char *str ) { return (int)strlen( str ); }
void CG_DrawStringExt( int x, int y, const char *string, const float *setColor, qboolean forceColor, qboolean shadow,
	int charWidth, int charHeight, int maxChars ) {
	(void)x; (void)y; (void)forceColor; (void)shadow; (void)charWidth; (void)charHeight; (void)maxChars;
	hintDrawn++; hintAlpha = setColor[3]; Q_strncpyz( hintText, string, sizeof( hintText ) );
}

/* ---- what the test drives ---- */
static snapshot_t snap;
void FlowCgameInit( const playerState_t *ps, int time ) {
	memset( &cg, 0, sizeof( cg ) ); memset( &cgs, 0, sizeof( cgs ) ); memset( &snap, 0, sizeof( snap ) );
	pmove_msec.integer = 8; cg_errorDecay.value = 100;
	snap.ps = *ps; snap.serverTime = time;
	cg.snap = &snap; cg.clientNum = ps->clientNum; cg.time = time;
	cg.predictedPlayerState = *ps;
	cg.eventSequence = ps->eventSequence;
	BG_PlayerStateToEntityState( &cg.predictedPlayerState, &cg.predictedPlayerEntity.currentState, qfalse );
	sentCommands = 0;
}
/* A frame: the newest snapshot (the server's state before this frame's
 * command), the real prediction over the client's commands with its events
 * and respawn reset, the predicted entity as CG_AddPacketEntities makes it,
 * and the 2D draw's hint. */
void FlowCgameFrame( const playerState_t *serverPs, int serverTime, int time ) {
	snap.ps = *serverPs; snap.serverTime = serverTime;
	cg.oldTime = cg.time; cg.time = time;
	CG_PredictPlayerState();
	BG_PlayerStateToEntityState( &cg.predictedPlayerState, &cg.predictedPlayerEntity.currentState, qfalse );
	hintDrawn = 0; hintAlpha = -1;
	CG_DrawInvalidCmdHint();
}
const playerState_t *FlowPredicted( void ) { return &cg.predictedPlayerState; }
int FlowSentCommands( void ) { return sentCommands; }
int FlowHintType( void ) { return cg.invalidCmdHintType; }
int FlowHintMs( void ) { return CG_INVALID_HINT_MS; }
int FlowHintProneType( void ) { return CG_INVALID_HINT_PRONE_BLOCKED; }
int FlowHintStamp( void ) { return cg.invalidCmdHintTime; }
int FlowHintDrawn( float *alpha, const char **text ) { *alpha = hintAlpha; *text = hintText; return hintDrawn; }
