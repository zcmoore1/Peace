/* What MW2 does on the think a weapon state ends, through the real Pmove at
 * 8, 16 and 33ms commands.
 *
 * MW2's ProcessHand (iw4x 0x579450) handles a state whose time has run out
 * in its state switch (the jump table at 0x5793D8) and returns before
 * ShouldBeFiring (0x5794B4-0x5794B6): on that think the gun neither fires nor
 * starts anything the switch did not, and every check of the think saw the
 * old state. Its checks run first, in this order (PM_Weapon 0x44C380):
 * sprint 0x578060, offhand 0x579030, change 0x578BC0 (the double-tap cancel
 * inside it, 0x578D00), reload 0x578670, knife 0x577670 (passed the think's
 * delayedAction, 0x44C4E3-0x44C4FC), then the hand. A think whose weapon
 * delay runs out skips the hand's time gate (0x579474), so the switch runs on
 * whatever state the checks left (path A).
 *
 * Only buttons are pressed. Every time compared against is read from the
 * game's tables or from the lock pmove stamped, never written here. */
#include "../../code/game/bg_pmove.c"

static playerState_t ps;
static pmove_t move;
static int checks, msec, buttons, forward, slot, stale;
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

/* A spawned player on the floor, the primary in hand and ready. */
static void Setup(int primary, int secondary) {
	bg_class_t cl = *BG_Class(0);
	memset(&ps, 0, sizeof(ps)); memset(&move, 0, sizeof(move));
	memset(fired, 0, sizeof(fired)); memset(prepared, 0, sizeof(prepared));
	cl.slot[0] = primary; cl.slot[1] = secondary; cl.attachments[0] = cl.attachments[1] = 0;
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
	buttons = forward = stale = 0; slot = SLOT_PRIMARY;
}
/* One command, msec long, through the whole Pmove. With 'stale' the slot's
 * command carries the other generation, as one sent before the client saw a
 * pickup's take: it names no gun (BG_SlotCommandCurrent), so an empty hand
 * stays empty. */
static void Frame(void) {
	int seq = ps.eventSequence;
	memset(&move.cmd, 0, sizeof(move.cmd));
	move.cmd.buttons = buttons; move.cmd.forwardmove = forward;
	move.cmd.weapon = BG_SlotCommand(&ps, slot);
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
 * out on command c + Thinks(ms). */
static int Thinks(int ms) { return (ms + msec - 1) / msec; }
static int Other(int gun) { return gun == WP_MACHINEGUN ? WP_SHOTGUN : WP_MACHINEGUN; }
static int Owes(int gun) { return (ps.stats[STAT_UNCHAMBERED] & (1 << gun)) != 0; }
static int Open(int gun) { int open = PM_ShotLength(gun); return open < 0 ? 0 : open; }
static int ActionLength(int gun) { return BG_WeaponFireLength(gun) - Open(gun); }

/* Into the sprint carry. With 'shoot', the gun's shot comes first and a
 * sprint tapped during it takes the gun once the shot is over (on a pump or
 * bolt gun the READY think after it: MW2's sprint check runs before its
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
	CHECK(ps.weaponstate == WEAPON_SPRINTING && Owes(gun) == owed);
}
/* Run the sprint-out stamped on the last command to the think before its
 * last, checking it holds every think. */
static void ThroughTheSprintOut(int gun) {
	int k;
	CHECK(ps.weaponstate == WEAPON_SPRINT_OUT && ps.weaponTime == BG_WeaponSprintOutTime(gun));
	for (k = 1; k < Thinks(BG_WeaponSprintOutTime(gun)); k++) {
		Frame();
		CHECK(ps.weaponstate == WEAPON_SPRINT_OUT && ps.weaponTime == BG_WeaponSprintOutTime(gun) - k * msec);
	}
}

/* D1. SPRINT_DROP's last think: MW2's switch makes the gun READY in
 * PM_Weapon_Idle (0x5793D8 entry 25 -> 0x579279 -> 0x5777A0) and the think
 * is over. The rechamber check ran first and starts nothing out of
 * SPRINT_DROP (0x575B77), the auto reload refuses the sprint states
 * (0x578872-0x57887A), and nothing fires: an owed pump or bolt, the shot and
 * an empty magazine's reload all come on the next think. A reload asked for
 * on it is taken (the reload check runs first and takes the sprint states,
 * 0x575E8E-0x575EA8). */
static void TestSprintOutEnds(int gun) {
	/* the action owed: the sprint ends when forward is let go */
	if (BG_WeaponFireLength(gun) > 0) {
		Setup(gun, Other(gun)); IntoTheCarry(gun, 1);
		forward = 0; Frame();
		ThroughTheSprintOut(gun);
		Frame();
		CHECK(ps.weaponstate == WEAPON_READY && ps.weaponAnimTime == -1 && Owes(gun));
		Frame();
		CHECK(ps.weaponstate == WEAPON_BOLTING && ps.weaponAnimTime == Open(gun));
		CHECK(ps.weaponTime == ActionLength(gun) && fired[gun] == 1);
	}
	/* the trigger ends the sprint and is held: the shot is the think after */
	Setup(gun, Other(gun)); IntoTheCarry(gun, 0);
	buttons = BUTTON_ATTACK; Frame();
	CHECK(!(ps.pm_flags & PMF_SPRINTING));
	ThroughTheSprintOut(gun);
	CHECK(fired[gun] == 0);
	Frame();
	CHECK(ps.weaponstate == WEAPON_READY && fired[gun] == 0);
	Frame();
	CHECK(ps.weaponstate == WEAPON_FIRING && fired[gun] == 1);
	/* the last round went in the sprint: the empty gun reloads the think after */
	Setup(gun, Other(gun)); ps.ammo[gun] = 1;
	IntoTheCarry(gun, 1);
	CHECK(ps.ammo[gun] == 0 && ps.ammoReserve[gun] > 0);
	forward = 0; Frame();
	ThroughTheSprintOut(gun);
	Frame();
	CHECK(ps.weaponstate == WEAPON_READY && ps.ammo[gun] == 0);
	Frame();
	CHECK(ps.weaponstate == WEAPON_RELOADING && ps.weaponAnimSeq == RSEQ_START && ps.weaponAnimTime == 0);
	/* a reload pressed on the last think is taken on it, in MW2 as here */
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
 * READY on the next think (0x5780FE -> 0x577EE0). */
static void TestSprintAgainInTheSprintOut(int gun) {
	int k, half;
	Setup(gun, Other(gun)); IntoTheCarry(gun, 0);
	forward = 0; Frame();
	CHECK(ps.weaponstate == WEAPON_SPRINT_OUT && !(ps.pm_flags & PMF_SPRINTING));
	half = Thinks(BG_WeaponSprintOutTime(gun)) / 2;
	CHECK(half >= 1);
	for (k = 1; k < half; k++) Frame();
	forward = 127; buttons = BUTTON_SPRINT; Frame(); buttons = 0;	/* a fresh press */
	CHECK((ps.pm_flags & PMF_SPRINTING) && ps.weaponstate == WEAPON_SPRINT_OUT);
	for (k = half + 1; k < Thinks(BG_WeaponSprintOutTime(gun)); k++) {
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
 * one-think weaponDelay, 0x577A1A-0x577A2A): MW2's change check refuses
 * those states whatever their time (0x578BC9-0x578BF2), so a swap queued
 * behind the return begins from READY on the think after it ends, and
 * equipment pressed on that think still comes out (its offhand check takes
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
	int p, k, t, lethal;
	for (p = 0; p < ARRAY_LEN(presses); p++) {
		/* a Y in the quick raise: the drop starts the think after it ends */
		Setup(gun, Other(gun)); ToTheQuickRaise(gun, presses[p]);
		slot = SLOT_SECONDARY;
		for (k = 1; k < Thinks(BG_WeaponQuickRaiseTime(gun)); k++) {
			Frame();
			CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_RAISING);
		}
		Frame();
		CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_READY && !(ps.weaponAction & WA_TYPE_MASK));
		Frame();
		CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_DROPPING && ps.weaponTime == BG_WeaponDropTime(gun));
		/* equipment pressed on that last think comes out on it, on the quick drop */
		Setup(gun, Other(gun)); ToTheQuickRaise(gun, presses[p]);
		CHECK(ps.ammo[WP_FLASH] > 0);
		for (k = 1; k < Thinks(BG_WeaponQuickRaiseTime(gun)); k++) Frame();
		buttons = BUTTON_TACTICAL; Frame(); buttons = 0;
		CHECK((ps.weaponAction & (WA_TYPE_MASK | WA_FIRED)) == WA_TACTICAL);
		CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_DROPPING && ps.weaponTime == BG_WeaponQuickDropTime(gun));
	}
	/* equipment out of an empty hand comes back to one; a gun selected all
	 * along is drawn the think after the hand's return ends */
	Setup(gun, Other(gun));
	ps.weapon = WP_NONE; ps.cursorHintEntIndex = ENTITYNUM_NONE;
	lethal = BG_LethalWeapon(&ps);
	buttons = BUTTON_LETHAL; Frame(); buttons = 0;
	CHECK(ps.weapon == lethal && (ps.weaponAction & WA_LEFT_EMPTY));
	for (t = 0; t < 20000 && ps.weapon != WP_NONE; t += msec) Frame();
	CHECK(ps.weapon == WP_NONE && ps.weaponstate == WEAPON_READY && fired[lethal] == 1);
	CHECK((ps.weaponAction & (WA_FIRED | WA_LEFT_EMPTY)) == (WA_FIRED | WA_LEFT_EMPTY));
	Frame();
	CHECK(ps.weapon == WP_NONE && ps.weaponstate == WEAPON_READY && !(ps.weaponAction & WA_TYPE_MASK));
	Frame();
	CHECK(ps.weapon == gun && ps.weaponstate == WEAPON_RAISING && ps.weaponTime == BG_WeaponDrawTime(&ps));
	/* that think is the end of OFFHAND_END's one-think delay: equipment
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
 * weapon 0). On the think the hand's return ends, the run-out of
 * OFFHAND_END's one-think delay, the hand's switch then runs on
 * OFFHAND_PREPARE whatever its time (0x579474), and that state's case is
 * OffHandHold (the entry for state 17, 0x5792DA -> 0x577920): the equipment
 * is in hand and ready on that think, its fuse written (0x577958-0x577961),
 * its prepare never played. A think later no delay runs out, and the prepare
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
		Frame();	/* the button is up: HOLD's first think throws it */
		CHECK(fired[WP_FLASH] == 1);
	}
}

/* D4. YY while sprinting: MW2's double-tap cancel is in its change check,
 * after its sprint check (0x44C4CA, 0x44C4D7), which refused the drop
 * (0x5780C6). So the YY think is READY: an owed action starts on it
 * (CheckForRechamber from READY, 0x575B77) and holds the sprint off until it
 * ends (the sprint check refuses RECHAMBERING, 0x57807D); with none owed the
 * sprint entry comes on the next think. */
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
		for (k = 1; k < Thinks(ActionLength(gun)); k++) {
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
 * readies the gun (0x576D99-0x576DA3), so the reload is the think after; a
 * held trigger reloads on that think, through FireWeapon's ammo check
 * (0x5770C0-0x5770C9). */
static void TestLastRoundEnds(int gun) {
	int held, k, lock;
	for (held = 0; held < 2; held++) {
		Setup(gun, Other(gun)); ps.ammo[gun] = 1;
		CHECK(BG_WeaponFireLength(gun) <= 0 && ps.ammoReserve[gun] > 0);
		buttons = BUTTON_ATTACK; Frame();
		lock = ps.weaponTime;	/* the gun's own fire lock, as pmove stamped it */
		CHECK(fired[gun] == 1 && ps.weaponstate == WEAPON_FIRING && ps.ammo[gun] == 0 && lock > 0);
		if (!held) buttons = 0;
		for (k = 1; k < Thinks(lock); k++) { Frame(); CHECK(ps.weaponstate == WEAPON_FIRING); }
		Frame();
		if (held) {
			CHECK(ps.weaponstate == WEAPON_RELOADING && ps.weaponAnimTime == 0);
			continue;
		}
		CHECK(ps.weaponstate == WEAPON_READY && ps.ammo[gun] == 0);
		Frame();
		CHECK(ps.weaponstate == WEAPON_RELOADING && ps.weaponAnimSeq == RSEQ_START && ps.weaponAnimTime == 0);
	}
}

/* D6. Equipment pressed on the think the knife hits: the knife's draw is
 * MW2's meleeDelay (MeleeInit 0x577581-0x577599), and on the think it runs
 * out the offhand check (which takes MELEE_INIT) runs OffHandInit, then the
 * hand's switch, the time gate skipped for the delay (0x579474), prepares the
 * equipment on that same think (0x5792C7 -> OffHandPrepare 0x577800): no
 * quick drop, and no hit. A think sooner the knife is cut on the gun's quick
 * drop, as ever. */
static void TestEquipmentOnTheHit(int gun) {
	int early, k, hit, lethal;
	for (early = 0; early < 2; early++) {
		Setup(gun, Other(gun));
		lethal = BG_LethalWeapon(&ps);
		buttons = BUTTON_MELEE; Frame(); buttons = 0;	/* P */
		CHECK(ps.weapon == WP_KNIFE && ps.weaponstate == WEAPON_RAISING && ps.weaponTime == BG_MELEE_DELAY);
		hit = Thinks(BG_MELEE_DELAY);	/* the hit's think, H, counted from P */
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
		for (k = 1; k < Thinks(BG_WeaponQuickDropTime(gun)); k++) { Frame(); CHECK(ps.weapon == WP_KNIFE && !prepared[lethal]); }
		Frame();
		CHECK(ps.weapon == lethal && ps.weaponstate == WEAPON_RAISING && prepared[lethal] == 1 && fired[WP_KNIFE] == 0);
	}
	/* with nothing pressed, that think is the hit */
	Setup(gun, Other(gun));
	buttons = BUTTON_MELEE; Frame(); buttons = 0;
	for (k = 1; k < Thinks(BG_MELEE_DELAY); k++) { Frame(); CHECK(fired[WP_KNIFE] == 0); }
	Frame();
	CHECK(fired[WP_KNIFE] == 1 && ps.weapon == WP_KNIFE);
}

/* D7. The knife on a reload's delayed-action think: MW2's knife check refuses
 * any think whose weapon delay runs out (0x5776ED-0x5776F2, passed
 * max(delayedAction), 0x44C4E3-0x44C4FC). In a reload that is its round
 * (the add delay) and an owed action's settle (PM_SetWeaponReloadAddAmmoDelay
 * 0x575C50), and the hand's switch then seats the round (FinishReload,
 * 0x579232) or pays the pump (ReloadDelayedAction, 0x5768C5). The think
 * before it takes the knife, as ever. */
static int FirstRound(int gun) {
	const bg_weaponNote_t *n;
	for (n = PM_SegNotes(gun, RSEQ_START); n->note != WNOTE_NONE; n++)
		if (n->note == WNOTE_MAG_IN) return BG_ReloadTime(&ps, n->time);
	for (n = PM_SegNotes(gun, RSEQ_LOOP); n->note != WNOTE_NONE; n++)
		if (n->note == WNOTE_MAG_IN) return BG_ReloadTime(&ps, BG_WeaponReloadSegLength(gun, RSEQ_START) + n->time);
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
			when = Settle(gun);
		} else {
			ps.ammo[gun] = BG_WeaponMagSize(gun) - 2;	/* the first round's think */
			when = FirstRound(gun);
		}
		CHECK(when > 0 && Thinks(when) > 1);
		buttons = BUTTON_RELOAD; Frame(); buttons = 0;	/* R */
		CHECK(ps.weaponstate == WEAPON_RELOADING && ps.weaponAnimTime == 0 && Owes(gun) == kind);
		for (k = 1; k < Thinks(when) - early; k++) Frame();
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
 * think no delay runs out on (0x577734-0x577755, 0x5776ED). So the knife
 * comes out on that note's think, as on the think before it. */
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
		/* to the think before the one that crosses the note (early: two before) */
		while (ps.weaponAnimTime + (1 + early) * msec < close) Frame();
		CHECK(ps.weaponstate == WEAPON_RELOADING && ps.weaponAnimSeq == RSEQ_END &&
			ps.weaponAnimTime + early * msec < close);
		buttons = BUTTON_MELEE; Frame(); buttons = 0;
		CHECK(ps.weapon == WP_KNIFE && ps.weaponstate == WEAPON_RAISING && ps.weaponTime == BG_MELEE_DELAY);
		CHECK(ps.ammo[gun] == BG_WeaponMagSize(gun) && (ps.weaponAction & WA_TYPE_MASK) == WA_MELEE);
	}
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
			TestYYWhileSprinting(guns[g]);
			TestEquipmentOnTheHit(guns[g]);
		}
		TestLastRoundEnds(WP_MACHINEGUN);
		TestKnifeOnADelay(WP_SHOTGUN);
		TestKnifeOnADelay(WP_M40A3);
		TestKnifeOnTheEndsNote(WP_SHOTGUN);
		TestKnifeOnTheEndsNote(WP_M40A3);
	}
	printf("PASS: %d checks of what a weapon state's last think does, at 8, 16 and 33ms commands\n", checks);
	return 0;
}
