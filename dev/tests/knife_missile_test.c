/* The throwing knife's real launch, flight, impact and rest code (g_missile.c)
 * against a traced boundary: MW2's G_FireGrenade and MissileImpact for
 * weapClass 9 with the knife's placeholder data. */
#include "../../code/game/g_missile.c"
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
static gclient_t clients[2];
static int checks, damage, damageCalls, traceCount, bounces, lastPass;
static trace_t collision;
static vec3_t firstStart, firstEnd;
static int frameTraces;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define NEAR(a,b) (fabs((a)-(b))<.01f)
void QDECL Com_Error(int code,const char *fmt,...) { (void)code; (void)fmt; abort(); }
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
/* The engine's Q_SnapVector rounds to nearest, as MW2's does; q_shared's macro truncates. */
void trap_SnapVector(float *v) { v[0] = rintf(v[0]); v[1] = rintf(v[1]); v[2] = rintf(v[2]); }
void trap_LinkEntity(gentity_t *e) { e->r.linked=qtrue; }
void trap_Trace(trace_t *t,const vec3_t s,const vec3_t mins,const vec3_t maxs,const vec3_t end,int pass,int mask) {
 (void)mins;(void)maxs;(void)mask; traceCount++; lastPass=pass;
 if(!frameTraces++) { VectorCopy(s,firstStart); VectorCopy(end,firstEnd); }
 *t=collision; if(t->fraction==1)VectorCopy(end,t->endpos); else if(t->startsolid)VectorCopy(s,t->endpos);
}
gentity_t *G_Spawn(void) {
 gentity_t *e=&g_entities[level.num_entities++]; CHECK(level.num_entities<MAX_GENTITIES);
 memset(e,0,sizeof(*e)); e->inuse=qtrue; e->s.number=e-g_entities; return e;
}
void G_FreeEntity(gentity_t *e) { e->inuse=qfalse; e->r.linked=qfalse; e->freetime=level.time; }
void G_SetOrigin(gentity_t *e,vec3_t p) {
 VectorCopy(p,e->s.pos.trBase); VectorCopy(p,e->r.currentOrigin); VectorCopy(p,e->s.origin);
 e->s.pos.trType=TR_STATIONARY; e->s.pos.trTime=0; e->s.pos.trDuration=0; VectorClear(e->s.pos.trDelta);
}
void G_AddEvent(gentity_t *e,int event,int parm) { e->s.event=event; e->s.eventParm=parm; bounces+=event==EV_GRENADE_BOUNCE; }
qboolean G_RadiusDamage(vec3_t p,gentity_t *a,float d,float radius,gentity_t *ignore,int mod) {
 (void)p;(void)a;(void)d;(void)radius;(void)ignore;(void)mod; abort();
}
/* As the real G_Damage does (g_combat.c): its direction is normalized in place. */
void G_Damage(gentity_t *t,gentity_t *i,gentity_t *a,vec3_t d,vec3_t p,int amount,int flags,int mod) {
 (void)t;(void)i;(void)p;(void)flags;
 CHECK(a==&g_entities[0] && mod==MOD_GAUNTLET); damageCalls++; damage=amount;
 if (d) VectorNormalize(d);
}
qboolean LogAccuracyHit(gentity_t *t,gentity_t *a) { (void)t;(void)a; return qfalse; }
void SnapVectorTowards(vec3_t v,vec3_t to) { (void)to; SnapVector(v); }
void Weapon_HookThink(gentity_t *e) { (void)e; }
void Weapon_HookFree(gentity_t *e) { G_FreeEntity(e); }
void G_RunThink(gentity_t *e) { if(e->nextthink>0 && e->nextthink<=level.time) { e->nextthink=0; e->think(e); } }

static void Reset(void) {
 memset(g_entities,0,sizeof(g_entities)); memset(clients,0,sizeof(clients));
 memset(&level,0,sizeof(level)); level.time=1000; level.previousTime=950; level.num_entities=MAX_CLIENTS;
 g_entities[0].inuse=qtrue; g_entities[0].client=&clients[0]; g_entities[0].s.number=0;
 g_entities[1].inuse=qtrue; g_entities[1].client=&clients[1]; g_entities[1].s.number=1;
 g_entities[1].takedamage=qtrue;
 clients[0].pers.connected=clients[1].pers.connected=CON_CONNECTED;
 clients[0].pers.enterTime=1; clients[1].pers.enterTime=2;
 /* a corpse (CopyToBodyQue takes damage) and a mover, neither a player */
 g_entities[MAX_CLIENTS].inuse=qtrue; g_entities[MAX_CLIENTS].s.number=MAX_CLIENTS; g_entities[MAX_CLIENTS].takedamage=qtrue;
 g_entities[MAX_CLIENTS+1].inuse=qtrue; g_entities[MAX_CLIENTS+1].s.number=MAX_CLIENTS+1; g_entities[MAX_CLIENTS+1].s.eType=ET_MOVER;
 level.num_entities=MAX_CLIENTS+2;
 damage=damageCalls=traceCount=bounces=0; lastPass=-1;
 memset(&collision,0,sizeof(collision)); collision.fraction=1; collision.entityNum=ENTITYNUM_NONE;
}
static void Frame(gentity_t *k) { frameTraces=0; level.previousTime=level.time; level.time+=50; G_RunMissile(k); }
static void Clear(void) { memset(&collision,0,sizeof(collision)); collision.fraction=1; collision.entityNum=ENTITYNUM_NONE; }
static void Behind(int entityNum) { Clear(); collision.fraction=.5f; collision.entityNum=entityNum; }
static qboolean Same(const vec3_t a,const vec3_t b) { return NEAR(a[0],b[0]) && NEAR(a[1],b[1]) && NEAR(a[2],b[2]); }
static gentity_t *Throw(float vx,float vy,float vz) {
 vec3_t start={0,0,24},dir={1,0,0};
 VectorSet(clients[0].ps.velocity,vx,vy,vz);
 return fire_throwingknife(&g_entities[0],start,dir);
}
/* Strike something on this frame with the knife moving at v. */
static void Strike(gentity_t *k,float vx,float vy,float vz,int entityNum,float nx,float ny,float nz) {
 k->s.pos.trType=TR_GRAVITY; k->s.pos.trTime=level.time; VectorSet(k->s.pos.trDelta,vx,vy,vz);
 VectorCopy(k->r.currentOrigin,k->s.pos.trBase);
 level.previousTime=level.time;
 memset(&collision,0,sizeof(collision)); collision.fraction=0; collision.entityNum=entityNum;
 VectorCopy(k->r.currentOrigin,collision.endpos); VectorSet(collision.plane.normal,nx,ny,nz);
 ThrowingKnifeImpact(k,&collision,&g_entities[entityNum]);
}
int main(void) {
 gentity_t *k,*first=NULL; vec3_t at,angles,fwd,off; int i,n;
 /* the throw: speed plus the thrower's own along the aim, end over end */
 Reset(); k=Throw(100,50,0);
 CHECK(k->s.eType==ET_MISSILE && k->s.weapon==WP_THROWING_KNIFE && !strcmp(k->classname,"throwingknife"));
 CHECK(k->s.pos.trType==TR_GRAVITY && k->s.pos.trDelta[0]==BG_THROWING_KNIFE_SPEED+100 &&
  k->s.pos.trDelta[1]==0 && k->s.pos.trDelta[2]==0);
 CHECK(k->s.apos.trType==TR_LINEAR && k->s.apos.trDelta[PITCH]==BG_THROWING_KNIFE_SPIN &&
  NEAR(k->s.apos.trBase[PITCH],-120));
 CHECK(k->damage==BG_THROWING_KNIFE_DAMAGE && k->methodOfDeath==MOD_GAUNTLET && !k->s.eFlags);
 CHECK(k->timestamp==level.time && k->nextthink==level.time+BG_THROWING_KNIFE_LIFE && k->r.ownerNum==0);
 /* thirty seconds in the air and it is gone */
 level.time=k->nextthink; G_RunMissile(k); CHECK(!k->inuse);
 /* into the sky: gone */
 Reset(); k=Throw(0,0,0); collision.fraction=.5f; collision.surfaceFlags=SURF_NOIMPACT;
 level.time+=50; G_RunMissile(k); CHECK(!k->inuse);
 /* head-on into a wall: buried along the throw, 1 unit out, at rest for good */
 Reset(); k=Throw(0,0,0); collision.fraction=.5f; collision.entityNum=ENTITYNUM_WORLD;
 VectorSet(collision.endpos,30,0,24); VectorSet(collision.plane.normal,-1,0,0);
 level.time+=50; G_RunMissile(k);
 CHECK(k->inuse && k->s.pos.trType==TR_STATIONARY && NEAR(k->r.currentOrigin[0],29) && !damageCalls);
 CHECK(VectorLength(k->s.pos.trDelta)==0 && k->s.apos.trType==TR_STATIONARY && !k->nextthink && !k->think);
 CHECK(NEAR(k->s.apos.trBase[YAW],0) && NEAR(k->s.angles[YAW],0) && k->s.event!=EV_GRENADE_BOUNCE);
 /* and nothing moves it after: no trace, no impact, not even a player in it */
 n=traceCount; VectorCopy(k->r.currentOrigin,at);
 collision.fraction=0; collision.startsolid=qtrue; collision.entityNum=1;
 for(i=0;i<100;i++) { level.time+=50; G_RunMissile(k); }
 CHECK(traceCount==n && VectorCompare(at,k->r.currentOrigin) && k->inuse && !damageCalls);
 /* glancing off a wall: reflected, scaled, still flying */
 Reset(); k=Throw(0,0,0); Strike(k,1000,-300,0,ENTITYNUM_WORLD,0,1,0);
 CHECK(k->s.pos.trType==TR_GRAVITY && k->s.event==EV_GRENADE_BOUNCE);
 CHECK(NEAR(k->s.pos.trDelta[0],1000*BG_THROWING_KNIFE_BOUNCE) && NEAR(k->s.pos.trDelta[1],300*BG_THROWING_KNIFE_BOUNCE));
 /* steep enough is a stick: the reflection within the stick angle of the normal */
 Reset(); k=Throw(0,0,0); Strike(k,600,-900,0,ENTITYNUM_WORLD,0,1,0);
 CHECK(k->s.pos.trType==TR_STATIONARY);
 /* settling on a floor below the rest speed, though it came in shallow */
 Reset(); k=Throw(0,0,0); Strike(k,40,0,-10,ENTITYNUM_WORLD,0,0,1);
 CHECK(k->s.pos.trType==TR_STATIONARY && NEAR(k->r.currentOrigin[2],collision.endpos[2]+1));
 { vec3_t v={40,0,-10}; vectoangles(v,angles); CHECK(NEAR(k->s.apos.trBase[PITCH],angles[PITCH])); }
 /* faster along the floor it skids on */
 Reset(); k=Throw(0,0,0); Strike(k,200,0,-10,ENTITYNUM_WORLD,0,0,1);
 CHECK(k->s.pos.trType==TR_GRAVITY && k->s.event==EV_GRENADE_BOUNCE);
 /* a wall it barely reaches is not a floor: it falls away */
 Reset(); k=Throw(0,0,0); Strike(k,0,-10,-30,ENTITYNUM_WORLD,0,1,0);
 CHECK(k->s.pos.trType==TR_GRAVITY);
 /* into a player (stickToPlayers): one hit above g_minGrenadeDamageSpeed, and
  * whatever its speed it is buried at the hit and carried, with no velocity */
 Reset(); VectorSet(g_entities[1].r.currentOrigin,20,0,24);
 k=Throw(0,0,0); Strike(k,BG_THROWING_KNIFE_HIT_SPEED+1,0,0,1,-1,0,0);
 CHECK(damageCalls==1 && damage==BG_THROWING_KNIFE_DAMAGE && !bounces);
 CHECK(k->enemy==&g_entities[1] && k->restNum==1 && k->r.linked && !k->nextthink && !k->think);
 CHECK(k->s.pos.trType==TR_INTERPOLATE && VectorLength(k->s.pos.trDelta)==0);
 CHECK(k->s.apos.trType==TR_INTERPOLATE && VectorLength(k->s.apos.trDelta)==0);
 /* MW2's bury: level into a wall within 15 degrees, 1.5 in and 4.5 back along itself */
 CHECK(fabs(k->s.apos.trBase[PITCH])<=15 && NEAR(k->s.apos.trBase[YAW],0));
 AngleVectors(k->s.apos.trBase,fwd,NULL,NULL);
 VectorMA(collision.endpos,-1.5f,collision.plane.normal,at); VectorMA(at,-4.5f,fwd,at);
 CHECK(Same(at,k->r.currentOrigin) && Same(at,k->s.pos.trBase));
 Reset(); k=Throw(0,0,0); Strike(k,0,0,-BG_THROWING_KNIFE_HIT_SPEED-1,1,0,0,1);
 CHECK(k->enemy==&g_entities[1] && fabs(k->s.apos.trBase[PITCH]-90)<=15);
 Reset(); k=Throw(0,0,0); Strike(k,BG_THROWING_KNIFE_HIT_SPEED,0,0,1,-1,0,0);
 CHECK(!damageCalls && k->enemy==&g_entities[1] && k->s.pos.trType==TR_INTERPOLATE);
 /* it rides them, turning with them, re-placed each frame with no trace */
 VectorCopy(k->r.currentOrigin,at); VectorCopy(k->s.apos.trBase,angles); n=traceCount;
 VectorSubtract(at,g_entities[1].r.currentOrigin,off);
 VectorSet(g_entities[1].r.currentOrigin,500,40,24); clients[1].ps.viewangles[YAW]=90;
 Frame(k);
 CHECK(NEAR(k->r.currentOrigin[0],500-off[1]) && NEAR(k->r.currentOrigin[1],40+off[0]) && NEAR(k->r.currentOrigin[2],24+off[2]));
 CHECK(Same(k->s.pos.trBase,k->r.currentOrigin) && k->s.pos.trType==TR_INTERPOLATE && VectorLength(k->s.pos.trDelta)==0);
 CHECK(NEAR(AngleNormalize180(k->s.apos.trBase[YAW]),90) && NEAR(AngleNormalize180(k->s.apos.trBase[PITCH]),AngleNormalize180(angles[PITCH])));
 CHECK(traceCount==n && k->enemy==&g_entities[1] && k->r.linked);
 /* they respawn without a corpse: it is let go where it was, and with
  * nothing behind it falls from there, the thrower never in its way */
 VectorCopy(k->r.currentOrigin,at); clients[1].ps.persistant[PERS_SPAWN_COUNT]++;
 VectorSet(g_entities[1].r.currentOrigin,-900,0,24);
 Clear(); Frame(k);
 CHECK(!k->enemy && k->s.pos.trType==TR_GRAVITY && k->s.pos.trTime==level.time && VectorLength(k->s.pos.trDelta)==0);
 CHECK(Same(k->s.pos.trBase,at) && lastPass==0);
 VectorSubtract(firstStart,firstEnd,off); CHECK(NEAR(VectorLength(off),THROWING_KNIFE_SUPPORT));
 Frame(k); CHECK(k->r.currentOrigin[2]<at[2] && lastPass==0);
 /* it lands on the world floor and stays for good */
 Behind(ENTITYNUM_WORLD); VectorSet(collision.plane.normal,0,0,1); VectorCopy(k->r.currentOrigin,collision.endpos);
 Frame(k); CHECK(k->s.pos.trType==TR_STATIONARY && k->restNum==ENTITYNUM_WORLD);
 n=traceCount; Clear(); for(i=0;i<20;i++) Frame(k);
 CHECK(traceCount==n && k->s.pos.trType==TR_STATIONARY);
 /* let go with something behind it, it stays */
 Reset(); k=Throw(0,0,0); Strike(k,BG_THROWING_KNIFE_HIT_SPEED+1,0,0,1,-1,0,0);
 VectorCopy(k->r.currentOrigin,at); clients[1].ps.persistant[PERS_SPAWN_COUNT]++;
 Behind(1); Frame(k); Frame(k);
 CHECK(!k->enemy && k->s.pos.trType==TR_STATIONARY && k->s.apos.trType==TR_STATIONARY && Same(at,k->r.currentOrigin));
 /* a carrier that disconnects, or is freed, lets go too */
 Reset(); k=Throw(0,0,0); Strike(k,BG_THROWING_KNIFE_HIT_SPEED+1,0,0,1,-1,0,0);
 g_entities[1].inuse=qfalse; Clear(); Frame(k); CHECK(!k->enemy && k->s.pos.trType==TR_GRAVITY);
 Reset(); k=Throw(0,0,0); Strike(k,BG_THROWING_KNIFE_HIT_SPEED+1,0,0,1,-1,0,0);
 clients[1].pers.enterTime++; Clear(); Frame(k); CHECK(!k->enemy && k->s.pos.trType==TR_GRAVITY);
 Reset(); k=Throw(0,0,0); Strike(k,BG_THROWING_KNIFE_HIT_SPEED+1,0,0,1,-1,0,0);
 g_entities[1].freetime=level.time; Clear(); Frame(k); CHECK(!k->enemy && k->s.pos.trType==TR_GRAVITY);
 /* ClonePlayer: what the player carries moves, where it is, to their corpse,
  * which carries it on; the corpse lets go when it sinks or is reused. A
  * Semtex keeps its own rule. */
 Reset(); VectorSet(g_entities[1].r.currentOrigin,20,0,24);
 k=Throw(0,0,0); Strike(k,BG_THROWING_KNIFE_HIT_SPEED+1,0,0,1,-1,0,0);
 { gentity_t *semtex=fire_semtex(&g_entities[0],at,fwd); Behind(1); G_RunMissile(semtex); CHECK(semtex->enemy==&g_entities[1]);
   VectorCopy(k->r.currentOrigin,at); VectorCopy(g_entities[1].r.currentOrigin,g_entities[MAX_CLIENTS].r.currentOrigin);
   G_MoveCarriedMissiles(&g_entities[1],&g_entities[MAX_CLIENTS]);
   CHECK(k->enemy==&g_entities[MAX_CLIENTS] && Same(at,k->r.currentOrigin) && semtex->enemy==&g_entities[1]); }
 clients[1].ps.persistant[PERS_SPAWN_COUNT]++; VectorSet(g_entities[1].r.currentOrigin,-900,0,24);
 g_entities[MAX_CLIENTS].r.currentOrigin[2]-=10; Frame(k);
 CHECK(k->enemy==&g_entities[MAX_CLIENTS] && k->s.pos.trType==TR_INTERPOLATE && NEAR(k->r.currentOrigin[2],at[2]-10));
 VectorCopy(k->r.currentOrigin,at); G_MoveCarriedMissiles(&g_entities[MAX_CLIENTS],NULL);
 CHECK(!k->enemy && k->s.pos.trType==TR_STATIONARY && Same(at,k->r.currentOrigin));
 /* a player who already respawned no longer carries it, so no corpse of
  * theirs takes it */
 Reset(); k=Throw(0,0,0); Strike(k,BG_THROWING_KNIFE_HIT_SPEED+1,0,0,1,-1,0,0);
 clients[1].ps.persistant[PERS_SPAWN_COUNT]++;
 G_MoveCarriedMissiles(&g_entities[1],&g_entities[MAX_CLIENTS]); CHECK(!k->enemy);
 /* on anything but the world it falls again once nothing is behind it: a
  * mover that moved off, a corpse that went away */
 Reset(); k=Throw(0,0,0); Strike(k,600,-900,0,MAX_CLIENTS+1,0,1,0);
 CHECK(k->s.pos.trType==TR_STATIONARY && !k->enemy && k->restNum==MAX_CLIENTS+1);
 VectorCopy(k->r.currentOrigin,at); Behind(MAX_CLIENTS+1); Frame(k);
 CHECK(k->s.pos.trType==TR_STATIONARY && Same(at,k->r.currentOrigin) && lastPass==0);
 VectorSubtract(firstEnd,firstStart,off); CHECK(NEAR(off[1],-THROWING_KNIFE_SUPPORT));
 Clear(); Frame(k); CHECK(k->s.pos.trType==TR_GRAVITY && Same(at,k->s.pos.trBase));
 Reset(); k=Throw(0,0,0); Strike(k,40,0,-10,MAX_CLIENTS,0,0,1);
 CHECK(k->s.pos.trType==TR_STATIONARY && k->restNum==MAX_CLIENTS && !damageCalls);
 Behind(MAX_CLIENTS); Frame(k); CHECK(k->s.pos.trType==TR_STATIONARY);
 Clear(); Frame(k); CHECK(k->s.pos.trType==TR_GRAVITY);
 /* a damageable corpse is hit with a copy: the knife glances on at full speed */
 Reset(); k=Throw(0,0,0); Strike(k,1128,-410,0,MAX_CLIENTS,0,1,0);
 CHECK(damageCalls==1 && k->s.pos.trType==TR_GRAVITY);
 CHECK(NEAR(k->s.pos.trDelta[0],1128*BG_THROWING_KNIFE_BOUNCE) && NEAR(k->s.pos.trDelta[1],410*BG_THROWING_KNIFE_BOUNCE));
 Reset(); k=Throw(0,0,0); Strike(k,1180,0,-200,MAX_CLIENTS,0,0,1);
 CHECK(damageCalls==1 && k->s.pos.trType==TR_GRAVITY && NEAR(k->s.pos.trDelta[2],200*BG_THROWING_KNIFE_BOUNCE));
 /* thrown from inside solid: no impact event (MW2's startsolid), and it is
  * gone with its flight lifetime */
 Reset(); k=Throw(0,0,0); Clear(); collision.fraction=0; collision.startsolid=collision.allsolid=qtrue;
 collision.entityNum=ENTITYNUM_WORLD;
 for(i=0;i<=BG_THROWING_KNIFE_LIFE/50 && k->inuse;i++) Frame(k);
 CHECK(!k->inuse && !bounces && i==BG_THROWING_KNIFE_LIFE/50);
 /* the world holds BG_THROWING_KNIFE_WORLD_MAX; another frees the oldest */
 Reset();
 for(i=0;i<BG_THROWING_KNIFE_WORLD_MAX;i++) { level.time+=10; k=Throw(0,0,0); if(!i) first=k; }
 CHECK(first->inuse);
 level.time+=10; k=Throw(0,0,0);
 for(n=0,i=MAX_CLIENTS;i<level.num_entities;i++) n+=g_entities[i].inuse && g_entities[i].s.weapon==WP_THROWING_KNIFE;
 CHECK(!first->inuse && k->inuse && n==BG_THROWING_KNIFE_WORLD_MAX);
 printf("PASS: %d throwing knife projectile checks\n",checks); return 0;
}
