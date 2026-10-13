/* Drive the actual server round/objective implementation with deterministic
 * time, traces and clients. No parallel model of the round rules. */
#include "../../code/game/g_snd.c"

level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
gclient_t g_clients[MAX_CLIENTS];
vmCvar_t g_gametype, g_cheats;
static int checks, exits, blocked, contents, spawnSite, copies;
static char configs[32][MAX_INFO_STRING];
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)

void QDECL Com_Error(int code,const char *fmt,...) { (void)code; (void)fmt; abort(); }
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL G_Printf(const char *fmt,...) { (void)fmt; }
void QDECL G_LogPrintf(const char *fmt,...) { (void)fmt; }
void trap_Cvar_Register(vmCvar_t *v,const char *name,const char *initial,int flags) {
 (void)name; (void)flags; memset(v,0,sizeof(*v));
 v->integer=atoi(initial); v->value=atof(initial); Q_strncpyz(v->string,initial,sizeof(v->string));
}
void trap_Cvar_Update(vmCvar_t *v) { (void)v; }
void trap_SetConfigstring(int n,const char *s) { CHECK(n>=0 && n<32); Q_strncpyz(configs[n],s,sizeof(configs[n])); }
void trap_SendServerCommand(int n,const char *s) { (void)n; (void)s; }
void trap_LinkEntity(gentity_t *e) { e->r.linked=qtrue; }
void trap_UnlinkEntity(gentity_t *e) { e->r.linked=qfalse; }
void trap_Trace(trace_t *tr,const vec3_t start,const vec3_t mins,const vec3_t maxs,
 const vec3_t end,int pass,int mask) {
 (void)mins; (void)maxs; (void)pass; (void)mask;
 memset(tr,0,sizeof(*tr)); tr->fraction=blocked ? .5f : 1; VectorCopy(end,tr->endpos);
 if(end[2]<start[2]-1000) { tr->fraction=.5f; tr->endpos[2]=0; }
}
int trap_PointContents(const vec3_t p,int n) { (void)p; (void)n; return contents; }
qboolean G_SpawnInt(const char *key,const char *initial,int *out) { (void)key; (void)initial; *out=spawnSite; return qtrue; }
qboolean G_SpawnFloat(const char *key,const char *initial,float *out) { (void)key; *out=atof(initial); return qtrue; }
gentity_t *G_Spawn(void) {
 gentity_t *e=&g_entities[level.num_entities++]; CHECK(level.num_entities<MAX_GENTITIES);
 memset(e,0,sizeof(*e)); e->inuse=qtrue; e->s.number=e-g_entities; return e;
}
void G_FreeEntity(gentity_t *e) { e->inuse=qfalse; e->r.linked=qfalse; }
void G_SetOrigin(gentity_t *e,vec3_t p) { VectorCopy(p,e->s.origin); VectorCopy(p,e->r.currentOrigin); }
int G_ModelIndex(char *s) { (void)s; return 1; }
void CalculateRanks(void) { }
void PM_UpdateViewAngles(playerState_t *ps,const usercmd_t *cmd) { (void)ps; (void)cmd; }
void LogExit(const char *s) { (void)s; exits++; level.intermissionQueued=level.time; }
void CopyToBodyQue(gentity_t *e) { (void)e; copies++; }
float *tv(float x,float y,float z) { static vec3_t v; VectorSet(v,x,y,z); return v; }
gentity_t *G_TempEntity(vec3_t p,int event) { gentity_t *e=G_Spawn(); G_SetOrigin(e,p); e->s.event=event; return e; }
void ClientSpawn(gentity_t *e) {
 playerState_t *ps=&e->client->ps;
 if(G_SNDWaitForRound(e))return;
 memset(ps,0,sizeof(*ps)); ps->clientNum=e-g_entities;
 ps->persistant[PERS_TEAM]=e->client->sess.sessionTeam;
 ps->stats[STAT_HEALTH]=ps->stats[STAT_MAX_HEALTH]=e->health=100;
 ps->groundEntityNum=ENTITYNUM_WORLD; ps->viewheight=26;
 ps->weapon=WP_SHOTGUN; ps->weaponstate=WEAPON_READY; ps->ammo[WP_SHOTGUN]=8;
 VectorSet(ps->origin, e->client->sess.sessionTeam==TEAM_RED ? -280 : 280, (e-g_entities)*80, 24);
 e->takedamage=qtrue; e->r.linked=qtrue;
 G_SNDClientSpawned(e);
}
static void Player(int n,team_t team) {
 gentity_t *e=&g_entities[n]; memset(e,0,sizeof(*e)); memset(&g_clients[n],0,sizeof(g_clients[n]));
 e->s.number=n; e->client=&g_clients[n]; e->inuse=qtrue;
 e->client->pers.connected=CON_CONNECTED; e->client->sess.sessionTeam=team;
 ClientSpawn(e);
}
static void Reset(int players) {
 gentity_t *site;
 memset(&level,0,sizeof(level)); memset(g_entities,0,sizeof(g_entities)); memset(g_clients,0,sizeof(g_clients));
 memset(configs,0,sizeof(configs)); exits=blocked=contents=spawnSite=copies=0;
 level.time=1000; level.maxclients=4; level.num_entities=MAX_CLIENTS; level.clients=g_clients;
 g_gametype.integer=GT_SND; g_cheats.integer=1;
 G_SNDInit(); sndCountdown.integer=1; sndRoundPause.integer=1;
 site=G_Spawn(); VectorSet(site->s.origin,0,0,8); SP_peace_bombsite(site); G_SNDMapReady();
 if(players>0) Player(0,TEAM_RED);
 if(players>1) Player(1,TEAM_BLUE);
 if(players>2) Player(2,TEAM_RED);
 if(players>3) Player(3,TEAM_BLUE);
}
static void Live(void) {
 G_SNDRunFrame(); CHECK(snd.phase==SND_COUNTDOWN);
 CHECK(!G_SNDCanDamage(&g_entities[0]));
 level.time=snd.deadline; G_SNDRunFrame(); CHECK(snd.phase==SND_LIVE);
 CHECK(!G_SNDCanRespawn());
}
static void MoveToSite(int n) { VectorSet(g_clients[n].ps.origin,0,0,24); }
static void Hold(int n,int duration) {
 usercmd_t cmd; int start=level.time;
 memset(&cmd,0,sizeof(cmd)); cmd.buttons=BUTTON_USE;
 do {
  CHECK(G_SNDUse(&g_entities[n],&cmd));
  if(level.time-start==duration)break;
  level.time+=duration-(level.time-start)<50 ? duration-(level.time-start) : 50;
 } while(1);
}
static void Die(int n) {
 CHECK(G_SNDCanDamage(&g_entities[n]));
 g_entities[n].health=g_clients[n].ps.stats[STAT_HEALTH]=0;
 CHECK(G_SNDCanDamage(&g_entities[n])); /* post-damage player_die entry */
 G_SNDClientDied(&g_entities[n]); CHECK(!G_SNDCanDamage(&g_entities[n]));
}
static void TestPlantDefuse(void) {
 int deadline;
 Reset(2); Live(); CHECK(snd.carrier==0); MoveToSite(0);
 deadline=snd.deadline; level.time=deadline-snd.plantMs-1;
 Hold(0,snd.plantMs); CHECK(snd.phase==SND_PLANTED && snd.carrier==-1);
 CHECK(snd.deadline>deadline && snd.bomb->r.linked);
 Die(0); G_SNDRunFrame(); CHECK(snd.phase==SND_PLANTED);
 MoveToSite(1); Hold(1,snd.defuseMs);
 CHECK(snd.phase==SND_ROUND_OVER && snd.result==SND_DEFUSED && level.teamScores[TEAM_BLUE]==1);
 CHECK(!G_SNDCanDamage(&g_entities[1]));
 G_SNDRunFrame(); CHECK(level.teamScores[TEAM_BLUE]==1);
 level.time=snd.deadline; G_SNDRunFrame();
 CHECK(snd.phase==SND_COUNTDOWN && snd.round==2 && SND_Alive(0));
 CHECK(g_clients[0].ps.ammo[WP_SHOTGUN]==8 && !snd.bomb->r.linked);
}
static void TestInterrupts(void) {
 usercmd_t cmd; int state;
 Reset(2); Live(); MoveToSite(0); memset(&cmd,0,sizeof(cmd)); cmd.buttons=BUTTON_USE;
 CHECK(G_SNDUse(&g_entities[0],&cmd)); level.time+=100;
 CHECK(G_SNDUse(&g_entities[0],&cmd)); CHECK(g_clients[0].ps.stats[STAT_USE_PROGRESS]>0);
 cmd.serverTime=INT_MAX; CHECK(G_SNDUse(&g_entities[0],&cmd));
 CHECK(snd.phase==SND_LIVE); /* future command timestamps grant no time */
 cmd.forwardmove=1; CHECK(!G_SNDUse(&g_entities[0],&cmd));
 CHECK(g_clients[0].ps.stats[STAT_USE_PROGRESS]==0);
 cmd.forwardmove=0; CHECK(G_SNDUse(&g_entities[0],&cmd));
 level.time+=201; CHECK(G_SNDUse(&g_entities[0],&cmd));
 CHECK(g_clients[0].ps.stats[STAT_USE_PROGRESS]==0); /* command gap restarts */
 cmd.buttons=0; CHECK(!G_SNDUse(&g_entities[0],&cmd));
 cmd.buttons=BUTTON_USE|BUTTON_ATTACK; CHECK(!G_SNDUse(&g_entities[0],&cmd));
 cmd.buttons=BUTTON_USE; blocked=1; CHECK(!G_SNDUse(&g_entities[0],&cmd)); blocked=0;
 g_clients[0].ps.stats[STAT_LADDER]=LADDER_ATTACHED; CHECK(!G_SNDUse(&g_entities[0],&cmd));
 g_clients[0].ps.stats[STAT_LADDER]=0;
 for(state=WEAPON_RAISING;state<=WEAPON_RELOADING;state++) {
  g_clients[0].ps.weaponstate=state; CHECK(!G_SNDUse(&g_entities[0],&cmd));
  CHECK(g_clients[0].ps.weaponstate==state);
 }
 g_clients[0].ps.weaponstate=WEAPON_READY;
 MoveToSite(1); CHECK(!G_SNDUse(&g_entities[1],&cmd)); /* wrong team */
 Hold(0,snd.plantMs);
 CHECK(!G_SNDUse(&g_entities[0],&cmd)); /* attackers cannot defuse */
 CHECK(G_SNDUse(&g_entities[1],&cmd));
 snd.holdStart[1]=snd.deadline-snd.defuseMs;
 snd.holdSeen[1]=snd.deadline;
 level.time=snd.deadline;
 CHECK(!G_SNDUse(&g_entities[1],&cmd)); G_SNDRunFrame();
 CHECK(snd.result==SND_EXPLODED && level.teamScores[TEAM_RED]==1);
}
static void TestCarrierSpectateJoin(void) {
 usercmd_t cmd;
 Reset(3); Live(); MoveToSite(0); Die(0);
 CHECK(snd.carrier==-1 && snd.bomb->r.linked);
 MoveToSite(1); G_SNDRunFrame(); CHECK(snd.carrier==-1); /* defenders cannot take bomb */
 MoveToSite(2); blocked=1; G_SNDRunFrame(); CHECK(snd.carrier==-1);
 blocked=0; G_SNDRunFrame(); CHECK(snd.carrier==2 && !snd.bomb->r.linked);
 g_clients[0].ps.persistant[PERS_SCORE]=7;
 CHECK(G_SNDClientEndFrame(&g_entities[0])); CHECK(snd.following[0]==2);
 CHECK(g_clients[0].sess.sessionTeam==TEAM_RED && !SND_Alive(0));
 CHECK(g_clients[0].ps.persistant[PERS_SCORE]==7 && (g_clients[0].ps.pm_flags&PMF_FOLLOW));
 memset(&cmd,0,sizeof(cmd)); cmd.buttons=BUTTON_ATTACK;
 CHECK(G_SNDClientThink(&g_entities[0],&cmd)); CHECK(!SND_Alive(0));
 Player(3,TEAM_RED); CHECK(!SND_Alive(3) && g_entities[3].health==0); /* late join */
 ClientSpawn(&g_entities[0]); CHECK(!SND_Alive(0)); /* cannot force respawn */
 G_SNDClientDied(&g_entities[2]); g_clients[2].pers.connected=CON_DISCONNECTED;
 CHECK(snd.carrier==-1); G_SNDRunFrame(); CHECK(snd.result==SND_ELIMINATION && snd.winner==TEAM_BLUE);
}
static void TestWinsAndSides(void) {
 int round;
 Reset(2); Live();
 for(round=1;round<=4;round++) {
  CHECK(snd.round==round);
  CHECK(snd.attacking==(round<=3 ? TEAM_RED : TEAM_BLUE));
  CHECK(G_SNDSpawnTeam(snd.attacking)==TEAM_RED);
  Die(1); G_SNDRunFrame(); CHECK(level.teamScores[TEAM_RED]==round);
  if(round<4) {
   level.time=snd.deadline; G_SNDRunFrame(); CHECK(snd.phase==SND_COUNTDOWN);
   level.time=snd.deadline; G_SNDRunFrame(); CHECK(snd.phase==SND_LIVE);
  }
 }
 CHECK(exits==1 && level.intermissionQueued);
 G_SNDRunFrame(); CHECK(exits==1);
 Reset(2); Live(); Die(0); Die(1); G_SNDRunFrame();
 CHECK(snd.result==SND_DRAW && level.teamScores[TEAM_RED]==0 && level.teamScores[TEAM_BLUE]==0);
 Reset(2); Live(); level.time=snd.deadline; G_SNDRunFrame();
 CHECK(snd.result==SND_TIME && snd.winner==TEAM_BLUE);
}
static void TestWaitingPracticeAndMode(void) {
 Reset(1); G_SNDRunFrame(); CHECK(snd.phase==SND_WAITING && G_SNDCanRespawn());
 sndPractice.integer=1; g_cheats.integer=0; G_SNDRunFrame(); CHECK(snd.phase==SND_WAITING);
 g_cheats.integer=1; Live(); G_SNDRunFrame(); CHECK(snd.phase==SND_LIVE && snd.practice);
 MoveToSite(0); Hold(0,snd.plantMs); CHECK(snd.phase==SND_PLANTED);
 Reset(2); snd.numSites=0; G_SNDRunFrame(); CHECK(snd.phase==SND_WAITING);
 Reset(2); G_SNDRunFrame(); g_clients[1].pers.connected=CON_DISCONNECTED;
 G_SNDRunFrame(); CHECK(snd.phase==SND_WAITING && snd.round==0);
 g_gametype.integer=GT_FFA; CHECK(G_SNDCanRespawn() && G_SNDCanDamage(&g_entities[0]));
 CHECK(!G_SNDWaitForRound(&g_entities[0])); CHECK(G_SNDSpawnTeam(TEAM_BLUE)==TEAM_BLUE);
}
static void TestCountdownAndEmptyTeam(void) {
 Reset(3); G_SNDRunFrame(); CHECK(snd.phase==SND_COUNTDOWN && snd.carrier==0);
 G_SNDClientDied(&g_entities[0]); g_clients[0].pers.connected=CON_DISCONNECTED;
 CHECK(snd.carrier==-1);
 level.time=snd.deadline; G_SNDRunFrame();
 CHECK(snd.phase==SND_LIVE && snd.carrier==2 && !snd.bomb->r.linked);
 Die(2); G_SNDRunFrame(); CHECK(snd.phase==SND_ROUND_OVER);
 g_clients[1].pers.connected=CON_DISCONNECTED;
 level.time=snd.deadline; G_SNDRunFrame();
 CHECK(snd.phase==SND_WAITING && G_SNDCanRespawn());
 CHECK(SND_Alive(2) && g_clients[2].ps.pm_type==PM_NORMAL);
 CHECK(!(g_clients[2].ps.pm_flags&PMF_FOLLOW) && !snd.bomb->r.linked);
 Player(1,TEAM_BLUE); G_SNDRunFrame(); CHECK(snd.phase==SND_COUNTDOWN && snd.round==2);
}
int main(void) {
 TestPlantDefuse(); TestInterrupts(); TestCarrierSpectateJoin(); TestWinsAndSides(); TestWaitingPracticeAndMode();
 TestCountdownAndEmptyTeam();
 printf("PASS: %i S&D round, bomb, timing, spectating, late-join and side-switch checks\n",checks);
 return 0;
}
