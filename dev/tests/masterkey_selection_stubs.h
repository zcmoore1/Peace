/* Unused rendering/sound boundaries needed by Windows linkers before dead-code
 * stripping. A selection test reaching any of them must fail immediately.
 * A test that links the real cg_event.c defines MASTERKEY_STUBS_REAL_EVENTS:
 * the four marked are then that file's handlers and the test's killcam. */
vmCvar_t cg_brassTime;
vmCvar_t cg_drawGun;
centity_t cg_entities[MAX_GENTITIES];
vmCvar_t cg_fov;
vmCvar_t cg_gun_frame;
vmCvar_t cg_gun_x;
vmCvar_t cg_gun_y;
vmCvar_t cg_gun_z;
itemInfo_t cg_items[MAX_ITEMS];
vmCvar_t cg_noProjectileTrail;
vmCvar_t cg_oldPlasma;
vmCvar_t cg_oldRail;
vmCvar_t cg_oldRocket;
vmCvar_t cg_railTrailTime;
vmCvar_t cg_showmiss;
vmCvar_t cg_tracerChance;
vmCvar_t cg_tracerLength;
vmCvar_t cg_tracerWidth;
vmCvar_t cg_trueLightning;
weaponInfo_t cg_weapons[MAX_WEAPONS];

void CG_AddBufferedSound( sfxHandle_t sfx) { abort(); }
localEntity_t *CG_AllocLocalEntity( void ) { abort(); }
void CG_Bleed( vec3_t origin, int entityNum ) { abort(); }
void CG_BubbleTrail( vec3_t start, vec3_t end, float spacing ) { abort(); }
void CG_DrawBigStringColor( int x, int y, const char *s, vec4_t color ) { abort(); }
void CG_DrawPic( float x, float y, float width, float height, qhandle_t hShader ) { abort(); }
int CG_DrawStrlen( const char *str ) { abort(); }
#ifndef MASTERKEY_STUBS_REAL_EVENTS
void CG_EntityEvent( centity_t *cent, vec3_t position ) { abort(); }
#endif
void QDECL CG_Error( const char *msg, ... ) { abort(); }	/* noreturn: cg_local.h's prototype */
#ifndef MASTERKEY_STUBS_REAL_EVENTS
qboolean CG_EventPlayerView( const centity_t *cent ) { abort(); }
#endif
float *CG_FadeColor( int startMsec, int totalMsec ) { abort(); }
void CG_ImpactMark( qhandle_t markShader, const vec3_t origin, const vec3_t dir, float orientation, float r, float g, float b, float a, qboolean alphaFade, float radius, qboolean temporary ) { abort(); }
#ifndef MASTERKEY_STUBS_REAL_EVENTS
qboolean CG_Killcam_Active( void ) { abort(); }
#endif
localEntity_t *CG_MakeExplosion( vec3_t origin, vec3_t dir, qhandle_t hModel, qhandle_t shader, int msec, qboolean isSprite ) { abort(); }
#ifndef MASTERKEY_STUBS_REAL_EVENTS
void CG_PainEvent( centity_t *cent, int health ) { abort(); }
#endif
void CG_ParticleExplosion (char *animStr, vec3_t origin, vec3_t vel, int duration, int sizeStart, int sizeEnd) { abort(); }
int CG_PointContents( const vec3_t point, int passEntityNum ) { abort(); }
void CG_PositionRotatedEntityOnTag( refEntity_t *entity, const refEntity_t *parent, qhandle_t parentModel, char *tagName ) { abort(); }
void QDECL CG_Printf( const char *msg, ... ) { abort(); }
void CG_Sights_AddOptic( int weapon, const refEntity_t *hand ) { abort(); }
void CG_Sights_Apply( int weapon, refEntity_t *hand ) { abort(); }
qboolean CG_Sights_HideViewModel(void) { abort(); }
void CG_Sights_Register( int weapon, const char *path ) { abort(); }
localEntity_t *CG_SmokePuff( const vec3_t p, const vec3_t vel, float radius, float r, float g, float b, float a, float duration, int startTime, int fadeInTime, int leFlags, qhandle_t hShader ) { abort(); }
void CG_Trace( trace_t *result, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int skipNumber, int mask ) { abort(); }
qboolean CG_WeapAnim_ActionFrame( const playerState_t *ps, refEntity_t *entity ) { abort(); }
qboolean CG_WeapAnim_Apply( playerState_t *ps, refEntity_t *entity, int msec ) { abort(); }
qboolean CG_WeapAnim_BuildPose( playerState_t *ps, qhandle_t hModel, int msec ) { abort(); }
qboolean CG_WeapAnim_HoldsDrop( void ) { abort(); }
float CG_WeapAnim_LadderLower( void ) { abort(); }
qboolean CG_WeapAnim_LadderStowed( const playerState_t *ps ) { abort(); }
void CG_WeapAnim_RegisterClips( int weapon, qhandle_t hModel, const char *cfgPath ) { abort(); }
qboolean CG_WeapPicture_Auto( const playerState_t *picture, const playerState_t *predicted ) { abort(); }
void trap_CM_BoxTrace( trace_t *results, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, clipHandle_t model, int brushmask ) { abort(); }
clipHandle_t trap_CM_TempBoxModel( const vec3_t mins, const vec3_t maxs ) { abort(); }
void trap_CM_TransformedBoxTrace( trace_t *results, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, clipHandle_t model, int brushmask, const vec3_t origin, const vec3_t angles ) { abort(); }
void trap_FS_FCloseFile( fileHandle_t f ) { abort(); }
int trap_FS_FOpenFile( const char *qpath, fileHandle_t *f, fsMode_t mode ) { abort(); }
void trap_R_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b ) { abort(); }
void trap_R_AddPolyToScene( qhandle_t hShader , int numVerts, const polyVert_t *verts ) { abort(); }
void trap_R_AddRefEntityToScene( const refEntity_t *re ) { abort(); }
int trap_R_LerpTag( orientation_t *tag, clipHandle_t mod, int startFrame, int endFrame, float frac, const char *tagName ) { abort(); }
void trap_R_ModelBounds( clipHandle_t model, vec3_t mins, vec3_t maxs ) { abort(); }
qhandle_t trap_R_RegisterModel( const char *name ) { abort(); }
qhandle_t trap_R_RegisterShader( const char *name ) { abort(); }
void trap_R_SetColor( const float *rgba ) { abort(); }
void trap_S_AddLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx ) { abort(); }
sfxHandle_t trap_S_RegisterSound( const char *sample, qboolean compressed ) { abort(); }
void trap_S_StartLocalSound( sfxHandle_t sfx, int channelNum ) { abort(); }
void trap_S_StartSound( vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx ) { abort(); }
void CG_ShotHitVolumes( trace_t *tr, const vec3_t start, const vec3_t end, int skipNumber ) { abort(); }
