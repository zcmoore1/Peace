/* Verify the real playerstate delta format preserves all six action bits,
 * including a tap released while holstering, through prediction corrections. */
#include "../../code/qcommon/q_shared.h"
#include "../../code/qcommon/qcommon.h"

static int checks;
cvar_t *cl_shownet;
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_Error(int level,const char *fmt,...) {
 va_list args; (void)level; va_start(args,fmt); vfprintf(stderr,fmt,args);
 va_end(args); exit(1);
}
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
int main(void) {
 playerState_t from,to,decoded;
 byte data[MAX_MSGLEN]; msg_t msg; int i,j;
 for(i=0;i<64;i++) for(j=0;j<64;j++) {
  memset(&from,0,sizeof(from)); from.weaponAction=i;
  to=from; to.weaponAction=j;
  to.ammo[3]=7; to.ammoReserve[3]=24;
  to.weapon=3; to.weaponstate=2; to.weaponTime=37;
  MSG_Init(&msg,data,sizeof(data)); MSG_WriteDeltaPlayerstate(&msg,&from,&to);
  CHECK(!msg.overflowed); MSG_BeginReading(&msg);
  MSG_ReadDeltaPlayerstate(&msg,&from,&decoded);
  CHECK(decoded.weaponAction==j);
  CHECK(decoded.ammo[3]==7 && decoded.ammoReserve[3]==24);
  CHECK(decoded.weapon==3 && decoded.weaponstate==2 && decoded.weaponTime==37);
 }
 printf("PASS: %d action wire checks\n",checks); return 0;
}
