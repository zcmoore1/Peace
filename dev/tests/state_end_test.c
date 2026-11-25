/* What MW2 does on the command a weapon state ends, through the real Pmove at
 * 8, 16 and 33ms commands.
 *
 * MW2's ProcessHand (iw4x 0x579450) handles a state whose time has run out
 * in its state switch (the jump table at 0x5793D8) and returns before
 * ShouldBeFiring (0x5794B4-0x5794B6): on that command the gun neither fires nor
 * starts anything the switch did not, and every check of the command saw the
 * old state. Its checks run first, in this order (PM_Weapon 0x44C380):
 * sprint 0x578060, offhand 0x579030, change 0x578BC0 (the double-tap cancel
 * inside it, 0x578D00), reload 0x578670, knife 0x577670 (passed the command's
 * delayedAction, 0x44C4E3-0x44C4FC), then the hand. A command whose weapon
 * delay runs out skips the hand's time gate (0x579474), so the switch runs on
 * whatever state the checks left (path A).
 *
 * Only buttons are pressed. Every time compared against is read from the
 * game's tables or from the lock pmove stamped, never written here. */
#include "../../code/game/bg_pmove.c"

static playerState_t ps;
static pmove_t move;
static int checks, msec, buttons, forward, slot, stale, cmdWeapon;
static int fired[MAX_WEAPONS], prepared[MAX_WEAPONS];

#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "FAIL line %d (%d ms commands): %s\n", __LINE__, msec, #x); exit(1); } } while (0)
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level, const char *fmt, ...) {
	va_list args; (void)level; va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args); exit(1);
}
/* The engine's Q_SnapVector rounds to nearest, as MW2's does. */
void trap_SnapVector(float *v) { v[0] = rintf(v[0]); v[1] = rintf(v[1]); v[2] = rintf(v[2]); }

/* A flat floor at z 0 (loadout_test.c's), for the full Pmove on the ground. */
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

/* A spawned player on the floor, the primary in hand and ready; with
 * 'lethal' the class carries that lethal instead of its own. */
static void SetupClass(int primary, int secondary, int lethal) {
	bg_class_t cl = *BG_Class(0);
	memset(&ps, 0, sizeof(ps)); memset(&move, 0, sizeof(move));
	memset(fired, 0, sizeof(fired)); memset(prepared, 0, sizeof(prepared));
	cl.slot[0] = primary; cl.slot[1] = secondary; cl.attachments[0] = cl.attachments[1] = 0;
	if (lethal) { cl.lethal = lethal; cl.lethalCount = BG_ClassEquipmentCount(lethal); }
	CHECK(BG_ValidateClass(&cl));
	BG_ApplyClass(&ps, &cl);
	BG_SpawnStance(&ps);
	ps.stats[STAT_HEALTH] = 100;
	ps.pm_type = PM_NORMAL; ps.speed = BG_PLAYER_SPEED; ps.gravity = 800;
	ps.origin[2] = 24; ps.groundEntityNum = ENTITYNUM_WORLD; ps.commandTime = 1000;
	ps.slotFlags[SLOT_PRIMARY] &= ~BG_GUN_FIRST_DRAW; ps.slotFlags[SLOT_SECONDARY] &= ~BG_GUN_FIRST_DRAW;
	BG_SelectSlot(&ps, SLOT_PRIMARY);
	ps.weapon = BG_ActiveSlotWeapon(&ps);
	ps.weaponstate = WEAPON_READY; ps.weaponTime = 0; ps.weaponAnimTime = -1;
	move.ps = &ps; move.trace = FloorTrace; move.pointcontents = NoContents; move.tracemask = MASK_PLAYERSOLID;
	buttons = forward = stale = cmdWeapon = 0; slot = SLOT_PRIMARY;
}
static void Setup(int primary, int secondary) { SetupClass(primary, secondary, 0); }
/* One command, msec long, through the whole Pmove. With 'stale' the slot's
 * command carries the other generation, as one sent before the client saw a
 * pickup's take: it names no gun (BG_SlotCommandCurrent), so an empty hand
 * stays empty. A nonzero 'cmdWeapon' is sent as the command instead. */
static void Frame(void) {
	int seq = ps.eventSequence;
	memset(&move.cmd, 0, sizeof(move.cmd));
	move.cmd.buttons = buttons; move.cmd.forwardmove = forward;
	move.cmd.weapon = cmdWeapon ? cmdWeapon : BG_SlotCommand(&ps, slot);
	if (stale) move.cmd.weapon = BG_SLOT_COMMAND + ((move.cmd.weapon - BG_SLOT_COMMAND) ^ SLOT_COUNT);
	move.cmd.serverTime = ps.commandTime + msec;
	Pmove(&move);
	for (; seq < ps.eventSequence; seq++) {
		int event = ps.events[seq & (MAX_PS_EVENTS - 1)], parm = ps.eventParms[seq & (MAX_PS_EVENTS - 1)];
		if (parm < 0 || parm >= MAX_WEAPONS) continue;
		if (event == EV_FIRE_WEAPON) fired[parm]++;
		if (event == EV_PREP_OFFHAND) prepared[parm]++;
	}
}
/* Whole commands a lock of that length keeps: stamped on command c, it runs
 * out on command c + Commands(ms). */
static int Commands(int ms) { return (ms + msec - 1) / msec; }
static int Other(int gun) { return gun == WP_MACHINEGUN ? WP_SHOTGUN : WP_MACHINEGUN; }
static int Owes(int gun) { return (ps.stats[STAT_UNCHAMBERED] & (1 << gun)) != 0; }
static int Open(int gun) { int open = PM_ShotLength(gun); return open < 0 ? 0 : open; }
static int ActionLength(int gun) { return BG_WeaponFireLength(gun) - Open(gun); }

/* Into the sprint carry. With 'shoot', the gun's shot comes first and a
 * sprint tapped during it takes the gun once the shot is over (on a pump or
 * bolt gun the READY command after it: MW2's sprint check runs before its
 * rechamber check), so its action is still owed. */
static void IntoTheCarry(int gun, int shoot) {
	int t, owed = shoot && BG_WeaponFireLength(gun) > 0;
	forward = 127;
	if (shoot) {
		buttons = BUTTON_ATTACK; Frame();
		CHECK(fired[gun] == 1 && ps.weaponstate == WEAPON_FIRING && Owes(gun) == owed);
	}
	buttons = BUTTON_SPRINT; Frame(); buttons = 0;	/* a tap: MW2's sprint latches */
	CHECK(ps.pm_flags & PMF_SPRINTING);
	for (t = 0; t < 10000 && ps.weaponstate != WEAPON_SPRINTING; t += msec) Frame();
	/* no time over: MW2's crossing drops the overshoot (0x578B71) and
	   Sprint_State_Loop zeroes the time again (0x577F82) */
	CHECK(ps.weaponstate == WEAPON_SPRINTING && ps.weaponTime == 0 && Owes(gun) == owed);
}
/* Run the sprint-out stamped on the last command to the command before its
 * last, checking it holds every command. */
static void ThroughTheSprintOut(int gun) {
	int k;
	CHECK(ps.weaponstate == WEAPON_SPRINT_OUT && ps.weaponTime == BG_WeaponSprintOutTime(gun));
	for (k = 1; k < Commands(BG_WeaponSprintOutTime(gun)); k++) {
		Frame();
		CHECK(ps.weaponstate == WEAPON_SPRINT_OUT && ps.weaponTime == BG_WeaponSprintOutTime(gun) - k * msec);
	}
}

/* D1. SPRINT_DROP's last command: MW2's switch makes the gun READY in
 * PM_Weapon_Idle (0x5793D8 entry 25 -> 0x579279 -> 0x5777A0), with no time
 * over (the crossing dropped the overshoot, 0x578B71, and PM_Weapon_Idle
 * zeroes weaponTime and weaponDelay, 0x5777CE-0x5777D8), and the command
 * is over. The rechamber check ran first and starts nothing out of
 * SPRINT_DROP (0x575B77), the auto reload refuses the sprint states
 * (0x578872-0x57887A), and nothing fires: an owed pump or bolt, the shot and
 * an empty magazine's reload all come on the next command. A reload asked for
 * on it is taken (the reload check runs first and takes the sprint states,
 * 0x575E8E-0x575EA8). */
static void TestSprintOutEnds(int gun) {
	/* the action owed: the sprint ends when forward is let go */
	if (BG_WeaponFireLength(gun) > 0) {
		Setup(gun, Other(gun)); IntoTheCarry(gun, 1);
		forward = 0; Frame();
		ThroughTheSprintOut(gun);
		Frame();
		CHECK(ps.weaponstate == WEAPON_READY && ps.weaponTime == 0 && ps.weaponAnimTime == -1 && Owes(gun));
		Frame();
		CHECK(ps.weaponstate == WEAPON_BOLTING && ps.weaponAnimTime == Open(gun));
		CHECK(ps.weaponTime == ActionLength(gun) && fired[gun] == 1);
	}
	/* the trigger ends the sprint and is held: the shot is the command after */
	Setup(gun, Other(gun)); IntoTheCarry(gun, 0);
	buttons = BUTTON_ATTACK; Frame();
	CHECK(!(ps.pm_flags & PMF_SPRINTING));
	ThroughTheSprintOut(gun);
	CHECK(fired[gun] == 0);
	Frame();
	CHECK(ps.weaponstate == WEAPON_READY && ps.weaponTime == 0 && fired[gun] == 0);
	Frame();
	CHECK(ps.weaponstate == WEAPON_FIRING && fired[gun] == 1);
	/* the last round went in the sprint: the empty gun reloads the command after */
	Setup(gun, Other(gun)); ps.ammo[gun] = 1;
	IntoTheCarry(gun, 1);
	CHECK(ps.ammo[gun] == 0 && ps.ammoReserve[gun] > 0);
	forward = 0; Frame();
	ThroughTheSprintOut(gun);
	Frame();
	CHECK(ps.weaponstate == WEAPON_READY && ps.ammo[gun] == 0);
	Frame();
	CHECK(ps.weaponstate == WEAPON_RELOADING && ps.weaponAnimSeq == RSEQ_START && ps.weaponAnimTime == 0);
	/* a reload pressed on the last command is taken on it, in MW2 as here */
	Setup(gun, Other(gun)); ps.ammo[gun] = BG_WeaponMagSize(gun) - 1;
	IntoTheCarry(gun, 0);
	forward = 0; Frame();
	ThroughTheSprintOut(gun);
	buttons = BUTTON_RELOAD; Frame(); buttons = 0;
	CHECK(ps.weaponstate == WEAPON_RELOADING && ps.weaponAnimSeq == RSEQ_START && ps.weaponAnimTime == 0);
}

/* D2. A sprint on again during the sprint-out: MW2's sprint check does
 * nothing while sprinting in SPRINT_RAISE, SPRINT_LOOP or SPRINT_DROP
 * (0x5780F4-0x578107), so the sprint-out runs to its end, PM_Weapon_Idle
 * readies the gun, and Sprint_State_Raise starts the whole sprint entry from
 * READY on the next command (0x5780FE -> 0x577EE0). */
static void TestSprintAgainInTheSprintOut(int gun) {
	int k, half;
	Setup(gun, Other(gun)); IntoTheCarry(gun, 0);
	forward = 0; Frame();
	CHECK(ps.weaponstate == WEAPON_SPRINT_OUT && !(ps.pm_flags & PMF_SPRINTING));
	half = Commands(BG_WeaponSprintOutTime(gun)) / 2;
	CHECK(half >= 1);
	for (k = 1; k < half; k++) Frame();
	forward = 127; buttons = BUTTON_SPRINT; Frame(); buttons = 0;	/* a fresh press */
	CHECK((ps.pm_flags & PMF_SPRINTING) && ps.weaponstate == WEAPON_SPRINT_OUT);
	for (k = half + 1; k < Commands(BG_WeaponSprintOutTime(gun)); k++) {
		Frame();
		CHECK((ps.pm_flags & PMF_SPRINTING) && ps.weaponstate == WEAPON_SPRINT_OUT &&
			ps.weaponTime == BG_WeaponSprintOutTime(gun) - k * msec);
	}
	Frame();
	CHECK((ps.pm_flags & PMF_SPRINTING) && ps.weaponstate == WEAPON_READY);
	Frame();
	CHECK(ps.weaponstate == WEAPON_SPRINT_IN && ps.weaponTime == BG_WeaponSprintInTime(gun));
}

/* D3. The gun's quick raise after the knife (MW2's MELEE_END) or equipment
 * (OFFHAND_END), and an empty hand's return (OFFHAND_END with weapon 0, a
 * one-command weaponDelay, 0x577A1A-0x577A2A): MW2's change check refuses
 * those states whatever their time (0x578BC9-0x578BF2), so a swap queued
 * behind the return begins from READY on the command after it ends, and
 * equipment pressed on that command still comes out (its offhand check takes
 * them, 0x579086-0x579096). */
static void ToTheQuickRaise(int gun, int press) {
	int t;
	buttons = press; Frame(); buttons = 0;
	for (t = 0; t < 20000 && !(ps.weapon == gun && ps.weaponstate == WEAPON_RAISING); t += msec) Frame();
	CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_RAISING && ps.weaponTime == BG_WeaponQuickRaiseTime(gun));
	CHECK(ps.weaponAction & WA_FIRED);
}
static void TestReturnEnds(int gun) {
	const int presses[] = { BUTTON_MELEE, BUTTON_LETHAL };
	const int offhands[] = { BUTTON_LETHAL, BUTTON_TACTICAL };
	int p, k, t, lethal, equip;
	for (p = 0; p < ARRAY_LEN(presses); p++) {
		/* a Y in the quick raise: the drop starts the command after it ends */
		Setup(gun, Other(gun)); ToTheQuickRaise(gun, presses[p]);
		slot = SLOT_SECONDARY;
		for (k = 1; k < Commands(BG_WeaponQuickRaiseTime(gun)); k++) {
			Frame();
			CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_RAISING);
		}
		Frame();
		/* no time over: MW2's crossing dropped it (0x578B71) before
		   PM_Weapon_Idle readied the gun (0x5777CE) */
		CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_READY && ps.weaponTime == 0);
		CHECK(!(ps.weaponAction & WA_TYPE_MASK));
		Frame();
		CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_DROPPING && ps.weaponTime == BG_WeaponDropTime(gun));
		/* equipment pressed on that last command comes out on it, on the quick drop */
		Setup(gun, Other(gun)); ToTheQuickRaise(gun, presses[p]);
		CHECK(ps.ammo[WP_FLASH] > 0);
		for (k = 1; k < Commands(BG_WeaponQuickRaiseTime(gun)); k++) Frame();
		buttons = BUTTON_TACTICAL; Frame(); buttons = 0;
		CHECK((ps.weaponAction & (WA_TYPE_MASK | WA_FIRED)) == WA_TACTICAL);
		CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_DROPPING && ps.weaponTime == BG_WeaponQuickDropTime(gun));
	}
	/* equipment out of an empty hand, the lethal or the tactical, comes back
	 * to one (MW2's OffHandEnd with weapon 0 does not look at the offhand,
	 * 0x577A10-0x577A2A); a gun selected all along is drawn the command after
	 * the hand's return ends, on its ordinary raise */
	for (p = 0; p < ARRAY_LEN(offhands); p++) {
		Setup(gun, Other(gun));
		ps.weapon = WP_NONE; ps.cursorHintEntIndex = ENTITYNUM_NONE;
		equip = offhands[p] == BUTTON_TACTICAL ? WP_FLASH : BG_LethalWeapon(&ps);
		CHECK(ps.ammo[equip] > 0);
		buttons = offhands[p]; Frame(); buttons = 0;
		CHECK(ps.weapon == equip && (ps.weaponAction & WA_LEFT_EMPTY));
		for (t = 0; t < 20000 && ps.weapon != WP_NONE; t += msec) Frame();
		CHECK(ps.weapon == WP_NONE && ps.weaponstate == WEAPON_READY && fired[equip] == 1);	/* E */
		CHECK((ps.weaponAction & (WA_FIRED | WA_LEFT_EMPTY)) == (WA_FIRED | WA_LEFT_EMPTY));
		Frame();	/* E+1: OFFHAND_END's delay command, nothing begins */
		CHECK(ps.weapon == WP_NONE && ps.weaponstate == WEAPON_READY && !(ps.weaponAction & WA_TYPE_MASK));
		Frame();	/* E+2: the gun's ordinary draw */
		CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_RAISING && ps.weaponTime == BG_WeaponDrawTime(&ps));
		CHECK(ps.weaponTime == BG_WeaponRaiseTime(gun));
	}
	/* that command is the end of OFFHAND_END's one-command delay: equipment
	 * pressed on it, with a use hint (no sea lion), is prepared on it. MW2's
	 * offhand check takes OFFHAND_END and OffHandInit stamps the empty hand's
	 * drop, then the hand's switch, its time gate skipped for the delay
	 * (0x579474), prepares it (OFFHAND_INIT's case, 0x5792C7 -> 0x577800).
	 * The first throw, out of an empty hand on no delay, pays that drop. */
	Setup(gun, Other(gun));
	ps.weapon = WP_NONE;
	CHECK(ps.cursorHintEntIndex != ENTITYNUM_NONE && ps.ammo[WP_FLASH] > 0);
	lethal = BG_LethalWeapon(&ps);
	buttons = BUTTON_LETHAL; Frame(); buttons = 0;
	CHECK(ps.weapon == WP_NONE && ps.weaponstate == WEAPON_DROPPING && ps.weaponTime == BG_HolsterTime(&ps));
	CHECK(BG_HolsterTime(&ps) > 0);
	for (t = 0; t < 20000 && ps.weapon == WP_NONE; t += msec) Frame();
	CHECK(ps.weapon == lethal && prepared[lethal] == 1);
	for (t = 0; t < 20000 && ps.weapon != WP_NONE; t += msec) Frame();
	CHECK(ps.weapon == WP_NONE && ps.weaponstate == WEAPON_READY && fired[lethal] == 1);
	buttons = BUTTON_TACTICAL; Frame(); buttons = 0;
	CHECK((ps.weaponAction & (WA_TYPE_MASK | WA_FIRED)) == WA_TACTICAL);
	CHECK(ps.weapon == WP_FLASH && ps.weaponstate == WEAPON_RAISING && prepared[WP_FLASH] == 1);
	CHECK(ps.weaponTime == BG_WeaponRaiseTime(WP_FLASH));	/* the prepare plays whole */
}

/* With no use hint, equipment comes straight out of an empty hand
 * (CheckForOffHand's OffHandPrepare, 0x5791B0-0x5791CE: hint 0x7FF and
 * weapon 0). On the command the hand's return ends, the run-out of
 * OFFHAND_END's one-command delay, the hand's switch then runs on
 * OFFHAND_PREPARE whatever its time (0x579474), and that state's case is
 * OffHandHold (the entry for state 17, 0x5792DA -> 0x577920): the equipment
 * is in hand and ready on that command, its fuse written (0x577958-0x577961),
 * its prepare never played. A command later no delay runs out, and the prepare
 * plays whole. */
static void TestStraightOutOfTheReturn(int gun) {
	int late, t, lethal;
	for (late = 0; late < 2; late++) {
		Setup(gun, Other(gun));
		ps.weapon = WP_NONE; ps.cursorHintEntIndex = ENTITYNUM_NONE;
		stale = 1;	/* the hand stays empty once the throw is over */
		lethal = BG_LethalWeapon(&ps);
		buttons = BUTTON_LETHAL; Frame(); buttons = 0;
		CHECK(ps.weapon == lethal && (ps.weaponAction & WA_LEFT_EMPTY) && ps.weaponstate == WEAPON_RAISING);
		CHECK(ps.weaponTime == BG_WeaponRaiseTime(lethal));	/* no delay ran out: it plays */
		for (t = 0; t < 20000 && ps.weapon != WP_NONE; t += msec) Frame();
		CHECK(ps.weapon == WP_NONE && ps.weaponstate == WEAPON_READY && fired[lethal] == 1);	/* E */
		if (late) {
			Frame();	/* E+1, nothing pressed: the return is over */
			CHECK(ps.weapon == WP_NONE && ps.weaponstate == WEAPON_READY && !(ps.weaponAction & WA_TYPE_MASK));
		}
		CHECK(ps.ammo[WP_FLASH] > 0 && BG_WeaponRaiseTime(WP_FLASH) > msec && prepared[WP_FLASH] == 0);
		buttons = BUTTON_TACTICAL; Frame(); buttons = 0;
		CHECK(ps.weapon == WP_FLASH && (ps.weaponAction & (WA_TYPE_MASK | WA_FIRED)) == WA_TACTICAL);
		CHECK(prepared[WP_FLASH] == 1 && (ps.weaponAction & WA_LEFT_EMPTY));
		if (late) {
			CHECK(ps.weaponstate == WEAPON_RAISING && ps.weaponTime == BG_WeaponRaiseTime(WP_FLASH));
			continue;
		}
		CHECK(ps.weaponstate == WEAPON_READY && ps.weaponTime == 0);
		CHECK(ps.grenadeTimeLeft == BG_OffhandFuseTime(WP_FLASH));
		Frame();	/* the button is up: HOLD's first command throws it */
		CHECK(fired[WP_FLASH] == 1);
	}
}

/* A knife pressed on the command after equipment's return to an empty hand is
 * lost, as in MW2: that command is OFFHAND_END (state 21) with weapon 0, whose
 * one-command delay runs out on it (OffHandEnd, 0x577A1A-0x577A2A, the same
 * for every offhand, the flash as the frag), and MW2's knife check refuses
 * the offhand states (0x5776AE-0x5776B6) and any command a delay runs out on
 * (0x5776ED). A command later the hand is READY with no delay, and the knife
 * comes out. */
static void TestKnifeAfterTheReturn(int gun) {
	const int offhands[] = { BUTTON_LETHAL, BUTTON_TACTICAL };
	int late, t, p, equip;
	for (p = 0; p < ARRAY_LEN(offhands); p++) for (late = 0; late < 2; late++) {
		Setup(gun, Other(gun));
		ps.weapon = WP_NONE; ps.cursorHintEntIndex = ENTITYNUM_NONE;
		stale = 1;	/* the hand stays empty once the throw is over */
		equip = offhands[p] == BUTTON_TACTICAL ? WP_FLASH : BG_LethalWeapon(&ps);
		CHECK(ps.ammo[equip] > 0);
		buttons = offhands[p]; Frame(); buttons = 0;
		CHECK(ps.weapon == equip && (ps.weaponAction & WA_LEFT_EMPTY));
		for (t = 0; t < 20000 && ps.weapon != WP_NONE; t += msec) Frame();
		CHECK(ps.weapon == WP_NONE && ps.weaponstate == WEAPON_READY && fired[equip] == 1);	/* E */
		CHECK((ps.weaponAction & (WA_FIRED | WA_LEFT_EMPTY)) == (WA_FIRED | WA_LEFT_EMPTY));
		if (late) Frame();
		buttons = BUTTON_MELEE; Frame(); buttons = 0;	/* E+1, or E+2 */
		if (late) {
			CHECK(ps.weapon == WP_KNIFE && ps.weaponstate == WEAPON_RAISING && ps.weaponTime == BG_MELEE_DELAY);
			CHECK((ps.weaponAction & (WA_TYPE_MASK | WA_FIRED | WA_LEFT_EMPTY)) == (WA_MELEE | WA_LEFT_EMPTY));
			continue;
		}
		CHECK(ps.weapon == WP_NONE && ps.weaponstate == WEAPON_READY && !(ps.weaponAction & WA_TYPE_MASK));
		Frame();	/* and the press is gone */
		CHECK(ps.weapon == WP_NONE && !(ps.weaponAction & WA_TYPE_MASK) && fired[WP_KNIFE] == 0);
	}
}

/* The knife's return to an empty hand is Peace's own: MW2 always has a gun
 * under its knife. Its knife ends in MeleeEnd, which stamps no weapon delay:
 * MELEE_END for the gun's quickRaiseTime with weaponDelay 0 (0x578DAD-
 * 0x578DC3), or, with no knife model to raise, PM_Weapon_Idle at once
 * (0x578DF3-0x578DF7: READY, no time, no delay, 0x5777C7-0x5777D8). With no
 * gun to raise Peace's knife ends in that Idle: the hand empty and READY on
 * the knife's last command, and the command after an ordinary READY command, with
 * no delay running out on it and in no state MW2's knife check refuses (it
 * refuses a delay command and the melee and offhand states, 0x577693-0x5776F2,
 * this command none of them). So a knife pressed there comes out, a gun asked for all
 * along is drawn there, and equipment with no use hint comes straight out of
 * the empty hand and plays its prepare. (Equipment's own return to an empty
 * hand is OFFHAND_END's one-command delay: TestReturnEnds, TestKnifeAfterTheReturn.) */
static void TestKnifeBackToAnEmptyHand(int gun) {
	int what, t;
	for (what = 0; what < 3; what++) {
		Setup(gun, Other(gun));
		ps.weapon = WP_NONE; ps.cursorHintEntIndex = ENTITYNUM_NONE;
		stale = what != 1;	/* 1: the gun asked for all along */
		buttons = BUTTON_MELEE; Frame(); buttons = 0;
		CHECK(ps.weapon == WP_KNIFE && ps.weaponstate == WEAPON_RAISING && (ps.weaponAction & WA_LEFT_EMPTY));
		for (t = 0; t < 20000 && ps.weapon != WP_NONE; t += msec) Frame();
		/* E: the knife's last command, the hand empty and READY */
		CHECK(ps.weapon == WP_NONE && ps.weaponstate == WEAPON_READY && ps.weaponTime == 0 && fired[WP_KNIFE] == 1);
		CHECK((ps.weaponAction & (WA_TYPE_MASK | WA_FIRED | WA_LEFT_EMPTY)) == (WA_MELEE | WA_FIRED | WA_LEFT_EMPTY));
		if (what == 0) {
			buttons = BUTTON_MELEE; Frame(); buttons = 0;	/* E+1 */
			CHECK(ps.weapon == WP_KNIFE && ps.weaponstate == WEAPON_RAISING && ps.weaponTime == BG_MELEE_DELAY);
			CHECK((ps.weaponAction & (WA_TYPE_MASK | WA_FIRED | WA_LEFT_EMPTY)) == (WA_MELEE | WA_LEFT_EMPTY));
		} else if (what == 1) {
			Frame();	/* E+1 */
			CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_RAISING && ps.weaponTime == BG_WeaponDrawTime(&ps));
			CHECK(!(ps.weaponAction & (WA_TYPE_MASK | WA_LEFT_EMPTY)));
		} else {
			CHECK(ps.ammo[WP_FLASH] > 0 && BG_WeaponRaiseTime(WP_FLASH) > msec);
			buttons = BUTTON_TACTICAL; Frame(); buttons = 0;	/* E+1 */
			CHECK(ps.weapon == WP_FLASH && (ps.weaponAction & (WA_TYPE_MASK | WA_FIRED | WA_LEFT_EMPTY)) == (WA_TACTICAL | WA_LEFT_EMPTY));
			CHECK(ps.weaponstate == WEAPON_RAISING && ps.weaponTime == BG_WeaponRaiseTime(WP_FLASH) && prepared[WP_FLASH] == 1);
		}
	}
}

/* Equipment pressed during a knife drawn out of an empty hand. Peace's own
 * case (MW2's knife check needs a gun's melee, 0x5776E0), played by MW2's
 * offhand code with weapon 0 under the knife, as its knife's end is
 * (TestKnifeBackToAnEmptyHand): MeleeInit writes the melee states over the
 * hand and never changes its weapon, so every check that reads the weapon
 * reads 0. With no use hint, CheckForOffHand takes OffHandPrepare at once
 * (0x5791B0-0x5791CE): the equipment comes out on its press and plays its
 * prepare, and on the knife's hit command, a delay command, the hand's switch
 * runs OffHandHold on it (0x579474, 0x5792DA): in hand and ready there, its
 * prepare unplayed, no hit. With a hint, OffHandInit stamps weapon 0's
 * 100 ms (0x578E70-0x578ED3), not a gun's quick drop. Its end is OffHandEnd
 * with weapon 0 (0x577A1A-0x577A2A): the hand empty again, OFFHAND_END's
 * one-command delay, then a gun the command names on its ordinary raise, or,
 * with a stale command, the hand stays empty. */
static void KnifeFromAnEmptyHand(int gun, int hint) {
	Setup(gun, Other(gun));
	ps.weapon = WP_NONE;
	if (!hint) ps.cursorHintEntIndex = ENTITYNUM_NONE;
	CHECK((ps.cursorHintEntIndex != ENTITYNUM_NONE) == hint);
	buttons = BUTTON_MELEE; Frame(); buttons = 0;	/* K */
	CHECK(ps.weapon == WP_KNIFE && ps.weaponstate == WEAPON_RAISING && ps.weaponTime == BG_MELEE_DELAY);
	CHECK((ps.weaponAction & (WA_TYPE_MASK | WA_FIRED | WA_LEFT_EMPTY)) == (WA_MELEE | WA_LEFT_EMPTY));
}
static void TestEquipmentOverTheKnifeFromAnEmptyHand(int gun) {
	int when, hint, k, t, lethal, hit, raise, st;
	hit = Commands(BG_MELEE_DELAY);	/* the hit's command, counted from K */
	CHECK(hit > 2 && Commands(BG_MELEE_TIME) > hit + 2);
	/* when 0: in the knife's draw (K+2); 1: on its hit's command; 2: after the hit (H+2) */
	for (when = 0; when < 3; when++) for (hint = 0; hint < 2; hint++) {
		KnifeFromAnEmptyHand(gun, hint);
		lethal = BG_LethalWeapon(&ps);
		raise = BG_WeaponRaiseTime(lethal);
		CHECK(raise > 2 * msec);
		for (k = 1; k < (when == 0 ? 2 : when == 1 ? hit : hit + 2); k++) Frame();
		CHECK(ps.weapon == WP_KNIFE && fired[WP_KNIFE] == (when == 2));
		buttons = BUTTON_LETHAL; Frame(); buttons = 0;
		CHECK((ps.weaponAction & (WA_TYPE_MASK | WA_FIRED | WA_LEFT_EMPTY)) == (WA_LETHAL | WA_LEFT_EMPTY));
		CHECK(fired[WP_KNIFE] == (when == 2));
		if (when == 1 && !hint) {
			/* the hit's command: in hand and ready on it, the fuse written */
			CHECK(ps.weapon == lethal && ps.weaponstate == WEAPON_READY && ps.weaponTime == 0);
			CHECK(prepared[lethal] == 1 && ps.grenadeTimeLeft == BG_OffhandFuseTime(lethal));
			Frame();	/* the button is up: HOLD's first command throws it */
			CHECK(fired[lethal] == 1 && fired[WP_KNIFE] == 0);
			continue;
		}
		if (hint && when != 1) {
			/* weapon 0's drop, with nothing of a gun's in the picture */
			CHECK(ps.weapon == WP_KNIFE && ps.weaponstate == WEAPON_DROPPING);
			CHECK(ps.weaponTime == BG_EMPTY_HAND_OFFHAND_TIME && BG_HolsterTime(&ps) == BG_EMPTY_HAND_OFFHAND_TIME);
			CHECK(BG_ViewmodelWeapon(&ps) == WP_NONE && prepared[lethal] == 0);
			for (k = 1; k < Commands(BG_EMPTY_HAND_OFFHAND_TIME); k++) {
				Frame();
				CHECK(ps.weapon == WP_KNIFE && ps.weaponstate == WEAPON_DROPPING && prepared[lethal] == 0);
			}
			Frame();
		}
		/* straight out, or after the drop (or the hit's path A with a hint): the prepare plays */
		CHECK(ps.weapon == lethal && ps.weaponstate == WEAPON_RAISING && ps.weaponTime == raise);
		CHECK(prepared[lethal] == 1 && (ps.weaponAction & WA_LEFT_EMPTY));
		for (k = 1; k < Commands(raise); k++) { Frame(); CHECK(ps.weapon == lethal && ps.weaponstate == WEAPON_RAISING); }
		Frame();
		CHECK(ps.weapon == lethal && ps.weaponstate == WEAPON_READY && ps.weaponTime == 0);
		Frame();
		CHECK(fired[lethal] == 1 && fired[WP_KNIFE] == (when == 2));
	}
	/* the end: the hand empty again, OFFHAND_END's delay command, then the gun
	   the command names on its ordinary raise, or none with a stale command */
	for (st = 0; st < 2; st++) {
		KnifeFromAnEmptyHand(gun, 0);
		stale = st;
		lethal = BG_LethalWeapon(&ps);
		Frame();
		buttons = BUTTON_LETHAL; Frame(); buttons = 0;	/* K+2 */
		CHECK(ps.weapon == lethal && ps.weaponstate == WEAPON_RAISING && (ps.weaponAction & WA_LEFT_EMPTY));
		for (t = 0; t < 20000 && ps.weapon != WP_NONE; t += msec) {
			Frame();
			CHECK(ps.weapon == lethal || ps.weapon == WP_NONE);
		}
		CHECK(ps.weapon == WP_NONE && ps.weaponstate == WEAPON_READY && ps.weaponTime == 0 && fired[lethal] == 1);	/* E */
		CHECK((ps.weaponAction & (WA_TYPE_MASK | WA_FIRED | WA_LEFT_EMPTY)) == (WA_LETHAL | WA_FIRED | WA_LEFT_EMPTY));
		Frame();	/* E+1: OFFHAND_END's delay command */
		CHECK(ps.weapon == WP_NONE && ps.weaponstate == WEAPON_READY && !(ps.weaponAction & WA_TYPE_MASK));
		Frame();	/* E+2 */
		if (!stale) {
			CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_RAISING);
			CHECK(ps.weaponTime == BG_WeaponDrawTime(&ps) && ps.weaponTime == BG_WeaponRaiseTime(gun));
			continue;
		}
		for (k = 0; k < Commands(BG_WeaponRaiseTime(gun)) + Commands(BG_WeaponQuickRaiseTime(gun)); k++) {
			CHECK(ps.weapon == WP_NONE && ps.weaponstate == WEAPON_READY && !(ps.weaponAction & WA_TYPE_MASK));
			Frame();
		}
	}
	stale = 0;
}

/* MW2's crossing drops what a weaponTime overshot by in every state its
 * held-trigger lock does not take (PM_Weapon_WeaponTimeAdjust, 0x578B71):
 * whatever handler runs on that command finds 0 and assigns its own whole time.
 * A raise's end (FinishWeaponRaise, 0x576720) assigns none, so the gun is
 * READY with no time over: an ordinary raise, a first raise and the
 * Masterkey's alternate raise (whose drop is 0, so it starts on the press). */
static void RaiseToItsEnd(int weapon, int raise) {
	int k;
	CHECK(ps.weapon == weapon && ps.weaponstate == WEAPON_RAISING && ps.weaponTime == raise);
	for (k = 1; k < Commands(raise); k++) { Frame(); CHECK(ps.weapon == weapon && ps.weaponstate == WEAPON_RAISING); }
	Frame();
	CHECK(ps.weapon == weapon && ps.weaponstate == WEAPON_READY && ps.weaponTime == 0);
	Frame();
	CHECK(ps.weapon == weapon && ps.weaponstate == WEAPON_READY && ps.weaponTime == 0);
}
static void TestRaiseEnds(int gun) {
	int first, t, other = Other(gun);
	for (first = 0; first < 2; first++) {
		Setup(gun, other);
		if (first) ps.slotFlags[SLOT_SECONDARY] |= BG_GUN_FIRST_DRAW;
		slot = SLOT_SECONDARY;
		for (t = 0; t < 20000 && !(ps.weapon == other && ps.weaponstate == WEAPON_RAISING); t += msec) Frame();
		CHECK(ps.weaponDraw == (first ? WDRAW_FIRST : WDRAW_NORMAL));
		RaiseToItsEnd(other, first ? BG_WeaponFirstRaiseTime(other) : BG_WeaponRaiseTime(other));
	}
}
static void TestAltRaiseEnds(void) {
	Setup(WP_M16, WP_SHOTGUN);
	CHECK(BG_WeaponAltDropTime(WP_M16) == 0);
	cmdWeapon = BG_WeaponCommand(&ps, SLOT_PRIMARY, qtrue);
	Frame();
	CHECK(ps.weaponDraw == WDRAW_ALT);
	RaiseToItsEnd(WP_MASTERKEY, BG_WeaponAltRaiseTime(WP_MASTERKEY));
	cmdWeapon = 0;
}

/* A reload's end is the same crossing (0x578B71) before FinishReloadEnd
 * readies the gun (0x5769A0): READY with no time over, also where no note of
 * the end zeroes its time (the Masterkey's end has none), the reload button
 * held through it: a reload asked for on the end's command is refused (MW2 asks
 * a reloading gun nothing), so that command's READY with nothing to fire zeroes
 * the time. */
static void TestReloadEnds(int gun) {
	int t;
	if (gun == WP_MASTERKEY) {
		Setup(WP_M16, WP_SHOTGUN);
		ps.slotFlags[SLOT_PRIMARY] |= BG_GUN_ALTERNATE; BG_SelectSlot(&ps, SLOT_PRIMARY);
		ps.weapon = BG_ActiveSlotWeapon(&ps);
	} else {
		Setup(gun, Other(gun));
	}
	CHECK(ps.weapon == gun && BG_WeaponMagSize(gun) > 1);
	ps.ammo[gun] = BG_WeaponMagSize(gun) - 1;
	buttons = BUTTON_RELOAD;	/* held from the press to the end */
	Frame();
	CHECK(ps.weaponstate == WEAPON_RELOADING && ps.weaponAnimSeq == RSEQ_START && ps.weaponAnimTime == 0);
	for (t = 0; t < 20000 && ps.weaponstate == WEAPON_RELOADING; t += msec) Frame();
	CHECK(ps.weaponstate == WEAPON_READY && ps.weaponTime == 0 && ps.ammo[gun] == BG_WeaponMagSize(gun));
	Frame();
	CHECK(ps.weaponstate == WEAPON_READY && ps.weaponTime == 0);
	buttons = 0;
}

/* Equipment's prepare ends in HOLD with no time over (the crossing,
 * 0x578B71; OffHandHold zeroes the times again, 0x577934-0x57793A), and the
 * throw is given its whole time (the HOLD handler assigns fireTime, 0x578F82):
 * so the throw holds BG_QUICK_ACTION_HOLD whole from its command (Peace's one
 * recovery, MW2_DIFFERENCES "Recovery after the throw"), and the rapid drop,
 * the gun's quick raise and the end of the recovery follow on their whole
 * times. Each item from a gun in hand, tapped, with a use hint: frag, Semtex,
 * the Tactical Insertion and the throwing knife (prepared on its press) on
 * the lethal button, the flash on the tactical. */
static void TestHoldIsWhole(int gun) {
	const int lethals[] = { WP_FRAG, WP_SEMTEX, WP_TACTICAL_INSERTION, WP_THROWING_KNIFE, WP_NONE };
	int i, k, t, equip, button, quick = BG_WeaponQuickRaiseTime(gun);
	for (i = 0; i < ARRAY_LEN(lethals); i++) {
		SetupClass(gun, Other(gun), lethals[i]);
		button = lethals[i] ? BUTTON_LETHAL : BUTTON_TACTICAL;
		equip = lethals[i] ? BG_LethalWeapon(&ps) : WP_FLASH;
		CHECK(equip == (lethals[i] ? lethals[i] : WP_FLASH) && ps.ammo[equip] > 0);
		CHECK(ps.cursorHintEntIndex != ENTITYNUM_NONE);
		buttons = button; Frame(); buttons = 0;
		for (t = 0; t < 20000 && !(ps.weapon == equip && ps.weaponstate == WEAPON_READY); t += msec) Frame();
		/* HOLD: in hand and ready, nothing over */
		CHECK(ps.weapon == equip && ps.weaponstate == WEAPON_READY && ps.weaponTime == 0 && fired[equip] == 0);
		Frame();	/* the button is up: the throw */
		CHECK(fired[equip] == 1 && ps.weapon == equip && ps.weaponstate == WEAPON_FIRING);
		CHECK(ps.weaponTime == BG_QUICK_ACTION_HOLD);
		for (k = 1; k < Commands(BG_QUICK_ACTION_HOLD); k++) {
			Frame();
			CHECK(ps.weapon == equip && ps.weaponstate == WEAPON_FIRING && ps.weaponTime == BG_QUICK_ACTION_HOLD - k * msec);
		}
		Frame();	/* the hold's crossing: the rapid drop, whole */
		CHECK(ps.weapon == equip && ps.weaponstate == WEAPON_DROPPING && ps.weaponTime == BG_RAPID_DROP_TIME);
		CHECK(BG_HolsterTime(&ps) == BG_RAPID_DROP_TIME);
		for (k = 1; k < Commands(BG_RAPID_DROP_TIME); k++) { Frame(); CHECK(ps.weapon == equip && ps.weaponstate == WEAPON_DROPPING); }
		Frame();
		CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_RAISING && ps.weaponTime == quick);
		for (k = 1; k < Commands(quick); k++) { Frame(); CHECK(ps.weapon == gun && (ps.weaponAction & WA_TYPE_MASK)); }
		Frame();	/* the recovery's end: READY, the latch clear, nothing over */
		CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_READY && ps.weaponTime == 0);
		CHECK(!(ps.weaponAction & WA_TYPE_MASK));
	}
}

/* D4. YY while sprinting: MW2's double-tap cancel is in its change check,
 * after its sprint check (0x44C4CA, 0x44C4D7), which refused the drop
 * (0x5780C6). So the YY command is READY: an owed action starts on it
 * (CheckForRechamber from READY, 0x575B77) and holds the sprint off until it
 * ends (the sprint check refuses RECHAMBERING, 0x57807D); with none owed the
 * sprint entry comes on the next command. */
static void TestYYWhileSprinting(int gun) {
	int owed, k;
	for (owed = 0; owed < 2; owed++) {
		if (owed && BG_WeaponFireLength(gun) <= 0) continue;
		Setup(gun, Other(gun)); IntoTheCarry(gun, owed);
		slot = SLOT_SECONDARY; Frame();
		CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_DROPPING && (ps.pm_flags & PMF_SPRINTING));
		slot = SLOT_PRIMARY; Frame();	/* YY on N */
		CHECK(ps.weapon == gun && (ps.pm_flags & PMF_SPRINTING));
		if (!owed) {
			CHECK(ps.weaponstate == WEAPON_READY && ps.weaponTime == 0);
			Frame();
			CHECK(ps.weaponstate == WEAPON_SPRINT_IN && ps.weaponTime == BG_WeaponSprintInTime(gun));
			continue;
		}
		CHECK(ps.weaponstate == WEAPON_BOLTING && ps.weaponAnimTime == Open(gun) && ps.weaponTime == ActionLength(gun));
		for (k = 1; k < Commands(ActionLength(gun)); k++) {
			Frame();
			CHECK(ps.weaponstate == WEAPON_BOLTING && (ps.pm_flags & PMF_SPRINTING));
		}
		Frame();
		CHECK(ps.weaponstate == WEAPON_READY && !Owes(gun));
		Frame();
		CHECK(ps.weaponstate == WEAPON_SPRINT_IN && ps.weaponTime == BG_WeaponSprintInTime(gun) && !Owes(gun));
	}
}

/* D5. A gun with no fire clip, its last round's lock running out: MW2's auto
 * reload refuses FIRING (0x57886D) and its trigger-up ShouldBeFiring only
 * readies the gun (0x576D99-0x576DA3), so the reload is the command after; a
 * held trigger reloads on that command, through FireWeapon's ammo check
 * (0x5770C0-0x5770C9). The machinegun (m4_mp); the Quake 3 guns with a
 * magazine go the same way. A burst gun (the M16 rifle, m16_shotgun_mp's
 * 3-Round Burst) is READY in its cooldown at the top of that command, its burst
 * ended by the empty clip (0x576A8A, 0x578939-0x5789CD), and the auto reload
 * takes that READY gun on that same command (0x578845-0x578889), the trigger up
 * or down; the reload begins a new pull (0x575EFE). */
static void TestLastRoundEnds(int gun) {
	int held, k, lock, type = BG_WeaponFireType(gun);
	qboolean burst = type >= BG_FIRE_BURST2 && type <= BG_FIRE_BURST4;
	for (held = 0; held < 2; held++) {
		Setup(gun, Other(gun)); ps.ammo[gun] = 1;
		CHECK(BG_WeaponFireLength(gun) <= 0 && ps.ammoReserve[gun] > 0);
		buttons = BUTTON_ATTACK; Frame();
		lock = ps.weaponTime;	/* the gun's own fire lock, as pmove stamped it */
		CHECK(fired[gun] == 1 && ps.weaponstate == WEAPON_FIRING && ps.ammo[gun] == 0 && lock > 0);
		if (!held) buttons = 0;
		for (k = 1; k < Commands(lock); k++) { Frame(); CHECK(ps.weaponstate == WEAPON_FIRING); }
		Frame();
		if (held || burst) {
			CHECK(ps.weaponstate == WEAPON_RELOADING && ps.weaponAnimTime == 0);
			CHECK(ps.weaponTime == BG_ReloadSegmentTime(&ps, ps.weaponAnimSeq) && ps.weaponShotCount == 0);
			continue;
		}
		CHECK(ps.weaponstate == WEAPON_READY && ps.ammo[gun] == 0);
		Frame();
		CHECK(ps.weaponstate == WEAPON_RELOADING && ps.weaponAnimSeq == RSEQ_START && ps.weaponAnimTime == 0);
	}
}

/* D6. Equipment pressed on the command the knife hits: the knife's draw is
 * MW2's meleeDelay (MeleeInit 0x577581-0x577599), and on the command it runs
 * out the offhand check (which takes MELEE_INIT) runs OffHandInit, then the
 * hand's switch, the time gate skipped for the delay (0x579474), prepares the
 * equipment on that same command (0x5792C7 -> OffHandPrepare 0x577800): no
 * quick drop, and no hit. A command sooner the knife is cut on the gun's quick
 * drop, as ever. */
static void TestEquipmentOnTheHit(int gun) {
	int early, k, hit, lethal;
	for (early = 0; early < 2; early++) {
		Setup(gun, Other(gun));
		lethal = BG_LethalWeapon(&ps);
		buttons = BUTTON_MELEE; Frame(); buttons = 0;	/* P */
		CHECK(ps.weapon == WP_KNIFE && ps.weaponstate == WEAPON_RAISING && ps.weaponTime == BG_MELEE_DELAY);
		hit = Commands(BG_MELEE_DELAY);	/* the hit's command, H, counted from P */
		CHECK(hit > 1);
		for (k = 1; k < hit - early; k++) Frame();
		buttons = BUTTON_LETHAL; Frame(); buttons = 0;
		CHECK(fired[WP_KNIFE] == 0 && (ps.weaponAction & (WA_TYPE_MASK | WA_FIRED)) == WA_LETHAL);
		if (!early) {
			CHECK(ps.weapon == lethal && ps.weaponstate == WEAPON_RAISING);
			CHECK(ps.weaponTime == BG_WeaponRaiseTime(lethal) && prepared[lethal] == 1);
			continue;
		}
		CHECK(ps.weapon == WP_KNIFE && ps.weaponstate == WEAPON_DROPPING && ps.weaponTime == BG_WeaponQuickDropTime(gun));
		for (k = 1; k < Commands(BG_WeaponQuickDropTime(gun)); k++) { Frame(); CHECK(ps.weapon == WP_KNIFE && !prepared[lethal]); }
		Frame();
		CHECK(ps.weapon == lethal && ps.weaponstate == WEAPON_RAISING && prepared[lethal] == 1 && fired[WP_KNIFE] == 0);
	}
	/* with nothing pressed, that command is the hit */
	Setup(gun, Other(gun));
	buttons = BUTTON_MELEE; Frame(); buttons = 0;
	for (k = 1; k < Commands(BG_MELEE_DELAY); k++) { Frame(); CHECK(fired[WP_KNIFE] == 0); }
	Frame();
	CHECK(fired[WP_KNIFE] == 1 && ps.weapon == WP_KNIFE);
}

/* D7. The knife on a reload's delayed-action command: MW2's knife check refuses
 * any command whose weapon delay runs out (0x5776ED-0x5776F2, passed
 * max(delayedAction), 0x44C4E3-0x44C4FC). In a reload that is its round
 * (the add delay) and an owed action's settle (PM_SetWeaponReloadAddAmmoDelay
 * 0x575C50), and the hand's switch then seats the round (FinishReload,
 * 0x579232) or pays the pump (ReloadDelayedAction, 0x5768C5). The command
 * before it takes the knife, as ever. */
/* The commands from the reload's press to its first round's command. A round in
 * a loop pass is counted from the command the start ended: each segment starts
 * afresh there (MW2's PM_SetReloadingState gives it its whole time, 0x575E20,
 * after the crossing dropped the start's overshoot, 0x578B71). */
static int FirstRound(int gun) {
	const bg_weaponNote_t *n;
	for (n = PM_SegNotes(gun, RSEQ_START); n->note != WNOTE_NONE; n++)
		if (n->note == WNOTE_MAG_IN) return Commands(BG_ReloadTime(&ps, n->time));
	for (n = PM_SegNotes(gun, RSEQ_LOOP); n->note != WNOTE_NONE; n++)
		if (n->note == WNOTE_MAG_IN)
			return Commands(BG_ReloadSegmentTime(&ps, RSEQ_START)) + Commands(BG_ReloadTime(&ps, n->time));
	return -1;
}
/* MW2's settle (PM_ReloadSettleCrossed's rule, from the tables): the gun's
 * own action length, open to close, capped by the start's first round or
 * its end. */
static int Settle(int gun) {
	const bg_weaponNote_t *n;
	int open = 0, close = -1, add = -1, cap = BG_WeaponReloadSegLength(gun, RSEQ_START), t;
	for (n = bg_weaponFire[gun].notes; n->note != WNOTE_NONE; n++) {
		if (n->note == WNOTE_BOLT_OPEN) open = n->time;
		if (n->note == WNOTE_BOLT_CLOSED) close = n->time;
	}
	for (n = PM_SegNotes(gun, RSEQ_START); n->note != WNOTE_NONE; n++)
		if (n->note == WNOTE_MAG_IN && add < 0) add = n->time;
	if (add >= 0 && add < cap) cap = add;
	t = close - open;
	return BG_ReloadTime(&ps, t < cap ? t : cap);
}
static void TestKnifeOnADelay(int gun) {
	int kind, early, k, when, ammo, reserve;
	for (kind = 0; kind < 2; kind++) for (early = 0; early < 2; early++) {
		Setup(gun, Other(gun));
		if (kind) {
			/* reload out of the shot: the settle pays the owed pump or bolt */
			buttons = BUTTON_ATTACK; Frame();
			CHECK(fired[gun] == 1 && Owes(gun));
			when = Commands(Settle(gun));
		} else {
			ps.ammo[gun] = BG_WeaponMagSize(gun) - 2;	/* the first round's command */
			when = FirstRound(gun);
		}
		CHECK(when > 1);
		buttons = BUTTON_RELOAD; Frame(); buttons = 0;	/* R */
		CHECK(ps.weaponstate == WEAPON_RELOADING && ps.weaponAnimTime == 0 && Owes(gun) == kind);
		for (k = 1; k < when - early; k++) Frame();
		CHECK(ps.weaponstate == WEAPON_RELOADING && Owes(gun) == kind);
		ammo = ps.ammo[gun]; reserve = ps.ammoReserve[gun];
		buttons = BUTTON_MELEE; Frame(); buttons = 0;
		if (early) {
			CHECK(ps.weapon == WP_KNIFE && ps.ammo[gun] == ammo && Owes(gun) == kind);
			continue;
		}
		CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_RELOADING && !(ps.weaponAction & WA_TYPE_MASK));
		if (kind) CHECK(!Owes(gun) && ps.ammo[gun] == ammo);
		else CHECK(ps.ammo[gun] == ammo + 1 && ps.ammoReserve[gun] == reserve - 1);
		Frame();
		CHECK(ps.weapon == gun && fired[WP_KNIFE] == 0);	/* and the press is gone */
	}
}

/* The reload end's close note (Peace's SPAS-12 and M300 data, which MW2's
 * reload end does not have) is no weapon delay: MW2's RELOAD_END has none
 * (FinishReload stamps reloadEndTime alone, 0x5785EB-0x578606;
 * PM_SetWeaponReloadAddAmmoDelay gives one only to RELOADING and
 * RELOAD_START, 0x575C50), and its knife check takes a reload state on any
 * command no delay runs out on (0x577734-0x577755, 0x5776ED). So the knife
 * comes out on that note's command, as on the command before it. */
static int EndNote(int gun) {
	const bg_weaponNote_t *n;
	for (n = PM_SegNotes(gun, RSEQ_END); n->note != WNOTE_NONE; n++)
		if (n->note == WNOTE_BOLT_CLOSED) return BG_ReloadTime(&ps, n->time);
	return -1;
}
static void TestKnifeOnTheEndsNote(int gun) {
	int early, t, close;
	for (early = 0; early < 2; early++) {
		Setup(gun, Other(gun));
		ps.ammo[gun] = BG_WeaponMagSize(gun) - 1;
		close = EndNote(gun);
		CHECK(close > 2 * msec);
		buttons = BUTTON_RELOAD; Frame(); buttons = 0;
		for (t = 0; t < 20000 && ps.weaponAnimSeq != RSEQ_END; t += msec) Frame();
		CHECK(ps.weaponstate == WEAPON_RELOADING && ps.weaponAnimSeq == RSEQ_END && ps.weaponAnimTime < msec);
		CHECK(ps.ammo[gun] == BG_WeaponMagSize(gun));
		/* to the command before the one that crosses the note (early: two before) */
		while (ps.weaponAnimTime + (1 + early) * msec < close) Frame();
		CHECK(ps.weaponstate == WEAPON_RELOADING && ps.weaponAnimSeq == RSEQ_END &&
			ps.weaponAnimTime + early * msec < close);
		buttons = BUTTON_MELEE; Frame(); buttons = 0;
		CHECK(ps.weapon == WP_KNIFE && ps.weaponstate == WEAPON_RAISING && ps.weaponTime == BG_MELEE_DELAY);
		CHECK(ps.ammo[gun] == BG_WeaponMagSize(gun) && (ps.weaponAction & WA_TYPE_MASK) == WA_MELEE);
	}
}

/* The held trigger at these ends (MW2's PM_Weapon_WeaponTimeAdjust,
 * 0x5788B0, run before every check, 0x44C42B). On the command a state's
 * weaponTime runs out with the trigger down, the pull's shots all fired
 * (ShotLimitReached 0x5769E0), the command naming the gun and a round in it,
 * the time becomes 1 (0x578AE6): FIRING and the melee states go READY
 * (0x578B16-0x578B39), the offhand states are never locked (0x578A94), any
 * other state keeps its state (0x578B25-0x578B28), and the 1 runs out on the
 * next command, which asks again. A command that asks with the trigger up zeroes
 * the count and the time (0x578B51-0x578B71). A single-shot gun's pull is
 * spent by its shot; a frag thrown out of the lock with the trigger held all
 * along keeps it spent (its return is an offhand state, so it stays unlocked
 * and the time is zeroed with the count kept, 0x578B51-0x578B71), and a
 * trigger let go on the command after the return asks nothing (no time runs
 * out), so the pull is still spent with the trigger up. */
static void ShootHeldToTheLock(int gun) {
	int t;
	buttons = BUTTON_ATTACK; Frame();
	CHECK(fired[gun] == 1 && ps.weaponShotCount == 1);
	for (t = 0; t < 20000 && !(ps.weaponstate == WEAPON_READY && !Owes(gun)); t += msec) Frame();
	CHECK(ps.weaponstate == WEAPON_READY && ps.weaponTime == 1 && !Owes(gun) && fired[gun] == 1);
}
static void SpentWithTheTriggerUp(int gun) {
	int t, lethal = BG_LethalWeapon(&ps);
	ShootHeldToTheLock(gun);
	buttons = BUTTON_ATTACK | BUTTON_LETHAL; Frame();
	CHECK((ps.weaponAction & WA_TYPE_MASK) == WA_LETHAL);
	buttons = BUTTON_ATTACK;
	for (t = 0; t < 20000 && !(ps.weapon == gun && ps.weaponstate == WEAPON_READY); t += msec) Frame();
	/* the return's last command: READY, nothing fired, the pull still spent */
	CHECK(fired[lethal] == 1 && fired[gun] == 1 && ps.weaponShotCount == 1 && !(ps.weaponAction & WA_TYPE_MASK));
	buttons = 0; Frame();
	CHECK(ps.weaponstate == WEAPON_READY && ps.weaponShotCount == 1 && fired[gun] == 1);
}
static void TestHeldTriggerAtTheEnds(int gun) {
	int k, t, owed, other = Other(gun), lethal, raise;
	CHECK(BG_ShotLimitReached(BG_WeaponFireType(gun), 1) && BG_WeaponFireLength(gun) > 0);

	/* D1 under the lock: the trigger pressed in the sprint entry ends the
	   sprint, and with the pull spent the sprint-out's last command locks it
	   (SPRINT_DROP keeps its state with weaponTime 1) for as long as the
	   trigger stays down; let go, it ends as ever, READY and nothing fired
	   (PM_Weapon_Idle), and a fresh pull fires on the next command */
	Setup(gun, other); SpentWithTheTriggerUp(gun);
	forward = 127; buttons = BUTTON_SPRINT; Frame(); buttons = 0;
	CHECK((ps.pm_flags & PMF_SPRINTING) && ps.weaponstate == WEAPON_SPRINT_IN);
	CHECK(Commands(BG_WeaponSprintInTime(gun)) > 2);
	Frame();
	buttons = BUTTON_ATTACK; Frame();
	CHECK(!(ps.pm_flags & PMF_SPRINTING));
	ThroughTheSprintOut(gun);
	Frame();
	CHECK(ps.weaponstate == WEAPON_SPRINT_OUT && ps.weaponTime == 1 && fired[gun] == 1);
	for (k = 0; k < Commands(BG_WeaponFireLength(gun)); k++) {
		Frame();
		CHECK(ps.weaponstate == WEAPON_SPRINT_OUT && ps.weaponTime == 1 && fired[gun] == 1);
	}
	buttons = 0; Frame();
	CHECK(ps.weaponstate == WEAPON_READY && ps.weaponShotCount == 0 && fired[gun] == 1);
	buttons = BUTTON_ATTACK; Frame();
	CHECK(ps.weaponstate == WEAPON_FIRING && fired[gun] == 2);
	/* the pull not spent (the shot's own last command let go): the same press
	   ends the sprint-out on its time, and the trigger fires the command after */
	Setup(gun, other); IntoTheCarry(gun, 0);
	CHECK(ps.weaponShotCount == 0);
	buttons = BUTTON_ATTACK; Frame();
	ThroughTheSprintOut(gun);
	Frame();
	CHECK(ps.weaponstate == WEAPON_READY && fired[gun] == 0);

	/* D4 with the trigger: pressed on the YY command, it ends the sprint first
	   (PM_UpdateSprint), the cancel readies the gun, and the hand fires on
	   that command (ShouldBeFiring), or, owing its action, starts the action
	   from READY and fires nothing (CheckForRechamber first, 0x579464); that
	   action's end is no lock (the shot's own last command let go ended its
	   pull) and fires as a rechamber's end does */
	for (owed = 0; owed < 2; owed++) {
		Setup(gun, other); IntoTheCarry(gun, owed);
		slot = SLOT_SECONDARY; Frame();
		CHECK(ps.weaponstate == WEAPON_DROPPING && (ps.pm_flags & PMF_SPRINTING));
		slot = SLOT_PRIMARY; buttons = BUTTON_ATTACK; Frame();	/* YY on N */
		CHECK(!(ps.pm_flags & PMF_SPRINTING) && ps.weapon == gun);
		if (!owed) {
			CHECK(ps.weaponstate == WEAPON_FIRING && fired[gun] == 1);
			continue;
		}
		CHECK(ps.weaponstate == WEAPON_BOLTING && ps.weaponAnimTime == Open(gun) && fired[gun] == 1);
		for (k = 1; k < Commands(ActionLength(gun)); k++) Frame();
		CHECK(ps.weaponstate == WEAPON_BOLTING && fired[gun] == 1);
		Frame();
		CHECK(ps.weaponstate == WEAPON_FIRING && Owes(gun) && fired[gun] == 2);
	}

	/* D6 with the trigger: the knife out of the lock, the trigger held, and
	   a frag on the hit's command, which is prepared there; the gun's return
	   after the throw ends READY and unlocked (an offhand state), nothing
	   fired, and the held trigger fires on the next command (no time ran out,
	   so nothing asks: ShouldBeFiring is a level test, 0x576CC0) */
	Setup(gun, other); lethal = BG_LethalWeapon(&ps);
	ShootHeldToTheLock(gun);
	buttons = BUTTON_ATTACK | BUTTON_MELEE; Frame();	/* P */
	CHECK(ps.weapon == WP_KNIFE && ps.weaponstate == WEAPON_RAISING && ps.weaponTime == BG_MELEE_DELAY);
	buttons = BUTTON_ATTACK;
	for (k = 1; k < Commands(BG_MELEE_DELAY); k++) Frame();
	buttons = BUTTON_ATTACK | BUTTON_LETHAL; Frame();	/* H */
	CHECK(ps.weapon == lethal && ps.weaponstate == WEAPON_RAISING && prepared[lethal] == 1 && fired[WP_KNIFE] == 0);
	buttons = BUTTON_ATTACK;
	for (t = 0; t < 20000 && !(ps.weapon == gun && ps.weaponstate == WEAPON_READY); t += msec) Frame();
	CHECK(fired[lethal] == 1 && fired[gun] == 1 && ps.weaponShotCount == 1);
	Frame();
	CHECK(ps.weaponstate == WEAPON_FIRING && fired[gun] == 2);

	/* the reload's delayed action, the trigger held from the shot: a Y on
	   the settle's command is the instant swap (MW2's path A), and the other
	   gun's raise ends READY, unlocked, since the reload began a new pull
	   (PM_BeginWeaponReload zeroes the count, 0x575EFE): the held trigger
	   fires on the next command. (The same raise with the pull spent waits
	   under the lock: trigger_test.c.) */
	Setup(gun, other);
	buttons = BUTTON_ATTACK; Frame();
	buttons = BUTTON_ATTACK | BUTTON_RELOAD; Frame();	/* R */
	CHECK(ps.weaponstate == WEAPON_RELOADING && Owes(gun) && ps.weaponShotCount == 0);
	buttons = BUTTON_ATTACK;
	for (k = 1; k < Commands(Settle(gun)); k++) Frame();
	CHECK(ps.weaponstate == WEAPON_RELOADING && Owes(gun));
	slot = SLOT_SECONDARY; Frame();
	CHECK(ps.weapon == other && ps.weaponstate == WEAPON_RAISING && Owes(gun));
	raise = ps.weaponTime;
	CHECK(raise == BG_WeaponDrawTime(&ps) && raise > msec);
	for (k = 1; k < Commands(raise); k++) Frame();
	CHECK(ps.weaponstate == WEAPON_RAISING && fired[other] == 0);
	Frame();
	CHECK(ps.weaponstate == WEAPON_READY && fired[other] == 0);
	Frame();
	CHECK(ps.weaponstate == WEAPON_FIRING && fired[other] == 1);
}

int main(void) {
	const int steps[] = { 8, 16, 33 };
	const int guns[] = { WP_SHOTGUN, WP_M40A3, WP_MACHINEGUN };
	int i, g;
	for (i = 0; i < ARRAY_LEN(steps); i++) {
		msec = steps[i];
		for (g = 0; g < ARRAY_LEN(guns); g++) {
			TestSprintOutEnds(guns[g]);
			TestSprintAgainInTheSprintOut(guns[g]);
			TestReturnEnds(guns[g]);
			TestStraightOutOfTheReturn(guns[g]);
			TestKnifeAfterTheReturn(guns[g]);
			TestKnifeBackToAnEmptyHand(guns[g]);
			TestRaiseEnds(guns[g]);
			TestReloadEnds(guns[g]);
			TestHoldIsWhole(guns[g]);
			TestEquipmentOverTheKnifeFromAnEmptyHand(guns[g]);
			TestYYWhileSprinting(guns[g]);
			TestEquipmentOnTheHit(guns[g]);
		}
		TestAltRaiseEnds();
		TestReloadEnds(WP_MASTERKEY);
		TestLastRoundEnds(WP_MACHINEGUN);
		TestLastRoundEnds(WP_M16);
		TestKnifeOnADelay(WP_SHOTGUN);
		TestKnifeOnADelay(WP_M40A3);
		TestKnifeOnTheEndsNote(WP_SHOTGUN);
		TestKnifeOnTheEndsNote(WP_M40A3);
		TestHeldTriggerAtTheEnds(WP_SHOTGUN);
		TestHeldTriggerAtTheEnds(WP_M40A3);
	}
	printf("PASS: %d checks of what a weapon state's last command does, at 8, 16 and 33ms commands\n", checks);
	return 0;
}
