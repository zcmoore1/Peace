#include "../../code/cgame/cg_draw.c"
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_Error(int code,const char *fmt,...) { (void)code; (void)fmt; abort(); }
int main(void) {
 vec3_t origin; float radius;
 CHECK(!CG_SNDReadSite("",origin,&radius));
 CHECK(!CG_SNDReadSite("1 2 3",origin,&radius));
 CHECK(!CG_SNDReadSite("1 2 3 0",origin,&radius));
 CHECK(!CG_SNDReadSite("1 2 3 1000",origin,&radius));
 CHECK(CG_SNDReadSite("264 104 328 72",origin,&radius));
 CHECK(origin[0]==264 && origin[1]==104 && origin[2]==328 && radius==72);
 CHECK(CG_SNDReadSite("-264.5 104.25 -328 72",origin,&radius));
 CHECK(origin[0]==-264.5f && origin[1]==104.25f && origin[2]==-328);
 printf("PASS: %i S&D HUD site parser checks\n",checks); return 0;
}
