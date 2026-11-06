/* MW2's UAV on the server: the real g_killstreak.c. Earning (3 enemy kills a
 * life, once a life), keeping it, using it, and the radar sweeps its owners
 * get (every compassRadarUpdateTime, compassFastRadarUpdateTime with two up,
 * gone after radarViewTime), in team games and free-for-all. */
#include "../../code/game/g_local.h"

level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
static gclient_t clients[MAX_CLIENTS];
vmCvar_t g_gametype;
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level, const char *fmt, ...) { (void)level; (void)fmt; exit(1); }
void trap_SnapVector(float *v) { (void)v; }

/* the last server command each client got, and how many */
static char last[MAX_CLIENTS][MAX_STRING_CHARS];
static int sent[MAX_CLIENTS];
void trap_SendServerCommand(int clientNum, const char *text) {
 int i;
 for (i = 0; i < MAX_CLIENTS; i++) if (clientNum == -1 || clientNum == i) {
  Q_strncpyz(last[i], text, sizeof(last[i])); sent[i]++;
 }
}
qboolean OnSameTeam(gentity_t *a, gentity_t *b) {
 return a->client && b->client && g_gametype.integer >= GT_TEAM &&
  a->client->sess.sessionTeam == b->client->sess.sessionTeam;
}

#define PLAYERS 4
static void Reset(int gametype) {
 int i;
 memset(&level, 0, sizeof(level)); memset(g_entities, 0, sizeof(g_entities));
 memset(clients, 0, sizeof(clients)); memset(last, 0, sizeof(last)); memset(sent, 0, sizeof(sent));
 g_gametype.integer = gametype;
 level.clients = clients; level.maxclients = PLAYERS; level.time = 1000;
 for (i = 0; i < PLAYERS; i++) {
  g_entities[i].inuse = qtrue; g_entities[i].client = &clients[i]; g_entities[i].s.number = i;
  g_entities[i].health = 100;
  clients[i].pers.connected = CON_CONNECTED;
  clients[i].sess.sessionTeam = gametype >= GT_TEAM ? (i < 2 ? TEAM_RED : TEAM_BLUE) : TEAM_FREE;
  VectorSet(g_entities[i].r.currentOrigin, 100 * i, -50 * i, 0);
 }
 G_ClearUAVs();
 memset(sent, 0, sizeof(sent));
}
static void Kill(int attacker, int victim) { G_KillstreakKill(&g_entities[attacker], &g_entities[victim]); }
static int Pings(int client) {
 int n = 0; const char *p = last[client];
 if (strncmp(p, "uav ", 4)) return -1;
 for (; *p; p++) if (*p == ' ') n++;
 return (n - 1) / 3;
}
static void Frames(int ms) { int t; for (t = 0; t < ms; t += 50) { level.time += 50; G_UpdateUAVs(); } }

static void TestEarning(void) {
 Reset(GT_TEAM);
 Kill(0, 2); Kill(0, 3);
 CHECK(clients[0].ps.ammo[WP_KILLSTREAK_UAV] == 0);
 Kill(0, 1);	/* a team kill is no step */
 CHECK(clients[0].killstreakKills == 2);
 Kill(0, 0);	/* nor a suicide */
 Kill(0, 2);
 CHECK(clients[0].killstreakKills == BG_UAV_KILLS && clients[0].ps.ammo[WP_KILLSTREAK_UAV] == 1);
 CHECK(clients[0].pers.killstreakUAVs == 1 && (clients[0].ps.stats[STAT_WEAPONS] & (1 << WP_KILLSTREAK_UAV)));
 /* once a life: the streak going on earns no second one */
 Kill(0, 3); Kill(0, 2); Kill(0, 3);
 CHECK(clients[0].ps.ammo[WP_KILLSTREAK_UAV] == 1);
 /* a dead attacker's late kill does not count */
 Reset(GT_TEAM); g_entities[0].health = 0; Kill(0, 2); Kill(0, 2); Kill(0, 2);
 CHECK(clients[0].killstreakKills == 0);
 /* a new life starts its count again but keeps what it earned (ClientSpawn
  * restores ammo from pers; the count lives in the part a spawn clears) */
 Reset(GT_TEAM); Kill(0, 2); Kill(0, 3); Kill(0, 2);
 clients[0].killstreakKills = 0;	/* the spawn's clear */
 Kill(0, 2); Kill(0, 3); Kill(0, 2);
 CHECK(clients[0].pers.killstreakUAVs == 2 && clients[0].ps.ammo[WP_KILLSTREAK_UAV] == 2);
}

static void TestRadarTeam(void) {
 int i;
 Reset(GT_TEAM);
 clients[0].ps.ammo[WP_KILLSTREAK_UAV] = 0; clients[0].pers.killstreakUAVs = 1;
 G_UseKillstreak(&g_entities[0]);
 CHECK(clients[0].pers.killstreakUAVs == 0);	/* pmove spent it; the server follows */
 /* the radar starts now, for the owner's team only, with both enemies */
 G_UpdateUAVs();
 CHECK(Pings(0) == 2 && Pings(1) == 2 && strncmp(last[2], "uav ", 4) && strncmp(last[3], "uav ", 4));
 CHECK(atoi(last[0] + 4) == BG_RADAR_UPDATE_TIME);
 /* the next sweep comes compassRadarUpdateTime later, not before */
 i = sent[0]; Frames(BG_RADAR_UPDATE_TIME - 50); CHECK(sent[0] == i);
 Frames(50); CHECK(sent[0] == i + 1 && Pings(0) == 2);
 /* a dead enemy is not on it */
 g_entities[3].health = 0; Frames(BG_RADAR_UPDATE_TIME); CHECK(Pings(0) == 1);
 g_entities[3].health = 100;
 /* a second UAV: fast radar */
 G_UseKillstreak(&g_entities[1]);
 i = sent[0]; Frames(BG_RADAR_UPDATE_TIME);
 CHECK(atoi(last[0] + 4) == BG_FAST_RADAR_UPDATE_TIME);
 /* both leave radarViewTime after their launch, and the radar is cleared once */
 Frames(BG_UAV_TIME);
 CHECK(!strcmp(last[0], "uav 0") && !strcmp(last[1], "uav 0"));
 i = sent[0]; Frames(BG_RADAR_UPDATE_TIME * 2); CHECK(sent[0] == i);
 /* a round restart takes every UAV down at once */
 G_UseKillstreak(&g_entities[2]); G_UpdateUAVs(); CHECK(Pings(2) == 2);
 G_ClearUAVs(); CHECK(!strcmp(last[2], "uav 0"));
 i = sent[2]; Frames(BG_RADAR_UPDATE_TIME); CHECK(sent[2] == i);
}

static void TestRadarFFA(void) {
 Reset(GT_FFA);
 G_UseKillstreak(&g_entities[1]); G_UpdateUAVs();
 /* the owner alone, with every other player on it */
 CHECK(Pings(1) == PLAYERS - 1 && Pings(0) < 0 && Pings(2) < 0);
}

int main(void) {
 TestEarning();
 TestRadarTeam();
 TestRadarFFA();
 printf("PASS: %d UAV killstreak server checks\n", checks);
 return 0;
}
