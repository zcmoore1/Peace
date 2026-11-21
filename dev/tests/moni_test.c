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
static int forward;	/* the move every command carries (a sprint needs forward) */

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
	forward = 0;
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
	move.cmd.forwardmove = forward;
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

/* The shot is command 0. Y on commands y1 and y2 and X on command x (-1: none),
 * the trigger let go after the shot or, with holdTrigger, held all along. */
static qboolean holdTrigger;
static run_t Shoot(int slot, qboolean alternate, int y1, int y2, int x, int frames) {
	run_t r;
	int f, from;
	Setup(slot, alternate);
	Idle(4);
	from = PS->weapon;
	r.reloadStart = r.dropStart = r.change = -1; r.changeTo = WP_NONE; r.rifle = qfalse;
	for (f = 0; f < frames; f++) {
		int was = PS->weaponstate;
		Frame((f == 0 || holdTrigger ? BUTTON_ATTACK : 0) | (f == x ? BUTTON_USE_RELOAD : 0), (f == y1 || f == y2) ? IN_Y : 0);
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
 * The shot's own length (the open note) is the whole window, the trigger let
 * go or held through the shot alike: MW2's held-trigger lock is asked only as
 * the shot runs out (WeaponTimeAdjust, 0x5789DD), and there the command names
 * the gun the Ys asked for, so it locks nothing (0x578ABC-0x578ACA) and the
 * change begins (0x578C0A-0x578C31) as it would with the trigger up. */
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
		/* MW2's window also takes the second Y on X's own command or the one
		 * after it, and the first on the shot's own command (moni-b.md 3.6) */
		for (y1 = 0; y1 < end; y1++) for (x = y1 + 1; x + 1 < end; x++) for (y2 = x; y2 <= x + 1; y2++) {
			run_t r;
			if (y2 <= y1) continue;
			r = Shoot(0, qtrue, y1, y2, x, end + Frames(drop) + 8);
			CHECK(!r.rifle && r.reloadStart == x + 1 && r.dropStart < 0);
			CHECK(r.change == x + 2 && r.changeTo == WP_SHOTGUN);
			CHECK(Shells(0, qtrue) == BG_WeaponMagSize(WP_MASTERKEY) - 1 && Owes(0, qtrue));
			inside++;
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
		/* back on the underbarrel it works its action before it can fire: the
		 * pump alone, from its open note (MW2's rechamber from READY, 575b77),
		 * bolted on the next command (its bolt delay of 1, 575c00) and over
		 * Frames(rechamberTime) commands after it began, where the held
		 * trigger fires */
		Frame(0, IN_ALT);
		CHECK(cg.weaponSelect == WP_M16);	/* refused during the mode change's raise */
		Settle();
		Frame(0, IN_ALT); Settle();
		CHECK(PS->weapon == WP_MASTERKEY && Owes(0, qtrue));
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_BOLTING && Shells(0, qtrue) == BG_WeaponMagSize(WP_MASTERKEY) - 1);
		CHECK(PS->weaponAnimTime == shot && PS->weaponTime == fire - shot && Owes(0, qtrue));
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_BOLTING && !Owes(0, qtrue));
		for (f = 2; f < Frames(fire - shot); f++) {
			Frame(BUTTON_ATTACK, 0);
			CHECK(PS->weaponstate == WEAPON_BOLTING);
		}
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_FIRING && Shells(0, qtrue) == BG_WeaponMagSize(WP_MASTERKEY) - 2);

		/* Y's command on the READY think after the shot, or on the pump's bolt
		 * think after that: the mode change, and the pump is owed */
		for (f = end; f <= end + 1; f++) {
			run_t r = Shoot(0, qtrue, f, -1, -1, f + 8);
			CHECK(r.change == f + 1 && r.changeTo == WP_M16 && Owes(0, qtrue));
		}
		/* Y's command arriving after the bolt think: nothing is owed */
		for (f = end + 2; f * msec < fire - msec; f++) {
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

/* MW2's rechamber, command by command, at 8, 16 and 33ms, for both pump guns.
 * The shot's last command (its clock reaches the open note: MW2's FIRING whose
 * weaponTime runs out) ends READY owing the action (ShouldBeFiring,
 * 576d99-576da3). The next starts the action alone, from READY at the open
 * note (CheckForRechamber, 575b77-575c03: rechamberTime, the bolt
 * rechamberBoltTime in, or 1 when that is 0); the command whose clock crosses
 * the close note takes the bolt (575abd-575acc); the gun is READY, and a held
 * trigger fires, Frames(rechamberTime) commands after the action began. MW2's
 * checks run before its rechamber check (44c4d7), so a change first seen on
 * the shot's last command or on the READY one is a whole drop with the action
 * owed, one first seen on the bolt's command is instant with it owed (the
 * bolt's delayed action finishes the drop begun on its think, 579474 ->
 * 57928c: model trace mk_rech_frame), and a later one is a whole drop with it
 * paid. A sprint takes the READY gun before the action starts and waits once
 * it is under way; a knife is taken from the shot and from the READY gun, and
 * refused while the bolt is pending (MW2's knife check refuses a delayed
 * action think and a weaponDelay still running, 5776ed, 577734-577753). */
typedef struct { int slot; qboolean alternate; int weapon, other; } pumpGun_t;
static const pumpGun_t pumpGuns[] = {
	{ 0, qtrue, WP_MASTERKEY, WP_SHOTGUN },
	{ 1, qfalse, WP_SHOTGUN, WP_M16 }
};
static int shotEnd, actionStart, boltCmd, actionEnd;
/* The shot on command 0; returns with nothing run after it. */
static void PumpShot(const pumpGun_t *g) {
	int open = PickupNoteTime(g->weapon, ASEQ_FIRE, WNOTE_BOLT_OPEN);
	int close = PickupNoteTime(g->weapon, ASEQ_FIRE, WNOTE_BOLT_CLOSED);
	int fire = BG_WeaponFireLength(g->weapon);
	CHECK(open > 0 && close > open && fire > close);
	shotEnd = Frames(open);
	actionStart = shotEnd + 1;
	boltCmd = actionStart + Frames(close - open);
	actionEnd = actionStart + Frames(fire - open);
	Setup(g->slot, g->alternate);
	Idle(4);
	Frame(BUTTON_ATTACK, 0);
	CHECK(PS->weapon == g->weapon && PS->weaponstate == WEAPON_FIRING && Owes(g->slot, g->alternate));
}
static void TestRechamberTiming(void) {
	static const int rates[] = { 8, 16, 33 };
	int i, k, c, cmd;
	for (i = 0; i < (int)(sizeof(rates) / sizeof(rates[0])); i++) for (k = 0; k < 2; k++) {
		const pumpGun_t *g = &pumpGuns[k];
		int open = PickupNoteTime(g->weapon, ASEQ_FIRE, WNOTE_BOLT_OPEN);
		int fire = BG_WeaponFireLength(g->weapon), shells = BG_WeaponMagSize(g->weapon) - 1;
		int points[5], n = 0, p;
		msec = rates[i];

		/* The cycle alone, the trigger held from the READY command on. */
		PumpShot(g);
		for (cmd = 1; cmd < shotEnd; cmd++) {
			Frame(0, 0);
			CHECK(PS->weaponstate == WEAPON_FIRING && PS->weaponAnimTime == cmd * msec);
		}
		Frame(0, 0);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 0 && BG_ShotAwaitsAction(PS));
		CHECK(PS->weaponAnimTime == open && Owes(g->slot, g->alternate));
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_BOLTING && PS->weaponAnimTime == open);
		CHECK(PS->weaponTime == fire - open && Owes(g->slot, g->alternate));
		for (cmd = actionStart + 1; cmd < actionEnd; cmd++) {
			Frame(BUTTON_ATTACK, 0);
			CHECK(PS->weaponstate == WEAPON_BOLTING && Shells(g->slot, g->alternate) == shells);
			CHECK(Owes(g->slot, g->alternate) == (cmd < boltCmd));
		}
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_FIRING && Shells(g->slot, g->alternate) == shells - 1);
		/* MW2's Masterkey: the bolt on the command after the pump began */
		if (g->weapon == WP_MASTERKEY) CHECK(boltCmd == actionStart + 1);

		/* A change first seen on the shot's last command, the READY command,
		 * the command before the bolt's (where there is one), the bolt's and
		 * the one after. */
		points[n++] = shotEnd; points[n++] = actionStart;
		if (boltCmd - 1 > actionStart) points[n++] = boltCmd - 1;
		points[n++] = boltCmd; points[n++] = boltCmd + 1;
		for (p = 0; p < n; p++) {
			c = points[p];
			PumpShot(g);
			for (cmd = 1; cmd < c - 1; cmd++) Frame(0, 0);
			/* pressed on the frame before: its selection goes out on command c.
			 * From the underbarrel, the second press of that frame is the next gun. */
			if (g->alternate) CG_NextWeapon_f();
			Frame(0, IN_Y);
			CHECK(cg.weaponSelect == g->other && PS->weapon == g->weapon);
			Frame(0, 0);
			if (c == boltCmd) {
				CHECK(PS->weapon == g->other && PS->weaponstate == WEAPON_RAISING);
			} else {
				CHECK(PS->weapon == g->weapon && PS->weaponstate == WEAPON_DROPPING);
				CHECK(PS->weaponTime == BG_WeaponDropTime(g->weapon));
			}
			CHECK(Owes(g->slot, g->alternate) == (c <= boltCmd));
			CHECK(Shells(g->slot, g->alternate) == shells);
		}

		/* A sprint first on the shot's last command lowers on the READY one;
		 * first on the READY command it lowers there; either way before the
		 * action starts, which stays owed. First on the command after, the
		 * action is under way: the gun works it and lowers chambered. */
		for (c = shotEnd; c <= actionStart + 1; c++) {
			PumpShot(g);
			for (cmd = 1; cmd < c; cmd++) { forward = 127; Frame(0, 0); }
			forward = 127; Frame(BUTTON_SPRINT, 0);
			CHECK(PS->pm_flags & PMF_SPRINTING);
			if (c == shotEnd) {
				CHECK(PS->weaponstate == WEAPON_READY && BG_ShotAwaitsAction(PS));
				forward = 127; Frame(0, 0);
			}
			if (c <= actionStart) {
				CHECK(PS->weaponstate == WEAPON_SPRINT_IN && PS->weaponTime == BG_WeaponSprintInTime(g->weapon));
				CHECK(PS->weaponAnimTime == open && Owes(g->slot, g->alternate));
			} else {
				for (cmd = c; cmd < actionEnd; cmd++) {
					CHECK(PS->weaponstate == WEAPON_BOLTING && (PS->pm_flags & PMF_SPRINTING));
					forward = 127; Frame(0, 0);
				}
				CHECK(PS->weaponstate == WEAPON_READY && !Owes(g->slot, g->alternate));
				forward = 127; Frame(0, 0);
				CHECK(PS->weaponstate == WEAPON_SPRINT_IN);
			}
		}

		/* A knife on the shot's last command, the READY one, the commands the
		 * bolt is pending on, and the one after the bolt. */
		for (c = shotEnd; c <= boltCmd + 1; c++) {
			qboolean taken = c <= actionStart || c > boltCmd;
			PumpShot(g);
			for (cmd = 1; cmd < c; cmd++) Frame(0, 0);
			Frame(BUTTON_MELEE, 0);
			if (taken) {
				CHECK(PS->weapon == WP_KNIFE && (PS->weaponAction & WA_TYPE_MASK) == WA_MELEE);
				CHECK(Owes(g->slot, g->alternate) == (c <= boltCmd));
			} else {
				CHECK(PS->weapon == g->weapon && PS->weaponstate == WEAPON_BOLTING);
				CHECK(!(PS->weaponAction & WA_TYPE_MASK));
			}
		}
	}
}

/* The rechamber with the trigger held from the shot: MW2's lock makes the gun
 * READY on the shot's last command with weaponTime 1 (WeaponTimeAdjust,
 * 0x5789DD-0x578AE6), and CheckForRechamber starts the action from that READY
 * on that same command (0x575B07-0x575BA9: READY, no weaponDelay), so the bolt
 * and the end come a command sooner, and the action ends READY under the lock.
 * A change first seen on the shot's last command is a whole drop with the
 * action owed (the command names the other gun: no lock); on the bolt's
 * command, now the one after the shot's last on the Masterkey, the instant
 * swap with it owed (the bolt's delayed action finishes the drop begun on its
 * think, 0x579474, 0x57928C); a command later a whole drop with it paid. */
static void TestHeldRechamber(void) {
	static const int rates[] = { 8, 16, 33 };
	int i, k, c, cmd;
	for (i = 0; i < (int)(sizeof(rates) / sizeof(rates[0])); i++) for (k = 0; k < 2; k++) {
		const pumpGun_t *g = &pumpGuns[k];
		int open = PickupNoteTime(g->weapon, ASEQ_FIRE, WNOTE_BOLT_OPEN);
		int close = PickupNoteTime(g->weapon, ASEQ_FIRE, WNOTE_BOLT_CLOSED);
		int fire = BG_WeaponFireLength(g->weapon), shells = BG_WeaponMagSize(g->weapon) - 1;
		int heldBolt, heldEnd, points[4], n = 0, p;
		msec = rates[i];
		PumpShot(g);
		heldBolt = shotEnd + Frames(close - open);
		heldEnd = shotEnd + Frames(fire - open);
		CHECK(heldBolt == boltCmd - 1 && heldEnd == actionEnd - 1);
		for (cmd = 1; cmd < shotEnd; cmd++) {
			Frame(BUTTON_ATTACK, 0);
			CHECK(PS->weaponstate == WEAPON_FIRING);
		}
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_BOLTING && PS->weaponAnimTime == open && Owes(g->slot, g->alternate));
		for (cmd = shotEnd + 1; cmd < heldEnd; cmd++) {
			Frame(BUTTON_ATTACK, 0);
			CHECK(PS->weaponstate == WEAPON_BOLTING && Owes(g->slot, g->alternate) == (cmd < heldBolt));
		}
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 1 && Shells(g->slot, g->alternate) == shells);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_READY && Shells(g->slot, g->alternate) == shells);
		if (g->weapon == WP_MASTERKEY) CHECK(heldBolt == shotEnd + 1);

		points[n++] = shotEnd;
		if (heldBolt - 1 > shotEnd) points[n++] = heldBolt - 1;
		points[n++] = heldBolt; points[n++] = heldBolt + 1;
		for (p = 0; p < n; p++) {
			c = points[p];
			PumpShot(g);
			for (cmd = 1; cmd < c - 1; cmd++) Frame(BUTTON_ATTACK, 0);
			if (g->alternate) CG_NextWeapon_f();
			Frame(BUTTON_ATTACK, IN_Y);
			CHECK(cg.weaponSelect == g->other && PS->weapon == g->weapon);
			Frame(BUTTON_ATTACK, 0);
			if (c == heldBolt) {
				CHECK(PS->weapon == g->other && PS->weaponstate == WEAPON_RAISING);
			} else {
				CHECK(PS->weapon == g->weapon && PS->weaponstate == WEAPON_DROPPING);
				CHECK(PS->weaponTime == BG_WeaponDropTime(g->weapon));
			}
			CHECK(Owes(g->slot, g->alternate) == (c <= heldBolt));
			CHECK(Shells(g->slot, g->alternate) == shells);
		}
	}
}

int main(void) {
	TestCycle(); TestAltToggle(); TestShootYYX(); TestSpasPaysItsDrop(); TestPumpOwed();
	holdTrigger = qtrue; TestShootYYX(); holdTrigger = qfalse;
	TestRechamberTiming(); TestHeldRechamber();
	printf("PASS: %i cycle, mode switch, shoot-Y-Y-X, pump and rechamber checks\n", checks);
	return 0;
}
