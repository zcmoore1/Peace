/* Exercise the real projectile and attachment code against a traced boundary. */
#include "../../code/game/g_missile.c"
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
static gclient_t clients[2];
static int checks, blasts, impacts, traceCount;
static trace_t collision;
static gentity_t *lastAttacker;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
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
qboolean G_RadiusDamage(vec3_t p,gentity_t *a,float damage,float radius,gentity_t *ignore,int mod) {
 (void)p;(void)damage;(void)radius;(void)ignore;(void)mod; blasts++; lastAttacker=a; return qtrue;
}
void G_Damage(gentity_t *t,gentity_t *i,gentity_t *a,vec3_t d,vec3_t p,int damage,int flags,int mod) {
 (void)t;(void)i;(void)a;(void)d;(void)p;(void)damage;(void)flags;(void)mod; impacts++;
}
qboolean LogAccuracyHit(gentity_t *t,gentity_t *a) { (void)t;(void)a; return qfalse; }
void SnapVectorTowards(vec3_t v,vec3_t to) { (void)to; SnapVector(v); }
void Weapon_HookThink(gentity_t *e) { (void)e; }
void Weapon_HookFree(gentity_t *e) { G_FreeEntity(e); }
void G_RunThink(gentity_t *e) { if(e->nextthink>0 && e->nextthink<=level.time) { e->nextthink=0; e->think(e); } }

static gentity_t *Launch(void) {
 vec3_t start={0,0,24},dir={1,0,0};
 gentity_t *m;
 memset(g_entities,0,sizeof(g_entities)); memset(clients,0,sizeof(clients));
 memset(&level,0,sizeof(level)); level.time=1000; level.previousTime=950; level.num_entities=MAX_CLIENTS;
 g_entities[0].inuse=qtrue; g_entities[0].client=&clients[0];
 clients[0].pers.connected=CON_CONNECTED; clients[0].pers.enterTime=1;
 blasts=impacts=traceCount=0;
 memset(&collision,0,sizeof(collision)); collision.fraction=.5f;
 collision.entityNum=ENTITYNUM_WORLD; VectorSet(collision.endpos,10,0,24); VectorSet(collision.plane.normal,-1,0,0);
 m=fire_semtex(&g_entities[0],start,dir);
 CHECK(m->s.weapon==WP_SEMTEX && m->s.pos.trType==TR_GRAVITY);
 CHECK(m->nextthink==level.time+SEMTEX_FUSE_TIME && !m->damage && !m->s.eFlags);
 return m;
}
int main(void) {
 gentity_t *m,*host; int deadline,n; vec3_t stuck;
 m=Launch(); deadline=m->nextthink; G_RunMissile(m);
 CHECK(m->s.pos.trType==TR_STATIONARY && !blasts && !impacts);
 CHECK(m->nextthink==deadline && !m->enemy);
 VectorCopy(m->r.currentOrigin,stuck); n=traceCount;
 level.time=deadline-1; G_RunMissile(m);
 CHECK(!blasts && traceCount==n && VectorCompare(stuck,m->r.currentOrigin));
 level.time=deadline; G_RunMissile(m);
 CHECK(blasts==1 && m->freeAfterEvent && m->s.eType==ET_GENERAL);
 CHECK(lastAttacker==&g_entities[0]);
 // World/mover/client contacts never restart a fuse, and rotating attachment
 // coordinates follow the host rather than accumulating frame-to-frame drift.
 m=Launch(); host=&g_entities[70]; host->inuse=qtrue; host->s.eType=ET_MOVER;
 collision.entityNum=70; G_RunMissile(m); CHECK(m->enemy==host);
 VectorSet(host->r.currentOrigin,100,0,0); host->r.currentAngles[YAW]=90;
 level.time+=50; G_RunMissile(m);
 CHECK(fabs(m->r.currentOrigin[0]-100)<.001 && fabs(m->r.currentOrigin[1]-9)<.001);
 VectorCopy(m->r.currentOrigin,stuck); host->freetime=level.time;
 VectorSet(host->r.currentOrigin,500,500,500); G_RunMissile(m);
 CHECK(!m->enemy && VectorCompare(stuck,m->r.currentOrigin));
 m=Launch(); host=&g_entities[1]; host->inuse=qtrue; host->client=&clients[1]; host->takedamage=qtrue;
 clients[1].pers.connected=CON_CONNECTED; clients[1].pers.enterTime=2;
 collision.entityNum=1; G_RunMissile(m);
 CHECK(m->enemy==host && !impacts && !blasts);
 host->r.currentOrigin[0]+=50; G_RunMissile(m); CHECK(m->r.currentOrigin[0]==59);
 VectorCopy(m->r.currentOrigin,stuck); clients[1].ps.persistant[PERS_SPAWN_COUNT]++;
 host->r.currentOrigin[0]+=500; G_RunMissile(m);
 CHECK(!m->enemy && VectorCompare(stuck,m->r.currentOrigin));
 clients[0].pers.connected=CON_DISCONNECTED; level.time=m->nextthink; G_RunMissile(m);
 CHECK(blasts==1 && lastAttacker==&g_entities[ENTITYNUM_WORLD]);
 m=Launch(); collision.surfaceFlags=SURF_NOIMPACT; G_RunMissile(m);
 CHECK(!m->inuse && !blasts);
 m=Launch(); clients[0].pers.enterTime++; level.time=m->nextthink; collision.fraction=1; G_RunMissile(m);
 CHECK(lastAttacker==&g_entities[ENTITYNUM_WORLD]);
 printf("PASS: %d Semtex projectile checks\n",checks); return 0;
}
