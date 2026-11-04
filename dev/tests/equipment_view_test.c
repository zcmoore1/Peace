/* The real held-equipment renderer, including the shipped MD3 grip tag.
 * Check release/cancel presentation without permitting it to change gameplay. */
#include "../../code/cgame/cg_weapons.c"

cg_t cg;
cgs_t cgs;
static refEntity_t rendered[2];
static orientation_t knifeGrip;
static int draws, checks, hasGrip;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define NEAR(a,b) CHECK(fabs((a)-(b))<.001f)
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_Error(int code,const char *fmt,...) { (void)code; (void)fmt; abort(); }
void trap_R_AddRefEntityToScene(const refEntity_t *entity) {
 CHECK(draws<2); rendered[draws++]=*entity;
}
int trap_R_LerpTag(orientation_t *tag,clipHandle_t model,int start,int end,float fraction,const char *name) {
 CHECK(model==2 && start==0 && end==0 && fraction==1 && !strcmp(name,"tag_weapon"));
 if(hasGrip) *tag=knifeGrip;
 return hasGrip;
}
static void Draw(playerState_t *ps,weaponInfo_t *weapon,int expected) {
 playerState_t before=*ps;
 draws=0;CG_EquipmentView(ps,weapon);
 CHECK(draws==expected && !memcmp(&before,ps,sizeof(before)));
 CHECK(rendered[0].hModel==weapon->handsModel);
 CHECK(rendered[0].renderfx==(RF_DEPTHHACK|RF_FIRST_PERSON|RF_MINLIGHT));
 if(expected==2) CHECK(rendered[1].hModel==weapon->weaponModel);
}
static void TestHeld(int gun) {
 playerState_t ps;
 weaponInfo_t weapon;
 vec3_t first,ready,axis[3];
 float previous;
 int t,i,j;
 memset(&ps,0,sizeof(ps));memset(&weapon,0,sizeof(weapon));memset(&cg,0,sizeof(cg));
 AxisClear(cg.refdef.viewaxis);
 weapon.weaponModel=1;weapon.handsModel=2;ps.weapon=gun;ps.weaponAction=WA_LETHAL;
 hasGrip=gun==WP_THROWING_KNIFE;
 ps.weaponstate=WEAPON_RAISING;ps.weaponTime=BG_WeaponRaiseTime(gun);
 Draw(&ps,&weapon,2);VectorCopy(rendered[0].origin,first);previous=first[2];
 for(t=BG_WeaponRaiseTime(gun)-1;t>=0;t--) {
  ps.weaponTime=t;Draw(&ps,&weapon,2);
  CHECK(rendered[0].origin[2]>=previous);previous=rendered[0].origin[2];
 }
 VectorCopy(rendered[0].origin,ready);
 CHECK(ready[2]>first[2]+18 && ready[0]<first[0]-6);
 if(hasGrip) {
  MatrixMultiply(knifeGrip.axis,rendered[0].axis,axis);
  for(i=0;i<3;i++) for(j=0;j<3;j++) NEAR(rendered[1].axis[i][j],axis[i][j]);
  for(i=0;i<3;i++) {
   float expected=rendered[0].origin[i];
   for(j=0;j<3;j++) expected+=knifeGrip.origin[j]*rendered[0].axis[j][i];
   NEAR(rendered[1].origin[i],expected);
   NEAR(rendered[1].oldorigin[i],expected);
  }
 } else {
  for(i=0;i<3;i++) for(j=0;j<3;j++) NEAR(rendered[1].axis[i][j],rendered[0].axis[i][j]);
 }
 ps.weaponstate=WEAPON_READY;Draw(&ps,&weapon,2);
 for(i=0;i<3;i++) NEAR(rendered[0].origin[i],ready[i]);
 /* A pre-throw cancel can lower the still-held item. It is never spent here. */
 ps.weaponstate=WEAPON_DROPPING;ps.weaponTime=BG_HolsterTime(&ps);Draw(&ps,&weapon,2);
 /* The projectile belongs to gameplay. The held blade disappears on that
  * release, including its first firing frame, while the hand follows through. */
 ps.weaponAction|=WA_FIRED;ps.weaponstate=WEAPON_FIRING;
 for(t=BG_QUICK_ACTION_HOLD;t>=0;t-=8) { ps.weaponTime=t;Draw(&ps,&weapon,1); }
 ps.weaponAction|=WA_RECOVERING;ps.weaponstate=WEAPON_DROPPING;
 ps.weaponTime=BG_HolsterTime(&ps);Draw(&ps,&weapon,1);
}
int main(void) {
 FILE *file=fopen("assets/baseq3/models/weapons2/throwingknife/knife_hand.md3","rb");
 char name[64];
 CHECK(file!=NULL && fseek(file,164,SEEK_SET)==0);
 CHECK(fread(name,1,64,file)==64 && !strcmp(name,"tag_weapon"));
 CHECK(fread(knifeGrip.origin,sizeof(float),3,file)==3);
 CHECK(fread(knifeGrip.axis,sizeof(float),9,file)==9);fclose(file);
 TestHeld(WP_THROWING_KNIFE);TestHeld(WP_SEMTEX);
 printf("PASS: %i held-equipment pose, release and gameplay-immutability checks\n",checks);
 return 0;
}
