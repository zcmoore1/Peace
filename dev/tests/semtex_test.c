/* Shared gameplay + real wire correction/replay. The preexisting harness also
 * guards ordinary swaps/reloads; use its timing helpers, never copied tables. */
#define main LoadoutTests
#include "loadout_test.c"
#undef main
#include "../../code/qcommon/qcommon.h"
cvar_t *cl_shownet;

static void SemtexReady(int gun) {
 bg_class_t cl = *BG_Class(0);
 Ready(gun,gun==WP_MACHINEGUN ? WP_SHOTGUN : WP_MACHINEGUN);
 cl.slot[0]=ps.weapon; cl.slot[1]=move.cmd.weapon==WP_MACHINEGUN ? WP_SHOTGUN : WP_MACHINEGUN;
 cl.lethal=WP_SEMTEX; cl.lethalCount=2;
 BG_ApplyClass(&ps,&cl);
 CHECK(ps.ammo[WP_SEMTEX]==2 && ps.ammo[WP_FRAG]==0);
 CHECK(ps.stats[STAT_WEAPONS]&(1<<WP_SEMTEX));
}
static void Hold(void) {
 move.cmd.buttons=BUTTON_LETHAL; Step(1);
 CHECK(ps.weaponstate==WEAPON_DROPPING);
 CHECK(ps.weaponTime==BG_HolsterTime(&ps) && ps.weaponTime==BG_FAST_HOLSTER_TIME);
 Run(ps.weaponTime+BG_WeaponRaiseTime(WP_SEMTEX)+1);
 CHECK(ps.weapon==WP_SEMTEX && ps.weaponstate==WEAPON_READY);
 CHECK(fired[WP_SEMTEX]==0 && ps.ammo[WP_SEMTEX]==2);
}
static void Cancel(int gun) {
 int rounds=ps.ammo[WP_SEMTEX];
 move.cmd.buttons|=BUTTON_WEAPON_SWAP; Step(1);
 CHECK(ps.weapon==gun && ps.weaponstate==WEAPON_RAISING);
 CHECK(ps.weaponTime==BG_WeaponRaiseTime(gun));
 /* Same recovery as a knife or a thrown grenade: the latch holds until the
  * gun is fully drawn, and blocks sprint the whole time. */
 CHECK(ps.ammo[WP_SEMTEX]==rounds && (ps.weaponAction&(WA_TYPE_MASK|WA_FIRED))==(WA_LETHAL|WA_FIRED));
 Run(BG_WeaponRaiseTime(gun)+1);
 CHECK(ps.weapon==gun && ps.weaponstate==WEAPON_READY);
 CHECK(ps.ammo[WP_SEMTEX]==rounds && !(ps.weaponAction&WA_TYPE_MASK));
 move.cmd.buttons=0; Step(1);
}
static void TestSemtex(void) {
 int i,gun,phase,seq,t,ammo,reserve,delta;
 for(i=0;i<BG_ClassWeaponCount();i++) {
  gun=BG_ClassWeapon(i);
  for(phase=0;phase<3;phase++) {
   SemtexReady(gun);
   if(phase==0) { move.cmd.buttons=BUTTON_LETHAL; Step(1); }
   else if(phase==1) { move.cmd.buttons=BUTTON_LETHAL; Step(1); Run(ps.weaponTime); }
   else Hold();
   Cancel(gun);
   CHECK(!fired[WP_SEMTEX]);
  }
  // Post-release Y selects immediately but does not interrupt the throw or
  // original-gun raise. The new holster begins on the exact ready boundary.
  SemtexReady(gun); Hold(); move.cmd.buttons=0; Step(1);
  CHECK(fired[WP_SEMTEX]==1);
  move.cmd.weapon=ps.stats[STAT_SLOT_SECONDARY]; move.cmd.buttons=BUTTON_WEAPON_SWAP; Step(1);
  CHECK(ps.weapon==WP_SEMTEX && ps.weaponstate==WEAPON_FIRING);
  CHECK(ps.weaponTime==BG_QUICK_ACTION_HOLD-1);
  move.cmd.buttons=0; Run(ps.weaponTime);
  CHECK(ps.weapon==WP_SEMTEX && ps.weaponstate==WEAPON_DROPPING);
  Run(ps.weaponTime);
  CHECK(ps.weapon==gun && ps.weaponstate==WEAPON_RAISING);
  CHECK(ps.weaponAction&WA_RECOVERING);
  Run(BG_WeaponRaiseTime(gun)-1);
  CHECK(ps.weapon==gun && ps.weaponstate==WEAPON_RAISING && ps.weaponTime==1);
  Step(1);
  CHECK(ps.weapon==gun && ps.weaponstate==WEAPON_DROPPING);
  CHECK(ps.weaponTime==BG_WeaponDropTime(gun) && !(ps.weaponAction&WA_RECOVERING));
  Returned(); CHECK(ps.weapon==move.cmd.weapon && fired[WP_SEMTEX]==1);
  CHECK(!(ps.weaponAction&(WA_TYPE_MASK|WA_RECOVERING)));
  // YY during recovery wraps the queued selection back, with no forced swap.
  SemtexReady(gun); Hold(); move.cmd.buttons=0; Step(1);
  Toggle(); move.cmd.buttons=BUTTON_WEAPON_SWAP; Step(1);
  move.cmd.buttons=0; Step(1); Toggle(); move.cmd.buttons=BUTTON_WEAPON_SWAP; Step(1);
  move.cmd.buttons=0; Returned(); CHECK(ps.weapon==gun && fired[WP_SEMTEX]==1);
  SemtexReady(gun); Hold(); Run(10000);
  CHECK(ps.weapon==WP_SEMTEX && fired[WP_SEMTEX]==0);
  move.cmd.buttons=BUTTON_ATTACK|BUTTON_ADS|BUTTON_RELOAD; PM_CheckADS();
  CHECK(!(ps.pm_flags&PMF_ADS)); Step(1);
  CHECK(fired[WP_SEMTEX]==1 && ps.ammo[WP_SEMTEX]==1);
  move.cmd.buttons=0; Returned(); CHECK(fired[WP_SEMTEX]==1);
  // Quick release during preparation is queued, not lost or repeated.
  SemtexReady(gun); move.cmd.buttons=BUTTON_LETHAL; Step(1);
  move.cmd.buttons=0; Returned(); CHECK(fired[WP_SEMTEX]==1);
  // Same-usercmd lethal+Y and stale slot selection: acknowledge same gun.
  SemtexReady(gun); move.cmd.weapon=ps.stats[STAT_SLOT_SECONDARY];
  move.cmd.buttons=BUTTON_LETHAL|BUTTON_WEAPON_SWAP; Step(1);
  CHECK(ps.weapon==gun && ps.weaponTime==BG_WeaponRaiseTime(gun));
  CHECK(ps.weaponAction&WA_RETURNING);
  Run(10); CHECK(ps.weaponTime==BG_WeaponRaiseTime(gun)-10);
  move.cmd.weapon=gun; move.cmd.buttons=0; Step(1);
  CHECK(!(ps.weaponAction&WA_RETURNING)); Returned(); CHECK(!fired[WP_SEMTEX]);
  // Cross the actual shipped reload notes, at several command intervals.
  for(seq=0;seq<RSEQ_COUNT;seq++) {
   t=Note(gun,seq,WNOTE_MAG_IN); if(t<0) t=Note(gun,seq,WNOTE_BOLT_CLOSED);
   if(t<0)continue;
   for(delta=1;delta<=33;delta+=8) {
    SemtexReady(gun); ps.ammo[gun]=1; ammo=ps.ammo[gun]; reserve=ps.ammoReserve[gun];
    ps.weaponstate=WEAPON_RELOADING; ps.weaponAnimSeq=seq;
    ps.weaponAnimTime=t-delta; ps.weaponTime=BG_WeaponReloadSegLength(gun,seq)-ps.weaponAnimTime;
    move.cmd.buttons=BUTTON_LETHAL; Step(delta);
    CHECK(ps.weapon==WP_SEMTEX && ps.weaponstate==WEAPON_RAISING);
    CHECK(ps.ammo[gun]==ammo && ps.ammoReserve[gun]==reserve);
    CHECK(!(ps.pm_flags&PMF_PENDING_MAG));
   }
  }
 }
 /* A knife pressed during the recovery draw is lost (MW2 skips the gun's
  * return); a grenade pressed there starts the next throw. Neither may strand
  * a stale post-throw destination or refund the first action. */
 SemtexReady(WP_SHOTGUN); Hold();
 move.cmd.buttons=0; Step(1); Run(ps.weaponTime); Run(ps.weaponTime);
 CHECK(ps.weapon==WP_SHOTGUN && ps.weaponstate==WEAPON_RAISING);
 move.cmd.buttons=BUTTON_MELEE; Step(1); move.cmd.buttons=0;
 Returned();
 CHECK(fired[WP_SEMTEX]==1 && fired[WP_KNIFE]==0 && ps.weapon==WP_SHOTGUN);
 CHECK(ps.ammo[WP_SEMTEX]==1 && !(ps.weaponAction&WA_RECOVERING));
 SemtexReady(WP_SHOTGUN); ps.ammo[WP_FLASH]=1; Hold();
 move.cmd.buttons=0; Step(1); Run(ps.weaponTime); Run(ps.weaponTime);
 CHECK(ps.weapon==WP_SHOTGUN && ps.weaponstate==WEAPON_RAISING);
 move.cmd.buttons=BUTTON_TACTICAL; Step(1); move.cmd.buttons=0;
 CHECK((ps.weaponAction&(WA_TYPE_MASK|WA_FIRED))==WA_TACTICAL);
 Returned();
 CHECK(fired[WP_SEMTEX]==1 && fired[WP_FLASH]==1 && ps.weapon==WP_SHOTGUN);
 CHECK(ps.ammo[WP_SEMTEX]==1 && !(ps.weaponAction&WA_RECOVERING));
 SemtexReady(WP_SHOTGUN); Hold();
 ps.stats[STAT_LADDER]=LADDER_ATTACHED; Step(1);
 CHECK(!(ps.weaponAction&WA_TYPE_MASK) && !fired[WP_SEMTEX]);
}
static playerState_t history[500];
static usercmd_t commands[500];
static void TestCorrectionReplay(void) {
 byte data[MAX_MSGLEN]; msg_t msg;
 playerState_t expected,decoded,zero;
 usercmd_t cmdzero,cmddecoded;
 int i,j,lag;
 SemtexReady(WP_SHOTGUN); memset(&zero,0,sizeof(zero)); memset(&cmdzero,0,sizeof(cmdzero));
 history[0]=ps;
 for(i=1;i<500;i++) {
  memset(&commands[i],0,sizeof(commands[i])); commands[i].weapon=WP_SHOTGUN;
  commands[i].serverTime=i*8;
  if(i<100)commands[i].buttons=BUTTON_LETHAL;
  if(i==80)commands[i].buttons|=BUTTON_WEAPON_SWAP;
  if(i>110 && i<180)commands[i].buttons=BUTTON_LETHAL;
  if(i>=185)commands[i].weapon=WP_MACHINEGUN;
  if(i==185)commands[i].buttons=BUTTON_WEAPON_SWAP;
  move.cmd=commands[i]; Step(8); history[i]=ps;
 }
 // A delayed authoritative snapshot plus unacknowledged commands must yield
 // the same state, not duplicate throws or grant fake server reloads.
 for(lag=1;lag<=40;lag+=3) for(i=lag+1;i<500;i+=7) {
  MSG_Init(&msg,data,sizeof(data)); MSG_WriteDeltaPlayerstate(&msg,&zero,&history[i-lag]);
  MSG_BeginReading(&msg); MSG_ReadDeltaPlayerstate(&msg,&zero,&decoded); ps=decoded;
  for(j=i-lag+1;j<=i;j++) {
   MSG_Init(&msg,data,sizeof(data)); MSG_WriteDeltaUsercmdKey(&msg,123,&cmdzero,&commands[j]);
   MSG_BeginReading(&msg); MSG_ReadDeltaUsercmdKey(&msg,123,&cmdzero,&cmddecoded);
   CHECK(cmddecoded.buttons==commands[j].buttons);
   move.cmd=cmddecoded; Step(8);
  }
  expected=history[i]; CHECK(!memcmp(&ps,&expected,sizeof(ps)));
 }
}
int main(void) {
 TestSemtex(); TestCorrectionReplay();
 printf("PASS: %d Semtex hold/cancel/note/prediction checks\n",checks); return 0;
}
