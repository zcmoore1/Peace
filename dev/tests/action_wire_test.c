/* Verify the real playerstate delta format preserves all ten action bits and MW2's grenadeTimeLeft,
 * including a tap released while holstering, through prediction corrections. */
#include "../../code/qcommon/q_shared.h"
#include "../../code/qcommon/qcommon.h"
#include "../../code/game/bg_public.h"

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
 for(i=0;i<1024;i++) for(j=0;j<1024;j++) {
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
  to.slotFlags[0]=i&7; to.slotFlags[1]=j&7;	/* with the generation bit */
  { const int hints[4]={0,37,ENTITYNUM_WORLD,ENTITYNUM_NONE}; to.cursorHintEntIndex=hints[(i+j)&3]; }
  from.cursorHint=i%CURSOR_HINT_COUNT; to.cursorHint=j%CURSOR_HINT_COUNT;	/* every hint type, from every other */
  to.pickupTarget=1023; to.pickupSerial=2147483000; to.pickupTime=1000;
  to.pickupSlot=j&1; to.pickupHeld=j&3;
  from.weapFlags=i&3; to.weapFlags=j&3;	/* PWF_USE_RELOAD and PWF_SPRINT_RELEASE */
  /* MW2's jump and sprint state: an airborne jump (-1), the longest landing
   * slowdown, the full jump wait, a fractional take-off height, the meter. */
  to.jumpLand=(j&1) ? -1 : BG_JUMP_LAND_TIME; to.jumpWait=(i&1) ? BG_JUMP_WAIT : 0;
  to.jumpOriginZ=-123.375f; to.sprintUsed=(j&2) ? BG_SPRINT_TIME : 0;
  /* MW2's grenadeTimeLeft: none, cooked out (-1), every offhand's whole
   * fuse, a cooked frag part way, from every other */
  { const int fuses[8]={0,-1,3500,1500,2000,BG_THROWING_KNIFE_LIFE,50,1};
    from.grenadeTimeLeft=fuses[i&7]; to.grenadeTimeLeft=fuses[j&7]; }
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
  CHECK(decoded.weapFlags==to.weapFlags);
  CHECK(decoded.jumpLand==to.jumpLand && decoded.jumpWait==to.jumpWait);
  CHECK(decoded.jumpOriginZ==to.jumpOriginZ && decoded.sprintUsed==to.sprintUsed);
  CHECK(decoded.cursorHintEntIndex==to.cursorHintEntIndex);
  CHECK(decoded.cursorHint==to.cursorHint);
  CHECK(decoded.grenadeTimeLeft==to.grenadeTimeLeft);
 }
 /* the use hint's six types in PS_CURSOR_HINT_BITS (3, protocol 84): both
  * Tactical Insertion types, the top one included, each from every other.
  * Protocol 85 adds grenadeTimeLeft. */
 CHECK(CURSOR_HINT_COUNT==6 && PS_CURSOR_HINT_BITS==3 && CURSOR_HINT_INSERTION_ENEMY==5 && PROTOCOL_VERSION==86);
 for(i=0;i<CURSOR_HINT_COUNT;i++) for(j=0;j<CURSOR_HINT_COUNT;j++) {
  memset(&from,0,sizeof(from)); to=from; from.cursorHint=i; to.cursorHint=j;
  to.cursorHintEntIndex=MAX_CLIENTS+j;
  MSG_Init(&msg,data,sizeof(data)); MSG_WriteDeltaPlayerstate(&msg,&from,&to);
  CHECK(!msg.overflowed); MSG_BeginReading(&msg);
  MSG_ReadDeltaPlayerstate(&msg,&from,&decoded);
  CHECK(decoded.cursorHint==j && decoded.cursorHintEntIndex==MAX_CLIENTS+j);
 }
 /* grenadeTimeLeft is MW2's signed 16 bits (0x742550): every fuse an offhand
  * has fits (the throwing knife's 30000 the longest), as do both ends */
 { const int fuses[6]={-32768,-1,0,1,BG_THROWING_KNIFE_LIFE,32767};
   CHECK(BG_THROWING_KNIFE_LIFE<32768);
   for(i=0;i<6;i++) for(j=0;j<6;j++) {
    memset(&from,0,sizeof(from)); to=from; from.grenadeTimeLeft=fuses[i]; to.grenadeTimeLeft=fuses[j];
    MSG_Init(&msg,data,sizeof(data)); MSG_WriteDeltaPlayerstate(&msg,&from,&to);
    CHECK(!msg.overflowed); MSG_BeginReading(&msg);
    MSG_ReadDeltaPlayerstate(&msg,&from,&decoded);
    CHECK(decoded.grenadeTimeLeft==fuses[j]);
   } }
 printf("PASS: %d action wire checks\n",checks); return 0;
}
