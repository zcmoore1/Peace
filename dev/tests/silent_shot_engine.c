/* The engine's server half of the silent shot test (silent_shot_test.c): the
 * real sv_snapshot.c records the killcam after every game frame
 * (SV_RecordReplayFrame, as SV_Frame calls it after GAME_RUN_FRAME) and builds
 * every client's snapshot, live or replayed (SV_BuildClientSnapshot), and the
 * real network codec (msg.c) carries each snapshot's playerstate and entities
 * to the client, delta'd from what that client last received. The map has
 * one area and one cluster, so everyone sees everything; only that and the
 * game module's entity array (silent_shot_server.c) are supplied here. */
#include "../../code/server/sv_snapshot.c"
#include "silent_shot.h"

server_t sv;
serverStatic_t svs;
static cvar_t maxclients;
cvar_t *sv_maxclients = &maxclients;
cvar_t *cl_shownet;
static client_t clients[SS_CLIENTS];
static entityState_t packetEntities[4096];
static byte *gameEntities;
static int gameEntitySize;

void QDECL Com_Error( int code, const char *fmt, ... ) {
	va_list a; (void)code; va_start( a, fmt ); vfprintf( stderr, fmt, a ); va_end( a ); fputc( '\n', stderr ); abort();
}
void QDECL Com_Printf( const char *fmt, ... ) { (void)fmt; }
void QDECL Com_DPrintf( const char *fmt, ... ) { (void)fmt; }
sharedEntity_t *SV_GentityNum( int n ) { return (sharedEntity_t *)( gameEntities + n * gameEntitySize ); }
playerState_t *SV_GameClientNum( int n ) { return SS_PlayerState( n ); }
svEntity_t *SV_SvEntityForGentity( sharedEntity_t *e ) { return &sv.svEntities[e->s.number]; }
int CM_PointLeafnum( const vec3_t p ) { (void)p; return 0; }
int CM_LeafArea( int leaf ) { (void)leaf; return 0; }
int CM_LeafCluster( int leaf ) { (void)leaf; return 0; }
qboolean CM_AreasConnected( int a, int b ) { return a == b; }
byte *CM_ClusterPVS( int cluster ) { static byte pvs[1] = { 1 }; (void)cluster; return pvs; }
int CM_WriteAreaBits( byte *bits, int area ) { bits[0] |= 1 << area; return 1; }

/* what each client last received, which the next snapshot is delta'd from */
static struct {
	playerState_t	ps;
	entityState_t	entities[MAX_GENTITIES];
	qboolean		has[MAX_GENTITIES];
} received[SS_CLIENTS];

static void Sync( void ) {
	int i, n;
	gameEntities = SS_Entities( &n, &gameEntitySize );
	sv.num_entities = n;
	for ( i = 0; i < n; i++ ) sv.svEntities[i].numClusters = 1;	/* cluster 0, area 0 */
}

void SE_Reset( void ) {
	int i;
	SV_ReplayReset( qtrue );
	memset( &sv, 0, sizeof( sv ) ); memset( &svs, 0, sizeof( svs ) ); memset( clients, 0, sizeof( clients ) );
	memset( received, 0, sizeof( received ) );
	sv.state = SS_GAME; maxclients.integer = SS_CLIENTS;
	svs.clients = clients; svs.snapshotEntities = packetEntities;
	svs.numSnapshotEntities = ARRAY_LEN( packetEntities );
	Sync();
	for ( i = 0; i < SS_CLIENTS; i++ ) {
		clients[i].state = CS_ACTIVE; clients[i].gentity = SV_GentityNum( i );
	}
}

void SE_Frame( int svTime ) {
	sv.time = svTime;
	Sync();
	SV_RecordReplayFrame();
}

int SE_ReplayStart( int client, int fromTime, int toTime, int beginTime ) {
	return SV_ReplayStart( client, fromTime, toTime, beginTime );
}

/* one delta through the codec: written by the server, read by the client */
static void WireEntity( entityState_t *from, entityState_t *to, entityState_t *out ) {
	static byte data[MAX_MSGLEN];
	msg_t msg;
	MSG_Init( &msg, data, sizeof( data ) );
	MSG_WriteDeltaEntity( &msg, from, to, qtrue );
	if ( msg.overflowed ) Com_Error( ERR_FATAL, "WireEntity: overflowed" );
	MSG_BeginReading( &msg );
	MSG_ReadDeltaEntity( &msg, from, out, MSG_ReadBits( &msg, GENTITYNUM_BITS ) );
}
static void WirePlayerstate( playerState_t *from, playerState_t *to, playerState_t *out ) {
	static byte data[MAX_MSGLEN];
	msg_t msg;
	MSG_Init( &msg, data, sizeof( data ) );
	MSG_WriteDeltaPlayerstate( &msg, from, to );
	if ( msg.overflowed ) Com_Error( ERR_FATAL, "WirePlayerstate: overflowed" );
	MSG_BeginReading( &msg );
	MSG_ReadDeltaPlayerstate( &msg, from, out );
}

void SE_Snapshot( int client, ssSnapshot_t *out ) {
	client_t *cl = &clients[client];
	clientSnapshot_t *frame;
	qboolean has[MAX_GENTITIES];
	int i;
	Sync();
	SV_BuildClientSnapshot( cl );
	frame = &cl->frames[cl->netchan.outgoingSequence & PACKET_MASK];
	cl->netchan.outgoingSequence++;
	memset( out, 0, sizeof( *out ) );
	out->serverTime = frame->serverTime;
	out->snapFlags = frame->snapFlags;
	WirePlayerstate( &received[client].ps, &frame->ps, &out->ps );
	received[client].ps = out->ps;
	memset( has, 0, sizeof( has ) );
	if ( frame->num_entities > SS_MAX_SNAP_ENTITIES ) Com_Error( ERR_FATAL, "SE_Snapshot: %i entities", frame->num_entities );
	for ( i = 0; i < frame->num_entities; i++ ) {
		entityState_t *state = &svs.snapshotEntities[( frame->first_entity + i ) % svs.numSnapshotEntities];
		entityState_t base;
		int n = state->number;
		if ( received[client].has[n] ) {
			base = received[client].entities[n];
		} else {
			memset( &base, 0, sizeof( base ) ); base.number = n;	/* a zero baseline */
		}
		WireEntity( &base, state, &out->entities[out->numEntities] );
		received[client].entities[n] = out->entities[out->numEntities];
		has[n] = qtrue;
		out->numEntities++;
	}
	memcpy( received[client].has, has, sizeof( has ) );
}
