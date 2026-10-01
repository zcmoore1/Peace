/* Exercise the actual cgame/UI syscall boundary and VM pointer translation.
 * A QVM's trailing 32-bit pose offset must never be dereferenced as a native
 * pointer, nor read together with the next four bytes on a 64-bit host. */
#include "../../code/client/cl_cgame.c"
#include "../../code/qcommon/vm_local.h"
refexport_t re;
static refEntity_t rendered;
static int checks, submissions;
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_Error(int level,const char *fmt,...) {
 va_list args; (void)level; va_start(args,fmt); vfprintf(stderr,fmt,args);
 va_end(args); exit(1);
}
static void Capture(const refEntity_t *entity) { rendered=*entity; submissions++; }
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
 vm.entryPoint=NativeEntry; vm.dataBase=NULL; input.pose=&pose;
 CHECK(VM_IsNative(&vm));
 CL_AddRefEntityFromVM(&vm,(intptr_t)&input);
 CHECK(submissions==3 && rendered.pose==&pose);
 CHECK(!memcmp(&rendered,&input,sizeof(input)));
 currentVM=NULL;
 printf("PASS: %d native/QVM pose boundary checks\n",checks); return 0;
}
