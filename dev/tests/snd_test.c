/* Drive the actual server round/objective implementation with deterministic
 * time, traces and clients. No parallel model of the round rules. */
#include "../../code/game/g_snd.c"

level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
gclient_t g_clients[MAX_CLIENTS];
vmCvar_t g_gametype, g_cheats;
static int checks, exits, blocked, contents, spawnSite, copies;
static int replayStarts, replayResets, replayKiller, replayFrom, replayTo, replayBegin, replayDeadline, replayFail;
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
void trap_ReplayReset(qboolean enabled) { CHECK(enabled); replayResets++; }
/* SV_ReplayStart's deadline: the window played at game speed from begin, or
 * 0 when the history is missing */
int trap_ReplayStart(int killer,int from,int to,int begin) {
 replayStarts++; replayKiller=killer; replayFrom=from; replayTo=to; replayBegin=begin;
 replayDeadline=replayFail ? 0 : begin+to-from; return replayDeadline;
}
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
 ps->groundEntityNum=ENTITYNUM_WORLD; ps->viewheight=DEFAULT_VIEWHEIGHT;
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
 replayStarts=replayResets=replayFail=replayDeadline=0;
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
 int deadline, defused;
 Reset(2); Live(); CHECK(snd.carrier==0); MoveToSite(0);
 deadline=snd.deadline; level.time=deadline-snd.plantMs-1;
 Hold(0,snd.plantMs); CHECK(snd.phase==SND_PLANTED && snd.carrier==-1);
 CHECK(snd.deadline>deadline && snd.bomb->r.linked);
 Die(0); G_SNDRunFrame(); CHECK(snd.phase==SND_PLANTED);
 MoveToSite(1); Hold(1,snd.defuseMs);
 /* sd.gsc bombDefused: the bomb stops and cannot be defused again; play goes
  * on (damage on) for its wait 1.5, then the round ends */
 defused=level.time;
 CHECK(snd.phase==SND_PLANTED && snd.result==SND_DEFUSED && level.teamScores[TEAM_BLUE]==0);
 CHECK(snd.deadline==defused+SND_DEFUSE_DELAY && snd.bomb->r.linked);
 CHECK(G_SNDCanDamage(&g_entities[1]) && !G_SNDUsable(&g_entities[1]));
 CHECK(g_clients[1].ps.stats[STAT_USE_TARGET]==-1);
 level.time=defused+SND_DEFUSE_DELAY-1; G_SNDRunFrame(); CHECK(snd.phase==SND_PLANTED);
 level.time=defused+SND_DEFUSE_DELAY; G_SNDRunFrame();
 CHECK(snd.phase==SND_ROUND_OVER && snd.result==SND_DEFUSED && snd.winner==TEAM_BLUE);
 CHECK(level.teamScores[TEAM_BLUE]==1 && snd.freezeTime==level.time+ROUNDEND_FREEZE_DELAY);
 CHECK(!G_SNDCanDamage(&g_entities[1]) && !replayStarts);
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
 cmd.buttons=BUTTON_USE_RELOAD; CHECK(G_SNDUse(&g_entities[0],&cmd)); /* use/reload uses it too */
 CHECK(G_SNDUsable(&g_entities[0]) && !G_SNDUsable(&g_entities[1])); /* only the carrier plants */
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
 CHECK(!G_SNDUsable(&g_entities[0]) && G_SNDUsable(&g_entities[1]));
 CHECK(G_SNDUse(&g_entities[1],&cmd));
 snd.holdStart[1]=snd.deadline-snd.defuseMs;
 snd.holdSeen[1]=snd.deadline;
 level.time=snd.deadline;
 CHECK(!G_SNDUse(&g_entities[1],&cmd)); G_SNDRunFrame();
 /* the detonation: the bomb is gone and every objective with it; the round
  * ends 3s later (sd.gsc's wait 3), still live meanwhile */
 CHECK(snd.phase==SND_PLANTED && snd.result==SND_EXPLODED && level.teamScores[TEAM_RED]==0);
 CHECK(!snd.bomb->r.linked && !G_SNDUsable(&g_entities[1]) && G_SNDCanDamage(&g_entities[1]));
 level.time+=SND_DETONATION_DELAY; G_SNDRunFrame();
 CHECK(snd.phase==SND_ROUND_OVER && snd.result==SND_EXPLODED && level.teamScores[TEAM_RED]==1);
 CHECK(snd.freezeTime==level.time+ROUNDEND_FREEZE_DELAY);
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
static void TestRoundKillcam(void) {
 int deadline, resets, death;
 /* the decisive kill at D decides the round in the death itself, as MW2's
  * onDeadEvent does: postgame (no damage) before anything else this frame */
 Reset(2); Live(); level.time+=6000;
 Die(1); G_SNDRecordKill(&g_entities[1],&g_entities[0]); death=level.time;
 CHECK(snd.phase==SND_ROUND_OVER && snd.winner==TEAM_RED && snd.result==SND_ELIMINATION);
 CHECK(replayStarts==1 && replayKiller==0 && !G_SNDCanDamage(&g_entities[0]));
 level.time+=50; G_SNDRunFrame();
 CHECK(replayStarts==1 && replayKiller==0);
 /* MW2's final killcam: 4.0s before the death to 1.95s after it, played once
  * roundEndWait( 4, true ) is over, from the death: 5.05s */
 CHECK(replayFrom==death-KILLCAM_REWIND && replayTo==death+KILLCAM_LINGER);
 CHECK(replayBegin==death+KILLCAM_ROUND_DELAY && replayDeadline==replayBegin+KILLCAM_REWIND+KILLCAM_LINGER);
 CHECK(KILLCAM_REWIND+KILLCAM_LINGER==5950 && KILLCAM_ROUND_DELAY==5050);
 /* the next round follows the killcam by doFinalKillcam's 0.1s */
 CHECK(snd.deadline==replayDeadline+SND_KILLCAM_RESTART);
 CHECK(level.replayEndTime==replayDeadline && level.teamScores[TEAM_RED]==1);
 CHECK(snd.freezeTime==death+ROUNDEND_FREEZE_DELAY);
 deadline=snd.deadline; resets=replayResets;
 level.time=deadline-1; G_SNDRunFrame();
 CHECK(snd.phase==SND_ROUND_OVER && replayStarts==1 && replayResets==resets);
 level.time=deadline; G_SNDRunFrame();
 CHECK(snd.phase==SND_COUNTDOWN && snd.round==2 && replayResets==resets+1);
 CHECK(level.replayEndTime==0 && snd.lastDeath[TEAM_BLUE].killer==-1);
 /* Match-winning round reuses this exact replay; no second selection. Its
  * killcam comes after roundEndWait( 4, false ): 4.55s from the death. */
 Reset(2); sndWinLimit.integer=1; Live();
 Die(1); G_SNDRecordKill(&g_entities[1],&g_entities[0]); death=level.time;
 level.time+=50; G_SNDRunFrame();
 CHECK(exits==1 && replayStarts==1 && level.replayEndTime==replayDeadline);
 CHECK(replayBegin==death+KILLCAM_MATCH_DELAY && replayTo==death+KILLCAM_LINGER);
 G_SNDRunFrame(); CHECK(replayStarts==1);
 /* one round short of the limit is not the match */
 Reset(2); sndWinLimit.integer=2; Live();
 Die(1); G_SNDRecordKill(&g_entities[1],&g_entities[0]); death=level.time; G_SNDRunFrame();
 CHECK(!exits && replayBegin==death+KILLCAM_ROUND_DELAY);
 /* A later non-kill elimination must not inherit an earlier frag. */
 Reset(4); Live(); Die(1); G_SNDRecordKill(&g_entities[1],&g_entities[0]);
 Die(3); G_SNDRunFrame(); CHECK(snd.result==SND_ELIMINATION && !replayStarts);
 Reset(2); Live(); Die(1); G_SNDRecordKill(&g_entities[1],&g_entities[1]);
 G_SNDRunFrame(); CHECK(!replayStarts);
 Reset(2); Live(); Die(1); G_SNDRecordKill(&g_entities[1],NULL);
 G_SNDRunFrame(); CHECK(!replayStarts);
 Reset(3); Live(); Die(0); G_SNDRecordKill(&g_entities[0],&g_entities[2]);
 CHECK(snd.lastDeath[TEAM_RED].killer==-1); /* friendly fire */
 /* Objective wins/draws never replay an unrelated kill. */
 Reset(3); Live(); Die(0); G_SNDRecordKill(&g_entities[0],&g_entities[1]);
 level.time=snd.deadline; G_SNDRunFrame(); CHECK(snd.result==SND_TIME && !replayStarts);
 /* A draw comes only from a frame that finds both teams emptied without a
  * deciding death (disconnects, team changes), and replays nothing. */
 Reset(4); Live(); Die(2); G_SNDRecordKill(&g_entities[2],&g_entities[1]);
 CHECK(snd.phase==SND_LIVE);
 Die(0); Die(1); Die(3); G_SNDRunFrame(); CHECK(snd.result==SND_DRAW && !replayStarts);
 /* Two last players: the first death decides; the second can no longer be
  * dealt (MW2's endGame is postgame before the rest of the frame). */
 Reset(2); Live(); Die(0); G_SNDRecordKill(&g_entities[0],&g_entities[1]);
 CHECK(snd.phase==SND_ROUND_OVER && snd.winner==TEAM_BLUE && snd.result==SND_ELIMINATION);
 CHECK(replayStarts==1 && replayKiller==1 && !G_SNDCanDamage(&g_entities[1]));
 /* The deadline wins a tie: a death on the frame it is due decides nothing. */
 Reset(2); Live(); level.time=snd.deadline; Die(1); G_SNDRecordKill(&g_entities[1],&g_entities[0]);
 CHECK(snd.phase==SND_LIVE); G_SNDRunFrame(); CHECK(snd.result==SND_TIME && snd.winner==TEAM_BLUE && !replayStarts);
 /* Practice never ends a round on a death. */
 Reset(2); sndPractice.integer=1; Live(); Die(1); G_SNDRecordKill(&g_entities[1],&g_entities[0]);
 G_SNDRunFrame(); CHECK(snd.phase==SND_LIVE && snd.practice); sndPractice.integer=0;
 /* A killer can already be dead when their projectile eliminates defenders. */
 Reset(2); Live(); MoveToSite(0); Hold(0,snd.plantMs);
 Die(0); G_SNDRecordKill(&g_entities[0],&g_entities[1]);
 Die(1); G_SNDRecordKill(&g_entities[1],&g_entities[0]); G_SNDRunFrame();
 CHECK(snd.winner==TEAM_RED && replayStarts==1 && replayKiller==0);
 /* The reverse splash-damage order selects the same decisive kill, and the
  * rest of the blast is refused: the planter survives it. */
 Reset(2); Live(); MoveToSite(0); Hold(0,snd.plantMs);
 Die(1); G_SNDRecordKill(&g_entities[1],&g_entities[0]);
 CHECK(snd.phase==SND_ROUND_OVER && !G_SNDCanDamage(&g_entities[0]) && SND_Alive(0));
 G_SNDRunFrame(); CHECK(snd.winner==TEAM_RED && replayStarts==1 && replayKiller==0);
 /* Missing history falls back to the ordinary pause instead of hanging. */
 Reset(2); Live(); replayFail=1; Die(1);
 G_SNDRecordKill(&g_entities[1],&g_entities[0]); G_SNDRunFrame();
 CHECK(snd.deadline==level.time+snd.pauseMs && level.replayEndTime==0);
}
/* MW2's endGame is postgame at once (no damage, no use) but leaves everyone
 * full control for its waitframe and freezePlayerForRoundEnd( 1.0 ): 1.05s
 * from the deciding death, or from the frame an objective result comes in.
 * Then the survivors freeze. */
static void TestRoundEndWindow(void) {
 usercmd_t cmd; int death;
 Reset(3); Live(); MoveToSite(0);
 Die(1); G_SNDRecordKill(&g_entities[1],&g_entities[0]); death=level.time;
 level.time+=50; G_SNDRunFrame();
 CHECK(snd.phase==SND_ROUND_OVER && snd.freezeTime==death+ROUNDEND_FREEZE_DELAY && ROUNDEND_FREEZE_DELAY==1050);
 memset(&cmd,0,sizeof(cmd)); cmd.buttons=BUTTON_USE;
 CHECK(!G_SNDCanDamage(&g_entities[0]) && !G_SNDCanDamage(&g_entities[2]));
 CHECK(!G_SNDUsable(&g_entities[0]) && !G_SNDUse(&g_entities[0],&cmd)); /* the carrier at a site: no plant */
 for(; level.time<death+ROUNDEND_FREEZE_DELAY; level.time+=50) {
  cmd.serverTime=level.time;
  /* both survivors' commands go to Pmove; the dead one still spectates */
  CHECK(!G_SNDClientThink(&g_entities[0],&cmd) && !G_SNDClientThink(&g_entities[2],&cmd));
  CHECK(g_clients[0].ps.pm_type==PM_NORMAL && G_SNDClientThink(&g_entities[1],&cmd));
  G_SNDRunFrame(); CHECK(snd.phase==SND_ROUND_OVER && !G_SNDCanDamage(&g_entities[0]));
 }
 cmd.serverTime=level.time;
 CHECK(G_SNDClientThink(&g_entities[0],&cmd) && g_clients[0].ps.pm_type==PM_FREEZE);
 CHECK(G_SNDClientThink(&g_entities[2],&cmd) && g_clients[0].ps.commandTime==cmd.serverTime);
 /* an objective's round end: from the frame it ends in */
 Reset(2); Live(); level.time=snd.deadline; G_SNDRunFrame();
 CHECK(snd.result==SND_TIME && snd.freezeTime==level.time+ROUNDEND_FREEZE_DELAY);
 /* an elimination with no killer: from the frame that sees it */
 Reset(2); Live(); Die(1); level.time+=50; G_SNDRunFrame();
 CHECK(snd.result==SND_ELIMINATION && snd.freezeTime==level.time+ROUNDEND_FREEZE_DELAY && !replayStarts);
}
/* sd.gsc: after a detonation (wait 3) or a defuse (wait 1.5) play goes on,
 * damage and all, but no elimination ends the round (onDeadEvent returns) and
 * none makes a killcam; then the objective's winner takes it. */
static void TestObjectiveWindows(void) {
 int boom, defused, bombDeadline, effects, n;
 Reset(2); Live(); MoveToSite(0); Hold(0,snd.plantMs);
 level.time=snd.deadline; G_SNDRunFrame(); boom=level.time;
 CHECK(snd.result==SND_EXPLODED && snd.phase==SND_PLANTED && snd.deadline==boom+SND_DETONATION_DELAY);
 for(effects=0,n=MAX_CLIENTS;n<level.num_entities;n++) effects+=g_entities[n].inuse && g_entities[n].s.event==EV_MISSILE_MISS;
 CHECK(effects==1);
 Die(1); G_SNDRecordKill(&g_entities[1],&g_entities[0]); level.time+=50; G_SNDRunFrame();
 CHECK(snd.phase==SND_PLANTED && !replayStarts && level.teamScores[TEAM_RED]==0);
 Die(0); level.time+=50; G_SNDRunFrame(); CHECK(snd.phase==SND_PLANTED);
 level.time=boom+SND_DETONATION_DELAY-1; G_SNDRunFrame(); CHECK(snd.phase==SND_PLANTED);
 level.time++; G_SNDRunFrame();
 CHECK(snd.phase==SND_ROUND_OVER && snd.winner==TEAM_RED && snd.result==SND_EXPLODED && !replayStarts);
 CHECK(level.teamScores[TEAM_RED]==1 && snd.freezeTime==level.time+ROUNDEND_FREEZE_DELAY);
 for(effects=0,n=MAX_CLIENTS;n<level.num_entities;n++) effects+=g_entities[n].inuse && g_entities[n].s.event==EV_MISSILE_MISS;
 CHECK(effects==1); /* it went off once */
 /* the defuser dies in the 1.5s, everyone dead: still the defenders' */
 Reset(2); Live(); MoveToSite(0); Hold(0,snd.plantMs); Die(0); G_SNDRunFrame();
 MoveToSite(1); Hold(1,snd.defuseMs); defused=level.time;
 Die(1); level.time+=50; G_SNDRunFrame(); CHECK(snd.phase==SND_PLANTED && snd.result==SND_DEFUSED);
 level.time=defused+SND_DEFUSE_DELAY; G_SNDRunFrame();
 CHECK(snd.phase==SND_ROUND_OVER && snd.winner==TEAM_BLUE && snd.result==SND_DEFUSED && level.teamScores[TEAM_BLUE]==1);
 /* a defuse stops the bomb: its own deadline then passes with no detonation */
 Reset(2); Live(); MoveToSite(0); Hold(0,snd.plantMs); bombDeadline=snd.deadline;
 MoveToSite(1); level.time=bombDeadline-snd.defuseMs-500; Hold(1,snd.defuseMs); defused=level.time;
 CHECK(snd.result==SND_DEFUSED && defused<bombDeadline && defused+SND_DEFUSE_DELAY>bombDeadline);
 level.time=bombDeadline; G_SNDRunFrame();
 CHECK(snd.phase==SND_PLANTED && snd.result==SND_DEFUSED && snd.bomb->r.linked);
 level.time=defused+SND_DEFUSE_DELAY; G_SNDRunFrame();
 CHECK(snd.phase==SND_ROUND_OVER && snd.winner==TEAM_BLUE && level.teamScores[TEAM_RED]==0);
}
/* A Tactical Insertion can be planted in S&D but never outlives its round
 * (MW2's round restart frees everything; ClientSpawn never spawns on it in
 * S&D): the next round's reset frees it with every missile, and so does going
 * back to waiting for players, which frees nothing else. */
static gentity_t *Missile(int weapon) {
 gentity_t *e=G_Spawn(); e->s.eType=ET_MISSILE; e->s.weapon=weapon; e->r.linked=qtrue; return e;
}
static void TestInsertionReset(void) {
 gentity_t *ti,*grenade;
 Reset(2); Live(); ti=Missile(WP_TACTICAL_INSERTION);
 Die(1); G_SNDRunFrame(); CHECK(snd.phase==SND_ROUND_OVER && ti->inuse);
 level.time=snd.deadline; G_SNDRunFrame(); CHECK(snd.phase==SND_COUNTDOWN && snd.round==2 && !ti->inuse);
 ti=Missile(WP_TACTICAL_INSERTION); grenade=Missile(WP_GRENADE_LAUNCHER);
 g_clients[1].pers.connected=CON_DISCONNECTED; G_SNDRunFrame();
 CHECK(snd.phase==SND_WAITING && !ti->inuse && grenade->inuse);
 ti=Missile(WP_TACTICAL_INSERTION); Player(1,TEAM_BLUE); G_SNDRunFrame();
 CHECK(snd.phase==SND_COUNTDOWN && !ti->inuse && !grenade->inuse);
}
int main(void) {
 TestPlantDefuse(); TestInterrupts(); TestCarrierSpectateJoin(); TestWinsAndSides(); TestWaitingPracticeAndMode();
 TestCountdownAndEmptyTeam();
 TestRoundKillcam();
 TestRoundEndWindow();
 TestObjectiveWindows();
 TestInsertionReset();
 printf("PASS: %i S&D round, bomb, timing, spectating, late-join and side-switch checks\n",checks);
 return 0;
}
