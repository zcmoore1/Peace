/* Real presentation code, real clip definitions, no gameplay mutations.
 * Ownership follows action categories, not weapon IDs or named glitches. */
#include "../../code/cgame/cg_anim.c"
cg_t cg;
static int checks;
static FILE *input;
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL CG_Printf(const char *fmt,...) { (void)fmt; }
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
 (void)model; (void)frame; (void)oldframe; (void)blend; (void)out; return qfalse;
}
void trap_R_BlendModelPoses(modelPose_t *out,const modelPose_t *a,float wa,const modelPose_t *b,float wb) {
 (void)out; (void)a; (void)wa; (void)b; (void)wb;
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
  ps.pm_flags|=PMF_SPRINTING;
  if(states[s]==WEAPON_FIRING || states[s]==WEAPON_BOLTING)
   ps.weaponstate=WEAPON_SPRINT_IN;
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
 /* With no holster, a finished one-shot releases to normal sprint carry. */
 BeginSource(def,&ps,WP_SHOTGUN,WEAPON_BOLTING);
 ps.pm_flags=PMF_SPRINTING; ps.weaponstate=WEAPON_SPRINT_IN; Update(def,&ps,1);
 CHECK(cg_retainedActive);
 Update(def,&ps,(int)CG_WeapAnim_ClipLength(def,WANIM_FIRE)*2);
 ps.weaponstate=WEAPON_SPRINTING; Update(def,&ps,1);
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
 refEntity_t sample;
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
  }
  weight=retained->weight;
  for(t=0;t<100 && retained->weight>0;t++) {
   Update(def,&ps,1); CHECK(retained->weight<weight); weight=retained->weight;
   CHECK(CG_WeapAnim_SampleLayers(def,&sample));
   if(!held && weight>.001f && weight<.999f) {
    /* Both sides of half-weight are continuous blends, not clip switches. */
    CHECK(sample.frame<def->clips[WANIM_IDLE].firstFrame+def->clips[WANIM_IDLE].numFrames);
    CHECK(sample.oldframe>=def->clips[WANIM_SPRINT_IN].firstFrame);
    CHECK(fabs(sample.backlerp-weight)<.0001f);
   }
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
static pmove_t flow;
static playerState_t flowPs;
static void FlowTrace(trace_t *t,const vec3_t a,const vec3_t b,const vec3_t c,const vec3_t d,int e,int f) {
 (void)a;(void)b;(void)c;(void)e;(void)f;
 memset(t,0,sizeof(*t)); t->fraction=1; VectorCopy(d,t->endpos); t->entityNum=ENTITYNUM_NONE;
}
static void FlowBegin(void) {
 bg_class_t cl=*BG_Class(0);
 memset(&flowPs,0,sizeof(flowPs)); memset(&flow,0,sizeof(flow));
 cl.slot[0]=WP_MACHINEGUN; cl.slot[1]=WP_SHOTGUN; BG_ApplyClass(&flowPs,&cl);
 flowPs.stats[STAT_HEALTH]=100; flowPs.pm_type=PM_NORMAL;
 flowPs.weapon=WP_MACHINEGUN; flowPs.weaponstate=WEAPON_READY;
 flow.ps=&flowPs; flow.trace=FlowTrace; flow.cmd.weapon=BG_SLOT_COMMAND+SLOT_PRIMARY;
 CG_WeapAnim_Reset();
}
/* One 8ms command, then one rendered frame of the real presentation. */
static void FlowStep(void) {
 playerState_t before;
 flow.cmd.serverTime+=8; PickupWeaponStep(&flow,8);
 before=flowPs; CG_WeapAnim_BuildPose(&flowPs,1,8);
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
   /* Back down no faster than the draw came up: 8ms of clip per 8ms frame at most. */
   CHECK(move->clip==WANIM_RAISE && move->fit==MOVE_FIT_REVERSE && move->time<=last+.01f &&
    last-move->time<=8*raise/BG_WeaponRaiseTime(WP_SHOTGUN)+.01f);
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
/* A grenade pressed while the gun goes down for the knife restamps the drop to
 * the gun's whole quick drop (MW2's OffHandInit). The picture carries on from
 * the pose on screen and still lands exactly when the lock runs out. */
static void TestDropRestamp(weapAnimDef_t *def) {
 animLayer_t *move=&cg_weapLayers[LAYER_MOVE];
 float drop=CG_WeapAnim_ClipLength(def,WANIM_DROP),last;
 int t;
 FlowBegin(); FlowSlot(SLOT_SECONDARY);
 for(t=0;t<10000 && !(flowPs.weapon==WP_SHOTGUN && flowPs.weaponstate==WEAPON_READY && move->weight==0);t+=8) FlowStep();
 flow.cmd.buttons=BUTTON_MELEE; FlowStep(); flow.cmd.buttons=0; FlowStep(); FlowStep();
 CHECK(flowPs.weaponstate==WEAPON_DROPPING && (flowPs.weaponAction&WA_TYPE_MASK)==WA_MELEE);
 CHECK(move->clip==WANIM_DROP && move->fit==MOVE_FIT_FORWARD && move->time>drop/8);
 last=move->time;
 flow.cmd.buttons=BUTTON_LETHAL; FlowStep(); flow.cmd.buttons=0;
 CHECK((flowPs.weaponAction&WA_TYPE_MASK)==WA_LETHAL && flowPs.weaponstate==WEAPON_DROPPING &&
  flowPs.weaponTime>=BG_WeaponQuickDropTime(WP_SHOTGUN)-8);
 CHECK(move->clip==WANIM_DROP && move->time>=last-.01f && move->time-last<=8*drop/BG_HolsterTime(&flowPs)+.01f);
 while(flowPs.weaponstate==WEAPON_DROPPING && flowPs.weapon==WP_SHOTGUN) {
  last=move->time; FlowStep();
  if(flowPs.weaponstate==WEAPON_DROPPING) CHECK(move->time>=last-.01f && move->time-last<=8*drop/BG_HolsterTime(&flowPs)+.01f);
 }
 CHECK(last>=drop*.9f);
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
int main(void) {
 playerState_t ps;
 const int states[]={WEAPON_READY,WEAPON_FIRING,WEAPON_BOLTING};
 int i,clip;
 weapAnimDef_t *def;
 CG_WeapAnim_Init();
 CG_WeapAnim_RegisterClips(WP_SHOTGUN,1,"assets/baseq3/models/weapons2/spas12/view.cfg");
 def=&bg_weapAnimDefs[WP_SHOTGUN];
 CHECK(CG_WeapAnim_ClipLength(def,WANIM_RAISE)>0);
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
 TestFlow(def); TestDropRestamp(def); TestKeptHolster(def);
 CG_WeapAnim_RegisterClips(WP_M40A3,2,"assets/baseq3/models/weapons2/m40a3/view.cfg");
 def=&bg_weapAnimDefs[WP_M40A3];
 TestSprint(def,WP_M40A3); TestFirstDraw(def,WP_M40A3);
 TestReloadPerk(def,WP_M40A3);
 TestOwnership(def); TestExclusions(def);
 printf("PASS: %d weapon presentation ownership checks\n",checks); return 0;
}
