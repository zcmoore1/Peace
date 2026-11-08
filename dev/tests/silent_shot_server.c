/* The server half of the silent shot test (silent_shot_test.c): the real game
 * module's ClientThink (g_active.c: Pmove, BG_PlayerStateToEntityState,
 * ClientEvents), FireWeapon (g_weapon.c), G_Damage (g_combat.c), the thrown
 * knife (g_missile.c), G_AddEvent and G_AgeEntityEvents (g_utils.c) and
 * ClientEndFrame. SS_RunFrame is G_RunFrame's own entity pass (g_main.c
 * 1802-1874), copied because g_main.c is the whole module's entry point; every
 * function it calls is the real one. The world is bullets_world.c's.
 *
 * Built apart from the cgame with -Dtrap_SendConsoleCommand=..., because the
 * game's and cgame's traps of that name take different arguments; everything
 * else the two modules share in one binary is the same function. */
#include "../../code/game/g_local.h"
#include "bullets_world.h"
#include "silent_shot.h"

static usercmd_t nextCmd;
void trap_GetUsercmd(int n,usercmd_t *cmd) { (void)n; *cmd=nextCmd; }
void trap_Cvar_Set(const char *name,const char *value) { (void)name;(void)value; }
void trap_Cvar_Update(vmCvar_t *c) { (void)c; }
void BotTestSolid(vec3_t o) { (void)o; }
qboolean G_SNDClientThink(gentity_t *e,usercmd_t *c) { (void)e;(void)c; return qfalse; }
qboolean G_SNDClientEndFrame(gentity_t *e) { (void)e; return qfalse; }
qboolean G_SNDCanRespawn(void) { return qtrue; }
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
static gclient_t clients[SS_CLIENTS];
vmCvar_t g_gametype, g_weaponRespawn, g_weaponTeamRespawn, g_smoothClients, g_friendlyFire, g_debugDamage,
 g_knockback, g_inactivity, g_quadfactor, g_forcerespawn, g_teamForceBalance, g_maxGameClients, g_dmflags,
 g_blood, g_cheats, g_teamAutoJoin, g_singlePlayer, g_doWarmup, g_warmup, g_motd, g_synchronousClients,
 g_speed, g_gravity, g_allowVote, g_restarted, g_logfile, pmove_fixed, pmove_msec, g_filterBan, g_password,
 g_needpass, g_maxclients, g_timelimit, g_fraglimit, g_capturelimit, g_obeliskHealth, g_obeliskRegenPeriod,
 g_obeliskRegenAmount, g_obeliskRespawnDelay, g_cubeTimeout, g_redteam, g_blueteam, g_enableDust,
 g_enableBreath, g_proxMineTimeout, g_rankings, g_listEntity, g_banIPs, g_dedicated, g_debugMove,
 g_debugAlloc, g_weaponDebug, g_inactivityWarning;
void QDECL G_Error(const char *fmt,...) { va_list a; va_start(a,fmt); vfprintf(stderr,fmt,a); va_end(a); abort(); }
void QDECL G_Printf(const char *fmt,...) { (void)fmt; }
void QDECL G_LogPrintf(const char *fmt,...) { (void)fmt; }

/* ---- the world (bullets_world.c) and the linked entities ---- */
static int damageTraces;
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
 /* a shot's ray (FireWeapon's are rays against MASK_SHOT) that ends on
  * something to be seen: the server's own impact rule */
 if(!mins && !maxs && mask==MASK_SHOT && t->entityNum!=ENTITYNUM_NONE && !(t->surfaceFlags&SURF_NOIMPACT)) damageTraces++;
 if(getenv("SILENTSHOT_SVTRACE")) printf("    sv trace %s pass %d mask %x (%.1f %.1f %.1f)->(%.1f %.1f %.1f) hit %d frac %.3f\n",mins?"box":"ray",pass,mask,
  start[0],start[1],start[2],end[0],end[1],end[2],t->entityNum,t->fraction);
}
void trap_LinkEntity(gentity_t *e) {
 e->r.linked=qtrue; VectorAdd(e->r.currentOrigin,e->r.mins,e->r.absmin); VectorAdd(e->r.currentOrigin,e->r.maxs,e->r.absmax);
 if(e->r.contents&(CONTENTS_SOLID|CONTENTS_BODY)) {	/* SV_LinkEntity's encoded box, which cgame traces */
  int x=(int)e->r.maxs[0], zd=(int)-e->r.mins[2], zu=(int)e->r.maxs[2]+32;
  e->s.solid=(zu<<16)|(zd<<8)|x;
 } else e->s.solid=0;
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

/* ---- the rest of the engine and the modules these files call ---- */
void trap_GetUserinfo(int n,char *b,int s) { (void)n; if(s>0) b[0]=0; }
void trap_SetUserinfo(int n,const char *b) { (void)n;(void)b; }
void trap_SetConfigstring(int n,const char *s) { (void)n;(void)s; }
void trap_GetConfigstring(int n,char *b,int s) { (void)n; if(s>0) b[0]=0; }
void trap_SendServerCommand(int n,const char *s) { (void)n;(void)s; }
void trap_LocateGameData(gentity_t *g,int n,int s,playerState_t *c,int cs) { (void)g;(void)n;(void)s;(void)c;(void)cs; }
void trap_SnapVector(float *v) { v[0]=rintf(v[0]); v[1]=rintf(v[1]); v[2]=rintf(v[2]); }
void trap_DropClient(int n,const char *r) { (void)n;(void)r; }
void trap_SendConsoleCommand(int when,const char *t) { (void)when;(void)t; }	/* renamed: the game's */
int trap_BotAllocateClient(void) { return -1; }
void trap_BotFreeClient(int n) { (void)n; }
qboolean G_SNDWaitForRound(gentity_t *e) { (void)e; return qfalse; }
void G_SNDClientSpawned(gentity_t *e) { (void)e; }
void G_SNDClientDied(gentity_t *e) { (void)e; }
qboolean G_SNDCanDamage(gentity_t *e) { (void)e; return qtrue; }
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
/* player_die's final killcam: nothing here reaches a score limit (above) */
int trap_ReplayStart(int killer,int from,int to,int begin) { (void)killer;(void)from;(void)to;(void)begin; abort(); return 0; }
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
void Team_FreeEntity(gentity_t *e) { (void)e; }
int Pickup_Team(gentity_t *e,gentity_t *o) { (void)e;(void)o; return 1; }
void SetLeader(int team,int client) { (void)team;(void)client; }
void CheckTeamLeader(int team) { (void)team; }
void AddTournamentQueue(gclient_t *c) { (void)c; }
/* g_mover.c's and g_misc.c's: nothing here is touched or teleported */
void Touch_DoorTrigger(gentity_t *e,gentity_t *o,trace_t *t) { (void)e;(void)o;(void)t; }
void TeleportPlayer(gentity_t *e,vec3_t o,vec3_t a) { (void)e;(void)o;(void)a; }
void G_RunMover(gentity_t *e) { (void)e; abort(); }	/* there are none */
/* g_main.c's, as it is */
void G_RunThink(gentity_t *e) {
 if(e->nextthink<=0 || e->nextthink>level.time) return;
 e->nextthink=0; if(e->think) e->think(e);
}

/* ---- the server, as the test drives it ---- */
static void Place(int n,float x,float y,float z,float yaw) {
 gentity_t *e=&g_entities[n];
 VectorSet(clients[n].ps.origin,x,y,z);
 clients[n].ps.viewangles[YAW]=yaw;
 VectorCopy(clients[n].ps.origin,e->r.currentOrigin); VectorCopy(clients[n].ps.origin,e->s.pos.trBase);
 trap_LinkEntity(e);
 BG_PlayerStateToEntityStateExtraPolate(&clients[n].ps,&e->s,clients[n].ps.commandTime,qtrue);
}
static int shooterGun;
void SS_Reset( int gun, int lethal, int levelTime ) {
 int i;
 memset(&level,0,sizeof(level)); memset(g_entities,0,sizeof(g_entities)); memset(clients,0,sizeof(clients));
 level.time=levelTime; level.previousTime=levelTime-50; level.num_entities=MAX_CLIENTS; level.maxclients=SS_CLIENTS;
 level.clients=clients; level.gentities=g_entities;
 g_gametype.integer=GT_SND; g_knockback.integer=0; g_speed.value=BG_PLAYER_SPEED; g_gravity.value=DEFAULT_GRAVITY;
 g_smoothClients.integer=1; g_quadfactor.value=3;
 pmove_msec.integer=8; pmove_fixed.integer=0;
 for(i=0;i<SS_CLIENTS;i++) {
  gentity_t *e=&g_entities[i];
  bg_class_t loadout=*BG_Class(0);
  if(i==SS_SHOOTER) {
   loadout.slot[0]=gun; loadout.slot[1]=gun==WP_SHOTGUN ? WP_MACHINEGUN : WP_SHOTGUN;
   loadout.lethal=lethal; loadout.lethalCount=BG_ClassEquipmentCount(lethal);
   if(!BG_ValidateClass(&loadout)) G_Error("SS_Reset: the shooter's class is not valid\n");
  }
  e->client=&clients[i]; e->inuse=qtrue; e->s.number=i; e->classname="player"; e->r.ownerNum=ENTITYNUM_NONE;
  clients[i].ps.clientNum=i; clients[i].pers.connected=CON_CONNECTED; clients[i].pers.enterTime=100+i;
  clients[i].pers.maxHealth=100; clients[i].sess.sessionTeam=i==SS_SHOOTER ? TEAM_RED : TEAM_BLUE;
  BG_ApplyClass(&clients[i].ps,&loadout); clients[i].pers.selectedLoadout=loadout;
  clients[i].ps.stats[STAT_MAX_HEALTH]=100;
  clients[i].ps.commandTime=level.time; clients[i].lastCmdTime=level.time; clients[i].useHold=ENTITYNUM_NONE;
  /* nobody dies of the test's shots (the killcam is the engine's, started as
   * S&D starts it); the health still fits stats[]' signed 16 bits on the wire */
  e->health=clients[i].ps.stats[STAT_HEALTH]=30000; e->takedamage=qtrue; e->r.contents=CONTENTS_BODY;
  clients[i].ps.pm_type=PM_NORMAL; clients[i].ps.viewheight=DEFAULT_VIEWHEIGHT;
  clients[i].ps.gravity=g_gravity.value; clients[i].ps.speed=g_speed.value;
  VectorSet(e->r.mins,-PLAYER_WIDTH,-PLAYER_WIDTH,MINS_Z); VectorSet(e->r.maxs,PLAYER_WIDTH,PLAYER_WIDTH,DEFAULT_HEIGHT);
  e->die=player_die;
  clients[i].ps.groundEntityNum=ENTITYNUM_WORLD;
  clients[i].ps.weaponstate=WEAPON_READY;
 }
 /* the shooter faces +y down the line; the victim stands in it; the watcher looks on from the side */
 Place(SS_SHOOTER,0,0,24,90); Place(SS_VICTIM,0,200,24,270); Place(SS_WATCHER,300,100,24,180);
 InitBodyQue();
 damageTraces=0; shooterGun=gun;
}
void SS_Command( int client, const usercmd_t *cmd, ssThink_t *out ) {
 playerState_t *ps=&clients[client].ps;
 int before=ps->eventSequence, traces=damageTraces, i;
 nextCmd=*cmd;
 ClientThink(client);
 memset(out,0,sizeof(*out));
 out->hits=damageTraces-traces;
 out->events=ps->eventSequence-before;
 for(i=ps->eventSequence-out->events;i<ps->eventSequence;i++) {
  int ev=ps->events[i&(MAX_PS_EVENTS-1)];
  if(ev==EV_FIRE_WEAPON && ps->eventParms[i&(MAX_PS_EVENTS-1)]==shooterGun) out->shots++;	/* the gun's, not a throw */
  if(ev==EV_SWITCH_OFFHAND) out->presses++;
 }
}
void SS_RunFrame( int levelTime ) {
 int i;
 gentity_t *ent;
 level.framenum++;
 level.previousTime=level.time;
 level.time=levelTime;
 for(i=0,ent=g_entities;i<level.num_entities;i++,ent++) {
  if(!ent->inuse) continue;
  G_AgeEntityEvents(ent);
  if(level.time-ent->eventTime>EVENT_VALID_MSEC) {
   if(ent->client) ent->client->ps.externalEvent=0;
   if(ent->freeAfterEvent) { G_FreeEntity(ent); continue; }
   else if(ent->unlinkAfterEvent) { ent->unlinkAfterEvent=qfalse; trap_UnlinkEntity(ent); }
  }
  if(ent->freeAfterEvent) continue;
  if(!ent->r.linked && ent->neverFree) continue;
  if(ent->s.eType==ET_MISSILE) { G_RunMissile(ent); continue; }
  if(ent->s.eType==ET_ITEM || ent->physicsObject) { G_RunItem(ent); continue; }
  if(ent->s.eType==ET_MOVER) { G_RunMover(ent); continue; }
  if(i<MAX_CLIENTS) { G_RunClient(ent); continue; }
  G_RunThink(ent);
 }
 for(i=0,ent=g_entities;i<level.maxclients;i++,ent++) if(ent->inuse) ClientEndFrame(ent);
}
int SS_Health( int client ) { return g_entities[client].health; }
int SS_TempEffects( void ) {
 int i, n=0;
 for(i=MAX_CLIENTS;i<level.num_entities;i++) {
  gentity_t *e=&g_entities[i]; int ev;
  if(!e->inuse || e->s.eType<ET_EVENTS) continue;
  ev=e->s.eType-ET_EVENTS;
  if(ev==EV_BULLET_HIT_FLESH || ev==EV_BULLET_HIT_WALL || ev==EV_SHOTGUN || ev==EV_RAILTRAIL ||
     ev==EV_MISSILE_HIT || ev==EV_MISSILE_MISS || ev==EV_MISSILE_MISS_METAL || ev==EV_LIGHTNINGBOLT) n++;
 }
 return n;
}
void *SS_Entities( int *numEntities, int *entitySize ) {
 *numEntities=level.num_entities; *entitySize=sizeof(gentity_t);
 return g_entities;
}
playerState_t *SS_PlayerState( int client ) { return &clients[client].ps; }

/* ---- the watcher's other views ---- */
/* A spectator following target, as Cmd_Follow_f leaves one: the real
 * SpectatorClientEndFrame copies the target's playerstate at each frame's end,
 * and the snapshot leaves the target's entity out. */
void SS_Follow( int client, int target ) {
 gentity_t *e=&g_entities[client]; gclient_t *c=&clients[client];
 c->sess.sessionTeam=TEAM_SPECTATOR; c->sess.spectatorState=SPECTATOR_FOLLOW; c->sess.spectatorClient=target;
 e->r.svFlags|=SVF_NOCLIENT; e->r.contents=0; e->takedamage=qfalse; trap_UnlinkEntity(e);
}
/* The watcher stands at (x, y, z) instead. */
void SS_Move( int client, float x, float y, float z ) { Place(client,x,y,z,clients[client].ps.viewangles[YAW]); }
/* An S&D player out of the round with no teammate to follow, as
 * G_SNDClientEndFrame (and G_SNDWaitForRound) leaves one: its own view, no
 * health, PM_FREEZE, no weapon, and no body in the world. */
void SS_OutOfRound( int client ) {
 gentity_t *e=&g_entities[client]; playerState_t *ps=&clients[client].ps;
 trap_UnlinkEntity(e); e->r.svFlags|=SVF_NOCLIENT; e->r.contents=0; e->takedamage=qfalse;
 ps->clientNum=client; ps->pm_flags&=~PMF_FOLLOW; ps->pm_type=PM_FREEZE;
 ps->stats[STAT_HEALTH]=e->health=0; ps->weapon=WP_NONE;
}
