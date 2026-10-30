/* The throwing knife's real launch, flight, impact and rest code (g_missile.c)
 * against a traced boundary: MW2's G_FireGrenade and MissileImpact for
 * weapClass 9 with the knife's placeholder data. */
#include "../../code/game/g_missile.c"
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
static gclient_t clients[2];
static int checks, damage, damageCalls, traceCount;
static trace_t collision;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define NEAR(a,b) (fabs((a)-(b))<.01f)
void QDECL Com_Error(int code,const char *fmt,...) { (void)code; (void)fmt; abort(); }
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
/* The engine's Q_SnapVector rounds to nearest, as MW2's does; q_shared's macro truncates. */
void trap_SnapVector(float *v) { v[0] = rintf(v[0]); v[1] = rintf(v[1]); v[2] = rintf(v[2]); }
void trap_LinkEntity(gentity_t *e) { e->r.linked=qtrue; }
void trap_Trace(trace_t *t,const vec3_t s,const vec3_t mins,const vec3_t maxs,const vec3_t end,int pass,int mask) {
 (void)s;(void)mins;(void)maxs;(void)pass;(void)mask; traceCount++;
 *t=collision; if(t->fraction==1)VectorCopy(end,t->endpos);
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
void G_AddEvent(gentity_t *e,int event,int parm) { e->s.event=event; e->s.eventParm=parm; }
qboolean G_RadiusDamage(vec3_t p,gentity_t *a,float d,float radius,gentity_t *ignore,int mod) {
 (void)p;(void)a;(void)d;(void)radius;(void)ignore;(void)mod; abort();
}
void G_Damage(gentity_t *t,gentity_t *i,gentity_t *a,vec3_t d,vec3_t p,int amount,int flags,int mod) {
 (void)t;(void)i;(void)d;(void)p;(void)flags;
 CHECK(a==&g_entities[0] && mod==MOD_GAUNTLET); damageCalls++; damage=amount;
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
 damage=damageCalls=traceCount=0;
 memset(&collision,0,sizeof(collision)); collision.fraction=1; collision.entityNum=ENTITYNUM_NONE;
}
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
 gentity_t *k,*first=NULL; vec3_t at,angles; int i,n;
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
 /* into a player above g_minGrenadeDamageSpeed: one hit, then it falls from
  * there, passing through them */
 Reset(); k=Throw(0,0,0); Strike(k,BG_THROWING_KNIFE_HIT_SPEED+1,0,0,1,-1,0,0);
 CHECK(damageCalls==1 && damage==BG_THROWING_KNIFE_DAMAGE);
 CHECK(k->s.pos.trType==TR_GRAVITY && VectorLength(k->s.pos.trDelta)==0 && k->target_ent==&g_entities[1]);
 Reset(); k=Throw(0,0,0); Strike(k,BG_THROWING_KNIFE_HIT_SPEED,0,0,1,-1,0,0);
 CHECK(!damageCalls && k->s.pos.trType==TR_GRAVITY);
 /* the world holds BG_THROWING_KNIFE_WORLD_MAX; another frees the oldest */
 Reset();
 for(i=0;i<BG_THROWING_KNIFE_WORLD_MAX;i++) { level.time+=10; k=Throw(0,0,0); if(!i) first=k; }
 CHECK(first->inuse);
 level.time+=10; k=Throw(0,0,0);
 for(n=0,i=MAX_CLIENTS;i<level.num_entities;i++) n+=g_entities[i].inuse && g_entities[i].s.weapon==WP_THROWING_KNIFE;
 CHECK(!first->inuse && k->inuse && n==BG_THROWING_KNIFE_WORLD_MAX);
 printf("PASS: %d throwing knife projectile checks\n",checks); return 0;
}
