/* The Y cycle with an underbarrel, MW2's mode switch, and what falls out of
 * them. The real client selection handlers (cg_weapons.c), the real full Pmove
 * and the server's real use/reload step (BG_UpdateActivate), run in the
 * client's frame order: a frame's bound commands run first (weapnext, weapalt),
 * its command is built with the weapon cgame set when it last drew, the server
 * thinks (Pmove, then the use step), and cgame draws and sets the next
 * command's weapon. So a Y's selection goes out one command after its press,
 * and X (+usereload) is a reload press on the command after its own.
 *
 * The test only presses buttons. Every time it compares against is read from
 * the game's tables, and nothing in the game names what it finds. */
#include "../../code/cgame/cg_weapons.c"
#include "masterkey_selection_stubs.h"

cg_t cg;
cgs_t cgs;
static snapshot_t snapshot;
static int checks, swapQueued, msec;
static char weaponArgument[16];
static pmove_t move;
static bg_activate_t use;
static int cmdValue;

#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d (%d ms commands): %s\n",__LINE__,msec,#x); exit(1); } } while (0)
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int code, const char *fmt, ...) { (void)code; (void)fmt; abort(); }
void trap_SnapVector(float *v) { int i; for (i=0;i<3;i++) v[i]=rintf(v[i]); }
void trap_SendConsoleCommand(const char *text) {
	if (strcmp(text, "_peaceWeaponSwap\n")) abort();
	swapQueued = 1;	/* IN_WeaponSwap: this frame's command carries the bit */
}
const char *CG_Argv(int arg) { (void)arg; return weaponArgument; }
int PickupNoteTime(int weapon, int seq, int kind);

/* loadout_test.c's flat floor at z=0, for the full Pmove on the ground. */
static void FloorTrace(trace_t *tr, const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, int pass, int mask) {
	float s = start[2] + mins[2], e = end[2] + mins[2];
	(void)maxs; (void)pass; (void)mask;
	memset(tr, 0, sizeof(*tr)); tr->fraction = 1; VectorCopy(end, tr->endpos); tr->entityNum = ENTITYNUM_NONE;
	if (s < -0.01f) { tr->startsolid = qtrue; tr->allsolid = e < 0; tr->fraction = 0; VectorCopy(start, tr->endpos);
		tr->entityNum = ENTITYNUM_WORLD; tr->plane.normal[2] = 1; return; }
	if (e < 0) {
		float f = (s - 0.03125f) / (s - e); if (f < 0) f = 0;
		tr->fraction = f; tr->endpos[0] = start[0] + f * (end[0] - start[0]);
		tr->endpos[1] = start[1] + f * (end[1] - start[1]); tr->endpos[2] = start[2] + f * (end[2] - start[2]);
		tr->plane.normal[2] = 1; tr->entityNum = ENTITYNUM_WORLD; tr->contents = CONTENTS_SOLID;
	}
}
static int NoContents(const vec3_t p, int pass) { (void)p; (void)pass; return 0; }

#define PS (&cg.predictedPlayerState)

/* The owner's test class: M16 with Masterkey in slot 0, SPAS in slot 1. */
static void Setup(int activeSlot, qboolean alternate) {
	bg_class_t cl = *BG_Class(0);
	playerState_t *ps = &snapshot.ps;
	memset(&cg, 0, sizeof(cg)); memset(&snapshot, 0, sizeof(snapshot));
	memset(&move, 0, sizeof(move)); memset(&use, 0, sizeof(use));
	cl.slot[0] = WP_M16; cl.slot[1] = WP_SHOTGUN;
	cl.attachments[0] = cl.attachments[1] = 0;
	CHECK(BG_ValidateClass(&cl));
	BG_ApplyClass(ps, &cl);
	BG_SpawnStance(ps);
	ps->stats[STAT_HEALTH] = 100;
	ps->pm_type = PM_NORMAL; ps->speed = BG_PLAYER_SPEED; ps->gravity = 800;
	ps->origin[2] = 24; ps->groundEntityNum = ENTITYNUM_WORLD; ps->commandTime = 1000;
	ps->slotFlags[0] &= ~BG_GUN_FIRST_DRAW; ps->slotFlags[1] &= ~BG_GUN_FIRST_DRAW;
	if (alternate) ps->slotFlags[0] |= BG_GUN_ALTERNATE;
	BG_SelectSlot(ps, activeSlot);
	ps->weapon = BG_ActiveSlotWeapon(ps);
	ps->weaponstate = WEAPON_READY; ps->weaponTime = 0; ps->weaponAnimTime = -1; ps->weaponDraw = WDRAW_NORMAL;
	cg.snap = &snapshot; cg.predictedPlayerState = *ps; cg.time = 1000;
	CG_Respawn();
	move.ps = PS; move.trace = FloorTrace; move.pointcontents = NoContents;
	move.tracemask = MASK_PLAYERSOLID;
	CG_ValidateWeaponSelect();
	cmdValue = BG_WeaponCommand(&snapshot.ps, cg.weaponSelectSlot, cg.weaponSelectAlternate);
	swapQueued = 0;
}

#define IN_Y    1
#define IN_ALT  2
#define IN_PREV 4
/* One client frame: bound commands, the command, the server's think, the draw. */
static void Frame(int buttons, int keys) {
	if (keys & IN_Y) CG_NextWeapon_f();
	if (keys & IN_PREV) CG_PrevWeapon_f();
	if (keys & IN_ALT) CG_AltWeapon_f();
	memset(&move.cmd, 0, sizeof(move.cmd));
	move.cmd.buttons = buttons | (swapQueued ? BUTTON_WEAPON_SWAP : 0);
	move.cmd.weapon = cmdValue;
	move.cmd.serverTime = PS->commandTime + msec;
	swapQueued = 0;
	Pmove(&move);
	BG_UpdateActivate(PS, &use, move.cmd.buttons, qfalse, qfalse);
	cg.time += msec;
	snapshot.ps = *PS;	/* own host: the next snapshot is this state */
	CG_ValidateWeaponSelect();
	cmdValue = BG_WeaponCommand(&snapshot.ps, cg.weaponSelectSlot, cg.weaponSelectAlternate);
}
static void Idle(int frames) { while (frames-- > 0) Frame(0, 0); }
static void Settle(void) {
	int t;
	for (t = 0; t < 5000; t++) {
		Frame(0, 0);
		if (PS->weaponstate == WEAPON_READY && PS->weapon == cg.weaponSelect) return;
	}
	CHECK(0);
}
/* Whole commands a timer of that length keeps a gun busy. */
static int Frames(int ms) { return (ms + msec - 1) / msec; }
static qboolean Owes(int slot, qboolean alternate) {
	bg_weaponInstance_t gun;
	BG_GetSlot(PS, slot, &gun);
	return (gun.flags & (alternate ? BG_GUN_ALT_UNCHAMBERED : BG_GUN_UNCHAMBERED)) != 0;
}
static int Shells(int slot, qboolean alternate) {
	bg_weaponInstance_t gun;
	BG_GetSlot(PS, slot, &gun);
	return alternate ? gun.altAmmo : gun.ammo;
}

/* Y from the underbarrel is its rifle; Y from the rifle is the next gun; the
 * cycle brings a gun back in its own mode and never selects the underbarrel. */
static void TestCycle(void) {
	msec = 8;
	Setup(0, qtrue);
	CHECK(PS->weapon == WP_MASTERKEY && cg.weaponSelect == WP_MASTERKEY);
	/* MW2's data: a mode change puts nothing down */
	CHECK(BG_WeaponAltDropTime(WP_MASTERKEY) == 0 && BG_WeaponAltDropTime(WP_M16) == 0);
	CHECK(BG_WeaponAltRaiseTime(WP_M16) > 0 && BG_WeaponAltRaiseTime(WP_MASTERKEY) > 0);

	Frame(0, IN_Y);
	CHECK(cg.weaponSelectSlot == 0 && cg.weaponSelect == WP_M16);	/* the press counts at once */
	CHECK(PS->weapon == WP_MASTERKEY && PS->weaponstate == WEAPON_READY);	/* its command goes out next frame */
	Frame(0, 0);
	/* the mode changed on that one think; only the alt raise takes time */
	CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_RAISING && PS->weaponDraw == WDRAW_ALT);
	CHECK(PS->weaponTime == BG_WeaponAltRaiseTime(WP_M16));
	CHECK(!(PS->slotFlags[0] & BG_GUN_ALTERNATE));
	Idle(Frames(BG_WeaponAltRaiseTime(WP_M16)) - 1);
	CHECK(PS->weaponstate == WEAPON_RAISING);
	Frame(0, 0);
	CHECK(PS->weaponstate == WEAPON_READY && PS->weapon == WP_M16);

	/* the second Y is the next gun: an ordinary drop of the rifle */
	Frame(0, IN_Y);
	CHECK(cg.weaponSelectSlot == 1 && cg.weaponSelect == WP_SHOTGUN);
	Frame(0, 0);
	CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_DROPPING && PS->weaponDraw != WDRAW_ALT);
	CHECK(PS->weaponTime == BG_WeaponDropTime(WP_M16));
	Settle();
	CHECK(PS->weapon == WP_SHOTGUN);
	/* and back: the rifle */
	Frame(0, IN_Y);
	CHECK(cg.weaponSelectSlot == 0 && cg.weaponSelect == WP_M16);
	Settle();
	CHECK(PS->weapon == WP_M16);

	/* weapprev leaves the underbarrel for its rifle too (the cycle's direction is not asked) */
	Setup(0, qtrue);
	Frame(0, IN_PREV);
	CHECK(cg.weaponSelectSlot == 0 && cg.weaponSelect == WP_M16);
	Settle();
	CHECK(PS->weapon == WP_M16);

	/* Two presses in one frame run from the latest selection: rifle, then the next gun.
	 * The server only ever hears the second, so the rifle never comes up. */
	Setup(0, qtrue);
	CG_NextWeapon_f();
	Frame(0, IN_Y);
	CHECK(cg.weaponSelectSlot == 1 && cg.weaponSelect == WP_SHOTGUN);
	Frame(0, 0);
	CHECK(PS->weapon == WP_MASTERKEY && PS->weaponstate == WEAPON_DROPPING);
	CHECK(PS->weaponTime == BG_WeaponDropTime(WP_MASTERKEY) && PS->weaponDraw != WDRAW_ALT);
	Settle();
	CHECK(PS->weapon == WP_SHOTGUN);
	/* The gun was put away as the underbarrel, and the cycle still brings the rifle. */
	CHECK(PS->slotFlags[0] & BG_GUN_ALTERNATE);
	Frame(0, IN_Y);
	CHECK(cg.weaponSelectSlot == 0 && cg.weaponSelect == WP_M16);
	Settle();
	CHECK(PS->weapon == WP_M16 && !(PS->slotFlags[0] & BG_GUN_ALTERNATE));

	/* Y during the mode change's own raise is taken on the first command that carries it */
	Setup(0, qtrue);
	Frame(0, IN_Y); Frame(0, IN_Y);
	CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_RAISING && PS->weaponDraw == WDRAW_ALT);
	Frame(0, 0);
	CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_DROPPING);
	CHECK(PS->weaponTime == BG_WeaponDropTime(WP_M16));
	/* and a third Y is back on the rifle still in hand: ready at once (MW2's drop cancel) */
	Frame(0, IN_Y);
	CHECK(cg.weaponSelect == WP_M16);
	Frame(0, 0);
	CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_READY);
}

/* MW2's alt toggle: refused while a gun goes down and during a mode change's
 * own raise, taken in an ordinary raise, and from a gun with no other mode it
 * goes straight to the next gun's underbarrel as one ordinary change. */
static void TestAltToggle(void) {
	int i;
	msec = 8;
	Setup(0, qfalse);
	Frame(0, IN_ALT);
	CHECK(cg.weaponSelect == WP_MASTERKEY);
	Frame(0, 0);
	CHECK(PS->weapon == WP_MASTERKEY && PS->weaponstate == WEAPON_RAISING && PS->weaponDraw == WDRAW_ALT);
	CHECK(PS->weaponTime == BG_WeaponAltRaiseTime(WP_MASTERKEY));
	/* the toggle is refused all through that raise */
	for (i = 1; i < Frames(BG_WeaponAltRaiseTime(WP_MASTERKEY)); i++) {
		Frame(0, IN_ALT);
		CHECK(cg.weaponSelect == WP_MASTERKEY && PS->weapon == WP_MASTERKEY && PS->weaponstate == WEAPON_RAISING);
	}
	Frame(0, 0);
	CHECK(PS->weaponstate == WEAPON_READY);
	/* and taken again once it is up */
	Frame(0, IN_ALT);
	CHECK(cg.weaponSelect == WP_M16);
	Settle();
	CHECK(PS->weapon == WP_M16);

	/* refused during an ordinary drop... */
	Frame(0, IN_Y); Frame(0, 0);
	CHECK(PS->weaponstate == WEAPON_DROPPING && cg.weaponSelect == WP_SHOTGUN);
	Frame(0, IN_ALT);
	CHECK(cg.weaponSelect == WP_SHOTGUN && cg.weaponSelectSlot == 1);
	/* ...and taken in an ordinary raise, where it is the next gun with another mode,
	 * in that mode: one ordinary change, the SPAS's own drop and the Masterkey's own raise */
	while (PS->weaponstate == WEAPON_DROPPING) Frame(0, 0);
	CHECK(PS->weapon == WP_SHOTGUN && PS->weaponstate == WEAPON_RAISING && PS->weaponDraw != WDRAW_ALT);
	Frame(0, IN_ALT);
	CHECK(cg.weaponSelectSlot == 0 && cg.weaponSelect == WP_MASTERKEY);
	Frame(0, 0);
	CHECK(PS->weapon == WP_SHOTGUN && PS->weaponstate == WEAPON_DROPPING);
	CHECK(PS->weaponTime == BG_WeaponDropTime(WP_SHOTGUN) && PS->weaponDraw != WDRAW_ALT);
	while (PS->weaponstate == WEAPON_DROPPING) Frame(0, 0);
	CHECK(PS->weapon == WP_MASTERKEY && PS->weaponstate == WEAPON_RAISING && PS->weaponDraw != WDRAW_ALT);
	CHECK(PS->weaponTime == BG_WeaponRaiseTime(WP_MASTERKEY));
	Settle();
	CHECK(PS->weapon == WP_MASTERKEY);
}

typedef struct {
	int reloadStart;	/* the command the reload began on, or -1 */
	int dropStart;		/* the first command that ended with the gun going down, or -1 */
	int change;			/* the command another gun came into the hand on, or -1 */
	int changeTo;
	qboolean rifle;		/* the rifle was in hand at some point */
} run_t;

/* The shot is command 0. Y on commands y1 and y2 and X on command x (-1: none). */
static run_t Shoot(int slot, qboolean alternate, int y1, int y2, int x, int frames) {
	run_t r;
	int f, from;
	Setup(slot, alternate);
	Idle(4);
	from = PS->weapon;
	r.reloadStart = r.dropStart = r.change = -1; r.changeTo = WP_NONE; r.rifle = qfalse;
	for (f = 0; f < frames; f++) {
		int was = PS->weaponstate;
		Frame((f == 0 ? BUTTON_ATTACK : 0) | (f == x ? BUTTON_USE_RELOAD : 0), (f == y1 || f == y2) ? IN_Y : 0);
		if (f == 0) CHECK(PS->weapon == from && PS->weaponstate == WEAPON_FIRING);
		if (r.reloadStart < 0 && PS->weaponstate == WEAPON_RELOADING && was != WEAPON_RELOADING) r.reloadStart = f;
		if (r.dropStart < 0 && PS->weaponstate == WEAPON_DROPPING) r.dropStart = f;
		if (PS->weapon == WP_M16) r.rifle = qtrue;
		if (PS->weapon != from) { r.change = f; r.changeTo = PS->weapon; break; }
	}
	return r;
}

/* Shoot the Masterkey, Y, Y, X: with all three inside the shot, the next gun is
 * in hand on the second command after X, with no drop at all, on every timing.
 * The shot's own length (the open note) is the whole window. */
static void TestShootYYX(void) {
	static const int rates[] = { 8, 16, 33 };
	int i, y1, y2, x, inside = 0, outside = 0;
	for (i = 0; i < (int)(sizeof(rates) / sizeof(rates[0])); i++) {
		int shot, drop, last, end;
		msec = rates[i];
		shot = PickupNoteTime(WP_MASTERKEY, ASEQ_FIRE, WNOTE_BOLT_OPEN);
		drop = BG_WeaponDropTime(WP_MASTERKEY);
		CHECK(shot > 0 && drop > 0);
		end = Frames(shot);			/* the shot's last command: its clock reaches the open note */
		last = end + 3;				/* X swept past the window as well */
		/* both Ys' selections reach the server inside the shot (a press goes out a command later) */
		for (y1 = 1; y1 < end; y1++) for (y2 = y1 + 1; y2 + 1 <= end; y2++) for (x = y2 + 1; x <= last; x++) {
			run_t r = Shoot(0, qtrue, y1, y2, x, end + Frames(drop) + 8);
			CHECK(!r.rifle);		/* the server never hears of the rifle */
			if (x + 1 < end) {
				/* the reload began inside the shot */
				CHECK(r.reloadStart == x + 1);
				CHECK(r.change == x + 2 && r.changeTo == WP_SHOTGUN && r.dropStart < 0);
				CHECK(PS->weaponstate == WEAPON_RAISING && PS->weaponTime == BG_WeaponDrawTime(PS));
				/* nothing went into the tube, and the pump is still owed */
				CHECK(Shells(0, qtrue) == BG_WeaponMagSize(WP_MASTERKEY) - 1 && Owes(0, qtrue));
				inside++;
			} else {
				/* X came too late: the change began on the shot's last command, as an
				 * ordinary drop that runs its whole length, and the reload press is lost */
				CHECK(r.reloadStart < 0 && r.dropStart == end);
				CHECK(r.change >= 0 && (r.change - r.dropStart) * msec >= drop && r.changeTo == WP_SHOTGUN);
				outside++;
			}
		}
		/* one Y and X: the change is the mode change, which had no drop to lose */
		for (y1 = 1; y1 + 2 < end; y1++) for (x = y1 + 1; x + 1 < end; x++) {
			run_t r = Shoot(0, qtrue, y1, -1, x, end + 8);
			CHECK(r.reloadStart == x + 1 && r.change == x + 2 && r.changeTo == WP_M16);
			CHECK(PS->weaponstate == WEAPON_RAISING && PS->weaponDraw == WDRAW_ALT);
			CHECK(PS->weaponTime == BG_WeaponAltRaiseTime(WP_M16) && Owes(0, qtrue));
		}
		/* without X the same two presses wait out the shot and pay the whole drop */
		{
			run_t r = Shoot(0, qtrue, 1, 2, -1, end + Frames(drop) + 8);
			CHECK(r.reloadStart < 0 && r.dropStart == end && (r.change - r.dropStart) * msec >= drop);
			CHECK(r.changeTo == WP_SHOTGUN && !r.rifle && Owes(0, qtrue));
		}
	}
	CHECK(inside > 0 && outside > 0);
}

/* The SPAS under the same presses: whenever X's reload begins before the gun
 * goes down, the gun pays its whole drop. Y then X, and Y, Y, X. */
static void TestSpasPaysItsDrop(void) {
	static const int rates[] = { 8, 16, 33 };
	int i, y1, y2, x, swaps = 0;
	for (i = 0; i < (int)(sizeof(rates) / sizeof(rates[0])); i++) {
		int shot, close, drop, end, last;
		msec = rates[i];
		shot = PickupNoteTime(WP_SHOTGUN, ASEQ_FIRE, WNOTE_BOLT_OPEN);
		close = PickupNoteTime(WP_SHOTGUN, ASEQ_FIRE, WNOTE_BOLT_CLOSED);
		drop = BG_WeaponDropTime(WP_SHOTGUN);
		CHECK(shot > 0 && close > shot && drop > 0);
		end = Frames(shot);
		/* every press before the pump's close note: that note's own think is the
		 * bolt cancel, which is another thing and asks for no reload */
		last = close / msec - 3;
		CHECK(last > end);
		for (y1 = 1; y1 < last; y1++) for (x = y1 + 1; x <= last; x++) {
			run_t r = Shoot(1, qfalse, y1, -1, x, last + Frames(drop) + 8);
			CHECK(r.change >= 0 && r.changeTo == WP_M16 && r.dropStart >= 0);
			CHECK((r.change - r.dropStart) * msec >= drop);
			swaps++;
		}
		/* two presses come back to the SPAS itself: no change at all, X only reloads */
		for (y1 = 1; y1 < end; y1++) for (y2 = y1 + 1; y2 < end; y2++) for (x = y2 + 1; x + 1 < end; x++) {
			run_t r = Shoot(1, qfalse, y1, y2, x, end + 8);
			CHECK(r.change < 0 && r.dropStart < 0 && r.reloadStart == x + 1 && PS->weapon == WP_SHOTGUN);
		}
	}
	CHECK(swaps > 0);
}

/* A change asked for during the shot begins on the shot's last command and
 * leaves the action owed (MW2: the rechamber starts only from READY, later).
 * Asked for once the pump is under way, the Masterkey's is already paid: MW2's
 * bolt delay is 1ms into its rechamber (rechamberBoltTime 0). */
static void TestPumpOwed(void) {
	static const int rates[] = { 8, 16, 33 };
	int i, f;
	for (i = 0; i < (int)(sizeof(rates) / sizeof(rates[0])); i++) {
		int shot, end, fire;
		msec = rates[i];
		shot = PickupNoteTime(WP_MASTERKEY, ASEQ_FIRE, WNOTE_BOLT_OPEN);
		fire = BG_WeaponFireLength(WP_MASTERKEY);
		end = Frames(shot);

		/* Y inside the shot: the rifle comes up on the shot's last command, pump owed */
		for (f = 1; f + 1 <= end; f++) {
			run_t r = Shoot(0, qtrue, f, -1, -1, end + 8);
			CHECK(r.change == end && r.changeTo == WP_M16 && r.dropStart < 0);
			CHECK(PS->weaponstate == WEAPON_RAISING && PS->weaponDraw == WDRAW_ALT && Owes(0, qtrue));
		}
		/* back on the underbarrel it works its action before it can fire */
		Frame(0, IN_ALT);
		CHECK(cg.weaponSelect == WP_M16);	/* refused during the mode change's raise */
		Settle();
		Frame(0, IN_ALT); Settle();
		CHECK(PS->weapon == WP_MASTERKEY);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_BOLTING && Shells(0, qtrue) == BG_WeaponMagSize(WP_MASTERKEY) - 1);
		Idle(Frames(fire) + 2);
		CHECK(PS->weaponstate == WEAPON_READY && !Owes(0, qtrue));

		/* Y's command arriving two commands or more into the pump: nothing is owed */
		for (f = end + 1; f * msec < fire - msec; f++) {
			run_t r = Shoot(0, qtrue, f, -1, -1, f + 8);
			CHECK(r.change == f + 1 && r.changeTo == WP_M16 && !Owes(0, qtrue));
		}

		/* The SPAS: Y inside its shot begins its own whole drop on the shot's last
		 * command, and its pump is owed. */
		shot = PickupNoteTime(WP_SHOTGUN, ASEQ_FIRE, WNOTE_BOLT_OPEN);
		end = Frames(shot);
		for (f = 1; f + 1 <= end; f++) {
			run_t r = Shoot(1, qfalse, f, -1, -1, end + Frames(BG_WeaponDropTime(WP_SHOTGUN)) + 8);
			CHECK(r.dropStart == end && r.changeTo == WP_M16);
			CHECK((r.change - r.dropStart) * msec >= BG_WeaponDropTime(WP_SHOTGUN) && Owes(1, qfalse));
		}
	}
}

int main(void) {
	TestCycle(); TestAltToggle(); TestShootYYX(); TestSpasPaysItsDrop(); TestPumpOwed();
	printf("PASS: %i cycle, mode switch, shoot-Y-Y-X and pump checks\n", checks);
	return 0;
}
