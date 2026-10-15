/* Actual shared pmove: the rifle is data on the existing note/slot engine. */
#include "../../code/game/bg_pmove.c"
static playerState_t ps;
static pmove_t move;
static int checks, shots;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_Error(int level,const char *fmt,...) {
 va_list a; (void)level; va_start(a,fmt); vfprintf(stderr,fmt,a); va_end(a); exit(1);
}
void trap_SnapVector(float *v) { SnapVector(v); }
static void Step(int ms) {
 int seq=ps.eventSequence;
 pml.msec=ms; move.cmd.serverTime+=ms; ps.commandTime=move.cmd.serverTime;
 PM_Weapon(); PM_CheckADS();
 for(;seq<ps.eventSequence;seq++) if(ps.events[seq&(MAX_PS_EVENTS-1)]==EV_FIRE_WEAPON) shots++;
}
static void Run(int ms) { while(ms-->0) Step(1); }
static int Note(int seq,int kind) {
 const bg_weaponNote_t *n=PM_SegNotes(WP_M40A3,seq);
 for(;n->note;n++) if(n->note==kind) return n->time;
 return -1;
}
static void Ready(void) {
 bg_class_t cl=*BG_Class(0); cl.slot[0]=WP_M40A3; cl.slot[1]=WP_MACHINEGUN;
 memset(&ps,0,sizeof(ps)); memset(&move,0,sizeof(move)); memset(&pml,0,sizeof(pml));
 BG_ApplyClass(&ps,&cl); ps.stats[STAT_HEALTH]=100; ps.groundEntityNum=ENTITYNUM_WORLD;
 move.ps=&ps; move.cmd.weapon=BG_SLOT_COMMAND; pm=&move; pml.groundPlane=qtrue; shots=0;
}
static void TestCatalog(void) {
 bg_weaponInstance_t gun; entityState_t item;
 Ready(); CHECK(!strcmp(BG_ClassWeaponName(WP_M40A3),"M300 Praetorian"));
 CHECK(BG_FindItemForWeapon(WP_M40A3)->giTag==WP_M40A3);
 CHECK(ps.ammo[WP_M40A3]==BG_WeaponMagSize(WP_M40A3));
 CHECK(ps.ammoReserve[WP_M40A3]==BG_WeaponMaxReserve(WP_M40A3));
 CHECK(BG_WeaponAttachmentMask(WP_M40A3)==0);
 gun.weapon=WP_M40A3; gun.ammo=2; gun.reserve=7; gun.attachments=BG_ATTACHMENT_REDDOT;
 gun.flags=BG_GUN_UNCHAMBERED|BG_GUN_FIRST_DRAW; BG_PutSlot(&ps,0,&gun);
 BG_GetSlot(&ps,0,&gun); CHECK(gun.ammo==2 && gun.reserve==7 && gun.attachments==0);
 CHECK(ps.stats[STAT_UNCHAMBERED]&(1<<WP_M40A3));
 memset(&item,0,sizeof(item)); item.eType=ET_ITEM; item.modelindex=BG_FindItemForWeapon(WP_M40A3)-bg_itemlist;
 item.modelindex2=1; item.time2=2|(7<<16); item.otherEntityNum2=BG_GUN_UNCHAMBERED;
 CHECK(BG_WeaponItem(&item,&gun)); CHECK(gun.ammo==2 && gun.reserve==7 && (gun.flags&BG_GUN_UNCHAMBERED));
}
static void TestCycle(void) {
 int open=Note(ASEQ_FIRE,WNOTE_BOLT_OPEN), close=Note(ASEQ_FIRE,WNOTE_BOLT_CLOSED), end=BG_WeaponFireLength(WP_M40A3);
 Ready(); move.cmd.buttons=BUTTON_ATTACK; Step(1);
 CHECK(shots==1 && ps.ammo[WP_M40A3]==BG_WeaponMagSize(WP_M40A3)-1);
 CHECK(ps.stats[STAT_UNCHAMBERED]&(1<<WP_M40A3));
 Run(open); CHECK(ps.weaponstate==WEAPON_BOLTING && shots==1);
 Run(close-open); CHECK(ps.weaponstate==WEAPON_BOLTING && ps.weaponTime==0);
 CHECK(!(ps.stats[STAT_UNCHAMBERED]&(1<<WP_M40A3)));
 Run(end-close-1); CHECK(shots==1); Run(2); CHECK(shots==2);
 /* Miss the close note: stowing cannot forgive chamber debt. */
 Ready(); move.cmd.buttons=BUTTON_ATTACK; Step(1); move.cmd.buttons=0; Run(open);
 move.cmd.weapon=BG_SLOT_COMMAND+1; Step(1);
 Run(BG_WeaponDropTime(WP_M40A3)+BG_WeaponRaiseTime(WP_MACHINEGUN)+1);
 CHECK(ps.weapon==WP_MACHINEGUN && ps.weaponstate==WEAPON_READY);
 CHECK(ps.stats[STAT_UNCHAMBERED]&(1<<WP_M40A3));
 move.cmd.weapon=BG_SLOT_COMMAND; Step(1);
 Run(BG_WeaponDropTime(WP_MACHINEGUN)+BG_WeaponRaiseTime(WP_M40A3)+1);
 CHECK(ps.weapon==WP_M40A3 && ps.weaponstate==WEAPON_BOLTING);
 CHECK(ps.weaponAnimTime<=1 && shots==1);
 /* A note colliding with the holster naturally clears that lock. */
 Ready(); move.cmd.buttons=BUTTON_ATTACK; Step(1); move.cmd.buttons=0; Run(close-1);
 move.cmd.weapon=BG_SLOT_COMMAND+1; Step(1);
 CHECK(ps.weapon==WP_MACHINEGUN && ps.weaponstate==WEAPON_RAISING);
 CHECK(!(ps.stats[STAT_UNCHAMBERED]&(1<<WP_M40A3)));
}
static void TestReload(void) {
 int start=BG_WeaponReloadSegLength(WP_M40A3,RSEQ_START), loop=BG_WeaponReloadSegLength(WP_M40A3,RSEQ_LOOP);
 int seat=Note(RSEQ_LOOP,WNOTE_MAG_IN), before, i;
 Ready(); ps.ammo[WP_M40A3]=1; ps.ammoReserve[WP_M40A3]=9; before=10;
 move.cmd.buttons=BUTTON_RELOAD|BUTTON_ADS; Step(1); move.cmd.buttons=BUTTON_ADS;
 for(i=0;i<10000 && ps.weaponstate!=WEAPON_READY;i++) {
  Step(1); CHECK(ps.ammo[WP_M40A3]+ps.ammoReserve[WP_M40A3]==before);
  if(ps.weaponstate==WEAPON_RELOADING) CHECK(!(ps.pm_flags&PMF_ADS));
 }
 CHECK(i<10000 && ps.ammo[WP_M40A3]==BG_WeaponMagSize(WP_M40A3));
 Ready(); ps.ammo[WP_M40A3]=1; move.cmd.buttons=BUTTON_RELOAD; Step(1); move.cmd.buttons=0;
 Run(start+seat-1); CHECK(ps.ammo[WP_M40A3]==1);
 move.cmd.weapon=BG_SLOT_COMMAND+1; Step(1);
 CHECK(ps.weapon==WP_MACHINEGUN && ps.weaponstate==WEAPON_RAISING);
 CHECK(ps.ammo[WP_M40A3]==1); /* finish-before-fill order survives */
 CHECK(loop>seat);
}
static void TestSelection(void) {
 int i, lengths[]={1,8,16,33,50};
 for(i=0;i<ARRAY_LEN(lengths);i++) {
  Ready(); move.cmd.weapon=BG_SLOT_COMMAND+1; Step(lengths[i]);
  move.cmd.weapon=BG_SLOT_COMMAND; move.cmd.buttons=BUTTON_ATTACK; Step(lengths[i]);
  CHECK(shots==1 && ps.weapon==WP_M40A3);
 }
 Ready(); ps.slotFlags[0]|=BG_GUN_FIRST_DRAW; BG_BeginWeaponDraw(&ps);
 CHECK(ps.weaponDraw && ps.weaponTime==BG_WeaponFirstRaiseTime(WP_M40A3));
 CHECK(!(ps.slotFlags[0]&BG_GUN_FIRST_DRAW));
 move.cmd.weapon=BG_SLOT_COMMAND+1; Step(1); move.cmd.weapon=BG_SLOT_COMMAND;
 move.cmd.buttons=BUTTON_ATTACK; Step(1); CHECK(shots==1);
 Ready(); CHECK(BG_WeaponSpread(&ps)>0); ps.pm_flags|=PMF_ADS; CHECK(BG_WeaponSpread(&ps)==0);
 ps.velocity[0]=320; CHECK(BG_WeaponSpread(&ps)>0);
}
int main(void) {
 TestCatalog(); TestCycle(); TestReload(); TestSelection();
 printf("PASS: %d sniper catalog, bolt, reload, chamber debt, first-draw and YY checks\n",checks); return 0;
}
