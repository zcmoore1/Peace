/* MW2's offhands through the real server: a command is the real ClientThink
 * (g_active.c), so the real Pmove runs it and the real ClientEvents answers its
 * events - the throw through the real FireWeapon and G_ThrowOffhand (g_weapon.c,
 * g_missile.c), the cook-out's EV_GRENADE_SUICIDE through the real
 * G_GrenadeSuicide and player_die (g_combat.c) - and the missiles fly and go
 * off through the real G_RunMissile, G_ExplodeMissile and G_RadiusDamage.
 * Covered: every offhand's launch velocity (its data's speed and up speed, plus
 * the thrower's velocity along the throw, looking level, up and down, standing,
 * running and backpedalling), every fuse (the frag's what its cook left, the
 * rest their fuseTime from the throw), the cook timeline end to end (thrown
 * late, run out in hand, a release on the think it runs out, god mode and a
 * death the server refuses, and a cook-out on one step of a command Pmove
 * chops into several), and MW2's death drop (5df920) of every class held at
 * death, the frag cooking, one let go on the command a fall kills its holder,
 * and nothing in INIT, PREPARE, after the throw or after a cancel. Every
 * missile leaves its start at level.time (408490: no Q3 prestep). Every
 * number MW2's data gives is written out here, from the
 * IW4 dump's weapons/mp files, so the shipped tables are checked against it.
 * The world is a floor at z = 0 plus the linked entities, as in
 * insertion_test.c; only engine traps and modules outside the real files are
 * stubbed. */
#include "../../code/game/g_local.h"
static int checks;
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

/* ---- MW2's data (the IW4 dump's weapons/mp files) ---- */
static const struct { int weapon, holdFireTime, speed, speedUp, fuse, cooks, cancelable; } mw2[]={
 { WP_FRAG,               600,  940, 120,  3500, 1, 0 },	/* frag_grenade_mp */
 { WP_FLASH,              600,  940, 120,  1500, 0, 0 },	/* flash_grenade_mp */
 { WP_SEMTEX,             600,  940, 120,  2000, 0, 1 },	/* semtex_mp */
 { WP_THROWING_KNIFE,       0, 1400,  40, 30000, 0, 1 },	/* throwingknife_mp */
 { WP_TACTICAL_INSERTION, 2630, 100, -10000, 50, 0, 1 },	/* flare_mp: an invisible flare Peace never throws */
};
#define FRAG 0
#define FLASH 1
#define SEMTEX 2
#define TKNIFE 3
#define TI 4

/* ---- the fixture: the thrower, and a player standing beside them ---- */
#define THROWER 0
#define VICTIM 1
#define CMD_MSEC 8	/* a 125fps client, one command a server frame */
static float viewPitch;
static int forwardMove, cmdWeapon, boomTime[MAX_GENTITIES];
static playerState_t *ps;
static int Button(int weapon) { return weapon==WP_FLASH ? BUTTON_TACTICAL : BUTTON_LETHAL; }
static void Link(gentity_t *e) {
 VectorCopy(e->client->ps.origin,e->r.currentOrigin); VectorCopy(e->client->ps.origin,e->s.pos.trBase); trap_LinkEntity(e);
}
static void Alive(int n) {
 gentity_t *e=&g_entities[n];
 e->health=cl[n].ps.stats[STAT_HEALTH]=100; e->takedamage=qtrue; e->r.contents=CONTENTS_BODY;
 cl[n].ps.pm_type=PM_NORMAL; cl[n].ps.viewheight=DEFAULT_VIEWHEIGHT;
 VectorSet(e->r.mins,-PLAYER_WIDTH,-PLAYER_WIDTH,MINS_Z); VectorSet(e->r.maxs,PLAYER_WIDTH,PLAYER_WIDTH,DEFAULT_HEIGHT);
 e->die=player_die;
}
/* ClientEndFrame for both, as G_RunFrame ends each frame (the TI's standing
 * spot is sampled there) */
static void EndFrames(void) { int i; for(i=0;i<2;i++) ClientEndFrame(&g_entities[i]); }
static void Reset(int row) {
 int i, weapon=mw2[row].weapon;
 memset(&level,0,sizeof(level)); memset(g_entities,0,sizeof(g_entities)); memset(cl,0,sizeof(cl));
 memset(boomTime,0,sizeof(boomTime));
 level.time=10000; level.previousTime=level.time-CMD_MSEC; level.num_entities=MAX_CLIENTS; level.maxclients=2;
 level.clients=cl; level.gentities=g_entities;
 g_gametype.integer=GT_FFA; g_knockback.integer=1000; g_speed.value=BG_PLAYER_SPEED; g_gravity.value=DEFAULT_GRAVITY;
 pmove_msec.integer=8; pmove_fixed.integer=0; sndCanDamage=qtrue; viewPitch=0; forwardMove=0; cmdWeapon=WP_SHOTGUN;
 for(i=0;i<2;i++) {
  gentity_t *e=&g_entities[i]; bg_class_t c=*BG_Class(0);
  c.slot[0]=WP_SHOTGUN; c.slot[1]=WP_MACHINEGUN;
  if(weapon!=WP_FLASH) { c.lethal=weapon; c.lethalCount=BG_ClassEquipmentCount(weapon); }
  CHECK(BG_ValidateClass(&c));
  e->client=&cl[i]; e->inuse=qtrue; e->s.number=i; e->classname="player"; e->r.ownerNum=ENTITYNUM_NONE;
  cl[i].ps.clientNum=i; cl[i].pers.connected=CON_CONNECTED; cl[i].pers.enterTime=100+i;
  cl[i].pers.selectedLoadout=c; cl[i].pers.maxHealth=100; cl[i].sess.sessionTeam=TEAM_FREE;
  BG_ApplyClass(&cl[i].ps,&c); cl[i].ps.stats[STAT_MAX_HEALTH]=100;
  cl[i].ps.commandTime=level.time; cl[i].lastCmdTime=level.time; cl[i].useHold=ENTITYNUM_NONE;
  Alive(i);
  VectorSet(cl[i].ps.origin,0,i*100,24); cl[i].ps.groundEntityNum=ENTITYNUM_WORLD; Link(e);
 }
 InitBodyQue();
 ps=&cl[THROWER].ps;
 EndFrames();
 CHECK(ps->weapon==WP_SHOTGUN && ps->weaponstate==WEAPON_READY && ps->ammo[weapon]>0 && !ps->grenadeTimeLeft);
 CHECK(cl[THROWER].insertionSpotValid);
}
/* One command from the thrower, as the engine hands it to the game: msec
 * long, which the real Pmove chops into steps (66 at most, or pmove_msec with
 * pmove_fixed) before the real ClientEvents reads any of their events. */
static void CommandMsec(int buttons,int msec) {
 level.previousTime=level.time; level.time+=msec;
 memset(&nextCmd,0,sizeof(nextCmd));
 nextCmd.serverTime=level.time; nextCmd.buttons=buttons; nextCmd.weapon=cmdWeapon;
 nextCmd.angles[PITCH]=ANGLE2SHORT(viewPitch); nextCmd.forwardmove=forwardMove;
 ClientThink(THROWER);
}
static void Command(int buttons) { CommandMsec(buttons,CMD_MSEC); }
/* The rest of the frame: every missile and think, then the end-frames. */
static void World(void) {
 int i;
 for(i=MAX_CLIENTS;i<level.num_entities;i++) {
  gentity_t *e=&g_entities[i];
  if(!e->inuse) continue;
  if(e->s.eType==ET_MISSILE) G_RunMissile(e); else G_RunThink(e);
  if(e->inuse && e->freeAfterEvent && !boomTime[i]) boomTime[i]=level.time;	/* G_ExplodeMissile */
 }
 EndFrames();
}
static void Frame(int buttons) { Command(buttons); World(); }
static void Frames(int buttons,int msec) { int t; for(t=0;t<msec;t+=CMD_MSEC) Frame(buttons); }
/* the thrower's equipment spawned since entity number `from` (a TI is not one) */
static int Spawned(int from, gentity_t **last) {
 int i, n=0;
 for(i=from;i<level.num_entities;i++) {
  gentity_t *e=&g_entities[i];
  if(e->inuse && e->parent==&g_entities[THROWER] && !G_IsInsertion(e)) { n++; if(last) *last=e; }
 }
 return n;
}
/* pressed, then `buttons` until it is in hand and ready (MW2's HOLD): the time
 * of the think that entered it. With a holdFireTime of 0 (the throwing knife)
 * that is the press. */
static int PullOut(int row, int buttons) {
 int t, press;
 Frame(Button(mw2[row].weapon)); press=level.time;
 for(t=0;t<10000 && !(ps->weapon==mw2[row].weapon && ps->weaponstate==WEAPON_READY);t+=CMD_MSEC) {
  CHECK(!ps->grenadeTimeLeft && !(ps->weaponAction&WA_FIRED));	/* no fuse in INIT or PREPARE */
  Frame(buttons);
 }
 CHECK(t<10000 && !(ps->weaponAction&WA_FIRED) && ps->grenadeTimeLeft==mw2[row].fuse);
 /* the gun's quick drop and the prepare, a think each at a time */
 if(BG_OffhandPreparesAtOnce(mw2[row].weapon)) CHECK(level.time==press);
 else CHECK(level.time-press==CMD_MSEC*((BG_WeaponQuickDropTime(WP_SHOTGUN)+CMD_MSEC-1)/CMD_MSEC)+
  CMD_MSEC*((mw2[row].holdFireTime+CMD_MSEC-1)/CMD_MSEC));
 return level.time;
}
static void Velocity(int row, vec3_t out) {
 vec3_t forward;
 AngleVectors(ps->viewangles,forward,NULL,NULL);
 VectorScale(forward,mw2[row].speed,out); out[2]+=mw2[row].speedUp;
}

/* The data itself: every prepare, fuse, cook and cancel is MW2's. */
static void TestData(void) {
 int i;
 for(i=0;i<ARRAY_LEN(mw2);i++) {
  vec3_t forward={1,0,0}, v;
  CHECK(BG_OffhandHeldToThrow(mw2[i].weapon) && BG_WeaponRaiseTime(mw2[i].weapon)==mw2[i].holdFireTime);
  CHECK(BG_OffhandFuseTime(mw2[i].weapon)==mw2[i].fuse && BG_OffhandCooks(mw2[i].weapon)==mw2[i].cooks);
  CHECK(BG_OffhandCancelable(mw2[i].weapon)==mw2[i].cancelable);
  BG_OffhandLaunchVelocity(mw2[i].weapon,forward,v);
  if(i!=TI) CHECK(v[0]==mw2[i].speed && v[1]==0 && v[2]==mw2[i].speedUp);
 }
 /* the flare's 50 forward along the flattened view: the one forward speed */
 { vec3_t forward={0.6f,0,0.8f}, v; BG_OffhandLaunchVelocity(WP_TACTICAL_INSERTION,forward,v);
   CHECK(NEAR(v[0],60+50) && v[1]==0 && NEAR(v[2],80-10000)); }
 CHECK(!BG_OffhandHeldToThrow(WP_KNIFE) && !BG_OffhandFuseTime(WP_SHOTGUN) && !BG_OffhandCooks(WP_KNIFE));
}

/* MW2's throw (4c3690, then 4c375f-4c37c3), through the real FireWeapon: the
 * view's forward at projectileSpeed plus projectileSpeedUp, snapped toward
 * zero, then the thrower's own velocity along that direction, its component
 * only, unsnapped. Each fuse runs from the throw, but the frag's from HOLD
 * entry, whenever it is let go. The fuse is the server's: the throw spends
 * ps.grenadeTimeLeft. */
static void TestThrow(void) {
 static const float pitches[]={0,-30,20};
 static const int sticks[]={0,127,-127};
 int row,p,s;
 for(row=0;row<ARRAY_LEN(mw2);row++) for(p=0;p<ARRAY_LEN(pitches);p++) for(s=0;s<ARRAY_LEN(sticks);s++) {
  int weapon=mw2[row].weapon, from, entry, count, t;
  gentity_t *m=NULL;
  Reset(row); viewPitch=pitches[p]; forwardMove=sticks[s]; count=ps->ammo[weapon];
  Frames(0,200);	/* up to speed */
  from=level.num_entities;
  entry=PullOut(row,0);	/* a tap, queued through the drop and the prepare */
  CHECK(!Spawned(from,NULL) && ps->ammo[weapon]==count);
  /* HOLD's first think throws it */
  Command(0);
  CHECK((ps->weaponAction&WA_FIRED) && ps->weaponstate==WEAPON_FIRING && ps->ammo[weapon]==count-1);
  CHECK(!ps->grenadeTimeLeft);	/* the server's throw spent it (5e944c) */
  if(weapon==WP_TACTICAL_INSERTION) {
   /* planted, never thrown: no missile at all */
   CHECK(!Spawned(from,NULL) && G_FindInsertion(&g_entities[THROWER]));
   World(); continue;
  }
  CHECK(Spawned(from,&m)==1 && m->s.eType==ET_MISSILE && m->s.pos.trType==TR_GRAVITY);
  CHECK(m->s.weapon==(weapon==WP_FRAG || weapon==WP_FLASH ? WP_GRENADE_LAUNCHER : weapon));
  /* MW2's trajectory (408490): from its start at level.time, no prestep */
  CHECK(m->s.pos.trTime==level.time);
  { vec3_t v, dir; float along; int k;
    Velocity(row,v); VectorNormalize2(v,dir); along=DotProduct(ps->velocity,dir);
    for(k=0;k<3;k++) CHECK(NEAR(m->s.pos.trDelta[k],(int)v[k]+dir[k]*along));
    if(sticks[s]>0) CHECK(along>100); else if(sticks[s]<0) CHECK(along<-100); else CHECK(along==0); }
  /* from Peace's muzzle point */
  { vec3_t forward, muzzle; AngleVectors(ps->viewangles,forward,NULL,NULL);
    VectorCopy(g_entities[THROWER].s.pos.trBase,muzzle); muzzle[2]+=ps->viewheight; VectorMA(muzzle,14,forward,muzzle);
    SnapVector(muzzle); CHECK(VectorCompare(m->s.pos.trBase,muzzle)); }
  /* the fuse: the frag explodes fuseTime after HOLD entry, the rest fuseTime
   * after the throw */
  if(weapon==WP_FRAG) CHECK(m->nextthink==entry+mw2[row].fuse);
  else CHECK(m->nextthink==level.time+mw2[row].fuse);
  if(weapon==WP_FLASH) CHECK(!m->damage && !m->splashDamage);	/* no flash effect yet */
  if(weapon==WP_FRAG) CHECK(m->splashDamage>0);
  World();
  if(weapon==WP_THROWING_KNIFE) continue;	/* it never goes off */
  { int at=m->nextthink, n=m->s.number;
    for(t=0;t<5000 && !boomTime[n];t+=CMD_MSEC) { CHECK(level.time<at); Frame(0); }
    CHECK(boomTime[n]>=at && boomTime[n]<at+CMD_MSEC); }
 }
}

/* The frag's cook (cookOffHold 1). Let go late, the fuse in flight is what
 * HOLD left it, so it still goes off fuseTime after HOLD entry. Held to the end
 * it is MW2's grenade suicide: the think it runs out on (a release on that
 * think included) sends EV_GRENADE_SUICIDE, spends the frag, and the server
 * kills its holder as a suicide; the death drop sets the frag off at once, at
 * the hand, hurting who stands there. A holder the server does not kill (god
 * mode, S&D between rounds) loses the frag and gets the gun back on its quick
 * raise, with nothing thrown; the -1 stays until the next offhand starts. */
static void TestCook(void) {
 static const int held[]={CMD_MSEC,800,2000,3496-CMD_MSEC};
 int k, entry, from, t, count, fuse=mw2[FRAG].fuse;
 gentity_t *m=NULL, *obit=NULL;
 for(k=0;k<ARRAY_LEN(held);k++) {
  Reset(FRAG); count=ps->ammo[WP_FRAG]; from=level.num_entities;
  entry=PullOut(FRAG,BUTTON_LETHAL);
  Frames(BUTTON_LETHAL,held[k]-CMD_MSEC);
  CHECK(ps->weapon==WP_FRAG && ps->grenadeTimeLeft==fuse-(level.time-entry) && !Spawned(from,NULL));
  Command(0);
  CHECK(Spawned(from,&m)==1 && m->nextthink==entry+fuse && !ps->grenadeTimeLeft && ps->ammo[WP_FRAG]==count-1);
  for(t=0;t<5000 && !boomTime[m->s.number];t+=CMD_MSEC) Frame(0);
  CHECK(boomTime[m->s.number]>=entry+fuse && boomTime[m->s.number]<entry+fuse+CMD_MSEC);
  CHECK(cl[THROWER].ps.stats[STAT_HEALTH]>0 && ps->pm_type==PM_NORMAL);
 }
 /* held, or let go on the very think it runs out: the suicide */
 for(k=0;k<2;k++) {
  int i, end, out;
  Reset(FRAG); count=ps->ammo[WP_FRAG]; from=level.num_entities;
  entry=PullOut(FRAG,BUTTON_LETHAL);
  out=entry+CMD_MSEC*((fuse+CMD_MSEC-1)/CMD_MSEC);	/* the think its msec takes it to 0 or past */
  while(level.time+CMD_MSEC<out) Frame(BUTTON_LETHAL);
  CHECK(ps->grenadeTimeLeft>0 && ps->grenadeTimeLeft<=CMD_MSEC && !Spawned(from,NULL));
  end=level.num_entities;
  Command(k ? 0 : BUTTON_LETHAL);
  CHECK(level.time==out);
  /* killed as a suicide, the frag spent in the hand and fired by the death drop */
  CHECK(g_entities[THROWER].health<=0 && ps->pm_type==PM_DEAD && ps->ammo[WP_FRAG]==count-1 && !ps->grenadeTimeLeft);
  CHECK(Spawned(from,&m)==1 && m->nextthink==level.time-1 && m->s.pos.trTime==level.time);
  for(i=end;i<level.num_entities;i++) if(g_entities[i].s.eType==ET_EVENTS+EV_OBITUARY) obit=&g_entities[i];
  CHECK(obit && obit->s.eventParm==MOD_SUICIDE && obit->s.otherEntityNum==THROWER && obit->s.otherEntityNum2==THROWER);
  CHECK(NEAR(m->s.pos.trBase[2],g_entities[THROWER].r.currentOrigin[2]+BG_OFFHAND_DEATH_HEIGHT));
  World();
  CHECK(boomTime[m->s.number]==out && g_entities[VICTIM].health<100);	/* at once, beside the hand */
  /* dead: nothing more comes out */
  Frames(0,fuse); CHECK(Spawned(from,NULL)==1 && ps->weapon==WP_NONE);
 }
 /* a holder the server does not kill: god mode, or S&D between rounds. The
  * -1 stays (MW2's OffHandEnd never writes it) and fires nothing: the hand
  * holds the gun again. */
 for(k=0;k<3;k++) {
  Reset(FRAG); count=ps->ammo[WP_FRAG]; from=level.num_entities;
  if(k==1) { g_gametype.integer=GT_SND; sndCanDamage=qfalse; } else g_entities[THROWER].flags|=FL_GODMODE;
  entry=PullOut(FRAG,BUTTON_LETHAL);
  while(ps->grenadeTimeLeft>0) Frame(BUTTON_LETHAL);
  CHECK(ps->grenadeTimeLeft==-1 && ps->weapon==WP_FRAG && ps->ammo[WP_FRAG]==count-1);
  CHECK(g_entities[THROWER].health==100 && ps->pm_type==PM_NORMAL && !Spawned(from,NULL));
  /* the next think ends it: the gun's quick raise, as after a cancel */
  Frame(BUTTON_LETHAL);
  CHECK(ps->weapon==WP_SHOTGUN && ps->weaponstate==WEAPON_RAISING && ps->grenadeTimeLeft==-1);
  CHECK(ps->weaponTime==BG_WeaponQuickRaiseTime(WP_SHOTGUN) && (ps->weaponAction&WA_FIRED));
  if(k==2) {
   /* killed in the raise (the kill command lifts god mode): the frag is gone,
    * there is nothing in hand to drop, and the -1 goes with the death */
   Cmd_Kill_f(&g_entities[THROWER]);
   CHECK(ps->pm_type==PM_DEAD && !ps->grenadeTimeLeft);
   World(); CHECK(!Spawned(from,NULL) && g_entities[VICTIM].health==100);
   continue;
  }
  Frames(0,fuse); CHECK(!Spawned(from,NULL) && ps->weapon==WP_SHOTGUN && ps->weaponstate==WEAPON_READY);
  CHECK(g_entities[THROWER].health==100 && ps->ammo[WP_FRAG]==count-1 && ps->grenadeTimeLeft==-1);
  /* the next offhand's start clears it (OffHandInit, 578e00) */
  Frame(BUTTON_TACTICAL); CHECK(ps->weaponstate==WEAPON_DROPPING && !ps->grenadeTimeLeft);
 }
}

/* A command Pmove chops into steps - pmove_fixed's pmove_msec each, or 66 at
 * most without it (a frame hitch) - all run before ClientEvents reads the
 * command's events. A cook-out on any step but the last is followed on the
 * same command by the next think's OffHandEnd: the gun (or the empty hand) is
 * back before the server sees the suicide. Its event names the frag (MW2's
 * parm, 577c37) and the -1 survives the end, so the holder still dies and the
 * frag still goes off at the hand at once, whatever the command's length and
 * whichever step it lands on. */
static void TestChoppedCookOut(void) {
 /* {pmove_fixed, the fuse left before it, the command's length} */
 static const int cases[][3]={
  {0,4,70},{0,36,100},{0,60,140},{0,4,200},{0,100,140},{0,130,200},{0,36,60},
  {1,4,16},{1,12,16},{1,4,24},{1,12,24},{1,20,24},{1,4,32},{1,28,32},
 };
 int c, empty;
 for(c=0;c<ARRAY_LEN(cases);c++) for(empty=0;empty<2;empty++) {
  int fixed=cases[c][0], left=cases[c][1], len=cases[c][2], count, from, end, i, steps, step;
  gentity_t *m=NULL, *obit=NULL;
  Reset(FRAG); count=ps->ammo[WP_FRAG];
  pmove_fixed.integer=fixed;
  if(empty) {
   /* no gun to come back to (a stale slot): the hand is empty again at once */
   ps->weapon=WP_NONE; ps->slotFlags[0]^=BG_GUN_GENERATION; cmdWeapon=BG_SLOT_COMMAND;
  }
  from=level.num_entities;
  Frame(BUTTON_LETHAL);
  while(!(ps->weapon==WP_FRAG && ps->weaponstate==WEAPON_READY)) Frame(BUTTON_LETHAL);
  CHECK(!empty || (ps->weaponAction&WA_LEFT_EMPTY));
  /* to `left`: 8ms commands, then 1ms ones (never needed with pmove_fixed,
   * whose 8ms thinks reach every `left` here) */
  while(ps->grenadeTimeLeft>left) {
   CommandMsec(BUTTON_LETHAL,ps->grenadeTimeLeft-left>=CMD_MSEC ? CMD_MSEC : 1); World();
  }
  CHECK(ps->grenadeTimeLeft==left && !Spawned(from,NULL));
  step=fixed ? pmove_msec.integer : 66;
  steps=(len+step-1)/step;
  end=level.num_entities;
  CommandMsec(BUTTON_LETHAL,len);
  /* the step it cooked out on was the last only if no step's think followed */
  if((left+step-1)/step<steps) CHECK(ps->weapon==(empty ? WP_NONE : WP_SHOTGUN));
  CHECK(g_entities[THROWER].health<=0 && ps->pm_type==PM_DEAD && ps->ammo[WP_FRAG]==count-1 && !ps->grenadeTimeLeft);
  CHECK(Spawned(from,&m)==1 && !strcmp(m->classname,"grenade") && m->nextthink==level.time-1 && m->s.pos.trTime==level.time);
  CHECK(NEAR(m->s.pos.trBase[2],g_entities[THROWER].r.currentOrigin[2]+BG_OFFHAND_DEATH_HEIGHT));
  for(i=end;i<level.num_entities;i++) if(g_entities[i].s.eType==ET_EVENTS+EV_OBITUARY) obit=&g_entities[i];
  CHECK(obit && obit->s.eventParm==MOD_SUICIDE && obit->s.otherEntityNum==THROWER);
  World();
  CHECK(boomTime[m->s.number]==level.time && g_entities[VICTIM].health<100);
  Frames(0,500); CHECK(Spawned(from,NULL)==1);
 }
}

/* Let go on the very command its holder dies of a fall: the command's events
 * are EV_FALL_FAR, then EV_FIRE_WEAPON. The fall's death comes first and
 * finds the offhand let go but not yet thrown - its fuse still the server's
 * to spend, MW2's FIRE before the throw (fireDelay) - so the death drop fires
 * it, every class (the TI planted), and the throw that follows has nothing to
 * throw: one missile, never two, none lost. The same fall survived is the
 * ordinary throw. */
static void TestReleaseOnFatalFall(void) {
 int row, lives;
 for(row=0;row<ARRAY_LEN(mw2);row++) for(lives=0;lives<2;lives++) {
  int weapon=mw2[row].weapon, from, entry, count, seq, fall=-1, fire=-1, i, left;
  gentity_t *m=NULL, *ti; vec3_t at;
  Reset(row); count=ps->ammo[weapon]; from=level.num_entities;
  entry=PullOut(row,Button(weapon));
  Frames(Button(weapon),400);
  left=weapon==WP_FRAG ? mw2[row].fuse-(level.time-entry) : mw2[row].fuse;
  /* just above the floor and falling fast; one hit point left, or all */
  ps->origin[2]=24+3; ps->velocity[2]=-1200; ps->groundEntityNum=ENTITYNUM_NONE;
  if(!lives) g_entities[THROWER].health=ps->stats[STAT_HEALTH]=1;
  seq=ps->eventSequence;
  Command(0);
  for(i=seq;i<ps->eventSequence;i++) {
   int e=ps->events[i&(MAX_PS_EVENTS-1)];
   if(e==EV_FALL_FAR) fall=i;
   if(e==EV_FIRE_WEAPON) fire=i;
  }
  CHECK(fall>=0 && fire>fall);	/* the death's event comes first */
  CHECK(!ps->grenadeTimeLeft && ps->ammo[weapon]==count-1);	/* spent once, by the release */
  VectorCopy(g_entities[THROWER].r.currentOrigin,at);
  if(weapon==WP_TACTICAL_INSERTION) {
   ti=G_FindInsertion(&g_entities[THROWER]);
   CHECK(ti && VectorCompare(ti->pos1,cl[THROWER].insertionSpot) && !Spawned(from,NULL));
  } else {
   CHECK(Spawned(from,&m)==1 && m->s.pos.trTime==level.time);
   if(weapon==WP_FRAG) CHECK(m->nextthink==entry+mw2[row].fuse);
   else CHECK(m->nextthink==level.time+left);
  }
  if(lives) {
   CHECK(g_entities[THROWER].health>0 && ps->pm_type==PM_NORMAL);
   if(m) {
    /* thrown: from the muzzle at the throw's velocity */
    vec3_t v, dir, forward, muzzle; float along; int k;
    Velocity(row,v); VectorNormalize2(v,dir); along=DotProduct(ps->velocity,dir);
    for(k=0;k<3;k++) CHECK(NEAR(m->s.pos.trDelta[k],(int)v[k]+dir[k]*along));
    AngleVectors(ps->viewangles,forward,NULL,NULL);
    VectorCopy(g_entities[THROWER].s.pos.trBase,muzzle); muzzle[2]+=ps->viewheight; VectorMA(muzzle,14,forward,muzzle);
    SnapVector(muzzle); CHECK(VectorCompare(m->s.pos.trBase,muzzle));
   }
  } else {
   CHECK(g_entities[THROWER].health<=0 && ps->pm_type==PM_DEAD);
   if(m) {
    /* dropped where the holder died, not thrown */
    CHECK(NEAR(m->s.pos.trBase[0],at[0]) && NEAR(m->s.pos.trBase[1],at[1]) &&
     NEAR(m->s.pos.trBase[2],at[2]+BG_OFFHAND_DEATH_HEIGHT));
    for(i=0;i<3;i++) CHECK(fabs(m->s.pos.trDelta[i])<=BG_OFFHAND_DEATH_SPEED);
   }
  }
  World(); Frames(0,500);
  CHECK(Spawned(from,NULL)==(weapon==WP_TACTICAL_INSERTION ? 0 : 1));
 }
}

static void Kill(void) {
 G_Damage(&g_entities[THROWER],&g_entities[VICTIM],&g_entities[VICTIM],NULL,NULL,1000,0,MOD_MACHINEGUN);
 CHECK(g_entities[THROWER].health<=0 && ps->pm_type==PM_DEAD);
}
/* MW2's death drop (player_die 42bd08 -> 5df920): whatever is in hand with a
 * fuse - from HOLD to the throw - is fired where its holder dies, from 40
 * above MW2's feet (BG_OFFHAND_DEATH_HEIGHT above Peace's origin) at up to 160
 * on each axis, none of the holder's own, with the fuse it has left. Every
 * class: the frag cooking, the flash and Semtex on their whole fuses, the
 * knife on its lifetime, and the Tactical Insertion planted. Ammo is not
 * touched. In INIT and PREPARE, after the throw and after a cancel there is
 * no fuse and nothing is dropped. */
static void TestDeathDrop(void) {
 int row, k;
 for(row=0;row<ARRAY_LEN(mw2);row++) for(k=0;k<3;k++) {
  int weapon=mw2[row].weapon, from, entry, count, held=k*400, left;
  gentity_t *m=NULL, *ti; vec3_t at;
  Reset(row); forwardMove=k==2 ? 127 : 0; count=ps->ammo[weapon]; from=level.num_entities;
  Frames(0,200);
  entry=PullOut(row,Button(weapon));
  Frames(Button(weapon),held);
  left=weapon==WP_FRAG ? mw2[row].fuse-(level.time-entry) : mw2[row].fuse;
  CHECK(ps->grenadeTimeLeft==left && !Spawned(from,NULL));
  VectorCopy(g_entities[THROWER].r.currentOrigin,at);
  Kill();
  CHECK(!ps->grenadeTimeLeft && ps->ammo[weapon]==count);
  if(weapon==WP_TACTICAL_INSERTION) {
   ti=G_FindInsertion(&g_entities[THROWER]);
   CHECK(ti && VectorCompare(ti->pos1,cl[THROWER].insertionSpot) && !Spawned(from,NULL));
   continue;
  }
  CHECK(Spawned(from,&m)==1 && m->s.eType==ET_MISSILE && m->s.pos.trType==TR_GRAVITY);
  CHECK(!strcmp(m->classname,weapon==WP_SEMTEX ? "semtex" : weapon==WP_THROWING_KNIFE ? "throwingknife" : "grenade"));
  CHECK(NEAR(m->s.pos.trBase[0],at[0]) && NEAR(m->s.pos.trBase[1],at[1]) &&
   NEAR(m->s.pos.trBase[2],at[2]+BG_OFFHAND_DEATH_HEIGHT));
  { int i; for(i=0;i<3;i++) CHECK(fabs(m->s.pos.trDelta[i])<=BG_OFFHAND_DEATH_SPEED && m->s.pos.trDelta[i]==(int)m->s.pos.trDelta[i]); }
  CHECK(m->nextthink==level.time+left && m->s.pos.trTime==level.time);
  if(weapon==WP_FRAG) CHECK(m->nextthink==entry+mw2[row].fuse);
  if(weapon==WP_THROWING_KNIFE) {
   /* far too slow to hurt (g_minGrenadeDamageSpeed); it comes to rest */
   CHECK(VectorLength(m->s.pos.trDelta)<BG_THROWING_KNIFE_HIT_SPEED);
  } else {
   int n=m->s.number, t, at=m->nextthink;
   for(t=0;t<5000 && !boomTime[n];t+=CMD_MSEC) { CHECK(level.time<at); Frame(0); }
   CHECK(boomTime[n]>=at && boomTime[n]<at+CMD_MSEC);
  }
  /* dead, the hand is empty and nothing else comes out */
  Frames(0,500); CHECK(ps->weapon==WP_NONE && Spawned(from,NULL)==1);
 }
 /* nothing in hand to drop: in the gun's quick drop (INIT), in the prepare,
  * after the throw and after a Semtex cancel */
 for(k=0;k<4;k++) {
  int from, n;
  Reset(SEMTEX); from=level.num_entities;
  Frame(BUTTON_LETHAL);
  if(k>=1) while(!(ps->weapon==WP_SEMTEX && ps->weaponstate==WEAPON_RAISING)) Frame(BUTTON_LETHAL);
  if(k>=1) Frames(BUTTON_LETHAL,100);
  if(k==2) { while(ps->weaponstate!=WEAPON_READY) Frame(BUTTON_LETHAL); Frame(0); CHECK(ps->weaponstate==WEAPON_FIRING); }
  if(k==3) { while(ps->weaponstate!=WEAPON_READY) Frame(BUTTON_LETHAL);
   Frame(BUTTON_LETHAL|BUTTON_WEAPON_SWAP); CHECK(ps->weapon==WP_SHOTGUN && ps->weaponstate==WEAPON_RAISING); }
  CHECK(!ps->grenadeTimeLeft);
  n=Spawned(from,NULL); CHECK(n==(k==2));
  Kill(); CHECK(Spawned(from,NULL)==n);
 }
}

/* MW2's event rings, through the real ClientThink (Pmove, then
 * BG_PlayerStateToEntityState and ClientEvents, 493873-493915). The offhand
 * press raises MW2's events in MW2's order: CheckForOffHand's EV_SWITCH_OFFHAND
 * (579168), OffHandInit's EV_WEAPON_SWITCH_STARTED and EV_RESET_ADS (578e10,
 * 578e5e) unless the offhand comes straight out (5791ce), and OffHandPrepare's
 * EV_PREP_OFFHAND (5778e6) when the pull begins - for the throwing knife on the
 * press think (578eec). Each parm is the offhand, the ADS reset's 0. The
 * entity's ring gets everything but MW2's serverOnly and singleClient events
 * (56eab0): of the press, only the pullback. */
static int psSeq, esSeq;
static void Mark(void) { psSeq=ps->eventSequence; esSeq=g_entities[THROWER].s.eventSequence; }
/* the playerstate's events since Mark, oldest first */
static int Raised(int *ev,int *parm) {
 int i, n=ps->eventSequence-psSeq;
 CHECK(n>=0 && n<=MAX_PS_EVENTS);
 for(i=0;i<n;i++) { ev[i]=ps->events[(psSeq+i)&(MAX_PS_EVENTS-1)]; parm[i]=ps->eventParms[(psSeq+i)&(MAX_PS_EVENTS-1)]; }
 return n;
}
/* the entity's, for everyone else */
static int Sent(int *ev,int *parm) {
 entityState_t *es=&g_entities[THROWER].s;
 int i, n=(es->eventSequence-esSeq)&ES_EVENT_SEQUENCE_MASK;
 CHECK(n<=MAX_ENTITY_EVENTS);
 for(i=0;i<n;i++) { ev[i]=es->events[(esSeq+i)&(MAX_ENTITY_EVENTS-1)]; parm[i]=es->eventParms[(esSeq+i)&(MAX_ENTITY_EVENTS-1)]; }
 return n;
}
/* Q3 sent each pending event as a temp entity too (SendPendingPredictableEvents);
 * MW2 has none (ClientThink_real 493873-493915, ClientEndFrame 5d8134). */
static int PlayerEventEntities(void) {
 int i, n=0;
 for(i=0;i<level.num_entities;i++)
  if(g_entities[i].inuse && g_entities[i].s.eType>ET_EVENTS && (g_entities[i].s.eFlags&EF_PLAYER_EVENT)) n++;
 return n;
}
static void Press(int row,int *ev,int *parm,int *n,int *sent,int *sev,int *sparm) {
 Mark(); Frame(Button(mw2[row].weapon)); *n=Raised(ev,parm); *sent=Sent(sev,sparm);
}
/* flash thrown, then held still until its recovery is the gun's quick raise
 * after equipment (MW2's OFFHAND_END) */
static void FlashToOffhandEnd(void) {
 int t;
 Frame(BUTTON_TACTICAL);
 for(t=0;t<10000 && !(ps->weapon==WP_SHOTGUN && ps->weaponstate==WEAPON_RAISING && (ps->weaponAction&WA_FIRED));t+=CMD_MSEC) Frame(0);
 CHECK(t<10000 && (ps->weaponAction&WA_TYPE_MASK)==WA_TACTICAL);
}
static void TestEventRing(void) {
 int ev[8], parm[8], sev[8], sparm[8], n, sent, row, t, i, k;
 /* a shot, then the throwing knife on the very next command */
 Reset(TKNIFE); Frames(0,100);
 Mark(); Frame(BUTTON_ATTACK);
 n=Raised(ev,parm); sent=Sent(sev,sparm);
 CHECK(n==1 && ev[0]==EV_FIRE_WEAPON && parm[0]==WP_SHOTGUN);	/* the parm is the gun that fired */
 CHECK(sent==1 && sev[0]==EV_FIRE_WEAPON && sparm[0]==WP_SHOTGUN);
 Press(TKNIFE,ev,parm,&n,&sent,sev,sparm);
 CHECK(n==4 && ev[0]==EV_SWITCH_OFFHAND && ev[1]==EV_WEAPON_SWITCH_STARTED && ev[2]==EV_RESET_ADS && ev[3]==EV_PREP_OFFHAND);
 CHECK(parm[0]==WP_THROWING_KNIFE && parm[1]==WP_THROWING_KNIFE && parm[2]==0 && parm[3]==WP_THROWING_KNIFE);
 CHECK(ps->weapon==WP_THROWING_KNIFE && ps->weaponstate==WEAPON_READY);
 /* four on one think: the playerstate's ring holds nothing of the shot now,
  * as MW2's archived playerstate does not */
 for(i=0;i<MAX_PS_EVENTS;i++) CHECK(ps->events[i]!=EV_FIRE_WEAPON);
 /* but everyone else keeps it: the entity holds the shot, naming the gun,
  * beside the knife's pullback, and nothing of the press meant for the
  * server or the player alone */
 CHECK(sent==1 && sev[0]==EV_PREP_OFFHAND && sparm[0]==WP_THROWING_KNIFE);
 k=(g_entities[THROWER].s.eventSequence-2)&(MAX_ENTITY_EVENTS-1);
 CHECK(g_entities[THROWER].s.events[k]==EV_FIRE_WEAPON && g_entities[THROWER].s.eventParms[k]==WP_SHOTGUN);
 CHECK(g_entities[THROWER].s.weapon==WP_THROWING_KNIFE);
 /* MW2's server notifies the switch's start with the offhand's name */
 CHECK(!strcmp(lastLog,"WeaponSwitchStarted: 0 weapon_throwingknife\n"));
 CHECK(!PlayerEventEntities());

 /* Q3's EVENT_VALID_MSEC on each slot of the entity's ring, as G_RunFrame
  * runs G_AgeEntityEvents for every entity once a frame: the shot and the
  * pullback go, the sequence stays */
 k=g_entities[THROWER].s.eventSequence;
 G_AgeEntityEvents(&g_entities[THROWER]);	/* the next frame stamps them */
 level.time+=EVENT_VALID_MSEC; G_AgeEntityEvents(&g_entities[THROWER]);
 CHECK(g_entities[THROWER].s.events[(k-1)&(MAX_ENTITY_EVENTS-1)]==EV_PREP_OFFHAND);
 level.time+=50; G_AgeEntityEvents(&g_entities[THROWER]);
 for(i=0;i<MAX_ENTITY_EVENTS;i++) CHECK(!g_entities[THROWER].s.events[i] && !g_entities[THROWER].s.eventParms[i]);
 CHECK(g_entities[THROWER].s.eventSequence==k);

 /* frag, flash and Semtex: three on the press think, nothing for anyone
  * else, and no weapon-change event (OffHandInit's drop raises none); the
  * pullback comes when the quick drop is over and the pull begins */
 for(row=FRAG;row<=SEMTEX;row++) {
  int weapon=mw2[row].weapon;
  Reset(row); Frames(0,100);
  Press(row,ev,parm,&n,&sent,sev,sparm);
  CHECK(n==3 && ev[0]==EV_SWITCH_OFFHAND && ev[1]==EV_WEAPON_SWITCH_STARTED && ev[2]==EV_RESET_ADS);
  CHECK(parm[0]==weapon && parm[1]==weapon && parm[2]==0 && sent==0);
  CHECK(ps->weapon==WP_SHOTGUN && ps->weaponstate==WEAPON_DROPPING);
  for(t=0;t<10000 && ps->weapon!=weapon;t+=CMD_MSEC) {
   Mark(); Frame(Button(weapon)); n=Raised(ev,parm); sent=Sent(sev,sparm);
   if(ps->weapon!=weapon) CHECK(n==0 && sent==0);
  }
  CHECK(n==1 && ev[0]==EV_PREP_OFFHAND && parm[0]==weapon);
  CHECK(sent==1 && sev[0]==EV_PREP_OFFHAND && sparm[0]==weapon);
  CHECK(ps->weaponstate==WEAPON_RAISING && ps->weaponTime==mw2[row].holdFireTime);
 }

 /* straight out (MW2's direct OffHandPrepare, 5791b0-5791ce): no use hint,
  * the gun's quick raise after equipment. Two events: the switch and the
  * pullback, both on the press think, for the knife and a frag alike. With a
  * hint the same press is OffHandInit's: four for the knife. */
 for(k=0;k<2;k++) {
  Reset(TKNIFE); Frames(0,100); FlashToOffhandEnd();
  if(!k) ps->cursorHintEntIndex=ENTITYNUM_NONE;
  Press(TKNIFE,ev,parm,&n,&sent,sev,sparm);
  if(!k) CHECK(n==2 && ev[0]==EV_SWITCH_OFFHAND && ev[1]==EV_PREP_OFFHAND && parm[0]==WP_THROWING_KNIFE && parm[1]==WP_THROWING_KNIFE);
  else CHECK(n==4 && ev[1]==EV_WEAPON_SWITCH_STARTED && ev[2]==EV_RESET_ADS && ev[3]==EV_PREP_OFFHAND);
  CHECK(sent==1 && sev[0]==EV_PREP_OFFHAND && ps->weapon==WP_THROWING_KNIFE);
 }
 Reset(FRAG); Frames(0,100); FlashToOffhandEnd(); ps->cursorHintEntIndex=ENTITYNUM_NONE;
 Press(FRAG,ev,parm,&n,&sent,sev,sparm);
 CHECK(n==2 && ev[0]==EV_SWITCH_OFFHAND && ev[1]==EV_PREP_OFFHAND && parm[1]==WP_FRAG);
 CHECK(ps->weapon==WP_FRAG && ps->weaponstate==WEAPON_RAISING);

 /* the knife is MW2's MeleeInit, not an offhand: none of these */
 Reset(TKNIFE); Frames(0,100); Mark(); Frame(BUTTON_MELEE); n=Raised(ev,parm);
 CHECK(ps->weapon==WP_KNIFE);
 for(i=0;i<n;i++) CHECK(ev[i]<EV_SWITCH_OFFHAND);
 /* a Y swap is the gun's own change, as before */
 Reset(TKNIFE); Frames(0,100); cmdWeapon=WP_MACHINEGUN; Mark(); Frame(0); n=Raised(ev,parm);
 CHECK(n==1 && ev[0]==EV_CHANGE_WEAPON && ps->weaponstate==WEAPON_DROPPING);

 /* G_AddEvent: a player's own external event, and on the entity's ring for
  * everyone else; any other entity's on its own ring (MW2's 0x469a50) */
 Reset(TKNIFE); Mark();
 G_AddEvent(&g_entities[THROWER],EV_PAIN,50);
 CHECK((ps->externalEvent&~EV_EVENT_BITS)==EV_PAIN && ps->externalEventParm==50);
 sent=Sent(sev,sparm); CHECK(sent==1 && sev[0]==EV_PAIN && sparm[0]==50);
 { gentity_t *e=G_Spawn(), *w=G_Spawn();
   G_AgeEntityEvents(e);
   G_AddEvent(e,EV_GENERAL_SOUND,3); G_AddEvent(e,EV_GENERAL_SOUND,4); G_AgeEntityEvents(e);
   CHECK(e->s.eventSequence==2 && !e->s.event);
   CHECK(e->s.events[0]==EV_GENERAL_SOUND && e->s.eventParms[0]==3 && e->s.events[1]==EV_GENERAL_SOUND && e->s.eventParms[1]==4);
   /* each slot ages on its own clock: the old ones go, a newer one stays */
   level.time+=EVENT_VALID_MSEC-100; G_AddEvent(e,EV_GENERAL_SOUND,6); G_AgeEntityEvents(e);
   level.time+=150; G_AgeEntityEvents(e);
   CHECK(!e->s.events[0] && !e->s.events[1] && e->s.events[2]==EV_GENERAL_SOUND && e->s.eventParms[2]==6 && e->s.eventSequence==3);
   /* the sequence is MW2's 11 bits */
   w->s.eventSequence=ES_EVENT_SEQUENCE_MASK; G_AddEvent(w,EV_GENERAL_SOUND,5);
   CHECK(w->s.eventSequence==0 && w->s.events[3]==EV_GENERAL_SOUND && w->s.eventParms[3]==5); }
}

/* Lying down (dev/STANCE.md): MW2's death drop reads no stance, so a prone
 * holder's grenade leaves BG_OFFHAND_DEATH_HEIGHT above the origin as a
 * standing one's does (5df990-5df9b0); a prone throw leaves from the prone
 * eye's muzzle, as every reach reads ps.viewheight. */
static void ProneOut(void) {
 int t;
 for(t=0;t<1000 && !(ps->viewHeightTarget==BG_EYE_PRONE && !ps->viewHeightLerpTarget);t+=CMD_MSEC) Frame(BUTTON_PRONE);
 CHECK((ps->pm_flags&PMF_PRONE) && ps->viewheight==PRONE_VIEWHEIGHT && g_entities[THROWER].r.maxs[2]==PRONE_HEIGHT);
 Frame(Button(WP_FRAG)|BUTTON_PRONE);
 for(t=0;t<10000 && !(ps->weapon==WP_FRAG && ps->weaponstate==WEAPON_READY);t+=CMD_MSEC) Frame(Button(WP_FRAG)|BUTTON_PRONE);
 CHECK(ps->weapon==WP_FRAG && ps->grenadeTimeLeft>0 && (ps->pm_flags&PMF_PRONE));
}
static void TestProne(void) {
 int from; gentity_t *m=NULL; vec3_t at;
 Reset(FRAG); from=level.num_entities;
 ProneOut();
 VectorCopy(g_entities[THROWER].r.currentOrigin,at);
 Kill();
 CHECK(Spawned(from,&m)==1 && NEAR(m->s.pos.trBase[0],at[0]) && NEAR(m->s.pos.trBase[1],at[1]) &&
  NEAR(m->s.pos.trBase[2],at[2]+BG_OFFHAND_DEATH_HEIGHT));
 /* thrown lying down: from the prone muzzle */
 Reset(FRAG); from=level.num_entities;
 ProneOut();
 Command(BUTTON_PRONE);
 CHECK(Spawned(from,&m)==1 && (ps->weaponAction&WA_FIRED));
 { vec3_t forward, muzzle; AngleVectors(ps->viewangles,forward,NULL,NULL);
   VectorCopy(g_entities[THROWER].s.pos.trBase,muzzle); muzzle[2]+=PRONE_VIEWHEIGHT; VectorMA(muzzle,14,forward,muzzle);
   SnapVector(muzzle); CHECK(VectorCompare(m->s.pos.trBase,muzzle)); }
 World();
}

int main(void) {
 TestData(); TestThrow(); TestCook(); TestChoppedCookOut(); TestDeathDrop(); TestReleaseOnFatalFall();
 TestProne();
 TestEventRing();
 printf("PASS: %d offhand server checks\n",checks); return 0;
}
