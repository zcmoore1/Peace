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
 /* Without sprint, interrupting a draw is an ordinary drop, not retention. */
 BeginSource(def,&ps,WP_SHOTGUN,WEAPON_RAISING);
 ps.weaponstate=WEAPON_DROPPING; ps.weaponTime=BG_WeaponDropTime(ps.weapon);
 Update(def,&ps,1);
 CHECK(!cg_retainedActive && cg_weapLayers[LAYER_MOVE].clip==WANIM_DROP);
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
 CG_WeapAnim_RegisterClips(WP_M40A3,2,"assets/baseq3/models/weapons2/m40a3/view.cfg");
 def=&bg_weapAnimDefs[WP_M40A3];
 TestSprint(def,WP_M40A3); TestFirstDraw(def,WP_M40A3);
 TestOwnership(def); TestExclusions(def);
 printf("PASS: %d weapon presentation ownership checks\n",checks); return 0;
}
