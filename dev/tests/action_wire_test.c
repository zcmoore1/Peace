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
  memset(&from,0,sizeof(from)); from.weaponAction=i; from.perks=i&((1<<PS_PERK_BITS)-1);
  to=from; to.weaponAction=j; to.perks=j&((1<<PS_PERK_BITS)-1);
  to.ammo[3]=7; to.ammoReserve[3]=24;
  to.stats[PS_STAT_WEAPONS]=(1<<14)|(1<<17)|(1<<(MAX_WEAPONS-1));
  to.stats[PS_STAT_UNCHAMBERED]=(1<<15)|(1<<18)|(1<<(MAX_WEAPONS-1));
  to.ammo[17]=2; to.ammo[MAX_WEAPONS-1]=3; to.ammoReserve[MAX_WEAPONS-1]=99;
  to.weapon=3; to.weaponstate=2; to.weaponTime=37;
  to.weaponDraw=j&1; to.slotAmmo[0]=7; to.slotAmmo[1]=2;
  to.slotReserve[0]=29; to.slotReserve[1]=40;
  to.slotAttachments[0]=i&1; to.slotAttachments[1]=j&1;
  to.slotFlags[0]=i&3; to.slotFlags[1]=j&3;
  to.pickupTarget=1023; to.pickupSerial=2147483000; to.pickupTime=1000;
  to.pickupSlot=j&1; to.pickupHeld=j%3;
  MSG_Init(&msg,data,sizeof(data)); MSG_WriteDeltaPlayerstate(&msg,&from,&to);
  CHECK(!msg.overflowed); MSG_BeginReading(&msg);
  MSG_ReadDeltaPlayerstate(&msg,&from,&decoded);
  CHECK(decoded.weaponAction==j);
  CHECK(decoded.perks==to.perks);
  CHECK(decoded.ammo[3]==7 && decoded.ammoReserve[3]==24);
  CHECK(decoded.stats[PS_STAT_WEAPONS]==to.stats[PS_STAT_WEAPONS]);
  CHECK(decoded.stats[PS_STAT_UNCHAMBERED]==to.stats[PS_STAT_UNCHAMBERED]);
  CHECK(decoded.ammo[17]==2 && decoded.ammo[MAX_WEAPONS-1]==3 && decoded.ammoReserve[MAX_WEAPONS-1]==99);
  CHECK(decoded.weapon==3 && decoded.weaponstate==2 && decoded.weaponTime==37);
  CHECK(decoded.weaponDraw==to.weaponDraw && decoded.slotAmmo[0]==7 && decoded.slotAmmo[1]==2);
  CHECK(decoded.slotReserve[0]==29 && decoded.slotReserve[1]==40);
  CHECK(decoded.slotFlags[0]==to.slotFlags[0] && decoded.slotFlags[1]==to.slotFlags[1]);
  CHECK(decoded.slotAttachments[0]==to.slotAttachments[0] && decoded.slotAttachments[1]==to.slotAttachments[1]);
  CHECK(decoded.pickupTarget==1023 && decoded.pickupSerial==to.pickupSerial && decoded.pickupTime==1000);
  CHECK(decoded.pickupSlot==to.pickupSlot && decoded.pickupHeld==to.pickupHeld);
 }
 printf("PASS: %d action wire checks\n",checks); return 0;
}
