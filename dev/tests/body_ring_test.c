/* A body and a podium model are copies of a player's entity, and each is a
 * new entity with no events of its own. This runs the real CopyToBodyQue
 * (g_client.c) and SpawnModelOnVictoryPad (g_arenas.c, included here for its
 * static function). Each must leave its copy no ring. If one did, the real
 * G_AgeEntityEvents would stamp the copied slots as new on the next frame,
 * and every client that sees the copy would play the player's last shots
 * again: a sound, and client-drawn tracers and impacts. The player's own ring
 * is left as it was. */
#include "../../code/game/g_arenas.c"

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
static gclient_t client;
vmCvar_t g_gametype;
void QDECL Com_Printf( const char *fmt, ... ) { (void)fmt; }
void QDECL Com_Error( int code, const char *fmt, ... ) { (void)code; (void)fmt; abort(); }
void QDECL G_Error( const char *fmt, ... ) { (void)fmt; abort(); }
void QDECL G_Printf( const char *fmt, ... ) { (void)fmt; }
void trap_LinkEntity( gentity_t *e ) { e->r.linked = qtrue; }
void trap_UnlinkEntity( gentity_t *e ) { e->r.linked = qfalse; }
void trap_LocateGameData( gentity_t *g, int n, int s, playerState_t *c, int cs ) { (void)g; (void)n; (void)s; (void)c; (void)cs; }
int trap_PointContents( const vec3_t p, int pass ) { (void)p; (void)pass; return 0; }
/* the corpse's own (g_combat.c, g_missile.c): not reached here */
void body_die( gentity_t *s, gentity_t *i, gentity_t *a, int damage, int mod ) { (void)s; (void)i; (void)a; (void)damage; (void)mod; abort(); }
void G_MoveCarriedMissiles( gentity_t *from, gentity_t *to ) { (void)from; (void)to; }

static void CheckNoRing( gentity_t *copy, const gentity_t *player ) {
	int i;
	CHECK( copy != player && copy->s.number == copy - g_entities );
	CHECK( !copy->s.event && !copy->s.eventSequence && !copy->eventRingSequence );
	for ( i = 0; i < MAX_ENTITY_EVENTS; i++ ) CHECK( !copy->s.events[i] && !copy->s.eventParms[i] );
	/* the next frame stamps nothing on it */
	level.time += 50;
	G_AgeEntityEvents( copy );
	CHECK( !copy->s.eventSequence && !copy->eventRingSequence );
	for ( i = 0; i < MAX_ENTITY_EVENTS; i++ ) CHECK( !copy->s.events[i] );
}

int main( void ) {
	gentity_t *player = &g_entities[0], *pad, *model;
	entityState_t ring;
	vec3_t offset = { 0, 0, 64 };
	memset( &level, 0, sizeof( level ) );
	level.time = 1000; level.num_entities = MAX_CLIENTS; level.maxclients = 1;
	level.clients = &client; level.gentities = g_entities;
	InitBodyQue();

	/* a player whose ring holds its last shot and a step */
	player->inuse = qtrue; player->client = &client; player->classname = "player";
	player->s.number = 0; player->s.eType = ET_PLAYER; player->s.weapon = WP_SHOTGUN;
	player->r.contents = CONTENTS_BODY; trap_LinkEntity( player );
	client.ps.clientNum = 0; client.ps.groundEntityNum = ENTITYNUM_WORLD;
	G_AddEvent( player, EV_FIRE_WEAPON, WP_SHOTGUN );
	G_AddEvent( player, EV_FOOTSTEP, 0 );
	G_AgeEntityEvents( player );
	CHECK( player->s.eventSequence == 2 && player->eventRingSequence == 2 );
	ring = player->s;

	/* the body it leaves */
	CopyToBodyQue( player );
	CheckNoRing( level.bodyQue[0], player );
	CHECK( player->s.eventSequence == ring.eventSequence && !memcmp( player->s.events, ring.events, sizeof( ring.events ) ) );

	/* the single-player podium's model of it (BeginIntermission's
	 * SpawnModelsOnVictoryPads, within EVENT_VALID_MSEC of the shot) */
	level.time = 1100;
	pad = G_Spawn();
	VectorSet( pad->r.currentOrigin, 100, 0, 0 );
	VectorSet( level.intermission_origin, 0, 0, 0 );
	model = SpawnModelOnVictoryPad( pad, offset, player, 1 );
	CHECK( model && model->client == player->client && model->s.eType == ET_PLAYER );
	CheckNoRing( model, player );
	CHECK( player->s.eventSequence == ring.eventSequence && !memcmp( player->s.eventParms, ring.eventParms, sizeof( ring.eventParms ) ) );

	printf( "PASS: %d body and podium ring checks\n", checks );
	return 0;
}
