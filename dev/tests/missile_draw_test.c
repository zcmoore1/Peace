/* What the real CG_Missile (cg_ents.c) draws, with the real CG_RegisterWeapon
 * (cg_weapons.c) and snapshot lerp: missiles met by a client that never saw
 * their weapon held, a throwing knife riding its carrier between snapshots
 * (MW2 re-places it with pos and apos TR_INTERPOLATE, 0x43B9D0), the knife in
 * flight and at rest, and the Semtex's own placement, which is unchanged. */
#include "../../code/cgame/cg_ents.c"
cg_t cg;
cgs_t cgs;
centity_t cg_entities[MAX_GENTITIES];
weaponInfo_t cg_weapons[MAX_WEAPONS];
itemInfo_t cg_items[MAX_ITEMS];
vmCvar_t cg_smoothClients;
static snapshot_t snap, nextSnap;
static refEntity_t drawn;
static int checks, draws, models;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define NEAR(a,b) (fabs((a)-(b))<.01f)
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL CG_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_Error(int code,const char *fmt,...) { (void)code; (void)fmt; abort(); }
void QDECL CG_Error(const char *fmt,...) {
 va_list args; va_start(args,fmt); vfprintf(stderr,fmt,args); va_end(args); exit(1);
}
/* The renderer: every name is a model, each its own handle (0 is the null model). */
qhandle_t trap_R_RegisterModel(const char *name) { (void)name; return ++models; }
qhandle_t trap_R_RegisterShader(const char *name) { (void)name; return 1; }
sfxHandle_t trap_S_RegisterSound(const char *name,qboolean compressed) { (void)name; (void)compressed; return 1; }
void trap_R_ModelBounds(clipHandle_t model,vec3_t mins,vec3_t maxs) { (void)model; VectorClear(mins); VectorClear(maxs); }
void trap_R_AddRefEntityToScene(const refEntity_t *re) { drawn=*re; draws++; }
void CG_AddRefEntityWithPowerups(refEntity_t *ent,entityState_t *state,int team) {
 (void)state; (void)team; trap_R_AddRefEntityToScene(ent);
}
void CG_WeapAnim_RegisterClips(int weapon,qhandle_t model,const char *path) { (void)weapon; (void)model; (void)path; }
void CG_Sights_Register(int weapon,const char *path) { (void)weapon; (void)path; }
/* Trails, brass, lights and sounds are not what is checked here: cg_noProjectileTrail
 * is set, and the rest is only linked because CG_RegisterWeapon names it. */
vmCvar_t cg_noProjectileTrail, cg_brassTime, cg_oldPlasma;
void trap_R_AddLightToScene(const vec3_t o,float i,float r,float g,float b) { (void)o;(void)i;(void)r;(void)g;(void)b; abort(); }
void trap_S_AddLoopingSound(int n,const vec3_t o,const vec3_t v,sfxHandle_t s) { (void)n;(void)o;(void)v;(void)s; abort(); }
int CG_PointContents(const vec3_t p,int pass) { (void)p; (void)pass; abort(); return 0; }
void CG_BubbleTrail(vec3_t a,vec3_t b,float spacing) { (void)a; (void)b; (void)spacing; abort(); }
localEntity_t *CG_AllocLocalEntity(void) { abort(); return NULL; }
localEntity_t *CG_SmokePuff(const vec3_t p,const vec3_t vel,float radius,float r,float g,float b,float a,
 float duration,int startTime,int fadeInTime,int leFlags,qhandle_t hShader) {
 (void)p;(void)vel;(void)radius;(void)r;(void)g;(void)b;(void)a;(void)duration;(void)startTime;
 (void)fadeInTime;(void)leFlags;(void)hShader; abort(); return NULL;
}

/* A fresh cgame, as CG_Init leaves it on connect and on vid_restart: nothing
 * registered but the map's items, snapshots at t and t+frame. */
static void Reset(int t,int frame) {
 memset(cg_entities,0,sizeof(cg_entities)); memset(cg_weapons,0,sizeof(cg_weapons));
 memset(cg_items,0,sizeof(cg_items)); memset(&cg,0,sizeof(cg)); memset(&cgs,0,sizeof(cgs));
 memset(&snap,0,sizeof(snap)); memset(&nextSnap,0,sizeof(nextSnap));
 snap.serverTime=t; nextSnap.serverTime=t+frame; snap.ps.clientNum=0;
 cg.snap=&snap; cg.nextSnap=&nextSnap; cg.time=t;
 cg_noProjectileTrail.integer=1;
}
static void At(float f) {
 cg.time=snap.serverTime+(int)(f*(nextSnap.serverTime-snap.serverTime));
 cg.frameInterpolation=(float)(cg.time-snap.serverTime)/(nextSnap.serverTime-snap.serverTime);
}
static centity_t *Entity(int number,int eType,int weapon) {
 centity_t *c=&cg_entities[number];
 c->currentState.number=number; c->currentState.eType=eType; c->currentState.weapon=weapon;
 c->currentValid=qtrue; c->nextState=c->currentState; return c;
}
static void Place(trajectory_t *tr,trType_t type,float x,float y,float z) {
 memset(tr,0,sizeof(*tr)); tr->trType=type; VectorSet(tr->trBase,x,y,z);
}
/* One CG_AddCEntity for a missile: the lerp, then the draw. */
static void Draw(centity_t *c) {
 int before=draws;
 CG_CalcEntityLerpPositions(c); CG_Missile(c); CHECK(draws==before+1);
}
static int SameAxis(vec3_t a[3],vec3_t b[3]) {
 int i,j;
 for (i=0;i<3;i++) for (j=0;j<3;j++) if (!NEAR(a[i][j],b[i][j])) return 0;
 return 1;
}

/* Lying in the map before this client ever saw anyone hold one. */
static void TestFirstSight(int weapon) {
 centity_t *c; int registered;
 Reset(1000,50);
 c=Entity(MAX_CLIENTS,ET_MISSILE,weapon);
 Place(&c->currentState.pos,TR_STATIONARY,64,0,8); c->nextState=c->currentState;
 Draw(c);
 CHECK(cg_weapons[weapon].registered && cg_weapons[weapon].missileModel);
 CHECK(drawn.hModel==cg_weapons[weapon].missileModel);
 /* registered once: later frames cost a flag test */
 registered=models; Draw(c); CHECK(models==registered && drawn.hModel==cg_weapons[weapon].missileModel);
 /* vid_restart re-runs CG_Init, which forgets it: it is registered again */
 memset(cg_weapons,0,sizeof(cg_weapons)); Draw(c);
 CHECK(models>registered && drawn.hModel==cg_weapons[weapon].missileModel && drawn.hModel);
}

/* Carried: the server re-places it on its carrier every frame. Between
 * snapshots it keeps its place on the carrier and turns with it. */
static void TestCarried(void) {
 static const float fractions[]={0,.25f,.5f,.75f,1};
 centity_t *carrier,*knife; vec3_t off={10,-4,30},axis[3],angles,expect; float f; int i;
 Reset(1000,50);
 carrier=Entity(1,ET_PLAYER,WP_NONE);
 Place(&carrier->currentState.pos,TR_INTERPOLATE,0,0,24);
 Place(&carrier->nextState.pos,TR_INTERPOLATE,9.5f,-3,24);
 carrier->interpolate=qtrue;
 knife=Entity(MAX_CLIENTS,ET_MISSILE,WP_THROWING_KNIFE);
 VectorSet(knife->currentState.angles,90,90,90);	/* never the knife's picture */
 knife->nextState.angles[YAW]=270;
 Place(&knife->currentState.pos,TR_INTERPOLATE,off[0],off[1],24+off[2]);
 Place(&knife->nextState.pos,TR_INTERPOLATE,9.5f+off[0],-3+off[1],24+off[2]);
 /* across north: 350 -> 10 turns 20 degrees, not 340 the long way round */
 Place(&knife->currentState.apos,TR_INTERPOLATE,20,350,0);
 Place(&knife->nextState.apos,TR_INTERPOLATE,10,10,0);
 knife->interpolate=qtrue;
 for (i=0;i<(int)(sizeof(fractions)/sizeof(fractions[0]));i++) {
  f=fractions[i]; At(f);
  CG_CalcEntityLerpPositions(carrier); Draw(knife);
  VectorAdd(carrier->lerpOrigin,off,expect);
  CHECK(NEAR(drawn.origin[0],expect[0]) && NEAR(drawn.origin[1],expect[1]) && NEAR(drawn.origin[2],expect[2]));
  VectorSet(angles,20-10*f,350+20*f,0); AnglesToAxis(angles,axis);
  CHECK(SameAxis(drawn.axis,axis));
  CHECK(drawn.hModel && drawn.hModel==cg_weapons[WP_THROWING_KNIFE].missileModel);
 }
 /* no snapshot ahead (or a teleport): where the last one put it */
 knife->interpolate=qfalse; At(.5f); Draw(knife);
 CHECK(NEAR(drawn.origin[0],off[0]) && NEAR(drawn.origin[2],24+off[2]));
 VectorSet(angles,20,350,0); AnglesToAxis(angles,axis); CHECK(SameAxis(drawn.axis,axis));
}

/* In flight it spins end over end along apos; stopped it stays as it stopped. */
static void TestFlightAndRest(void) {
 centity_t *knife; vec3_t axis[3],angles,at; int t;
 Reset(1000,50);
 knife=Entity(MAX_CLIENTS,ET_MISSILE,WP_THROWING_KNIFE);
 VectorSet(knife->currentState.angles,90,90,90);
 knife->currentState.pos.trType=TR_GRAVITY; knife->currentState.pos.trTime=1000;
 VectorSet(knife->currentState.pos.trDelta,BG_THROWING_KNIFE_SPEED,0,0);
 knife->currentState.apos.trType=TR_LINEAR; knife->currentState.apos.trTime=1000;
 knife->currentState.apos.trBase[PITCH]=-120; knife->currentState.apos.trDelta[PITCH]=BG_THROWING_KNIFE_SPIN;
 knife->nextState=knife->currentState; knife->interpolate=qtrue;
 for (t=0;t<=50;t+=10) {
  cg.time=1000+t; Draw(knife);
  BG_EvaluateTrajectory(&knife->currentState.pos,cg.time,at);
  BG_EvaluateTrajectory(&knife->currentState.apos,cg.time,angles); AnglesToAxis(angles,axis);
  CHECK(NEAR(drawn.origin[0],at[0]) && NEAR(drawn.origin[2],at[2]) && SameAxis(drawn.axis,axis));
 }
 /* buried: TR_STATIONARY, facing the throw */
 Place(&knife->currentState.pos,TR_STATIONARY,128,0,40);
 Place(&knife->currentState.apos,TR_STATIONARY,30,45,0);
 knife->nextState=knife->currentState; At(.5f); Draw(knife);
 VectorSet(angles,30,45,0); AnglesToAxis(angles,axis);
 CHECK(NEAR(drawn.origin[0],128) && NEAR(drawn.origin[2],40) && SameAxis(drawn.axis,axis));
}

/* A Semtex stuck to a player is TR_STATIONARY, re-placed each frame: drawn at
 * this snapshot's placement, facing its s.angles, exactly as before. */
static void TestSemtex(void) {
 centity_t *semtex; vec3_t axis[3];
 Reset(1000,50);
 semtex=Entity(MAX_CLIENTS,ET_MISSILE,WP_SEMTEX);
 Place(&semtex->currentState.pos,TR_STATIONARY,16,0,40);
 VectorSet(semtex->currentState.angles,0,90,0);
 semtex->nextState=semtex->currentState;
 Place(&semtex->nextState.pos,TR_STATIONARY,25.5f,-3,40);
 semtex->nextState.angles[YAW]=100;
 semtex->interpolate=qtrue;
 At(.5f); Draw(semtex);
 AnglesToAxis(semtex->currentState.angles,axis);
 CHECK(NEAR(drawn.origin[0],16) && NEAR(drawn.origin[1],0) && SameAxis(drawn.axis,axis));
 CHECK(drawn.hModel && drawn.hModel==cg_weapons[WP_SEMTEX].missileModel);
}

int main(void) {
 TestFirstSight(WP_THROWING_KNIFE);
 TestFirstSight(WP_SEMTEX);
 TestFirstSight(WP_GRENADE_LAUNCHER);	/* a frag or a flash: fire_grenade's weapon, held by no one */
 TestCarried();
 TestFlightAndRest();
 TestSemtex();
 printf("PASS: %i missile drawing checks\n",checks);
 return 0;
}
