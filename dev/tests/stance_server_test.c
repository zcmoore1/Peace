/* MW2's stance through the real server (dev/STANCE.md): a command is the real
 * ClientThink (g_active.c), so the real Pmove runs it and the entity carries
 * what pmove left (BG_PlayerStateToEntityState). Covered: the bullet-only hit
 * volumes (owner's decision D7: BG_PlayerHitVolumes) through the real
 * G_ShotTrace, against a prone body behind its origin and a player whose eye
 * is above their box on a downward leg; a shot past every volume is a miss;
 * a shot from inside a volume that leaves it is a miss, as from the engine's
 * box (two players prone in a file); G_ShotTrace and the shared function
 * agree on any ray, over the box and on the body; the railgun and the knife
 * keep the box (bullets and pellets only). And SetClientViewAngle's prone
 * clamps. Every wait is for a state (an eye at rest, a leg running, a gun
 * ready), never a written time. The world is a floor at z = 0 plus the linked
 * entities, as offhand_server_test.c's. */
#include "../../code/game/g_local.h"
static int checks;
static usercmd_t nextCmd;
void trap_GetUsercmd(int n,usercmd_t *cmd) { (void)n; *cmd=nextCmd; }
void trap_Cvar_Set(const char *name,const char *value) { (void)name;(void)value; }
void trap_Cvar_Update(vmCvar_t *c) { (void)c; }
void BotTestSolid(vec3_t o) { (void)o; }
qboolean G_SNDClientThink(gentity_t *e,usercmd_t *c) { (void)e;(void)c; return qfalse; }
qboolean G_SNDClientEndFrame(gentity_t *e) { (void)e; return qfalse; }
qboolean G_SNDCanRespawn(void) { return qtrue; }
void Cmd_Kill_f(gentity_t *ent);	/* g_cmds.c's, which g_local.h does not declare */
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define NEAR(a,b) (fabs((a)-(b))<0.01f)
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
static gclient_t cl[2];
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
static char lastLog[256];	/* the game log's last line */
void QDECL G_LogPrintf(const char *fmt,...) { va_list a; va_start(a,fmt); Q_vsnprintf(lastLog,sizeof(lastLog),fmt,a); va_end(a); }

/* ---- the world: a floor at z = 0 and the linked entities ---- */
/* a box (mins/maxs) swept from s to e against [lo, hi]: the Minkowski box against the segment */
static void Sweep(trace_t *t,const vec3_t s,const vec3_t e,const vec3_t mins,const vec3_t maxs,
 const vec3_t lo,const vec3_t hi,int num,int contents) {
 float enter=-1,leave=1; vec3_t normal={0,0,0},a,b; int k; qboolean inside=qtrue;
 for(k=0;k<3;k++) { a[k]=lo[k]-maxs[k]; b[k]=hi[k]-mins[k]; if(!(s[k]>a[k] && s[k]<b[k])) inside=qfalse; }
 if(inside) {
  if(t->fraction>0 || !t->startsolid) {
   t->startsolid=qtrue; t->fraction=0; t->entityNum=num; t->contents=contents; VectorClear(t->plane.normal);
   t->allsolid=e[0]>a[0] && e[0]<b[0] && e[1]>a[1] && e[1]<b[1] && e[2]>a[2] && e[2]<b[2];
  }
  return;
 }
 for(k=0;k<3;k++) {
  float d=e[k]-s[k], t0, t1, side=-1;
  if(d==0) { if(s[k]<=a[k] || s[k]>=b[k]) return; continue; }
  t0=(a[k]-s[k])/d; t1=(b[k]-s[k])/d;
  if(t0>t1) { float x=t0; t0=t1; t1=x; side=1; }
  if(t0>enter) { enter=t0; VectorClear(normal); normal[k]=side; }
  if(t1<leave) leave=t1;
  if(enter>=leave) return;
 }
 if(enter<0 || enter>1) return;
 if(enter<t->fraction) { t->fraction=enter; VectorCopy(normal,t->plane.normal); t->entityNum=num; t->contents=contents; }
}
void trap_Trace(trace_t *t,const vec3_t start,const vec3_t mins,const vec3_t maxs,const vec3_t end,int pass,int mask) {
 static const vec3_t zero={0,0,0};
 vec3_t floorLo={-100000,-100000,-100000}, floorHi={100000,100000,0};
 int i, passOwner=-1;
 if(!mins) mins=zero;
 if(!maxs) maxs=zero;
 memset(t,0,sizeof(*t)); t->fraction=1; t->entityNum=ENTITYNUM_NONE;
 if(mask&(CONTENTS_SOLID|CONTENTS_PLAYERCLIP))
  Sweep(t,start,end,mins,maxs,floorLo,floorHi,ENTITYNUM_WORLD,CONTENTS_SOLID);
 if(pass!=ENTITYNUM_NONE && g_entities[pass].r.ownerNum!=ENTITYNUM_NONE) passOwner=g_entities[pass].r.ownerNum;
 for(i=0;i<level.num_entities;i++) {
  gentity_t *e=&g_entities[i]; vec3_t lo,hi;
  if(!e->inuse || !e->r.linked || !(e->r.contents&mask)) continue;
  if(pass!=ENTITYNUM_NONE && (i==pass || e->r.ownerNum==pass || e->r.ownerNum==passOwner)) continue;
  VectorAdd(e->r.currentOrigin,e->r.mins,lo); VectorAdd(e->r.currentOrigin,e->r.maxs,hi);
  Sweep(t,start,end,mins,maxs,lo,hi,i,e->r.contents);
 }
 for(i=0;i<3;i++) t->endpos[i]=start[i]+t->fraction*(end[i]-start[i]);
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
int trap_PointContents(const vec3_t p,int pass) { (void)pass; return p[2]<0 ? CONTENTS_SOLID : 0; }

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
qboolean G_SNDCanDamage(gentity_t *e) { (void)e; return qtrue; }	/* MW2's S&D between rounds */
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


/* ---- the fixture: a shooter and a target, both real clients ---- */
#define SHOOTER 0
#define TARGET 1
#define CMD_MSEC 8
static usercmd_t cmds[2];
static void Link(gentity_t *e) {
 VectorCopy(e->client->ps.origin,e->r.currentOrigin); VectorCopy(e->client->ps.origin,e->s.pos.trBase); trap_LinkEntity(e);
}
static void Place(int n,float x,float y) {
 VectorSet(cl[n].ps.origin,x,y,-MINS_Z+0.125f); cl[n].ps.groundEntityNum=ENTITYNUM_WORLD; Link(&g_entities[n]);
}
static void Reset(void) {
 int i;
 memset(&level,0,sizeof(level)); memset(g_entities,0,sizeof(g_entities)); memset(cl,0,sizeof(cl)); memset(cmds,0,sizeof(cmds));
 level.time=10000; level.previousTime=level.time-CMD_MSEC; level.num_entities=MAX_CLIENTS; level.maxclients=2;
 level.clients=cl; level.gentities=g_entities;
 g_gametype.integer=GT_FFA; g_knockback.integer=1000; g_speed.value=BG_PLAYER_SPEED; g_gravity.value=DEFAULT_GRAVITY;
 pmove_msec.integer=8; pmove_fixed.integer=0;
 for(i=0;i<2;i++) {
  gentity_t *e=&g_entities[i]; bg_class_t c=*BG_Class(0);
  c.slot[0]=WP_MACHINEGUN; c.slot[1]=WP_SHOTGUN;
  CHECK(BG_ValidateClass(&c));
  e->client=&cl[i]; e->inuse=qtrue; e->s.number=i; e->s.eType=ET_PLAYER; e->classname="player"; e->r.ownerNum=ENTITYNUM_NONE;
  cl[i].ps.clientNum=i; cl[i].pers.connected=CON_CONNECTED; cl[i].pers.enterTime=100+i;
  cl[i].pers.selectedLoadout=c; cl[i].pers.maxHealth=100; cl[i].sess.sessionTeam=TEAM_FREE;
  BG_ApplyClass(&cl[i].ps,&c); cl[i].ps.stats[STAT_MAX_HEALTH]=100;
  BG_SpawnStance(&cl[i].ps);
  cl[i].ps.commandTime=level.time; cl[i].lastCmdTime=level.time; cl[i].useHold=ENTITYNUM_NONE;
  e->health=cl[i].ps.stats[STAT_HEALTH]=100; e->takedamage=qtrue; e->r.contents=CONTENTS_BODY; e->die=player_die;
  cl[i].ps.pm_type=PM_NORMAL; cl[i].ps.speed=BG_PLAYER_SPEED; cl[i].ps.gravity=DEFAULT_GRAVITY;
  VectorSet(e->r.mins,-PLAYER_WIDTH,-PLAYER_WIDTH,MINS_Z); VectorSet(e->r.maxs,PLAYER_WIDTH,PLAYER_WIDTH,DEFAULT_HEIGHT);
 }
 Place(SHOOTER,0,0); Place(TARGET,200,0);
 InitBodyQue();
}
/* one server frame: each client's command (its own buttons, view and stick) */
static void Frame(void) {
 int i;
 level.previousTime=level.time; level.time+=CMD_MSEC;
 for(i=0;i<2;i++) { nextCmd=cmds[i]; nextCmd.serverTime=level.time; ClientThink(i); }
 for(i=0;i<2;i++) ClientEndFrame(&g_entities[i]);
}
static void Frames(int msec) { int t; for(t=0;t<msec;t+=CMD_MSEC) Frame(); }
static void Aim(int n,float yaw,float pitch) {
 cmds[n].angles[YAW]=ANGLE2SHORT(yaw)-cl[n].ps.delta_angles[YAW];
 cmds[n].angles[PITCH]=ANGLE2SHORT(pitch)-cl[n].ps.delta_angles[PITCH];
}
static gentity_t *Ent(int n) { return &g_entities[n]; }
/* n's eye at rest at a stance's (BG_EYE_*), no leg running */
static qboolean Rested(int n,int eye) { return cl[n].ps.viewHeightTarget==eye && !cl[n].ps.viewHeightLerpTarget; }
/* frames until both eyes rest where asked (the bound is only against a hang) */
static void Rest(int shooterEye,int targetEye) {
 int t;
 for(t=0;t<1000 && !(Rested(SHOOTER,shooterEye) && Rested(TARGET,targetEye));t++) Frame();
 CHECK(Rested(SHOOTER,shooterEye) && Rested(TARGET,targetEye));
}
/* frames until n's gun is up */
static void Ready(int n) {
 int t;
 for(t=0;t<1000 && cl[n].ps.weaponstate!=WEAPON_READY;t++) Frame();
 CHECK(cl[n].ps.weaponstate==WEAPON_READY);
}
/* n's hit volumes, from its entity and the box the server traces */
static int Volumes(int n,bgHitVolume_t vols[BG_MAX_HIT_VOLUMES]) {
 return BG_PlayerHitVolumes(&Ent(n)->s,Ent(n)->r.currentOrigin,Ent(n)->r.mins,Ent(n)->r.maxs,vols);
}
static int VolumeOf(const bgHitVolume_t *vols,int count,int type) {
 int v; for(v=0;v<count;v++) if(vols[v].type==type) return v; return -1;
}
static qboolean InVolume(const bgHitVolume_t *v,const vec3_t p) {
 vec3_t rel; int k;
 VectorSubtract(p,v->center,rel);
 for(k=0;k<3;k++) if(fabs(DotProduct(rel,v->axis[k]))>=v->halfSize[k]) return qfalse;
 return qtrue;
}

/* What G_ShotTrace must give: the engine's boxes, then every linked
 * player's extra volumes from the shared function, the nearest. */
static void Expected(const vec3_t start,const vec3_t end,int pass,int *ent,float *fraction) {
 trace_t tr; bgHitVolume_t vols[BG_MAX_HIT_VOLUMES]; vec3_t n; float f; int i,v,count;
 trap_Trace(&tr,start,NULL,NULL,end,pass,MASK_SHOT);
 *ent=tr.entityNum; *fraction=tr.fraction;
 for(i=0;i<2;i++) {
  if(i==pass || !Ent(i)->r.linked) continue;
  count=BG_PlayerHitVolumes(&Ent(i)->s,Ent(i)->r.currentOrigin,Ent(i)->r.mins,Ent(i)->r.maxs,vols);
  for(v=1;v<count;v++) if(BG_HitVolumeTrace(&vols[v],start,end,&f,n) && f<*fraction) { *fraction=f; *ent=i; }
 }
}
static float shotFraction;	/* the last Shot's */
static int Shot(const vec3_t start,const vec3_t end) {
 trace_t tr; int ent; float fraction;
 G_ShotTrace(&tr,start,end,SHOOTER);
 Expected(start,end,SHOOTER,&ent,&fraction);
 CHECK(tr.entityNum==ent && tr.fraction==fraction);	/* the server and the shared function agree */
 shotFraction=tr.fraction;
 if(tr.entityNum==TARGET) {
  int k; for(k=0;k<3;k++) CHECK(NEAR(tr.endpos[k],start[k]+tr.fraction*(end[k]-start[k])));
 }
 return tr.fraction<1 ? tr.entityNum : ENTITYNUM_NONE;
}
static int HorizontalShot(float x,float z) {	/* across the target's line, from the side */
 vec3_t a,b; VectorSet(a,x,-300,z); VectorSet(b,x,300,z); return Shot(a,b);
}

/* The prone body behind the origin and the leg over the box, through the real
 * G_ShotTrace (Bullet_Fire's and ShotgunPellet's trace). */
static void TestHitVolumes(void) {
 float feet, top, eye; int t, hit;
 bgHitVolume_t vols[BG_MAX_HIT_VOLUMES];
 Reset();
 cmds[TARGET].buttons=BUTTON_PRONE;
 Rest(BG_EYE_STAND,BG_EYE_PRONE);
 CHECK(cl[TARGET].ps.pm_flags&PMF_PRONE && Ent(TARGET)->r.maxs[2]==PRONE_HEIGHT && (Ent(TARGET)->s.eFlags&EF_PRONE));
 CHECK(Ent(TARGET)->s.origin2[0]==cl[TARGET].ps.proneDirection && cl[TARGET].ps.proneDirection==0);
 feet=Ent(TARGET)->r.currentOrigin[2]+MINS_Z; top=Ent(TARGET)->r.currentOrigin[2]+PRONE_HEIGHT;
 /* the legs, behind the box (the body lies along -x from x 200), are hit */
 CHECK(HorizontalShot(200-PLAYER_WIDTH-5,feet+5)==TARGET);
 CHECK(HorizontalShot(200-BG_PRONE_FEET_DIST+2,feet+15)==TARGET);
 /* past the feet, above the body, or the world beside: a miss */
 CHECK(HorizontalShot(200-BG_PRONE_FEET_DIST-2,feet+5)==ENTITYNUM_NONE);
 CHECK(HorizontalShot(200-PLAYER_WIDTH-5,top+1)==ENTITYNUM_NONE);
 /* the box is still a box */
 CHECK(HorizontalShot(200,feet+5)==TARGET);
 /* a shot along the body, just beside it (the body is 12 wide): it passes
  * the legs and stops at the box */
 { vec3_t a,b; trace_t tr; VectorSet(a,0,BG_PRONE_TRACE_HALF+1,feet+5); VectorSet(b,400,BG_PRONE_TRACE_HALF+1,feet+5);
   CHECK(Shot(a,b)==TARGET); G_ShotTrace(&tr,a,b,SHOOTER);
   CHECK(NEAR(tr.endpos[0],200-PLAYER_WIDTH));
   a[1]=b[1]=BG_PRONE_TRACE_HALF-1; G_ShotTrace(&tr,a,b,SHOOTER);	/* inside it: the feet first */
   CHECK(tr.entityNum==TARGET && NEAR(tr.endpos[0],200-BG_PRONE_FEET_DIST)); }
 /* the real railgun (straight, no spread) keeps the box: a ray at the legs that
  * hits them as a bullet does the rail no damage (bullets and pellets only) */
 cl[SHOOTER].ps.stats[STAT_WEAPONS]|=1<<WP_RAILGUN; cl[SHOOTER].ps.ammo[WP_RAILGUN]=5;
 cl[SHOOTER].ps.weapon=WP_RAILGUN; cl[SHOOTER].ps.weaponstate=WEAPON_READY;
 cl[SHOOTER].ps.stats[STAT_SLOT_PRIMARY]=WP_RAILGUN; cmds[SHOOTER].weapon=WP_RAILGUN;
 Place(SHOOTER,175,-200);
 {
  vec3_t muzzle, fwd; float pitch;
  /* down from the eye to the legs at y = 0: a rail that never meets the box */
  pitch=RAD2DEG(atan2(Ent(SHOOTER)->s.pos.trBase[2]+cl[SHOOTER].ps.viewheight-(feet+10),200-14));
  Aim(SHOOTER,90,pitch); cmds[SHOOTER].buttons=0; Frame();
  AngleVectors(cl[SHOOTER].ps.viewangles,fwd,NULL,NULL);
  BG_ShotMuzzle(Ent(SHOOTER)->s.pos.trBase,cl[SHOOTER].ps.viewheight,fwd,muzzle);
  { vec3_t far; VectorMA(muzzle,8192,fwd,far); CHECK(Shot(muzzle,far)==TARGET); }	/* a bullet's trace: the legs */
  cmds[SHOOTER].buttons=BUTTON_ATTACK; Frame(); cmds[SHOOTER].buttons=0;
  CHECK(cl[SHOOTER].ps.ammo[WP_RAILGUN]==4);
  CHECK(Ent(TARGET)->health==100);
 }
 /* the knife keeps the box: from behind the feet, down into the legs */
 Reset(); cmds[TARGET].buttons=BUTTON_PRONE; Rest(BG_EYE_STAND,BG_EYE_PRONE);
 Place(SHOOTER,200-BG_PRONE_FEET_DIST-30,0);
 Aim(SHOOTER,0,45); Frame(); Ready(SHOOTER);
 CHECK(NEAR(cl[SHOOTER].ps.viewangles[PITCH],45));
 { vec3_t a,b,fwd; AngleVectors(cl[SHOOTER].ps.viewangles,fwd,NULL,NULL);
   BG_ShotMuzzle(Ent(SHOOTER)->s.pos.trBase,cl[SHOOTER].ps.viewheight,fwd,a); VectorMA(a,64,fwd,b);
   CHECK(Shot(a,b)==TARGET); }	/* a bullet there hits the legs... */
 { int knifed=0;
   cmds[SHOOTER].buttons=BUTTON_MELEE;
   for(t=0;t<BG_MELEE_DELAY+2*CMD_MSEC;t+=CMD_MSEC) { Frame(); knifed|=(cl[SHOOTER].ps.weaponAction&WA_TYPE_MASK)==WA_MELEE; }
   cmds[SHOOTER].buttons=0; Frames(BG_MELEE_TIME);
   CHECK(knifed); }	/* the knife came out and swung... */
 CHECK(Ent(TARGET)->health==100);	/* ...and does not hit the legs */

 /* a leg down: while the eye is over the new box, over the box is hit */
 Reset();
 cmds[TARGET].buttons=BUTTON_CROUCH; Frame();
 CHECK(Ent(TARGET)->r.maxs[2]==CROUCH_HEIGHT && cl[TARGET].ps.viewheight>CROUCH_HEIGHT);
 eye=Ent(TARGET)->r.currentOrigin[2]+cl[TARGET].ps.viewheight;
 CHECK(BG_PlayerHitVolumes(&Ent(TARGET)->s,Ent(TARGET)->r.currentOrigin,Ent(TARGET)->r.mins,Ent(TARGET)->r.maxs,vols)==2);
 CHECK(HorizontalShot(200,Ent(TARGET)->r.currentOrigin[2]+CROUCH_HEIGHT+1)==TARGET);
 CHECK(HorizontalShot(200,eye+BG_HIT_HEAD_ALLOWANCE-0.5f)==TARGET);
 CHECK(HorizontalShot(200,eye+BG_HIT_HEAD_ALLOWANCE+0.5f)==ENTITYNUM_NONE);
 for(t=0,hit=1;t<100 && cl[TARGET].ps.viewheight>CROUCH_HEIGHT;t++) Frame();
 CHECK(cl[TARGET].ps.viewheight<=CROUCH_HEIGHT);
 CHECK(HorizontalShot(200,Ent(TARGET)->r.currentOrigin[2]+CROUCH_HEIGHT+1)==ENTITYNUM_NONE);
 /* a corpse is its box */
 Ent(TARGET)->s.eFlags|=EF_DEAD; Ent(TARGET)->s.origin2[2]=DEFAULT_VIEWHEIGHT;
 CHECK(HorizontalShot(200,Ent(TARGET)->r.currentOrigin[2]+CROUCH_HEIGHT+1)==ENTITYNUM_NONE);
 (void)hit;
}

/* Two players prone in a file, the shooter's muzzle inside the other's prone
 * body (only boxes collide, and the prone room check never meets a body). A
 * bullet that starts inside a hit volume and leaves it passes, as the
 * engine's own box passes one (CM_TraceThroughBrush: startsolid, the fraction
 * kept). So through the real ClientThink and Bullet_Fire the shooter firing
 * over the other, or turned away from them, hits nothing of them, and firing
 * into their box still hits it. */
static void TestProneFile(void) {
 static const struct { float back, body, yaw, pitch; qboolean hits; } cases[] = {
  { 2*PLAYER_WIDTH+10, 0, 0, -BG_PRONE_PITCH_CAP, qfalse },	/* over them, as steep as prone aims */
  { 2*PLAYER_WIDTH+10, 0, 20, -BG_PRONE_PITCH_CAP, qfalse },	/* over them, turned aside */
  { 2*PLAYER_WIDTH+5, 180, 180, 0, qfalse },	/* turned away from them, the body too */
  { 2*PLAYER_WIDTH+5, 0, 0, 0, qtrue },	/* into their box, from inside their body */
 };
 bgHitVolume_t vols[BG_MAX_HIT_VOLUMES];
 int i, n, b, ammo;
 for(i=0;i<ARRAY_LEN(cases);i++) {
  vec3_t fwd, muzzle, far;
  Reset();
  Place(SHOOTER,200-cases[i].back,0);
  Aim(SHOOTER,cases[i].body,0); Frame();	/* the body lies the way the view looks as it goes down */
  cmds[SHOOTER].buttons=cmds[TARGET].buttons=BUTTON_PRONE;
  Rest(BG_EYE_PRONE,BG_EYE_PRONE);
  CHECK(NEAR(BG_AngleDelta(cl[SHOOTER].ps.proneDirection,cases[i].body),0) && cl[TARGET].ps.proneDirection==0);
  CHECK(Ent(SHOOTER)->r.currentOrigin[0]+Ent(SHOOTER)->r.maxs[0] < Ent(TARGET)->r.currentOrigin[0]+Ent(TARGET)->r.mins[0]);	/* boxes apart */
  Ready(SHOOTER);
  Aim(SHOOTER,cases[i].yaw,cases[i].pitch); Frame();
  CHECK(Rested(SHOOTER,BG_EYE_PRONE) && cl[SHOOTER].ps.weaponstate==WEAPON_READY);
  CHECK(NEAR(BG_AngleDelta(cl[SHOOTER].ps.viewangles[YAW],cases[i].yaw),0) && NEAR(cl[SHOOTER].ps.viewangles[PITCH],cases[i].pitch));
  /* FireWeapon's muzzle is inside the target's prone body */
  AngleVectors(cl[SHOOTER].ps.viewangles,fwd,NULL,NULL);
  BG_ShotMuzzle(Ent(SHOOTER)->s.pos.trBase,cl[SHOOTER].ps.viewheight,fwd,muzzle);
  n=Volumes(TARGET,vols); b=VolumeOf(vols,n,HITVOL_PRONE_BODY);
  CHECK(b>0 && InVolume(&vols[b],muzzle) && !InVolume(&vols[0],muzzle));
  /* the view's own line, no spread: out of the body, then the box or nothing */
  VectorMA(muzzle,8192,fwd,far);
  CHECK((Shot(muzzle,far)==TARGET)==cases[i].hits);
  if(cases[i].hits) CHECK(shotFraction>0);
  /* the gun fired, spread and all */
  ammo=cl[SHOOTER].ps.ammo[WP_MACHINEGUN];
  cmds[SHOOTER].buttons=BUTTON_PRONE|BUTTON_ATTACK; Frame(); cmds[SHOOTER].buttons=BUTTON_PRONE;
  CHECK(cl[SHOOTER].ps.ammo[WP_MACHINEGUN]==ammo-1);
  CHECK((Ent(TARGET)->health<100)==cases[i].hits);
 }
}

/* Any ray near a prone body and a player mid-leg: the server's trace is the
 * shared function's. The target is driven, by its state alone, into the
 * crouch to prone leg while its eye is still over its new box (the over-box
 * and the body at once); rays are fired every frame for as long as the
 * over-box lasts, then for as many frames with the body alone. Each ray
 * passes through a point in or near one of the target's volumes, and the box,
 * the over-box and the body must each be what some rays hit. */
#define AGREEMENT_RAYS 400	/* a frame's */
static qboolean OverAndBody(void) {
 return cl[TARGET].ps.viewHeightLerpTarget==BG_EYE_PRONE && Ent(TARGET)->s.origin2[2]>Ent(TARGET)->r.maxs[2] &&
  (Ent(TARGET)->s.eFlags&EF_PRONE);
}
static void TestAgreement(void) {
 bgHitVolume_t vols[BG_MAX_HIT_VOLUMES];
 int t, i, k, n, v, over=0, after, entered[BG_MAX_HIT_VOLUMES]={0}, rays=0;
 Reset(); cmds[TARGET].buttons=BUTTON_PRONE;
 for(t=0;t<1000 && !OverAndBody();t++) Frame();
 CHECK(OverAndBody());
 for(after=-1;after;) {
  n=Volumes(TARGET,vols);
  if(after<0) {
   if(VolumeOf(vols,n,HITVOL_OVER_BOX)<0) { CHECK(over>0); after=over; continue; }	/* the window has closed */
   CHECK(n==BG_MAX_HIT_VOLUMES); over++;
  } else {
   CHECK(n==2 && VolumeOf(vols,n,HITVOL_PRONE_BODY)==1); after--;
  }
  for(i=0;i<AGREEMENT_RAYS;i++) {
   vec3_t p,dir,a,b; const bgHitVolume_t *vol=&vols[rand()%n];
   VectorCopy(vol->center,p);
   for(k=0;k<3;k++) VectorMA(p,1.25f*crandom()*vol->halfSize[k],vol->axis[k],p);	/* in it, or just outside */
   for(k=0;k<3;k++) dir[k]=crandom();
   if(VectorNormalize(dir)==0) continue;
   if(dir[2]>0) VectorNegate(dir,dir);	/* from above the floor */
   VectorMA(p,-200,dir,a); VectorMA(p,200,dir,b);
   rays++;
   if(Shot(a,b)!=TARGET) continue;
   for(v=n-1;v>0;v--) {
    float f; vec3_t nrm;
    if(BG_HitVolumeTrace(&vols[v],a,b,&f,nrm) && f==shotFraction) break;
   }
   entered[vols[v].type]++;	/* v 0: the box, the engine's */
  }
  Frame();
 }
 printf("agreement: %d rays, %d frames with the over-box and as many without: box %d, over the box %d, body %d\n",
  rays,over,entered[HITVOL_BOX],entered[HITVOL_OVER_BOX],entered[HITVOL_PRONE_BODY]);
 CHECK(entered[HITVOL_BOX]>0 && entered[HITVOL_OVER_BOX]>0 && entered[HITVOL_PRONE_BODY]>0);
}

/* MW2's SetClientViewAngle (0x428010): a prone player's new view keeps the
 * prone limits. */
static void TestSetViewAngle(void) {
 vec3_t want={70,150,0};
 Reset(); cmds[TARGET].buttons=BUTTON_PRONE; Rest(BG_EYE_STAND,BG_EYE_PRONE);
 CHECK(cl[TARGET].ps.pm_flags&PMF_PRONE);
 cl[TARGET].ps.proneTorsoPitch=10;
 SetClientViewAngle(Ent(TARGET),want);
 CHECK(NEAR(cl[TARGET].ps.viewangles[YAW],BG_AngleNormalize360(cl[TARGET].ps.proneDirection+BG_PRONE_YAW_CAP)));
 CHECK(NEAR(cl[TARGET].ps.viewangles[PITCH],10+BG_PRONE_PITCH_CAP));
 CHECK(cl[TARGET].ps.delta_angles[YAW]==ANGLE2SHORT(cl[TARGET].ps.viewangles[YAW])-cl[TARGET].pers.cmd.angles[YAW]);
 /* standing, the view is the view */
 Reset(); SetClientViewAngle(Ent(TARGET),want);
 CHECK(NEAR(cl[TARGET].ps.viewangles[YAW],150) && NEAR(cl[TARGET].ps.viewangles[PITCH],70));
}

int main(void) {
 TestHitVolumes();
 TestProneFile();
 TestAgreement();
 TestSetViewAngle();
 printf("PASS: %d stance server checks\n",checks); return 0;
}
