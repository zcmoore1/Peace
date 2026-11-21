/* MW2's trigger, held against pressed, through the real code: the real full
 * Pmove, the real CG_EntityEvent for the player's own out-of-ammo events and
 * the real selection handlers (cg_weapons.c), in empty_switch_test.c's frame
 * order (a frame's bound commands, its command, the server's think, then the
 * draw that plays the new events and sets the next command's weapon).
 *
 * MW2's fire is a level test of the trigger (ShouldBeFiring, iw4x 0x576CC0).
 * What makes a gun fire once a pull is the lock PM_Weapon_WeaponTimeAdjust
 * (0x5788B0, 0x5789DD-0x578B71) puts on a state whose time runs out with the
 * trigger down and the pull's shots all fired (ShotLimitReached 0x5769E0, the
 * shots counted by StartFiring 0x576F93-0x576FC5): weaponTime 1, the gun READY
 * at a shot's, an action's or the knife's end, any other state kept, asked
 * again every think until the trigger is let go. The SPAS, the M300 (as the
 * Intervention) and the Masterkey are single shot in MW2's files; the
 * machinegun (m4_mp) is full auto and the M16 rifle Peace's placeholder
 * automatic, each shot given its whole time (0x576E9C, the overshoot dropped
 * at 0x578B71). A segmented reload stops at the end of the segment it is in,
 * asked by a fresh press (in the start only past 0.4 of it, 0x578670) or a
 * trigger down as a segment ends, and each segment starts afresh (0x575E20).
 *
 * The test presses buttons and sets ammunition. Every time it compares
 * against is read from the game's tables. */
#define MASTERKEY_STUBS_REAL_EVENTS
#include "../../code/cgame/cg_weapons.c"
#include "masterkey_selection_stubs.h"
#include "../../code/game/bg_m16_data.h"

cg_t cg;
cgs_t cgs;
vmCvar_t cg_debugEvents, cg_footsteps, cg_nopredict, cg_synchronousClients;
#ifdef MISSIONPACK
vmCvar_t cg_cameraOrbit, cg_singlePlayerActive;
#endif
static snapshot_t snapshot;
static int checks, swapQueued, msec, cmdValue;
static char weaponArgument[16];
static pmove_t move;
static bg_activate_t use;
/* this frame's events, by type, and every frame's shots and out-of-ammo events so far */
static int raised[EV_STANCE_INVALID + 1], shots, noAmmoEvents;

#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d (%d ms commands): %s\n",__LINE__,msec,#x); exit(1); } } while (0)
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int code, const char *fmt, ...) { (void)code; (void)fmt; abort(); }
void trap_SnapVector(float *v) { int i; for (i=0;i<3;i++) v[i]=rintf(v[i]); }
void trap_SendConsoleCommand(const char *text) {
	if (strcmp(text, "_peaceWeaponSwap\n")) abort();
	swapQueued = 1;
}
const char *CG_Argv(int arg) { (void)arg; return weaponArgument; }
int PickupNoteTime(int weapon, int seq, int kind);
qboolean CG_Killcam_Active(void) { return cg.snap && (cg.snap->snapFlags & SNAPFLAG_REPLAY); }

/* What the rest of cg_event.c names, for events this test never plays. */
#define NOT_HERE(name) { fprintf(stderr, #name " is not part of this test\n"); exit(1); }
void CG_Beam(centity_t *cent) NOT_HERE(CG_Beam)
void CG_CenterPrint(const char *str, int y, int charWidth) NOT_HERE(CG_CenterPrint)
const char *CG_ConfigString(int index) { (void)index; NOT_HERE(CG_ConfigString) }
sfxHandle_t CG_CustomSound(int clientNum, const char *soundName) { (void)clientNum; (void)soundName; NOT_HERE(CG_CustomSound) }
void CG_GibPlayer(vec3_t playerOrigin) NOT_HERE(CG_GibPlayer)
void CG_ScorePlum(int client, vec3_t org, int score) NOT_HERE(CG_ScorePlum)
void CG_SpawnEffect(vec3_t org) NOT_HERE(CG_SpawnEffect)
void CG_SetEntitySoundPosition(centity_t *cent) NOT_HERE(CG_SetEntitySoundPosition)
void CG_KickFire(void) NOT_HERE(CG_KickFire)
void trap_S_StopLoopingSound(int entityNum) NOT_HERE(trap_S_StopLoopingSound)
#ifdef MISSIONPACK
void CG_KamikazeEffect(vec3_t org) NOT_HERE(CG_KamikazeEffect)
void CG_ObeliskExplode(vec3_t org, int entityNum) NOT_HERE(CG_ObeliskExplode)
void CG_ObeliskPain(vec3_t org) NOT_HERE(CG_ObeliskPain)
void CG_InvulnerabilityImpact(vec3_t org, vec3_t angles) NOT_HERE(CG_InvulnerabilityImpact)
void CG_InvulnerabilityJuiced(vec3_t org) NOT_HERE(CG_InvulnerabilityJuiced)
void CG_LightningBoltBeam(vec3_t start, vec3_t end) NOT_HERE(CG_LightningBoltBeam)
void CG_VoiceChatLocal(int mode, qboolean voiceOnly, int clientNum, int color, const char *cmd) NOT_HERE(CG_VoiceChatLocal)
#endif

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

/* A gun and the class it is tested in: the gun in slot 'slot' (its other
 * mode when 'alternate'), beside 'other'. */
typedef struct { int weapon, slot; qboolean alternate; int other; } gun_t;
static const gun_t spas = { WP_SHOTGUN, 1, qfalse, WP_M16 };
static const gun_t m300 = { WP_M40A3, 1, qfalse, WP_M16 };
static const gun_t masterkey = { WP_MASTERKEY, 0, qtrue, WP_SHOTGUN };
static const gun_t *singleShot[] = { &spas, &m300, &masterkey };

static void SetAmmo(int slot, int ammo, int reserve, int altAmmo, int altReserve) {
	bg_weaponInstance_t gun;
	BG_GetSlot(PS, slot, &gun);
	gun.ammo = ammo; gun.reserve = reserve; gun.altAmmo = altAmmo; gun.altReserve = altReserve;
	BG_PutSlot(PS, slot, &gun);
	snapshot.ps = *PS;
}
/* The gun's own magazine and reserve, its slot's other mode left as it is. */
static void GunAmmo(const gun_t *g, int ammo, int reserve) {
	bg_weaponInstance_t gun;
	BG_GetSlot(PS, g->slot, &gun);
	if (g->alternate) { gun.altAmmo = ammo; gun.altReserve = reserve; }
	else { gun.ammo = ammo; gun.reserve = reserve; }
	BG_PutSlot(PS, g->slot, &gun);
	snapshot.ps = *PS;
}
static qboolean Owes(const gun_t *g) {
	bg_weaponInstance_t gun;
	BG_GetSlot(PS, g->slot, &gun);
	return (gun.flags & (g->alternate ? BG_GUN_ALT_UNCHAMBERED : BG_GUN_UNCHAMBERED)) != 0;
}
static int Rounds(const gun_t *g) {
	bg_weaponInstance_t gun;
	BG_GetSlot(PS, g->slot, &gun);
	return g->alternate ? gun.altAmmo : gun.ammo;
}

static void SetupClass(int primary, int secondary, int activeSlot, qboolean alternate) {
	bg_class_t cl = *BG_Class(0);
	playerState_t *ps = &snapshot.ps;
	memset(&cg, 0, sizeof(cg)); memset(&snapshot, 0, sizeof(snapshot));
	memset(&move, 0, sizeof(move)); memset(&use, 0, sizeof(use));
	cl.slot[0] = primary; cl.slot[1] = secondary;
	cl.attachments[0] = cl.attachments[1] = 0;
	CHECK(BG_ValidateClass(&cl));
	BG_ApplyClass(ps, &cl);
	BG_SpawnStance(ps);
	ps->stats[STAT_HEALTH] = 100;
	ps->pm_type = PM_NORMAL; ps->speed = BG_PLAYER_SPEED; ps->gravity = 800;
	ps->origin[2] = 24; ps->groundEntityNum = ENTITYNUM_WORLD; ps->commandTime = 1000;
	ps->slotFlags[0] &= ~BG_GUN_FIRST_DRAW; ps->slotFlags[1] &= ~BG_GUN_FIRST_DRAW;
	if (alternate) ps->slotFlags[activeSlot] |= BG_GUN_ALTERNATE;
	BG_SelectSlot(ps, activeSlot);
	ps->weapon = BG_ActiveSlotWeapon(ps);
	ps->weaponstate = WEAPON_READY; ps->weaponTime = 0; ps->weaponAnimTime = -1; ps->weaponDraw = WDRAW_NORMAL;
	cg.snap = &snapshot; cg.predictedPlayerState = *ps; cg.time = 1000;
	BG_PlayerStateToEntityState(PS, &cg.predictedPlayerEntity.currentState, qfalse);
	CG_Respawn();
	move.ps = PS; move.trace = FloorTrace; move.pointcontents = NoContents;
	move.tracemask = MASK_PLAYERSOLID;
	CG_ValidateWeaponSelect();
	cmdValue = BG_WeaponCommand(&snapshot.ps, cg.weaponSelectSlot, cg.weaponSelectAlternate);
	swapQueued = 0; noAmmoEvents = 0; shots = 0;
}
static void Setup(const gun_t *g) {
	if (g->slot == 0) SetupClass(BG_BaseWeapon(g->weapon), g->other, 0, g->alternate);
	else SetupClass(g->other, g->weapon, 1, qfalse);
	CHECK(PS->weapon == g->weapon);
}

static void PlayEvents(int from) {
	int i;
	memset(raised, 0, sizeof(raised));
	for (i = from; i < PS->eventSequence; i++) {
		int event = PS->events[i & (MAX_PS_EVENTS - 1)] & ~EV_EVENT_BITS;
		CHECK(event > 0 && event <= EV_STANCE_INVALID);
		raised[event]++;
		if (event == EV_FIRE_WEAPON) shots++;
		if (event != EV_NOAMMO) continue;
		noAmmoEvents++;
		cg.predictedPlayerEntity.currentState.event = PS->events[i & (MAX_PS_EVENTS - 1)];
		cg.predictedPlayerEntity.currentState.eventParm = PS->eventParms[i & (MAX_PS_EVENTS - 1)];
		cg.eventPlayerState = PS;
		cg.eventPlayerSequence = i;
		CG_EntityEvent(&cg.predictedPlayerEntity, cg.predictedPlayerEntity.lerpOrigin);
		cg.eventPlayerState = NULL;
	}
}

#define IN_Y   1
#define IN_ALT 2
static void Frame(int buttons, int keys) {
	int from;
	if (keys & IN_Y) CG_NextWeapon_f();
	if (keys & IN_ALT) CG_AltWeapon_f();
	memset(&move.cmd, 0, sizeof(move.cmd));
	move.cmd.buttons = buttons | (swapQueued ? BUTTON_WEAPON_SWAP : 0);
	move.cmd.weapon = cmdValue;
	move.cmd.serverTime = PS->commandTime + msec;
	swapQueued = 0;
	from = PS->eventSequence;
	Pmove(&move);
	BG_UpdateActivate(PS, &use, move.cmd.buttons, qfalse, qfalse);
	cg.time += msec;
	snapshot.ps = *PS;
	PlayEvents(from);
	CG_ValidateWeaponSelect();
	cmdValue = BG_WeaponCommand(&snapshot.ps, cg.weaponSelectSlot, cg.weaponSelectAlternate);
}
static qboolean Selected(const gun_t *g) {
	return cg.weaponSelectSlot == g->slot && cg.weaponSelect == g->weapon;
}
static void Hold(int frames) { while (frames-- > 0) Frame(BUTTON_ATTACK, 0); }
static void Idle(int frames) { while (frames-- > 0) Frame(0, 0); }
static int Frames(int ms) { return (ms + msec - 1) / msec; }

/* A gun's fire cycle in commands, the shot's own command 0: its last
 * command (the clock reaches the open note), and, from the command its action
 * starts, the bolt's and the end's. */
static int open_, close_, fire_, shotEnd;
static void Cycle(const gun_t *g) {
	open_ = PickupNoteTime(g->weapon, ASEQ_FIRE, WNOTE_BOLT_OPEN);
	close_ = PickupNoteTime(g->weapon, ASEQ_FIRE, WNOTE_BOLT_CLOSED);
	fire_ = BG_WeaponFireLength(g->weapon);
	CHECK(0 < open_ && open_ < close_ && close_ < fire_);
	/* the close note, which gives the action's lock away, never shares a
	   think with the action's end, where the held trigger's lock goes on:
	   their gap is longer than Pmove's longest step */
	CHECK(fire_ - close_ > 66);
	CHECK(BG_ShotLimitReached(BG_WeaponFireType(g->weapon), 1));	/* MW2's single shot */
	shotEnd = Frames(open_);
}
static int BoltFrom(int start) { return start + Frames(close_ - open_); }
static int EndFrom(int start) { return start + Frames(fire_ - open_); }

/* (1) A single-shot gun held through its shot: READY under the lock on the
 * shot's last command, its action started from that READY on that same
 * command (MW2's CheckForRechamber, 0x575B07-0x575BA9), a command sooner than
 * with the trigger let go; the action ends READY under the lock, and nothing
 * fires until a release. Let go on the action's last command and pulled on
 * the next, it fires on the command the trigger let go would have. */
static void TestHeldShot(void) {
	static const int rates[] = { 8, 16, 33 };
	int i, k, cmd;
	for (i = 0; i < 3; i++) for (k = 0; k < 3; k++) {
		const gun_t *g = singleShot[k];
		int mag = BG_WeaponMagSize(g->weapon), heldBolt, heldEnd, upEnd;
		msec = rates[i];
		Setup(g); Cycle(g);
		heldBolt = BoltFrom(shotEnd); heldEnd = EndFrom(shotEnd); upEnd = EndFrom(shotEnd + 1);

		/* the trigger let go: READY at the end of the shot's last command,
		   the action from the next, a trigger pulled on its end fires there */
		Frame(BUTTON_ATTACK, 0);
		CHECK(shots == 1 && PS->weaponstate == WEAPON_FIRING && PS->weaponShotCount == 1);
		Idle(shotEnd - 1);
		CHECK(PS->weaponstate == WEAPON_FIRING);
		Frame(0, 0);
		CHECK(PS->weaponstate == WEAPON_READY && BG_ShotAwaitsAction(PS) && PS->weaponShotCount == 0);
		Frame(0, 0);
		CHECK(PS->weaponstate == WEAPON_BOLTING && PS->weaponAnimTime == open_);
		Hold(upEnd - shotEnd - 2);
		CHECK(PS->weaponstate == WEAPON_BOLTING && shots == 1);
		Frame(BUTTON_ATTACK, 0);
		CHECK(shots == 2 && PS->weaponstate == WEAPON_FIRING);

		/* held from the shot on */
		Setup(g);
		Frame(BUTTON_ATTACK, 0);
		for (cmd = 1; cmd < shotEnd; cmd++) {
			Frame(BUTTON_ATTACK, 0);
			CHECK(PS->weaponstate == WEAPON_FIRING && PS->weaponAnimTime == cmd * msec);
		}
		Frame(BUTTON_ATTACK, 0);	/* the shot's last command: the action, never READY */
		CHECK(PS->weaponstate == WEAPON_BOLTING && !BG_ShotAwaitsAction(PS));
		CHECK(PS->weaponAnimTime == open_ && PS->weaponTime == fire_ - open_ && Owes(g));
		for (cmd = shotEnd + 1; cmd < heldEnd; cmd++) {
			Frame(BUTTON_ATTACK, 0);
			CHECK(PS->weaponstate == WEAPON_BOLTING && Owes(g) == (cmd < heldBolt));
		}
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 1 && !Owes(g) && shots == 1);
		CHECK(heldEnd + 1 == upEnd);
		/* nothing for as long as it is held */
		for (cmd = 0; cmd < 3 * Frames(fire_); cmd++) {
			Frame(BUTTON_ATTACK, 0);
			CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 1 && shots == 1);
		}
		CHECK(Rounds(g) == mag - 1 && PS->weaponShotCount == 1);
		Frame(0, 0);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 0 && PS->weaponShotCount == 0 && shots == 1);
		Frame(BUTTON_ATTACK, 0);
		CHECK(shots == 2 && PS->weaponstate == WEAPON_FIRING && Rounds(g) == mag - 2);

		/* let go on the action's last command, pulled on the next: the
		   command the trigger let go would have fired on */
		Setup(g);
		Frame(BUTTON_ATTACK, 0);
		Hold(heldEnd - 1);
		CHECK(PS->weaponstate == WEAPON_BOLTING);
		Frame(0, 0);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime <= 0 && shots == 1);
		Frame(BUTTON_ATTACK, 0);
		CHECK(shots == 2 && PS->weaponstate == WEAPON_FIRING);

		/* (A release only inside the action, pulled again before its end:
		   there is no time running out in it to ask, so the end is locked.) */
		Setup(g);
		Frame(BUTTON_ATTACK, 0);
		Hold(shotEnd);
		Frame(0, 0);
		Hold(heldEnd - shotEnd - 1);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 1 && shots == 1);
		Hold(Frames(fire_));
		CHECK(shots == 1);
	}
}

/* (2) Full auto keeps firing while held: the machinegun (m4_mp) and the M16,
 * Peace's placeholder automatic; neither counts its shots. Each shot is given
 * its whole time (MW2's StartFiring, 0x576E9C-0x576E9F) on the think the last
 * one's ran out, what that overshot by dropped (0x578B71): a shot every
 * Frames(fire time) commands. */
static void TestFullAuto(void) {
	static const int rates[] = { 8, 16, 33 };
	int i, g;
	for (i = 0; i < 3; i++) for (g = 0; g < 2; g++) {
		int weapon = g ? WP_M16 : WP_MACHINEGUN, n, cmd, period, fireTime, last = 0;
		msec = rates[i];
		n = Frames(10 * M16_FIRE_MS);
		if (g) SetupClass(WP_M16, WP_SHOTGUN, 0, qfalse);
		else SetupClass(WP_MACHINEGUN, WP_SHOTGUN, 0, qfalse);
		CHECK(PS->weapon == weapon && !BG_ShotLimitReached(BG_WeaponFireType(weapon), BG_SHOT_COUNT_MAX));
		Frame(BUTTON_ATTACK, 0);
		fireTime = PS->weaponTime;	/* the game's own: the first shot's from 0 */
		CHECK(shots == 1 && fireTime > 0 && fireTime % msec);
		period = Frames(fireTime);
		for (cmd = 1; cmd <= n; cmd++) {
			Frame(BUTTON_ATTACK, 0);
			CHECK(raised[EV_FIRE_WEAPON] == (cmd % period == 0));
			if (raised[EV_FIRE_WEAPON]) last = cmd;
		}
		CHECK(PS->weaponShotCount == 0 && shots == 1 + n / period && shots > BG_SHOT_COUNT_MAX);
		/* the overshoot is gone wherever the command's length does not divide
		   the fire time: the shot's time is whole, not what the last one left */
		CHECK(last % period == 0 && PS->weaponTime == fireTime - (n - last) * msec);
		if (g) CHECK(period == Frames(M16_FIRE_MS));
	}
}

/* What lets a locked gun go without a release: anything that zeroes its time
 * (MW2's PM_Weapon_Idle, 0x5777A0). YY readies the gun and it fires on that
 * command; the gun's return after a grenade is an offhand state, never locked
 * (0x578A94), and it fires once it is up. The count is the hand's, not the
 * gun's: a single-shot gun coming up with the trigger still down from another
 * single-shot gun's shot stays in its raise, under the lock, until a release;
 * a full-auto gun fires as soon as it is up. */
static void TestLockReleases(void) {
	static const int rates[] = { 8, 16, 33 };
	int i, f;
	for (i = 0; i < 3; i++) {
		msec = rates[i];
		Setup(&spas); Cycle(&spas);
		Frame(BUTTON_ATTACK, 0); Hold(EndFrom(shotEnd) + 2);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 1 && shots == 1);
		Frame(BUTTON_ATTACK, IN_Y);
		Frame(BUTTON_ATTACK, IN_Y);
		CHECK(PS->weapon == WP_SHOTGUN && PS->weaponstate == WEAPON_DROPPING && shots == 1);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weapon == WP_SHOTGUN && PS->weaponstate == WEAPON_FIRING && shots == 2);

		/* a frag out of the lock, the trigger held all along */
		Setup(&spas);
		Frame(BUTTON_ATTACK, 0); Hold(EndFrom(shotEnd) + 2);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 1);
		Frame(BUTTON_ATTACK | BUTTON_LETHAL, 0);
		CHECK(PS->weaponAction & WA_TYPE_MASK);
		for (f = 0; f < 5000 && !(PS->weapon == WP_SHOTGUN && PS->weaponstate == WEAPON_READY); f++)
			Frame(BUTTON_ATTACK, 0);
		CHECK(f < 5000 && shots == 2 && PS->weaponShotCount == 1);	/* the frag's throw is the second event */
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_FIRING && shots == 3);

		/* the count goes with the hand: the SPAS's shot held into a swap to
		   the M300, which waits in its raise; to the machinegun, which fires */
		for (f = 0; f < 2; f++) {
			int to = f ? WP_MACHINEGUN : WP_M40A3, raise = BG_WeaponRaiseTime(to), t;
			SetupClass(WP_SHOTGUN, to, 0, qfalse);
			Frame(BUTTON_ATTACK, 0); Hold(EndFrom(shotEnd) + 2);
			Frame(BUTTON_ATTACK, IN_Y);
			for (t = 0; t < 5000 && PS->weapon != to; t++) Frame(BUTTON_ATTACK, 0);
			CHECK(PS->weaponstate == WEAPON_RAISING && PS->weaponTime == raise && shots == 1);
			Hold(Frames(raise) - 1);
			CHECK(PS->weaponstate == WEAPON_RAISING && PS->weaponTime > 0);
			Frame(BUTTON_ATTACK, 0);
			if (f) {
				CHECK(PS->weaponstate == WEAPON_READY && shots == 1);
				Frame(BUTTON_ATTACK, 0);
				CHECK(PS->weaponstate == WEAPON_FIRING && shots == 2);
			} else {
				CHECK(PS->weaponstate == WEAPON_RAISING && PS->weaponTime == 1 && shots == 1);
				Hold(Frames(raise));
				CHECK(PS->weaponstate == WEAPON_RAISING && PS->weaponTime == 1 && shots == 1);
				Frame(0, 0);
				CHECK(PS->weaponstate == WEAPON_READY && PS->weaponShotCount == 0 && shots == 1);
				Frame(BUTTON_ATTACK, 0);
				CHECK(PS->weaponstate == WEAPON_FIRING && shots == 2);
			}
		}
	}
}

/* (3) The last round, held. With rounds in reserve the reload begins on the
 * shot's last command, out of the shot, the action owed (MW2's
 * ShouldBeFiring then CheckFiringAmmo, 0x5770C0), a command before the auto
 * reload a let-go trigger gets on the READY one. With nothing anywhere and
 * nothing to go to, the gun dry-fires on that same command, out of the shot:
 * the out-of-ammo event, BG_DRY_FIRE_TIME, the owed action cleared (MW2's
 * BG_ClearRechamber, 0x577094) and the gun left FIRING, which holds a change
 * until the time has run out (0x578C31); it clicks again every time it runs
 * out while held, and never works its action. */
static void TestLastRoundHeld(void) {
	static const int rates[] = { 8, 16, 33 };
	int i, k, f, period, y;
	for (i = 0; i < 3; i++) for (k = 0; k < 3; k++) {
		const gun_t *g = singleShot[k];
		msec = rates[i];
		period = Frames(BG_DRY_FIRE_TIME);
		Setup(g); Cycle(g);
		GunAmmo(g, 1, 3);
		Frame(BUTTON_ATTACK, 0); Hold(shotEnd - 1);
		CHECK(PS->weaponstate == WEAPON_FIRING);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_RELOADING && PS->weaponAnimSeq == RSEQ_START);
		CHECK(PS->weaponAnimTime == 0 && Owes(g) && PS->weaponShotCount == 0);
		Setup(g);
		GunAmmo(g, 1, 3);
		Frame(BUTTON_ATTACK, 0); Idle(shotEnd);
		CHECK(PS->weaponstate == WEAPON_READY && BG_ShotAwaitsAction(PS));
		Frame(0, 0);
		CHECK(PS->weaponstate == WEAPON_RELOADING && Owes(g));
		if (g == &masterkey) continue;	/* its rifle is always somewhere to go */

		/* nothing anywhere: the gun, and its slot's other gun, dry */
		Setup(g);
		GunAmmo(g, 1, 0);
		SetAmmo(0, 0, 0, 0, 0);
		Frame(BUTTON_ATTACK, 0);
		CHECK(shots == 1 && raised[EV_NOAMMO] == 1 && Selected(g));
		Hold(shotEnd - 1);
		CHECK(PS->weaponstate == WEAPON_FIRING && noAmmoEvents == 1);
		Frame(BUTTON_ATTACK, 0);
		CHECK(raised[EV_NOAMMO] == 1 && PS->weaponstate == WEAPON_FIRING && BG_DryFiring(PS));
		CHECK(PS->weaponTime == BG_DRY_FIRE_TIME && PS->weaponAnimTime < 0 && !Owes(g));
		for (f = 1; f <= 3 * period; f++) {
			Frame(BUTTON_ATTACK, 0);
			CHECK(raised[EV_NOAMMO] == (f % period == 0) && raised[EV_FIRE_WEAPON] == 0);
			CHECK(PS->weaponstate == WEAPON_FIRING && !Owes(g) && PS->weaponstate != WEAPON_BOLTING);
		}
		CHECK(noAmmoEvents == 2 + 3 && shots == 1);
		/* Each click is the empty switch's event, so Y stays held off for as
		   long as the trigger is (MW2's 750 after each, 0x5A084C). The alt
		   toggle is not held off: a change it asks for in the 500 waits for
		   the 500 to run out, and begins on that command. */
		CHECK(cg.time < cg.weaponForcedSelectTime);
		Frame(BUTTON_ATTACK, IN_ALT);
		CHECK(cg.weaponSelect == WP_MASTERKEY && PS->weapon == g->weapon && PS->weaponstate == WEAPON_FIRING);
		for (y = 2; y < period; y++) {
			Frame(BUTTON_ATTACK, 0);
			CHECK(PS->weapon == g->weapon && PS->weaponstate == WEAPON_FIRING);
		}
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weapon == g->weapon && PS->weaponstate == WEAPON_DROPPING && !Owes(g));
		CHECK(raised[EV_NOAMMO] == 0);

		/* let go on the shot's last command, the same gun works its action */
		Setup(g);
		GunAmmo(g, 1, 0);
		SetAmmo(0, 0, 0, 0, 0);
		Frame(BUTTON_ATTACK, 0); Hold(shotEnd - 1); Frame(0, 0);
		CHECK(PS->weaponstate == WEAPON_READY && BG_ShotAwaitsAction(PS) && noAmmoEvents == 1);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_BOLTING && Owes(g));
	}
}

/* (4) The knife's end with the trigger held (MW2's lock on MELEE_FIRE,
 * 0x578B2A-0x578B39): a single-shot gun whose pull has fired is READY under
 * the knife at once, with none of the quick raise, and works an owed action
 * from there. Let go there, the knife ends in the quick raise. A full-auto
 * gun always takes the quick raise, and fires the command after it. */
static void TestKnifeEnd(void) {
	static const int rates[] = { 8, 16, 33 };
	int i, k, knife, end;
	for (i = 0; i < 3; i++) for (k = 0; k < 3; k++) {
		const gun_t *g = singleShot[k];
		msec = rates[i];
		Setup(g); Cycle(g);
		if (shotEnd < 2) continue;	/* no command inside the shot */
		/* the knife pressed inside the shot, the trigger held throughout */
		knife = 1;
		end = knife + Frames(BG_MELEE_TIME);
		Frame(BUTTON_ATTACK, 0);
		Frame(BUTTON_ATTACK | BUTTON_MELEE, 0);
		CHECK(PS->weapon == WP_KNIFE && Owes(g));
		Hold(end - knife - 1);
		CHECK(PS->weapon == WP_KNIFE);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weapon == g->weapon && PS->weaponstate == WEAPON_BOLTING);
		CHECK(PS->weaponAnimTime == open_ && !(PS->weaponAction & WA_TYPE_MASK) && Owes(g));
		Hold(Frames(fire_ - open_));
		/* the shot and the knife's hit, MW2's MeleeFire, are the fire events */
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 1 && !Owes(g) && shots == 2);
		Hold(Frames(fire_));
		CHECK(PS->weaponstate == WEAPON_READY && shots == 2);

		/* the knife's end asked with the command naming another gun (a Y on
		   its last command): no lock, the quick raise; the gun asked for again
		   during it, its end (MW2's MELEE_END) is READY under the lock, and the
		   owed pump starts there */
		Setup(g);
		Frame(BUTTON_ATTACK, 0);
		Frame(BUTTON_ATTACK | BUTTON_MELEE, 0);
		Hold(end - knife - 2);
		Frame(BUTTON_ATTACK, IN_Y);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weapon == g->weapon && PS->weaponstate == WEAPON_RAISING && Owes(g));
		CHECK(PS->weaponTime == BG_WeaponQuickRaiseTime(g->weapon) && PS->weaponShotCount == 1);
		/* back: Y from the rifle is the next slot's gun; the underbarrel is
		   its rifle's alt toggle (Y never selects one) */
		Frame(BUTTON_ATTACK, g->alternate ? IN_ALT : IN_Y);
		CHECK(Selected(g));
		Hold(Frames(BG_WeaponQuickRaiseTime(g->weapon)) - 2);
		CHECK(PS->weapon == g->weapon && PS->weaponstate == WEAPON_RAISING);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weapon == g->weapon && PS->weaponstate == WEAPON_BOLTING && PS->weaponAnimTime == open_);
		CHECK(!(PS->weaponAction & WA_TYPE_MASK) && shots == 2);

		/* let go on the knife's last command: the quick raise */
		Setup(g);
		Frame(BUTTON_ATTACK, 0);
		Frame(BUTTON_ATTACK | BUTTON_MELEE, 0);
		Hold(end - knife - 1);
		Frame(0, 0);
		CHECK(PS->weapon == g->weapon && PS->weaponstate == WEAPON_RAISING);
		CHECK(PS->weaponTime == BG_WeaponQuickRaiseTime(g->weapon) && PS->weaponShotCount == 0);

		/* the knife out of the lock: READY under it at once, the action paid */
		Setup(g);
		Frame(BUTTON_ATTACK, 0); Hold(EndFrom(shotEnd) + 1);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 1);
		Frame(BUTTON_ATTACK | BUTTON_MELEE, 0);
		CHECK(PS->weapon == WP_KNIFE);
		Hold(Frames(BG_MELEE_TIME) - 1);
		CHECK(PS->weapon == WP_KNIFE);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weapon == g->weapon && PS->weaponstate == WEAPON_READY && PS->weaponTime == 1 && shots == 2);
		Hold(Frames(fire_));
		CHECK(PS->weaponstate == WEAPON_READY && shots == 2);
		Frame(0, 0);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_FIRING && shots == 3);
	}
	/* the machinegun */
	for (i = 0; i < 3; i++) {
		int quick;
		msec = rates[i];
		SetupClass(WP_MACHINEGUN, WP_SHOTGUN, 0, qfalse);
		quick = BG_WeaponQuickRaiseTime(WP_MACHINEGUN);
		Frame(BUTTON_ATTACK, 0);
		Frame(BUTTON_ATTACK | BUTTON_MELEE, 0);
		CHECK(PS->weapon == WP_KNIFE);
		Hold(Frames(BG_MELEE_TIME));
		CHECK(PS->weapon == WP_MACHINEGUN && PS->weaponstate == WEAPON_RAISING && PS->weaponTime == quick);
		Hold(Frames(quick));
		CHECK(PS->weaponstate == WEAPON_READY && shots == 2);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_FIRING && shots == 3);
	}
}

/* (5) A segmented reload and the trigger: MW2 asks it to stop and lets the
 * segment it is in play out with its round, then the end; a press counts in
 * the loop at once and in the start only past 0.4 of it (0x578670), and a
 * trigger down as a segment ends counts too. A held trigger fires on the
 * reload's last command (the owner's choice). The reload is begun with R from
 * one round on command 0. Each segment starts afresh on the command the one
 * before it ended (MW2's crossing drops the overshoot, 0x578B71, and
 * PM_SetReloadingState gives the next its whole time, 0x575E20), so it ends
 * Frames(its length) commands later; every length is read from the gun's
 * tables. */
static int start_, loop_, end_, feed;
static void Segments(const gun_t *g) {
	start_ = BG_ReloadSegmentTime(PS, RSEQ_START);
	loop_ = BG_ReloadSegmentTime(PS, RSEQ_LOOP);
	end_ = BG_ReloadSegmentTime(PS, RSEQ_END);
	feed = PickupNoteTime(g->weapon, RSEQ_START, WNOTE_MAG_IN) >= 0 ? 1 : 0;
	CHECK(start_ > 0 && loop_ > 0 && end_ > 0);
}
/* The command the end begins on, after the start and 'passes' loop passes. */
static int EndBegins(int passes) { return Frames(start_) + passes * Frames(loop_); }
/* Commands from command 1 on, the trigger down from 'pressFrom' to 'pressTo';
 * the reload's end is expected to begin on command 'stop', with 'loaded'
 * rounds, and the gun to be ready Frames(end) commands later. A held trigger
 * fires there, the reload's last command (the owner's choice). */
static void ReloadRun(const gun_t *g, int stop, int loaded, int pressFrom, int pressTo) {
	int cmd, last = stop + Frames(end_);
	for (cmd = 1; cmd <= last; cmd++) {
		int attack = cmd >= pressFrom && cmd <= pressTo;
		Frame(attack ? BUTTON_ATTACK : 0, 0);
		if (cmd < last) {
			CHECK(PS->weaponstate == WEAPON_RELOADING && shots == 0);
			CHECK((PS->weaponAnimSeq == RSEQ_END) == (cmd >= stop));
			if (cmd >= stop) CHECK(Rounds(g) == loaded);
		} else if (pressTo < last) {
			CHECK(PS->weaponstate == WEAPON_READY && shots == 0 && Rounds(g) == loaded);
		} else {
			CHECK(PS->weaponstate == WEAPON_FIRING && shots == 1 && Rounds(g) == loaded - 1);
		}
	}
	/* the request is the reload's own, gone with it */
	CHECK(!(PS->weapFlags & PWF_RELOAD_INTERRUPT));
}
static void TestReloadInterrupt(void) {
	static const int rates[] = { 8, 16, 33 };
	int i, k, j;
	for (i = 0; i < 3; i++) for (k = 0; k < 3; k++) {
		const gun_t *g = singleShot[k];
		int mag = BG_WeaponMagSize(g->weapon), full;
		msec = rates[i];
		Setup(g); Segments(g);
		full = EndBegins(mag - 1 - feed);
		CHECK(mag - 1 - feed >= 2);
		/* held from the command after R: the start plays out, then the end */
		Setup(g); GunAmmo(g, 1, 20);
		Frame(BUTTON_RELOAD, 0);
		CHECK(PS->weaponstate == WEAPON_RELOADING && PS->weaponAnimTime == 0);
		ReloadRun(g, EndBegins(0), 1 + feed, 1, 1 << 20);
		/* a tap in the start's first 0.4: forgotten, the reload fills */
		for (j = 1; j * msec * 5 <= start_ * 2; j++) {
			if (j > 1 && j * msec * 5 + 5 * msec <= start_ * 2) continue;	/* the first and the last */
			Setup(g); GunAmmo(g, 1, 20);
			Frame(BUTTON_RELOAD, 0);
			ReloadRun(g, full, mag, j, j);
		}
		/* a tap past it, to the start's last command: the start plays out,
		   then the end */
		for (; j <= Frames(start_); j++) {
			Setup(g); GunAmmo(g, 1, 20);
			Frame(BUTTON_RELOAD, 0);
			ReloadRun(g, EndBegins(0), 1 + feed, j, j);
		}
		/* a tap in the first pass, to its last command: the pass and its
		   round, then the end */
		for (; j <= EndBegins(1); j += Frames(loop_) / 3 + 1) {
			Setup(g); GunAmmo(g, 1, 20);
			Frame(BUTTON_RELOAD, 0);
			ReloadRun(g, EndBegins(1), 2 + feed, j, j);
		}
		Setup(g); GunAmmo(g, 1, 20);
		Frame(BUTTON_RELOAD, 0);
		ReloadRun(g, EndBegins(1), 2 + feed, EndBegins(1), EndBegins(1));
		/* asked to stop and then put away: the request goes with the reload */
		Setup(g); GunAmmo(g, 1, 20);
		Frame(BUTTON_RELOAD, 0);
		Idle(Frames(start_) + 1);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_RELOADING && (PS->weapFlags & PWF_RELOAD_INTERRUPT));
		Frame(0, g->alternate ? IN_ALT : IN_Y);	/* the change goes out with the next command */
		Frame(0, 0);
		CHECK(PS->weaponstate != WEAPON_RELOADING);
		Frame(0, 0);
		CHECK(!(PS->weapFlags & PWF_RELOAD_INTERRUPT));
		/* A press is this command's trigger against the last's (MW2's pm->oldcmd):
		   held into the reload from before it began and let go before the start
		   ends, past its 0.4, it asks nothing, and the reload fills. */
		Setup(g); GunAmmo(g, 1, 20);
		Frame(BUTTON_ATTACK | BUTTON_RELOAD, 0);
		CHECK(PS->weaponstate == WEAPON_RELOADING && shots == 0);
		CHECK(Frames(start_) - 1 > 0 && (Frames(start_) - 1) * msec * 5 > start_ * 2);
		ReloadRun(g, full, mag, 1, Frames(start_) - 1);
		/* So through a freeze, which reads the trigger as up (MW2's PmoveSingle
		   0x57440D) but keeps what is held held (PM_KeepHeldThroughFreeze): the
		   start ends frozen and asks nothing, and a trigger held as the freeze
		   lifts in the pass is no new press; let go before the pass ends, the
		   reload fills. */
		Setup(g); GunAmmo(g, 1, 20);
		Frame(BUTTON_RELOAD, 0);
		PS->pm_flags |= PMF_FROZEN;
		Hold(Frames(start_) + 1);
		CHECK(PS->weaponstate == WEAPON_RELOADING && PS->weaponAnimSeq == RSEQ_LOOP);
		CHECK(!(PS->weapFlags & PWF_RELOAD_INTERRUPT));
		PS->pm_flags &= ~PMF_FROZEN;
		Hold(1);
		CHECK(PS->weaponstate == WEAPON_RELOADING && PS->weaponAnimSeq == RSEQ_LOOP);
		for (j = 0; j < 5000 && PS->weaponstate == WEAPON_RELOADING; j++) Frame(0, 0);
		CHECK(PS->weaponstate == WEAPON_READY && Rounds(g) == mag && shots == 0);
		/* an empty magazine, the trigger held: the reload begins on the pull;
		   with no round from its start, one pass and then the end */
		Setup(g); GunAmmo(g, 0, 20);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_RELOADING && shots == 0);
		ReloadRun(g, EndBegins(feed ? 0 : 1), 1, 1, 1 << 20);
	}
}

/* MW2's ShotLimitReached (0x5769E0, table 0x576A50) for every fire type, and
 * each Peace gun that stands for an MW2 one has that file's fireType. */
static void TestFireTypes(void) {
	int n;
	for (n = 0; n <= BG_SHOT_COUNT_MAX; n++) {
		CHECK(!BG_ShotLimitReached(BG_FIRE_FULLAUTO, n));
		CHECK(BG_ShotLimitReached(BG_FIRE_SINGLESHOT, n) == (n >= 1));
		CHECK(BG_ShotLimitReached(BG_FIRE_DOUBLEBARREL, n) == (n >= 1));
		CHECK(BG_ShotLimitReached(BG_FIRE_BURST2, n) == (n >= 2));
		CHECK(BG_ShotLimitReached(BG_FIRE_BURST3, n) == (n >= 3));
		CHECK(BG_ShotLimitReached(BG_FIRE_BURST4, n) == (n >= 4));
	}
	CHECK(BG_WeaponFireType(WP_SHOTGUN) == BG_FIRE_SINGLESHOT && BG_WeaponFireType(WP_M40A3) == BG_FIRE_SINGLESHOT);
	CHECK(BG_WeaponFireType(WP_MASTERKEY) == BG_FIRE_SINGLESHOT && BG_WeaponFireType(WP_MACHINEGUN) == BG_FIRE_FULLAUTO);
	CHECK(BG_WeaponFireType(WP_ROCKET_LAUNCHER) == BG_FIRE_FULLAUTO && BG_WeaponFireType(WP_M16) == BG_FIRE_FULLAUTO);
	CHECK(BG_WeaponFireType(WP_KNIFE) == BG_FIRE_FULLAUTO && BG_WeaponFireType(WP_FRAG) == BG_FIRE_FULLAUTO);
}

int main(void) {
	TestFireTypes();
	TestHeldShot();
	TestFullAuto();
	TestLockReleases();
	TestLastRoundHeld();
	TestKnifeEnd();
	TestReloadInterrupt();
	printf("PASS: %i held-trigger checks\n", checks);
	return 0;
}
