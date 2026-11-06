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
 printf("PASS: %i equipment/slash pose, tag, hit/release/cancel and gameplay-immutability checks\n",checks);
 return 0;
}
