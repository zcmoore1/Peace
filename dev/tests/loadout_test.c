/* Exercise real shared gameplay: pending selection, cancellation, quick actions
 * and untrusted saved class definitions. No test-only production entry points. */
#include "../../code/game/bg_pmove.c"

static playerState_t ps;
static pmove_t move;
static int checks, fired[MAX_WEAPONS];
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level, const char *fmt, ...) {
 va_list args; (void)level; va_start(args,fmt); vfprintf(stderr,fmt,args);
 va_end(args); exit(1);
}
void trap_SnapVector(float *v) { SnapVector(v); }

static void Ready(int primary, int secondary) {
 bg_class_t cl = *BG_Class(0);
 memset(&ps,0,sizeof(ps)); memset(&move,0,sizeof(move));
 memset(&pml,0,sizeof(pml)); memset(fired,0,sizeof(fired));
 cl.slot[0]=primary; cl.slot[1]=secondary;
 BG_ApplyClass(&ps,&cl);
 ps.stats[STAT_HEALTH]=100;
 move.ps=&ps; move.cmd.weapon=primary; pm=&move;
}
static void Step(int msec) {
 int seq=ps.eventSequence;
 pml.msec=msec; PM_Weapon();
 for (;seq<ps.eventSequence;seq++)
  if(ps.events[seq & (MAX_PS_EVENTS-1)]==EV_FIRE_WEAPON) fired[ps.weapon]++;
}
static void Run(int msec) { while(msec-- > 0) Step(1); }
static void Toggle(void) { move.cmd.weapon=BG_ToggleSlotWeapon(&ps,move.cmd.weapon); }
static void Returned(void) {
 int watchdog=10000; /* timeout only; no gameplay timing expectations */
 do { Step(1); } while (--watchdog && (ps.weapon!=move.cmd.weapon ||
  ps.weaponstate!=WEAPON_READY || (ps.weaponAction & WA_TYPE_MASK)));
 CHECK(watchdog>0);
}
static int Note(int weapon,int seq,int kind) {
 const bg_weaponNote_t *note=PM_SegNotes(weapon,seq);
 for(;note->note!=WNOTE_NONE;note++) if(note->note==kind) return note->time;
 return -1;
}

static void TestClasses(void) {
 bg_class_t cl, copy, before;
 char text[BG_CLASS_STRING_SIZE], small[2], bad[BG_CLASS_STRING_SIZE+1];
 int i,j,k;
 for(i=0;i<BG_ClassCount();i++) {
  cl=*BG_Class(i);
  CHECK(BG_ValidateClass(&cl));
  CHECK(BG_SerializeClass(&cl,text,sizeof(text)));
  CHECK(BG_ParseClass(text,&copy));
  CHECK(!memcmp(&cl,&copy,sizeof(cl)));
 }
 for(i=0;i<BG_ClassWeaponCount();i++) for(j=0;j<BG_ClassWeaponCount();j++) {
  cl=*BG_Class(0); cl.slot[0]=BG_ClassWeapon(i); cl.slot[1]=BG_ClassWeapon(j);
  CHECK(BG_ValidateClass(&cl)==(i!=j));
  if(i==j) continue;
  memset(&ps,0,sizeof(ps)); BG_ApplyClass(&ps,&cl);
  CHECK(ps.stats[STAT_SLOT_PRIMARY]==cl.slot[0]);
  CHECK(ps.stats[STAT_SLOT_SECONDARY]==cl.slot[1]);
  CHECK(ps.stats[STAT_WEAPONS]==((1<<cl.slot[0])|(1<<cl.slot[1])|
   (1<<WP_KNIFE)|(1<<WP_FRAG)|(1<<WP_FLASH)));
  CHECK(ps.ammo[cl.slot[0]]==BG_WeaponMagSize(cl.slot[0]));
  CHECK(ps.ammoReserve[cl.slot[1]]==BG_WeaponMaxReserve(cl.slot[1]));
  CHECK(ps.ammo[WP_KNIFE]==-1);
  CHECK(ps.weaponAction==0 && ps.weaponTime==0 && ps.weaponAnimTime==-1);
  /* Even with active slot/snapshot frozen, every pair wraps back. */
  k=cl.slot[0];
  CHECK((k=BG_ToggleSlotWeapon(&ps,k))==cl.slot[1]);
  CHECK(BG_ToggleSlotWeapon(&ps,k)==cl.slot[0]);
  BG_SetSlotWeapon(&ps,SLOT_PRIMARY,WP_KNIFE);
  CHECK(ps.stats[STAT_SLOT_PRIMARY]==cl.slot[0]);
 }
 cl=*BG_Class(0); cl.lethal=cl.tactical=WP_NONE; cl.lethalCount=cl.tacticalCount=0;
 CHECK(BG_ValidateClass(&cl));
 memset(&ps,0,sizeof(ps)); BG_ApplyClass(&ps,&cl);
 CHECK(!(ps.stats[STAT_WEAPONS]&((1<<WP_FRAG)|(1<<WP_FLASH))));
 CHECK(!BG_SerializeClass(&cl,small,sizeof(small)));
 cl.perk[1]=1; CHECK(!BG_ValidateClass(&cl));
 cl=*BG_Class(0); cl.slot[0]=WP_KNIFE; CHECK(!BG_ValidateClass(&cl));
 cl=*BG_Class(0); cl.lethalCount=4; CHECK(!BG_ValidateClass(&cl));
 cl=*BG_Class(0); cl.tacticalCount=0; CHECK(!BG_ValidateClass(&cl));
 cl=*BG_Class(0); memset(cl.name,'a',sizeof(cl.name)); CHECK(!BG_ValidateClass(&cl));
 cl=*BG_Class(0); strcpy(cl.name,"   "); CHECK(!BG_ValidateClass(&cl));
 cl=*BG_Class(0); strcpy(cl.name,"Trickshot 1-_ ");
 CHECK(BG_SerializeClass(&cl,text,sizeof(text)));
 CHECK(BG_ParseClass(text,&copy));
 CHECK(!strcmp(cl.name,copy.name));
 before=copy;
 for(i=0;i<(int)strlen(text);i++) {
  strcpy(bad,text); bad[i]='\\';
  CHECK(!BG_ParseClass(bad,&copy)); CHECK(!memcmp(&copy,&before,sizeof(copy)));
 }
 memset(bad,'1',sizeof(bad)-1); bad[sizeof(bad)-1]=0;
 CHECK(!BG_ParseClass(bad,&copy));
 CHECK(!BG_ParseClass("2|2|3|12|2|13|2|0|0|0|Name",&copy));
 CHECK(!BG_ParseClass("1|2|3|12|2|13|2|0|0|0|Name;quit",&copy));
 CHECK(!BG_ParseClass("1|-2|3|12|2|13|2|0|0|0|Name",&copy));
 CHECK(!BG_ParseClass("1|9999999999|3|12|2|13|2|0|0|0|Name",&copy));
}

static void TestYY(void) {
 int i, primary, secondary, ammo;
 for(i=0;i<BG_ClassWeaponCount();i++) {
  primary=BG_ClassWeapon(i);
  secondary=BG_ClassWeapon((i+1)%BG_ClassWeaponCount());
  Ready(primary,secondary); ammo=ps.ammo[primary];
  Toggle(); Step(1);
  CHECK(ps.weaponstate==WEAPON_DROPPING);
  CHECK(ps.weaponTime==BG_WeaponDropTime(primary));
  Run(BG_WeaponDropTime(primary)/2);
  Toggle(); Step(1);
  CHECK(ps.weapon==primary && ps.weaponstate==WEAPON_READY && ps.weaponTime==0);
  CHECK(ps.ammo[primary]==ammo);
  Toggle(); Step(1); Toggle(); move.cmd.buttons=BUTTON_ATTACK; Step(1);
  CHECK(fired[primary]==1 && ps.ammo[primary]==ammo-1);
  /* Let one swap finish, then reverse a drop DURING the new gun's raise. */
  Ready(primary,secondary); Toggle(); Step(1); Run(BG_WeaponDropTime(primary));
  CHECK(ps.weapon==secondary && ps.weaponstate==WEAPON_RAISING);
  Toggle(); Step(1); CHECK(ps.weaponstate==WEAPON_DROPPING);
  Toggle(); move.cmd.buttons=BUTTON_ATTACK; Step(1);
  CHECK(ps.weapon==secondary && fired[secondary]==1);
 }
 Ready(WP_SHOTGUN,WP_MACHINEGUN);
 ps.stats[STAT_UNCHAMBERED]=1<<WP_SHOTGUN;
 Toggle(); Step(1); Toggle(); move.cmd.buttons=BUTTON_ATTACK; Step(1);
 CHECK(fired[WP_SHOTGUN]==0 && ps.weaponstate==WEAPON_BOLTING);
 CHECK(ps.weaponAnimTime==0 && ps.weaponTime==BG_WeaponFireLength(WP_SHOTGUN));
 Ready(WP_MACHINEGUN,WP_SHOTGUN); ps.ammo[WP_MACHINEGUN]=ps.ammoReserve[WP_MACHINEGUN]=0;
 Toggle(); Step(1); Toggle(); move.cmd.buttons=BUTTON_ATTACK; Step(1);
 CHECK(fired[WP_MACHINEGUN]==0);
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 Toggle(); Step(1); Toggle(); ps.pm_flags|=PMF_SPRINTING;
 move.cmd.buttons=BUTTON_ATTACK; Step(1);
 CHECK(fired[WP_MACHINEGUN]==0 && ps.weaponstate==WEAPON_SPRINT_IN);
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 ps.stats[STAT_LADDER]=LADDER_ATTACHED; Step(1);
 Toggle(); Step(1); Toggle(); move.cmd.buttons=BUTTON_ATTACK; Step(1);
 CHECK(fired[WP_MACHINEGUN]==0 && (ps.stats[STAT_LADDER]&LADDER_HOLSTER));
}
static void TestReloadTail(void) {
 int seat=Note(WP_MACHINEGUN,RSEQ_LOOP,WNOTE_MAG_IN), loaded;
 CHECK(seat>0);
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 ps.ammo[WP_MACHINEGUN]=1; PM_BeginReload();
 Run(BG_WeaponReloadSegLength(WP_MACHINEGUN,RSEQ_START)+seat);
 CHECK(ps.weaponstate==WEAPON_RELOADING && ps.weaponTime==0);
 loaded=ps.ammo[WP_MACHINEGUN]; CHECK(loaded==BG_WeaponMagSize(WP_MACHINEGUN));
 Toggle(); Step(1); Toggle(); move.cmd.buttons=BUTTON_ATTACK; Step(1);
 CHECK(fired[WP_MACHINEGUN]==1 && ps.ammo[WP_MACHINEGUN]==loaded-1);
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 ps.ammo[WP_MACHINEGUN]=1; PM_BeginReload();
 Toggle(); Step(1); Toggle(); move.cmd.buttons=BUTTON_ATTACK; Step(1);
 CHECK(fired[WP_MACHINEGUN]==1 && ps.ammo[WP_MACHINEGUN]==0);
 CHECK(ps.ammoReserve[WP_MACHINEGUN]==BG_WeaponMaxReserve(WP_MACHINEGUN));
}
static void TestActions(void) {
 const int buttons[]={BUTTON_MELEE,BUTTON_LETHAL,BUTTON_TACTICAL};
 const int weapons[]={WP_KNIFE,WP_FRAG,WP_FLASH};
 int i,hold,count;
 for(i=0;i<3;i++) for(hold=0;hold<2;hold++) {
  Ready(WP_MACHINEGUN,WP_SHOTGUN); count=ps.ammo[weapons[i]];
  ps.pm_flags|=PMF_ADS;
  move.cmd.buttons=buttons[i]; Step(1);
  CHECK(!(ps.pm_flags&PMF_ADS));
  CHECK(ps.weaponstate==WEAPON_DROPPING);
  if(!hold) move.cmd.buttons=0;
  Returned();
  CHECK(fired[weapons[i]]==1 && fired[WP_MACHINEGUN]==0);
  CHECK(ps.ammo[weapons[i]]==(i ? count-1 : -1));
  Run(BG_WeaponDropTime(WP_MACHINEGUN)+BG_WeaponRaiseTime(weapons[i]));
  CHECK(fired[weapons[i]]==1);
  move.cmd.buttons=0; Step(1); move.cmd.buttons=buttons[i]; Step(1);
  move.cmd.buttons=0; Returned(); CHECK(fired[weapons[i]]==2);
 }
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 ps.ammo[WP_FRAG]=0; move.cmd.buttons=BUTTON_LETHAL; Step(1);
 CHECK(!(ps.weaponAction&WA_TYPE_MASK) && ps.weaponstate==WEAPON_READY);
 move.cmd.weapon=WP_KNIFE; move.cmd.buttons=0; Step(1);
 CHECK(ps.weapon==WP_MACHINEGUN && ps.weaponstate==WEAPON_READY);
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 move.cmd.buttons=BUTTON_MELEE; Step(1);
 ps.stats[STAT_LADDER]=LADDER_ATTACHED; Step(1);
 CHECK(!(ps.weaponAction&WA_TYPE_MASK) && fired[WP_KNIFE]==0);
}
int main(void) {
 TestClasses(); TestYY(); TestReloadTail(); TestActions();
 printf("PASS: %d class, YY and quick-action checks\n",checks);
 return 0;
}
