/* MW2's Tactical Insertion through the real server code: the plant on its fire
 * (FireWeapon, g_weapon.c, and G_PlantInsertion, g_insertion.c), the standing
 * spot it is planted from (G_InsertionSample), its damage through the real
 * G_Damage and G_RadiusDamage (g_combat.c) from real shots (Bullet_Fire, the
 * railgun), G_RunMissile leaving it be (g_missile.c), the spawn on it inside
 * the real ClientSpawn (g_client.c), and the ways it ends: the real
 * ClientDisconnect, SetTeam (g_cmds.c) and G_FreeEntity (g_utils.c), whose free
 * empties any use hint naming it (g_items.c's hint pass). The world is a floor
 * at z = 0 plus solid boxes, traced as a box sweep against them and the linked
 * entities; only engine traps and modules outside these files are stubbed. */
#include "../../code/game/g_local.h"
static int checks, endFrames, spawnHinted;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define NEAR(a,b) (fabs((a)-(b))<0.01f)
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
static gclient_t cl[3];
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

/* ---- the world: a floor at z = 0, solid boxes, and the linked entities ---- */
static int worldBoxes;
static vec3_t worldLo[8], worldHi[8];
static void WorldBox(float x0,float y0,float z0,float x1,float y1,float z1) {
 VectorSet(worldLo[worldBoxes],x0,y0,z0); VectorSet(worldHi[worldBoxes],x1,y1,z1); worldBoxes++;
}
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
 if(mask&(CONTENTS_SOLID|CONTENTS_PLAYERCLIP)) {
  Sweep(t,start,end,mins,maxs,floorLo,floorHi,ENTITYNUM_WORLD,CONTENTS_SOLID);
  for(i=0;i<worldBoxes;i++) Sweep(t,start,end,mins,maxs,worldLo[i],worldHi[i],ENTITYNUM_WORLD,CONTENTS_SOLID);
 }
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
void trap_GetUsercmd(int n,usercmd_t *cmd) { (void)n; memset(cmd,0,sizeof(*cmd)); }
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
qboolean G_SNDCanDamage(gentity_t *e) { (void)e; return qtrue; }
qboolean G_SNDUsable(gentity_t *e) { (void)e; return qfalse; }
qboolean G_SNDUse(gentity_t *e,usercmd_t *c) { (void)e;(void)c; return qfalse; }
team_t G_SNDSpawnTeam(team_t t) { return t; }
void G_SNDPlayerKilled(gentity_t *v,gentity_t *a) { (void)v;(void)a; }
void G_SNDRecordKill(gentity_t *v,gentity_t *a) { (void)v;(void)a; }
#ifdef MISSIONPACK
qboolean CheckObeliskAttack(gentity_t *o,gentity_t *a) { (void)o;(void)a; return qfalse; }
gentity_t *neutralObelisk;
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
void ClientThink(int n) { (void)n; }
/* ClientEndFrame's two server-side steps this file is about, in its order (the
 * real one is wired in cursor_hint_wiring_test.c) */
void ClientEndFrame(gentity_t *e) {
 endFrames++; G_InsertionSample(e); G_UpdateCursorHint(e);
 if(e->client->ps.cursorHintEntIndex>=MAX_CLIENTS && e->client->ps.cursorHintEntIndex<ENTITYNUM_MAX_NORMAL &&
  G_IsInsertion(&g_entities[e->client->ps.cursorHintEntIndex])) spawnHinted++;
}
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
void G_RunThink(gentity_t *e) {
 if(e->nextthink<=0 || e->nextthink>level.time) return;
 e->nextthink=0; if(e->think) e->think(e);
}
void G_RunClient(gentity_t *e) { (void)e; }
qboolean G_SNDClientEndFrame(gentity_t *e) { (void)e; return qfalse; }

/* ---- the fixture ---- */
static void Link(gentity_t *e) {
 VectorCopy(e->client->ps.origin,e->r.currentOrigin); VectorCopy(e->client->ps.origin,e->s.pos.trBase); trap_LinkEntity(e);
}
static void Stand(int n,float x,float y,float z) {
 gentity_t *e=&g_entities[n]; playerState_t *ps=&cl[n].ps;
 VectorSet(ps->origin,x,y,z); ps->groundEntityNum=ENTITYNUM_WORLD; Link(e);
}
static bg_class_t InsertionClassDef(void) {
 bg_class_t c=*BG_Class(0); c.lethal=WP_TACTICAL_INSERTION; c.lethalCount=1; return c;
}
static void Alive(int n) {
 gentity_t *e=&g_entities[n];
 e->health=cl[n].ps.stats[STAT_HEALTH]=100; e->takedamage=qtrue; e->r.contents=CONTENTS_BODY;
 cl[n].ps.pm_type=PM_NORMAL; cl[n].ps.viewheight=DEFAULT_VIEWHEIGHT;
 VectorSet(e->r.mins,-PLAYER_WIDTH,-PLAYER_WIDTH,MINS_Z); VectorSet(e->r.maxs,PLAYER_WIDTH,PLAYER_WIDTH,DEFAULT_HEIGHT);
 e->die=player_die;
}
static gentity_t *spawnSpot;
static void Reset(int gametype) {
 int i;
 memset(&level,0,sizeof(level)); memset(g_entities,0,sizeof(g_entities)); memset(cl,0,sizeof(cl));
 level.time=10000; level.startTime=0; level.num_entities=MAX_CLIENTS; level.maxclients=3;
 level.clients=cl; level.gentities=g_entities;
 g_gametype.integer=gametype; g_knockback.integer=1000; g_friendlyFire.integer=0;
 worldBoxes=endFrames=spawnHinted=0;
 for(i=0;i<3;i++) {
  gentity_t *e=&g_entities[i];
  e->client=&cl[i]; e->inuse=qtrue; e->s.number=i; e->classname="player"; e->r.ownerNum=ENTITYNUM_NONE;
  cl[i].ps.clientNum=i; cl[i].pers.connected=CON_CONNECTED; cl[i].pers.enterTime=100+i;
  cl[i].pers.selectedLoadout=InsertionClassDef(); cl[i].pers.maxHealth=100;
  cl[i].sess.sessionTeam=gametype>=GT_TEAM ? (i==1 ? TEAM_BLUE : TEAM_RED) : TEAM_FREE;
  BG_ApplyClass(&cl[i].ps,&cl[i].pers.selectedLoadout); cl[i].ps.stats[STAT_MAX_HEALTH]=100;
  cl[i].useHold=ENTITYNUM_NONE; Alive(i); Stand(i,-400+200*i,-300,24);
 }
 InitBodyQue();
 /* the map's one spawn point, well away from everything */
 spawnSpot=G_Spawn(); spawnSpot->classname="info_player_deathmatch";
 VectorSet(spawnSpot->s.origin,-800,0,24); VectorSet(spawnSpot->s.angles,0,90,0);
}
static int Insertions(void) {
 int i,n=0; for(i=MAX_CLIENTS;i<level.num_entities;i++) if(G_IsInsertion(&g_entities[i])) n++; return n;
}
/* The owner throws it: FireWeapon on its EV_FIRE_WEAPON, as ClientEvents runs it. */
static gentity_t *Fire(int n) {
 gentity_t *e=&g_entities[n];
 e->s.weapon=WP_TACTICAL_INSERTION; VectorCopy(cl[n].ps.origin,cl[n].oldOrigin);
 FireWeapon(e);
 return G_FindInsertion(e);
}
/* ClientEndFrame for every player */
static void EndFrames(void) { int i; for(i=0;i<3;i++) if(g_entities[i].inuse) ClientEndFrame(&g_entities[i]); }

static void TestPlant(void) {
 gentity_t *ti,*first; vec3_t at,end; trace_t tr;
 Reset(GT_FFA);
 /* no standing spot sampled this life: nothing at all is planted */
 CHECK(!cl[0].insertionSpotValid && !Fire(0) && !Insertions());
 /* the owner stands at (100, 50), facing 30 degrees, and the end-frame samples it */
 Stand(0,100,50,24); cl[0].ps.viewangles[YAW]=30; cl[0].ps.viewangles[PITCH]=-40; EndFrames();
 CHECK(cl[0].insertionSpotValid && VectorCompare(cl[0].insertionSpot,cl[0].ps.origin));
 ti=Fire(0);
 CHECK(ti && Insertions()==1 && ti->s.eType==ET_MISSILE && ti->s.weapon==WP_TACTICAL_INSERTION);
 /* TIGroundPosition: the box's floor under the spot, plus 1 */
 CHECK(ti->s.pos.trType==TR_STATIONARY && NEAR(ti->r.currentOrigin[0],100) && NEAR(ti->r.currentOrigin[1],50) &&
  NEAR(ti->r.currentOrigin[2],1) && VectorCompare(ti->s.pos.trBase,ti->r.currentOrigin));
 CHECK(VectorCompare(ti->pos1,cl[0].ps.origin));	/* playerSpawnPos: the spot itself */
 CHECK(NEAR(ti->s.angles[YAW],30) && ti->s.angles[PITCH]==0 && ti->s.angles[ROLL]==0 && ti->s.apos.trType==TR_STATIONARY);
 CHECK(ti->r.contents==CONTENTS_CORPSE && ti->takedamage && ti->r.linked && ti->r.ownerNum==ENTITYNUM_NONE);
 CHECK(ti->s.clientNum==0 && ti->s.generic1==TEAM_FREE && G_InsertionOwner(ti)==&g_entities[0] && ti->timestamp==level.time);
 /* shootable, but never in a player's way (the owner stepped aside) */
 CHECK(ti->r.contents&MASK_SHOT); CHECK(!(ti->r.contents&MASK_PLAYERSOLID));
 Stand(0,100,200,24);
 VectorSet(at,60,50,24); VectorSet(end,140,50,24);
 trap_Trace(&tr,at,g_entities[1].r.mins,g_entities[1].r.maxs,end,1,MASK_PLAYERSOLID); CHECK(tr.fraction==1);
 VectorSet(at,60,50,4); VectorSet(end,140,50,4);
 trap_Trace(&tr,at,NULL,NULL,end,1,MASK_SHOT); CHECK(tr.entityNum==ti->s.number);
 /* G_RunMissile leaves it where it is, frame after frame */
 { int i; for(i=0;i<100;i++) { level.previousTime=level.time; level.time+=50; G_RunMissile(ti); } }
 CHECK(ti->inuse && NEAR(ti->r.currentOrigin[2],1) && ti->s.pos.trType==TR_STATIONARY);
 /* thrown in the air (or crouched under a low ceiling): the last good spot */
 Stand(0,300,50,200); cl[0].ps.groundEntityNum=ENTITYNUM_NONE; EndFrames();
 WorldBox(380,0,40,420,100,60);	/* a ceiling 40 over the floor */
 Stand(0,400,50,24); EndFrames();
 CHECK(cl[0].insertionSpot[0]==100);
 first=ti; ti=Fire(0);
 /* one per player: the new plant replaces the old */
 CHECK(!first->inuse || first!=ti); CHECK(Insertions()==1 && ti && NEAR(ti->r.currentOrigin[0],100));
 /* standing on a crate: it lies on the crate, and is spawned on at the spot */
 WorldBox(580,0,0,620,100,40);
 Stand(0,600,50,64); EndFrames(); ti=Fire(0);
 CHECK(Insertions()==1 && NEAR(ti->r.currentOrigin[2],41) && NEAR(ti->pos1[2],64));
 /* another player's is their own: untouched by this owner's plants */
 Stand(1,-200,50,24); EndFrames(); first=Fire(1); CHECK(first && Insertions()==2);
 Stand(0,100,50,24); EndFrames(); ti=Fire(0); CHECK(first->inuse && Insertions()==2);
 /* a new life has no spot until it stands somewhere: the plant only frees the
  * old one (MW2's monitorTIUse deletes it first, then finds no spot) */
 cl[0].insertionSpotValid=qfalse; CHECK(!Fire(0) && !ti->inuse && Insertions()==1);
 /* dead (player_die has marked it, MW2's 'death' that ends monitorTIUse):
  * nothing. A TI held at death is planted by the death drop before that
  * mark (offhand_server_test.c). */
 Stand(0,100,50,24); EndFrames(); g_entities[0].health=cl[0].ps.stats[STAT_HEALTH]=0; cl[0].ps.pm_type=PM_DEAD;
 CHECK(!G_PlantInsertion(&g_entities[0]) && Insertions()==1);
 /* the dead sample nothing */
 cl[0].insertionSpotValid=qfalse; EndFrames(); CHECK(!cl[0].insertionSpotValid);
 /* touching a trigger_hurt: nothing (MW2's monitorTIUse) */
 Alive(0); Stand(0,100,50,24); EndFrames();
 { gentity_t *hurt=G_Spawn(); hurt->classname="trigger_hurt"; hurt->r.contents=CONTENTS_TRIGGER;
   VectorSet(hurt->r.currentOrigin,100,50,0); VectorSet(hurt->r.mins,-64,-64,-64); VectorSet(hurt->r.maxs,64,64,64);
   trap_LinkEntity(hurt);
   CHECK(!Fire(0) && Insertions()==1);
   trap_UnlinkEntity(hurt); CHECK(Fire(0) && Insertions()==2); }
}

/* MW2's GlowStickDamageListener through the real G_Damage: more than 20
 * counted damage destroys it, from an enemy or its owner; in a team game a
 * teammate's (not the owner's) never counts. Freed, it empties the hints. */
/* planted standing at x, y; then the planter steps out of the line of fire */
static gentity_t *Planted(int n,float x,float y) {
 gentity_t *ti;
 Stand(n,x,y,24); EndFrames(); ti=Fire(n); CHECK(ti!=NULL);
 Stand(n,x,y+150,24); return ti;
}
static void TestDamage(void) {
 gentity_t *ti; vec3_t dir={1,0,0}, point; int i;
 Reset(GT_FFA); ti=Planted(0,100,50); VectorCopy(ti->r.currentOrigin,point);
 G_Damage(ti,&g_entities[1],&g_entities[1],dir,point,20,0,MOD_MACHINEGUN);
 CHECK(ti->inuse && ti->health==BG_INSERTION_HEALTH);
 G_Damage(ti,&g_entities[1],&g_entities[1],dir,point,1,0,MOD_MACHINEGUN);
 CHECK(!ti->inuse && !Insertions());
 /* the owner's own damage counts, and FFA counts anyone's */
 ti=Planted(0,100,50); G_Damage(ti,&g_entities[0],&g_entities[0],dir,point,21,0,MOD_MACHINEGUN); CHECK(!ti->inuse);
 ti=Planted(0,100,50); for(i=0;i<3;i++) G_Damage(ti,&g_entities[2],&g_entities[2],dir,point,7,0,MOD_MACHINEGUN);
 CHECK(!ti->inuse);
 /* the world's too (a fall, a trigger_hurt) */
 ti=Planted(0,100,50); G_Damage(ti,NULL,NULL,NULL,NULL,25,0,MOD_TRIGGER_HURT); CHECK(!ti->inuse);
 /* one huge hit is no different: destroyed, not left on a wrapped health */
 ti=Planted(0,100,50); G_Damage(ti,&g_entities[1],&g_entities[1],dir,point,100000,DAMAGE_NO_PROTECTION,MOD_TELEFRAG);
 CHECK(!ti->inuse);
 /* a team game: a teammate's damage does nothing, however much; the owner's
  * and the other team's count */
 Reset(GT_TEAM); ti=Planted(0,100,50); VectorCopy(ti->r.currentOrigin,point);
 CHECK(ti->s.generic1==TEAM_RED && cl[2].sess.sessionTeam==TEAM_RED && cl[1].sess.sessionTeam==TEAM_BLUE);
 for(i=0;i<10;i++) G_Damage(ti,&g_entities[2],&g_entities[2],dir,point,50,0,MOD_MACHINEGUN);
 G_Damage(ti,&g_entities[2],&g_entities[2],dir,point,100000,DAMAGE_NO_PROTECTION,MOD_TELEFRAG);
 CHECK(ti->inuse && ti->count==0 && ti->health==BG_INSERTION_HEALTH);
 G_Damage(ti,&g_entities[1],&g_entities[1],dir,point,10,0,MOD_MACHINEGUN);
 G_Damage(ti,&g_entities[0],&g_entities[0],dir,point,10,0,MOD_MACHINEGUN);
 CHECK(ti->inuse && ti->count==20);
 G_Damage(ti,&g_entities[2],&g_entities[2],dir,point,50,0,MOD_MACHINEGUN); CHECK(ti->inuse);
 G_Damage(ti,&g_entities[0],&g_entities[0],dir,point,1,0,MOD_MACHINEGUN); CHECK(!ti->inuse);
 /* real shots: the railgun's beam (relinking nothing it destroyed) and a
  * machinegun's bullets, aimed at it from the enemy's eye */
 Reset(GT_FFA); ti=Planted(0,100,50);
 Stand(1,-100,50,24); cl[1].ps.viewangles[YAW]=0;
 cl[1].ps.viewangles[PITCH]=RAD2DEG(atan2(cl[1].ps.viewheight+24-4,200));
 g_entities[1].s.weapon=WP_RAILGUN; VectorCopy(cl[1].ps.origin,cl[1].oldOrigin); FireWeapon(&g_entities[1]);
 CHECK(!ti->inuse && !ti->r.linked && !Insertions());
 ti=Planted(0,100,50); Stand(1,-100,50,24); cl[1].ps.pm_flags|=PMF_ADS; VectorClear(cl[1].ps.velocity);
 g_entities[1].s.weapon=WP_MACHINEGUN;
 for(i=0;i<10 && ti->inuse;i++) FireWeapon(&g_entities[1]);
 CHECK(!ti->inuse && i==3);	/* 7 a bullet: the third makes it 21 */
 /* splash, through the real G_RadiusDamage */
 ti=Planted(0,100,50); VectorSet(point,130,50,10);
 G_RadiusDamage(point,&g_entities[1],100,120,NULL,MOD_GRENADE_SPLASH); CHECK(!ti->inuse);
 /* destroyed while it is a player's hint: their hint is empty (the sea lion) */
 ti=Planted(0,100,50); Stand(0,90,50,24); Stand(1,140,50,24); EndFrames();
 CHECK(cl[1].ps.cursorHintEntIndex==ti->s.number && cl[1].ps.cursorHint==CURSOR_HINT_INSERTION_ENEMY);
 CHECK(cl[0].ps.cursorHintEntIndex==ti->s.number && cl[0].ps.cursorHint==CURSOR_HINT_INSERTION);
 G_Damage(ti,&g_entities[1],&g_entities[1],dir,ti->r.currentOrigin,30,0,MOD_MACHINEGUN);
 CHECK(!ti->inuse && cl[1].ps.cursorHintEntIndex==ENTITYNUM_NONE && cl[0].ps.cursorHintEntIndex==ENTITYNUM_NONE);
}

/* MW2's spawn on it, inside the real ClientSpawn: at playerSpawnPos facing its
 * yaw, the TI freed before the spawn frame's hint pass; then the next life
 * spawns normally. Another player in the box: a normal spawn, the TI freed,
 * nobody telefragged. Never in S&D. */
static void Die(int n) { g_entities[n].health=cl[n].ps.stats[STAT_HEALTH]=0; cl[n].ps.pm_type=PM_DEAD; }
/* Player 1 dies on player 0's TI, shot by player 2 (in a team game player 2 is
 * player 0's teammate), and is still a corpse when player 0 spawns on it. */
static void CorpseOnInsertion(int gametype) {
 gentity_t *ti,*body=&g_entities[1]; vec3_t spot, dir={1,0,0}; int event;
 Reset(gametype); g_blood.integer=1;
 ti=Planted(0,100,50); VectorCopy(ti->pos1,spot);
 Stand(1,spot[0]+10,spot[1],spot[2]);
 G_Damage(body,&g_entities[2],&g_entities[2],dir,body->r.currentOrigin,101,0,MOD_MACHINEGUN);
 CHECK(body->health==-1 && body->die==body_die && body->r.contents==CONTENTS_CORPSE && body->r.linked);
 CHECK(body->takedamage && body->s.eType!=ET_INVISIBLE && cl[1].ps.persistant[PERS_ATTACKER]==2);
 CHECK(cl[1].lasthurt_client==2 && cl[1].lasthurt_mod==MOD_MACHINEGUN);
 event=cl[1].ps.externalEvent;	/* player_die's own events, no gib */
 CHECK((event&~EV_EVENT_BITS)!=EV_GIB_PLAYER);
 Stand(0,-300,300,24); Die(0); ClientSpawn(&g_entities[0]);
 CHECK(!ti->inuse && !Insertions() && VectorCompare(cl[0].ps.origin,spot) && g_entities[0].health>0);
 CHECK(body->health==-1 && body->die==body_die && body->r.contents==CONTENTS_CORPSE && body->r.linked);
 CHECK(body->takedamage && body->s.eType!=ET_INVISIBLE && cl[1].ps.externalEvent==event);
 CHECK(cl[1].ps.persistant[PERS_ATTACKER]==2 && cl[1].lasthurt_client==2 && cl[1].lasthurt_mod==MOD_MACHINEGUN);
 g_blood.integer=0;
}
static void TestSpawn(void) {
 gentity_t *ti; vec3_t spot; int number, hinted;
 Reset(GT_FFA); cl[0].ps.viewangles[YAW]=30; ti=Planted(0,100,50); number=ti->s.number; VectorCopy(ti->pos1,spot);
 CHECK(NEAR(ti->s.angles[YAW],30));
 Stand(0,100,60,24); EndFrames(); CHECK(cl[0].ps.cursorHintEntIndex==number);
 /* an enemy hinted on it, as the owner dies and respawns */
 Stand(1,140,50,24); EndFrames(); CHECK(cl[1].ps.cursorHintEntIndex==number);
 Stand(0,-300,300,24); cl[0].ps.viewangles[YAW]=200; Die(0);
 spawnHinted=0; hinted=endFrames;
 ClientSpawn(&g_entities[0]);
 CHECK(endFrames==hinted+1 && !spawnHinted);	/* its first hint pass never saw it */
 CHECK(VectorCompare(cl[0].ps.origin,spot) && fabs(AngleNormalize180(cl[0].ps.viewangles[YAW]-30))<0.01f);
 CHECK(cl[0].ps.viewangles[PITCH]==0 && cl[0].ps.viewangles[ROLL]==0);
 CHECK(!ti->inuse && !Insertions() && cl[1].ps.cursorHintEntIndex==ENTITYNUM_NONE);
 CHECK(cl[0].ps.cursorHintEntIndex!=number && g_entities[0].health>0 && cl[0].ps.ammo[WP_TACTICAL_INSERTION]==1);
 CHECK(cl[0].insertionSpotValid && VectorCompare(cl[0].insertionSpot,spot));	/* the new life's first spot */
 CHECK(g_entities[1].health>0);
 /* the next life spawns normally: it was used up */
 Die(0); ClientSpawn(&g_entities[0]);
 CHECK(cl[0].ps.origin[0]==-800 && cl[0].ps.origin[1]==0 && NEAR(cl[0].ps.viewangles[YAW],90));
 /* someone standing on it: a normal spawn, the TI gone, nobody telefragged */
 ti=Planted(0,100,50); VectorCopy(ti->pos1,spot);
 Stand(1,spot[0]+10,spot[1],spot[2]); Stand(0,-300,300,24); Die(0);
 ClientSpawn(&g_entities[0]);
 CHECK(!ti->inuse && !Insertions() && cl[0].ps.origin[0]==-800 && g_entities[1].health==100);
 /* a dead body there is no one standing: spawned on */
 ti=Planted(0,100,50); Stand(1,spot[0]+10,spot[1],spot[2]); g_entities[1].r.contents=CONTENTS_CORPSE; Die(1);
 Stand(0,-300,300,24); Die(0); ClientSpawn(&g_entities[0]);
 CHECK(!ti->inuse && VectorCompare(cl[0].ps.origin,spot));
 /* ... and nobody is telefragged, a corpse included: killed through the real
  * G_Damage (player_die leaves it linked, CONTENTS_CORPSE and body_die until
  * that player respawns), it is neither gibbed nor credited to the spawner */
 CorpseOnInsertion(GT_FFA); CorpseOnInsertion(GT_TEAM);
 /* a team game: the same */
 Reset(GT_TEAM); ti=Planted(0,100,50); VectorCopy(ti->pos1,spot);
 Stand(0,-300,300,24); Die(0); ClientSpawn(&g_entities[0]);
 CHECK(!ti->inuse && VectorCompare(cl[0].ps.origin,spot));
 /* S&D: planted, never spawned on; it stays for the round (g_snd.c frees it) */
 Reset(GT_SND); ti=Planted(0,100,50); Stand(0,-300,300,24); Die(0); ClientSpawn(&g_entities[0]);
 CHECK(ti->inuse && VectorCompare(cl[0].ps.origin,ctfSpot));
 /* a new connection in the owner's slot is not the owner: no spawn on it */
 Reset(GT_FFA); ti=Planted(0,100,50);
 cl[0].pers.enterTime=level.time+1; CHECK(!G_InsertionOwner(ti) && !G_FindInsertion(&g_entities[0]));
 Die(0); ClientSpawn(&g_entities[0]); CHECK(ti->inuse && cl[0].ps.origin[0]==-800);
 G_FreeInsertion(&g_entities[0]); CHECK(!ti->inuse);	/* freed by the slot all the same */
}

/* MW2's clearPreviousTISpawnpoint: a team change, joining the spectators and a
 * disconnect free it, through the real SetTeam and ClientDisconnect. Death
 * leaves it. */
static void TestCleanup(void) {
 gentity_t *ti,*other;
 Reset(GT_TEAM); ti=Planted(0,100,50); other=Planted(1,-100,50);
 G_Damage(&g_entities[0],&g_entities[1],&g_entities[1],NULL,NULL,1000,0,MOD_MACHINEGUN);
 CHECK(g_entities[0].health<=0 && ti->inuse);	/* death leaves it */
 SetTeam(&g_entities[0],"blue");
 CHECK(!ti->inuse && other->inuse && cl[0].sess.sessionTeam==TEAM_BLUE);
 Alive(0); ti=Planted(0,100,50); SetTeam(&g_entities[0],"spectator");
 CHECK(!ti->inuse && other->inuse && cl[0].sess.sessionTeam==TEAM_SPECTATOR);
 Reset(GT_FFA); ti=Planted(0,100,50); other=Planted(1,-100,50);
 ClientDisconnect(0);
 CHECK(!ti->inuse && other->inuse && Insertions()==1);
}

int main(void) {
 TestPlant(); TestDamage(); TestSpawn(); TestCleanup();
 printf("PASS: %d Tactical Insertion server checks (plant, damage, spawn, cleanup)\n",checks);
 return 0;
}
