/* MW2's underbarrel shotgun (the Masterkey) and the UAV killstreak's trigger,
 * through the real PM_Weapon. Every time is read from the shipped tables and
 * the generated MW2 data (bg_masterkey_data.h); no timing literal is written
 * here. What each check asserts is MW2's rule from its executable, named where
 * it is checked. */
#include "../../code/game/bg_pmove.c"

static playerState_t ps;
static pmove_t move;
static int checks, fired[MAX_WEAPONS], used;
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level, const char *fmt, ...) {
 va_list args; (void)level; va_start(args,fmt); vfprintf(stderr,fmt,args);
 va_end(args); exit(1);
}
void trap_SnapVector(float *v) { v[0] = rintf(v[0]); v[1] = rintf(v[1]); v[2] = rintf(v[2]); }

#define HOST WP_MACHINEGUN
#define ALT WP_MASTERKEY
#define OTHER WP_SHOTGUN

static int killstreakToggle;
static void Select(int slot, qboolean alt) {
 move.cmd.weapon = BG_SelectionCommand(&ps, slot, alt, killstreakToggle);
}
/* A class with the Shotgun on its machinegun, the SPAS in the other slot. */
static void Ready(int attachments) {
 bg_class_t cl = *BG_Class(0);
 memset(&ps,0,sizeof(ps)); memset(&move,0,sizeof(move));
 memset(&pml,0,sizeof(pml)); memset(fired,0,sizeof(fired)); used=0; killstreakToggle=0;
 cl.slot[0]=HOST; cl.slot[1]=OTHER; cl.attachments[0]=attachments; cl.attachments[1]=0;
 cl.perk[0]=PERK_SECOND_NATURE;
 CHECK(BG_ValidateClass(&cl));
 BG_ApplyClass(&ps,&cl);
 ps.stats[STAT_HEALTH]=100;
 move.ps=&ps; pm=&move; Select(SLOT_PRIMARY,qfalse);
}
static void Step(int msec) {
 int seq=ps.eventSequence;
 pml.msec=msec; PM_Weapon();
 for (;seq<ps.eventSequence;seq++) {
  int ev=ps.events[seq & (MAX_PS_EVENTS-1)];
  if(ev==EV_FIRE_WEAPON) fired[ps.weapon]++;
  if(ev==EV_USE_KILLSTREAK) used++;
 }
}
static void Run(int ms, int msec) { while(ms>0) { Step(ms<msec?ms:msec); ms-=msec; } }
/* Run until the gun the selection asks for is up and ready (at least one think). */
static void Settle(int msec) {
 int watchdog=20000;
 do { Step(msec); } while(--watchdog && (ps.weaponstate!=WEAPON_READY || ps.weaponTime>0 ||
  ps.weapon!=PM_SelectedWeapon()));
 CHECK(watchdog>0);
}
/* The rifle switched to its underbarrel and up. */
static void OnAlt(int msec) {
 Select(SLOT_PRIMARY,qtrue); Step(msec); Settle(msec);
 CHECK(ps.weapon==ALT && BG_SlotAltActive(&ps,SLOT_PRIMARY));
}
static qboolean Owes(int weapon) { return (ps.stats[STAT_UNCHAMBERED] & (1<<weapon)) != 0; }
static int Note(int weapon,int seq,int kind) {
 const bg_weaponNote_t *note=PM_SegNotes(weapon,seq);
 for(;note->note!=WNOTE_NONE;note++) if(note->note==kind) return note->time;
 return -1;
}

static const int thinks[]={1,2,3,4,7,8,11,16,25,33,50};

/* ---- MW2's data: m4_shotgun_attach_mp and m4_shotgun_mp's alt fields ---- */
static void TestData(void) {
 int w;
#ifdef MISSIONPACK
 CHECK(WP_MASTERKEY==21 && WP_KILLSTREAK_UAV==22 && WP_NUM_WEAPONS==23);
#else
 CHECK(WP_MASTERKEY==18 && WP_KILLSTREAK_UAV==19 && WP_NUM_WEAPONS==20);
#endif
 CHECK(WP_NUM_WEAPONS<=MAX_WEAPONS && WP_NUM_WEAPONS<=32);	/* 5-bit ps.weapon, 32-bit masks */
 CHECK(BG_WeaponMagSize(ALT)==MK_MAG_SIZE && BG_WeaponMaxReserve(ALT)==MK_MAX_RESERVE);
 CHECK(BG_WeaponStartReserve(ALT)==MK_START_RESERVE && BG_WeaponStartReserve(HOST)==BG_WeaponMaxReserve(HOST));
 CHECK(BG_WeaponRaiseTime(ALT)==MK_RAISE_MS && BG_WeaponDropTime(ALT)==MK_DROP_MS);
 CHECK(BG_WeaponQuickRaiseTime(ALT)==MK_QUICK_RAISE_MS && BG_WeaponQuickDropTime(ALT)==MK_QUICK_DROP_MS);
 CHECK(BG_WeaponSprintInTime(ALT)==MK_SPRINT_IN_MS && BG_WeaponSprintOutTime(ALT)==MK_SPRINT_OUT_MS);
 CHECK(BG_WeaponAltRaiseTime(ALT)==MK_ALT_RAISE_MS && BG_WeaponAltRaiseTime(HOST)==MK_HOST_ALT_RAISE_MS);
 /* the fire cycle is MW2's shot and pump in one clip; the shell is chambered
  * when MW2's rechamber weaponDelay ends: rechamberBoltTime, or 1 ms when that
  * is 0 (575bd8-575c03), as this gun's is */
 CHECK(BG_WeaponFireLength(ALT)==MK_FIRE_MS);
 CHECK(Note(ALT,ASEQ_FIRE,WNOTE_BOLT_OPEN)==MK_BOLT_OPEN_MS);
 CHECK(Note(ALT,ASEQ_FIRE,WNOTE_BOLT_CLOSED)==MK_BOLT_OPEN_MS+1);
 CHECK(BG_WeaponReloadSegLength(ALT,RSEQ_START)==MK_RELOAD_START_MS && Note(ALT,RSEQ_START,WNOTE_MAG_IN)==MK_RELOAD_START_ADD_MS);
 CHECK(BG_WeaponReloadSegLength(ALT,RSEQ_LOOP)==MK_RELOAD_LOOP_MS && Note(ALT,RSEQ_LOOP,WNOTE_MAG_IN)==MK_ROUND_IN_MS);
 CHECK(BG_WeaponReloadSegLength(ALT,RSEQ_END)==MK_RELOAD_END_MS && BG_WeaponReload(ALT)->interruptible);
 /* only the machinegun takes the Shotgun; only it has an alt raise */
 for(w=WP_NONE+1;w<WP_NUM_WEAPONS;w++) {
  CHECK(!!(BG_WeaponAttachmentMask(w)&BG_ATTACHMENT_SHOTGUN)==(w==HOST));
  CHECK(BG_AltWeapon(w,BG_ATTACHMENT_SHOTGUN)==(w==HOST ? ALT : WP_NONE));
  CHECK(BG_AltWeapon(w,0)==WP_NONE && BG_IsAltWeapon(w)==(w==ALT));
  if(w!=HOST && w!=ALT) CHECK(BG_WeaponAltRaiseTime(w)==BG_WeaponRaiseTime(w));
 }
 /* the trigger: killstreak_uav_mp's raise and drop, no magazine, never on the floor */
 CHECK(BG_WeaponRaiseTime(WP_KILLSTREAK_UAV)==BG_UAV_TRIGGER_RAISE_MS && BG_WeaponDropTime(WP_KILLSTREAK_UAV)==BG_UAV_TRIGGER_DROP_MS);
 CHECK(BG_WeaponMagSize(WP_KILLSTREAK_UAV)==0 && BG_WeaponFireLength(WP_KILLSTREAK_UAV)==0 && !BG_IsActionWeapon(WP_KILLSTREAK_UAV));
 CHECK(BG_FindItemForWeapon(ALT)->giTag==ALT && BG_FindItemForWeapon(WP_KILLSTREAK_UAV)->giTag==WP_KILLSTREAK_UAV);
 /* MW2's pellets: shotCount, damage at maxDamageRange falling to minDamage at minDamageRange */
 CHECK(BG_WeaponPellets(ALT)==MK_PELLETS && BG_WeaponPellets(HOST)==0 && BG_WeaponPellets(OTHER)==0);
 CHECK(BG_PelletDamage(ALT,0)==MK_DAMAGE && BG_PelletDamage(ALT,MK_MAX_DAMAGE_RANGE)==MK_DAMAGE);
 CHECK(BG_PelletDamage(ALT,MK_MIN_DAMAGE_RANGE)==MK_MIN_DAMAGE && BG_PelletDamage(ALT,MK_MIN_DAMAGE_RANGE*4)==MK_MIN_DAMAGE);
 CHECK(BG_PelletDamage(ALT,(MK_MAX_DAMAGE_RANGE+MK_MIN_DAMAGE_RANGE)/2.0f)==(MK_DAMAGE+MK_MIN_DAMAGE)/2);
 CHECK(BG_PelletSpread(ALT,qfalse)==MK_HIP_SPREAD_DEG && BG_PelletSpread(ALT,qtrue)==MK_ADS_SPREAD_DEG);
 /* MW2's UAV and compass: killstreakTable's 3 kills, radarViewTime, the radar dvars */
 CHECK(BG_UAV_KILLS==3 && BG_UAV_TIME==30000 && BG_RADAR_UPDATE_TIME==4000 && BG_FAST_RADAR_UPDATE_TIME==2000);
 CHECK(BG_RADAR_PING_FADE_TIME==4000 && BG_COMPASS_MAX_RANGE==2500);
}

/* ---- the class: the Shotgun is an attachment of the machinegun ---- */
static void TestClass(void) {
 bg_weaponInstance_t gun;
 Ready(BG_ATTACHMENT_SHOTGUN);
 CHECK(ps.weapon==HOST && ps.slotAttachments[0]==BG_ATTACHMENT_SHOTGUN);
 CHECK(ps.stats[STAT_WEAPONS] & (1<<ALT));
 CHECK(ps.ammo[ALT]==MK_MAG_SIZE && ps.ammoReserve[ALT]==MK_START_RESERVE);	/* MW2: a full tube and startAmmo */
 CHECK(ps.slotAltAmmo[0]==MK_MAG_SIZE && ps.slotAltReserve[0]==MK_START_RESERVE && !ps.slotAltAmmo[1]);
 CHECK(BG_SlotWeapon(&ps,0,qtrue)==ALT && BG_SlotWeapon(&ps,1,qtrue)==OTHER && !BG_SlotAltActive(&ps,0));
 BG_GetSlot(&ps,0,&gun); CHECK(gun.altAmmo==MK_MAG_SIZE && gun.altReserve==MK_START_RESERVE);
 Ready(0);
 CHECK(!(ps.stats[STAT_WEAPONS] & (1<<ALT)) && ps.ammo[ALT]==0);
 /* without the attachment there is no alt to ask for: the command never names one */
 CHECK(!BG_RequestedAlt(BG_SelectionCommand(&ps,0,qtrue,qfalse)));
}

/* ---- MW2's alt switch: nothing is put down, the alt raise ---- */
static void TestAltSwitch(void) {
 int i, msec;
 for(i=0;i<ARRAY_LEN(thinks);i++) {
  msec=thinks[i];
  Ready(BG_ATTACHMENT_SHOTGUN);
  /* the switch command itself has the underbarrel in hand, raising in its
   * altRaiseTime (5765a8-5765dc), its draw MW2's RAISING_ALTSWITCH */
  Select(SLOT_PRIMARY,qtrue); Step(msec);
  CHECK(ps.weapon==ALT && ps.weaponstate==WEAPON_RAISING && ps.weaponDraw==BG_DRAW_ALT);
  CHECK(ps.weaponTime==BG_WeaponAltRaiseTime(ALT) && BG_WeaponDrawTime(&ps)==BG_WeaponAltRaiseTime(ALT));
  CHECK(BG_SlotAltActive(&ps,SLOT_PRIMARY) && ps.stats[STAT_ACTIVE_SLOT]==SLOT_PRIMARY);
  CHECK(BG_ActiveSlotWeapon(&ps)==ALT);
  Run(BG_WeaponAltRaiseTime(ALT)-msec,msec);
  if(ps.weaponTime>0) Step(msec);
  Step(msec); CHECK(ps.weapon==ALT && ps.weaponstate==WEAPON_READY);
  /* and back: the rifle in hand at once, in its own altRaiseTime */
  Select(SLOT_PRIMARY,qfalse); Step(msec);
  CHECK(ps.weapon==HOST && ps.weaponstate==WEAPON_RAISING && ps.weaponDraw==BG_DRAW_ALT);
  CHECK(ps.weaponTime==BG_WeaponAltRaiseTime(HOST) && !BG_SlotAltActive(&ps,SLOT_PRIMARY));
  /* out of that raise, again at once: an alt raise is no lock on a switch */
  Select(SLOT_PRIMARY,qtrue); Step(msec);
  CHECK(ps.weapon==ALT && ps.weaponstate==WEAPON_RAISING && ps.weaponTime==BG_WeaponAltRaiseTime(ALT));
  /* while sprinting it is no alt switch (MW2: pm_flags 0x4000 clears it in
   * BeginWeaponChange): the gun goes down in full and the other comes up in full */
  Settle(msec); ps.pm_flags|=PMF_SPRINTING;
  Select(SLOT_PRIMARY,qfalse); Step(msec);
  CHECK(ps.weapon==ALT && ps.weaponstate==WEAPON_DROPPING && ps.weaponTime==BG_WeaponDropTime(ALT));
  Run(BG_WeaponDropTime(ALT),msec);
  CHECK(ps.weapon==HOST && ps.weaponstate==WEAPON_RAISING && ps.weaponDraw==0);
  ps.pm_flags&=~PMF_SPRINTING;
 }
}

/* ---- MW2's weapnext (59ed40): from the alt to its gun; from a gun to the next slot's gun ---- */
static void TestWeapnext(void) {
 int sel;
 Ready(BG_ATTACHMENT_SHOTGUN);
 sel=BG_SelectionCommand(&ps,SLOT_PRIMARY,qtrue,qfalse);
 sel=BG_ToggleSlotWeapon(&ps,sel);
 CHECK(BG_RequestedSlot(&ps,sel)==SLOT_PRIMARY && !BG_RequestedAlt(sel));	/* 59ed8c: the alt's own gun */
 sel=BG_ToggleSlotWeapon(&ps,sel);
 CHECK(BG_RequestedSlot(&ps,sel)==SLOT_SECONDARY && !BG_RequestedAlt(sel));
 sel=BG_ToggleSlotWeapon(&ps,sel);
 CHECK(BG_RequestedSlot(&ps,sel)==SLOT_PRIMARY && !BG_RequestedAlt(sel));	/* 59eef4: the gun, never its alt */
 /* the killstreak toggle rides along untouched */
 sel=BG_ToggleSlotWeapon(&ps,BG_SelectionCommand(&ps,SLOT_PRIMARY,qtrue,qtrue));
 CHECK(BG_RequestedKillstreak(sel) && !BG_RequestedAlt(sel));
 /* the generation bit still tells a selection from before a pickup apart */
 sel=BG_SelectionCommand(&ps,SLOT_PRIMARY,qtrue,qtrue);
 ps.slotFlags[0]^=BG_GUN_GENERATION;
 CHECK(!BG_SlotCommandCurrent(&ps,sel));
 sel=BG_SelectionCommand(&ps,SLOT_PRIMARY,qtrue,qtrue);
 CHECK(BG_SlotCommandCurrent(&ps,sel) && BG_RequestedAlt(sel) && BG_RequestedKillstreak(sel));
 CHECK(BG_IS_SLOT_COMMAND(sel) && sel<256);
}

/* ---- the moni NAC: Shoot, Y, Y, X - guaranteed ---- */
static void TestMoniNAC(void) {
 int i, msec, t, inHand, gunAtShot;
 qboolean owed;
 for(i=0;i<ARRAY_LEN(thinks);i++) {
  msec=thinks[i];
  Ready(BG_ATTACHMENT_SHOTGUN); OnAlt(msec);
  gunAtShot=ps.ammo[ALT];
  /* Shoot */
  move.cmd.buttons=BUTTON_ATTACK; Step(msec); move.cmd.buttons=0;
  CHECK(fired[ALT]==1 && ps.weaponstate==WEAPON_FIRING && Owes(ALT) && ps.ammo[ALT]==gunAtShot-1);
  /* Y at once: from the underbarrel weapnext asks for its rifle. A swap waits
   * out the shot (MW2's CheckForChangeWeapon refuses FIRING), then the alt
   * switch takes the rifle straight out of the pump */
  Select(SLOT_PRIMARY,qfalse);
  t=msec; inHand=-1;
  while(t<MK_FIRE_MS) {
   Step(msec); t+=msec;
   if(ps.weapon==HOST) { inHand=t; break; }
   CHECK(ps.weapon==ALT);
  }
  inHand-=msec;	/* on the shot's own clock, which started on the shooting think */
  CHECK(inHand>MK_BOLT_OPEN_MS && inHand<=MK_BOLT_OPEN_MS+2*msec);
  CHECK(ps.weaponstate==WEAPON_RAISING && ps.weaponDraw==BG_DRAW_ALT);
  /* Guaranteed: the rifle is in hand on the first think the shot allows,
   * whatever the timing, because nothing is put down (there is no holster
   * that has to beat the bolt). Whether the shell went in first is MW2's
   * order: its change check (578bc0) runs before the bolt closes (575a30, from
   * the expiry pass 579450), so a switch on the pump's first think leaves the
   * pump owed - here exactly when that think did not already cross the 1 ms
   * close along with the open, i.e. when it landed on the open itself. */
  owed=(MK_BOLT_OPEN_MS % msec)==0;
  if(getenv("MK_DEBUG")) fprintf(stderr,"msec %d inHand %d owes %d\n",msec,inHand,Owes(ALT));
  CHECK(Owes(ALT)==owed);
  /* Y again: from the rifle, the other slot; the rifle's raise is put down */
  Select(SLOT_SECONDARY,qfalse); Step(msec);
  CHECK(ps.weapon==HOST && ps.weaponstate==WEAPON_DROPPING);
  /* X: a reload press as the other gun comes up */
  Settle(msec); CHECK(ps.weapon==OTHER);
  /* back to the underbarrel later: an owed pump is worked whole from its
   * start (MW2's needsRechamber; Peace's reload never resumes either), a
   * chambered gun is simply ready */
  Select(SLOT_PRIMARY,qfalse); Settle(msec); Select(SLOT_PRIMARY,qtrue); Step(msec);
  while(ps.weaponstate==WEAPON_RAISING) Step(msec);
  Step(msec);	/* a raise that ends is ready from the next think (step 6), where an owed pump starts (4d) */
  CHECK(ps.weapon==ALT && ps.ammo[ALT]==gunAtShot-1);
  CHECK(ps.weaponstate==(owed ? WEAPON_BOLTING : WEAPON_READY));
  Settle(msec); CHECK(!Owes(ALT) && fired[ALT]==1);
  /* the same inputs on the SPAS leave its pump owed: its bolt closes far
   * into its cycle, MW2's rechamberBoltTime 0.449 against this gun's 0 */
  Ready(BG_ATTACHMENT_SHOTGUN); Select(SLOT_SECONDARY,qfalse); Settle(msec);
  move.cmd.buttons=BUTTON_ATTACK; Step(msec); move.cmd.buttons=0;
  Select(SLOT_PRIMARY,qfalse);
  while(ps.weapon==OTHER && ps.weaponstate!=WEAPON_DROPPING) Step(msec);
  CHECK(Owes(OTHER));
 }
}

/* ---- swap and sprint out of its raise, drop and pump ---- */
static void TestOutOf(void) {
 int i, msec, left;
 for(i=0;i<ARRAY_LEN(thinks);i++) {
  msec=thinks[i];
  /* swap out of its raise: the rifle back at once (an alt switch) */
  Ready(BG_ATTACHMENT_SHOTGUN); Select(SLOT_PRIMARY,qtrue); Step(msec);
  Step(msec); CHECK(ps.weapon==ALT && ps.weaponstate==WEAPON_RAISING);
  Select(SLOT_PRIMARY,qfalse); Step(msec); CHECK(ps.weapon==HOST);
  /* swap out of its ordinary raise to the other slot: down at once */
  Ready(BG_ATTACHMENT_SHOTGUN); OnAlt(msec);
  Select(SLOT_SECONDARY,qfalse); Run(BG_WeaponDropTime(ALT),msec); Settle(msec); CHECK(ps.weapon==OTHER);
  Select(SLOT_PRIMARY,qtrue); Step(msec);
  CHECK(ps.weapon==OTHER && ps.weaponstate==WEAPON_DROPPING);
  while(ps.weapon==OTHER) { CHECK(ps.weaponstate==WEAPON_DROPPING); Step(msec); }
  /* from the other slot it is no alt switch: the underbarrel's own raise (MW2's
   * FinishWeaponChange after a DROPPING, not a DROPPING_ALT) */
  CHECK(ps.weapon==ALT && ps.weaponstate==WEAPON_RAISING && ps.weaponDraw==0 && ps.weaponTime==BG_WeaponRaiseTime(ALT));
  Step(msec);
  Select(SLOT_SECONDARY,qfalse); Step(msec);
  CHECK(ps.weapon==ALT && ps.weaponstate==WEAPON_DROPPING);
  /* swap out of its drop: YY back to it, ready on that command */
  Step(msec); CHECK(ps.weaponstate==WEAPON_DROPPING);
  Select(SLOT_PRIMARY,qtrue); Step(msec);
  CHECK(ps.weapon==ALT && ps.weaponstate==WEAPON_READY);
  /* swap out of its pump: the other slot's holster starts from the pump */
  move.cmd.buttons=BUTTON_ATTACK; Step(msec); move.cmd.buttons=0;
  while(ps.weaponstate==WEAPON_FIRING) Step(msec);
  CHECK(ps.weaponstate==WEAPON_BOLTING);
  Select(SLOT_SECONDARY,qfalse); Step(msec);
  /* On the think the 1 ms bolt close lands (the pump's first, when the shot's
   * last think stopped on the open), the note takes the holster's lock away
   * and the swap is instant - Peace's NAC, the pump still owed. Later, an
   * ordinary holster from the pump, the shell in. */
  if((MK_BOLT_OPEN_MS % msec)==0) {
   CHECK(ps.weapon==OTHER && ps.weaponstate==WEAPON_RAISING && Owes(ALT));
  } else {
   CHECK(ps.weapon==ALT && ps.weaponstate==WEAPON_DROPPING && ps.weaponTime==BG_WeaponDropTime(ALT) && !Owes(ALT));
  }
  /* sprint out of its pump: the carry from the pump, the shell already in */
  Ready(BG_ATTACHMENT_SHOTGUN); OnAlt(msec);
  move.cmd.buttons=BUTTON_ATTACK; Step(msec); move.cmd.buttons=0;
  while(ps.weaponstate==WEAPON_FIRING) Step(msec);
  ps.pm_flags|=PMF_SPRINTING; Step(msec);
  CHECK(ps.weaponstate==WEAPON_SPRINT_IN || ps.weaponstate==WEAPON_SPRINTING);
  ps.pm_flags&=~PMF_SPRINTING; Settle(msec);
  CHECK(ps.weapon==ALT && !Owes(ALT));	/* the close was crossed under the carry, as for any gun */
  /* a sprint meanwhile holds its raise and drop to their end, as MW2's sprint
   * check refuses states 1-5 (5780bc-5780d0), then lowers into the carry */
  Ready(BG_ATTACHMENT_SHOTGUN); Select(SLOT_PRIMARY,qtrue); Step(msec);
  ps.pm_flags|=PMF_SPRINTING; left=ps.weaponTime;
  while(ps.weaponTime>0) { Step(msec); CHECK(ps.weaponstate==WEAPON_RAISING || ps.weaponTime<=0); }
  Step(msec); CHECK(ps.weaponstate==WEAPON_SPRINT_IN || ps.weaponstate==WEAPON_READY || ps.weaponstate==WEAPON_SPRINTING);
  ps.pm_flags&=~PMF_SPRINTING; (void)left;
 }
}

/* ---- the UAV's trigger: D-pad right, Y takes it back to the shotgun ---- */
static void TestKillstreak(void) {
 int i, msec;
 for(i=0;i<ARRAY_LEN(thinks);i++) {
  msec=thinks[i];
  Ready(BG_ATTACHMENT_SHOTGUN); OnAlt(msec);
  ps.ammo[WP_KILLSTREAK_UAV]=1; BG_RebuildWeaponMask(&ps);
  CHECK(ps.stats[STAT_WEAPONS] & (1<<WP_KILLSTREAK_UAV));
  /* asked for: the shotgun goes down for it, in its own drop */
  killstreakToggle=1; Select(SLOT_PRIMARY,qtrue); Step(msec);
  CHECK(ps.weapon==ALT && ps.weaponstate==WEAPON_DROPPING && ps.weaponTime==BG_WeaponDropTime(ALT));
  /* a knife pressed while it goes down for the trigger is lost: MW2's knife
   * refuses every drop, and this drop is no YY back to the gun in hand */
  move.cmd.buttons=BUTTON_MELEE; Step(msec); move.cmd.buttons=0;
  CHECK(ps.weapon==ALT && ps.weaponstate==WEAPON_DROPPING && !(ps.weaponAction & WA_TYPE_MASK));
  /* Y before the trigger is out takes the request back: the drop returns to
   * the shotgun in hand, ready on that command, and the UAV is kept */
  killstreakToggle=0; Select(SLOT_PRIMARY,qtrue); Step(msec);
  CHECK(ps.weapon==ALT && ps.weaponstate==WEAPON_READY && ps.ammo[WP_KILLSTREAK_UAV]==1 && !used);
  /* asked for again and left alone: the trigger in hand is MW2's weapon_change,
   * which uses it; the request is served and the trigger goes away */
  killstreakToggle=1; Select(SLOT_PRIMARY,qtrue);
  while(ps.weapon!=WP_KILLSTREAK_UAV) { CHECK(ps.weapon==ALT); Step(msec); }
  CHECK(used==1 && ps.ammo[WP_KILLSTREAK_UAV]==0 && (ps.weapFlags & PWF_KILLSTREAK_SERVED));
  CHECK(ps.weaponstate==WEAPON_RAISING && ps.stats[STAT_ACTIVE_SLOT]==SLOT_PRIMARY && BG_SlotAltActive(&ps,SLOT_PRIMARY));
  Step(msec);
  CHECK(ps.weapon==WP_KILLSTREAK_UAV && ps.weaponstate==WEAPON_DROPPING && ps.weaponTime==BG_WeaponDropTime(WP_KILLSTREAK_UAV));
  BG_RebuildWeaponMask(&ps); CHECK(ps.stats[STAT_WEAPONS] & (1<<WP_KILLSTREAK_UAV));	/* still in hand */
  /* back to the shotgun it was pulled from, on its ordinary raise */
  while(ps.weapon==WP_KILLSTREAK_UAV) Step(msec);
  CHECK(ps.weapon==ALT && ps.weaponstate==WEAPON_RAISING && ps.weaponDraw==0);
  Settle(msec); CHECK(used==1 && fired[WP_KILLSTREAK_UAV]==0);
  BG_RebuildWeaponMask(&ps); CHECK(!(ps.stats[STAT_WEAPONS] & (1<<WP_KILLSTREAK_UAV)));
  /* with none left the toggle asks for nothing */
  killstreakToggle=0; Select(SLOT_PRIMARY,qtrue); Run(2000,msec); CHECK(ps.weapon==ALT && used==1);
 }
}

/* ---- the underbarrel's shells belong to the gun ---- */
static void TestInstance(void) {
 bg_weaponInstance_t gun, back;
 entityState_t item;
 Ready(BG_ATTACHMENT_SHOTGUN); OnAlt(8);
 move.cmd.buttons=BUTTON_ATTACK; Step(8); move.cmd.buttons=0; Settle(8);
 CHECK(ps.ammo[ALT]==MK_MAG_SIZE-1);
 /* stowed and back: the same tube */
 Select(SLOT_SECONDARY,qfalse); Settle(8); Settle(8);
 CHECK(ps.weapon==OTHER && ps.slotAltAmmo[0]==MK_MAG_SIZE-1);
 Select(SLOT_PRIMARY,qfalse); Settle(8); Settle(8); CHECK(ps.ammo[ALT]==MK_MAG_SIZE-1);
 /* a dropped gun carries the tube, its spares and its pump in the item's frame */
 BG_GetSlot(&ps,SLOT_PRIMARY,&gun);
 gun.flags|=BG_GUN_ALT_UNCHAMBERED;
 memset(&item,0,sizeof(item)); item.frame=BG_WeaponItemAltFrame(&gun);
 memset(&back,0,sizeof(back)); BG_WeaponItemAlt(item.frame,&back);
 CHECK(back.altAmmo==gun.altAmmo && back.altReserve==gun.altReserve && (back.flags & BG_GUN_ALT_UNCHAMBERED));
 CHECK(item.frame<65536 && BG_WeaponItemAltFrame(&back)==item.frame);
 /* the same gun put back (a scavenge) stays on its underbarrel; another gun is put in on its own */
 OnAlt(8); BG_GetSlot(&ps,SLOT_PRIMARY,&gun); gun.altReserve=MK_MAX_RESERVE;
 BG_PutSlot(&ps,SLOT_PRIMARY,&gun);
 CHECK(BG_SlotAltActive(&ps,SLOT_PRIMARY) && ps.ammoReserve[ALT]==MK_MAX_RESERVE);
 gun.weapon=WP_RAILGUN; gun.attachments=0; BG_PutSlot(&ps,SLOT_PRIMARY,&gun);
 CHECK(!BG_SlotAltActive(&ps,SLOT_PRIMARY) && !(ps.stats[STAT_WEAPONS] & (1<<ALT)));
 CHECK(BG_SlotWeapon(&ps,SLOT_PRIMARY,qtrue)==WP_RAILGUN);
}

/* ---- MW2's segmented reload: the start loads a shell of its own ---- */
static void TestReload(void) {
 int t;
 /* one shell missing: the start's own shell fills the tube, so the end follows the start */
 Ready(BG_ATTACHMENT_SHOTGUN); OnAlt(1);
 ps.ammo[ALT]=MK_MAG_SIZE-1; PM_BeginReload();
 for(t=0;ps.weaponstate==WEAPON_RELOADING && t<20000;t++) {
  if(ps.weaponAnimSeq==RSEQ_LOOP) CHECK(!"no loop pass");
  Step(1);
 }
 CHECK(ps.ammo[ALT]==MK_MAG_SIZE && ps.ammoReserve[ALT]==MK_START_RESERVE-1);
 CHECK(t>=MK_RELOAD_START_MS+MK_RELOAD_END_MS-2 && t<=MK_RELOAD_START_MS+MK_RELOAD_END_MS+2);
 /* empty: the start's shell and a loop pass for each of the rest */
 ps.ammo[ALT]=0; ps.ammoReserve[ALT]=MK_START_RESERVE; PM_BeginReload();
 for(t=0;ps.weaponstate==WEAPON_RELOADING && t<20000;t++) Step(1);
 CHECK(ps.ammo[ALT]==MK_MAG_SIZE && ps.ammoReserve[ALT]==MK_START_RESERVE-MK_MAG_SIZE);
 CHECK(t>=MK_RELOAD_START_MS+(MK_MAG_SIZE-1)*MK_RELOAD_LOOP_MS+MK_RELOAD_END_MS-2 &&
       t<=MK_RELOAD_START_MS+(MK_MAG_SIZE-1)*MK_RELOAD_LOOP_MS+MK_RELOAD_END_MS+2);
 /* a reload started owing the pump settles it after the gun's own action
  * (1 ms, MW2's weaponDelay), never later than the start's shell */
 ps.ammo[ALT]=1; ps.stats[STAT_UNCHAMBERED]|=1<<ALT; PM_BeginReload();
 Step(1); Step(1); CHECK(!Owes(ALT));
}

int main(void) {
 TestData();
 TestClass();
 TestAltSwitch();
 TestWeapnext();
 TestMoniNAC();
 TestOutOf();
 TestKillstreak();
 TestInstance();
 TestReload();
 printf("PASS: %d underbarrel shotgun and UAV trigger checks\n",checks);
 return 0;
}
