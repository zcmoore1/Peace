/* The silent shot test's halves, as silent_shot_test.c drives them. Each half
 * is its own TU against its own headers: silent_shot_server.c against
 * g_local.h (the game module), silent_shot_engine.c against server.h and
 * qcommon.h (sv_snapshot.c and the network codec), silent_shot_client.c
 * against cg_local.h (the cgame), and kick_client.c against client.h
 * (CL_FinishMove). Only q_shared.h and bg_public.h types cross between them. */
#ifndef SILENT_SHOT_H
#define SILENT_SHOT_H

#define SS_SHOOTER		0	/* fires, then pulls the throwing knife */
#define SS_VICTIM		1	/* stands in the line of fire */
#define SS_WATCHER		2	/* an opponent off to the side: watches live, then the killcam */
#define SS_CLIENTS		3
#define SS_MAX_SNAP_ENTITIES	256	/* MAX_ENTITIES_IN_SNAPSHOT */

/* ---- server: silent_shot_server.c, the game module ---- */
typedef struct {
	int		shots;		/* EV_FIRE_WEAPON raised by this think, with the gun */
	int		presses;	/* EV_SWITCH_OFFHAND raised by this think */
	int		events;		/* playerstate events raised by this think */
	int		hits;		/* the think's shot traces that ended on something to be seen */
} ssThink_t;
void SS_Reset( int gun, int lethal, int levelTime );
void SS_Command( int client, const usercmd_t *cmd, ssThink_t *out );
void SS_RunFrame( int levelTime );			/* G_RunFrame's entity pass and ClientEndFrame */
int SS_Health( int client );
int SS_TempEffects( void );					/* impact/tracer temp entities alive now */
void SS_Follow( int client, int target );	/* a spectator following target (SpectatorClientEndFrame) */
void SS_Move( int client, float x, float y, float z );
void SS_OutOfRound( int client );			/* S&D's player out of the round, alone: no body */
/* for the engine half */
void *SS_Entities( int *numEntities, int *entitySize );
playerState_t *SS_PlayerState( int client );

/* ---- engine: silent_shot_engine.c, sv_snapshot.c and msg.c ---- */
typedef struct {
	int				serverTime, snapFlags, numEntities;
	playerState_t	ps;
	entityState_t	entities[SS_MAX_SNAP_ENTITIES];
} ssSnapshot_t;
void SE_Reset( void );
void SE_Frame( int svTime );				/* after the game's frame: SV_RecordReplayFrame */
int SE_ReplayStart( int client, int fromTime, int toTime );
void SE_Snapshot( int client, ssSnapshot_t *out );	/* SV_BuildClientSnapshot, through the codec */

/* ---- cgame: silent_shot_client.c ---- */
typedef struct {
	int		gunFire[SS_CLIENTS];	/* EV_FIRE_WEAPON naming the gun, played for each client's entity number */
	int		playerGunFire;		/* ... of them, from the playerstate (MW2's isPlayerView) */
	int		knifeThrows;		/* EV_FIRE_WEAPON naming the throwing knife */
	int		gunSounds[SS_CLIENTS];	/* the gun's fire sound started on each client's entity */
	int		flashes;			/* entities flashing this frame (muzzleFlashTime == cg.time) */
	int		playerFlash;		/* the predicted (or replayed) player's own muzzle flash */
	/* each other player's third-person weapon, drawn by the real CG_AddPlayerWeapon */
	int		flashDrawn[SS_CLIENTS];	/* the weapon whose flash model was drawn on them, 0 none */
	int		flashTag[SS_CLIENTS];	/* the weapon whose model's tag_flash placed it */
	int		flashLight[SS_CLIENTS];	/* the weapon whose flashDlightColor lit it */
	int		flashHeld[SS_CLIENTS];	/* the weapon their entity held while it was drawn */
	int		impacts, fleshImpacts, tracers, smokes;
	int		viewerBleeds;		/* blood drawn on the viewed player's own body */
	int		killcam;			/* cg.snap is a replay snapshot */
	vec3_t	kickAngles, kickAVel;
	playerState_t	snapPs, predictedPs;
	int		snapServerTime;
} ccFrame_t;
void CC_Reset( int clientNum, int gun );
void CC_StoreCommand( const usercmd_t *cmd );
void CC_Snapshot( const ssSnapshot_t *snap );
void CC_Frame( int time, ccFrame_t *out );	/* CG_DrawActiveFrame's part: snapshots, prediction, kick */
int CC_WeaponCommand( void );				/* what CG_DrawActiveFrame hands the client for cmd.weapon */
extern unsigned int cg_kickSeed;

/* ---- client engine: kick_client.c's real CL_FinishMove ---- */
void KickTestFinishMove( usercmd_t *cmd, int serverTime, const vec3_t view, int weapon );
void KickTestClientKick( vec3_t out );
void KickTestClientView( vec3_t out );
void KickTestClientReset( void );

#endif
