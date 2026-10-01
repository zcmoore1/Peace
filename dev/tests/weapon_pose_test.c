/* A released gameplay transition must not leave a busy raise/drop hiding the
 * firing pose. Sprint animation ownership during swaps remains unchanged. */
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
  CG_WeapAnim_WeaponChanged(WP_SHOTGUN); memset(&ps,0,sizeof(ps));
  ps.weapon=WP_SHOTGUN; ps.weaponstate=states[i];
  cg_weapLayers[LAYER_MOVE].clip=clip;
  cg_weapLayers[LAYER_MOVE].weight=1;
  cg_weapLayers[LAYER_MOVE].time=0;
  CHECK(CG_WeapAnim_TransitionBusy(def,&cg_weapLayers[LAYER_MOVE]));
  CG_WeapAnim_UpdateLayers(def,&ps,1);
  CHECK(cg_weapLayers[LAYER_MOVE].targetWeight==0);
  CHECK(cg_weapLayers[LAYER_MOVE].weight<1);
  CHECK(cg_weapLayers[LAYER_BASE].clip==(i ? WANIM_FIRE : WANIM_IDLE));
 }
 CG_WeapAnim_WeaponChanged(WP_SHOTGUN);
 ps.weaponstate=WEAPON_DROPPING; ps.weaponTime=BG_WeaponDropTime(WP_SHOTGUN);
 cg_weapLayers[LAYER_SPRINT].weight=1;
 cg_weapLayers[LAYER_SPRINT].clip=WANIM_SPRINT_IN;
 CG_WeapAnim_UpdateLayers(def,&ps,1);
 CHECK(cg_weapLayers[LAYER_MOVE].targetWeight==0);
 CHECK(cg_weapLayers[LAYER_SPRINT].targetWeight==1);
 CHECK(cg_weapLayers[LAYER_SPRINT].clip==WANIM_SPRINT_IN);
 printf("PASS: %d weapon pose cancellation checks\n",checks); return 0;
}
