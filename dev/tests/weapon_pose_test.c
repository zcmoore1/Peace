/* Real presentation code, real clip definitions, no gameplay mutations.
 * Ownership follows action categories, not weapon IDs or named glitches. */
#include "../../code/cgame/cg_anim.c"
cg_t cg;
static int checks;
static FILE *input;
static modelPose_t authored[512];
static int authoredFrames;
static qboolean nativePose;
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL CG_Printf(const char *fmt,...) { (void)fmt; }
/* The engine's Q_SnapVector rounds to nearest, as MW2's does; q_shared's macro truncates. */
void trap_SnapVector(float *v) { v[0] = rintf(v[0]); v[1] = rintf(v[1]); v[2] = rintf(v[2]); }
void QDECL Com_Error(int level,const char *fmt,...) {
 va_list args; (void)level; va_start(args,fmt); vfprintf(stderr,fmt,args);
 va_end(args); exit(1);
}
int trap_FS_FOpenFile(const char *path,fileHandle_t *handle,fsMode_t mode) {
 long size; (void)mode; input=fopen(path,"rb"); CHECK(input!=NULL);
 fseek(input,0,SEEK_END); size=ftell(input); rewind(input); *handle=1; return size;
}
void trap_FS_Read(void *buffer,int size,fileHandle_t handle) {
 (void)handle; CHECK(fread(buffer,1,size,input)==(size_t)size);
}
void trap_FS_FCloseFile(fileHandle_t handle) { (void)handle; fclose(input); }
qboolean trap_R_BuildModelPose(qhandle_t model,int frame,int oldframe,float blend,modelPose_t *out) {
 (void)model;
 if(!nativePose)return qfalse;
 CHECK(frame>=0 && frame<authoredFrames && oldframe>=0 && oldframe<authoredFrames);
 trap_R_BlendModelPoses(out,&authored[frame],1-blend,&authored[oldframe],blend);
 return qtrue;
}
void trap_R_BlendModelPoses(modelPose_t *out,const modelPose_t *a,float wa,const modelPose_t *b,float wb) {
 int j,k;float t=wb/(wa+wb);
 out->numJoints=a->numJoints;
 CHECK(a->numJoints==b->numJoints);
 for(j=0;j<out->numJoints;j++) {
  for(k=0;k<3;k++) {
   out->joints[j].translate[k]=a->joints[j].translate[k]*(1-t)+b->joints[j].translate[k]*t;
   out->joints[j].scale[k]=a->joints[j].scale[k]*(1-t)+b->joints[j].scale[k]*t;
  }
  {
   float dot=0,sign=1,wa=1-t,wb=t;
   for(k=0;k<4;k++)dot+=a->joints[j].rotate[k]*b->joints[j].rotate[k];
   if(dot<0) { dot=-dot;sign=-1; }
   if(dot<.999999f) {
    float angle=acosf(dot),den=sinf(angle);
    wa=sinf((1-t)*angle)/den;wb=sinf(t*angle)/den;
   }
   for(k=0;k<4;k++)out->joints[j].rotate[k]=a->joints[j].rotate[k]*wa+b->joints[j].rotate[k]*wb*sign;
  }
 }
}

/* Decode the shipped IQM, independently of cgame's sampler. Both the native
 * pose and frame-only paths are checked against actual joint transforms. */
static void LoadAuthoredPoses(const char *path) {
 struct { int parent; unsigned mask; float offset[10],scale[10]; } poses[MAX_MODEL_JOINTS];
 unsigned h[27];char magic[16];FILE *f=fopen(path,"rb");int frame,j,k;
 CHECK(f!=NULL);CHECK(fread(magic,1,16,f)==16);CHECK(!memcmp(magic,"INTERQUAKEMODEL",14));
 CHECK(fread(h,sizeof(unsigned),27,f)==27);
 CHECK(h[19]<ARRAY_LEN(authored) && h[15]<=MAX_MODEL_JOINTS);
 CHECK(sizeof(poses[0])==88);
 fseek(f,h[16],SEEK_SET);CHECK(fread(poses,sizeof(poses[0]),h[15],f)==h[15]);
 fseek(f,h[21],SEEK_SET);authoredFrames=h[19];
 for(frame=0;frame<authoredFrames;frame++) {
  authored[frame].numJoints=h[15];
  for(j=0;j<(int)h[15];j++) {
   float c[10];
   for(k=0;k<10;k++) {
    unsigned short channel=0;
    if(poses[j].mask&(1<<k))CHECK(fread(&channel,2,1,f)==1);
    c[k]=poses[j].offset[k]+poses[j].scale[k]*channel;
   }
   VectorCopy(c,authored[frame].joints[j].translate);
   memcpy(authored[frame].joints[j].rotate,c+3,sizeof(quat_t));
   VectorCopy(c+7,authored[frame].joints[j].scale);
  }
 }
 fclose(f);
}
static float PoseDistance(const modelPose_t *a,const modelPose_t *b) {
 float distance=0;int j,k;
 CHECK(a->numJoints==b->numJoints);
 for(j=0;j<a->numJoints;j++)for(k=0;k<3;k++) {
  float d=fabs(a->joints[j].translate[k]-b->joints[j].translate[k]);
  if(d>distance)distance=d;
 }
 for(j=0;j<a->numJoints;j++)for(k=0;k<4;k++) {
  float d=fabs(a->joints[j].rotate[k]-b->joints[j].rotate[k]);
  if(d>distance)distance=d;
 }
 return distance;
}
static void Picture(playerState_t *ps,int msec,modelPose_t *out) {
 refEntity_t entity;playerState_t before=*ps;
 memset(&entity,0,sizeof(entity));entity.hModel=1;
 cg.time+=msec;
 CHECK(CG_WeapAnim_Apply(ps,&entity,msec));
 CHECK(!memcmp(ps,&before,sizeof(*ps)));
 if(entity.pose)*out=*entity.pose;
 else trap_R_BlendModelPoses(out,&authored[entity.frame],1-entity.backlerp,
  &authored[entity.oldframe],entity.backlerp);
}

static void Ready(playerState_t *ps,int weapon) {
 memset(ps,0,sizeof(*ps)); ps->weapon=weapon; ps->stats[STAT_HEALTH]=100;
 CG_WeapAnim_WeaponChanged(weapon);
}
static void Update(weapAnimDef_t *def,playerState_t *ps,int msec) {
 playerState_t before=*ps;
 CG_WeapAnim_UpdateLayers(def,ps,msec);
 CHECK(!memcmp(ps,&before,sizeof(*ps)));
}

static void TestSprint(weapAnimDef_t *def, int weapon) {
 playerState_t ps;
 animLayer_t *sprint=&cg_weapLayers[LAYER_SPRINT];
 refEntity_t sample;
 float len, time;
 int i;
 CHECK(def->clips[WANIM_SPRINT_IN].numFrames>1 && !def->clips[WANIM_SPRINT_IN].loop);
 CHECK(def->clips[WANIM_SPRINT_LOOP].numFrames>1 && def->clips[WANIM_SPRINT_LOOP].loop);
 CHECK(def->clips[WANIM_SPRINT_OUT].numFrames>1 && !def->clips[WANIM_SPRINT_OUT].loop);
 CHECK(fabs(CG_WeapAnim_ClipLength(def,WANIM_SPRINT_IN)-BG_WeaponSprintInTime(weapon))<1);
 CHECK(fabs(CG_WeapAnim_ClipLength(def,WANIM_SPRINT_OUT)-BG_WeaponSprintOutTime(weapon))<1);

 Ready(&ps,weapon);
 ps.weaponstate=WEAPON_SPRINT_IN; ps.weaponTime=BG_WeaponSprintInTime(ps.weapon);
 Update(def,&ps,16);
 CHECK(sprint->clip==WANIM_SPRINT_IN && sprint->weight==1 && sprint->time==0);
 ps.weaponTime/=2; CG_WeapAnim_UpdateLayers(def,&ps,16);
 len=CG_WeapAnim_LastFrameTime(def,WANIM_SPRINT_IN);
 CHECK(fabs(sprint->time-len*.5f)<.01f);
 CHECK(CG_WeapAnim_SampleFrame(def,sprint,&sample));
 CHECK(sample.frame>def->clips[WANIM_SPRINT_IN].firstFrame);

 /* A short sprint release reverses the partial picture, not a full carry. */
 ps.weaponstate=WEAPON_SPRINT_OUT; ps.weaponTime=BG_WeaponSprintOutTime(ps.weapon);
 Update(def,&ps,1);
 CHECK(sprint->clip==WANIM_SPRINT_OUT && fabs(sprint->startFraction-.5f)<.01f);
 len=CG_WeapAnim_LastFrameTime(def,WANIM_SPRINT_OUT);
 CHECK(fabs(sprint->time-len*.5f)<.01f);
 ps.weaponTime/=2; CG_WeapAnim_UpdateLayers(def,&ps,16);
 CHECK(fabs(sprint->time-len*.75f)<.01f);
 ps.weaponstate=WEAPON_SPRINT_IN; ps.weaponTime=BG_WeaponSprintInTime(ps.weapon);
 CG_WeapAnim_UpdateLayers(def,&ps,1);
 CHECK(fabs(sprint->startFraction-.25f)<.01f);

 /* Instant completion is just a new state; no named cancel or note check. */
 ps.weaponstate=WEAPON_SPRINTING; ps.weaponTime=0;
 CG_WeapAnim_UpdateLayers(def,&ps,8);
 CHECK(sprint->clip==WANIM_SPRINT_LOOP && sprint->weight==1);
 len=CG_WeapAnim_ClipLength(def,WANIM_SPRINT_LOOP);
 time=sprint->time;
 for(i=0;i<100;i++) {
  CG_WeapAnim_UpdateLayers(def,&ps,33);
  CHECK(sprint->time>time); time=sprint->time;
  CHECK(CG_WeapAnim_SampleFrame(def,sprint,&sample));
  CHECK(sample.frame>=def->clips[WANIM_SPRINT_LOOP].firstFrame &&
        sample.frame<def->clips[WANIM_SPRINT_LOOP].firstFrame+def->clips[WANIM_SPRINT_LOOP].numFrames);
 }
 /* Stopping early in any loop exits immediately, not at the end of a lap. */
 sprint->time=len*.1f;
 ps.weaponstate=WEAPON_SPRINT_OUT; ps.weaponTime=BG_WeaponSprintOutTime(ps.weapon);
 CG_WeapAnim_UpdateLayers(def,&ps,8);
 CHECK(sprint->clip==WANIM_SPRINT_OUT && sprint->time==0);

 /* Gameplay-ready/fire releases even a still-busy sprint picture. */
 ps.weaponstate=WEAPON_FIRING; ps.weaponTime=100;
 sprint->time=1;
 CG_WeapAnim_UpdateLayers(def,&ps,60);
 CHECK(sprint->targetWeight==0 && sprint->weight==0);
 CHECK(cg_weapLayers[LAYER_BASE].clip==WANIM_FIRE);
 CG_WeapAnim_WeaponChanged(WP_MACHINEGUN);
 CHECK(sprint->weight==0 && sprint->time==0);
}

static int StateClip(int state) {
 switch(state) {
 case WEAPON_RAISING: return WANIM_RAISE;
 case WEAPON_DROPPING: return WANIM_DROP;
 case WEAPON_FIRING: case WEAPON_BOLTING: return WANIM_FIRE;
 case WEAPON_SPRINT_IN: return WANIM_SPRINT_IN;
 case WEAPON_SPRINTING: return WANIM_SPRINT_LOOP;
 case WEAPON_SPRINT_OUT: return WANIM_SPRINT_OUT;
 }
 return WANIM_IDLE;
}
static void BeginSource(weapAnimDef_t *def,playerState_t *ps,int weapon,int state) {
 Ready(ps,weapon); ps->weaponstate=state;
 switch(state) {
 case WEAPON_RAISING: ps->weaponTime=BG_WeaponRaiseTime(weapon)/2; break;
 case WEAPON_DROPPING: ps->weaponTime=BG_WeaponDropTime(weapon)/2; break;
 case WEAPON_FIRING: case WEAPON_BOLTING:
  ps->weaponAnimSeq=ASEQ_FIRE; ps->weaponAnimTime=BG_WeaponFireLength(weapon)/3; break;
 case WEAPON_SPRINT_IN: ps->weaponTime=BG_WeaponSprintInTime(weapon)/2; break;
 case WEAPON_SPRINT_OUT: ps->weaponTime=BG_WeaponSprintOutTime(weapon)/2; break;
 }
 Update(def,ps,1);
}

static void TestOwnership(weapAnimDef_t *def) {
 const int states[]={WEAPON_RAISING,WEAPON_DROPPING,WEAPON_FIRING,WEAPON_BOLTING,
  WEAPON_SPRINT_IN,WEAPON_SPRINTING,WEAPON_SPRINT_OUT};
 const int weapons[]={WP_SHOTGUN,WP_MACHINEGUN,WP_RAILGUN,WP_M40A3};
 animLayer_t *retained=&cg_weapLayers[LAYER_RETAINED];
 playerState_t ps;
 refEntity_t sample;
 int s,w,ladder,t,clip;
 float time;
 /* Reuse the same authored clips with several weapon IDs and both holster
  * destinations. This intentionally catches per-weapon/per-destination rules. */
 for(w=0;w<ARRAY_LEN(weapons);w++) for(s=0;s<ARRAY_LEN(states);s++) for(ladder=0;ladder<2;ladder++) {
  BeginSource(def,&ps,weapons[w],states[s]); clip=StateClip(states[s]);
  /* Pmove keeps the action under a sprint (MW2's CheckForSprint), as it
   * keeps a raise or a drop: the sprint flag alone acquires their clip. The
   * shot's end lowers the gun into the sprint entry. */
  ps.pm_flags|=PMF_SPRINTING;
  if(states[s]==WEAPON_FIRING) ps.weaponstate=WEAPON_SPRINT_IN;
  Update(def,&ps,1);
  /* Sprint clips acquire on the drop itself; other clips acquire on entry. */
  ps.weaponstate=WEAPON_DROPPING;
  ps.weaponTime=ladder ? BG_LADDER_DROP_TIME : BG_WeaponDropTime(ps.weapon);
  if(ladder) { ps.stats[STAT_LADDER]=LADDER_ATTACHED|LADDER_HOLSTER; ps.pm_flags&=~PMF_SPRINTING; }
  for(t=ps.weaponTime;t>0;t--) {
   ps.weaponTime=t; Update(def,&ps,1);
   if(!CG_WeapAnim_HoldsDrop()) fprintf(stderr,"ownership: weapon %d state %d ladder %d remaining %d source %d\n",
    weapons[w],states[s],ladder,t,clip);
   CHECK(CG_WeapAnim_HoldsDrop());
   CHECK(retained->clip==clip && retained->weight==1 && retained->targetWeight==1);
   CHECK(cg_weapLayers[LAYER_MOVE].weight==0 && cg_weapLayers[LAYER_SPRINT].weight==0);
   CHECK(CG_WeapAnim_LadderLower()==0);	/* a retained picture keeps its pose: no ladder lowering under it */
  }
  /* Even a short source stays the owner for arbitrarily long holsters.
   * Authored one-shots clamp; authored loops keep moving. */
  Update(def,&ps,(int)(CG_WeapAnim_ClipLength(def,clip)*3));
  CHECK(CG_WeapAnim_HoldsDrop());
  CHECK(CG_WeapAnim_SampleFrame(def,retained,&sample));
  if(!def->clips[clip].loop) {
   int end=def->clips[clip].firstFrame+def->clips[clip].numFrames-1;
   CHECK(sample.frame==end && sample.oldframe==end);
  } else CHECK(sample.frame!=sample.oldframe);
  if(ladder) {
   ps.weaponstate=WEAPON_LADDER; Update(def,&ps,1);
   CHECK(!CG_WeapAnim_HoldsDrop() && retained->weight==0);
  } else {
   ps.pm_flags=0; ps.weaponstate=WEAPON_READY; ps.weaponTime=0;
   Update(def,&ps,1);
   CHECK(!CG_WeapAnim_HoldsDrop() && retained->weight<1 && retained->targetWeight==0);
   ps.weaponstate=WEAPON_FIRING; Update(def,&ps,1);
   CHECK(retained->weight==0 && cg_weapLayers[LAYER_BASE].clip==WANIM_FIRE);
  }
 }
 /* Initial pullout and sprint may arrive in the same first rendered frame. */
 Ready(&ps,WP_SHOTGUN); ps.weaponstate=WEAPON_RAISING;
 ps.weaponTime=BG_WeaponRaiseTime(ps.weapon); ps.pm_flags=PMF_SPRINTING;
 Update(def,&ps,1);
 CHECK(cg_retainedActive && retained->clip==WANIM_RAISE);
 /* Without sprint, interrupting a draw is not retention either: that same draw
  * runs back down from the pose on screen, never from the drop's first frame. */
 BeginSource(def,&ps,WP_SHOTGUN,WEAPON_RAISING); time=cg_weapLayers[LAYER_MOVE].time;
 ps.weaponstate=WEAPON_DROPPING; ps.weaponTime=BG_WeaponDropTime(ps.weapon);
 Update(def,&ps,1);
 CHECK(!cg_retainedActive && cg_weapLayers[LAYER_MOVE].clip==WANIM_RAISE);
 CHECK(cg_weapLayers[LAYER_MOVE].fit==MOVE_FIT_REVERSE && fabs(cg_weapLayers[LAYER_MOVE].time-time)<1);
 /* With no holster, a finished one-shot releases to normal sprint entry: the
  * action ends READY under the sprint flag, and the entry follows. */
 BeginSource(def,&ps,WP_SHOTGUN,WEAPON_BOLTING);
 ps.pm_flags=PMF_SPRINTING; Update(def,&ps,1);
 CHECK(cg_retainedActive && retained->clip==WANIM_FIRE);
 Update(def,&ps,(int)CG_WeapAnim_ClipLength(def,WANIM_FIRE)*2);
 ps.weaponstate=WEAPON_READY; ps.weaponAnimTime=-1; Update(def,&ps,1);
 CHECK(!cg_retainedActive && cg_weapLayers[LAYER_SPRINT].weight==0);
 ps.weaponstate=WEAPON_SPRINT_IN; ps.weaponTime=BG_WeaponSprintInTime(ps.weapon); Update(def,&ps,1);
 CHECK(!cg_retainedActive && cg_weapLayers[LAYER_SPRINT].clip==WANIM_SPRINT_IN);
 CHECK(cg_weapLayers[LAYER_SPRINT].startFraction==0);
 ps.weaponstate=WEAPON_SPRINTING; ps.weaponTime=0; Update(def,&ps,1);
 CHECK(!cg_retainedActive && cg_weapLayers[LAYER_SPRINT].clip==WANIM_SPRINT_LOOP);
}

static void TestExclusions(weapAnimDef_t *def) {
 const int equipment[]={WP_KNIFE,WP_FRAG,WP_FLASH,WP_SEMTEX};
 const int actions[]={WA_MELEE,WA_LETHAL,WA_TACTICAL};
 playerState_t ps;
 int i;
 for(i=RSEQ_START;i<=RSEQ_END;i++) {
  Ready(&ps,WP_SHOTGUN); ps.weaponstate=WEAPON_RELOADING;
  ps.weaponAnimSeq=i; ps.weaponAnimTime=BG_WeaponReloadSegLength(ps.weapon,i)/2;
  Update(def,&ps,1);
  ps.pm_flags=PMF_SPRINTING; ps.weaponstate=WEAPON_SPRINT_IN; Update(def,&ps,1);
  CHECK(!cg_retainedActive);
  CHECK(cg_weapLayers[LAYER_RETAINED].weight==0);
 }
 for(i=0;i<ARRAY_LEN(equipment);i++) {
  BeginSource(def,&ps,equipment[i],WEAPON_RAISING);
  ps.pm_flags=PMF_SPRINTING; Update(def,&ps,1);
  ps.weaponstate=WEAPON_DROPPING; Update(def,&ps,1);
  CHECK(!cg_retainedActive && !cg_previousSprint);
 }
 for(i=0;i<ARRAY_LEN(actions);i++) {
  BeginSource(def,&ps,WP_SHOTGUN,WEAPON_RAISING);
  ps.pm_flags=PMF_SPRINTING; Update(def,&ps,1); CHECK(cg_retainedActive);
  ps.weaponAction=actions[i]; Update(def,&ps,1);
  CHECK(!cg_retainedActive && cg_weapLayers[LAYER_RETAINED].weight==0);
 }
 BeginSource(def,&ps,WP_SHOTGUN,WEAPON_RAISING);
 ps.pm_flags=PMF_SPRINTING; Update(def,&ps,1);
 ps.weaponstate=WEAPON_RELOADING; Update(def,&ps,1);
 CHECK(!cg_retainedActive && cg_weapLayers[LAYER_RETAINED].weight==0);
 BeginSource(def,&ps,WP_SHOTGUN,WEAPON_RAISING);
 ps.pm_flags=PMF_SPRINTING; Update(def,&ps,1);
 ps.stats[STAT_HEALTH]=0; Update(def,&ps,1);
 CHECK(!cg_retainedActive && cg_weapLayers[LAYER_RETAINED].weight==0);
}

static void TestRelease(weapAnimDef_t *def) {
 playerState_t ps;
 animLayer_t *retained=&cg_weapLayers[LAYER_RETAINED];
 animLayer_t *move=&cg_weapLayers[LAYER_MOVE];
 float time,weight;
 int held,t;
 for(held=0;held<2;held++) {
  BeginSource(def,&ps,WP_SHOTGUN,WEAPON_SPRINT_IN);
  ps.weaponstate=WEAPON_DROPPING; ps.weaponTime=BG_WeaponDropTime(ps.weapon);
  Update(def,&ps,1); CHECK(CG_WeapAnim_HoldsDrop()); time=retained->time;
  ps.weaponstate=held ? WEAPON_SPRINT_IN : WEAPON_READY;
  ps.weaponTime=held ? BG_WeaponSprintInTime(ps.weapon) : 0;
  Update(def,&ps,1);
  CHECK(!cg_retainedActive && retained->targetWeight==0 && retained->weight<1);
  if(held) {
   CHECK(cg_weapLayers[LAYER_SPRINT].time>=time);
   CHECK(cg_weapLayers[LAYER_SPRINT].startFraction>0);
  } else {
   CHECK(retained->weight==0 && move->fit==MOVE_FIT_RETURN);
   CHECK(move->clip==WANIM_SPRINT_IN && move->weight==1);
   CHECK(move->time<time && move->returnTo==0);
  }
  weight=retained->weight;
  for(t=0;t<100 && retained->weight>0;t++) {
   Update(def,&ps,1); CHECK(retained->weight<weight); weight=retained->weight;
  }
  CHECK(retained->weight==0);
 }
}

static void TestContext(weapAnimDef_t *def) {
 playerState_t ps;
 int i;
 for(i=0;i<6;i++) {
  Ready(&ps,WP_SHOTGUN); ps.weaponstate=WEAPON_RAISING;
  CG_WeapAnim_BuildPose(&ps,1,1);
  ps.pm_flags=PMF_SPRINTING; CG_WeapAnim_BuildPose(&ps,1,1);
  CHECK(cg_retainedActive);
  ps.weaponstate=WEAPON_READY; ps.pm_flags=0;
  switch(i) {
  case 0: ps.weapon=WP_MACHINEGUN; break;
  case 1: ps.clientNum++; break;
  case 2: ps.persistant[PERS_SPAWN_COUNT]++; break;
  case 3: ps.eFlags^=EF_TELEPORT_BIT; break;
  case 4: ps.stats[STAT_ACTIVE_SLOT]=SLOT_SECONDARY; break;
  case 5: ps.slotAttachments[SLOT_PRIMARY]=BG_ATTACHMENT_REDDOT; break;
  }
  CG_WeapAnim_BuildPose(&ps,1,1);
  CHECK(!cg_retainedActive && cg_weapLayers[LAYER_RETAINED].weight==0);
 }
 (void)def;
}
static void TestFirstDraw(weapAnimDef_t *def, int weapon) {
 playerState_t ps;
 refEntity_t sample;
 int t;
 Ready(&ps,weapon);
 ps.weaponDraw=1; ps.weaponstate=WEAPON_RAISING;
 ps.weaponTime=BG_WeaponDrawTime(&ps)/2;
 Update(def,&ps,16);
 CHECK(cg_weapLayers[LAYER_MOVE].clip==WANIM_FIRST_RAISE);
 CHECK(fabs(CG_WeapAnim_ClipLength(def,WANIM_FIRST_RAISE)-BG_WeaponFirstRaiseTime(ps.weapon))<1);
 ps.pm_flags=PMF_SPRINTING; Update(def,&ps,1);
 CHECK(cg_retainedActive && cg_weapLayers[LAYER_RETAINED].clip==WANIM_FIRST_RAISE);
 ps.weaponstate=WEAPON_DROPPING; ps.weaponTime=BG_HolsterTime(&ps);
 for(t=0;t<2000;t+=8) Update(def,&ps,8);
 CHECK(cg_retainedActive && cg_weapLayers[LAYER_RETAINED].clip==WANIM_FIRST_RAISE);
 CHECK(cg_weapLayers[LAYER_RETAINED].time>=CG_WeapAnim_ClipLength(def,WANIM_FIRST_RAISE));
 CHECK(CG_WeapAnim_SampleFrame(def,&cg_weapLayers[LAYER_RETAINED],&sample));
 CHECK(sample.frame==def->clips[WANIM_FIRST_RAISE].firstFrame+def->clips[WANIM_FIRST_RAISE].numFrames-1);
 ps.weaponstate=WEAPON_READY; ps.weaponTime=0; ps.pm_flags=0;
 Update(def,&ps,1); CHECK(!cg_retainedActive);
 for(t=0;t<100;t++) Update(def,&ps,1);
 CHECK(cg_weapLayers[LAYER_RETAINED].weight==0);
}
/* YY is one continuous motion, driven by the REAL PM_Weapon (no hand-built
 * states). Gameplay is READY the moment the drop is cancelled; only the picture
 * eases home along the clip it was showing, and a shot takes it over at once. */
void PickupWeaponStep(pmove_t *move, int msec);
int PickupNoteTime(int weapon, int seq, int kind);
static pmove_t flow;
static playerState_t flowPs;
static void FlowTrace(trace_t *t,const vec3_t a,const vec3_t b,const vec3_t c,const vec3_t d,int e,int f) {
 (void)a;(void)b;(void)c;(void)e;(void)f;
 memset(t,0,sizeof(*t)); t->fraction=1; VectorCopy(d,t->endpos); t->entityNum=ENTITYNUM_NONE;
}
static void FlowBeginWith(int secondary) {
 bg_class_t cl=*BG_Class(0);
 memset(&flowPs,0,sizeof(flowPs)); memset(&flow,0,sizeof(flow));
 cl.slot[0]=WP_MACHINEGUN; cl.slot[1]=secondary; BG_ApplyClass(&flowPs,&cl);
 flowPs.stats[STAT_HEALTH]=100; flowPs.pm_type=PM_NORMAL;
 flowPs.weapon=WP_MACHINEGUN; flowPs.weaponstate=WEAPON_READY;
 flow.ps=&flowPs; flow.trace=FlowTrace; flow.cmd.weapon=BG_SLOT_COMMAND+SLOT_PRIMARY;
 CG_WeapAnim_Reset();
}
static void FlowBegin(void) { FlowBeginWith(WP_SHOTGUN); }
/* One 8ms command, then one rendered frame of the real presentation. */
static void FlowStep(void) {
 playerState_t before;
 flow.cmd.serverTime+=8; PickupWeaponStep(&flow,8);
 before=flowPs; cg.time+=8; CG_WeapAnim_BuildPose(&flowPs,1,8);
 CHECK(!memcmp(&before,&flowPs,sizeof(flowPs)));
}
static void FlowSlot(int slot) { flow.cmd.weapon=BG_SLOT_COMMAND+slot; }
/* Ease home, never jump: every frame moves toward the rest pose by at most the
 * whole clip over MOVE_RETURN_MS at smoothstep's 1.5x peak, and arrives in time. */
static void FlowReturn(int clip,float home,float len) {
 animLayer_t *move=&cg_weapLayers[LAYER_MOVE];
 float last,step=len/MOVE_RETURN_MS*8*1.5f+.01f,steps[64],peak=0;
 int held,n=0;
 CHECK(flowPs.weaponstate==WEAPON_READY && flowPs.weaponTime==0);
 CHECK(cg_returnTime==RETURN_BLEND_MSEC); /* no second blend over the clip return */
 CHECK(move->clip==clip && move->fit==MOVE_FIT_RETURN && move->weight==1);
 for(held=0;move->targetWeight>0 && held<=MOVE_RETURN_MS+16;held+=8) {
  last=move->time; FlowStep();
  CHECK(move->clip==clip && fabs(move->time-last)<=step);
  CHECK(fabs(home-move->time)<=fabs(home-last)+.01f);
  if(n<64) steps[n++]=fabs(move->time-last);
 }
 CHECK(move->targetWeight==0 && fabs(move->time-home)<.01f && held<=MOVE_RETURN_MS+8);
 /* Eased, not linear: it sets off and settles gently around a faster middle. */
 for(held=0;held<n;held++) if(steps[held]>peak) peak=steps[held];
 if(n>=8) CHECK(steps[0]<peak*.5f && steps[n-1]<peak*.5f);
 for(held=0;held<48;held+=8) {
  FlowStep();CHECK(fabs(move->time-home)<.01f);
 }
 CHECK(move->weight==0);
}
/* A Y pressed while a cancel is still easing home carries on from that pose. */
static void FlowHolsterMidReturn(weapAnimDef_t *def,int clip) {
 animLayer_t *move=&cg_weapLayers[LAYER_MOVE];
 float last=move->time,len=CG_WeapAnim_ClipLength(def,clip);
 int t;
 for(t=0;t<MOVE_RETURN_MS/3;t+=8) { FlowStep(); }
 CHECK(move->fit==MOVE_FIT_RETURN && move->clip==clip);
 last=move->time;
 FlowSlot(flowPs.stats[STAT_ACTIVE_SLOT]==SLOT_PRIMARY ? SLOT_SECONDARY : SLOT_PRIMARY); FlowStep();
 CHECK(flowPs.weaponstate==WEAPON_DROPPING && move->clip==clip && move->weight==1);
 CHECK(fabs(move->time-last)<=8*len/BG_WeaponDropTime(flowPs.weapon)+.01f);
}
static void TestFlow(weapAnimDef_t *def) {
 animLayer_t *move=&cg_weapLayers[LAYER_MOVE];
 float raise=CG_WeapAnim_ClipLength(def,WANIM_RAISE), drop=CG_WeapAnim_ClipLength(def,WANIM_DROP), last;
 int sixth,t;
 for(sixth=1;sixth<=5;sixth+=2) {
  /* YY during the SPAS draw: Y reverses the same draw, Y again eases it up. */
  FlowBegin(); FlowSlot(SLOT_SECONDARY);
  for(t=0;t<10000 && !(flowPs.weapon==WP_SHOTGUN && flowPs.weaponstate==WEAPON_RAISING &&
   flowPs.weaponTime<=BG_WeaponRaiseTime(WP_SHOTGUN)*(6-sixth)/6);t+=8) FlowStep();
  CHECK(move->clip==WANIM_RAISE && move->weight==1); last=move->time;
  FlowSlot(SLOT_PRIMARY);
  for(t=0;t<48;t+=8) {
   FlowStep(); CHECK(flowPs.weaponstate==WEAPON_DROPPING);
   /* Reverse the visible stretch over the holster lock, which need not equal
    * the draw's duration. Never traverse more than a whole draw in that time. */
   CHECK(move->clip==WANIM_RAISE && move->fit==MOVE_FIT_REVERSE && move->time<=last+.01f &&
    last-move->time<=8*raise/BG_WeaponDropTime(WP_SHOTGUN)+.01f);
   last=move->time;
  }
  FlowSlot(SLOT_SECONDARY); FlowStep();
  FlowReturn(WANIM_RAISE,raise,raise);
  /* YY cancelling the SPAS holster: the gun comes back up its own drop. */
  for(t=0;t<200;t+=8) FlowStep();
  CHECK(flowPs.weaponstate==WEAPON_READY && move->weight==0);
  FlowSlot(SLOT_PRIMARY); FlowStep();
  /* A gun at rest fades into its holster instead of popping to frame 0. */
  CHECK(flowPs.weaponstate==WEAPON_DROPPING && move->clip==WANIM_DROP && move->weight>0 && move->weight<1);
  while(flowPs.weaponstate==WEAPON_DROPPING &&
   flowPs.weaponTime>BG_WeaponDropTime(WP_SHOTGUN)*(6-sixth)/6) FlowStep();
  CHECK(flowPs.weaponstate==WEAPON_DROPPING && move->clip==WANIM_DROP && move->weight==1);
  FlowSlot(SLOT_SECONDARY); FlowStep();
  FlowReturn(WANIM_DROP,0,drop);
 }
 /* Y again while either kind of cancel is still easing home. */
 FlowBegin(); FlowSlot(SLOT_SECONDARY);
 for(t=0;t<10000 && !(flowPs.weapon==WP_SHOTGUN && flowPs.weaponstate==WEAPON_RAISING &&
  flowPs.weaponTime<=BG_WeaponRaiseTime(WP_SHOTGUN)/2);t+=8) FlowStep();
 FlowSlot(SLOT_PRIMARY); FlowStep(); FlowSlot(SLOT_SECONDARY); FlowStep();
 FlowHolsterMidReturn(def,WANIM_RAISE);
 CHECK(move->fit==MOVE_FIT_REVERSE);
 FlowBegin(); FlowSlot(SLOT_SECONDARY);
 for(t=0;t<10000 && !(flowPs.weapon==WP_SHOTGUN && flowPs.weaponstate==WEAPON_READY && move->weight==0);t+=8) FlowStep();
 FlowSlot(SLOT_PRIMARY); FlowStep();
 while(flowPs.weaponstate==WEAPON_DROPPING && flowPs.weaponTime>BG_WeaponDropTime(WP_SHOTGUN)/2) FlowStep();
 CHECK(flowPs.weaponstate==WEAPON_DROPPING && move->clip==WANIM_DROP);
 FlowSlot(SLOT_SECONDARY); FlowStep();
 FlowHolsterMidReturn(def,WANIM_DROP);
 CHECK(move->fit==MOVE_FIT_FORWARD);
 /* Sprinting out of a cancel still easing home carries nothing: the return is
  * not an action, and retaining the drop would send the gun back down. */
 FlowSlot(SLOT_SECONDARY); FlowStep();
 CHECK(flowPs.weaponstate==WEAPON_READY && move->fit==MOVE_FIT_RETURN && move->clip==WANIM_DROP);
 flowPs.pm_flags|=PMF_SPRINTING; flowPs.weaponstate=WEAPON_SPRINT_IN;
 flowPs.weaponTime=BG_WeaponSprintInTime(flowPs.weapon);
 Update(def,&flowPs,8);
 CHECK(!cg_retainedActive && cg_weapLayers[LAYER_SPRINT].clip==WANIM_SPRINT_IN);
 /* YY then fire: the shot owns the picture on its first frame. */
 FlowBegin(); FlowSlot(SLOT_SECONDARY);
 for(t=0;t<10000 && !(flowPs.weapon==WP_SHOTGUN && flowPs.weaponstate==WEAPON_RAISING &&
  flowPs.weaponTime<=BG_WeaponRaiseTime(WP_SHOTGUN)/2);t+=8) FlowStep();
 FlowSlot(SLOT_PRIMARY); FlowStep(); FlowSlot(SLOT_SECONDARY); FlowStep();
 CHECK(flowPs.weaponstate==WEAPON_READY && move->fit==MOVE_FIT_RETURN);
 flow.cmd.buttons=BUTTON_ATTACK; FlowStep();
 CHECK(flowPs.weaponstate==WEAPON_FIRING && move->targetWeight==0 && move->fit==MOVE_FIT_NONE);
 CHECK(cg_weapLayers[LAYER_BASE].clip==WANIM_FIRE);
}
/* The knife puts nothing down (MW2's MeleeInit and MeleeEnd write over the
 * gun's state): the press takes the gun away on that command, no drop is ever
 * fitted for it or after it, and the gun comes back up its draw clip fitted to
 * the quick raise pmove stamped at BG_MELEE_TIME, arriving with that lock. */
static void TestKnifePicture(weapAnimDef_t *def) {
 animLayer_t *move=&cg_weapLayers[LAYER_MOVE];
 float raise=CG_WeapAnim_ClipLength(def,WANIM_RAISE),last=0;
 int t,since;
 FlowBegin(); FlowSlot(SLOT_SECONDARY);
 for(t=0;t<10000 && !(flowPs.weapon==WP_SHOTGUN && flowPs.weaponstate==WEAPON_READY && move->weight==0);t+=8) FlowStep();
 flow.cmd.buttons=BUTTON_MELEE; FlowStep(); flow.cmd.buttons=0;
 CHECK(flowPs.weapon==WP_KNIFE && flowPs.weaponstate==WEAPON_RAISING && (flowPs.weaponAction&WA_TYPE_MASK)==WA_MELEE);
 for(since=0;since<10000 && flowPs.weapon==WP_KNIFE;) {
  FlowStep(); since+=8;
  CHECK(flowPs.weaponstate!=WEAPON_DROPPING && !(move->fit==MOVE_FIT_FORWARD && move->clip==WANIM_DROP));
 }
 /* the first 8ms command at or past meleeTime: straight into the quick raise */
 CHECK(since>=BG_MELEE_TIME && since-8<BG_MELEE_TIME);
 CHECK(flowPs.weapon==WP_SHOTGUN && flowPs.weaponstate==WEAPON_RAISING &&
  flowPs.weaponTime==BG_WeaponQuickRaiseTime(WP_SHOTGUN) && BG_WeaponDrawTime(&flowPs)==flowPs.weaponTime);
 CHECK(move->clip==WANIM_RAISE && move->fit==MOVE_FIT_FORWARD);
 while(flowPs.weaponstate==WEAPON_RAISING) {
  float want=(1.0f-(float)flowPs.weaponTime/BG_WeaponQuickRaiseTime(WP_SHOTGUN))*raise;
  CHECK(move->clip==WANIM_RAISE && fabs(move->time-want)<=.01f && move->time>=last-.01f);
  last=move->time; FlowStep();
 }
 CHECK(last>=raise*.9f && flowPs.weaponstate==WEAPON_READY);
}
/* A grenade pressed during a running holster keeps that drop: the picture runs
 * on at the gun's own rate, across BG_HolsterTime handing over to the quick
 * drop's fit, and arrives with the lock. */
static void TestKeptHolster(weapAnimDef_t *def) {
 animLayer_t *move=&cg_weapLayers[LAYER_MOVE];
 float drop=CG_WeapAnim_ClipLength(def,WANIM_DROP),last=0;
 float rate=8*drop/BG_WeaponDropTime(WP_SHOTGUN)*1.02f+.01f;
 int t;
 FlowBegin(); FlowSlot(SLOT_SECONDARY);
 for(t=0;t<10000 && !(flowPs.weapon==WP_SHOTGUN && flowPs.weaponstate==WEAPON_READY && move->weight==0);t+=8) FlowStep();
 FlowSlot(SLOT_PRIMARY); FlowStep();
 for(t=0;t<200;t+=8) FlowStep();
 flow.cmd.buttons=BUTTON_LETHAL; FlowStep(); flow.cmd.buttons=0;
 CHECK((flowPs.weaponAction&WA_TYPE_MASK)==WA_LETHAL && flowPs.weapon==WP_SHOTGUN &&
  flowPs.weaponstate==WEAPON_DROPPING && flowPs.weaponTime>BG_WeaponQuickDropTime(WP_SHOTGUN));
 while(flowPs.weaponstate==WEAPON_DROPPING && flowPs.weapon==WP_SHOTGUN) {
  last=move->time; FlowStep();
  if(flowPs.weaponstate==WEAPON_DROPPING && flowPs.weapon==WP_SHOTGUN) {
   CHECK(move->clip==WANIM_DROP && move->time>=last-.01f && move->time-last<=rate);
  }
 }
 CHECK(last>=drop*.95f);
}
/* A ladder grabbed inside a running holster restamps nothing: MW2's
 * PM_BeginWeaponChange returns for a drop already running (0x575F96-0x575FB1),
 * so the swap's drop, or the draw it reversed, keeps its time and its fit. The
 * picture runs on at the same rate to the drop's own end, never skipped ahead
 * and never stalled, with none of the ladder holster's lowering under it; at
 * that end the hands are the ladder's (FinishWeaponChange on a ladder, 0x576388:
 * WEAPON_LADDER) and the gun is out of view. (This test pinned Peace's old
 * restamp of that drop as the 100ms ladder holster.) */
static void TestLadderKeepsDrop(weapAnimDef_t *def,int gun) {
 animLayer_t *move=&cg_weapLayers[LAYER_MOVE];
 float len,step,last;
 int reversed,t,lock,fit;
 for(reversed=0;reversed<2;reversed++) {
  FlowBeginWith(gun); FlowSlot(SLOT_SECONDARY);
  for(t=0;t<10000 && !(flowPs.weapon==gun && (reversed ? flowPs.weaponstate==WEAPON_RAISING &&
   flowPs.weaponTime<=BG_WeaponDrawTime(&flowPs)/2 : flowPs.weaponstate==WEAPON_READY && move->weight==0));t+=8) FlowStep();
  FlowSlot(SLOT_PRIMARY); FlowStep();
  while(flowPs.weaponstate==WEAPON_DROPPING && flowPs.weaponTime>BG_WeaponDropTime(gun)*(reversed ? 3 : 2)/4) FlowStep();
  CHECK(flowPs.weapon==gun && flowPs.weaponstate==WEAPON_DROPPING && BG_HolsterTime(&flowPs)==BG_WeaponDropTime(gun));
  CHECK(move->fit==(reversed ? MOVE_FIT_REVERSE : MOVE_FIT_FORWARD) && move->weight==1);
  len=CG_WeapAnim_ClipLength(def,move->clip); step=8*len/BG_WeaponDropTime(gun)*1.02f+.01f; last=move->time;
  fit=move->fit; lock=flowPs.weaponTime;
  flowPs.stats[STAT_LADDER]|=LADDER_ATTACHED;	/* PM_CheckLadder's grab, ahead of PM_Weapon */
  FlowStep();
  CHECK(flowPs.weapon==gun && flowPs.weaponstate==WEAPON_DROPPING && flowPs.weaponTime==lock-8);
  CHECK(!(flowPs.stats[STAT_LADDER]&LADDER_HOLSTER) && BG_HolsterTime(&flowPs)==BG_WeaponDropTime(gun));
  CHECK(fabs(move->time-last)<=step && (reversed ? move->time<last : move->time>last));
  while(flowPs.weaponstate==WEAPON_DROPPING && flowPs.weapon==gun) {
   last=move->time; lock=flowPs.weaponTime; FlowStep();
   if(flowPs.weaponstate==WEAPON_DROPPING && flowPs.weapon==gun) {
    CHECK(flowPs.weaponTime==lock-8 && !(flowPs.stats[STAT_LADDER]&LADDER_HOLSTER) && move->fit==fit);
    CHECK(fabs(move->time-last)<=step && (reversed ? move->time<=last+.01f : move->time>=last-.01f));
    CHECK(CG_WeapAnim_LadderLower()==0 && !BG_LadderHolsterDown(&flowPs) && !CG_WeapAnim_LadderStowed(&flowPs));
   }
  }
  CHECK(lock<=8 && flowPs.weapon==gun && flowPs.weaponstate==WEAPON_LADDER && CG_WeapAnim_LadderLower()==0);
  CHECK(reversed ? last<=len*.1f : last>=len*.9f);
 }
}
/* A holster keeps its sprint source through its whole duration. A grenade
 * pressed part way through leaves pmove running that same drop (MW2's
 * OffHandInit keeps a running drop), so the retained picture stays at full
 * weight until the weapon changes; it is never cut to idle mid-holster. */
static void TestRetainedKeptHolster(void) {
 animLayer_t *retained=&cg_weapLayers[LAYER_RETAINED];
 int quick,t,clip;
 for(quick=0;quick<2;quick++) {
  FlowBegin(); FlowSlot(SLOT_SECONDARY);
  for(t=0;t<10000 && !(flowPs.weapon==WP_SHOTGUN && flowPs.weaponstate==WEAPON_READY &&
   cg_weapLayers[LAYER_MOVE].weight==0);t+=8) FlowStep();
  flowPs.pm_flags|=PMF_SPRINTING;	/* PM_CheckSprint's flag, ahead of PM_Weapon */
  for(t=0;t<10000 && flowPs.weaponstate!=WEAPON_SPRINTING;t+=8) FlowStep();
  FlowSlot(SLOT_PRIMARY); FlowStep();
  CHECK(flowPs.weaponstate==WEAPON_DROPPING && CG_WeapAnim_HoldsDrop()); clip=retained->clip;
  /* Half the gun's own holster left, or less than a quick drop (the fit hands over). */
  while(flowPs.weaponstate==WEAPON_DROPPING && flowPs.weaponTime>(quick ?
   BG_WeaponQuickDropTime(WP_SHOTGUN)*3/4 : BG_WeaponDropTime(WP_SHOTGUN)/2)) FlowStep();
  CHECK(CG_WeapAnim_HoldsDrop() && retained->weight==1);
  flowPs.pm_flags&=~PMF_SPRINTING;	/* PM_CheckSprint ends sprint for the grenade */
  flow.cmd.buttons=BUTTON_LETHAL; FlowStep(); flow.cmd.buttons=0;
  CHECK((flowPs.weaponAction&WA_TYPE_MASK)==WA_LETHAL && flowPs.weapon==WP_SHOTGUN);
  while(flowPs.weapon==WP_SHOTGUN && flowPs.weaponstate==WEAPON_DROPPING) {
   CHECK(CG_WeapAnim_HoldsDrop() && retained->clip==clip && retained->weight==1 && retained->targetWeight==1);
   CHECK(cg_weapLayers[LAYER_MOVE].weight==0 && cg_weapLayers[LAYER_SPRINT].weight==0);
   FlowStep();
  }
  CHECK(BG_IsActionWeapon(flowPs.weapon) && !CG_WeapAnim_HoldsDrop() && retained->weight==0);
 }
}
/* Every drop starts from the pose on screen (owner's rule). A spectator, killcam
 * or demo sees one state per snapshot, so YY then Y in one snapshot reaches the
 * picture as the same drop restamped higher at the same BG_HolsterTime: it
 * carries on from the pose, never from frame 0, and still lands with the lock. */
static void TestSnapshotRestamp(weapAnimDef_t *def) {
 animLayer_t *move=&cg_weapLayers[LAYER_MOVE];
 float drop=CG_WeapAnim_ClipLength(def,WANIM_DROP),last;
 int t,lock,duration;
 FlowBegin(); FlowSlot(SLOT_SECONDARY);
 for(t=0;t<10000 && !(flowPs.weapon==WP_SHOTGUN && flowPs.weaponstate==WEAPON_READY && move->weight==0);t+=8) FlowStep();
 FlowSlot(SLOT_PRIMARY); FlowStep();
 while(flowPs.weaponstate==WEAPON_DROPPING && flowPs.weaponTime>BG_WeaponDropTime(WP_SHOTGUN)/2) FlowStep();
 CHECK(flowPs.weaponstate==WEAPON_DROPPING && move->clip==WANIM_DROP && move->fit==MOVE_FIT_FORWARD);
 last=move->time; lock=flowPs.weaponTime; duration=BG_HolsterTime(&flowPs);
 FlowSlot(SLOT_SECONDARY); flow.cmd.serverTime+=8; PickupWeaponStep(&flow,8);	/* no picture */
 CHECK(flowPs.weaponstate==WEAPON_READY);
 FlowSlot(SLOT_PRIMARY); FlowStep();
 CHECK(flowPs.weaponstate==WEAPON_DROPPING && flowPs.weaponTime>lock && BG_HolsterTime(&flowPs)==duration);
 CHECK(move->clip==WANIM_DROP && fabs(move->time-last)<=16*drop/duration+.01f);
 while(flowPs.weaponstate==WEAPON_DROPPING && flowPs.weapon==WP_SHOTGUN) {
  last=move->time; FlowStep();
  if(flowPs.weaponstate==WEAPON_DROPPING) CHECK(move->time>=last-.01f && move->time-last<=8*drop/duration+.01f);
 }
 CHECK(last>=drop*.9f);
}
/* MW2's OffHandInit keeps a running drop's time and its animation. On a gun
 * whose own drop is shorter than its quick drop (clips are data; any gun may get
 * them), a grenade hands the fit to the longer quick drop at the same lock: the
 * drop, or a late draw played back down, runs on at the gun's own rate. */
static void TestShortDropKept(void) {
 const int gun=WP_MACHINEGUN, drop=BG_WeaponDropTime(gun), quick=BG_WeaponQuickDropTime(gun);
 weapAnimDef_t *def=&bg_weapAnimDefs[gun];
 animLayer_t *move=&cg_weapLayers[LAYER_MOVE];
 float len,rate=0,last=0;
 int reversed,pressed,t;
 CHECK(drop<quick);
 CG_WeapAnim_RegisterClips(gun,1,"assets/baseq3/models/weapons2/spas12/view.cfg");
 for(reversed=0;reversed<2;reversed++) {
  FlowBegin(); FlowStep(); FlowSlot(SLOT_SECONDARY);
  if(reversed) {
   for(t=0;t<10000 && !(flowPs.weapon==WP_SHOTGUN && flowPs.weaponstate==WEAPON_READY);t+=8) FlowStep();
   /* Back down from above drop/quick of the draw: the longer fit needs an anchor above 1. */
   FlowSlot(SLOT_PRIMARY);
   for(t=0;t<10000 && !(flowPs.weapon==gun && flowPs.weaponstate==WEAPON_RAISING &&
    flowPs.weaponTime<=BG_WeaponDrawTime(&flowPs)*(quick-drop)/quick/2);t+=8) FlowStep();
   FlowSlot(SLOT_SECONDARY);
  }
  FlowStep();
  CHECK(flowPs.weapon==gun && flowPs.weaponstate==WEAPON_DROPPING && BG_HolsterTime(&flowPs)==drop);
  CHECK(move->fit==(reversed ? MOVE_FIT_REVERSE : MOVE_FIT_FORWARD));
  len=CG_WeapAnim_ClipLength(def,move->clip);
  if(reversed) CHECK(move->startFraction*quick>drop);
  for(pressed=0;flowPs.weapon==gun && flowPs.weaponstate==WEAPON_DROPPING;) {
   if(!pressed && flowPs.weaponTime<=drop/2) { flow.cmd.buttons=BUTTON_LETHAL; pressed=1; }
   last=move->time; FlowStep(); flow.cmd.buttons=0;
   if(flowPs.weapon!=gun || flowPs.weaponstate!=WEAPON_DROPPING) break;
   if(!rate) rate=fabs(move->time-last);
   CHECK(fabs(fabs(move->time-last)-rate)<=rate*.02f+.01f && (reversed ? move->time<=last+.01f : move->time>=last-.01f));
  }
  CHECK(pressed && (flowPs.weaponAction&WA_TYPE_MASK)==WA_LETHAL);
  if(!reversed) CHECK(fabs(rate-8*len/drop)<=rate*.02f && last>=len*.9f);
  rate=0;
 }
 CG_WeapAnim_RegisterClips(gun,0,NULL);
}
/* A short sprint, Y, end the sprint, Y: the real weapon think cancels the
 * holster immediately. Its retained entry must travel home along the authored
 * motion in both native and frame-only rendering, with no gameplay lock.
 * PM_CheckSprint's flag is set and cleared directly here; how a sprint ends
 * (MW2's latch) is TestSprintReentry's and the loadout suite's. */
static void TestSprintFlow(weapAnimDef_t *def) {
 animLayer_t *move=&cg_weapLayers[LAYER_MOVE], *retained=&cg_weapLayers[LAYER_RETAINED];
 modelPose_t before,after;
 int native,exiting,t,part,clip;
 float from,last,home;
 LoadAuthoredPoses("assets/baseq3/models/weapons2/spas12/view.iqm");
 for(native=0;native<2;native++)for(exiting=0;exiting<2;exiting++)for(part=1;part<4;part++) {
  nativePose=native;
  FlowBegin(); FlowSlot(SLOT_SECONDARY);
  for(t=0;t<10000 && !(flowPs.weapon==WP_SHOTGUN && flowPs.weaponstate==WEAPON_READY &&
   move->weight==0);t+=8) FlowStep();
  flow.cmd.buttons=BUTTON_SPRINT; flowPs.pm_flags|=PMF_SPRINTING;
  for(t=0;t<BG_WeaponSprintInTime(flowPs.weapon)*part/4;t+=8) FlowStep();
  CHECK(flowPs.weaponstate==WEAPON_SPRINT_IN);
  /* Ending it just before the first Y can also leave a partial exit as the
   * visible source. Both halves of a posture transition are cancellable. */
  if(exiting) {
   flow.cmd.buttons=0; flowPs.pm_flags&=~PMF_SPRINTING;
   FlowStep(); FlowStep(); CHECK(flowPs.weaponstate==WEAPON_SPRINT_OUT);
  }
  clip=exiting ? WANIM_SPRINT_OUT : WANIM_SPRINT_IN;
  FlowSlot(SLOT_PRIMARY); FlowStep();
  CHECK(flowPs.weaponstate==WEAPON_DROPPING && CG_WeapAnim_HoldsDrop());
  CHECK(retained->clip==clip && retained->weight==1);
  Picture(&flowPs,0,&before); from=retained->time;
  flow.cmd.buttons=0; flowPs.pm_flags&=~PMF_SPRINTING;
  FlowSlot(SLOT_SECONDARY); flow.cmd.serverTime+=8; PickupWeaponStep(&flow,8);
  Picture(&flowPs,0,&after);
  CHECK(flowPs.weaponstate==WEAPON_READY && flowPs.weaponTime==0);
  CHECK(!CG_WeapAnim_HoldsDrop() && retained->weight==0);
  CHECK(move->fit==MOVE_FIT_RETURN && move->clip==clip && move->weight==1);
  CHECK(fabs(move->time-from)<.01f && PoseDistance(&before,&after)<.001f);
  home=exiting ? CG_WeapAnim_LastFrameTime(def,clip) : 0;
  /* A partial entry runs back over the whole exit, as a sprint tap reverses
   * it; an exit under way keeps the exit's own pace for what is left of it. */
  CHECK(fabs(move->returnLength-(exiting ? (home-from)/home*BG_WeaponSprintOutTime(flowPs.weapon) :
   BG_WeaponSprintOutTime(flowPs.weapon)))<1);
  for(t=0;move->targetWeight>0 && t<BG_WeaponSprintOutTime(flowPs.weapon)+16;t+=8) {
   last=move->time; FlowStep();
   CHECK(flowPs.weaponstate==WEAPON_READY && flowPs.weaponTime==0);
   CHECK(move->clip==clip && fabs(home-move->time)<=fabs(home-last)+.01f);
  }
  CHECK(move->targetWeight==0 && fabs(move->time-home)<.01f && t+8>=move->returnLength);
  /* The return has reached home. Its short release must hold that endpoint;
   * advancing a returned entry away again gives the tail a visible hiccup. */
  for(t=0;t<48;t+=8) {
   FlowStep();CHECK(fabs(move->time-home)<.01f);
  }
  CHECK(move->weight==0 && cg_weapLayers[LAYER_SPRINT].weight==0);

  /* A trigger after cancellation takes the picture immediately, even while
   * the entry is still returning. It must never preserve a delayed shot. */
  flow.cmd.buttons=BUTTON_SPRINT; flowPs.pm_flags|=PMF_SPRINTING;
  for(t=0;t<64;t+=8) FlowStep();
  FlowSlot(SLOT_PRIMARY); FlowStep();
  flow.cmd.buttons=0; flowPs.pm_flags&=~PMF_SPRINTING;
  FlowSlot(SLOT_SECONDARY); FlowStep(); CHECK(move->fit==MOVE_FIT_RETURN);
  flow.cmd.buttons=BUTTON_ATTACK; FlowStep();
  CHECK(flowPs.weaponstate==WEAPON_FIRING && move->targetWeight==0 && move->fit==MOVE_FIT_NONE);
  CHECK(!cg_retainedActive && retained->weight==0);
 }
 nativePose=qfalse;
}

/* The glide's picture is the ordinary sprint exit's, minus the lock. Sprint, Y,
 * end the sprint, Y: gameplay is ready at once, and the gun comes home exactly
 * as the sprint layer would bring that same posture home when a sprint stops -
 * from the carry, from an exit already under way, out of the carry loop, and
 * back down a partial entry over the whole sprint-out time (a sprint tap's
 * reversal). So it leaves the carry at the exit's pace (the old double-eased
 * return barely moved for 40ms) and settles as gently as the exit does (the
 * old twice-rate retrace was home in half the exit, at twice its peak speed).
 * Shipped poses are compared frame by frame in both rendering paths, and every
 * duration is read from the weapon's tables, never written here. */
#define POSTURE_CARRY   0	/* the entry finished and holding its carry */
#define POSTURE_EXITING 1	/* an exit already part way home */
#define POSTURE_LOOP    2	/* the carry loop, at its widest bob */
#define POSTURE_PARTIAL 3	/* an entry still part way out */
#define SPRINT_FRAMES   64
static float GunRotationDistance(const modelPose_t *a,const modelPose_t *b) {
 float same=0,opposite=0;int k;
 for(k=0;k<4;k++) {
  float x=a->joints[0].rotate[k],y=b->joints[0].rotate[k];
  same+=(x-y)*(x-y);opposite+=(x+y)*(x+y);
 }
 return sqrtf(same<opposite ? same : opposite);
}
/* Clips may store a joint's rotation with either quaternion sign; q and -q
 * are the same rotation, so compare whichever sign is nearer. */
static float RigDistance(const modelPose_t *a,const modelPose_t *b) {
 float distance=0;int j,k;
 CHECK(a->numJoints==b->numJoints);
 for(j=0;j<a->numJoints;j++) {
  float same=0,opposite=0;
  for(k=0;k<3;k++) {
   float d=fabs(a->joints[j].translate[k]-b->joints[j].translate[k]);
   if(d>distance)distance=d;
  }
  for(k=0;k<4;k++) {
   float x=a->joints[j].rotate[k],y=b->joints[j].rotate[k];
   if(fabs(x-y)>same)same=fabs(x-y);
   if(fabs(x+y)>opposite)opposite=fabs(x+y);
  }
  if((same<opposite ? same : opposite)>distance)distance=same<opposite ? same : opposite;
 }
 return distance;
}
static void SprintPosture(playerState_t *ps,int weapon,int posture,int quarter) {
 modelPose_t pose;
 int in=BG_WeaponSprintInTime(weapon),out=BG_WeaponSprintOutTime(weapon);
 Ready(ps,weapon);
 ps->pm_flags=PMF_SPRINTING;ps->weaponstate=WEAPON_SPRINT_IN;ps->weaponTime=in;Picture(ps,0,&pose);
 if(posture==POSTURE_PARTIAL) {
  ps->weaponTime=in-in*quarter/4;Picture(ps,in*quarter/4,&pose);return;
 }
 ps->weaponTime=0;Picture(ps,in,&pose);
 if(posture==POSTURE_LOOP) {
  ps->weaponstate=WEAPON_SPRINTING;Picture(ps,0,&pose);
  Picture(ps,(int)CG_WeapAnim_ClipLength(&bg_weapAnimDefs[weapon],WANIM_SPRINT_LOOP)/4,&pose);
 } else if(posture==POSTURE_EXITING) {
  ps->pm_flags=0;ps->weaponstate=WEAPON_SPRINT_OUT;ps->weaponTime=out;Picture(ps,0,&pose);
  ps->weaponTime=out-out*quarter/4;Picture(ps,out*quarter/4,&pose);
 }
}
/* An ordinary sprint stop: pmove stamps the whole exit unless one is already
 * running, and READY follows the think its lock runs out. */
static int SprintExitFrames(playerState_t *ps,int weapon,modelPose_t *poses) {
 int n=0;
 ps->pm_flags=0;
 if(ps->weaponstate!=WEAPON_SPRINT_OUT) {
  ps->weaponstate=WEAPON_SPRINT_OUT;ps->weaponTime=BG_WeaponSprintOutTime(weapon);
 }
 Picture(ps,0,&poses[n++]);
 while(ps->weaponstate==WEAPON_SPRINT_OUT) {
  ps->weaponTime-=8;
  if(ps->weaponTime<=0) { ps->weaponTime=0;ps->weaponstate=WEAPON_READY; }
  CHECK(n<SPRINT_FRAMES);Picture(ps,8,&poses[n++]);
 }
 return n;
}
/* A sprint over the shot or the action, through the real PM_Weapon. Pmove
 * keeps the shot to its end and the action to its end (MW2's
 * PM_Weapon_CheckForSprint returns on FIRING/RECHAMBERING). MW2's shot ends
 * READY on its last think and its sprint check comes before the rechamber on
 * the next, so a sprint already on lowers the gun there, where the shot ended,
 * carrying the fire clip it was showing (the READY think between still shows
 * it), and the action is still owed: once the gun is back up it plays the
 * whole action from its open note. A sprint begun in the action lets the fire clip
 * play to its end with no sprint posture over it, and the entry starts from
 * rest on the think after the cycle ends READY: the gun never drops into a
 * carry over an unfinished pump or bolt. */
static void TestSprintAfterTheCycle(int gun) {
 animLayer_t *base=&cg_weapLayers[LAYER_BASE],*sprint=&cg_weapLayers[LAYER_SPRINT];
 animLayer_t *retained=&cg_weapLayers[LAYER_RETAINED];
 int press,t;
 /* pressed in the shot, in the action, and in the action after its close */
 for(press=0;press<3;press++) {
  FlowBeginWith(gun); FlowSlot(SLOT_SECONDARY);
  for(t=0;t<10000 && !(flowPs.weapon==gun && flowPs.weaponstate==WEAPON_READY &&
   cg_weapLayers[LAYER_MOVE].weight==0);t+=8) FlowStep();
  CHECK(flowPs.weapon==gun && flowPs.weaponstate==WEAPON_READY);
  flow.cmd.buttons=BUTTON_ATTACK; FlowStep(); flow.cmd.buttons=0;
  CHECK(flowPs.weaponstate==WEAPON_FIRING && base->clip==WANIM_FIRE);
  CHECK(flowPs.stats[STAT_UNCHAMBERED]&(1<<gun));
  for(t=0;t<10000 && press && !(flowPs.weaponstate==WEAPON_BOLTING &&
   (press==1 || !(flowPs.stats[STAT_UNCHAMBERED]&(1<<gun))));t+=8) FlowStep();
  CHECK(flowPs.weaponstate==(press ? WEAPON_BOLTING : WEAPON_FIRING));
  flowPs.pm_flags|=PMF_SPRINTING;
  if(!press) {
   /* the shot plays on to its end: its last think ends READY with the fire
    * clip still on screen at the open note, and the next lowers the gun
    * there, the action owed */
   for(t=0;t<10000;t+=8) {
    FlowStep();
    if(flowPs.weaponstate!=WEAPON_FIRING) break;
    CHECK(sprint->weight==0);
   }
   CHECK(flowPs.weaponstate==WEAPON_READY && BG_ShotAwaitsAction(&flowPs));
   CHECK(base->clip==WANIM_FIRE && sprint->weight==0);
   FlowStep();
   CHECK(flowPs.weaponstate==WEAPON_SPRINT_IN && flowPs.weaponTime==BG_WeaponSprintInTime(gun));
   CHECK(flowPs.stats[STAT_UNCHAMBERED]&(1<<gun));
   /* the picture carries the fire clip it was showing into the sprint */
   CHECK(cg_retainedActive && retained->clip==WANIM_FIRE);
   for(t=0;t<BG_WeaponSprintInTime(gun)+8 && flowPs.weaponstate==WEAPON_SPRINT_IN;t+=8) FlowStep();
   CHECK(flowPs.weaponstate==WEAPON_SPRINTING && (flowPs.stats[STAT_UNCHAMBERED]&(1<<gun)));
   for(t=0;t<2000;t+=8) FlowStep();
   CHECK(!cg_retainedActive && sprint->clip==WANIM_SPRINT_LOOP && sprint->weight==1);
   /* back up: the sprint-out's last think ends READY and is over (MW2's
    * SPRINT_DROP ends in PM_Weapon_Idle, iw4x 0x5793D8 entry 25), and the
    * next starts the whole action, from its open note on the fire clip (MW2's
    * rechamber from READY), the shot not played again */
   flowPs.pm_flags&=~PMF_SPRINTING;
   for(t=0;t<BG_WeaponSprintOutTime(gun)+8 && flowPs.weaponstate==WEAPON_SPRINTING;t+=8) FlowStep();
   for(t=0;t<BG_WeaponSprintOutTime(gun)+8 && flowPs.weaponstate==WEAPON_SPRINT_OUT;t+=8) FlowStep();
   CHECK(flowPs.weaponstate==WEAPON_READY && (flowPs.stats[STAT_UNCHAMBERED]&(1<<gun)));
   FlowStep();
   CHECK(flowPs.weaponstate==WEAPON_BOLTING && flowPs.weaponAnimTime==PickupNoteTime(gun,ASEQ_FIRE,WNOTE_BOLT_OPEN));
   FlowStep();
   CHECK(flowPs.weaponstate==WEAPON_BOLTING && flowPs.weaponAnimTime==PickupNoteTime(gun,ASEQ_FIRE,WNOTE_BOLT_OPEN)+8);
   CHECK(base->clip==WANIM_FIRE && fabs(base->time-(float)(PickupNoteTime(gun,ASEQ_FIRE,WNOTE_BOLT_OPEN)+8)/BG_WeaponFireLength(gun)*
    CG_WeapAnim_ClipLength(&bg_weapAnimDefs[gun],WANIM_FIRE))<.01f);
   for(t=0;t<10000 && flowPs.weaponstate==WEAPON_BOLTING;t+=8) FlowStep();
   CHECK(flowPs.weaponstate==WEAPON_READY && !(flowPs.stats[STAT_UNCHAMBERED]&(1<<gun)));
   continue;
  }
  for(t=0;t<10000;t+=8) {
   FlowStep();
   if(flowPs.weaponstate==WEAPON_READY) break;
   CHECK(flowPs.weaponstate==WEAPON_BOLTING);
   CHECK(base->clip==WANIM_FIRE && sprint->weight==0);
   CHECK(!cg_retainedActive || (retained->clip==WANIM_FIRE && fabs(retained->time-base->time)<.5f));
  }
  /* The cycle's last think: chambered, still no sprint posture. */
  CHECK(flowPs.weaponstate==WEAPON_READY && !(flowPs.stats[STAT_UNCHAMBERED]&(1<<gun)));
  CHECK(sprint->weight==0);
  FlowStep();
  CHECK(flowPs.weaponstate==WEAPON_SPRINT_IN && flowPs.weaponTime==BG_WeaponSprintInTime(gun));
  CHECK(!cg_retainedActive && retained->targetWeight==0);
  CHECK(sprint->clip==WANIM_SPRINT_IN && sprint->weight==1 && sprint->startFraction==0);
  for(t=0;t<BG_WeaponSprintInTime(gun)+8 && flowPs.weaponstate==WEAPON_SPRINT_IN;t+=8) FlowStep();
  CHECK(flowPs.weaponstate==WEAPON_SPRINTING && sprint->clip==WANIM_SPRINT_LOOP && sprint->weight==1);
 }
}
/* One fire clip from the shot to the action's end, through the real PM_Weapon
 * at 8ms. The shot's last think leaves the gun READY owing its action (MW2's
 * ShouldBeFiring) with the clip's clock at the open note, and the action
 * starts from there on the next think: the picture stays on the fire clip at
 * that clock across the READY think, with no base-action release eased over
 * it, and the release comes only when the action ends. */
static void TestShotPictureAcrossReady(int gun) {
 animLayer_t *base=&cg_weapLayers[LAYER_BASE];
 float len=CG_WeapAnim_ClipLength(&bg_weapAnimDefs[gun],WANIM_FIRE);
 int t,ready=0;
 CHECK(len>0);
 FlowBeginWith(gun); FlowSlot(SLOT_SECONDARY);
 for(t=0;t<10000 && !(flowPs.weapon==gun && flowPs.weaponstate==WEAPON_READY &&
  cg_weapLayers[LAYER_MOVE].weight==0);t+=8) FlowStep();
 flow.cmd.buttons=BUTTON_ATTACK; FlowStep(); flow.cmd.buttons=0;
 CHECK(flowPs.weaponstate==WEAPON_FIRING && base->clip==WANIM_FIRE);
 for(t=0;t<10000;t+=8) {
  FlowStep();
  if(flowPs.weaponstate!=WEAPON_FIRING && flowPs.weaponstate!=WEAPON_BOLTING && !BG_ShotAwaitsAction(&flowPs)) break;
  if(BG_ShotAwaitsAction(&flowPs)) {
   ready++;
   CHECK(flowPs.weaponstate==WEAPON_READY && flowPs.weaponAnimTime==PickupNoteTime(gun,ASEQ_FIRE,WNOTE_BOLT_OPEN));
  }
  CHECK(base->clip==WANIM_FIRE && base->weight==1);
  CHECK(fabs(base->time-(float)flowPs.weaponAnimTime/BG_WeaponFireLength(gun)*len)<.01f);
  CHECK(cg_returnTime==RETURN_BLEND_MSEC && CG_WeapAnim_ReturnWeight()==0);
 }
 /* one READY think between the shot and its action, and the cycle's end is
  * the release */
 CHECK(ready==1 && flowPs.weaponstate==WEAPON_READY && !(flowPs.stats[STAT_UNCHAMBERED]&(1<<gun)));
 CHECK(cg_returnTime==0 && base->clip==WANIM_IDLE);
}
/* MW2's dry fire with the trigger held from the last round's shot and nothing
 * anywhere: the gun stays FIRING with no fire clock (BG_DryFiring) and plays its
 * idle (PM_Weapon_CheckFiringAmmo, 0x5770A0), so the picture plays the shot to
 * its end and then eases home, never the pump or bolt it does not work. */
static void TestDryFirePicture(int gun) {
 animLayer_t *base=&cg_weapLayers[LAYER_BASE];
 int t;
 FlowBeginWith(gun); FlowSlot(SLOT_SECONDARY);
 for(t=0;t<10000 && !(flowPs.weapon==gun && flowPs.weaponstate==WEAPON_READY &&
  cg_weapLayers[LAYER_MOVE].weight==0);t+=8) FlowStep();
 flowPs.ammo[gun]=1; flowPs.ammoReserve[gun]=0;
 flow.cmd.buttons=BUTTON_ATTACK; FlowStep();
 CHECK(flowPs.weaponstate==WEAPON_FIRING && base->clip==WANIM_FIRE);
 for(t=0;t<10000 && flowPs.weaponstate==WEAPON_FIRING && !BG_DryFiring(&flowPs);t+=8) {
  CHECK(base->clip==WANIM_FIRE);
  FlowStep();
 }
 CHECK(BG_DryFiring(&flowPs) && !(flowPs.stats[STAT_UNCHAMBERED]&(1<<gun)));
 CHECK(base->clip==WANIM_IDLE && cg_returnTime==0);	/* the shot's pose eases home */
 for(t=0;t<3*BG_DRY_FIRE_TIME;t+=8) {
  FlowStep();
  CHECK(BG_DryFiring(&flowPs) && base->clip==WANIM_IDLE);
 }
 flow.cmd.buttons=0;
}
static void TestSprintReturnLikeExit(weapAnimDef_t *def,int weapon,const char *path) {
 static modelPose_t exitPoses[SPRINT_FRAMES];
 animLayer_t *move=&cg_weapLayers[LAYER_MOVE];
 playerState_t ps;
 modelPose_t before,after,last,home;
 int native,posture,quarter,frames,k,t,out=BG_WeaponSprintOutTime(weapon);
 float exitPeak,returnPeak,step;
 LoadAuthoredPoses(path);
 home=authored[def->clips[WANIM_SPRINT_IN].firstFrame];
 for(native=0;native<2;native++)for(posture=POSTURE_CARRY;posture<=POSTURE_PARTIAL;posture++)
 for(quarter=1;quarter<4;quarter++) {
  int expected;
  if(posture!=POSTURE_EXITING && posture!=POSTURE_PARTIAL && quarter>1) continue;
  nativePose=native;
  SprintPosture(&ps,weapon,posture,quarter);
  frames=SprintExitFrames(&ps,weapon,exitPoses);
  /* How long an ordinary stop's exit takes from this posture: the whole
   * sprint-out, or what is left of one already under way. */
  expected=posture==POSTURE_EXITING ? out-out*quarter/4 : out;
  CHECK(frames==(expected+7)/8+1);

  SprintPosture(&ps,weapon,posture,quarter);
  ps.weaponstate=WEAPON_DROPPING;ps.weaponTime=BG_WeaponDropTime(weapon);
  Picture(&ps,0,&before);CHECK(CG_WeapAnim_HoldsDrop());
  ps.pm_flags=0;ps.weaponstate=WEAPON_READY;ps.weaponTime=0;
  Picture(&ps,0,&after);
  /* Ownership passes to the return with the pose on screen; only a carry
   * loop drops its bob, as an ordinary stop in the loop does. */
  if(posture!=POSTURE_LOOP) CHECK(PoseDistance(&before,&after)<.001f);
  CHECK(move->fit==MOVE_FIT_RETURN && move->weight==1 && cg_weapLayers[LAYER_RETAINED].weight==0);
  CHECK(move->clip==(posture==POSTURE_CARRY || posture==POSTURE_PARTIAL ? WANIM_SPRINT_IN : WANIM_SPRINT_OUT));
  CHECK(fabs(move->returnLength-expected)<1);
  exitPeak=returnPeak=0;last=after;
  for(k=0;k<frames;k++) {
   float d;
   if(k) {
    Picture(&ps,8,&after);
    step=GunRotationDistance(&after,&last);if(step>returnPeak)returnPeak=step;
    step=GunRotationDistance(&exitPoses[k],&exitPoses[k-1]);if(step>exitPeak)exitPeak=step;
   }
   /* Still travelling until the exit itself would arrive, then home. */
   CHECK(ps.weaponstate==WEAPON_READY && ps.weaponTime==0);
   if(k*8+1<expected) CHECK(move->fit==MOVE_FIT_RETURN);
   else if(k*8>expected+1) CHECK(move->fit==MOVE_FIT_NONE);
   /* The gun's own progress home is the exit's; the whole rig is its pose
    * within the sampling of two clips of one authored curve (a reversal
    * runs back down the entry, which has its own frame count). */
   d=fabs(GunRotationDistance(&after,&home)-GunRotationDistance(&exitPoses[k],&home));
   if(d>=.005f || RigDistance(&after,&exitPoses[k])>=.15f)
    fprintf(stderr,"sprint return: gun %d native %d posture %d quarter %d frame %d off the exit by %.4f / %.4f\n",
     weapon,native,posture,quarter,k,d,RigDistance(&after,&exitPoses[k]));
   CHECK(d<.005f && RigDistance(&after,&exitPoses[k])<.15f);
   last=after;
  }
  /* Never faster than the exit, so no rush; a reversal never flicks home. */
  CHECK(returnPeak<=exitPeak*1.05f+.002f);
  /* The 40ms release holds the ready endpoint. */
  for(t=0;t<48;t+=8) {
   Picture(&ps,8,&after);
   CHECK(GunRotationDistance(&after,&home)<.02f); /* allow idle breathing */
  }
  CHECK(move->weight==0 && move->fit==MOVE_FIT_NONE);
 }
 nativePose=qfalse;
}
static void TestSprintReturnUnmoved(int weapon,const char *path) {
 playerState_t ps;
 modelPose_t before,after,home;
 int native,t;
 LoadAuthoredPoses(path);
 /* If both presses arrive before the entry visibly moves, releasing its
  * ownership must not start that entry while fading it out over ready.
  * There is nothing to retrace, so it is no whole-exit return either. */
 for(native=0;native<2;native++) {
  nativePose=native;Ready(&ps,weapon);
  ps.pm_flags=PMF_SPRINTING;ps.weaponstate=WEAPON_SPRINT_IN;
  ps.weaponTime=BG_WeaponSprintInTime(weapon);Picture(&ps,0,&home);
  ps.weaponstate=WEAPON_DROPPING;ps.weaponTime=BG_WeaponDropTime(weapon);
  Picture(&ps,0,&before);CHECK(CG_WeapAnim_HoldsDrop());
  ps.pm_flags=0;ps.weaponstate=WEAPON_READY;ps.weaponTime=0;
  Picture(&ps,0,&after);
  CHECK(cg_weapLayers[LAYER_MOVE].fit==MOVE_FIT_RETURN && cg_weapLayers[LAYER_MOVE].returnLength<=1);
  for(t=0;t<80;t+=8) {
   Picture(&ps,8,&after);
   CHECK(GunRotationDistance(&after,&home)<.015f);
  }
 }
 nativePose=qfalse;
}

/* A real floor and full Pmove keep landing in the sequence. A presentation
 * restart must depend on the picture still visible, never on old layer clocks
 * left behind by an airborne holster. */
static void FlowFloorTrace(trace_t *tr,const vec3_t start,const vec3_t mins,
 const vec3_t maxs,const vec3_t end,int pass,int mask) {
 float bottom=start[2]+(mins ? mins[2] : 0), endBottom=end[2]+(mins ? mins[2] : 0);
 (void)maxs;(void)pass;(void)mask;
 memset(tr,0,sizeof(*tr));tr->fraction=1;tr->entityNum=ENTITYNUM_NONE;
 VectorCopy(end,tr->endpos);
 if(bottom<-.01f) { tr->allsolid=tr->startsolid=qtrue;tr->fraction=0; }
 else if(endBottom<bottom && endBottom<=0) tr->fraction=bottom/(bottom-endBottom);
 if(tr->fraction<1) {
  int i;
  if(tr->fraction<0)tr->fraction=0;
  for(i=0;i<3;i++)tr->endpos[i]=start[i]+(end[i]-start[i])*tr->fraction;
  tr->plane.normal[2]=1;tr->entityNum=ENTITYNUM_WORLD;tr->contents=CONTENTS_SOLID;
 }
}
static int FlowFloorContents(const vec3_t point,int pass) {
 (void)pass;return point[2]<0 ? CONTENTS_SOLID : 0;
}
static void FlowWorldStep(qboolean render) {
 playerState_t before;
 flow.cmd.serverTime+=8;Pmove(&flow);cg.time+=8;
 if(render) {
  before=flowPs;CG_WeapAnim_BuildPose(&flowPs,1,8);
  CHECK(!memcmp(&before,&flowPs,sizeof(flowPs)));
 }
}
static void FlowWorldBegin(void) {
 int t;
 FlowBegin();FlowSlot(SLOT_SECONDARY);
 for(t=0;t<10000 && !(flowPs.weapon==WP_SHOTGUN && flowPs.weaponstate==WEAPON_READY &&
  cg_weapLayers[LAYER_MOVE].weight==0);t+=8)FlowStep();
 CHECK(flowPs.weapon==WP_SHOTGUN && flowPs.weaponstate==WEAPON_READY);
 flow.trace=FlowFloorTrace;flow.pointcontents=FlowFloorContents;flow.tracemask=MASK_PLAYERSOLID;
 flow.cmd.forwardmove=127;flowPs.commandTime=flow.cmd.serverTime;
 flowPs.speed=BG_PLAYER_SPEED;flowPs.gravity=800;flowPs.origin[2]=24;
 flowPs.groundEntityNum=ENTITYNUM_WORLD;FlowWorldStep(qtrue);
 CHECK(flowPs.groundEntityNum==ENTITYNUM_WORLD);
}
/* A sprint latches (MW2): letting go keeps it, and only a fresh press ends it.
 * Leaves the button up, so the next press starts a new sprint. */
static void FlowEndSprint(void) {
 flow.cmd.buttons=0;FlowWorldStep(qtrue);
 CHECK(flowPs.pm_flags&PMF_SPRINTING);
 flow.cmd.buttons=BUTTON_SPRINT;FlowWorldStep(qtrue);
 CHECK(!(flowPs.pm_flags&PMF_SPRINTING));
 flow.cmd.buttons=0;
}
static void TestSprintReentry(weapAnimDef_t *def) {
 animLayer_t *move=&cg_weapLayers[LAYER_MOVE],*sprint=&cg_weapLayers[LAYER_SPRINT];
 modelPose_t before,after;
 int native,t,ammo,settle;
 float last,len=CG_WeapAnim_LastFrameTime(def,WANIM_SPRINT_IN);
 LoadAuthoredPoses("assets/baseq3/models/weapons2/spas12/view.iqm");
 for(native=0;native<2;native++) {
  nativePose=native;FlowWorldBegin();ammo=flowPs.ammo[WP_SHOTGUN];
  flow.cmd.upmove=127;FlowWorldStep(qtrue);flow.cmd.upmove=0;
  CHECK(flowPs.groundEntityNum==ENTITYNUM_NONE);
  flow.cmd.buttons=BUTTON_SPRINT;
  for(t=0;t<BG_WeaponSprintInTime(WP_SHOTGUN)*3/4;t+=8)FlowWorldStep(qtrue);
  CHECK(flowPs.weaponstate==WEAPON_SPRINT_IN && flowPs.groundEntityNum==ENTITYNUM_NONE);
  FlowSlot(SLOT_PRIMARY);FlowWorldStep(qtrue);
  CHECK(flowPs.weaponstate==WEAPON_DROPPING && CG_WeapAnim_HoldsDrop());
  CHECK(flowPs.pm_flags&PMF_SPRINTING);	/* a swap does not end a sprint */
  FlowEndSprint();
  for(t=0;t<BG_WeaponDropTime(WP_SHOTGUN) && flowPs.groundEntityNum==ENTITYNUM_NONE;t+=8)
   FlowWorldStep(qtrue);
  CHECK(flowPs.groundEntityNum==ENTITYNUM_WORLD && flowPs.weaponstate==WEAPON_DROPPING);
  FlowSlot(SLOT_SECONDARY);FlowWorldStep(qtrue);
  CHECK(flowPs.weaponstate==WEAPON_READY && flowPs.weaponTime==0 && move->fit==MOVE_FIT_RETURN);
  for(t=0;t<BG_WeaponSprintOutTime(WP_SHOTGUN)+100;t+=8)FlowWorldStep(qtrue);
  CHECK(move->weight==0 && sprint->weight==0 && !cg_retainedActive);
  flow.cmd.buttons=BUTTON_SPRINT;FlowWorldStep(qtrue);Picture(&flowPs,0,&after);
  CHECK(flowPs.weaponstate==WEAPON_SPRINT_IN && flowPs.weaponTime==BG_WeaponSprintInTime(WP_SHOTGUN));
  CHECK(sprint->startFraction==0 && sprint->time==0);
  /* Idle breathes through its own loop; a new entry must start at its authored
   * ready pose rather than an arbitrary breathing frame or stale carry pose. */
  CHECK(PoseDistance(&after,&authored[def->clips[WANIM_SPRINT_IN].firstFrame])<.001f);
  for(t=0;t<BG_WeaponSprintInTime(WP_SHOTGUN)/2;t+=8) {
   last=sprint->time;FlowWorldStep(qtrue);
   CHECK(sprint->time>last && sprint->time-last<=len*8/BG_WeaponSprintInTime(WP_SHOTGUN)+.01f);
  }
  CHECK(flowPs.ammo[WP_SHOTGUN]==ammo);

  /* Re-request sprint while the cancelled entry is visibly returning. The
   * current return pose is the start, rather than either its old entry clock
   * or frame zero. The full gameplay entry lock is still stamped. */
  FlowSlot(SLOT_PRIMARY);FlowWorldStep(qtrue);FlowEndSprint();
  FlowSlot(SLOT_SECONDARY);FlowWorldStep(qtrue);
  CHECK(move->fit==MOVE_FIT_RETURN && move->clip==WANIM_SPRINT_IN);
  for(t=0;t<32;t+=8)FlowWorldStep(qtrue);
  CHECK(move->fit==MOVE_FIT_RETURN && move->weight==1);
  Picture(&flowPs,0,&before);last=move->time;
  flow.cmd.buttons=BUTTON_SPRINT;FlowWorldStep(qtrue);Picture(&flowPs,0,&after);
  CHECK(flowPs.weaponstate==WEAPON_SPRINT_IN && flowPs.weaponTime==BG_WeaponSprintInTime(WP_SHOTGUN));
  CHECK(fabs(sprint->time-last)<.01f && PoseDistance(&before,&after)<.001f);
  CHECK(sprint->startFraction>0 && sprint->startFraction<1);
  FlowWorldStep(qtrue);CHECK(sprint->time>last);

  /* One rendered entry frame still shows idle, but the next Y carries the
   * live transition forward before the second Y returns it. */
  FlowWorldBegin();flow.cmd.buttons=BUTTON_SPRINT;FlowWorldStep(qtrue);
  CHECK(sprint->clip==WANIM_SPRINT_IN && sprint->time==0);
  FlowSlot(SLOT_PRIMARY);FlowWorldStep(qtrue);
  CHECK(CG_WeapAnim_HoldsDrop() && cg_weapLayers[LAYER_RETAINED].clip==WANIM_SPRINT_IN);
  FlowEndSprint();FlowSlot(SLOT_SECONDARY);FlowWorldStep(qtrue);
  CHECK(flowPs.weaponstate==WEAPON_READY && move->fit==MOVE_FIT_RETURN && move->clip==WANIM_SPRINT_IN);

  /* The glide out of a full sprint: the carry loop was the source, and its
   * way home is its own exit from the carry, over the whole sprint-out time,
   * never a crossfade of the carry into idle. Gameplay is ready throughout. */
  for(settle=0;settle<2;settle++) {
   FlowWorldBegin();flow.cmd.buttons=BUTTON_SPRINT;
   for(t=0;t<BG_WeaponSprintInTime(WP_SHOTGUN)+32;t+=8)FlowWorldStep(qtrue);
   CHECK(flowPs.weaponstate==WEAPON_SPRINTING);
   FlowSlot(SLOT_PRIMARY);FlowWorldStep(qtrue);
   CHECK(CG_WeapAnim_HoldsDrop() && cg_weapLayers[LAYER_RETAINED].clip==WANIM_SPRINT_LOOP);
   FlowEndSprint();
   FlowSlot(SLOT_SECONDARY);FlowWorldStep(qtrue);
   CHECK(flowPs.weaponstate==WEAPON_READY && flowPs.weaponTime==0 && !cg_retainedActive);
   CHECK(cg_weapLayers[LAYER_RETAINED].weight==0);
   CHECK(move->fit==MOVE_FIT_RETURN && move->clip==WANIM_SPRINT_OUT && move->weight==1);
   CHECK(fabs(move->returnLength-BG_WeaponSprintOutTime(WP_SHOTGUN))<1);
   for(t=0;t<48;t+=8)FlowWorldStep(qtrue);
   CHECK(move->fit==MOVE_FIT_RETURN && move->weight==1);
   if(settle) {
    /* Left alone, it arrives with the exit and releases to idle. */
    for(t=56;t<BG_WeaponSprintOutTime(WP_SHOTGUN)+48;t+=8) {
     FlowWorldStep(qtrue);
     CHECK(flowPs.weaponstate==WEAPON_READY && flowPs.weaponTime==0);
     if(t+8<BG_WeaponSprintOutTime(WP_SHOTGUN)) CHECK(move->fit==MOVE_FIT_RETURN);
    }
    CHECK(move->fit==MOVE_FIT_NONE && move->weight==0);
    continue;
   }
   /* A restart part way home takes the exit's visible posture, neither the
    * full carry nor frame zero, and still stamps the full entry lock. */
   Picture(&flowPs,0,&before);
   flow.cmd.buttons=BUTTON_SPRINT;FlowWorldStep(qtrue);Picture(&flowPs,0,&after);
   CHECK(flowPs.weaponstate==WEAPON_SPRINT_IN && flowPs.weaponTime==BG_WeaponSprintInTime(WP_SHOTGUN));
   /* Exit and entry are separately sampled clips of one authored curve. */
   CHECK(GunRotationDistance(&before,&after)<.005f && RigDistance(&before,&after)<.15f);
   CHECK(sprint->startFraction>0 && sprint->startFraction<1);
  }

  /* YY with the sprint still on: MW2's double-tap cancel is in its change
   * check, after its sprint check (iw4x 0x44C4CA, 0x44C4D7), so the YY think
   * readies the gun, its picture returning along the entry it showed, and
   * the next think lowers it again from that visible pose with the whole
   * entry lock. */
  FlowWorldBegin();flow.cmd.buttons=BUTTON_SPRINT;
  for(t=0;t<BG_WeaponSprintInTime(WP_SHOTGUN)/2;t+=8)FlowWorldStep(qtrue);
  CHECK(flowPs.weaponstate==WEAPON_SPRINT_IN);
  FlowSlot(SLOT_PRIMARY);FlowWorldStep(qtrue);
  CHECK(flowPs.weaponstate==WEAPON_DROPPING && (flowPs.pm_flags&PMF_SPRINTING) && CG_WeapAnim_HoldsDrop());
  FlowSlot(SLOT_SECONDARY);FlowWorldStep(qtrue);
  CHECK(flowPs.weaponstate==WEAPON_READY && flowPs.weaponTime==0 && (flowPs.pm_flags&PMF_SPRINTING));
  CHECK(move->fit==MOVE_FIT_RETURN && move->clip==WANIM_SPRINT_IN && move->weight==1);
  Picture(&flowPs,0,&before);last=move->time;
  FlowWorldStep(qtrue);Picture(&flowPs,0,&after);
  CHECK(flowPs.weaponstate==WEAPON_SPRINT_IN && flowPs.weaponTime==BG_WeaponSprintInTime(WP_SHOTGUN));
  CHECK(fabs(sprint->time-last)<.01f && PoseDistance(&before,&after)<.001f);
  CHECK(sprint->startFraction>0 && sprint->startFraction<1);

  /* A sprint on again during the exit: MW2's sprint check does nothing while
   * sprinting in SPRINT_DROP (iw4x 0x5780F4-0x578107), so the exit plays to
   * its end, the gun is ready for a think, and the next think's entry starts
   * at its authored ready pose, where the exit left it, never reversing the
   * exit part way. */
  FlowWorldBegin();flow.cmd.buttons=BUTTON_SPRINT;
  for(t=0;t<BG_WeaponSprintInTime(WP_SHOTGUN)+32;t+=8)FlowWorldStep(qtrue);
  CHECK(flowPs.weaponstate==WEAPON_SPRINTING);
  FlowEndSprint();
  CHECK(flowPs.weaponstate==WEAPON_SPRINT_OUT);
  for(t=0;t<BG_WeaponSprintOutTime(WP_SHOTGUN)/2;t+=8)FlowWorldStep(qtrue);
  flow.cmd.buttons=BUTTON_SPRINT;FlowWorldStep(qtrue);
  CHECK((flowPs.pm_flags&PMF_SPRINTING) && flowPs.weaponstate==WEAPON_SPRINT_OUT && sprint->clip==WANIM_SPRINT_OUT);
  for(t=0;t<BG_WeaponSprintOutTime(WP_SHOTGUN) && flowPs.weaponstate==WEAPON_SPRINT_OUT;t+=8) {
   last=sprint->time;FlowWorldStep(qtrue);
   if(flowPs.weaponstate==WEAPON_SPRINT_OUT) CHECK(sprint->clip==WANIM_SPRINT_OUT && sprint->time>=last);
  }
  CHECK(flowPs.weaponstate==WEAPON_READY && (flowPs.pm_flags&PMF_SPRINTING));
  FlowWorldStep(qtrue);Picture(&flowPs,0,&after);
  CHECK(flowPs.weaponstate==WEAPON_SPRINT_IN && flowPs.weaponTime==BG_WeaponSprintInTime(WP_SHOTGUN));
  CHECK(sprint->startFraction==0 && sprint->time==0);
  CHECK(PoseDistance(&after,&authored[def->clips[WANIM_SPRINT_IN].firstFrame])<.001f);
 }
 nativePose=qfalse;
}
static void TestReloadPerk(weapAnimDef_t *def,int weapon) {
 playerState_t ps;int seq,perk,duration;
 for(seq=0;seq<RSEQ_COUNT;seq++)for(perk=0;perk<2;perk++) {
  Ready(&ps,weapon);ps.perks=perk ? 1<<(PERK_QUICK_RELOAD-1) : 0;
  duration=BG_ReloadSegmentTime(&ps,seq);if(!duration)continue;
  ps.weaponstate=WEAPON_RELOADING;ps.weaponAnimSeq=seq;ps.weaponAnimTime=duration/2;
  Update(def,&ps,16);
  CHECK(cg_weapLayers[LAYER_BASE].clip==WANIM_RELOAD_START+seq);
  CHECK(fabs(cg_weapLayers[LAYER_BASE].time-
    (float)(duration/2)/duration*CG_WeapAnim_ClipLength(def,WANIM_RELOAD_START+seq))<.01f);
  CHECK(!cg_retainedActive);
 }
}
static void TestPictureContinuity(weapAnimDef_t *def,int weapon,const char *path) {
 playerState_t ps;modelPose_t before,after,middle,destination;
 int native,held,fraction,t;
 LoadAuthoredPoses(path);
 for(native=0;native<2;native++) {
  nativePose=native;
  /* The authored pump endpoint is not idle. A real sampled pose, not just
   * a layer flag, must survive the exact simulation-ready boundary. */
  Ready(&ps,weapon);ps.weaponstate=WEAPON_BOLTING;ps.weaponAnimSeq=ASEQ_FIRE;
  ps.weaponAnimTime=BG_WeaponFireLength(weapon)-1;
  Picture(&ps,0,&before);
  ps.weaponstate=WEAPON_READY;ps.weaponAnimTime=-1;
  Picture(&ps,0,&after);CHECK(PoseDistance(&before,&after)<.001f);
  Picture(&ps,50,&middle);Picture(&ps,50,&destination);
  CHECK(cg_returnTime==RETURN_BLEND_MSEC);
  CHECK(PoseDistance(&middle,&destination)<=PoseDistance(&before,&destination)+.01f);

  /* A new shot cancels the pump's cosmetic release immediately. */
  Ready(&ps,weapon);ps.weaponstate=WEAPON_BOLTING;ps.weaponAnimSeq=ASEQ_FIRE;
  ps.weaponAnimTime=BG_WeaponFireLength(weapon)-1;Picture(&ps,16,&before);
  ps.weaponstate=WEAPON_READY;ps.weaponAnimTime=-1;Picture(&ps,16,&after);
  CHECK(cg_returnTime==0);
  ps.weaponstate=WEAPON_FIRING;ps.weaponAnimSeq=ASEQ_FIRE;ps.weaponAnimTime=0;
  Picture(&ps,16,&after);
  CHECK(cg_returnTime==RETURN_BLEND_MSEC);
  CHECK(PoseDistance(&after,&authored[def->clips[WANIM_FIRE].firstFrame])<.001f);

  Ready(&ps,weapon);Picture(&ps,16,&before);
  ps.weaponstate=WEAPON_FIRING;ps.weaponAnimSeq=ASEQ_FIRE;ps.weaponAnimTime=0;
  Picture(&ps,16,&after);
  CHECK(cg_returnTime==RETURN_BLEND_MSEC);
  CHECK(PoseDistance(&after,&authored[def->clips[WANIM_FIRE].firstFrame])<.001f);

  /* Reload-ending YY releases the observed pose too; it never acquires the
   * retained action layer that owns sprint/holster presentation. */
  Ready(&ps,weapon);ps.weaponstate=WEAPON_RELOADING;ps.weaponAnimSeq=RSEQ_END;
  ps.weaponAnimTime=BG_WeaponReloadSegLength(weapon,RSEQ_END)/2;
  Picture(&ps,16,&before);
  ps.weaponstate=WEAPON_DROPPING;ps.weaponTime=BG_WeaponDropTime(weapon);
  Picture(&ps,0,&after);CHECK(PoseDistance(&before,&after)<.001f);
  CHECK(!cg_retainedActive);
  ps.weaponTime-=16;Picture(&ps,16,&before);
  ps.weaponstate=WEAPON_READY;ps.weaponTime=0;
  Picture(&ps,0,&after);CHECK(PoseDistance(&before,&after)<.001f);

  for(held=0;held<2;held++)for(fraction=1;fraction<4;fraction++) {
   Ready(&ps,weapon);ps.weaponstate=WEAPON_RAISING;
   ps.weaponTime=BG_WeaponRaiseTime(weapon)*fraction/4;
   Picture(&ps,16,&before);
   ps.weaponstate=WEAPON_DROPPING;ps.weaponTime=BG_WeaponDropTime(weapon);
   Picture(&ps,0,&after);CHECK(PoseDistance(&before,&after)<.001f);
   CHECK(cg_weapLayers[LAYER_MOVE].startFraction>0);
   for(t=0;t<4;t++) { ps.weaponTime-=8;Picture(&ps,8,&before); }
   /* Reverse again while the first visual blend is still in flight. */
   ps.weaponstate=held ? WEAPON_FIRING : WEAPON_READY;
   ps.weaponAnimSeq=ASEQ_FIRE;ps.weaponAnimTime=0;ps.weaponTime=held ? BG_WeaponFireLength(weapon) : 0;
   Picture(&ps,0,&after);CHECK(PoseDistance(&before,&after)<.001f);
   for(t=0;t<100;t+=10) {
    if(held)ps.weaponAnimTime+=10;
    Picture(&ps,10,&after);
   }
   CHECK(cg_returnTime==RETURN_BLEND_MSEC);
   CHECK(ps.weaponstate==(held ? WEAPON_FIRING : WEAPON_READY));
  }
  /* A second stereo submission is the same picture, not another tick. */
  Ready(&ps,weapon);ps.weaponstate=WEAPON_BOLTING;ps.weaponAnimSeq=ASEQ_FIRE;
  ps.weaponAnimTime=BG_WeaponFireLength(weapon)-1;Picture(&ps,16,&before);
  ps.weaponstate=WEAPON_READY;Picture(&ps,16,&before);Picture(&ps,16,&before);
  {
   refEntity_t second;float time=cg_returnTime;
   memset(&second,0,sizeof(second));second.hModel=1;
   CHECK(CG_WeapAnim_Apply(&ps,&second,16));CHECK(cg_returnTime==time);
   if(second.pose)after=*second.pose;
   else trap_R_BlendModelPoses(&after,&authored[second.frame],1-second.backlerp,
    &authored[second.oldframe],second.backlerp);
   CHECK(PoseDistance(&before,&after)<.001f);
  }
  /* The gameplay shot/bolt distinction shares one continuous fire clip. */
  Ready(&ps,weapon);ps.weaponstate=WEAPON_FIRING;ps.weaponAnimSeq=ASEQ_FIRE;
  ps.weaponAnimTime=BG_WeaponFireLength(weapon)/3;Picture(&ps,0,&before);
  ps.weaponstate=WEAPON_BOLTING;Picture(&ps,0,&after);
  CHECK(PoseDistance(&before,&after)<.001f && cg_returnTime==RETURN_BLEND_MSEC);
  ps.weaponstate=WEAPON_READY;Picture(&ps,0,&after);CHECK(cg_returnTime==0);
  ps.clientNum++;Picture(&ps,0,&after);CHECK(cg_returnTime==RETURN_BLEND_MSEC);
 }
 nativePose=qfalse;
}
/* The MD3 hand path must announce a real weapon change too: a gun coming back
 * from melee/equipment cannot inherit its hidden sprint, fire or retained pose. */
static void TestActionHandoff(int gun) {
 const int actions[]={WP_KNIFE,WP_THROWING_KNIFE,WP_SEMTEX};
 const int states[]={WEAPON_FIRING,WEAPON_SPRINTING};
 playerState_t ps,before; refEntity_t entity; int i,j;
 CG_WeapAnim_RegisterClips(WP_KNIFE,2,"assets/baseq3/models/weapons2/throwingknife/melee.cfg");
 CG_WeapAnim_RegisterClips(WP_THROWING_KNIFE,2,"assets/baseq3/models/weapons2/throwingknife/throw.cfg");
 for(i=0;i<ARRAY_LEN(actions);i++)for(j=0;j<ARRAY_LEN(states);j++) {
  Ready(&ps,gun); ps.weaponstate=states[j];ps.weaponAnimSeq=ASEQ_FIRE;ps.weaponAnimTime=100;
  memset(&entity,0,sizeof(entity));entity.hModel=1;CG_WeapAnim_Apply(&ps,&entity,16);
  ps.weapon=actions[i];ps.weaponstate=WEAPON_RAISING;ps.weaponTime=BG_WeaponRaiseTime(ps.weapon);
  before=ps;CG_WeapAnim_ActionFrame(&ps,&entity);
  CHECK(!memcmp(&before,&ps,sizeof(ps)) && cg_weapAnimWeapon==ps.weapon);
  ps.weapon=gun;ps.weaponstate=WEAPON_RAISING;ps.weaponAction=WA_MELEE|WA_FIRED;
  ps.weaponTime=BG_WeaponDrawTime(&ps);cg.time+=1000;CG_WeapAnim_Apply(&ps,&entity,16);
  CHECK(!cg_retainedActive && cg_weapLayers[LAYER_SPRINT].weight==0);
  CHECK(entity.oldframe==bg_weapAnimDefs[gun].clips[WANIM_RAISE].firstFrame);
  CHECK(entity.backlerp==1 && cg_weapLayers[LAYER_MOVE].weight==1);
 }
}

/* The throwing knife is ready on its press (throwingknife_mp holdFireTime 0),
 * but its authored pull-out still plays, as picture only, from when the
 * picture first showed it, then holds; a throw takes the picture at once, and
 * the player state is never touched. */
static void TestKnifePullPicture(void) {
 playerState_t ps,before; refEntity_t entity; const weapAnimDef_t *def; int start,len,first,last;
 CG_WeapAnim_RegisterClips(WP_THROWING_KNIFE,2,"assets/baseq3/models/weapons2/throwingknife/throw.cfg");
 def=&bg_weapAnimDefs[WP_THROWING_KNIFE];
 first=def->clips[WANIM_RAISE].firstFrame; last=first+def->clips[WANIM_RAISE].numFrames-1;
 len=(int)CG_WeapAnim_LastFrameTime(def,WANIM_RAISE);
 CHECK(len>0 && BG_THROWING_KNIFE_PULL_MS==0);
 Ready(&ps,WP_SHOTGUN); cg.time=50000; memset(&entity,0,sizeof(entity)); entity.hModel=1;
 ps.weapon=WP_THROWING_KNIFE; ps.weaponstate=WEAPON_READY; ps.weaponTime=0;
 start=cg.time; before=ps; CG_WeapAnim_ActionFrame(&ps,&entity);
 CHECK(!memcmp(&before,&ps,sizeof(ps)) && entity.oldframe==first);
 cg.time=start+len/2; CG_WeapAnim_ActionFrame(&ps,&entity);
 CHECK(entity.oldframe>first && entity.oldframe<last);
 cg.time=start+len+1; CG_WeapAnim_ActionFrame(&ps,&entity);
 CHECK(entity.oldframe<first || entity.oldframe>last);	/* held */
 /* a throw on the next command cuts the pull-out */
 ps.weapon=WP_SHOTGUN; CG_WeapAnim_ActionFrame(&ps,&entity);
 ps.weapon=WP_THROWING_KNIFE; ps.weaponstate=WEAPON_READY; cg.time+=16; CG_WeapAnim_ActionFrame(&ps,&entity);
 CHECK(entity.oldframe==first);
 ps.weaponstate=WEAPON_FIRING; ps.weaponTime=BG_QUICK_ACTION_HOLD; cg.time+=16; before=ps;
 { qboolean drawn=CG_WeapAnim_ActionFrame(&ps,&entity);
   CHECK(!memcmp(&before,&ps,sizeof(ps)));
   /* the throw's own clip, or (none authored) the caller's picture: never the pull-out */
   CHECK(def->clips[WANIM_FIRE].numFrames>0 ? drawn && entity.oldframe==def->clips[WANIM_FIRE].firstFrame : !drawn); }
}

/* MW2's mode switch: its raise is a clip of its own, fitted to the alt raise's
 * lock like any draw, and the clip tables are the gameplay timers. A gun with
 * no such clip falls back to its ordinary raise. */
static void TestModeSwitchPicture(void) {
 static const struct { int weapon; const char *cfg; } modes[]={
  {WP_M16,"assets/baseq3/models/weapons2/m16/view.cfg"},
  {WP_MASTERKEY,"assets/baseq3/models/weapons2/m16/masterkey.cfg"}};
 playerState_t ps; refEntity_t sample; weapAnimDef_t *def; int i;
 for(i=0;i<ARRAY_LEN(modes);i++) {
  int gun=modes[i].weapon, lock=BG_WeaponAltRaiseTime(gun);
  const weapAnimClip_t *clip;
  CG_WeapAnim_RegisterClips(gun,3,modes[i].cfg);
  def=&bg_weapAnimDefs[gun]; clip=&def->clips[WANIM_ALT_RAISE];
  CHECK(lock>0 && fabs(CG_WeapAnim_ClipLength(def,WANIM_ALT_RAISE)-lock)<.5f);
  CHECK(fabs(CG_WeapAnim_ClipLength(def,WANIM_RAISE)-BG_WeaponRaiseTime(gun))<.5f);
  CHECK(fabs(CG_WeapAnim_ClipLength(def,WANIM_DROP)-BG_WeaponDropTime(gun))<.5f);
  if(BG_WeaponFireLength(gun)>0)
   CHECK(fabs(CG_WeapAnim_ClipLength(def,WANIM_FIRE)-BG_WeaponFireLength(gun))<.5f);
  Ready(&ps,gun);
  ps.weaponstate=WEAPON_RAISING; ps.weaponDraw=WDRAW_ALT; ps.weaponTime=lock;
  CHECK(BG_WeaponDrawTime(&ps)==lock);
  Update(def,&ps,1);
  CHECK(cg_weapLayers[LAYER_MOVE].clip==WANIM_ALT_RAISE && cg_weapLayers[LAYER_MOVE].time==0);
  CHECK(CG_WeapAnim_SampleFrame(def,&cg_weapLayers[LAYER_MOVE],&sample));
  CHECK(sample.oldframe==clip->firstFrame);
  ps.weaponTime=lock/2; Update(def,&ps,lock/2);
  CHECK(fabs(cg_weapLayers[LAYER_MOVE].time-
   CG_WeapAnim_ClipLength(def,WANIM_ALT_RAISE)*(1.0f-(float)(lock/2)/lock))<1);
  ps.weaponTime=0; Update(def,&ps,lock/2);
  CHECK(CG_WeapAnim_SampleFrame(def,&cg_weapLayers[LAYER_MOVE],&sample));
  CHECK(sample.frame==clip->firstFrame+clip->numFrames-1);
  /* cancelled, it comes home along its own clip like any draw */
  Ready(&ps,gun); ps.weaponstate=WEAPON_RAISING; ps.weaponDraw=WDRAW_ALT; ps.weaponTime=lock/2;
  Update(def,&ps,1);
  ps.weaponstate=WEAPON_READY; ps.weaponTime=0; Update(def,&ps,1);
  CHECK(cg_weapLayers[LAYER_MOVE].clip==WANIM_ALT_RAISE && cg_weapLayers[LAYER_MOVE].fit==MOVE_FIT_RETURN);
  /* an ordinary raise of the same gun is its own clip */
  Ready(&ps,gun); ps.weaponstate=WEAPON_RAISING; ps.weaponDraw=WDRAW_NORMAL;
  ps.weaponTime=BG_WeaponDrawTime(&ps);
  CHECK(ps.weaponTime==BG_WeaponRaiseTime(gun));
  Update(def,&ps,1); CHECK(cg_weapLayers[LAYER_MOVE].clip==WANIM_RAISE);
  CG_WeapAnim_RegisterClips(gun,0,NULL);
 }
 def=&bg_weapAnimDefs[WP_SHOTGUN];
 Ready(&ps,WP_SHOTGUN); ps.weaponstate=WEAPON_RAISING; ps.weaponDraw=WDRAW_ALT;
 Update(def,&ps,1); CHECK(cg_weapLayers[LAYER_MOVE].clip==WANIM_RAISE);
}

/* MW2's ladder with the trigger held, as the picture shows it, through the real
 * PM_Weapon at 8ms (the ladder's mark is PM_CheckLadder's, set and cleared here
 * as a grab and a jump leave it; ladder_test.c drives the real ladder and pins
 * the commands). The holster takes the gun down its drop clip and a further
 * lowering over its own time. Hung under the lock (weaponTime 1 on every
 * think) the picture stays at the drop's end, neither stopped part-way nor
 * back up, and the gun is out of view there (BG_LadderHolsterDown: MW2's drop
 * animation is started once and holds its last frame, 0x576149, 0x59B5E0).
 * Left with the trigger still down the gun is in hand at once, already firing
 * or being pumped: nothing was on screen to flow from, and MW2 starts that
 * animation with no blend. Left with the trigger let go it is READY at once
 * and only the picture comes home, up the drop it went down, the lowering
 * with it: YY flows, it never snaps. Out of a holster still on screen a shot
 * takes the picture from the pose shown, and a drop that runs on toward
 * another gun keeps its lowering to its end.
 * The ladder grabbed again while a cancelled holster is still coming home (the
 * two AGAIN ways): the new holster takes the gun down from the pose and the
 * lowering on screen, over its own time, never from the top; run out with the
 * trigger up it is WEAPON_LADDER, and the raise after it carries none of the
 * ladder's lowering, also when the ladder is left on the very next command. */
enum { LADDER_PIC_SHOT, LADDER_PIC_ACTION, LADDER_PIC_LET_GO, LADDER_PIC_SHOT_INSIDE, LADDER_PIC_UP_INSIDE,
 LADDER_PIC_OTHER_GUN, LADDER_PIC_LET_GO_AGAIN, LADDER_PIC_UP_AGAIN, LADDER_PIC_WAYS };
static void TestLadderHeldPicture(weapAnimDef_t *def,int gun) {
 animLayer_t *base=&cg_weapLayers[LAYER_BASE],*move=&cg_weapLayers[LAYER_MOVE];
 float drop=CG_WeapAnim_ClipLength(def,WANIM_DROP),fire=CG_WeapAnim_ClipLength(def,WANIM_FIRE);
 float last,lower,from,step,held;
 int open=PickupNoteTime(gun,ASEQ_FIRE,WNOTE_BOLT_OPEN),way,t,n;
 CHECK(drop>0 && fire>0 && open>0 && BG_WeaponFireType(gun)==BG_FIRE_SINGLESHOT);
 for(way=0;way<LADDER_PIC_WAYS;way++) {
  qboolean again=way==LADDER_PIC_LET_GO_AGAIN || way==LADDER_PIC_UP_AGAIN;
  qboolean hung=way==LADDER_PIC_SHOT || way==LADDER_PIC_ACTION || way==LADDER_PIC_LET_GO ||
   way==LADDER_PIC_LET_GO_AGAIN;
  FlowBeginWith(gun); FlowSlot(SLOT_SECONDARY);
  for(t=0;t<10000 && !(flowPs.weapon==gun && flowPs.weaponstate==WEAPON_READY && move->weight==0);t+=8) FlowStep();
  CHECK(flowPs.weapon==gun && flowPs.weaponstate==WEAPON_READY && CG_WeapAnim_LadderLower()==0);
  if(way!=LADDER_PIC_UP_INSIDE && way!=LADDER_PIC_OTHER_GUN && way!=LADDER_PIC_UP_AGAIN) {
   flow.cmd.buttons=BUTTON_ATTACK; FlowStep();
   CHECK(flowPs.weaponstate==WEAPON_FIRING && flowPs.weaponShotCount==1 && base->clip==WANIM_FIRE);
   if(way==LADDER_PIC_ACTION) {
    /* grabbed in the shot: the gun stays up and firing until the shot's last
     * think, where the holster takes the lock's READY and the action is owed */
    flowPs.stats[STAT_LADDER]|=LADDER_ATTACHED;
    for(t=0;t<10000 && flowPs.weaponstate==WEAPON_FIRING;t+=8) {
     CHECK(base->clip==WANIM_FIRE && move->weight==0 && CG_WeapAnim_LadderLower()==0 && !BG_LadderHolsterDown(&flowPs));
     FlowStep();
    }
    CHECK(flowPs.stats[STAT_UNCHAMBERED]&(1<<gun));
   } else {
    for(t=0;t<10000 && !(flowPs.weaponstate==WEAPON_READY && flowPs.weaponTime==1 &&
     !(flowPs.stats[STAT_UNCHAMBERED]&(1<<gun)));t+=8) FlowStep();
    for(t=0;t<200;t+=8) FlowStep();	/* the action's release has left the picture */
    CHECK(flowPs.weaponstate==WEAPON_READY && flowPs.weaponTime==1 && CG_WeapAnim_ReturnWeight()==0);
   }
  }
  if(way!=LADDER_PIC_ACTION) { flowPs.stats[STAT_LADDER]|=LADDER_ATTACHED; FlowStep(); }
  CHECK(flowPs.weapon==gun && flowPs.weaponstate==WEAPON_DROPPING && (flowPs.stats[STAT_LADDER]&LADDER_HOLSTER));
  CHECK(flowPs.weaponTime==BG_LADDER_DROP_TIME && BG_HolsterTime(&flowPs)==BG_LADDER_DROP_TIME);
  CHECK(move->clip==WANIM_DROP && move->fit==MOVE_FIT_FORWARD && move->targetWeight==1);
  CHECK(CG_WeapAnim_LadderLower()==0 && !BG_LadderHolsterDown(&flowPs) && !CG_WeapAnim_HoldsDrop());
  /* down over the holster's own time: the clip and the lowering together */
  last=move->time; lower=0;
  while(flowPs.weaponTime>(hung ? 1 : BG_LADDER_DROP_TIME/2)) {
   FlowStep();
   CHECK(flowPs.weaponstate==WEAPON_DROPPING && (flowPs.stats[STAT_LADDER]&LADDER_HOLSTER) && move->clip==WANIM_DROP);
   CHECK(move->time>=last-.01f && CG_WeapAnim_LadderLower()>=lower);
   CHECK(fabs(CG_WeapAnim_LadderLower()-(1.0f-(float)flowPs.weaponTime/BG_LADDER_DROP_TIME))<.001f);
   CHECK(fabs(move->time-drop*(1.0f-(float)flowPs.weaponTime/BG_LADDER_DROP_TIME))<.01f);
   last=move->time; lower=CG_WeapAnim_LadderLower();
  }
  CHECK(move->weight==1 && lower>.4f);
  if(hung) {
   /* the lock's 1, every think, for as long as the trigger is held: stowed */
   CHECK((flow.cmd.buttons&BUTTON_ATTACK) && flowPs.weaponShotCount==1);
   CHECK(lower>=.99f && last>=drop*.98f);
   for(t=0;t<3000;t+=8) {
    FlowStep();
    CHECK(flowPs.weapon==gun && flowPs.weaponstate==WEAPON_DROPPING && flowPs.weaponTime==1);
    CHECK(BG_LadderHolsterDown(&flowPs) && cg_ladderStowed);
    CHECK(move->clip==WANIM_DROP && move->fit==MOVE_FIT_FORWARD && move->weight==1 && move->targetWeight==1);
    CHECK(fabs(move->time-last)<.01f && CG_WeapAnim_LadderLower()==lower);
   }
  }
  flowPs.stats[STAT_LADDER]&=~LADDER_ATTACHED;	/* the jump: PM_LadderMove's, ahead of PM_Weapon */
  switch(way) {
  case LADDER_PIC_SHOT:
   FlowStep();
   CHECK(flowPs.weaponstate==WEAPON_FIRING && flowPs.weaponShotCount==2 && flowPs.weaponAnimTime==0);
   CHECK(base->clip==WANIM_FIRE && base->weight==1 && base->time==0 && CG_WeapAnim_ReturnWeight()==0);
   CHECK(move->weight==0 && cg_weapLayers[LAYER_RETAINED].weight==0 && CG_WeapAnim_LadderLower()==0);
   FlowStep();
   CHECK(move->weight==0 && CG_WeapAnim_LadderLower()==0 && base->clip==WANIM_FIRE);
   break;
  case LADDER_PIC_ACTION:
   FlowStep();
   CHECK(flowPs.weaponstate==WEAPON_BOLTING && flowPs.weaponAnimTime==open && flowPs.weaponShotCount==1);
   CHECK(base->clip==WANIM_FIRE && base->weight==1 && CG_WeapAnim_ReturnWeight()==0);
   CHECK(fabs(base->time-(float)open/BG_WeaponFireLength(gun)*fire)<.01f);
   CHECK(move->weight==0 && cg_weapLayers[LAYER_RETAINED].weight==0 && CG_WeapAnim_LadderLower()==0);
   break;
  case LADDER_PIC_LET_GO:
  case LADDER_PIC_UP_INSIDE:
  case LADDER_PIC_LET_GO_AGAIN:
  case LADDER_PIC_UP_AGAIN:
   flow.cmd.buttons=0; FlowStep();
   CHECK(flowPs.weaponstate==WEAPON_READY && flowPs.weaponTime==0 && flowPs.weaponShotCount==0);
   CHECK(!(flowPs.stats[STAT_LADDER]&LADDER_HOLSTER) && !BG_LadderHolsterDown(&flowPs));
   CHECK(move->clip==WANIM_DROP && move->fit==MOVE_FIT_RETURN && move->weight==1 && move->returnTo==0);
   from=move->returnFrom; CHECK(fabs(from-last)<.01f && from>0 && move->returnLength>0);
   /* no faster than the drop clip itself comes home (smoothstep's 1.5x peak) */
   step=lower*8*1.5f/move->returnLength+.001f;
   CHECK(CG_WeapAnim_LadderLower()>=lower-step && CG_WeapAnim_LadderLower()<=lower);
   for(held=8,n=0;move->targetWeight>0 && held<=MOVE_RETURN_MS+16 && !(again && n==3);held+=8,n++) {
    CHECK(fabs(CG_WeapAnim_LadderLower()-lower*move->time/from)<.001f);
    last=CG_WeapAnim_LadderLower(); FlowStep();
    CHECK(flowPs.weaponstate==WEAPON_READY && move->clip==WANIM_DROP);
    CHECK(CG_WeapAnim_LadderLower()<=last && last-CG_WeapAnim_LadderLower()<=step);
   }
   if(again) {
    /* the ladder grabbed again inside the return: the new holster's lowering
     * and its clip start from what is on screen and go down from there, no
     * frame moving more than the holster's own step */
    float shown=CG_WeapAnim_LadderLower(),at=move->time,down=(1.0f-shown)*8/BG_LADDER_DROP_TIME+.001f;
    CHECK(n==3 && shown>0 && shown<lower && at>0 && move->fit==MOVE_FIT_RETURN && move->targetWeight>0);
    flowPs.stats[STAT_LADDER]|=LADDER_ATTACHED; FlowStep();
    CHECK(flowPs.weaponstate==WEAPON_DROPPING && (flowPs.stats[STAT_LADDER]&LADDER_HOLSTER));
    CHECK(flowPs.weaponTime==BG_LADDER_DROP_TIME && !BG_LadderHolsterDown(&flowPs));
    CHECK(move->clip==WANIM_DROP && move->fit==MOVE_FIT_FORWARD && move->weight==1 && fabs(move->time-at)<.01f);
    CHECK(CG_WeapAnim_LadderLower()==shown);
    for(n=0,last=shown;;n++) {
     FlowStep();
     if(flowPs.weaponstate!=WEAPON_DROPPING) break;
     CHECK((flowPs.stats[STAT_LADDER]&LADDER_HOLSTER) && !BG_LadderHolsterDown(&flowPs) && !cg_ladderStowed);
     CHECK(CG_WeapAnim_LadderLower()>=last && CG_WeapAnim_LadderLower()-last<=down && move->time>=at-.01f);
     CHECK(fabs(CG_WeapAnim_LadderLower()-(shown+(1.0f-shown)*(1.0f-(float)flowPs.weaponTime/BG_LADDER_DROP_TIME)))<.001f);
     last=CG_WeapAnim_LadderLower(); at=move->time;
    }
    CHECK(n>=8 && last>.9f && at>drop*.9f);
    /* run out with the trigger up: empty hands. The raise is its own clip and
     * takes nothing of the ladder's lowering with it, though the ladder is left
     * on the next command */
    CHECK(flowPs.weaponstate==WEAPON_LADDER && flowPs.weaponShotCount==0 && CG_WeapAnim_LadderLower()==0);
    flowPs.stats[STAT_LADDER]&=~LADDER_ATTACHED; FlowStep();
    CHECK(flowPs.weapon==gun && flowPs.weaponstate==WEAPON_RAISING && flowPs.weaponTime==BG_WeaponDrawTime(&flowPs));
    CHECK(move->clip==CG_WeapAnim_DrawClip(def,&flowPs) && CG_WeapAnim_LadderLower()==0);
    for(t=0;t<64;t+=8) {
     FlowStep();
     CHECK(flowPs.weaponstate==WEAPON_RAISING && move->fit==MOVE_FIT_FORWARD && CG_WeapAnim_LadderLower()==0);
    }
    break;
   }
   CHECK(move->targetWeight==0 && CG_WeapAnim_LadderLower()==0 && fabs(move->time)<.01f && n>=4);
   break;
  case LADDER_PIC_SHOT_INSIDE:
   FlowStep();
   CHECK(flowPs.weaponstate==WEAPON_FIRING && flowPs.weaponShotCount==2 && base->clip==WANIM_FIRE);
   /* the holster was on screen: the shot takes the picture from that pose */
   CHECK(move->targetWeight==0 && move->weight>.5f && move->weight<1);
   CHECK(CG_WeapAnim_LadderLower()>0 && CG_WeapAnim_LadderLower()<lower);
   for(t=0;t<48;t+=8) {
    last=CG_WeapAnim_LadderLower(); held=move->weight; FlowStep();
    CHECK(CG_WeapAnim_LadderLower()<=last && move->weight<=held);
   }
   CHECK(move->weight==0 && CG_WeapAnim_LadderLower()==0);
   break;
  default:
   /* Y on the leaving command: the drop is no longer the ladder's and runs out
    * the time it had toward the other gun, its picture and its lowering kept */
   FlowSlot(SLOT_PRIMARY); FlowStep();
   CHECK(flowPs.weapon==gun && flowPs.weaponstate==WEAPON_DROPPING && !(flowPs.stats[STAT_LADDER]&LADDER_HOLSTER));
   CHECK(flowPs.weaponTime>0 && BG_HolsterTime(&flowPs)==BG_WeaponDropTime(gun));
   for(n=0;flowPs.weapon==gun && n<100;n++) {
    CHECK(flowPs.weaponstate==WEAPON_DROPPING && move->clip==WANIM_DROP && move->weight==1);
    CHECK(move->time>=last-.01f && CG_WeapAnim_LadderLower()==lower);
    last=move->time; FlowStep();
   }
   CHECK(n>=2 && flowPs.weapon==WP_MACHINEGUN && flowPs.weaponstate==WEAPON_RAISING && CG_WeapAnim_LadderLower()==0);
   break;
  }
  flow.cmd.buttons=0;
 }
 /* A camera cut (CG_WeapAnim_Reset) drops the ladder's picture with the rest:
  * out of a hung holster, the lowering and the stowed flag are gone at once. */
 FlowBeginWith(gun); FlowSlot(SLOT_SECONDARY);
 for(t=0;t<10000 && !(flowPs.weapon==gun && flowPs.weaponstate==WEAPON_READY && move->weight==0);t+=8) FlowStep();
 flow.cmd.buttons=BUTTON_ATTACK;
 for(t=0;t<10000 && !(flowPs.weaponstate==WEAPON_READY && flowPs.weaponTime==1 &&
  !(flowPs.stats[STAT_UNCHAMBERED]&(1<<gun)));t+=8) FlowStep();
 flowPs.stats[STAT_LADDER]|=LADDER_ATTACHED;
 for(t=0;t<400;t+=8) FlowStep();
 CHECK(flowPs.weaponstate==WEAPON_DROPPING && flowPs.weaponTime==1 && BG_LadderHolsterDown(&flowPs));
 CHECK(CG_WeapAnim_LadderLower()>=.99f && cg_ladderLowerFrom>=.99f && cg_ladderStowed && cg_ladderHolster);
 CHECK(CG_WeapAnim_LadderStowed(&flowPs));
 CG_WeapAnim_Reset();
 CHECK(CG_WeapAnim_LadderLower()==0 && cg_ladderLowerFrom==0 && cg_ladderLowerStart==0);
 CHECK(!cg_ladderStowed && !cg_ladderHolster);
 flow.cmd.buttons=0;
}

/* The G19's select-fire switch is worked as a bolt is (owner's rule, "Switch
 * NAC"), and the picture draws it as it draws a pump or a bolt: its clip is
 * the action's clip. Side by side, the SPAS's pump and the G19's switch,
 * through the real PM_Weapon at 8, 16 and 33ms, one rendered frame a command,
 * from the shipped poses. A swap inside the action releases the action's clip
 * from the pose on screen into the drop; a sprint begun inside it carries the
 * action's clip, which plays on at the action's own pace under no sprint
 * posture and fades over the entry after its end; a sprint and then a swap
 * (the sprint swap) leave the action's clip the picture through the whole
 * holster, the drop hidden; YY inside it eases home. Wherever the state
 * changes, the frame drawn with no time passed is the frame on screen:
 * nothing snaps. Begun while a sprint YY's cancelled drop still eases home,
 * the action takes the picture over at the move layer's crossfade, as the
 * SPAS's shot does. And once the gun is at rest, the idle is drawn: after an
 * action played out, after YY lost it, and on the next gun after a swap,
 * though the state still names the action's last segment (weaponAnimSeq). */
enum { SWITCH_PIC_SWAP, SWITCH_PIC_SPRINT, SWITCH_PIC_SPRINT_SWAP, SWITCH_PIC_YY, SWITCH_PIC_FINISH, SWITCH_PIC_WAYS };
static const struct { int gun, next; const char *model; } switchGuns[]={
 {WP_SHOTGUN,WP_G19,"assets/baseq3/models/weapons2/spas12/view.iqm"},
 {WP_G19,WP_SHOTGUN,"assets/baseq3/models/weapons2/g19/view.iqm"}};
static modelPose_t switchShown;
static int switchState, switchWeapon, switchFrames, switchMsec;
static int SwitchFrames(int ms) { return (ms+switchMsec-1)/switchMsec; }
static const char *SwitchModel(int gun) {
 int g;
 for(g=0;g<ARRAY_LEN(switchGuns);g++) if(switchGuns[g].gun==gun) return switchGuns[g].model;
 CHECK(0); return NULL;
}
/* The gun in slot 0 at rest, the other one of the two in slot 1. */
static void SwitchPictureBegin(int gun,int next) {
 bg_class_t cl=*BG_Class(0);
 int t;
 memset(&flowPs,0,sizeof(flowPs)); memset(&flow,0,sizeof(flow));
 cl.slot[0]=gun; cl.slot[1]=next;
 cl.attachments[0]=gun==WP_G19 ? BG_ATTACHMENT_SWITCH : 0;
 cl.attachments[1]=next==WP_G19 ? BG_ATTACHMENT_SWITCH : 0;
 CHECK(BG_ValidateClass(&cl));
 BG_ApplyClass(&flowPs,&cl);
 flowPs.slotFlags[0]&=~BG_GUN_FIRST_DRAW; flowPs.slotFlags[1]&=~BG_GUN_FIRST_DRAW;
 flowPs.stats[STAT_HEALTH]=100; flowPs.pm_type=PM_NORMAL;
 BG_SelectSlot(&flowPs,0);
 flowPs.weapon=gun; flowPs.weaponstate=WEAPON_READY; flowPs.weaponAnimTime=-1;
 flow.ps=&flowPs; flow.trace=FlowTrace; flow.cmd.weapon=BG_SLOT_COMMAND+SLOT_PRIMARY;
 LoadAuthoredPoses(SwitchModel(gun));
 CG_WeapAnim_Reset();
 for(t=0;t<80;t++) Picture(&flowPs,8,&switchShown);
 switchState=flowPs.weaponstate; switchWeapon=flowPs.weapon; switchFrames=0;
 CHECK(BG_HasSelector(&flowPs,gun)==(gun==WP_G19) && cg_weapLayers[LAYER_MOVE].weight==0);
}
/* One command, then the frame. Where the action begins out of READY, or the
 * action or its drop ends on the gun in hand (into the drop, READY or back
 * from a drop), the frame is drawn first with no time passed, and must be the
 * frame already on screen. (A shot takes the picture over at once, by design:
 * it is not checked.) The next gun is drawn from its own model. */
static void SwitchPictureStep(int buttons) {
 modelPose_t now;
 flow.cmd.buttons=buttons; flow.cmd.serverTime+=switchMsec; PickupWeaponStep(&flow,switchMsec); switchFrames++;
 if(flowPs.weapon!=switchWeapon) {
  LoadAuthoredPoses(SwitchModel(flowPs.weapon));
  switchWeapon=flowPs.weapon;
 } else if(flowPs.weaponstate!=switchState && (((switchState==WEAPON_BOLTING || switchState==WEAPON_DROPPING) &&
  (flowPs.weaponstate==WEAPON_DROPPING || flowPs.weaponstate==WEAPON_READY)) ||
  (switchState==WEAPON_READY && flowPs.weaponstate==WEAPON_BOLTING))) {
  Picture(&flowPs,0,&now);
  CHECK(PoseDistance(&switchShown,&now)<.001f);
 }
 Picture(&flowPs,switchMsec,&switchShown);
 switchState=flowPs.weaponstate;
}
/* At rest: READY and nothing pressed for a while, the idle alone drawn. */
static void SwitchPictureAtRest(void) {
 int n,l;
 for(n=0;n<SwitchFrames(10000) && flowPs.weaponstate!=WEAPON_READY;n++) SwitchPictureStep(0);
 for(n=0;n<SwitchFrames(1000);n++) SwitchPictureStep(0);
 CHECK(flowPs.weaponstate==WEAPON_READY);
 CHECK(cg_weapLayers[LAYER_BASE].clip==WANIM_IDLE && cg_weapLayers[LAYER_BASE].weight==1);
 for(l=LAYER_MOVE;l<ANIM_LAYER_COUNT;l++) CHECK(cg_weapLayers[l].weight==0);
}
/* The action begun d commands into the ease home of a sprint YY's drop (a
 * sprint, Y, then Y back with the sprint let go): the cancelled holster's
 * picture, handed to the move layer to come home, fades out under the action
 * at the move layer's crossfade (MOVE_BLEND_SPEED), never dropped. In the
 * pose: no frame moves further than the action's own frames (own) and the
 * ease's own frames (ease) do, plus that crossfade's share of the distance
 * between the two pictures being crossed (slerp's at most pi/2 on a rotation
 * component). Returns the largest frame-to-frame move. */
static float SwitchPictureEase(int gun,int d,int action,float own,float ease) {
 animLayer_t *base=&cg_weapLayers[LAYER_BASE],*move=&cg_weapLayers[LAYER_MOVE];
 const weapAnimDef_t *def=&bg_weapAnimDefs[gun];
 modelPose_t before,under,over;
 int n,next=gun==WP_G19 ? WP_SHOTGUN : WP_G19;
 float w,jump,gap,worst=0;
 SwitchPictureBegin(gun,next);
 flowPs.pm_flags|=PMF_SPRINTING;
 for(n=0;n<SwitchFrames(200);n++) SwitchPictureStep(0);
 CHECK(flowPs.weaponstate==WEAPON_SPRINT_IN || flowPs.weaponstate==WEAPON_SPRINTING);
 FlowSlot(SLOT_SECONDARY); SwitchPictureStep(0);
 CHECK(flowPs.weaponstate==WEAPON_DROPPING && cg_retainedActive && cg_retainedDrop);
 flowPs.pm_flags&=~PMF_SPRINTING;
 FlowSlot(SLOT_PRIMARY); SwitchPictureStep(0);
 CHECK(flowPs.weapon==gun && flowPs.weaponstate==WEAPON_READY && move->fit==MOVE_FIT_RETURN && move->weight>0);
 for(n=1;n<d;n++) SwitchPictureStep(0);
 CHECK(flowPs.weaponstate==WEAPON_READY && move->weight>0);
 for(n=0;n<=SwitchFrames((int)MOVE_RETURN_MS);n++) {
  w=move->weight; before=switchShown; gap=0;
  if(w>0) {
   CHECK(CG_WeapAnim_SampleLayer(1,def,base,&under) && CG_WeapAnim_SampleLayer(1,def,move,&over));
   gap=PoseDistance(&under,&over);
  }
  SwitchPictureStep(!n && action ? (gun==WP_G19 ? BUTTON_SELECTFIRE : BUTTON_ATTACK) : 0);
  if(!n && action) CHECK(flowPs.weaponstate==(gun==WP_G19 ? WEAPON_BOLTING : WEAPON_FIRING));
  CHECK(move->weight>=w-MOVE_BLEND_SPEED*switchMsec-.0001f);
  jump=PoseDistance(&before,&switchShown); if(jump>worst) worst=jump;
  if(action) CHECK(jump<=own+ease+MOVE_BLEND_SPEED*switchMsec*gap*1.6f+.001f);
 }
 return worst;
}
/* The largest frame-to-frame move of the action begun at rest, over the same
 * stretch the ease is checked for. */
static float SwitchPictureOwn(int gun,int next) {
 modelPose_t before; float jump,own=0; int n;
 SwitchPictureBegin(gun,next);
 for(n=0;n<=SwitchFrames((int)MOVE_RETURN_MS);n++) {
  before=switchShown;
  SwitchPictureStep(!n ? (gun==WP_G19 ? BUTTON_SELECTFIRE : BUTTON_ATTACK) : 0);
  jump=PoseDistance(&before,&switchShown); if(jump>own) own=jump;
 }
 return own;
}
static void TestSwitchPicture(void) {
 static const int rates[]={8,16,33};
 animLayer_t *base=&cg_weapLayers[LAYER_BASE],*move=&cg_weapLayers[LAYER_MOVE];
 animLayer_t *sprint=&cg_weapLayers[LAYER_SPRINT],*retained=&cg_weapLayers[LAYER_RETAINED];
 int r,g,way,n,start,close,end,clip,dropped,d;
 playerState_t pic;
 CG_WeapAnim_RegisterClips(WP_G19,4,"assets/baseq3/models/weapons2/g19/view.cfg");
 /* the switch's clips are its segment's length (BG_ActionLength), as the fire clip is the fire table's */
 Ready(&pic,WP_G19); pic.weaponAnimSeq=ASEQ_SELECT_AUTO;
 CHECK(BG_ActionLength(&pic)>0 && fabs(CG_WeapAnim_ClipLength(&bg_weapAnimDefs[WP_G19],WANIM_SELECT_AUTO)-BG_ActionLength(&pic))<.5f);
 pic.weaponAnimSeq=ASEQ_SELECT_SEMI;
 CHECK(fabs(CG_WeapAnim_ClipLength(&bg_weapAnimDefs[WP_G19],WANIM_SELECT_SEMI)-BG_ActionLength(&pic))<.5f);
 nativePose=qtrue;
 for(r=0;r<ARRAY_LEN(rates);r++) for(g=0;g<ARRAY_LEN(switchGuns);g++) for(way=0;way<SWITCH_PIC_WAYS;way++) {
  int gun=switchGuns[g].gun,next=switchGuns[g].next;
  switchMsec=rates[r];
  SwitchPictureBegin(gun,next);
  /* the action begins on command 0; its first command, its close note's and its end */
  SwitchPictureStep(gun==WP_G19 ? BUTTON_SELECTFIRE : BUTTON_ATTACK);
  for(n=0;n<10000 && flowPs.weaponstate!=WEAPON_BOLTING;n++) SwitchPictureStep(0);
  CHECK(flowPs.weaponstate==WEAPON_BOLTING);
  clip=base->clip; start=switchFrames;
  CHECK(clip==(gun==WP_G19 ? WANIM_SELECT_AUTO : WANIM_FIRE) && base->weight==1);
  close=start+SwitchFrames(PickupNoteTime(gun,flowPs.weaponAnimSeq,WNOTE_BOLT_CLOSED)-flowPs.weaponAnimTime);
  end=start+SwitchFrames(BG_ActionLength(&flowPs)-flowPs.weaponAnimTime);
  CHECK(close>start+SwitchFrames(64)+1 && end>close);
  if(way==SWITCH_PIC_FINISH) {
   /* played out: at rest the idle, the switch's segment still on the state */
   SwitchPictureAtRest();
   if(gun==WP_G19) CHECK(BG_IS_SELECT_SEQ(flowPs.weaponAnimSeq) && (flowPs.slotFlags[0]&BG_GUN_AUTO));
   continue;
  }
  /* a few commands in */
  while(switchFrames<start+SwitchFrames(32)) SwitchPictureStep(0);
  if(way==SWITCH_PIC_SPRINT || way==SWITCH_PIC_SPRINT_SWAP) {
   flowPs.pm_flags|=PMF_SPRINTING;
   SwitchPictureStep(0);
   CHECK(flowPs.weaponstate==WEAPON_BOLTING && cg_retainedActive && retained->clip==clip);
   while(switchFrames<start+SwitchFrames(64)) SwitchPictureStep(0);
  }
  if(way==SWITCH_PIC_SPRINT) {
   /* the action plays to its end, carried, under no sprint posture */
   for(n=0;n<10000 && flowPs.weaponstate==WEAPON_BOLTING;n++) {
    CHECK(cg_retainedActive && retained->clip==clip && fabs(retained->time-base->time)<.5f);
    CHECK(sprint->weight==0);
    SwitchPictureStep(0);
   }
   CHECK(switchFrames==end && flowPs.weaponstate==WEAPON_READY && cg_retainedActive && retained->clip==clip);
   SwitchPictureStep(0);
   CHECK(flowPs.weaponstate==WEAPON_SPRINT_IN && !cg_retainedActive && retained->targetWeight==0);
   CHECK(retained->clip==clip && retained->weight>0 && sprint->clip==WANIM_SPRINT_IN);
   for(n=0;n<SwitchFrames(BG_WeaponSprintInTime(gun));n++) SwitchPictureStep(0);
   CHECK(retained->weight==0);
   continue;
  }
  /* Y, before the close note */
  FlowSlot(SLOT_SECONDARY); SwitchPictureStep(0);
  CHECK(switchFrames<close && flowPs.weaponstate==WEAPON_DROPPING && flowPs.weaponTime==BG_WeaponDropTime(gun));
  if(way==SWITCH_PIC_SPRINT_SWAP) {
   /* the action's clip is the picture through the whole holster, the drop hidden */
   for(dropped=0;dropped<10000 && flowPs.weapon==gun;dropped++) {
    CHECK(flowPs.weaponstate==WEAPON_DROPPING && cg_retainedActive && cg_retainedDrop);
    CHECK(retained->clip==clip && retained->weight==1 && move->weight==0 && sprint->weight==0);
    SwitchPictureStep(0);
   }
   CHECK(dropped==SwitchFrames(BG_WeaponDropTime(gun)) && flowPs.weapon==next);
   continue;
  }
  /* released from the action's pose, the drop fading in under it */
  CHECK(!cg_retainedActive && CG_WeapAnim_ReturnWeight()>0 && move->clip==WANIM_DROP);
  if(way==SWITCH_PIC_SWAP) {
   for(n=0;n<10000 && flowPs.weapon==gun;n++) SwitchPictureStep(0);
   CHECK(flowPs.weapon==next);
   /* the next gun, at rest, draws its idle */
   SwitchPictureAtRest();
   CHECK(flowPs.weapon==next);
   continue;
  }
  /* YY: back two commands later; the gun is ready (the pump owed and worked
   * again, the switch lost) and the picture eases home along the drop */
  SwitchPictureStep(0);
  FlowSlot(SLOT_PRIMARY); SwitchPictureStep(0);
  CHECK(flowPs.weapon==gun && flowPs.weaponstate==(gun==WP_G19 ? WEAPON_READY : WEAPON_BOLTING));
  CHECK(move->clip==WANIM_DROP && move->weight>0);
  if(gun==WP_G19) CHECK((move->fit==MOVE_FIT_RETURN || move->targetWeight==0) && !(flowPs.slotFlags[0]&BG_GUN_AUTO));
  for(n=0;n<SwitchFrames((int)MOVE_RETURN_MS+64);n++) SwitchPictureStep(0);
  CHECK(move->weight==0);
  /* at rest, the idle: the pump played out, the switch lost */
  SwitchPictureAtRest();
  CHECK(flowPs.weapon==gun);
  if(gun==WP_G19) CHECK(BG_IS_SELECT_SEQ(flowPs.weaponAnimSeq) && !(flowPs.slotFlags[0]&BG_GUN_AUTO));
 }
 /* The action begun 1 to 6 commands into a sprint YY's ease home: the
  * SPAS's shot, which the pump follows, and the G19's switch. Each beside
  * the same ease with nothing begun, and the action begun at rest. */
 for(r=0;r<ARRAY_LEN(rates);r++) for(g=0;g<ARRAY_LEN(switchGuns);g++) for(d=1;d<=6;d++) {
  int gun=switchGuns[g].gun;
  float own,ease;
  switchMsec=rates[r];
  own=SwitchPictureOwn(gun,switchGuns[g].next);
  ease=SwitchPictureEase(gun,d,0,0,0);
  SwitchPictureEase(gun,d,1,own,ease);
 }
 nativePose=qfalse;
 CG_WeapAnim_RegisterClips(WP_G19,0,NULL);
}

int main(void) {
 playerState_t ps;
 const int states[]={WEAPON_READY,WEAPON_FIRING,WEAPON_BOLTING};
 int i,clip;
 weapAnimDef_t *def;
 CG_WeapAnim_Init();
 CG_WeapAnim_RegisterClips(WP_SHOTGUN,1,"assets/baseq3/models/weapons2/spas12/view.cfg");
 def=&bg_weapAnimDefs[WP_SHOTGUN];
 CHECK(CG_WeapAnim_ClipLength(def,WANIM_RAISE)>0);
 /* The clip table and the gameplay timers are the same numbers: a mismatch is
  * a time-warped, jittery draw or holster. */
 CHECK(fabs(CG_WeapAnim_ClipLength(def,WANIM_RAISE)-BG_WeaponRaiseTime(WP_SHOTGUN))<.5f);
 CHECK(fabs(CG_WeapAnim_ClipLength(def,WANIM_DROP)-BG_WeaponDropTime(WP_SHOTGUN))<.5f);
 for(clip=WANIM_RAISE;clip<=WANIM_DROP;clip++) for(i=0;i<ARRAY_LEN(states);i++) {
  Ready(&ps,WP_SHOTGUN); ps.weaponstate=states[i];
  cg_weapLayers[LAYER_MOVE].clip=clip;
  cg_weapLayers[LAYER_MOVE].weight=1;
  cg_weapLayers[LAYER_MOVE].time=0;
  CHECK(CG_WeapAnim_TransitionBusy(def,&cg_weapLayers[LAYER_MOVE]));
  CG_WeapAnim_UpdateLayers(def,&ps,1);
  CHECK(cg_weapLayers[LAYER_MOVE].targetWeight==0);
  CHECK(cg_weapLayers[LAYER_MOVE].weight<1);
  CHECK(cg_weapLayers[LAYER_BASE].clip==(i ? WANIM_FIRE : WANIM_IDLE));
 }
 TestSprint(def,WP_SHOTGUN);
 TestOwnership(def); TestExclusions(def); TestRelease(def); TestContext(def);
 TestFirstDraw(def,WP_SHOTGUN);
 TestReloadPerk(def,WP_SHOTGUN);
 TestFlow(def); TestKnifePicture(def); TestKeptHolster(def);
 TestLadderKeepsDrop(def,WP_SHOTGUN); TestRetainedKeptHolster(); TestSnapshotRestamp(def);
 TestLadderHeldPicture(def,WP_SHOTGUN);
 TestShortDropKept();
 TestSprintFlow(def);
 TestSprintAfterTheCycle(WP_SHOTGUN);
 TestShotPictureAcrossReady(WP_SHOTGUN);
 TestDryFirePicture(WP_SHOTGUN);
 TestSprintReturnLikeExit(def,WP_SHOTGUN,"assets/baseq3/models/weapons2/spas12/view.iqm");
 TestSprintReturnUnmoved(WP_SHOTGUN,"assets/baseq3/models/weapons2/spas12/view.iqm");
 TestSprintReentry(def);
 TestPictureContinuity(def,WP_SHOTGUN,"assets/baseq3/models/weapons2/spas12/view.iqm");
 TestActionHandoff(WP_SHOTGUN);
 TestKnifePullPicture();
 CG_WeapAnim_RegisterClips(WP_M40A3,2,"assets/baseq3/models/weapons2/m40a3/view.cfg");
 def=&bg_weapAnimDefs[WP_M40A3];
 TestLadderKeepsDrop(def,WP_M40A3);
 TestLadderHeldPicture(def,WP_M40A3);
 TestSprint(def,WP_M40A3); TestFirstDraw(def,WP_M40A3);
 TestSprintReturnLikeExit(def,WP_M40A3,"assets/baseq3/models/weapons2/m40a3/view.iqm");
 TestSprintReturnUnmoved(WP_M40A3,"assets/baseq3/models/weapons2/m40a3/view.iqm");
 TestReloadPerk(def,WP_M40A3);
 TestOwnership(def); TestExclusions(def);
 TestPictureContinuity(def,WP_M40A3,"assets/baseq3/models/weapons2/m40a3/view.iqm");
 TestActionHandoff(WP_M40A3);
 TestSprintAfterTheCycle(WP_M40A3);
 TestShotPictureAcrossReady(WP_M40A3);
 TestDryFirePicture(WP_M40A3);
 TestModeSwitchPicture();
 TestSwitchPicture();
 printf("PASS: %d weapon presentation ownership checks\n",checks); return 0;
}
