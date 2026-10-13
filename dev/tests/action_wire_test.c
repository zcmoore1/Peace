/* Verify the real playerstate delta format preserves all nine action bits,
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
 for(i=0;i<512;i++) for(j=0;j<512;j++) {
  memset(&from,0,sizeof(from)); from.weaponAction=i;
  to=from; to.weaponAction=j;
  to.ammo[3]=7; to.ammoReserve[3]=24;
  to.stats[PS_STAT_WEAPONS]=(1<<14)|(1<<17)|(1<<(MAX_WEAPONS-1));
  to.ammo[17]=2; to.ammo[MAX_WEAPONS-1]=3; to.ammoReserve[MAX_WEAPONS-1]=99;
  to.weapon=3; to.weaponstate=2; to.weaponTime=37;
  MSG_Init(&msg,data,sizeof(data)); MSG_WriteDeltaPlayerstate(&msg,&from,&to);
  CHECK(!msg.overflowed); MSG_BeginReading(&msg);
  MSG_ReadDeltaPlayerstate(&msg,&from,&decoded);
  CHECK(decoded.weaponAction==j);
  CHECK(decoded.ammo[3]==7 && decoded.ammoReserve[3]==24);
  CHECK(decoded.stats[PS_STAT_WEAPONS]==to.stats[PS_STAT_WEAPONS]);
  CHECK(decoded.ammo[17]==2 && decoded.ammo[MAX_WEAPONS-1]==3 && decoded.ammoReserve[MAX_WEAPONS-1]==99);
  CHECK(decoded.weapon==3 && decoded.weaponstate==2 && decoded.weaponTime==37);
 }
 printf("PASS: %d action wire checks\n",checks); return 0;
}
