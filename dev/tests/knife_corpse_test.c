/* A throwing knife stuck in a player, through the real server code: flight
 * and carry (g_missile.c), the corpse it moves to and the corpse's sinking and
 * reuse (CopyToBodyQue, BodySink in g_client.c), G_Spawn/G_FreeEntity
 * (g_utils.c), and the use hint and pickup (G_UpdateCursorHint, which
 * ClientEndFrame runs, and G_UseThink in g_items.c). The world is a floor at z = 0 and the linked
 * entities' boxes; only engine traps and modules outside these files are
 * stubbed. */
#include "../../code/game/g_local.h"
static int checks, damageCalls;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
static gclient_t cl[2];
vmCvar_t g_gametype, g_weaponRespawn, g_weaponTeamRespawn, g_smoothClients;
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_Error(int code,const char *fmt,...) { (void)code;(void)fmt; abort(); }
void QDECL G_Error(const char *fmt,...) { (void)fmt; abort(); }
void QDECL G_Printf(const char *fmt,...) { (void)fmt; }
void QDECL G_LogPrintf(const char *fmt,...) { (void)fmt; }
/* The thrower is never hit by their own knife. A hit kills, as the
 * placeholder damage does, and the player lies down. */
void G_Damage(gentity_t *t,gentity_t *i,gentity_t *a,vec3_t d,vec3_t p,int damage,int flags,int mod) {
 (void)i;(void)d;(void)p;(void)flags;(void)mod;
 CHECK(t!=&g_entities[0] && a==&g_entities[0]); damageCalls++;
 if (t->client && (t->health-=damage)<=0) { t->client->ps.stats[STAT_HEALTH]=t->health; t->r.maxs[2]=-8; }
}
void BodySink(gentity_t *ent);
void body_die(gentity_t *s,gentity_t *i,gentity_t *a,int damage,int mod) { (void)s;(void)i;(void)a;(void)damage;(void)mod; }
qboolean LogAccuracyHit(gentity_t *t,gentity_t *a) { (void)t;(void)a; return qfalse; }
qboolean G_RadiusDamage(vec3_t p,gentity_t *a,float d,float r,gentity_t *ig,int mod) { (void)p;(void)a;(void)d;(void)r;(void)ig;(void)mod; abort(); }
void Weapon_HookThink(gentity_t *e) { (void)e; }
void Weapon_HookFree(gentity_t *e) { (void)e; }
void SnapVectorTowards(vec3_t v,vec3_t to) { (void)to; SnapVector(v); }
void G_RunThink(gentity_t *e) { if(e->nextthink>0 && e->nextthink<=level.time) { e->nextthink=0; e->think(e); } }
qboolean G_SNDUsable(gentity_t *e) { (void)e; return qfalse; }
qboolean G_SNDUse(gentity_t *e,usercmd_t *c) { (void)e;(void)c; return qfalse; }
int Pickup_Team(gentity_t *e,gentity_t *o) { (void)e;(void)o; return 1; }
void Team_CheckDroppedItem(gentity_t *e) { (void)e; }
void Team_DroppedFlagThink(gentity_t *e) { (void)e; }
void trap_GetConfigstring(int n,char *b,int s) { (void)n; if(s>0)b[0]=0; }
void trap_GetUserinfo(int n,char *b,int s) { (void)n; if(s>0)b[0]=0; }
void trap_SetConfigstring(int n,const char *s) { (void)n;(void)s; }
void trap_SnapVector(float *v) { v[0]=rintf(v[0]); v[1]=rintf(v[1]); v[2]=rintf(v[2]); }
void trap_LinkEntity(gentity_t *e) { e->r.linked=qtrue; }
void trap_UnlinkEntity(gentity_t *e) { e->r.linked=qfalse; }
void trap_LocateGameData(gentity_t *g,int n,int s,playerState_t *c,int cs) { (void)g;(void)n;(void)s;(void)c;(void)cs; }
int trap_PointContents(const vec3_t p,int pass) { (void)p;(void)pass; return 0; }
static qboolean Inside(const vec3_t p,const vec3_t lo,const vec3_t hi) {
 return p[0]>lo[0] && p[0]<hi[0] && p[1]>lo[1] && p[1]<hi[1] && p[2]>lo[2] && p[2]<hi[2];
}
/* a point against a box: the nearest entry, its face normal */
static void Box(trace_t *t,const vec3_t s,const vec3_t e,const vec3_t lo,const vec3_t hi,int num) {
 float enter=0,leave=1; vec3_t normal={0,0,0}; int k;
 if (Inside(s,lo,hi)) { if (t->fraction>0) { t->fraction=0; t->startsolid=qtrue; t->allsolid=Inside(e,lo,hi);
  VectorClear(t->plane.normal); t->entityNum=num; } return; }
 for (k=0;k<3;k++) {
  float d=e[k]-s[k], t0, t1, side=-1;
  if (d==0) { if (s[k]<lo[k] || s[k]>hi[k]) return; continue; }
  t0=(lo[k]-s[k])/d; t1=(hi[k]-s[k])/d;
  if (t0>t1) { float x=t0; t0=t1; t1=x; side=1; }
  if (t0>enter) { enter=t0; VectorClear(normal); normal[k]=side; }
  if (t1<leave) leave=t1;
  if (enter>leave) return;
 }
 if (enter<t->fraction) { t->fraction=enter; VectorCopy(normal,t->plane.normal); t->entityNum=num; }
}
void trap_Trace(trace_t *t,const vec3_t start,const vec3_t mins,const vec3_t maxs,const vec3_t end,int pass,int mask) {
 int i;
 (void)mins;(void)maxs;
 memset(t,0,sizeof(*t)); t->fraction=1; t->entityNum=ENTITYNUM_NONE;
 if (mask&CONTENTS_SOLID) {
  if (start[2]<0) { t->fraction=0; t->startsolid=qtrue; t->allsolid=end[2]<0; t->entityNum=ENTITYNUM_WORLD; }
  else if (end[2]<0) { t->fraction=start[2]/(start[2]-end[2]); VectorSet(t->plane.normal,0,0,1); t->entityNum=ENTITYNUM_WORLD; }
 }
 for (i=0;i<level.num_entities;i++) {
  gentity_t *e=&g_entities[i]; vec3_t lo,hi;
  if (i==pass || !e->inuse || !e->r.linked || !(e->r.contents&mask)) continue;
  VectorAdd(e->r.currentOrigin,e->r.mins,lo); VectorAdd(e->r.currentOrigin,e->r.maxs,hi);
  Box(t,start,end,lo,hi,i);
 }
 for (i=0;i<3;i++) t->endpos[i]=start[i]+t->fraction*(end[i]-start[i]);
}
static void Place(gentity_t *e,float x,float y) {
 VectorSet(e->client->ps.origin,x,y,24); VectorCopy(e->client->ps.origin,e->s.pos.trBase);
 VectorCopy(e->client->ps.origin,e->r.currentOrigin); e->s.pos.trType=TR_INTERPOLATE; trap_LinkEntity(e);
}
static void Spawn(gentity_t *e,float x,float y) {
 e->health=e->client->ps.stats[STAT_HEALTH]=100; e->client->ps.persistant[PERS_SPAWN_COUNT]++;
 VectorSet(e->r.mins,-15,-15,-24); VectorSet(e->r.maxs,15,15,32); e->r.contents=CONTENTS_BODY;
 e->s.groundEntityNum=ENTITYNUM_WORLD; Place(e,x,y);
}
/* one server frame for everything that runs */
static void Frame(void) {
 int i;
 level.previousTime=level.time; level.time+=50;
 for (i=MAX_CLIENTS;i<level.num_entities;i++) {
  gentity_t *e=&g_entities[i];
  if (!e->inuse || (!e->r.linked && e->neverFree)) continue;
  if (e->s.eType==ET_MISSILE) G_RunMissile(e); else G_RunThink(e);
 }
}
static gentity_t *Throw(void) {
 vec3_t start={0,0,12},dir={1,0,0}; gentity_t *k;	/* low enough to stay in the corpse */
 Place(&g_entities[0],0,0); Spawn(&g_entities[1],100,0);
 k=fire_throwingknife(&g_entities[0],start,dir);
 while (k->s.pos.trType==TR_GRAVITY) Frame();
 return k;
}
/* Is the knife the thrower's use hint, standing at x, y? */
static qboolean Hinted(gentity_t *k,float x,float y) {
 Place(&g_entities[0],x,y); G_UpdateCursorHint(&g_entities[0]);
 return cl[0].ps.cursorHintEntIndex==k->s.number;
}
static qboolean Carried(gentity_t *k,gentity_t *by) {
 return k->inuse && k->enemy==by && k->s.pos.trType==TR_INTERPOLATE && VectorLength(k->s.pos.trDelta)==0 &&
  VectorCompare(k->s.pos.trBase,k->r.currentOrigin) && k->r.linked;
}
/* fall to the floor and rest there for good */
static void Lands(gentity_t *k) {
 int i;
 for (i=0;i<100 && k->s.pos.trType!=TR_STATIONARY;i++) Frame();
 CHECK(k->s.pos.trType==TR_STATIONARY && k->restNum==ENTITYNUM_WORLD && k->r.currentOrigin[2]>0 && k->r.currentOrigin[2]<2);
}
int main(void) {
 gentity_t *k,*body,*other; vec3_t at; int i,slot,n;
 usercmd_t cmd;
 memset(&level,0,sizeof(level));
 level.time=10000; level.num_entities=MAX_CLIENTS; level.maxclients=2;
 level.clients=cl; level.gentities=g_entities;
 for(i=0;i<2;i++) {
  gentity_t *e=&g_entities[i]; e->client=&cl[i]; e->inuse=qtrue; e->s.number=i; e->takedamage=qtrue;
  BG_ApplyClass(&cl[i].ps,BG_Class(0)); cl[i].ps.clientNum=i; cl[i].ps.pm_type=PM_NORMAL; cl[i].ps.viewheight=26;
  cl[i].pers.connected=CON_CONNECTED; cl[i].pers.enterTime=i+1; cl[i].lastCmdTime=level.time;
  Spawn(e,i*100,0);
 }
 cl[0].knifeMax=2; cl[0].ps.ammo[WP_THROWING_KNIFE]=0; cl[0].knifeHold=ENTITYNUM_NONE;
 InitBodyQue();
 /* thrown into the victim: one hit, buried at the hit and carried by them */
 k=Throw();
 CHECK(damageCalls==1 && Carried(k,&g_entities[1]) && k->r.currentOrigin[0]<85);
 /* a player walking to it sees it, on the victim, wherever they go */
 level.time+=BG_USE_SPAWN_DELAY;
 g_entities[1].r.currentOrigin[0]+=20; Frame();
 CHECK(Carried(k,&g_entities[1]) && k->r.currentOrigin[0]>85 && Hinted(k,60,0));
 /* the victim respawns: the knife stays in their corpse, not with them */
 VectorCopy(k->r.currentOrigin,at);
 CopyToBodyQue(&g_entities[1]); body=level.bodyQue[0]; Spawn(&g_entities[1],-500,0);
 Frame();
 CHECK(Carried(k,body) && VectorCompare(at,k->r.currentOrigin) && Hinted(k,60,0));
 /* taken from the corpse: freed, and nothing is left pointing at it */
 slot=k->s.number; memset(&cmd,0,sizeof(cmd)); cmd.buttons=BUTTON_USE;
 G_UseThink(&g_entities[0],&cmd);
 CHECK(!k->inuse && !k->enemy && cl[0].ps.ammo[WP_THROWING_KNIFE]==1 && cl[0].ps.cursorHintEntIndex==ENTITYNUM_NONE);
 BodySink(body); Frame();
 CHECK(!k->inuse);
 level.time+=1000; other=G_Spawn(); CHECK(other->s.number==slot && !other->enemy); G_FreeEntity(other);
 cl[0].ps.ammo[WP_THROWING_KNIFE]=0; cmd.buttons=0; G_UseThink(&g_entities[0],&cmd);
 /* the corpse sinks: it lets go, the knife stays while the body is there,
  * then falls to the floor once it is gone, and is still there to take */
 k=Throw(); CopyToBodyQue(&g_entities[1]); body=level.bodyQue[1]; Spawn(&g_entities[1],-500,0);
 Frame(); CHECK(Carried(k,body));
 VectorCopy(k->r.currentOrigin,at); level.time=body->nextthink-50; Frame();
 CHECK(!k->enemy && k->s.pos.trType==TR_STATIONARY && VectorCompare(at,k->r.currentOrigin) && body->r.linked);
 while (body->r.linked) { Frame(); CHECK(body->r.linked ? k->s.pos.trType==TR_STATIONARY : 1); }
 Lands(k);
 level.time+=BG_USE_SPAWN_DELAY; CHECK(Hinted(k,k->r.currentOrigin[0]-30,0));
 /* the thrower is never in its way: it falls through them to the floor */
 G_FreeEntity(k); k=Throw(); g_entities[1].s.groundEntityNum=ENTITYNUM_WORLD;
 Place(&g_entities[0],k->r.currentOrigin[0],0); n=damageCalls;
 g_entities[1].inuse=qfalse; trap_UnlinkEntity(&g_entities[1]);	/* the victim disconnects */
 Frame(); CHECK(!k->enemy && k->s.pos.trType==TR_GRAVITY);
 Lands(k); CHECK(damageCalls==n);
 g_entities[1].inuse=qtrue;
 /* the body queue reuses the corpse: the knife is let go and falls */
 G_FreeEntity(k); k=Throw(); CopyToBodyQue(&g_entities[1]); body=level.bodyQue[2]; Spawn(&g_entities[1],-500,0);
 Frame(); CHECK(Carried(k,body));
 for (i=0;i<BODY_QUEUE_SIZE;i++) { Place(&g_entities[1],-500,300); CopyToBodyQue(&g_entities[1]); }
 CHECK(level.bodyQue[2]->r.currentOrigin[1]==300 && !k->enemy);
 Frame(); CHECK(k->s.pos.trType==TR_GRAVITY);
 Lands(k);
 printf("PASS: %d knife carry and corpse checks\n",checks); return 0;
}
