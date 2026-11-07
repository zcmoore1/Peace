/* The server half of the client-side bullets test (bullets_test.c): the real
 * ClientThink (g_active.c), so the real Pmove and ClientEvents run each
 * command, the real FireWeapon (g_weapon.c) and G_Damage (g_combat.c). Every
 * shot's ray trace is recorded where it ends; the world is bullets_world.c's.
 * Only engine traps and modules outside these files are stubbed, as in
 * offhand_server_test.c. */
#include "../../code/game/g_local.h"
#include "bullets_world.h"
#include "bullets.h"
static usercmd_t nextCmd;
static qboolean sndCanDamage;
void trap_GetUsercmd(int n,usercmd_t *cmd) { (void)n; *cmd=nextCmd; }
void trap_Cvar_Set(const char *name,const char *value) { (void)name;(void)value; }
void trap_Cvar_Update(vmCvar_t *c) { (void)c; }
void BotTestSolid(vec3_t o) { (void)o; }
qboolean G_SNDClientThink(gentity_t *e,usercmd_t *c) { (void)e;(void)c; return qfalse; }
qboolean G_SNDClientEndFrame(gentity_t *e) { (void)e; return qfalse; }
qboolean G_SNDCanRespawn(void) { return qtrue; }
void Cmd_Kill_f(gentity_t *ent);	/* g_cmds.c's, which g_local.h does not declare */
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
static gclient_t cl[BS_CLIENTS];
vmCvar_t g_gametype, g_weaponRespawn, g_weaponTeamRespawn, g_smoothClients, g_friendlyFire, g_debugDamage,
 g_knockback, g_inactivity, g_quadfactor, g_forcerespawn, g_teamForceBalance, g_maxGameClients, g_dmflags,
 g_blood, g_cheats, g_teamAutoJoin, g_singlePlayer, g_doWarmup, g_warmup, g_motd, g_synchronousClients,
 g_speed, g_gravity, g_allowVote, g_restarted, g_logfile, pmove_fixed, pmove_msec, g_filterBan, g_password,
 g_needpass, g_maxclients, g_timelimit, g_fraglimit, g_capturelimit, g_obeliskHealth, g_obeliskRegenPeriod,
 g_obeliskRegenAmount, g_obeliskRespawnDelay, g_cubeTimeout, g_redteam, g_blueteam, g_enableDust,
 g_enableBreath, g_proxMineTimeout, g_rankings, g_listEntity, g_banIPs, g_dedicated, g_debugMove,
 g_debugAlloc, g_weaponDebug, g_inactivityWarning;
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_Error(int code,const char *fmt,...) { va_list a; (void)code; va_start(a,fmt); vfprintf(stderr,fmt,a); va_end(a); abort(); }
void QDECL G_Error(const char *fmt,...) { va_list a; va_start(a,fmt); vfprintf(stderr,fmt,a); va_end(a); abort(); }
void QDECL G_Printf(const char *fmt,...) { (void)fmt; }
void QDECL G_LogPrintf(const char *fmt,...) { (void)fmt; }

/* ---- the world (bullets_world.c) and the linked entities ---- */
bwMark_t bs_marks[BW_MAX_MARKS];
int bs_markCount, bs_shotTraces;
void trap_Trace(trace_t *t,const vec3_t start,const vec3_t mins,const vec3_t maxs,const vec3_t end,int pass,int mask) {
 int i, passOwner=-1;
 BW_TraceWorld(t,start,mins,maxs,end,mask);
 if(pass!=ENTITYNUM_NONE && g_entities[pass].r.ownerNum!=ENTITYNUM_NONE) passOwner=g_entities[pass].r.ownerNum;
 for(i=0;i<level.num_entities;i++) {
  gentity_t *e=&g_entities[i]; vec3_t lo,hi;
  if(!e->inuse || !e->r.linked || !(e->r.contents&mask)) continue;
  if(pass!=ENTITYNUM_NONE && (i==pass || e->r.ownerNum==pass || e->r.ownerNum==passOwner)) continue;
  VectorAdd(e->r.currentOrigin,e->r.mins,lo); VectorAdd(e->r.currentOrigin,e->r.maxs,hi);
  BW_Sweep(t,start,end,mins,maxs,lo,hi,i,e->r.contents,0);
 }
 BW_Finish(t,start,end);
 /* A shot's trace (FireWeapon's are rays against MASK_SHOT): where it ends,
  * if anything is to be seen there - the server's own impact rule. */
 if(!mins && !maxs && mask==MASK_SHOT) {
  bs_shotTraces++;
  if(bs_markCount<BW_MAX_MARKS && t->entityNum!=ENTITYNUM_NONE && !(t->surfaceFlags&SURF_NOIMPACT)) {
   bwMark_t *m=&bs_marks[bs_markCount++];
   VectorCopy(t->endpos,m->point); m->entityNum=t->entityNum; m->flesh=t->entityNum<MAX_CLIENTS;
  }
 }
}
void trap_LinkEntity(gentity_t *e) {
 e->r.linked=qtrue; VectorAdd(e->r.currentOrigin,e->r.mins,e->r.absmin); VectorAdd(e->r.currentOrigin,e->r.maxs,e->r.absmax);
}
void trap_UnlinkEntity(gentity_t *e) { e->r.linked=qfalse; }
static qboolean Overlap(const vec3_t mins,const vec3_t maxs,const gentity_t *e) {
 int k;
 for(k=0;k<3;k++) if(mins[k]>e->r.absmax[k] || maxs[k]<e->r.absmin[k]) return qfalse;
 return qtrue;
}
int trap_EntitiesInBox(const vec3_t mins,const vec3_t maxs,int *list,int maxcount) {
 int i,n=0;
 for(i=0;i<level.num_entities && n<maxcount;i++)
  if(g_entities[i].inuse && g_entities[i].r.linked && Overlap(mins,maxs,&g_entities[i])) list[n++]=i;
 return n;
}
qboolean trap_EntityContact(const vec3_t mins,const vec3_t maxs,const gentity_t *e) { return Overlap(mins,maxs,e); }
int trap_PointContents(const vec3_t p,int pass) { (void)pass; return BW_PointContents(p); }

/* ---- the rest of the engine and modules these files call ---- */
void trap_GetUserinfo(int n,char *b,int s) { (void)n; if(s>0) b[0]=0; }
void trap_SetUserinfo(int n,const char *b) { (void)n;(void)b; }
void trap_SetConfigstring(int n,const char *s) { (void)n;(void)s; }
void trap_GetConfigstring(int n,char *b,int s) { (void)n; if(s>0) b[0]=0; }
void trap_SendServerCommand(int n,const char *s) { (void)n;(void)s; }
void trap_LocateGameData(gentity_t *g,int n,int s,playerState_t *c,int cs) { (void)g;(void)n;(void)s;(void)c;(void)cs; }
void trap_SnapVector(float *v) { v[0]=rintf(v[0]); v[1]=rintf(v[1]); v[2]=rintf(v[2]); }
void trap_DropClient(int n,const char *r) { (void)n;(void)r; }
void trap_SendConsoleCommand(int when,const char *t) { (void)when;(void)t; }
int trap_BotAllocateClient(void) { return -1; }
void trap_BotFreeClient(int n) { (void)n; }
qboolean G_SNDWaitForRound(gentity_t *e) { (void)e; return qfalse; }
void G_SNDClientSpawned(gentity_t *e) { (void)e; }
void G_SNDClientDied(gentity_t *e) { (void)e; }
qboolean G_SNDCanDamage(gentity_t *e) { (void)e; return sndCanDamage; }	/* MW2's S&D between rounds */
qboolean G_SNDUsable(gentity_t *e) { (void)e; return qfalse; }
qboolean G_SNDUse(gentity_t *e,usercmd_t *c) { (void)e;(void)c; return qfalse; }
team_t G_SNDSpawnTeam(team_t t) { return t; }
void G_SNDPlayerKilled(gentity_t *v,gentity_t *a) { (void)v;(void)a; }
void G_SNDRecordKill(gentity_t *v,gentity_t *a) { (void)v;(void)a; }
#ifdef MISSIONPACK
qboolean CheckObeliskAttack(gentity_t *o,gentity_t *a) { (void)o;(void)a; return qfalse; }
gentity_t *neutralObelisk;
void DropPortalSource(gentity_t *e) { (void)e; }
void DropPortalDestination(gentity_t *e) { (void)e; }
#endif
/* g_team.c's, as it is */
qboolean OnSameTeam(gentity_t *a,gentity_t *b) {
 return a->client && b->client && g_gametype.integer>=GT_TEAM && a->client->sess.sessionTeam==b->client->sess.sessionTeam;
}
static vec3_t ctfSpot={600,600,33};
gentity_t *SelectCTFSpawnPoint(team_t team,int state,vec3_t origin,vec3_t angles,qboolean bot) {
 (void)team;(void)state;(void)bot; VectorCopy(ctfSpot,origin); VectorSet(angles,0,180,0); return NULL;
}
void MoveClientToIntermission(gentity_t *e) { (void)e; }
void FindIntermissionPoint(void) { }
void CalculateRanks(void) { }
int BotAIShutdownClient(int n,qboolean r) { (void)n;(void)r; return 0; }
void G_RemoveQueuedBotBegin(int n) { (void)n; }
void BotTestAAS(vec3_t o) { (void)o; }
void G_CheckBotSpawn(void) { }
qboolean G_FilterPacket(char *from) { (void)from; return qfalse; }
qboolean G_BotConnect(int n,qboolean restart) { (void)n;(void)restart; return qtrue; }
void G_InitSessionData(gclient_t *c,char *u) { (void)c;(void)u; }
void G_ReadSessionData(gclient_t *c) { (void)c; }
void G_WriteClientSessionData(gclient_t *c) { (void)c; }
void Team_CheckHurtCarrier(gentity_t *t,gentity_t *a) { (void)t;(void)a; }
void Team_FragBonuses(gentity_t *t,gentity_t *i,gentity_t *a) { (void)t;(void)i;(void)a; }
void Team_ReturnFlag(int team) { (void)team; }
void Team_CheckDroppedItem(gentity_t *e) { (void)e; }
void Team_DroppedFlagThink(gentity_t *e) { (void)e; }
int Pickup_Team(gentity_t *e,gentity_t *o) { (void)e;(void)o; return 1; }
void SetLeader(int team,int client) { (void)team;(void)client; }
void CheckTeamLeader(int team) { (void)team; }
void AddTournamentQueue(gclient_t *c) { (void)c; }
/* g_mover.c's and g_misc.c's: nothing here is touched or teleported */
void Touch_DoorTrigger(gentity_t *e,gentity_t *o,trace_t *t) { (void)e;(void)o;(void)t; }
void TeleportPlayer(gentity_t *e,vec3_t o,vec3_t a) { (void)e;(void)o;(void)a; }
/* g_main.c's */
void G_RunThink(gentity_t *e) {
 if(e->nextthink<=0 || e->nextthink>level.time) return;
 e->nextthink=0; if(e->think) e->think(e);
}


/* ---- the server, as the test drives it ---- */
void BS_Reset( int gametype ) {
 int i;
 memset(&level,0,sizeof(level)); memset(g_entities,0,sizeof(g_entities)); memset(cl,0,sizeof(cl));
 level.time=BS_START_TIME; level.previousTime=level.time-8; level.num_entities=MAX_CLIENTS; level.maxclients=BS_CLIENTS;
 level.clients=cl; level.gentities=g_entities;
 g_gametype.integer=gametype; g_knockback.integer=0; g_speed.value=320; g_gravity.value=DEFAULT_GRAVITY;
 g_smoothClients.integer=1; g_quadfactor.value=3;
 pmove_msec.integer=8; pmove_fixed.integer=0; sndCanDamage=qtrue;
 for(i=0;i<BS_CLIENTS;i++) {
  gentity_t *e=&g_entities[i];
  e->client=&cl[i]; e->inuse=qtrue; e->s.number=i; e->classname="player"; e->r.ownerNum=ENTITYNUM_NONE;
  cl[i].ps.clientNum=i; cl[i].pers.connected=CON_CONNECTED; cl[i].pers.enterTime=100+i;
  cl[i].pers.maxHealth=100; cl[i].sess.sessionTeam=i==0 ? TEAM_RED : TEAM_BLUE;
  BG_ApplyClass(&cl[i].ps,BG_Class(0)); cl[i].pers.selectedLoadout=*BG_Class(0);
  cl[i].ps.stats[STAT_MAX_HEALTH]=100;
  cl[i].ps.commandTime=level.time; cl[i].lastCmdTime=level.time; cl[i].useHold=ENTITYNUM_NONE;
  e->health=cl[i].ps.stats[STAT_HEALTH]=BS_HEALTH; e->takedamage=qtrue; e->r.contents=CONTENTS_BODY;
  cl[i].ps.pm_type=PM_NORMAL; cl[i].ps.viewheight=DEFAULT_VIEWHEIGHT;
  cl[i].ps.gravity=g_gravity.value; cl[i].ps.speed=g_speed.value;	/* as every ClientThink_real leaves them */
  VectorSet(e->r.mins,-PLAYER_WIDTH,-PLAYER_WIDTH,MINS_Z); VectorSet(e->r.maxs,PLAYER_WIDTH,PLAYER_WIDTH,DEFAULT_HEIGHT);
  e->die=player_die;
  cl[i].ps.groundEntityNum=ENTITYNUM_WORLD;
  BS_Place(i,0,i*200,24);
 }
 InitBodyQue();
 BS_ClearMarks();
}
void BS_Place( int n, float x, float y, float z ) {
 gentity_t *e=&g_entities[n];
 VectorSet(cl[n].ps.origin,x,y,z);
 VectorCopy(cl[n].ps.origin,e->r.currentOrigin); VectorCopy(cl[n].ps.origin,e->s.pos.trBase);
 trap_LinkEntity(e);
 BG_PlayerStateToEntityStateExtraPolate(&cl[n].ps,&e->s,cl[n].ps.commandTime,qtrue);
}
void BS_Arm( int n, int weapon ) {
 playerState_t *ps=&cl[n].ps;
 BG_SetSlotWeapon(ps,SLOT_PRIMARY,weapon);
 BG_SelectSlot(ps,SLOT_PRIMARY);
 ps->weapon=weapon; ps->weaponstate=WEAPON_READY; ps->weaponTime=0; ps->weaponAnimTime=-1;
 ps->stats[STAT_UNCHAMBERED]=0;
 BG_RebuildWeaponMask(ps);
 g_entities[n].s.weapon=weapon;
}
playerState_t *BS_PlayerState( int n ) { return &cl[n].ps; }
int BS_Health( int n ) { return g_entities[n].health; }
void BS_Command( int n, const usercmd_t *cmd ) {
 if(cmd->serverTime>level.time) { level.previousTime=level.time; level.time=cmd->serverTime; }
 { int before=cl[n].ps.eventSequence, tr=bs_shotTraces;
 nextCmd=*cmd;
 ClientThink(n);
 if(getenv("BULLETS_TRACE")) printf("sv cmd t=%d seq %d->%d traces+%d ev=%d,%d\n",cmd->serverTime,before,cl[n].ps.eventSequence,bs_shotTraces-tr,cl[n].ps.events[0],cl[n].ps.events[1]); }
}
void BS_EndFrame( void ) { int i; for(i=0;i<BS_CLIENTS;i++) ClientEndFrame(&g_entities[i]); }
/* An impact or tracer the server sent: any such temp entity in the world now. */
int BS_EffectsSent( void ) {
 int i, n=0;
 for(i=MAX_CLIENTS;i<level.num_entities;i++) {
  gentity_t *e=&g_entities[i]; int ev;
  if(!e->inuse || e->s.eType<ET_EVENTS) continue;
  ev=e->s.eType-ET_EVENTS;
  if(ev==EV_BULLET_HIT_FLESH || ev==EV_BULLET_HIT_WALL || ev==EV_SHOTGUN || ev==EV_RAILTRAIL ||
     ev==EV_MISSILE_HIT || ev==EV_MISSILE_MISS || ev==EV_MISSILE_MISS_METAL || ev==EV_LIGHTNINGBOLT) n++;
 }
 for(i=0;i<MAX_CLIENTS;i++) {
  int ev=g_entities[i].s.event&~EV_EVENT_BITS;
  if(ev==EV_BULLET_HIT_FLESH || ev==EV_BULLET_HIT_WALL || ev==EV_SHOTGUN || ev==EV_RAILTRAIL) n++;
 }
 return n;
}
int BS_Missiles( void ) {
 int i, n=0;
 for(i=MAX_CLIENTS;i<level.num_entities;i++) if(g_entities[i].inuse && g_entities[i].s.eType==ET_MISSILE) n++;
 return n;
}
/* What a viewer's snapshot carries: every entity but their own, a temp entity
 * once (then freed, as the engine sends it once), none meant only for others. */
int BS_Snapshot( int viewer, entityState_t *out, int max ) {
 int i, n=0;
 for(i=0;i<level.num_entities && n<max;i++) {
  gentity_t *e=&g_entities[i];
  if(!e->inuse || i==viewer || !e->r.linked) continue;
  if((e->r.svFlags&SVF_NOTSINGLECLIENT) && e->r.singleClient==viewer) continue;
  out[n]=e->s;
  if(e->r.contents&(CONTENTS_SOLID|CONTENTS_BODY)) {	/* SV_LinkEntity's encoded box */
   int x=(int)e->r.maxs[0], zd=(int)-e->r.mins[2], zu=(int)e->r.maxs[2]+32;
   out[n].solid=(zu<<16)|(zd<<8)|x;
  }
  n++;
 }
 return n;
}
void BS_FreeTempEntities( void ) {
 int i;
 for(i=MAX_CLIENTS;i<level.num_entities;i++) if(g_entities[i].inuse && g_entities[i].freeAfterEvent) G_FreeEntity(&g_entities[i]);
}
void BS_ClearMarks( void ) { bs_markCount=0; bs_shotTraces=0; }
