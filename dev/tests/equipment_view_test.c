/* Real quick-action renderer, clip loader, shipped MD3 tags and PM_Weapon.
 * Art must follow the hit/release/cancel, never change gameplay to fit itself. */
#include "../../code/cgame/cg_weapons.c"
cg_t cg;
cgs_t cgs;
static refEntity_t rendered[2];
static orientation_t grips[1024], sampledGrip;
static int draws, checks, hasGrip, frameCount;
static FILE *input;
static weaponInfo_t weapon;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define NEAR(a,b) CHECK(fabs((a)-(b))<.001f)
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL CG_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_Error(int code,const char *fmt,...) { (void)code; (void)fmt; abort(); }
/* What the real CG_AddViewWeapon links beyond the equipment's own picture
 * (TestLadderView): the gun's table and cvars, and a renderer that draws
 * frames (no model poses here: the layers' frame path). The rest is only
 * linked because CG_RegisterWeapon and the gun's effects name it, and is never
 * reached. */
weaponInfo_t cg_weapons[MAX_WEAPONS];
itemInfo_t cg_items[MAX_ITEMS];
centity_t cg_entities[MAX_GENTITIES];
vmCvar_t cg_drawGun, cg_gun_frame, cg_gun_x, cg_gun_y, cg_gun_z;
vmCvar_t cg_noProjectileTrail, cg_brassTime, cg_oldPlasma, cg_trueLightning;
qboolean trap_R_BuildModelPose(qhandle_t model,int frame,int oldframe,float blend,modelPose_t *out) {
 (void)model; (void)frame; (void)oldframe; (void)blend; (void)out; return qfalse;
}
void trap_R_BlendModelPoses(modelPose_t *out,const modelPose_t *a,float wa,const modelPose_t *b,float wb) {
 (void)out; (void)a; (void)wa; (void)b; (void)wb; abort();
}
void CG_Sights_Apply(int gun,refEntity_t *hand) { (void)gun; (void)hand; }
qboolean CG_Sights_HideViewModel(void) { return qfalse; }
void CG_Sights_AddOptic(int gun,const refEntity_t *hand) { (void)gun; (void)hand; }
void CG_Sights_Register(int gun,const char *path) { (void)gun; (void)path; abort(); }
void QDECL CG_Error(const char *fmt,...) { (void)fmt; abort(); }
qhandle_t trap_R_RegisterModel(const char *name) { (void)name; abort(); return 0; }
qhandle_t trap_R_RegisterShader(const char *name) { (void)name; abort(); return 0; }
sfxHandle_t trap_S_RegisterSound(const char *name,qboolean compressed) { (void)name; (void)compressed; abort(); return 0; }
void trap_R_ModelBounds(clipHandle_t model,vec3_t mins,vec3_t maxs) { (void)model; (void)mins; (void)maxs; abort(); }
void trap_R_AddLightToScene(const vec3_t o,float i,float r,float g,float b) { (void)o;(void)i;(void)r;(void)g;(void)b; abort(); }
void trap_S_AddLoopingSound(int n,const vec3_t o,const vec3_t v,sfxHandle_t sfx) { (void)n;(void)o;(void)v;(void)sfx; abort(); }
void trap_S_StartSound(vec3_t o,int n,int channel,sfxHandle_t sfx) { (void)o;(void)n;(void)channel;(void)sfx; abort(); }
int CG_PointContents(const vec3_t point,int pass) { (void)point; (void)pass; abort(); return 0; }
void CG_Trace(trace_t *tr,const vec3_t a,const vec3_t mins,const vec3_t maxs,const vec3_t b,int skip,int mask) {
 (void)tr;(void)a;(void)mins;(void)maxs;(void)b;(void)skip;(void)mask; abort();
}
void CG_BubbleTrail(vec3_t a,vec3_t b,float spacing) { (void)a; (void)b; (void)spacing; abort(); }
localEntity_t *CG_AllocLocalEntity(void) { abort(); return NULL; }
localEntity_t *CG_SmokePuff(const vec3_t at,const vec3_t vel,float radius,float r,float g,float b,float a,
 float duration,int startTime,int fadeInTime,int leFlags,qhandle_t hShader) {
 (void)at;(void)vel;(void)radius;(void)r;(void)g;(void)b;(void)a;(void)duration;(void)startTime;
 (void)fadeInTime;(void)leFlags;(void)hShader; abort(); return NULL;
}
void CG_PositionRotatedEntityOnTag(refEntity_t *entity,const refEntity_t *parent,qhandle_t parentModel,char *tagName) {
 (void)entity;(void)parent;(void)parentModel;(void)tagName; abort();
}
void trap_SnapVector(float *v) { int i; for(i=0;i<3;i++) v[i]=rintf(v[i]); }
int trap_FS_FOpenFile(const char *path,fileHandle_t *handle,fsMode_t mode) {
 long size; (void)mode; input=fopen(path,"rb"); CHECK(input!=NULL);
 fseek(input,0,SEEK_END); size=ftell(input); rewind(input); *handle=1; return size;
}
void trap_FS_Read(void *buffer,int size,fileHandle_t handle) {
 (void)handle; CHECK(fread(buffer,1,size,input)==(size_t)size);
}
void trap_FS_FCloseFile(fileHandle_t handle) { (void)handle; fclose(input); }
void trap_R_AddRefEntityToScene(const refEntity_t *entity) {
 CHECK(draws<2); rendered[draws++]=*entity;
}
int trap_R_LerpTag(orientation_t *tag,clipHandle_t model,int start,int end,float fraction,const char *name) {
 int i,j;
 CHECK(model==2 && !strcmp(name,"tag_weapon"));
 CHECK(start>=0 && end>=0 && start<frameCount && end<frameCount && fraction>=0 && fraction<=1);
 if(!hasGrip) { CHECK(start==0 && end==0); return 0; }
 /* The renderer's linear tag interpolation, using the exported binary. */
 for(i=0;i<3;i++) {
  tag->origin[i]=grips[start].origin[i]*(1-fraction)+grips[end].origin[i]*fraction;
  for(j=0;j<3;j++) tag->axis[i][j]=grips[start].axis[i][j]*(1-fraction)+grips[end].axis[i][j]*fraction;
  VectorNormalize(tag->axis[i]);
 }
 sampledGrip=*tag; return 1;
}
static void Draw(playerState_t *ps,int expected) {
 playerState_t before=*ps;
 vec3_t origin,axis[3]; int i,j;
 hasGrip=ps->weapon!=WP_SEMTEX;
 draws=0;CG_EquipmentView(ps,&weapon);
 CHECK(draws==expected && !memcmp(&before,ps,sizeof(before)));
 CHECK(rendered[0].hModel==weapon.handsModel);
 CHECK(rendered[0].renderfx==(RF_DEPTHHACK|RF_FIRST_PERSON|RF_MINLIGHT));
 if(expected!=2) return;
 CHECK(rendered[1].hModel==weapon.weaponModel);
 CHECK(rendered[1].frame==0 && rendered[1].oldframe==0 && rendered[1].backlerp==0 && !rendered[1].pose);
 VectorCopy(rendered[0].origin,origin); AxisCopy(rendered[0].axis,axis);
 if(hasGrip) {
  for(i=0;i<3;i++) VectorMA(origin,sampledGrip.origin[i],rendered[0].axis[i],origin);
  MatrixMultiply(sampledGrip.axis,rendered[0].axis,axis);
 }
 for(i=0;i<3;i++) {
  NEAR(rendered[1].origin[i],origin[i]); NEAR(rendered[1].oldorigin[i],origin[i]);
  for(j=0;j<3;j++) NEAR(rendered[1].axis[i][j],axis[i][j]);
 }
}
static void SameBlade(const refEntity_t *a,const refEntity_t *b) {
 int i,j;
 for(i=0;i<3;i++) {
  NEAR(a->origin[i],b->origin[i]);
  for(j=0;j<3;j++) NEAR(a->axis[i][j],b->axis[i][j]);
 }
}
static void TestHeld(int gun) {
 playerState_t ps; refEntity_t first,ready; vec3_t previous; int t;
 memset(&ps,0,sizeof(ps)); ps.weapon=gun; ps.weaponAction=WA_LETHAL;
 if(BG_WeaponRaiseTime(gun)>0) {
  ps.weaponstate=WEAPON_RAISING;ps.weaponTime=BG_WeaponRaiseTime(gun);
  Draw(&ps,2); first=rendered[1]; VectorCopy(first.origin,previous);
  for(t=BG_WeaponRaiseTime(gun)-1;t>=0;t--) {
   ps.weaponTime=t; Draw(&ps,2);
   CHECK(Distance(previous,rendered[1].origin)<1);
   if(gun==WP_SEMTEX) CHECK(rendered[1].origin[2]>=previous[2]);
   VectorCopy(rendered[1].origin,previous);
  }
  ready=rendered[1]; CHECK(ready.origin[2]>first.origin[2]+15);
 } else {
  /* holdFireTime 0 (the throwing knife's): it is in HOLD, ready, on its press
   * think, so it is never drawn raising. The picture still plays the authored
   * pull-out at its own length from when it is first shown, smoothly, then
   * holds where the pull ends. */
  int start;
  CHECK(gun==WP_THROWING_KNIFE);
  ps.weapon=WP_KNIFE; ps.weaponstate=WEAPON_RAISING; ps.weaponTime=BG_MELEE_DELAY; Draw(&ps,2);
  ps.weapon=gun; ps.weaponstate=WEAPON_READY; ps.weaponTime=0; start=cg.time;
  Draw(&ps,2); first=rendered[1]; VectorCopy(first.origin,previous);
  for(t=1;t<=400;t++) {
   cg.time=start+t; Draw(&ps,2);
   CHECK(Distance(previous,rendered[1].origin)<1); VectorCopy(rendered[1].origin,previous);
  }
  ready=rendered[1]; CHECK(Distance(ready.origin,first.origin)>5);	/* it did move */
  ps.weaponstate=WEAPON_RAISING; ps.weaponTime=0; Draw(&ps,2); SameBlade(&ready,&rendered[1]);	/* where the pull ends */
 }
 ps.weaponstate=WEAPON_READY; Draw(&ps,2); SameBlade(&ready,&rendered[1]);
 cg.time+=1000; Draw(&ps,2); SameBlade(&ready,&rendered[1]);
 ps.weaponstate=WEAPON_DROPPING;ps.weaponTime=BG_HolsterTime(&ps); Draw(&ps,2);
 ps.weaponAction|=WA_FIRED;ps.weaponstate=WEAPON_FIRING;
 for(t=BG_QUICK_ACTION_HOLD;t>=0;t-=8) { ps.weaponTime=t;Draw(&ps,1); }
 ps.weaponAction|=WA_RECOVERING;ps.weaponstate=WEAPON_DROPPING;
 ps.weaponTime=BG_HolsterTime(&ps);Draw(&ps,1);
}
static void TestSlash(void) {
 playerState_t ps; refEntity_t contact,first; vec3_t previous; int t;
 memset(&ps,0,sizeof(ps)); ps.weapon=WP_KNIFE; ps.weaponAction=WA_MELEE;
 ps.weaponstate=WEAPON_RAISING;ps.weaponTime=BG_MELEE_DELAY;
 Draw(&ps,2); first=rendered[1]; VectorCopy(first.origin,previous);
 for(t=BG_MELEE_DELAY-1;t>=0;t--) {
  ps.weaponTime=t;Draw(&ps,2); CHECK(Distance(previous,rendered[1].origin)<1);
  CHECK(rendered[1].origin[1]>=previous[1]); VectorCopy(rendered[1].origin,previous);
 }
 contact=rendered[1]; CHECK(contact.origin[1]>first.origin[1]+18);
 CHECK(contact.axis[0][1]>.8f && fabs(contact.axis[0][2])<.15f); /* horizontal */
 ps.weaponstate=WEAPON_FIRING;ps.weaponAction|=WA_FIRED;
 ps.weaponTime=BG_MELEE_TIME-BG_MELEE_DELAY; Draw(&ps,2); SameBlade(&contact,&rendered[1]);
 for(t=ps.weaponTime-1;t>=0;t--) {
  ps.weaponTime=t;Draw(&ps,2); CHECK(Distance(previous,rendered[1].origin)<1);
  VectorCopy(rendered[1].origin,previous);
 }
 CHECK(rendered[1].origin[2]<-30); /* hand clears the screen for the gun's raise */
}
void PickupWeaponStep(pmove_t *move,int msec);
static void TestGameplay(void) {
 playerState_t ps; pmove_t move; bg_class_t cl=*BG_Class(0);
 int t,seq,hit=0,cancel;
 cl.slot[0]=WP_SHOTGUN;cl.slot[1]=WP_M40A3;cl.lethal=WP_THROWING_KNIFE;cl.lethalCount=1;
 for(cancel=0;cancel<2;cancel++) {
  memset(&ps,0,sizeof(ps));memset(&move,0,sizeof(move)); BG_ApplyClass(&ps,&cl);
  ps.stats[STAT_HEALTH]=100;ps.weapon=WP_SHOTGUN;ps.weaponstate=WEAPON_READY;
  move.ps=&ps;move.cmd.weapon=BG_SlotCommand(&ps,SLOT_PRIMARY);move.cmd.buttons=BUTTON_MELEE;
  PickupWeaponStep(&move,1); CHECK(ps.weapon==WP_KNIFE && ps.weaponTime==BG_MELEE_DELAY);
  Draw(&ps,2);hit=0;
  for(t=1;t<=BG_MELEE_TIME;t++) {
   move.cmd.buttons=cancel && t>=40 ? BUTTON_LETHAL : 0;
   seq=ps.eventSequence; PickupWeaponStep(&move,1);
   for(;seq<ps.eventSequence;seq++) if(ps.events[seq&(MAX_PS_EVENTS-1)]==EV_FIRE_WEAPON) {
    CHECK(!cancel && t==BG_MELEE_DELAY && ps.weapon==WP_KNIFE);hit++;
   }
   if(ps.weapon==WP_KNIFE || ps.weapon==WP_THROWING_KNIFE) Draw(&ps,2);
   if(!cancel && t<BG_MELEE_TIME) CHECK(ps.weapon==WP_KNIFE);
  }
  CHECK(hit==!cancel && ps.ammo[WP_THROWING_KNIFE]==1);
  if(cancel) CHECK(ps.weapon==WP_THROWING_KNIFE && ps.weaponstate==WEAPON_READY);
  else CHECK(ps.weapon==WP_SHOTGUN && ps.weaponstate==WEAPON_RAISING && ps.weaponTime==BG_WeaponQuickRaiseTime(WP_SHOTGUN));
 }
}
/* The real CG_AddViewWeapon on a ladder, with the SPAS's own clips, the real
 * layers and the real PM_Weapon at 8ms (the ladder's mark set and cleared as a
 * grab and a jump leave it; ladder_test.c drives the real ladder). The holster
 * lowers the gun 24 units over its own time on top of its drop clip. Run out,
 * the gun is not drawn: in WEAPON_LADDER, and just the same in a holster hung
 * under the held trigger's lock, however long (MW2's drop animation has played
 * out and holds its last frame, the gun stowed; the state is still the drop).
 * The layers keep running under it. Left with the trigger still down, the gun
 * is drawn in hand on that frame, firing; let go on that command, it is READY
 * at once and drawn coming home from the bottom, never snapped up. */
#define VIEW_GUN 3
static pmove_t view;
static playerState_t viewPs;
static void ViewBegin(void) {
 bg_class_t cl=*BG_Class(0); int t;
 cl.slot[0]=WP_SHOTGUN; cl.slot[1]=WP_M40A3;
 memset(&viewPs,0,sizeof(viewPs)); memset(&view,0,sizeof(view)); BG_ApplyClass(&viewPs,&cl);
 viewPs.stats[STAT_HEALTH]=100; viewPs.weapon=WP_SHOTGUN; viewPs.weaponstate=WEAPON_READY;
 view.ps=&viewPs; view.cmd.weapon=BG_SlotCommand(&viewPs,SLOT_PRIMARY);
 CG_WeapAnim_Reset();
 for(t=0;t<400;t+=8) PickupWeaponStep(&view,8);
 CHECK(viewPs.weapon==WP_SHOTGUN && viewPs.weaponstate==WEAPON_READY && viewPs.weaponTime==0);
}
/* One 8ms command, then one rendered frame. Returns how low the gun was drawn
 * below where it is carried, or -1 when no gun was drawn at all. */
static float ViewStep(void) {
 playerState_t before;
 view.cmd.serverTime+=8; PickupWeaponStep(&view,8);
 before=viewPs; cg.time+=8; cg.frametime=8; draws=0; CG_AddViewWeapon(&viewPs);
 CHECK(!memcmp(&before,&viewPs,sizeof(before)) && draws<=1);
 if(!draws) return -1;
 CHECK(rendered[0].hModel==VIEW_GUN && rendered[0].renderfx==(RF_DEPTHHACK|RF_FIRST_PERSON|RF_MINLIGHT));
 CHECK(rendered[0].origin[0]==0 && rendered[0].origin[1]==0 && rendered[0].origin[2]<=0);
 CHECK(fabs(rendered[0].origin[2]+24*CG_WeapAnim_LadderLower())<.001f);
 return -rendered[0].origin[2];
}
static void TestLadderView(void) {
 float low,last; int way,t,n;
 memset(cg_weapons,0,sizeof(cg_weapons));
 cg_weapons[WP_SHOTGUN].registered=qtrue; cg_weapons[WP_SHOTGUN].viewModel=VIEW_GUN;
 CG_WeapAnim_RegisterClips(WP_SHOTGUN,VIEW_GUN,"assets/baseq3/models/weapons2/spas12/view.cfg");
 cg_drawGun.integer=1; VectorClear(cg.refdef.vieworg); AxisClear(cg.refdef.viewaxis);
 /* 0: hung, the jump with the trigger held. 1: hung, the trigger let go on the
  * jump's command. 2: hung, let go on the ladder. 3: the trigger never down. */
 for(way=0;way<4;way++) {
  ViewBegin();
  CHECK(ViewStep()==0);
  if(way<3) {
   view.cmd.buttons=BUTTON_ATTACK;
   for(t=0;t<10000 && !(viewPs.weaponstate==WEAPON_READY && viewPs.weaponTime==1 &&
    !(viewPs.stats[STAT_UNCHAMBERED]&(1<<WP_SHOTGUN)));t+=8) CHECK(ViewStep()==0);
   CHECK(t<10000 && viewPs.weaponShotCount==1);
  }
  viewPs.stats[STAT_LADDER]|=LADDER_ATTACHED;	/* PM_CheckLadder's grab, ahead of PM_Weapon */
  last=ViewStep();
  CHECK(viewPs.weaponstate==WEAPON_DROPPING && (viewPs.stats[STAT_LADDER]&LADDER_HOLSTER) && last==0);
  for(n=0;viewPs.weaponstate==WEAPON_DROPPING && viewPs.weaponTime>1;n++) {
   low=ViewStep();
   if(viewPs.weaponstate!=WEAPON_DROPPING || viewPs.weaponTime<=1) break;
   CHECK(low>last && fabs(low-24*(1.0f-(float)viewPs.weaponTime/BG_LADDER_DROP_TIME))<.001f);
   last=low;
  }
  CHECK(n>=8 && last>20 && low==-1);	/* all the way down, then out of view */
  if(way==3) {
   CHECK(viewPs.weaponstate==WEAPON_LADDER && viewPs.weapon==WP_SHOTGUN);
   for(t=0;t<1000;t+=8) CHECK(ViewStep()==-1 && viewPs.weaponstate==WEAPON_LADDER);
   continue;
  }
  for(t=0;t<3000;t+=8) {
   CHECK(viewPs.weapon==WP_SHOTGUN && viewPs.weaponstate==WEAPON_DROPPING && viewPs.weaponTime==1);
   CHECK(BG_LadderHolsterDown(&viewPs) && CG_WeapAnim_LadderLower()>=.99f);	/* the layers ran on, hidden */
   CHECK(ViewStep()==-1);
  }
  if(way==2) {
   view.cmd.buttons=0;
   CHECK(ViewStep()==-1 && viewPs.weaponstate==WEAPON_LADDER && !BG_LadderHolsterDown(&viewPs));
   for(t=0;t<1000;t+=8) CHECK(ViewStep()==-1 && viewPs.weaponstate==WEAPON_LADDER);
   continue;
  }
  viewPs.stats[STAT_LADDER]&=~LADDER_ATTACHED;	/* the jump: PM_LadderMove's, ahead of PM_Weapon */
  if(way==0) {
   CHECK(ViewStep()==0 && viewPs.weaponstate==WEAPON_FIRING && viewPs.weaponShotCount==2);
   for(t=0;t<100;t+=8) CHECK(ViewStep()==0);
   continue;
  }
  view.cmd.buttons=0; low=ViewStep();
  CHECK(viewPs.weaponstate==WEAPON_READY && viewPs.weaponTime==0 && low>22 && low<=24);
  for(n=0;low>0 && n<64;n++) {
   last=low; low=ViewStep();
   CHECK(viewPs.weaponstate==WEAPON_READY && low>=0 && low<=last && last-low<4);
  }
  CHECK(low==0 && n>=8 && n*8<=300);
 }
 CG_WeapAnim_RegisterClips(WP_SHOTGUN,0,NULL);
}
int main(void) {
 FILE *file=fopen("assets/baseq3/models/weapons2/throwingknife/knife_hand.md3","rb");
 char name[64];int offset,i;
 CHECK(file!=NULL && fseek(file,76,SEEK_SET)==0 && fread(&frameCount,4,1,file)==1);
 CHECK(frameCount>1 && frameCount<=1024 && fseek(file,96,SEEK_SET)==0 && fread(&offset,4,1,file)==1);
 CHECK(fseek(file,offset,SEEK_SET)==0);
 for(i=0;i<frameCount;i++) {
  CHECK(fread(name,1,64,file)==64 && !strcmp(name,"tag_weapon"));
  CHECK(fread(grips[i].origin,sizeof(float),3,file)==3 && fread(grips[i].axis,sizeof(float),9,file)==9);
 }
 fclose(file); AxisClear(cg.refdef.viewaxis);weapon.weaponModel=1;weapon.handsModel=2;
 CG_WeapAnim_RegisterClips(WP_THROWING_KNIFE,2,"assets/baseq3/models/weapons2/throwingknife/throw.cfg");
 CG_WeapAnim_RegisterClips(WP_KNIFE,2,"assets/baseq3/models/weapons2/throwingknife/melee.cfg");
 TestHeld(WP_THROWING_KNIFE);TestHeld(WP_SEMTEX);TestSlash();TestGameplay();
 TestLadderView();
 printf("PASS: %i equipment/slash pose, tag, hit/release/cancel, ladder view and gameplay-immutability checks\n",checks);
 return 0;
}
