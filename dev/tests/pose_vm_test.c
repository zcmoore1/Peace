/* Exercise the actual cgame/UI syscall boundary and VM pointer translation.
 * A QVM's trailing 32-bit pose offset must never be dereferenced as a native
 * pointer, nor read together with the next four bytes on a 64-bit host.
 * The entity and its pose must each lie inside the VM's memory: one that runs
 * past it is refused with ERR_DROP (VM_ExplicitArgBlock) before the renderer
 * sees it. A native module's pointers are its own and are not checked. */
#include "../../code/client/client.h"
#include "../../code/qcommon/vm_local.h"
#include <setjmp.h>
refexport_t re;
vm_t *currentVM;
static refEntity_t rendered;
static int checks, submissions;
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void Capture(const refEntity_t *entity) { rendered=*entity; submissions++; }
static jmp_buf dropJump;
static int dropping, drops;
void QDECL Com_Error(int level,const char *fmt,...) {
 (void)fmt; CHECK(dropping && level==ERR_DROP); drops++; longjmp(dropJump,1);
}
/* CL_AddRefEntityFromVM must drop instead of submitting */
static void Dropped(vm_t *vm,intptr_t address) {
 int before=submissions, dropsBefore=drops;
 dropping=1;
 if(!setjmp(dropJump)) { CL_AddRefEntityFromVM(vm,address); dropping=0; CHECK(!"not dropped"); }
 dropping=0;
 CHECK(drops==dropsBefore+1 && submissions==before);
}
static intptr_t QDECL NativeEntry(int call,int a0,int a1,int a2,int a3,int a4,
 int a5,int a6,int a7,int a8,int a9,int a10,int a11) { return 0; }
int main(void) {
 static byte memory[16384];
 vm_t vm;
 refEntity_t input;
 modelPose_t pose;
 int address=64, offset=512, sentinel=-1;
 size_t prefix=offsetof(refEntity_t,rotation)+sizeof(float);
 memset(&vm,0,sizeof(vm)); memset(&input,0,sizeof(input)); memset(&pose,0,sizeof(pose));
 re.AddRefEntityToScene=Capture;
 vm.dataBase=memory; vm.dataMask=sizeof(memory)-1; currentVM=&vm;
 input.reType=RT_MODEL; input.hModel=17; input.origin[2]=33;
 input.shaderRGBA[3]=255; input.rotation=12.5f;
 pose.numJoints=1; pose.joints[0].translate[0]=42;
 memcpy(memory+address,&input,prefix);
 memcpy(memory+address+prefix,&offset,sizeof(offset));
 memcpy(memory+address+prefix+sizeof(offset),&sentinel,sizeof(sentinel));
 memcpy(memory+offset,&pose,sizeof(pose));
 CHECK(!VM_IsNative(&vm));
 CL_AddRefEntityFromVM(&vm,address);
 CHECK(submissions==1 && !memcmp(&rendered,&input,prefix));
 CHECK(rendered.pose==(const modelPose_t *)(memory+offset));
 CHECK(rendered.pose->numJoints==1 && rendered.pose->joints[0].translate[0]==42);
 offset=0; memcpy(memory+address+prefix,&offset,sizeof(offset));
 CL_AddRefEntityFromVM(&vm,address);
 CHECK(submissions==2 && rendered.pose==NULL);
 CL_AddRefEntityFromVM(&vm,0); CHECK(submissions==2);
 /* the entity and its pose inside the VM's memory, ending on its last byte */
 {
  int end=(int)sizeof(memory), entityAt=end-(int)(prefix+sizeof(int)), poseAt=end-(int)sizeof(modelPose_t);
  int submitted=submissions;
  memcpy(memory+entityAt,&input,prefix);
  memcpy(memory+entityAt+prefix,&poseAt,sizeof(poseAt));
  CL_AddRefEntityFromVM(&vm,entityAt);
  CHECK(submissions==submitted+1 && rendered.pose==(const modelPose_t *)(memory+poseAt));
  CHECK(rendered.hModel==17 && rendered.rotation==12.5f);
  /* an address past the memory is masked back into it, as the VM masks it */
  CL_AddRefEntityFromVM(&vm,entityAt+end);
  CHECK(submissions==submitted+2 && rendered.pose==(const modelPose_t *)(memory+poseAt));
  /* a pose 4 bytes past the end */
  poseAt+=4; memcpy(memory+entityAt+prefix,&poseAt,sizeof(poseAt));
  Dropped(&vm,entityAt);
  /* an entity whose pose offset is 4 bytes past the end */
  memcpy(memory+address,&input,prefix);
  memcpy(memory+address+prefix,&offset,sizeof(offset));
  Dropped(&vm,entityAt+4);
  CHECK(drops==2);
 }
 /* a native module: its vm_t's data is zeroed (vm.c), and its pointers are
  * its own. VM_ExplicitArgBlock, which native cgame's pose traps go through,
  * hands them back unchecked: any host address, any size, no drop. */
 vm.entryPoint=NativeEntry; vm.dataBase=NULL; vm.dataMask=0; input.pose=&pose;
 CHECK(VM_IsNative(&vm));
 {
  int dropsBefore=drops;
  static modelPose_t far[2];
  CHECK(VM_ExplicitArgBlock(&vm,(intptr_t)&pose,sizeof(pose),"native pose")==&pose);
  CHECK(VM_ExplicitArgBlock(&vm,(intptr_t)&far[1],sizeof(far[1]),"native pose")==&far[1]);
  CHECK(VM_ExplicitArgBlock(&vm,(intptr_t)&input,sizeof(input),"native entity")==&input);
  CHECK(VM_ExplicitArgBlock(&vm,0,sizeof(pose),"native pose")==NULL);
  CHECK(drops==dropsBefore);
 }
 {
  int submitted=submissions;
  CL_AddRefEntityFromVM(&vm,(intptr_t)&input);
  CHECK(submissions==submitted+1 && rendered.pose==&pose);
  CHECK(!memcmp(&rendered,&input,sizeof(input)));
 }
 currentVM=NULL;
 printf("PASS: %d native/QVM pose boundary checks\n",checks); return 0;
}
