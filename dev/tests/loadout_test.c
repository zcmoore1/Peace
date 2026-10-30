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
 cl.perk[1]=BG_PERK_COUNT; CHECK(!BG_ValidateClass(&cl));
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
/* Melee is its own kind of swap. Whatever gun is out, it goes down in the one
 * universal BG_RAPID_DROP_TIME - never its own holster - and asking for melee
 * part way through a heavy gun's holster finishes that holster as a melee one.
 * Equipment and ordinary swaps keep the gun's own holster. Every time here is
 * read from the shipped constants and tables. */
static void TestMeleeHolster(void) {
 int i, n = BG_ClassWeaponCount(), t;
 for (i = 0; i < n; i++) {
  int gun = BG_ClassWeapon(i), other = gun == WP_MACHINEGUN ? WP_SHOTGUN : WP_MACHINEGUN;
  if (gun <= WP_NONE) continue;
  /* melee: the universal holster, not the gun's */
  Ready(gun, other);
  move.cmd.buttons = BUTTON_MELEE; Step(1); move.cmd.buttons = 0;
  CHECK(ps.weaponstate == WEAPON_DROPPING);
  CHECK(ps.weaponTime == BG_RAPID_DROP_TIME && BG_HolsterTime(&ps) == BG_RAPID_DROP_TIME);
  for (t = 0; t < BG_RAPID_DROP_TIME + BG_WeaponRaiseTime(WP_KNIFE) + 5 && !fired[WP_KNIFE]; t++) Step(1);
  CHECK(fired[WP_KNIFE] == 1);
  /* equipment: the gun's quick drop (MW2's OffHandInit), not its holster */
  Ready(gun, other);
  move.cmd.buttons = BUTTON_LETHAL; Step(1); move.cmd.buttons = 0;
  CHECK(ps.weaponTime == BG_WeaponQuickDropTime(gun) && BG_HolsterTime(&ps) == BG_WeaponQuickDropTime(gun));
  /* an ordinary swap: the gun's own holster */
  Ready(gun, other);
  move.cmd.weapon = other; Step(1);
  CHECK(ps.weaponTime == BG_WeaponDropTime(gun));
  /* a knife asked for during that holster, or the raise after it, is lost
   * (MW2 refuses a knife in any raise or drop): the swap runs on at its own
   * times, and only a press once the gun is up knifes */
  Run(1);
  {
   int left = ps.weaponTime;
   move.cmd.buttons = BUTTON_MELEE; Step(1); move.cmd.buttons = 0;
   CHECK(!(ps.weaponAction & WA_TYPE_MASK) && ps.weaponstate == WEAPON_DROPPING &&
    ps.weaponTime < left && ps.weaponTime >= left - 1);
  }
  for (t = 0; t < 20000 && !(ps.weapon == other && ps.weaponstate == WEAPON_READY); t++) {
   if (ps.weaponstate == WEAPON_RAISING && t % 7 == 0) {
    move.cmd.buttons = BUTTON_MELEE; Step(1); move.cmd.buttons = 0;
    CHECK(!(ps.weaponAction & WA_TYPE_MASK));
   } else Step(1);
  }
  CHECK(ps.weapon == other && ps.weaponstate == WEAPON_READY && !fired[WP_KNIFE]);
  Step(1);	/* the last refused press let go */
  move.cmd.buttons = BUTTON_MELEE; Step(1); move.cmd.buttons = 0;
  CHECK((ps.weaponAction & WA_TYPE_MASK) == WA_MELEE);
  for (t = 0; t < BG_RAPID_DROP_TIME + BG_WeaponRaiseTime(WP_KNIFE) + 5 && !fired[WP_KNIFE]; t++) Step(1);
  CHECK(fired[WP_KNIFE] == 1);
  /* a knife pressed on the command YY returns the drop to the held gun is
   * taken, as MW2's weapon change readies the gun before its knife */
  Ready(gun, other);
  move.cmd.weapon = other; Step(1); Run(1);
  CHECK(ps.weaponstate == WEAPON_DROPPING);
  move.cmd.weapon = gun; move.cmd.buttons = BUTTON_MELEE; Step(1); move.cmd.buttons = 0;
  CHECK((ps.weaponAction & WA_TYPE_MASK) == WA_MELEE);
  for (t = 0; t < BG_WeaponDropTime(gun) + BG_WeaponRaiseTime(WP_KNIFE) + 5 && !fired[WP_KNIFE]; t++) Step(1);
  CHECK(fired[WP_KNIFE] == 1);
 }
 /* a ladder holster keeps its own universal time */
 Ready(WP_SHOTGUN, WP_MACHINEGUN);
 ps.stats[STAT_LADDER] = LADDER_HOLSTER;
 CHECK(BG_HolsterTime(&ps) == BG_LADDER_DROP_TIME);
}

/* Recovering from melee and from equipment is ONE thing, in time and function:
 * the knife or grenade is held BG_QUICK_ACTION_HOLD, goes away in the universal
 * BG_RAPID_DROP_TIME, then the gun comes back with its own normal draw - MW2's
 * recovery is the normal draw, except you cannot sprint through it. YY cancels
 * the recovery draw like any draw, and a quick action pressed during it starts
 * the next one. Every time comes from the shipped constants and tables. */
static int sprintNow;
static void RStep(void) {
 pml.groundPlane = qtrue;
 PM_CheckSprint(); sprintNow = !!(ps.pm_flags & PMF_SPRINTING);
 Step(1);
}
static int RecoverPhases(int gun, int button, int special, int *hold, int *down, int *draw) {
 int t, act, d = -1, u = -1, back = -1, other = gun == WP_MACHINEGUN ? WP_SHOTGUN : WP_MACHINEGUN;
 Ready(gun, other);
 if(special==WP_SEMTEX) {
  bg_class_t cl=*BG_Class(0);
  cl.slot[0]=gun; cl.slot[1]=other; cl.lethal=WP_SEMTEX;
  BG_ApplyClass(&ps,&cl);
 }
 move.cmd.buttons = button;
 for (t = 1; t < 9000 && !fired[special]; t++) { RStep(); if (t == 1) move.cmd.buttons = 0; }
 CHECK(fired[special] == 1);
 act = t;
 move.cmd.buttons = BUTTON_SPRINT; move.cmd.forwardmove = 127;     /* asking to sprint throughout */
 for (; t < 12000 && back < 0; t++) {
  int w = ps.weapon;
  RStep();
  if (back < 0) CHECK(!sprintNow);                                   /* never while recovering */
  if (d < 0 && w == special && ps.weaponstate == WEAPON_DROPPING) d = t;
  if (u < 0 && ps.weapon == gun && ps.weaponstate == WEAPON_RAISING) u = t;
  if (ps.weapon == gun && ps.weaponstate != WEAPON_RAISING && ps.weaponstate != WEAPON_DROPPING) back = t;
 }
 CHECK(d > 0 && u > 0 && back > 0);
 *hold = d - act; *down = u - d; *draw = back - u;
 for (t = 0; t < 5 && !sprintNow; t++) RStep();                     /* and then sprint is allowed */
 CHECK(sprintNow);
 move.cmd.buttons = 0; move.cmd.forwardmove = 0;
 return back - act;
}
static void TestRecovery(void) {
 const int buttons[] = { BUTTON_MELEE, BUTTON_LETHAL, BUTTON_TACTICAL, BUTTON_LETHAL };
 const int specials[] = { WP_KNIFE, WP_FRAG, WP_FLASH, WP_SEMTEX };
 int i, a, n = BG_ClassWeaponCount();
 for (i = 0; i < n; i++) {
  int gun = BG_ClassWeapon(i), first = -1;
  if (gun <= WP_NONE) continue;
  for (a = 0; a < ARRAY_LEN(specials); a++) {
   int hold, down, draw, total = RecoverPhases(gun, buttons[a], specials[a], &hold, &down, &draw);
   CHECK(hold >= BG_QUICK_ACTION_HOLD - 1 && hold <= BG_QUICK_ACTION_HOLD + 1);
   CHECK(down == BG_RAPID_DROP_TIME);
   CHECK(draw == BG_WeaponQuickRaiseTime(gun));	/* MW2's quick raise */
   if (first < 0) first = total;
   CHECK(total == first);                         /* all quick actions recover alike */
  }
 }
 /* YY cancels the recovery draw, like any draw */
 {
  int t;
  Ready(WP_SHOTGUN, WP_MACHINEGUN);
  move.cmd.buttons = BUTTON_MELEE; RStep(); move.cmd.buttons = 0;
  for (t = 0; t < 9000 && !(ps.weapon == WP_SHOTGUN && ps.weaponstate == WEAPON_RAISING); t++) RStep();
  CHECK(ps.weaponstate == WEAPON_RAISING && ps.weaponTime > 2);
  move.cmd.weapon = WP_MACHINEGUN; RStep();
  /* a real swap out of the recovery draw is a normal holster, not melee's */
  CHECK(ps.weaponstate == WEAPON_DROPPING && ps.weaponTime == BG_WeaponDropTime(WP_SHOTGUN));
  move.cmd.weapon = WP_SHOTGUN; RStep();
  CHECK(ps.weapon == WP_SHOTGUN && ps.weaponstate == WEAPON_READY);
 }
 /* MW2's knife check skips the gun's return from a quick action: a knife
  * pressed during the recovery draw, or on the command that draw ends, is
  * lost. A grenade pressed there still starts the next throw, and a knife
  * once the gun is back is a knife. */
 {
  int t, when;
  for (when = 0; when < 3; when++) {
   Ready(WP_SHOTGUN, WP_MACHINEGUN);
   move.cmd.buttons = BUTTON_LETHAL; RStep(); move.cmd.buttons = 0;
   for (t = 0; t < 9000 && !(ps.weapon == WP_SHOTGUN && ps.weaponstate == WEAPON_RAISING); t++) RStep();
   CHECK(ps.weapon == WP_SHOTGUN && (ps.weaponAction & WA_TYPE_MASK));
   if (when) {	/* to the command the latch clears on, or the one after it */
    for (t = 0; t < 9000; t++) {
     playerState_t save = ps;
     RStep();
     if (!(ps.weaponAction & WA_TYPE_MASK)) { if (when == 1) ps = save; break; }
    }
   }
   move.cmd.buttons = BUTTON_MELEE; RStep(); move.cmd.buttons = 0;
   for (t = 0; t < 3000; t++) RStep();
   CHECK(fired[WP_KNIFE] == (when == 2));
  }
  Ready(WP_SHOTGUN, WP_MACHINEGUN);
  move.cmd.buttons = BUTTON_LETHAL; RStep(); move.cmd.buttons = 0;
  for (t = 0; t < 9000 && !(ps.weapon == WP_SHOTGUN && ps.weaponstate == WEAPON_RAISING); t++) RStep();
  ps.ammo[WP_FLASH] = 1;
  move.cmd.buttons = BUTTON_TACTICAL; RStep(); move.cmd.buttons = 0;
  CHECK((ps.weaponAction & (WA_TYPE_MASK | WA_FIRED)) == WA_TACTICAL);
 }
 /* A swap asked for during the recovery draw is an ordinary holster, in which
  * a knife is lost (MW2 refuses a knife in any drop); once YY has cancelled
  * the draw the gun is back, so a knife on the very next command is a knife. */
 {
  int t, yy;
  for (yy = 0; yy < 2; yy++) {
   Ready(WP_SHOTGUN, WP_MACHINEGUN);
   move.cmd.buttons = BUTTON_LETHAL; RStep(); move.cmd.buttons = 0;
   for (t = 0; t < 9000 && !(ps.weapon == WP_SHOTGUN && ps.weaponstate == WEAPON_RAISING); t++) RStep();
   for (t = 0; t < 200; t++) RStep();
   move.cmd.weapon = WP_MACHINEGUN; RStep();
   CHECK(ps.weaponstate == WEAPON_DROPPING && ps.weaponTime > BG_RAPID_DROP_TIME);
   if (yy) { move.cmd.weapon = WP_SHOTGUN; RStep(); CHECK(ps.weapon == WP_SHOTGUN && ps.weaponstate == WEAPON_READY); }
   else for (t = 0; t < 100; t++) RStep();
   move.cmd.buttons = BUTTON_MELEE; RStep(); move.cmd.buttons = 0;
   for (t = 0; t < 4000; t++) RStep();
   CHECK(fired[WP_KNIFE] == yy);
  }
 }
}

static void SprintStep(int msec) {
 pml.groundPlane=qtrue; move.cmd.forwardmove=127;
 PM_CheckSprint(); Step(msec);
}
/* One think with chosen movement, sprint held unless 'held' is clear. */
static void SprintMove(int held, int forward, int up, int ground) {
 move.cmd.buttons=held ? BUTTON_SPRINT : 0;
 move.cmd.forwardmove=forward; move.cmd.upmove=up; pml.groundPlane=ground;
 PM_CheckSprint(); Step(1);
 move.cmd.upmove=0;
}
static void TestSprintTransitions(void) {
 const int equipment[]={WP_KNIFE,WP_FRAG,WP_FLASH,WP_SEMTEX};
 const int buttons[]={BUTTON_MELEE,BUTTON_LETHAL,BUTTON_TACTICAL};
 int i,ammo;
 for(i=0;i<BG_ClassWeaponCount();i++) {
  int weapon=BG_ClassWeapon(i), other=BG_ClassWeapon((i+1)%BG_ClassWeaponCount());
  Ready(weapon,other); ammo=ps.ammo[weapon];
  move.cmd.buttons=BUTTON_SPRINT; SprintStep(1);
  CHECK(ps.weaponstate==WEAPON_SPRINT_IN && ps.weaponTime==BG_WeaponSprintInTime(weapon));
  SprintStep(BG_WeaponSprintInTime(weapon)/2);
  Toggle(); SprintStep(1);
  CHECK(ps.weaponstate==WEAPON_DROPPING && ps.weaponTime==BG_WeaponDropTime(weapon));
  Toggle(); move.cmd.buttons=BUTTON_ATTACK; SprintStep(1);
  CHECK(fired[weapon]==1 && ps.ammo[weapon]==ammo-1);
  CHECK(!(ps.pm_flags&PMF_SPRINTING));
  /* Keeping sprint held still requests sprint; presentation cannot erase the
   * real sprint lock. Releasing input allows ordinary same-think YY firing. */
  Ready(weapon,other); move.cmd.buttons=BUTTON_SPRINT; SprintStep(1);
  Toggle(); SprintStep(1); Toggle(); SprintStep(1);
  CHECK(ps.weaponstate==WEAPON_SPRINT_IN && ps.weaponTime==BG_WeaponSprintInTime(weapon));
  CHECK(fired[weapon]==0 && (ps.pm_flags&PMF_SPRINTING));
 }
 Ready(WP_SHOTGUN,WP_MACHINEGUN);
 ps.stats[STAT_UNCHAMBERED]=1<<WP_SHOTGUN;
 move.cmd.buttons=BUTTON_SPRINT; SprintStep(1); Toggle(); SprintStep(1);
 Toggle(); move.cmd.buttons=BUTTON_ATTACK; SprintStep(1);
 CHECK(fired[WP_SHOTGUN]==0 && ps.weaponstate==WEAPON_BOLTING);
 CHECK(ps.stats[STAT_UNCHAMBERED]&(1<<WP_SHOTGUN));
 /* Action press arrives after PM_CheckSprint. It must clear sprint in that
  * very think, not leave a one-frame opening for equipment retention. */
 for(i=0;i<ARRAY_LEN(buttons);i++) {
  Ready(WP_MACHINEGUN,WP_SHOTGUN); move.cmd.buttons=BUTTON_SPRINT; SprintStep(1);
  move.cmd.buttons|=buttons[i]; SprintStep(1);
  CHECK(ps.weaponAction&WA_TYPE_MASK);
  CHECK(!(ps.pm_flags&PMF_SPRINTING));
 }
 /* MW2: a reload press, use/reload's included, ends a sprint and the reload
  * comes straight out of the rise; the sprint then waits for its own button
  * to come up, and a fresh sprint press cancels the reload as ever. */
 for(i=0;i<2;i++) {
  int t;
  Ready(WP_MACHINEGUN,WP_SHOTGUN); ps.ammo[WP_MACHINEGUN]=5;
  move.cmd.buttons=BUTTON_SPRINT; for(t=0;t<500;t++) SprintStep(1);
  CHECK(ps.weaponstate==WEAPON_SPRINTING && (ps.pm_flags&PMF_SPRINTING));
  move.cmd.buttons=BUTTON_SPRINT|(i ? BUTTON_USE_RELOAD : BUTTON_RELOAD); SprintStep(1);
  CHECK(!(ps.pm_flags&PMF_SPRINTING));
  if(i) {	/* use/reload: the server's request arrives on the next command */
   CHECK(ps.weaponstate==WEAPON_SPRINT_OUT);
   ps.weapFlags|=PWF_USE_RELOAD; SprintStep(1);
  }
  CHECK(ps.weaponstate==WEAPON_RELOADING && ps.weaponAnimSeq==RSEQ_START);
  move.cmd.buttons=BUTTON_SPRINT; for(t=0;t<200;t++) SprintStep(1);
  CHECK(ps.weaponstate==WEAPON_RELOADING && !(ps.pm_flags&PMF_SPRINTING));
  move.cmd.buttons=0; SprintStep(1); move.cmd.buttons=BUTTON_SPRINT; SprintStep(1);
  CHECK((ps.pm_flags&PMF_SPRINTING) && ps.weaponstate==WEAPON_SPRINT_IN);
 }
 /* MW2: any sprint that ends while its button is held waits for the button
  * to come up - letting go of forward, jump pressed in the air, a crouch, a
  * knife. (A jump off the floor does not wait: the ladder suite drives that
  * through the real jump.) Leaving the ground without a jump ends no sprint
  * in MW2: it goes on in the air. */
 for(i=0;i<5;i++) {
  int t;
  Ready(WP_MACHINEGUN,WP_SHOTGUN);
  for(t=0;t<300;t++) SprintMove(1,127,0,1);
  CHECK(ps.pm_flags&PMF_SPRINTING);
  if(i==0) SprintMove(1,0,0,1);						/* forward let go */
  else if(i==1) SprintMove(1,127,127,0);			/* jump pressed in the air */
  else if(i==2) { ps.pm_flags|=PMF_DUCKED; SprintMove(1,127,0,1); ps.pm_flags&=~PMF_DUCKED; }
  else if(i==3) {									/* knife, all the way back */
   move.cmd.buttons=BUTTON_SPRINT|BUTTON_MELEE; move.cmd.forwardmove=127; pml.groundPlane=qtrue;
   PM_CheckSprint(); Step(1);
   for(t=0;t<9000 && (ps.weaponAction&WA_TYPE_MASK);t++) SprintMove(1,127,0,1);
  } else { SprintMove(1,127,0,0); CHECK(ps.pm_flags&PMF_SPRINTING); continue; }	/* off a ledge: it goes on */
  CHECK(!(ps.pm_flags&PMF_SPRINTING));
  for(t=0;t<100;t++) SprintMove(1,127,0,1);
  CHECK(!(ps.pm_flags&PMF_SPRINTING));
  SprintMove(0,127,0,1); SprintMove(1,127,0,1);
  CHECK(ps.pm_flags&PMF_SPRINTING);
 }
 /* A reload pressed in the air after walking off a ledge ends that sprint
  * for good: landing with sprint held does not cancel the reload. */
 for(i=0;i<2;i++) {
  int t;
  Ready(WP_MACHINEGUN,WP_SHOTGUN); ps.ammo[WP_MACHINEGUN]=5;
  for(t=0;t<300;t++) SprintMove(1,127,0,1);
  for(t=0;t<50;t++) SprintMove(1,127,0,0);
  CHECK(ps.pm_flags&PMF_SPRINTING);
  move.cmd.buttons=BUTTON_SPRINT|(i ? BUTTON_USE_RELOAD : BUTTON_RELOAD); move.cmd.forwardmove=127; pml.groundPlane=qfalse;
  PM_CheckSprint(); Step(1);
  if(i) { ps.weapFlags|=PWF_USE_RELOAD; SprintMove(1,127,0,0); }
  CHECK(ps.weaponstate==WEAPON_RELOADING && !(ps.pm_flags&PMF_SPRINTING));
  for(t=0;t<4000;t++) SprintMove(1,127,0,1);
  CHECK(!(ps.pm_flags&PMF_SPRINTING) && ps.ammo[WP_MACHINEGUN]==BG_WeaponMagSize(WP_MACHINEGUN));
 }
 /* A reload button held across many commands keeps a sprint from starting:
  * pressing sprint while a held reload plays does not cancel it. */
 for(i=0;i<2;i++) {
  int t;
  Ready(WP_MACHINEGUN,WP_SHOTGUN); ps.ammo[WP_MACHINEGUN]=5;
  move.cmd.forwardmove=127; pml.groundPlane=qtrue;
  move.cmd.buttons=BUTTON_RELOAD; PM_CheckSprint(); Step(1);
  CHECK(ps.weaponstate==WEAPON_RELOADING);
  for(t=0;t<300;t++) {
   move.cmd.buttons=BUTTON_SPRINT|(i ? BUTTON_USE_RELOAD : BUTTON_RELOAD); PM_CheckSprint(); Step(1);
  }
  CHECK(!(ps.pm_flags&PMF_SPRINTING) && ps.weaponstate==WEAPON_RELOADING);
 }
 /* MW2: the trigger ends a sprint too, and the gun fires once it is back up
  * if the trigger is still held; a pull let go during the rise fires nothing. */
 for(i=0;i<2;i++) {
  int t;
  Ready(WP_MACHINEGUN,WP_SHOTGUN);
  for(t=0;t<300;t++) SprintMove(1,127,0,1);
  CHECK(ps.weaponstate==WEAPON_SPRINTING);
  move.cmd.buttons=BUTTON_SPRINT|BUTTON_ATTACK; move.cmd.forwardmove=127; pml.groundPlane=qtrue;
  PM_CheckSprint(); Step(1);
  CHECK(!(ps.pm_flags&PMF_SPRINTING) && ps.weaponstate==WEAPON_SPRINT_OUT && !fired[WP_MACHINEGUN]);
  for(t=0;t<BG_WeaponSprintOutTime(WP_MACHINEGUN)+20;t++) {
   move.cmd.buttons=BUTTON_SPRINT|(i ? 0 : BUTTON_ATTACK); PM_CheckSprint(); Step(1);
  }
  CHECK(!(ps.pm_flags&PMF_SPRINTING) && (fired[WP_MACHINEGUN]>0)==(i==0));
 }
 /* The knife and equipment buttons end a sprint whether or not anything comes
  * out, and a held one keeps a sprint from starting; so does too little
  * forward (MW2's player_sprintForwardMinimum). */
 for(i=0;i<3;i++) {
  int t;
  Ready(WP_MACHINEGUN,WP_SHOTGUN); ps.ammo[WP_FRAG]=ps.ammo[WP_SEMTEX]=ps.ammo[WP_FLASH]=0;
  for(t=0;t<100;t++) SprintMove(1,127,0,1);
  CHECK(ps.pm_flags&PMF_SPRINTING);
  move.cmd.buttons=BUTTON_SPRINT|(i==0 ? BUTTON_LETHAL : i==1 ? BUTTON_TACTICAL : BUTTON_MELEE);
  move.cmd.forwardmove=127; pml.groundPlane=qtrue; PM_CheckSprint(); Step(1);
  CHECK(!(ps.pm_flags&PMF_SPRINTING) && (ps.weapFlags&PWF_SPRINT_RELEASE));
 }
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 move.cmd.buttons=BUTTON_SPRINT|BUTTON_MELEE; move.cmd.forwardmove=127; pml.groundPlane=qtrue;
 PM_CheckSprint(); CHECK(!(ps.pm_flags&PMF_SPRINTING));
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 SprintMove(1,BG_SPRINT_FORWARD_MINIMUM,0,1); CHECK(!(ps.pm_flags&PMF_SPRINTING));
 SprintMove(1,BG_SPRINT_FORWARD_MINIMUM+1,0,1); CHECK(ps.pm_flags&PMF_SPRINTING);
 /* A reload button held keeps a sprint from starting, full magazine or not. */
 for(i=0;i<2;i++) {
  Ready(WP_MACHINEGUN,WP_SHOTGUN);
  move.cmd.buttons=BUTTON_SPRINT|(i ? BUTTON_USE_RELOAD : BUTTON_RELOAD); SprintStep(1);
  CHECK(!(ps.pm_flags&PMF_SPRINTING) && ps.weaponstate==WEAPON_READY);
  move.cmd.buttons=BUTTON_SPRINT; SprintStep(1);
  CHECK(ps.pm_flags&PMF_SPRINTING);	/* never sprinting, so nothing to release */
 }
 /* Once the action latch has cleared for return, equipment is still in hand
  * during its drop. Classification, not the latch alone, must block sprint. */
 for(i=0;i<ARRAY_LEN(equipment);i++) {
  Ready(WP_MACHINEGUN,WP_SHOTGUN); ps.weapon=equipment[i];
  ps.weaponAction=0; ps.weaponstate=WEAPON_DROPPING;
  move.cmd.buttons=BUTTON_SPRINT; move.cmd.forwardmove=127; pml.groundPlane=qtrue;
  PM_CheckSprint(); CHECK(!(ps.pm_flags&PMF_SPRINTING));
 }
}
/* MW2's knife waits out the rechamber's bolt-close: a press while the action
 * is still open is lost, and one after WNOTE_BOLT_CLOSED cuts the rest of the
 * cycle short, leaving no debt. Pump and bolt alike. */
/* MW2's sprint time: BG_SPRINT_TIME used while sprinting, refilled 1:1, and a
 * start needs more than BG_SPRINT_MIN_TIME left. Spent with the button held,
 * the sprint also waits for the button to come up. (Each SprintMove is 1ms.) */
static void TestSprintTime(void) {
 int t;
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 for(t=0;t<BG_SPRINT_TIME+5 && (t<2 || (ps.pm_flags&PMF_SPRINTING));t++) SprintMove(1,127,0,1);
 CHECK(!(ps.pm_flags&PMF_SPRINTING) && t>=BG_SPRINT_TIME && t<=BG_SPRINT_TIME+3);
 CHECK(ps.weapFlags&PWF_SPRINT_RELEASE);
 SprintMove(0,127,0,1); SprintMove(1,127,0,1); CHECK(!(ps.pm_flags&PMF_SPRINTING));
 for(t=0;t<BG_SPRINT_MIN_TIME-4;t++) SprintMove(0,127,0,1);
 SprintMove(1,127,0,1); CHECK(!(ps.pm_flags&PMF_SPRINTING));
 for(t=0;t<10;t++) SprintMove(0,127,0,1);
 SprintMove(1,127,0,1); CHECK(ps.pm_flags&PMF_SPRINTING);
 /* What it started with is what it has: about a second and a bit. */
 for(t=0;t<3000 && (ps.pm_flags&PMF_SPRINTING);t++) SprintMove(0,127,0,1);
 CHECK(t>BG_SPRINT_MIN_TIME && t<BG_SPRINT_MIN_TIME+40);
 /* A rest refills it. */
 for(t=0;t<BG_SPRINT_TIME;t++) SprintMove(0,127,0,1);
 CHECK(ps.sprintUsed==0);
}
/* MW2's start gate skips the knife and equipment states in the air after a
 * jump: the sprint starts, the quick action ends it that think, and a held
 * button then waits to come up. Off a ledge (no jump) nothing starts. */
static void TestAirSprintDuringAction(void) {
 int i;
 for(i=0;i<2;i++) {
  Ready(WP_MACHINEGUN,WP_SHOTGUN);
  move.cmd.buttons=BUTTON_MELEE; move.cmd.forwardmove=127; pml.groundPlane=qtrue;
  PM_CheckSprint(); Step(1);
  CHECK((ps.weaponAction&WA_TYPE_MASK)==WA_MELEE);
  ps.jumpLand=i ? -1 : 0;
  SprintMove(1,127,0,0);
  CHECK(!(ps.pm_flags&PMF_SPRINTING) && !!(ps.weapFlags&PWF_SPRINT_RELEASE)==i);
 }
}
/* Equipment cuts the knife at any point, as MW2's offhand check takes every
 * melee state: pressed while the gun goes down for the knife or while the
 * knife comes up, the knife never lands; pressed after it, the throw follows. */
static void TestEquipmentCutsKnife(void) {
 int stage,t,lethal;
 for(stage=0;stage<3;stage++) {
  Ready(WP_MACHINEGUN,WP_SHOTGUN); lethal=BG_LethalWeapon(&ps);
  CHECK(ps.ammo[lethal]>0);
  move.cmd.buttons=BUTTON_MELEE; Step(1); move.cmd.buttons=0;
  CHECK((ps.weaponAction&WA_TYPE_MASK)==WA_MELEE && ps.weaponstate==WEAPON_DROPPING);
  if(stage>=1) for(t=0;t<5000 && !(ps.weapon==WP_KNIFE && ps.weaponstate==WEAPON_RAISING);t++) Step(1);
  if(stage==2) for(t=0;t<5000 && !fired[WP_KNIFE];t++) Step(1);
  CHECK(fired[WP_KNIFE]==(stage==2));
  move.cmd.buttons=BUTTON_LETHAL; Step(1); move.cmd.buttons=0;
  CHECK((ps.weaponAction&WA_TYPE_MASK)==WA_LETHAL);
  /* The knife in hand goes away on the gun's quick drop (MW2's OffHandInit). */
  if(stage==1) CHECK(ps.weapon==WP_KNIFE && ps.weaponstate==WEAPON_DROPPING &&
   ps.weaponTime==BG_WeaponQuickDropTime(WP_MACHINEGUN));
  for(t=0;t<12000 && (ps.weaponAction&WA_TYPE_MASK);t++) Step(1);
  CHECK(fired[WP_KNIFE]==(stage==2) && fired[lethal]==1);
 }
}
/* A knife pressed during the knife is lost: MW2's knife check refuses its
 * own melee states, so one press is one knife. */
static void TestKnifeInKnife(void) {
 int t;
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 move.cmd.buttons=BUTTON_MELEE; Step(1); move.cmd.buttons=0;
 for(t=0;t<12000 && (ps.weaponAction&WA_TYPE_MASK);t++) {
  if(t%5==0) { move.cmd.buttons=BUTTON_MELEE; Step(1); move.cmd.buttons=0; }
  else Step(1);
 }
 CHECK(t<12000 && fired[WP_KNIFE]==1);
}
/* Equipment pressed on the knife's own command wins, as MW2 checks its
 * offhands before its knife. */
static void TestEquipmentBeatsKnife(void) {
 const int buttons[]={BUTTON_LETHAL,BUTTON_TACTICAL};
 const int actions[]={WA_LETHAL,WA_TACTICAL};
 int i;
 for(i=0;i<ARRAY_LEN(buttons);i++) {
  Ready(WP_MACHINEGUN,WP_SHOTGUN);
  CHECK(ps.ammo[i ? WP_FLASH : BG_LethalWeapon(&ps)]>0);
  move.cmd.buttons=BUTTON_MELEE|buttons[i]; Step(1); move.cmd.buttons=0;
  CHECK((ps.weaponAction&WA_TYPE_MASK)==actions[i]);
 }
}
static void TestKnifeInBolt(void) {
 const int guns[]={WP_SHOTGUN,WP_M40A3};
 int t,i;
 for(i=0;i<ARRAY_LEN(guns);i++) {
  int gun=guns[i];
  Ready(gun,WP_MACHINEGUN);
  move.cmd.buttons=BUTTON_ATTACK; Step(1); move.cmd.buttons=0;
  CHECK(fired[gun]==1);
  for(t=0;t<5000 && ps.weaponstate!=WEAPON_BOLTING;t++) Step(1);
  CHECK(ps.weaponstate==WEAPON_BOLTING && (ps.stats[STAT_UNCHAMBERED]&(1<<gun)));
  move.cmd.buttons=BUTTON_MELEE; Step(1); move.cmd.buttons=0; Step(1);
  CHECK(!(ps.weaponAction&WA_TYPE_MASK) && ps.weaponstate==WEAPON_BOLTING);
  for(t=0;t<5000 && (ps.stats[STAT_UNCHAMBERED]&(1<<gun));t++) Step(1);
  CHECK(ps.weaponstate==WEAPON_BOLTING);	/* the cycle's tail, after the close */
  move.cmd.buttons=BUTTON_MELEE; Step(1); move.cmd.buttons=0;
  CHECK((ps.weaponAction&WA_TYPE_MASK)==WA_MELEE);
  for(t=0;t<9000 && (ps.weaponAction&WA_TYPE_MASK);t++) Step(1);
  CHECK(fired[WP_KNIFE]==1 && !(ps.stats[STAT_UNCHAMBERED]&(1<<gun)));
 }
}
/* MW2's sprint latches (PC): a press starts it, it goes on with the button let
 * go or held, and a fresh press ends it; a press still held from that end
 * starts nothing. Aiming ends a sprint and keeps one from starting, and
 * holding the sprint button keeps an unscoped gun from aiming. */
static void AimMove(int buttons) {
 move.cmd.buttons=buttons; move.cmd.forwardmove=127; move.cmd.upmove=0; pml.groundPlane=qtrue;
 PM_CheckSprint(); PM_CheckADS(); Step(1);
}
static void TestSprintLatch(void) {
 int t,i;
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 SprintMove(1,127,0,1); CHECK(ps.pm_flags&PMF_SPRINTING);
 for(t=0;t<300;t++) SprintMove(0,127,0,1);
 CHECK((ps.pm_flags&PMF_SPRINTING) && ps.weaponstate==WEAPON_SPRINTING);
 SprintMove(1,127,0,1); CHECK(!(ps.pm_flags&PMF_SPRINTING));
 for(t=0;t<100;t++) SprintMove(1,127,0,1);
 CHECK(!(ps.pm_flags&PMF_SPRINTING));
 SprintMove(0,127,0,1); SprintMove(1,127,0,1); CHECK(ps.pm_flags&PMF_SPRINTING);
 for(t=0;t<300;t++) SprintMove(1,127,0,1);
 CHECK(ps.pm_flags&PMF_SPRINTING);
 /* Ended with its button up, a sprint waits for nothing but a press. */
 SprintMove(0,127,0,1); SprintMove(0,0,0,1); CHECK(!(ps.pm_flags&PMF_SPRINTING));
 SprintMove(0,127,0,1); CHECK(!(ps.pm_flags&PMF_SPRINTING));
 SprintMove(1,127,0,1); CHECK(ps.pm_flags&PMF_SPRINTING);
 /* A jump or deep water ends a latched sprint for good; a press, in the air
  * as well, starts the next one. */
 for(i=0;i<2;i++) {
  Ready(WP_MACHINEGUN,WP_SHOTGUN);
  SprintMove(1,127,0,1); SprintMove(0,127,0,1); CHECK(ps.pm_flags&PMF_SPRINTING);
  if(i) { move.waterlevel=2; SprintMove(0,127,0,1); move.waterlevel=0; }
  else SprintMove(0,127,127,0);
  CHECK(!(ps.pm_flags&PMF_SPRINTING));
  for(t=0;t<100;t++) SprintMove(0,127,0,0);
  CHECK(!(ps.pm_flags&PMF_SPRINTING));
  SprintMove(1,127,0,0); CHECK(ps.pm_flags&PMF_SPRINTING);
 }
 /* The aim button ends a latched sprint and aims that same think, as MW2's
  * ADS check ignores the sprint's weapon states; the sprint-out still holds
  * the trigger until the gun is up. */
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 SprintMove(1,127,0,1); for(t=0;t<300;t++) SprintMove(0,127,0,1);
 CHECK(ps.weaponstate==WEAPON_SPRINTING);
 AimMove(BUTTON_ADS);
 CHECK(!(ps.pm_flags&PMF_SPRINTING) && (ps.pm_flags&PMF_ADS) && ps.weaponstate==WEAPON_SPRINT_OUT);
 AimMove(BUTTON_ADS|BUTTON_ATTACK);
 CHECK(!fired[WP_MACHINEGUN] && ps.weaponstate==WEAPON_SPRINT_OUT);
 for(t=0;t<BG_WeaponSprintOutTime(WP_MACHINEGUN)+20 && !fired[WP_MACHINEGUN];t++)
  AimMove(BUTTON_ADS|BUTTON_ATTACK);
 CHECK(fired[WP_MACHINEGUN]==1 && (ps.pm_flags&PMF_ADS));
 for(t=0;t<500;t++) AimMove(BUTTON_ADS);
 /* Sprint pressed while aiming: no sprint, and no aim while it is held. */
 for(t=0;t<50;t++) {
  AimMove(BUTTON_ADS|BUTTON_SPRINT);
  CHECK(!(ps.pm_flags&(PMF_SPRINTING|PMF_ADS)));
 }
 AimMove(BUTTON_ADS); CHECK((ps.pm_flags&PMF_ADS) && !(ps.pm_flags&PMF_SPRINTING));
 /* Aim let go with sprint held: never sprinting, so nothing to release. */
 AimMove(BUTTON_ADS|BUTTON_SPRINT); AimMove(BUTTON_SPRINT);
 CHECK(ps.pm_flags&PMF_SPRINTING);
 /* Aim ends a sprint whose button is held from its start; that sprint then
  * waits for the button to come up. */
 for(t=0;t<300;t++) AimMove(BUTTON_SPRINT);
 AimMove(BUTTON_ADS|BUTTON_SPRINT); CHECK(!(ps.pm_flags&PMF_SPRINTING));
 for(t=0;t<100;t++) AimMove(BUTTON_SPRINT);
 CHECK(!(ps.pm_flags&(PMF_SPRINTING|PMF_ADS)));
 AimMove(0); AimMove(BUTTON_SPRINT); CHECK(ps.pm_flags&PMF_SPRINTING);
 /* The scoped M300 aims with the sprint button held (MW2's breath); an
  * unscoped gun at rest does not. */
 for(i=0;i<2;i++) {
  Ready(i ? WP_M40A3 : WP_MACHINEGUN,WP_SHOTGUN);
  CHECK(ps.weaponstate==WEAPON_READY);
  for(t=0;t<20;t++) AimMove(BUTTON_ADS|BUTTON_SPRINT);
  CHECK(!(ps.pm_flags&PMF_SPRINTING) && !!(ps.pm_flags&PMF_ADS)==i);
 }
}
static void TestMovementScale(void) {
 const int flags[]={0,PMF_SPRINTING,PMF_ADS};
 int i,j;
 float scale,normal;
 Ready(WP_MACHINEGUN,WP_SHOTGUN); ps.speed=320; pml.walking=qtrue;
 move.cmd.forwardmove=127; normal=PM_CmdScale(&move.cmd);
 for(i=0;i<ARRAY_LEN(flags);i++) {
  ps.pm_flags=flags[i]; scale=PM_CmdScale(&move.cmd);
  if(i==1) CHECK(scale>normal);
  if(i==2) CHECK(scale<normal);
  /* Prediction may replay many commands from one snapshot; no command may
   * compound the modifier into the authoritative base speed. */
  for(j=0;j<100;j++) {
   CHECK(fabs(PM_CmdScale(&move.cmd)-scale)<.0001f);
   CHECK(ps.speed==320);
  }
 }
 move.cmd.forwardmove=0; ps.pm_flags=PMF_SPRINTING;
 CHECK(PM_CmdScale(&move.cmd)==0 && ps.speed==320);
 /* Sprint speed is the walk's: off the ground it adds nothing (MW2). */
 move.cmd.forwardmove=127; ps.pm_flags=0; normal=PM_CmdScale(&move.cmd);
 pml.walking=qfalse; ps.pm_flags=PMF_SPRINTING;
 CHECK(fabs(PM_CmdScale(&move.cmd)-normal)<.0001f);
}
/* Full Pmove reaches the respawn gate; PM_Weapon alone cannot cover it. */
static void RespawnTrace(trace_t *tr, const vec3_t start, const vec3_t mins,
 const vec3_t maxs, const vec3_t end, int pass, int mask) {
 (void)start; (void)mins; (void)maxs; (void)pass; (void)mask;
 memset(tr,0,sizeof(*tr)); tr->fraction=1;
 VectorCopy(end,tr->endpos); tr->entityNum=ENTITYNUM_NONE;
}
static int RespawnContents(const vec3_t point, int pass) {
 (void)point; (void)pass; return 0;
}
static void RespawnMove(int buttons, int type) {
 move.cmd.buttons=buttons; move.cmd.serverTime=ps.commandTime+8;
 ps.pm_type=type; Pmove(&move);
}
/* MW2 has no ground test: a sprint starts and runs in the air, but its speed
 * is the walk's only, so the fall itself is no faster; it pays off on landing. */
static void TestAirSprint(void) {
 float speed[2]; int s,t;
 for(s=0;s<2;s++) {
  Ready(WP_MACHINEGUN,WP_SHOTGUN);
  move.trace=RespawnTrace; move.pointcontents=RespawnContents;
  move.tracemask=MASK_PLAYERSOLID; ps.speed=320; ps.gravity=800; ps.pm_type=PM_NORMAL;
  ps.origin[2]=4000; ps.velocity[0]=320; ps.groundEntityNum=ENTITYNUM_NONE;
  move.cmd.forwardmove=127; move.cmd.upmove=0;
  for(t=0;t<400;t+=8) RespawnMove(s ? BUTTON_SPRINT : 0,PM_NORMAL);
  CHECK(ps.groundEntityNum==ENTITYNUM_NONE && !!(ps.pm_flags&PMF_SPRINTING)==s);
  speed[s]=sqrt(ps.velocity[0]*ps.velocity[0]+ps.velocity[1]*ps.velocity[1]);
 }
 CHECK(fabs(speed[1]-speed[0])<.01f);
 /* Holding jump keeps it from starting, as MW2's jump button does. */
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 move.trace=RespawnTrace; move.pointcontents=RespawnContents;
 move.tracemask=MASK_PLAYERSOLID; ps.speed=320; ps.gravity=800;
 ps.origin[2]=4000; ps.groundEntityNum=ENTITYNUM_NONE;
 move.cmd.forwardmove=127; move.cmd.upmove=127;
 for(t=0;t<200;t+=8) RespawnMove(BUTTON_SPRINT,PM_NORMAL);
 CHECK(!(ps.pm_flags&PMF_SPRINTING));
 move.cmd.upmove=0; RespawnMove(0,PM_NORMAL); RespawnMove(BUTTON_SPRINT,PM_NORMAL);
 CHECK(ps.pm_flags&PMF_SPRINTING);
 move.cmd.forwardmove=0;
}
/* MW2's air move: the stick alone sets the push - base speed, slowed while
 * aiming, never sprint or the jump button - and pushing against your motion
 * always takes speed away, at least as hard as at pm_stopspeed. One 50ms command
 * in open air from horizontal speed vx; Pmove truncates velocity, so these
 * pushes are whole. */
static float AirStep(int forward, int up, int flags, float vx) {
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 move.trace=RespawnTrace; move.pointcontents=RespawnContents;
 move.tracemask=MASK_PLAYERSOLID; ps.speed=320; ps.gravity=800; ps.pm_type=PM_NORMAL;
 ps.origin[2]=4000; ps.velocity[0]=vx; ps.groundEntityNum=ENTITYNUM_NONE;
 ps.pm_flags|=flags|(up ? PMF_JUMP_HELD : 0);
 move.cmd.forwardmove=forward; move.cmd.rightmove=0; move.cmd.upmove=up; move.cmd.buttons=0;
 move.cmd.serverTime=ps.commandTime+50; Pmove(&move);
 move.cmd.forwardmove=move.cmd.upmove=0;
 return ps.velocity[0];
}
static void TestAirControl(void) {
 const int flags[]={0,PMF_SPRINTING,PMF_ADS};
 const float floor=100*.05f, light=320*10/127.f;
 int i,up;
 for(i=0;i<ARRAY_LEN(flags);i++) for(up=0;up<2;up++) {
  const float push=(flags[i]==PMF_ADS ? (int)(320*pm_adsScale) : 320)*.05f;
  CHECK(push>floor);
  CHECK(AirStep(127,up ? 127 : 0,flags[i],0)==push);
  CHECK(AirStep(-127,up ? 127 : 0,flags[i],448)==448-push);	/* pull back off a sprint jump */
  CHECK(AirStep(127,up ? 127 : 0,flags[i],448)==448);			/* forward: already faster */
 }
 CHECK(light>floor);
 CHECK(AirStep(10,0,0,0)==floor);			/* a light stick pushes at stopspeed... */
 CHECK(AirStep(-10,0,0,448)==448-floor);	/* ...and brakes at it */
 /* A ladder keeps the plain rate, as MW2's ladder branch of accelerate does. */
 {
  vec3_t up={0,0,1}; const float wish=25, rate=wish*.05f*BG_LADDER_ACCELERATE;
  Ready(WP_MACHINEGUN,WP_SHOTGUN); pml.frametime=.05f;
  CHECK(wish<pm_stopspeed && rate<wish);
  ps.stats[STAT_LADDER]|=LADDER_ATTACHED; VectorClear(ps.velocity);
  PM_Accelerate(up,wish,BG_LADDER_ACCELERATE);
  CHECK(fabs(ps.velocity[2]-rate)<.001f);
 }
}
/* Why a sprint trick mid-jump throws the jump off (MW2's own effect): aimed
 * in, the air push is cut; a sprint press cancels an unscoped gun's aim, so
 * from the next command the air pushes at the full base speed. */
static void TestSprintCancelsAirAim(void) {
 float v;
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 move.trace=RespawnTrace; move.pointcontents=RespawnContents;
 move.tracemask=MASK_PLAYERSOLID; ps.speed=320; ps.gravity=800; ps.pm_type=PM_NORMAL;
 ps.origin[2]=4000; ps.groundEntityNum=ENTITYNUM_NONE;
 move.cmd.forwardmove=127; move.cmd.upmove=0;
 RespawnMove(BUTTON_ADS,PM_NORMAL);
 CHECK(ps.pm_flags&PMF_ADS);
 v=ps.velocity[0]; move.cmd.serverTime=ps.commandTime+50; Pmove(&move);
 CHECK((ps.pm_flags&PMF_ADS) && ps.velocity[0]-v==(int)(320*pm_adsScale)*.05f);
 move.cmd.buttons=BUTTON_ADS|BUTTON_SPRINT;
 move.cmd.serverTime=ps.commandTime+50; Pmove(&move);
 CHECK(!(ps.pm_flags&(PMF_ADS|PMF_SPRINTING)));
 v=ps.velocity[0]; move.cmd.serverTime=ps.commandTime+50; Pmove(&move);
 CHECK(ps.velocity[0]-v==320*.05f);
 move.cmd.forwardmove=0; move.cmd.buttons=0;
}
/* Off the ground during a landing's slowdown, the jump state is cleared. */
static void TestLedgeClearsLanding(void) {
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 move.trace=RespawnTrace; move.pointcontents=RespawnContents;
 move.tracemask=MASK_PLAYERSOLID; ps.speed=320; ps.gravity=800; ps.pm_type=PM_NORMAL;
 ps.origin[2]=4000; ps.groundEntityNum=ENTITYNUM_NONE; ps.jumpLand=900; ps.jumpOriginZ=1;
 RespawnMove(0,PM_NORMAL);
 CHECK(ps.jumpLand==0 && ps.jumpOriginZ==0);
 ps.jumpLand=-1; RespawnMove(0,PM_NORMAL);
 CHECK(ps.jumpLand==-1);
}
/* MW2's camera bob (BG_GetPlayerViewOrigin): its curve and amplitudes, the
 * 8-unit cap, and the snap - the cycle holds still in the air, so a sprint
 * switching there moves the eye. Its cadence (PM_Footsteps): speed over the
 * posture's most, times MW2's rate, truncated each think. */
static void TestViewBob(void) {
 const float speed=320, c0=BG_ViewBobCycle(&ps);
 float c,v,h,vs,hs;
 int i,steps,before;
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 ps.bobCycle=0; CHECK(fabs(BG_ViewBobCycle(&ps)-2*M_PI)<1e-4f);
 ps.bobCycle=255; CHECK(fabs(BG_ViewBobCycle(&ps)-4*M_PI)<1e-4f);
 (void)c0;
 ps.bobCycle=40; c=BG_ViewBobCycle(&ps);
 v=BG_VerticalViewBob(&ps,c,speed,0); h=BG_HorizontalViewBob(&ps,c,speed,0);
 CHECK(fabs(h-sin(c)*0.007f*speed)<1e-3f);
 CHECK(fabs(v-(sin(2*c)+0.2f*sin(4*c+M_PI/2))*0.75f*0.007f*speed)<1e-3f);
 ps.pm_flags|=PMF_SPRINTING;
 vs=BG_VerticalViewBob(&ps,c,speed,0); hs=BG_HorizontalViewBob(&ps,c,speed,0);
 CHECK(fabs(hs-sin(c)*0.02f*speed)<1e-3f && fabs(hs-h)>1);			/* the snap */
 CHECK(fabs(vs-v*0.014f/0.007f)<1e-3f);
 CHECK(fabs(BG_HorizontalViewBob(&ps,c,1000,0)-sin(c)*8)<1e-3f);		/* capped */
 ps.pm_flags=PMF_DUCKED;
 CHECK(fabs(BG_HorizontalViewBob(&ps,c,speed,0)-sin(c)*0.0075f*speed)<1e-3f);
 /* Cadence: an 8ms think at the posture's full speed. */
 for(i=0;i<5;i++) {
  const int flags[]={0,PMF_SPRINTING,PMF_DUCKED,PMF_ADS,PMF_BACKWARDS_RUN};
  const float rate[]={0.335f,0.5f,0.34f,0.305f,0.36f};
  const float most[]={1,1.4f,pm_duckScale,pm_adsScale,1};
  Ready(WP_MACHINEGUN,WP_SHOTGUN); ps.speed=320; ps.groundEntityNum=0; ps.pm_flags=flags[i];
  move.cmd.forwardmove=i==4 ? -127 : 127; ps.velocity[0]=320*most[i]; ps.bobCycle=10;
  pml.msec=100; PM_Footsteps();	/* long enough that each rate truncates apart */
  CHECK(ps.bobCycle==10+(int)(rate[i]*100));
 }
 /* Held still too slow to bob, and in the air; reset once stopped. */
 ps.velocity[0]=9.5f; ps.bobCycle=50; pml.msec=1000; PM_Footsteps(); CHECK(ps.bobCycle==50);
 pml.msec=8;
 ps.velocity[0]=320; ps.groundEntityNum=ENTITYNUM_NONE; PM_Footsteps(); CHECK(ps.bobCycle==50);
 ps.groundEntityNum=0; ps.velocity[0]=0; move.cmd.forwardmove=0; PM_Footsteps(); CHECK(ps.bobCycle==0);
 /* Over a second of running the cycle keeps MW2's per-think truncation. */
 Ready(WP_MACHINEGUN,WP_SHOTGUN); ps.speed=320; ps.groundEntityNum=0; ps.velocity[0]=320;
 move.cmd.forwardmove=127; pml.msec=8; ps.bobCycle=0; before=0;
 for(steps=0;steps<100;steps++) { PM_Footsteps(); before+=(int)(0.335f*8); }
 CHECK(ps.bobCycle==(before&255));
 move.cmd.forwardmove=0;
}
/* A flat floor at z=0, for full Pmove on the ground. */
static void FloorTrace(trace_t *tr, const vec3_t start, const vec3_t mins,
 const vec3_t maxs, const vec3_t end, int pass, int mask) {
 float s=start[2]+mins[2], e=end[2]+mins[2];
 (void)maxs; (void)pass; (void)mask;
 memset(tr,0,sizeof(*tr)); tr->fraction=1; VectorCopy(end,tr->endpos); tr->entityNum=ENTITYNUM_NONE;
 if (s<-0.01f) { tr->startsolid=qtrue; tr->allsolid=e<0; tr->fraction=0; VectorCopy(start,tr->endpos);
  tr->entityNum=ENTITYNUM_WORLD; tr->plane.normal[2]=1; return; }
 if (e<0) {
  float f=(s-0.03125f)/(s-e); if (f<0) f=0;
  tr->fraction=f; tr->endpos[0]=start[0]+f*(end[0]-start[0]);
  tr->endpos[1]=start[1]+f*(end[1]-start[1]); tr->endpos[2]=start[2]+f*(end[2]-start[2]);
  tr->plane.normal[2]=1; tr->entityNum=ENTITYNUM_WORLD; tr->contents=CONTENTS_SOLID;
 }
}
static void FloorMove(int buttons, int up) {
 move.cmd.buttons=buttons; move.cmd.forwardmove=127; move.cmd.upmove=up;
 move.cmd.serverTime=ps.commandTime+8; Pmove(&move);
 /* The airborne sprint under a quick action never outlives its think. */
 CHECK(!((ps.pm_flags&PMF_SPRINTING) && ((ps.weaponAction&WA_TYPE_MASK) || BG_IsActionWeapon(ps.weapon))));
}
/* Knife, jump, let jump go in the air with sprint held - also with the use-item
 * button, which returns from PM_Weapon before its action update runs. */
static void TestAirSprintEndsSameThink(void) {
 int extra,t;
 for(extra=0;extra<2;extra++) {
  Ready(WP_MACHINEGUN,WP_SHOTGUN);
  move.trace=FloorTrace; move.pointcontents=RespawnContents; move.tracemask=MASK_PLAYERSOLID;
  ps.speed=320; ps.gravity=800; ps.pm_type=PM_NORMAL; ps.origin[2]=24; ps.groundEntityNum=ENTITYNUM_WORLD;
  for(t=0;t<20;t++) FloorMove(BUTTON_SPRINT,0);
  FloorMove(BUTTON_SPRINT|BUTTON_MELEE,0); FloorMove(BUTTON_SPRINT,0);
  CHECK((ps.weaponAction&WA_TYPE_MASK)==WA_MELEE);
  FloorMove(BUTTON_SPRINT,127); CHECK(ps.jumpLand==-1);
  FloorMove(BUTTON_SPRINT|(extra ? BUTTON_USE_HOLDABLE : 0),0);
  CHECK(ps.weapFlags&PWF_SPRINT_RELEASE);
  for(t=0;t<300;t++) FloorMove(BUTTON_SPRINT,0);
 }
 move.cmd.forwardmove=0;
}
static int AllWater(const vec3_t point, int pass) { (void)point; (void)pass; return CONTENTS_WATER; }
/* A jump that comes down in deep water ends there: no slowdown waits for the walk out. */
static void TestWaterEndsJump(void) {
 Ready(WP_MACHINEGUN,WP_SHOTGUN);
 move.trace=RespawnTrace; move.pointcontents=AllWater; move.tracemask=MASK_PLAYERSOLID;
 ps.speed=320; ps.gravity=800; ps.pm_type=PM_NORMAL; ps.origin[2]=4000; ps.groundEntityNum=ENTITYNUM_NONE;
 ps.jumpLand=-1; ps.jumpOriginZ=5; RespawnMove(0,PM_NORMAL);
 CHECK(ps.jumpLand==0 && ps.jumpOriginZ==0);
}
static void TestHeldThroughRespawn(void) {
 const int buttons[]={BUTTON_MELEE,BUTTON_LETHAL,BUTTON_TACTICAL,BUTTON_LETHAL};
 const int specials[]={WP_KNIFE,WP_FRAG,WP_FLASH,WP_SEMTEX};
 const int actions[]={WA_MELEE,WA_LETHAL,WA_TACTICAL,WA_LETHAL};
 int i,mode,t,count,holdMs;
 for(i=0;i<ARRAY_LEN(buttons);i++) for(mode=0;mode<3;mode++) {
  Ready(WP_MACHINEGUN,WP_SHOTGUN);
  if(specials[i]==WP_SEMTEX) {
   bg_class_t cl=*BG_Class(0);
   cl.slot[0]=WP_MACHINEGUN; cl.slot[1]=WP_SHOTGUN; cl.lethal=WP_SEMTEX;
   BG_ApplyClass(&ps,&cl);
  }
  move.trace=RespawnTrace; move.pointcontents=RespawnContents;
  move.tracemask=MASK_PLAYERSOLID; ps.speed=320; ps.gravity=800;
  ps.pm_flags|=PMF_RESPAWNED; count=ps.ammo[specials[i]];
  /* Ordinary spawn, frozen countdown, and attack delaying the spawn gate. */
  if(mode==1) RespawnMove(buttons[i],PM_FREEZE);
  if(mode==2) {
   RespawnMove(buttons[i]|BUTTON_ATTACK,PM_NORMAL);
   CHECK(ps.pm_flags&PMF_RESPAWNED);
  }
  RespawnMove(buttons[i],PM_NORMAL);
  CHECK(!(ps.pm_flags&PMF_RESPAWNED));
  CHECK(!(ps.weaponAction&WA_TYPE_MASK));
  holdMs=BG_WeaponDropTime(WP_MACHINEGUN)+BG_WeaponRaiseTime(specials[i])+
   BG_QUICK_ACTION_HOLD+BG_RAPID_DROP_TIME+BG_WeaponRaiseTime(WP_MACHINEGUN);
  for(t=0;t<holdMs;t+=8) RespawnMove(buttons[i],PM_NORMAL);
  CHECK(!(ps.weaponAction&WA_TYPE_MASK));
  CHECK(ps.weapon==WP_MACHINEGUN && ps.ammo[specials[i]]==count);
  /* Release and press again: this is a new action, not a permanently blocked button. */
  RespawnMove(0,PM_NORMAL); RespawnMove(buttons[i],PM_NORMAL);
  CHECK((ps.weaponAction&WA_TYPE_MASK)==actions[i]);
 }
}
int main(void) {
 TestClasses(); TestYY(); TestReloadTail(); TestActions(); TestSprintTransitions();
 TestSprintLatch(); TestSprintTime(); TestAirSprintDuringAction(); TestKnifeInBolt(); TestEquipmentBeatsKnife(); TestEquipmentCutsKnife(); TestKnifeInKnife(); TestMeleeHolster(); TestRecovery();
 TestMovementScale(); TestAirSprint(); TestAirControl(); TestSprintCancelsAirAim(); TestLedgeClearsLanding(); TestViewBob(); TestAirSprintEndsSameThink(); TestWaterEndsJump(); TestHeldThroughRespawn();
 printf("PASS: %d class, YY and quick-action checks\n",checks);
 return 0;
}
