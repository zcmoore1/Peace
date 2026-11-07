/* The client-side bullets test's two halves, as bullets_test.c drives them.
 * bullets_server.c is built against g_local.h, bullets_client.c against
 * cg_local.h; only q_shared.h and bg_public.h types cross between them. */
#ifndef BULLETS_H
#define BULLETS_H

#define BS_CLIENTS		3		/* 0 shoots, 1 stands in the way, 2 watches from the side */
#define BS_START_TIME	10000
#define BS_HEALTH		100000	/* nobody dies of the test's shots */
#define BS_MAX_SNAPSHOT_ENTITIES	256	/* MAX_ENTITIES_IN_SNAPSHOT */

/* server: bullets_server.c */
void BS_Reset( int gametype );
void BS_Place( int n, float x, float y, float z );
void BS_Arm( int n, int weapon );
playerState_t *BS_PlayerState( int n );
int BS_Health( int n );
void BS_Command( int n, const usercmd_t *cmd );
void BS_EndFrame( void );
int BS_EffectsSent( void );
int BS_Missiles( void );
int BS_Snapshot( int viewer, entityState_t *out, int max );
void BS_FreeTempEntities( void );
void BS_ClearMarks( void );
extern bwMark_t bs_marks[BW_MAX_MARKS];
extern int bs_markCount, bs_shotTraces;

/* client: bullets_client.c, one cgame at a time */
void BC_Reset( int clientNum );
void BC_StoreCommand( const usercmd_t *cmd );
void BC_Snapshot( int serverTime, const playerState_t *ps, const entityState_t *ents, int count );
void BC_PredictFrame( int time );		/* the real CG_PredictPlayerState, then the entities' events */
void BC_UnpredictedFrame( int time );	/* a demo, a followed player or the killcam: the snapshot's own events */
void BC_ClearMarks( void );
extern bwMark_t bc_marks[BW_MAX_MARKS];
extern int bc_markCount, bc_rails, bc_shotgunSmokes, bc_tracers;
extern vec3_t bc_railEnd;

#endif
