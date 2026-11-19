/* Actual client selection handlers, snapshot validation and weapon thinks.
 * A delayed snapshot must never undo a more recent mode press or Y. The cycle
 * is MW2's: an underbarrel's Y is its rifle, the rifle's Y is the next gun,
 * and the cycle brings a gun back in its own mode. */
#include "../../code/cgame/cg_weapons.c"
#include "masterkey_selection_stubs.h"

cg_t cg;
cgs_t cgs;
static snapshot_t snapshot;
static int checks, swapPresses;
static char weaponArgument[16];

#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int code, const char *fmt, ...) { (void)code; (void)fmt; abort(); }
void trap_SnapVector(float *v) { int i; for (i=0;i<3;i++) v[i]=rintf(v[i]); }
void trap_SendConsoleCommand(const char *text) {
	CHECK(!strcmp(text, "_peaceWeaponSwap\n")); swapPresses++;
}
const char *CG_Argv(int arg) { CHECK(arg==1); return weaponArgument; }
void PickupWeaponStep(pmove_t *move, int msec);

static void Setup(int primary, int secondary, qboolean alternate) {
	bg_class_t cl = *BG_Class(0);
	memset(&cg,0,sizeof(cg)); memset(&snapshot,0,sizeof(snapshot));
	cl.slot[0]=primary; cl.slot[1]=primary==secondary ? WP_SHOTGUN : secondary;
	cl.attachments[0]=cl.attachments[1]=0;
	CHECK(BG_ValidateClass(&cl));
	BG_ApplyClass(&snapshot.ps,&cl);
	/* Duplicate instances come from pickups; class menus disallow duplicates. */
	if (primary==secondary) BG_SetSlotWeapon(&snapshot.ps,1,secondary);
	snapshot.ps.stats[STAT_HEALTH]=100;
	snapshot.ps.weapon=primary;
	snapshot.ps.weaponstate=WEAPON_READY;
	snapshot.ps.weaponTime=0;
	snapshot.ps.weaponAnimTime=-1;
	snapshot.ps.weaponDraw=0;
	snapshot.ps.slotFlags[0]&=~BG_GUN_FIRST_DRAW;
	snapshot.ps.slotFlags[1]&=~BG_GUN_FIRST_DRAW;
	if (alternate) {
		snapshot.ps.slotFlags[0]|=BG_GUN_ALTERNATE;
		snapshot.ps.weapon=BG_AlternateWeapon(primary);
	}
	cg.snap=&snapshot;
	cg.predictedPlayerState=snapshot.ps;
	cg.time=1000;
	CG_Respawn();
	swapPresses=0;
}

/* Same selection encoding as CG_DrawActiveFrame, with deliberately stale
 * authoritative state until a test explicitly receives the next snapshot. */
static int Command(void) {
	CG_ValidateWeaponSelect();
	return BG_WeaponCommand(&snapshot.ps,cg.weaponSelectSlot,cg.weaponSelectAlternate);
}
static void Step(int msec, int buttons) {
	pmove_t move;
	memset(&move,0,sizeof(move));
	move.ps=&cg.predictedPlayerState;
	move.cmd.weapon=Command(); move.cmd.buttons=buttons;
	cg.time+=msec; move.cmd.serverTime=cg.time;
	PickupWeaponStep(&move,msec);
}
static void ExpectIntent(int slot, int weapon) {
	playerState_t before=cg.predictedPlayerState;
	int command=Command();
	CHECK(cg.weaponSelectSlot==slot && cg.weaponSelect==weapon);
	CHECK(BG_RequestedWeapon(&snapshot.ps,command)==weapon);
	CHECK(!memcmp(&before,&cg.predictedPlayerState,sizeof(before)));
}
static void Settle(void) {
	int t;
	for (t=0;t<5000;t++) {
		Step(1,0);
		if (cg.predictedPlayerState.weaponstate==WEAPON_READY &&
			cg.predictedPlayerState.weapon==cg.weaponSelect) return;
	}
	CHECK(0);
}

static void TestRapidModeInput(void) {
	playerState_t before;
	Setup(WP_M16,WP_SHOTGUN,qfalse);
	ExpectIntent(0,WP_M16);
	before=cg.predictedPlayerState;
	CG_AltWeapon_f();
	ExpectIntent(0,WP_MASTERKEY);
	CHECK(!memcmp(&before,&cg.predictedPlayerState,sizeof(before)));
	CG_AltWeapon_f();
	ExpectIntent(0,WP_M16);
	CHECK(!swapPresses);
	/* An actual mode change is there on its first think (MW2's altDropTime),
	 * and MW2's toggle is refused all through its raise: no way back, no shot. */
	CG_AltWeapon_f(); Step(1,0);
	CHECK(cg.predictedPlayerState.weapon==WP_MASTERKEY);
	CHECK(cg.predictedPlayerState.weaponstate==WEAPON_RAISING && cg.predictedPlayerState.weaponDraw==WDRAW_ALT);
	CHECK(cg.predictedPlayerState.weaponTime==BG_WeaponAltRaiseTime(WP_MASTERKEY));
	CG_AltWeapon_f(); ExpectIntent(0,WP_MASTERKEY);
	Step(1,BUTTON_ATTACK);
	CHECK(cg.predictedPlayerState.weapon==WP_MASTERKEY);
	CHECK(cg.predictedPlayerState.weaponstate==WEAPON_RAISING);
	CHECK(cg.predictedPlayerState.ammo[WP_M16]==BG_WeaponMagSize(WP_M16));
	CHECK(cg.predictedPlayerState.ammo[WP_MASTERKEY]==BG_WeaponMagSize(WP_MASTERKEY));
	Settle();
	/* refused while a gun goes down as well */
	CG_NextWeapon_f(); ExpectIntent(0,WP_M16); Settle();
	CG_NextWeapon_f(); ExpectIntent(1,WP_SHOTGUN); Step(1,0);
	CHECK(cg.predictedPlayerState.weaponstate==WEAPON_DROPPING);
	CG_AltWeapon_f(); ExpectIntent(1,WP_SHOTGUN);
	CHECK(!swapPresses || swapPresses==2);
}

static void TestCycle(void) {
	int rounds;
	Setup(WP_M16,WP_SHOTGUN,qfalse);
	CG_AltWeapon_f(); Settle();
	CHECK(cg.predictedPlayerState.weapon==WP_MASTERKEY);
	CHECK(!(snapshot.ps.slotFlags[0]&BG_GUN_ALTERNATE)); /* server still behind */
	/* The underbarrel's Y is its rifle: the mode change, there on its think. */
	CG_NextWeapon_f(); ExpectIntent(0,WP_M16); Step(1,0);
	CHECK(cg.predictedPlayerState.weapon==WP_M16);
	CHECK(cg.predictedPlayerState.weaponstate==WEAPON_RAISING && cg.predictedPlayerState.weaponDraw==WDRAW_ALT);
	/* The rifle's Y is the next gun, taken out of that raise as the rifle's own drop. */
	CG_NextWeapon_f(); ExpectIntent(1,WP_SHOTGUN); Step(1,0);
	CHECK(cg.predictedPlayerState.weapon==WP_M16 && cg.predictedPlayerState.weaponstate==WEAPON_DROPPING);
	CHECK(cg.predictedPlayerState.weaponTime==BG_WeaponDropTime(WP_M16));
	/* A third is back on the rifle still in hand: ready on that think, and it fires. */
	CG_NextWeapon_f(); ExpectIntent(0,WP_M16);
	rounds=cg.predictedPlayerState.ammo[WP_M16];
	Step(1,BUTTON_ATTACK);
	CHECK(cg.predictedPlayerState.weapon==WP_M16);
	CHECK(cg.predictedPlayerState.weaponstate==WEAPON_FIRING);
	CHECK(cg.predictedPlayerState.ammo[WP_M16]==rounds-1);
	CHECK(swapPresses==3);
	/* A late acknowledgement of the underbarrel does not replace current intent. */
	snapshot.ps.slotFlags[0]|=BG_GUN_ALTERNATE;
	ExpectIntent(0,WP_M16);
	snapshot.ps.slotFlags[0]&=~BG_GUN_ALTERNATE;
	Settle();
	CG_NextWeapon_f(); Settle();
	CHECK(cg.predictedPlayerState.weapon==WP_SHOTGUN);
	CG_PrevWeapon_f(); ExpectIntent(0,WP_M16); Settle();
	CHECK(cg.predictedPlayerState.weapon==WP_M16);
	/* weapprev leaves the underbarrel for its rifle as well. */
	CG_AltWeapon_f(); Settle();
	CHECK(cg.predictedPlayerState.weapon==WP_MASTERKEY);
	CG_PrevWeapon_f(); ExpectIntent(0,WP_M16); Settle();
	/* Put away as the underbarrel (two presses the server hears as one change),
	 * the gun still comes back as the rifle: the cycle never selects the underbarrel. */
	CG_AltWeapon_f(); Settle();
	CG_NextWeapon_f(); ExpectIntent(0,WP_M16);
	CG_NextWeapon_f(); ExpectIntent(1,WP_SHOTGUN); Step(1,0);
	CHECK(cg.predictedPlayerState.weapon==WP_MASTERKEY && cg.predictedPlayerState.weaponstate==WEAPON_DROPPING);
	CHECK(cg.predictedPlayerState.weaponTime==BG_WeaponDropTime(WP_MASTERKEY));
	Settle();
	CHECK(cg.predictedPlayerState.weapon==WP_SHOTGUN);
	CHECK(cg.predictedPlayerState.slotFlags[0]&BG_GUN_ALTERNATE);
	CG_NextWeapon_f(); ExpectIntent(0,WP_M16); Settle();
	CHECK(cg.predictedPlayerState.weapon==WP_M16);
	CHECK(!(cg.predictedPlayerState.slotFlags[0]&BG_GUN_ALTERNATE));
}

static void TestDuplicateGuns(void) {
	/* Each copy's underbarrel returns to its own rifle, and the cycle moves
	 * between the copies as rifles. */
	Setup(WP_M16,WP_M16,qtrue);
	ExpectIntent(0,WP_MASTERKEY);
	CG_NextWeapon_f(); ExpectIntent(0,WP_M16); Settle();
	CG_NextWeapon_f(); ExpectIntent(1,WP_M16); Settle();
	CHECK(cg.predictedPlayerState.stats[STAT_ACTIVE_SLOT]==1);
	CG_AltWeapon_f(); ExpectIntent(1,WP_MASTERKEY); Settle();
	CG_NextWeapon_f(); ExpectIntent(1,WP_M16); Settle();
	CHECK(cg.predictedPlayerState.stats[STAT_ACTIVE_SLOT]==1);
	CG_NextWeapon_f(); ExpectIntent(0,WP_M16); Settle();
	CHECK(cg.predictedPlayerState.stats[STAT_ACTIVE_SLOT]==0);
	CG_AltWeapon_f(); ExpectIntent(0,WP_MASTERKEY); Settle();
	CG_NextWeapon_f(); ExpectIntent(0,WP_M16);
}

static void TestPickupAndRespawn(void) {
	int oldCommand;
	Setup(WP_M16,WP_SHOTGUN,qfalse);
	CG_AltWeapon_f(); oldCommand=Command();
	/* The received replacement is another M16, in rifle mode. A stable weapon
	 * ID cannot identify this change, but its inventory generation can. */
	BG_SetSlotWeapon(&snapshot.ps,0,WP_M16);
	snapshot.ps.slotFlags[0]^=BG_GUN_GENERATION;
	CHECK(!BG_SlotCommandCurrent(&snapshot.ps,oldCommand));
	cg.predictedPlayerState=snapshot.ps;
	ExpectIntent(0,WP_M16);
	CG_AltWeapon_f(); ExpectIntent(0,WP_MASTERKEY);
	CG_NextWeapon_f(); ExpectIntent(0,WP_M16);
	CG_NextWeapon_f(); ExpectIntent(1,WP_SHOTGUN);
	BG_SetSlotWeapon(&snapshot.ps,0,WP_M16);
	snapshot.ps.slotFlags[0]^=BG_GUN_GENERATION;
	cg.predictedPlayerState=snapshot.ps;
	ExpectIntent(1,WP_SHOTGUN); /* a pickup in the other slot cannot undo Y */
	cg.weaponSelectAlternate=qtrue;
	CG_Respawn(); ExpectIntent(0,WP_M16);
	snapshot.ps.slotFlags[0]|=BG_GUN_ALTERNATE;
	snapshot.ps.weapon=WP_MASTERKEY;
	cg.predictedPlayerState=snapshot.ps;
	CG_Respawn(); ExpectIntent(0,WP_MASTERKEY);
}

static void TestDirectSelectionAndGuards(void) {
	Setup(WP_M16,WP_SHOTGUN,qfalse);
	Com_sprintf(weaponArgument,sizeof(weaponArgument),"%i",WP_MASTERKEY);
	CG_Weapon_f(); ExpectIntent(0,WP_MASTERKEY);
	Com_sprintf(weaponArgument,sizeof(weaponArgument),"%i",WP_M16);
	CG_Weapon_f(); ExpectIntent(0,WP_M16);
	CG_NextWeapon_f(); ExpectIntent(1,WP_SHOTGUN);
	/* MW2's toggle from a gun with no other mode: the next gun that has one, in that mode. */
	CG_AltWeapon_f(); ExpectIntent(0,WP_MASTERKEY);
	CG_NextWeapon_f(); ExpectIntent(0,WP_M16);
	CG_NextWeapon_f(); ExpectIntent(1,WP_SHOTGUN);
	snapshot.ps.pm_flags|=PMF_FOLLOW;
	CG_AltWeapon_f(); CG_NextWeapon_f(); CG_Weapon_f();
	ExpectIntent(1,WP_SHOTGUN);
	snapshot.ps.pm_flags&=~PMF_FOLLOW;
	cg.weaponSelectSlot=99;
	ExpectIntent(0,WP_M16);
	CG_AltWeapon_f();
	cg.predictedPlayerState.weaponAction=WA_RETURNING;
	ExpectIntent(0,WP_M16);
	cg.snap=NULL;
	CG_AltWeapon_f(); CG_NextWeapon_f(); CG_Weapon_f(); CG_ValidateWeaponSelect();
	CHECK(cg.weaponSelect==WP_M16);
	/* With no gun that has another mode the toggle does nothing. */
	Setup(WP_SHOTGUN,WP_MACHINEGUN,qfalse);
	ExpectIntent(0,WP_SHOTGUN);
	CG_AltWeapon_f(); ExpectIntent(0,WP_SHOTGUN);
	CG_NextWeapon_f(); ExpectIntent(1,WP_MACHINEGUN);
	CG_AltWeapon_f(); ExpectIntent(1,WP_MACHINEGUN);
	CG_NextWeapon_f(); ExpectIntent(0,WP_SHOTGUN);
}

int main(void) {
	TestRapidModeInput(); TestCycle(); TestDuplicateGuns();
	TestPickupAndRespawn(); TestDirectSelectionAndGuards();
	printf("PASS: %i client mode input, cycle, snapshot, pickup and respawn checks\n",checks);
	return 0;
}
