/* MW2's switch away from a gun that has run completely dry, through the real
 * code: the real full Pmove raises MW2's out-of-ammo event after the gun's
 * last round (BG_SwitchWeaponsIfEmpty, iw4x 0x576320) and on a dry pull of the
 * trigger (PM_Weapon_CheckFiringAmmo, 0x576FE0); the real CG_EntityEvent
 * (cg_event.c) plays it for the player's own view, and the real
 * CG_OutOfAmmoChange and cycle (cg_weapons.c, MW2's 0x5A07C0 and 0x59ED40)
 * select. The client's frame order is moni_test.c's: a frame's bound commands,
 * its command (built with the weapon cgame set when it last drew), the
 * server's think (Pmove, then the use step), then cgame plays the predicted
 * state's new out-of-ammo events, as CG_CheckPlayerstateEvents does, and sets
 * the next command's weapon. The other events are counted, not played: their
 * pictures and sounds are not under test. Y and the alt toggle are the real
 * CG_NextWeapon_f and CG_AltWeapon_f, with MW2's Y lockout after the change
 * (0x5A084C-0x5A0865, 0x59D5CD).
 *
 * The test presses buttons and sets ammunition. Every time it compares
 * against is read from the game's tables. */
#define MASTERKEY_STUBS_REAL_EVENTS
#include "../../code/cgame/cg_weapons.c"
#include "masterkey_selection_stubs.h"

cg_t cg;
cgs_t cgs;
vmCvar_t cg_debugEvents, cg_footsteps, cg_nopredict, cg_synchronousClients;
#ifdef MISSIONPACK
vmCvar_t cg_cameraOrbit, cg_singlePlayerActive;
#endif
static snapshot_t snapshot;
static int checks, swapQueued, swapPresses, msec, cmdValue;
static char weaponArgument[16];
static pmove_t move;
static bg_activate_t use;
/* this frame's events, by type, and every frame's out-of-ammo events so far */
static int raised[EV_STANCE_INVALID + 1], noAmmoEvents;

#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d (%d ms commands): %s\n",__LINE__,msec,#x); exit(1); } } while (0)
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int code, const char *fmt, ...) { (void)code; (void)fmt; abort(); }
void trap_SnapVector(float *v) { int i; for (i=0;i<3;i++) v[i]=rintf(v[i]); }
void trap_SendConsoleCommand(const char *text) {
	if (strcmp(text, "_peaceWeaponSwap\n")) abort();
	swapQueued = 1;
	swapPresses++;
}
const char *CG_Argv(int arg) { (void)arg; return weaponArgument; }
int PickupNoteTime(int weapon, int seq, int kind);
/* cg_killcam.c's test, which CG_EventOwnInput asks */
qboolean CG_Killcam_Active(void) { return cg.snap && (cg.snap->snapFlags & SNAPFLAG_REPLAY); }

/* What the rest of cg_event.c names, for events this test never plays (MSVC's
 * linker asks for every one, GNU ld's --gc-sections for fewer). */
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

/* Ammunition for one slot's two modes, in the inventory instance itself. */
static void SetAmmo(int slot, int ammo, int reserve, int altAmmo, int altReserve) {
	bg_weaponInstance_t gun;
	BG_GetSlot(PS, slot, &gun);
	gun.ammo = ammo; gun.reserve = reserve; gun.altAmmo = altAmmo; gun.altReserve = altReserve;
	BG_PutSlot(PS, slot, &gun);
	snapshot.ps = *PS;
}
static int Shells(int slot, qboolean alternate) {
	bg_weaponInstance_t gun;
	BG_GetSlot(PS, slot, &gun);
	return alternate ? gun.altAmmo : gun.ammo;
}
static qboolean Owes(int slot, qboolean alternate) {
	bg_weaponInstance_t gun;
	BG_GetSlot(PS, slot, &gun);
	return (gun.flags & (alternate ? BG_GUN_ALT_UNCHAMBERED : BG_GUN_UNCHAMBERED)) != 0;
}

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
	BG_PlayerStateToEntityState(PS, &cg.predictedPlayerEntity.currentState, qfalse);
	CG_Respawn();
	move.ps = PS; move.trace = FloorTrace; move.pointcontents = NoContents;
	move.tracemask = MASK_PLAYERSOLID;
	CG_ValidateWeaponSelect();
	cmdValue = BG_WeaponCommand(&snapshot.ps, cg.weaponSelectSlot, cg.weaponSelectAlternate);
	swapQueued = 0; noAmmoEvents = 0;
}

/* The predicted state's new events: the out-of-ammo ones go to the real
 * CG_EntityEvent exactly as CG_CheckPlayerstateEvents hands them over. */
static void PlayEvents(int from) {
	int i;
	memset(raised, 0, sizeof(raised));
	for (i = from; i < PS->eventSequence; i++) {
		int event = PS->events[i & (MAX_PS_EVENTS - 1)] & ~EV_EVENT_BITS;
		CHECK(event > 0 && event <= EV_STANCE_INVALID);
		raised[event]++;
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

#define IN_Y 1
/* One client frame: bound commands, the command, the server's think, the draw. */
static void Frame(int buttons, int keys) {
	int from;
	if (keys & IN_Y) CG_NextWeapon_f();
	memset(&move.cmd, 0, sizeof(move.cmd));
	move.cmd.buttons = buttons | (swapQueued ? BUTTON_WEAPON_SWAP : 0);
	move.cmd.weapon = cmdValue;
	move.cmd.serverTime = PS->commandTime + msec;
	swapQueued = 0;
	from = PS->eventSequence;
	Pmove(&move);
	BG_UpdateActivate(PS, &use, move.cmd.buttons, qfalse, qfalse);
	cg.time += msec;
	snapshot.ps = *PS;	/* own host: the next snapshot is this state */
	PlayEvents(from);
	CG_ValidateWeaponSelect();
	cmdValue = BG_WeaponCommand(&snapshot.ps, cg.weaponSelectSlot, cg.weaponSelectAlternate);
}
static void Settle(void) {
	int t;
	for (t = 0; t < 5000; t++) {
		Frame(0, 0);
		/* not the READY think between a shot and the action it owes */
		if (PS->weaponstate == WEAPON_READY && PS->weapon == cg.weaponSelect &&
		    !BG_ShotAwaitsAction(PS)) return;
	}
	CHECK(0);
}
static int Frames(int ms) { return (ms + msec - 1) / msec; }
static qboolean Selected(int slot, int weapon) {
	return cg.weaponSelectSlot == slot && cg.weaponSelect == weapon &&
		cg.weaponSelectAlternate == (BG_BaseWeapon(weapon) != weapon);
}

typedef struct {
	int dropStart;		/* the first command that ended with the gun going down, or -1 */
	int change;			/* the command another gun came into the hand on, or -1 */
	int changeTo;
} run_t;
/* The commands after a shot (command 0), the trigger let go or still held,
 * until another gun is in the hand or the commands run out. */
static run_t After(qboolean held, int frames) {
	run_t r;
	int f, from = PS->weapon;
	r.dropStart = r.change = -1; r.changeTo = WP_NONE;
	for (f = 1; f <= frames; f++) {
		Frame(held ? BUTTON_ATTACK : 0, 0);
		if (r.dropStart < 0 && PS->weaponstate == WEAPON_DROPPING) r.dropStart = f;
		if (PS->weapon != from) { r.change = f; r.changeTo = PS->weapon; break; }
	}
	return r;
}

/* The Masterkey with no shells and none in reserve goes to its own M16, by
 * MW2's alt switch: no drop (altDropTime 0) and the alt raise. */
static void TestMasterkeyToItsRifle(void) {
	static const int rates[] = { 8, 16, 33 };
	int i, end;
	for (i = 0; i < (int)(sizeof(rates) / sizeof(rates[0])); i++) {
		run_t r;
		msec = rates[i];
		end = Frames(PickupNoteTime(WP_MASTERKEY, ASEQ_FIRE, WNOTE_BOLT_OPEN));

		/* The last shell: the event follows the shot, the M16 is asked for at
		 * once, and it is in hand on the shot's last command (MW2's change
		 * waits out the shot, 0x578C0A), with the pump owed. */
		Setup(0, qtrue);
		SetAmmo(0, BG_WeaponMagSize(WP_M16), 0, 1, 0);
		Frame(BUTTON_ATTACK, 0);
		CHECK(raised[EV_FIRE_WEAPON] == 1 && raised[EV_NOAMMO] == 1);
		CHECK(PS->weapon == WP_MASTERKEY && PS->weaponstate == WEAPON_FIRING && Shells(0, qtrue) == 0);
		CHECK(Selected(0, WP_M16));
		r = After(qfalse, end + 8);
		CHECK(r.change == end && r.changeTo == WP_M16 && r.dropStart < 0);
		CHECK(PS->weaponstate == WEAPON_RAISING && PS->weaponDraw == WDRAW_ALT);
		CHECK(PS->weaponTime == BG_WeaponAltRaiseTime(WP_M16));
		CHECK(Owes(0, qtrue) && noAmmoEvents == 1);

		/* A dry pull of the empty Masterkey: no shot and no action, the event,
		 * the 500ms, and the M16 is in hand on the next command. */
		Setup(0, qtrue);
		SetAmmo(0, BG_WeaponMagSize(WP_M16), 0, 0, 0);
		Frame(BUTTON_ATTACK, 0);
		CHECK(raised[EV_FIRE_WEAPON] == 0 && raised[EV_NOAMMO] == 1);
		CHECK(PS->weapon == WP_MASTERKEY && PS->weaponstate == WEAPON_READY);
		CHECK(PS->weaponTime == BG_DRY_FIRE_TIME && !Owes(0, qtrue) && PS->weaponAnimTime < 0);
		CHECK(Selected(0, WP_M16));
		Frame(0, 0);
		CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_RAISING && PS->weaponDraw == WDRAW_ALT);
		CHECK(PS->weaponTime == BG_WeaponAltRaiseTime(WP_M16));
		Settle();
		CHECK(PS->weapon == WP_M16 && PS->ammo[WP_M16] == BG_WeaponMagSize(WP_M16));

		/* An empty Masterkey on an empty rifle still goes to the rifle: MW2's
		 * cycle asks an underbarrel's step no ammo (0x59ED8C-0x59EDCE). The
		 * rifle's own dry pull then goes to the SPAS, by the rifle's drop. */
		Setup(0, qtrue);
		SetAmmo(0, 0, 0, 0, 0);
		Frame(BUTTON_ATTACK, 0);
		CHECK(raised[EV_NOAMMO] == 1 && Selected(0, WP_M16));
		Settle();
		CHECK(PS->weapon == WP_M16);
		Frame(BUTTON_ATTACK, 0);
		CHECK(raised[EV_FIRE_WEAPON] == 0 && raised[EV_NOAMMO] == 1 && Selected(1, WP_SHOTGUN));
		Frame(0, 0);
		CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_DROPPING && PS->weaponDraw != WDRAW_ALT);
		CHECK(PS->weaponTime == BG_WeaponDropTime(WP_M16));
		Settle();
		CHECK(PS->weapon == WP_SHOTGUN);

		/* Y with the last shell, on one frame: the event finds the rifle
		 * already asked for and keeps it, for the gun in hand is the
		 * underbarrel and its rifle has ammo (0x5A0891-0x5A08C4); a cycle
		 * from that selection would have gone on to the SPAS. */
		Setup(0, qtrue);
		SetAmmo(0, BG_WeaponMagSize(WP_M16), 0, 1, 0);
		Frame(BUTTON_ATTACK, IN_Y);
		CHECK(raised[EV_FIRE_WEAPON] == 1 && raised[EV_NOAMMO] == 1 && Selected(0, WP_M16));
		/* The same on a dry rifle: the rifle has nothing, so the cycle runs
		 * from the selection the Y made, the rifle, and finds the SPAS with
		 * its shells (0x5A08DC); the underbarrel's own step is not asked. */
		Setup(0, qtrue);
		SetAmmo(0, 0, 0, 1, 0);
		Frame(BUTTON_ATTACK, IN_Y);
		CHECK(raised[EV_FIRE_WEAPON] == 1 && raised[EV_NOAMMO] == 1 && Selected(1, WP_SHOTGUN));
		Settle();
		CHECK(PS->weapon == WP_SHOTGUN);
		/* Y, Y and the last shell with both guns dry: the cycle from the SPAS
		 * finds no ammo, and the underbarrel still goes to its rifle
		 * (0x5A08E8-0x5A08FA). */
		Setup(0, qtrue);
		SetAmmo(0, 0, 0, 1, 0);
		SetAmmo(1, 0, 0, 0, 0);
		CG_NextWeapon_f();
		Frame(BUTTON_ATTACK, IN_Y);
		CHECK(raised[EV_FIRE_WEAPON] == 1 && raised[EV_NOAMMO] == 1 && Selected(0, WP_M16));
	}
}

/* An empty M16 or SPAS goes to the other gun, in that gun's own mode, by the
 * ordinary change: its own drop, waiting out the shot as any change does. */
static void TestToTheOtherGun(void) {
	static const int rates[] = { 8, 16, 33 };
	int i;
	for (i = 0; i < (int)(sizeof(rates) / sizeof(rates[0])); i++) {
		run_t r;
		int lock, end;
		msec = rates[i];

		/* The M16's last round: the SPAS, not the M16's own Masterkey, which
		 * still has its shells. The rifle's drop starts once the shot's lock
		 * has run out. */
		Setup(0, qfalse);
		SetAmmo(0, 1, 0, BG_WeaponMagSize(WP_MASTERKEY), 4);
		Frame(BUTTON_ATTACK, 0);
		CHECK(raised[EV_FIRE_WEAPON] == 1 && raised[EV_NOAMMO] == 1 && PS->ammo[WP_M16] == 0);
		CHECK(Selected(1, WP_SHOTGUN));
		lock = PS->weaponTime;
		CHECK(lock > 0 && PS->weaponstate == WEAPON_FIRING);
		r = After(qfalse, Frames(lock) + Frames(BG_WeaponDropTime(WP_M16)) + 4);
		CHECK(r.dropStart == Frames(lock));
		CHECK(r.change > r.dropStart && (r.change - r.dropStart) * msec >= BG_WeaponDropTime(WP_M16));
		CHECK(r.changeTo == WP_SHOTGUN && noAmmoEvents == 1);

		/* The SPAS's last shell: the M16 in its rifle mode, though it was put
		 * away as its Masterkey. The change begins on the shot's last command
		 * and the SPAS owes its pump. */
		Setup(1, qtrue);
		SetAmmo(1, 1, 0, 0, 0);
		end = Frames(PickupNoteTime(WP_SHOTGUN, ASEQ_FIRE, WNOTE_BOLT_OPEN));
		Frame(BUTTON_ATTACK, 0);
		CHECK(raised[EV_FIRE_WEAPON] == 1 && raised[EV_NOAMMO] == 1 && Selected(0, WP_M16));
		r = After(qfalse, end + Frames(BG_WeaponDropTime(WP_SHOTGUN)) + 4);
		CHECK(r.dropStart == end && r.changeTo == WP_M16);
		CHECK((r.change - r.dropStart) * msec >= BG_WeaponDropTime(WP_SHOTGUN) && Owes(1, qfalse));
		CHECK(PS->weaponDraw != WDRAW_ALT && !(PS->slotFlags[0] & BG_GUN_ALTERNATE));

		/* A dry pull of the empty SPAS: the 500ms does not hold the change. */
		Setup(1, qfalse);
		SetAmmo(1, 0, 0, 0, 0);
		Frame(BUTTON_ATTACK, 0);
		CHECK(raised[EV_FIRE_WEAPON] == 0 && raised[EV_NOAMMO] == 1 && Selected(0, WP_M16));
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == BG_DRY_FIRE_TIME);
		Frame(0, 0);
		CHECK(PS->weapon == WP_SHOTGUN && PS->weaponstate == WEAPON_DROPPING);
		CHECK(PS->weaponTime == BG_WeaponDropTime(WP_SHOTGUN));
		Settle();
		CHECK(PS->weapon == WP_M16);

		/* The other gun's reserve counts with its magazine (MW2's
		 * BG_WeaponAmmo, 0x4071C0): an empty magazine with rounds behind it
		 * is somewhere to go, and that gun reloads once it is up. */
		Setup(1, qfalse);
		SetAmmo(1, 0, 0, 0, 0);
		SetAmmo(0, 0, BG_WeaponMagSize(WP_M16), 0, 0);
		Frame(BUTTON_ATTACK, 0);
		CHECK(raised[EV_NOAMMO] == 1 && Selected(0, WP_M16));
		Settle();
		CHECK(PS->weapon == WP_M16);
		Frame(0, 0);
		CHECK(PS->weaponstate == WEAPON_RELOADING);
	}
}

/* With nothing to go to the gun stays: MW2's cycle skips an empty gun
 * (ignoreEmpty, 0x59EEBF), asks for the other gun's own mode only, and the
 * event forces no cycle (0x5A0906). */
static void TestNothingToGoTo(void) {
	static const int rates[] = { 8, 16, 33 };
	int i;
	for (i = 0; i < (int)(sizeof(rates) / sizeof(rates[0])); i++) {
		msec = rates[i];
		/* the SPAS dry, the rifle dry: the Masterkey's shells do not count */
		Setup(1, qfalse);
		SetAmmo(1, 0, 0, 0, 0);
		SetAmmo(0, 0, 0, BG_WeaponMagSize(WP_MASTERKEY), 4);
		Frame(BUTTON_ATTACK, 0);
		CHECK(raised[EV_NOAMMO] == 1 && Selected(1, WP_SHOTGUN));
		Frame(0, 0);
		CHECK(PS->weapon == WP_SHOTGUN && PS->weaponstate == WEAPON_READY);
		/* the rifle dry, the SPAS dry */
		Setup(0, qfalse);
		SetAmmo(0, 0, 0, BG_WeaponMagSize(WP_MASTERKEY), 4);
		SetAmmo(1, 0, 0, 0, 0);
		Frame(BUTTON_ATTACK, 0);
		CHECK(raised[EV_NOAMMO] == 1 && Selected(0, WP_M16));
		Frame(0, 0);
		CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_READY);
		/* the SPAS's last shell with the rifle dry: it stays and works its pump */
		Setup(1, qfalse);
		SetAmmo(1, 1, 0, 0, 0);
		SetAmmo(0, 0, 0, 0, 0);
		Frame(BUTTON_ATTACK, 0);
		CHECK(raised[EV_FIRE_WEAPON] == 1 && raised[EV_NOAMMO] == 1 && Selected(1, WP_SHOTGUN));
		Settle();
		CHECK(PS->weapon == WP_SHOTGUN && !Owes(1, qfalse) && noAmmoEvents == 1);
	}
}

/* A held trigger on a dry gun: the event on the pull and again on each first
 * command its 500ms has run out (MW2's weaponTime is 0 by then), never a shot.
 * Held through the last round with somewhere to go, it is one event and the
 * change of a tap. */
static void TestHeldTrigger(void) {
	static const int rates[] = { 8, 16, 33 };
	int i, f, period, expected;
	for (i = 0; i < (int)(sizeof(rates) / sizeof(rates[0])); i++) {
		run_t tap, hold;
		msec = rates[i];
		period = Frames(BG_DRY_FIRE_TIME);
		Setup(1, qfalse);
		SetAmmo(1, 0, 0, 0, 0);
		SetAmmo(0, 0, 0, 0, 0);
		for (f = 0; f < 4 * period + 1; f++) {
			Frame(BUTTON_ATTACK, 0);
			CHECK(raised[EV_NOAMMO] == (f % period == 0) && raised[EV_FIRE_WEAPON] == 0);
			CHECK(PS->weapon == WP_SHOTGUN && PS->weaponstate == WEAPON_READY && Selected(1, WP_SHOTGUN));
		}
		expected = 5;
		CHECK(noAmmoEvents == expected);
		/* let go: a pull before the 500ms has run out is lost, as MW2's
		 * ProcessHand reads no trigger while weaponTime is left (0x579474) */
		for (f = 1; f < period - 1; f++) Frame(0, 0);
		Frame(BUTTON_ATTACK, 0);
		CHECK(raised[EV_NOAMMO] == 0 && PS->weaponTime > 0);
		Frame(BUTTON_ATTACK, 0);
		CHECK(raised[EV_NOAMMO] == 1 && noAmmoEvents == expected + 1);

		/* the SPAS's last shell, tapped or held */
		Setup(1, qfalse);
		SetAmmo(1, 1, 0, 0, 0);
		Frame(BUTTON_ATTACK, 0);
		tap = After(qfalse, 400);
		Setup(1, qfalse);
		SetAmmo(1, 1, 0, 0, 0);
		Frame(BUTTON_ATTACK, 0);
		hold = After(qtrue, 400);
		CHECK(tap.change >= 0 && hold.change == tap.change && hold.dropStart == tap.dropStart);
		CHECK(hold.changeTo == WP_M16 && noAmmoEvents == 1);
	}
}

/* Rounds in reserve: the gun reloads, nothing is raised and nothing selected.
 * A gun with rounds left raises nothing either, until its last. */
static void TestNotRaised(void) {
	static const int rates[] = { 8, 16, 33 };
	int i, f;
	for (i = 0; i < (int)(sizeof(rates) / sizeof(rates[0])); i++) {
		msec = rates[i];
		/* an empty magazine with a reserve: the reload (MW2's 0x5770C0) */
		Setup(1, qfalse);
		SetAmmo(1, 0, 3, 0, 0);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_RELOADING && raised[EV_NOAMMO] == 0 && Selected(1, WP_SHOTGUN));
		Setup(0, qtrue);
		SetAmmo(0, BG_WeaponMagSize(WP_M16), 0, 0, 2);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weapon == WP_MASTERKEY && PS->weaponstate == WEAPON_RELOADING && Selected(0, WP_MASTERKEY));
		/* Rounds given during a dry pull's 500ms (a scavenger bag): the gun
		 * reloads on the next command, the trigger up or held, as MW2's reload
		 * check comes before the weapon's time is asked (0x578670, 0x579474)
		 * and its reload takes READY (0x575E60). */
		for (f = 0; f < 2; f++) {
			Setup(1, qfalse);
			SetAmmo(1, 0, 0, 0, 0);
			SetAmmo(0, 0, 0, 0, 0);
			Frame(BUTTON_ATTACK, 0);
			CHECK(raised[EV_NOAMMO] == 1 && PS->weaponstate == WEAPON_READY && PS->weaponTime == BG_DRY_FIRE_TIME);
			SetAmmo(1, 0, BG_WeaponMagSize(WP_SHOTGUN), 0, 0);
			Frame(f ? BUTTON_ATTACK : 0, 0);
			CHECK(PS->weapon == WP_SHOTGUN && PS->weaponstate == WEAPON_RELOADING && raised[EV_NOAMMO] == 0);
		}
		/* the last shell with a reserve: the gun reloads itself out of its pump */
		Setup(1, qfalse);
		SetAmmo(1, 1, 3, 0, 0);
		Frame(BUTTON_ATTACK, 0);
		CHECK(raised[EV_FIRE_WEAPON] == 1 && raised[EV_NOAMMO] == 0);
		for (f = 0; f < 200 && PS->weaponstate != WEAPON_RELOADING; f++) Frame(0, 0);
		CHECK(PS->weapon == WP_SHOTGUN && PS->weaponstate == WEAPON_RELOADING && noAmmoEvents == 0);
		/* a magazine held down to its last round: the event comes with that round only */
		Setup(0, qfalse);
		SetAmmo(0, 3, 0, 0, 0);
		for (f = 0; f < 400 && PS->ammo[WP_M16] > 0; f++) {
			int left = PS->ammo[WP_M16];
			Frame(BUTTON_ATTACK, 0);
			CHECK(raised[EV_NOAMMO] == (left == 1 && PS->ammo[WP_M16] == 0));
			if (PS->ammo[WP_M16] > 0) CHECK(Selected(0, WP_M16));
		}
		CHECK(PS->ammo[WP_M16] == 0 && noAmmoEvents == 1 && Selected(1, WP_SHOTGUN));
	}
}

/* A dry pull once the last one's 500ms has run out. */
static void DryPull(void) {
	while (PS->weaponTime > 0) Frame(0, 0);
	Frame(BUTTON_ATTACK, 0);
}
/* The out-of-ammo event played from the player's own entity, as
 * CG_CheckPlayerstateEvents hands one over, with no pmove behind it. */
static void PlayOwnNoAmmo(void) {
	cg.predictedPlayerEntity.currentState.event = EV_NOAMMO | EV_EVENT_BIT1;
	cg.predictedPlayerEntity.currentState.eventParm = 0;
	cg.eventPlayerState = PS;
	cg.eventPlayerSequence = PS->eventSequence;
	CG_EntityEvent(&cg.predictedPlayerEntity, cg.predictedPlayerEntity.lerpOrigin);
	cg.eventPlayerState = NULL;
}
/* Only the player's own live view selects (MW2's isPlayerView, 0x4DD76F, and
 * 0x5A07DA), and a frozen player pulls no trigger at all. */
static void TestOwnLiveViewOnly(void) {
	msec = 8;
	Setup(1, qfalse);
	SetAmmo(1, 0, 0, 0, 0);
	snapshot.snapFlags |= SNAPFLAG_REPLAY;	/* a killcam */
	DryPull();
	CHECK(raised[EV_NOAMMO] == 1 && Selected(1, WP_SHOTGUN));
	snapshot.snapFlags &= ~SNAPFLAG_REPLAY;
	cg.demoPlayback = qtrue;
	DryPull();
	CHECK(raised[EV_NOAMMO] == 1 && Selected(1, WP_SHOTGUN));
	cg.demoPlayback = qfalse;
	cg.clientNum = 1;	/* the playerstate is someone else's: a followed player */
	DryPull();
	CHECK(raised[EV_NOAMMO] == 1 && Selected(1, WP_SHOTGUN));
	cg.clientNum = 0;
	DryPull();
	CHECK(raised[EV_NOAMMO] == 1 && Selected(0, WP_M16));

	/* frozen: the command keeps no trigger (MW2's PmoveSingle 0x57440D) */
	Setup(1, qfalse);
	SetAmmo(1, 0, 0, 0, 0);
	PS->pm_flags |= PMF_FROZEN;
	Frame(BUTTON_ATTACK, 0);
	CHECK(raised[EV_NOAMMO] == 0 && Selected(1, WP_SHOTGUN) && PS->weaponTime == 0);

	/* The event played straight from the player's own entity selects (the
	 * control for the cases after it). */
	Setup(1, qfalse);
	PlayOwnNoAmmo();
	CHECK(Selected(0, WP_M16) && cg.weaponForcedSelectTime == cg.time + CG_EMPTY_SWITCH_LOCKOUT);
	/* Another player's dry fire reaches this client from their entity
	 * (BG_EventReachesOthers): not this player's view, nothing selected. */
	Setup(1, qfalse);
	{
		centity_t *other = &cg_entities[5];
		memset(other, 0, sizeof(*other));
		other->currentState.number = other->currentState.clientNum = 5;
		other->currentState.eType = ET_PLAYER;
		other->currentState.event = EV_NOAMMO | EV_EVENT_BIT1;
		CG_EntityEvent(other, other->lerpOrigin);
	}
	CHECK(Selected(1, WP_SHOTGUN) && cg.weaponForcedSelectTime == 0);
	/* The event in a state that is already dead (MW2's 0x5A07E7). */
	Setup(1, qfalse);
	PS->pm_type = PM_DEAD;
	snapshot.ps = *PS;
	PlayOwnNoAmmo();
	CHECK(Selected(1, WP_SHOTGUN) && cg.weaponForcedSelectTime == 0);
	/* A followed player's playerstate, asked directly: the change is this
	 * client's own selection. */
	Setup(1, qfalse);
	snapshot.ps.pm_flags |= PMF_FOLLOW;
	CG_OutOfAmmoChange();
	CHECK(Selected(1, WP_SHOTGUN) && cg.weaponForcedSelectTime == 0);
}

/* MW2 holds Y and weapprev off after the change from an empty gun that is not
 * an underbarrel (CG_OutOfAmmoChange 0x5A084C-0x5A0865: cg.time plus 750,
 * iw4x.exe 0x798524, for the event's change, which forces no cycle), whether
 * or not anything was then selected; WeaponCycleAllowed refuses them while
 * cg.time is below it (0x59D5CD). A Y then is lost, so the change that went
 * for a gun with ammo goes on. The alt toggle is not held off (its own gate,
 * 0x59D6F0, never reads it), and an underbarrel's change holds nothing
 * (0x5A0847-0x5A084A). */
static void YThroughLockout(int slot, int weapon, int from) {
	int presses = swapPresses;
	while (cg.time < from + CG_EMPTY_SWITCH_LOCKOUT) {
		Frame(0, IN_Y);
		CHECK(Selected(slot, weapon));
	}
	CHECK(swapPresses == presses);
}
static void TestYLockout(void) {
	/* 25ms puts a command's start on the lockout's very end */
	static const int rates[] = { 8, 16, 25, 33 };
	static const int offsets[] = { 0, 160, 320, 480, 640, CG_EMPTY_SWITCH_LOCKOUT - 1 };
	int i, k, t, f;
	CHECK(CG_EMPTY_SWITCH_LOCKOUT == 750);
	for (i = 0; i < (int)(sizeof(rates) / sizeof(rates[0])); i++) {
		msec = rates[i];
		for (k = 0; k < 2; k++) {
			/* k 0: the SPAS's last shell asks for the M16; k 1: the M16's
			 * last round asks for the SPAS. */
			int from = k ? 0 : 1, to = k ? 1 : 0, gun = k ? WP_SHOTGUN : WP_M16;
			int first = k ? WP_M16 : WP_SHOTGUN;
			Setup(from, qfalse);
			SetAmmo(from, 1, 0, k ? BG_WeaponMagSize(WP_MASTERKEY) : 0, k ? 4 : 0);
			Frame(BUTTON_ATTACK, 0);
			t = cg.time;
			CHECK(raised[EV_NOAMMO] == 1 && Selected(to, gun));
			CHECK(cg.weaponForcedSelectTime == t + CG_EMPTY_SWITCH_LOCKOUT && cg.weaponSelectTime == t);
			/* Y on every command short of it is lost; the first after it
			 * is taken. */
			YThroughLockout(to, gun, t);
			Frame(0, IN_Y);
			CHECK(Selected(from, first));
			/* One Y anywhere in it, the reflex as the gun runs dry, and the
			 * change goes on to the gun with ammo, which a later Y leaves. */
			for (f = 0; f < (int)(sizeof(offsets) / sizeof(offsets[0])); f++) {
				Setup(from, qfalse);
				SetAmmo(from, 1, 0, k ? BG_WeaponMagSize(WP_MASTERKEY) : 0, k ? 4 : 0);
				Frame(BUTTON_ATTACK, 0);
				t = cg.time;
				while (cg.time + msec <= t + offsets[f]) Frame(0, 0);
				Frame(0, IN_Y);
				CHECK(Selected(to, gun));
				Settle();
				CHECK(PS->weapon == gun && PS->weaponstate == WEAPON_READY);
				while (cg.time < t + CG_EMPTY_SWITCH_LOCKOUT) Frame(0, 0);
				Frame(0, IN_Y);
				CHECK(Selected(from, first));
			}
		}
		/* A number select is held off with Y (MW2 has none: every selection
		 * but the alt toggle is WeaponCycleAllowed's), and taken after it. */
		Setup(1, qfalse);
		SetAmmo(1, 1, 0, 0, 0);
		Frame(BUTTON_ATTACK, 0);
		t = cg.time;
		CHECK(Selected(0, WP_M16) && cg.weaponForcedSelectTime == t + CG_EMPTY_SWITCH_LOCKOUT);
		Com_sprintf(weaponArgument, sizeof(weaponArgument), "%d", WP_SHOTGUN);
		while (cg.time < t + CG_EMPTY_SWITCH_LOCKOUT) {
			CG_Weapon_f();
			CHECK(Selected(0, WP_M16));
			Frame(0, 0);
		}
		CG_Weapon_f();
		CHECK(Selected(1, WP_SHOTGUN));

		/* The alt toggle is taken in the lockout: the M16's last round asks
		 * for the SPAS, and the toggle, with the M16 still in its shot, asks
		 * for the Masterkey (from a gun with no other mode, the next gun's
		 * underbarrel). */
		Setup(0, qfalse);
		SetAmmo(0, 1, 0, BG_WeaponMagSize(WP_MASTERKEY), 4);
		Frame(BUTTON_ATTACK, 0);
		CHECK(Selected(1, WP_SHOTGUN) && cg.time < cg.weaponForcedSelectTime);
		CHECK(PS->weaponstate == WEAPON_FIRING);
		CG_AltWeapon_f();
		CHECK(Selected(0, WP_MASTERKEY));

		/* The Masterkey's last shell holds nothing: a Y on the next command
		 * goes on from its rifle to the SPAS. */
		Setup(0, qtrue);
		SetAmmo(0, BG_WeaponMagSize(WP_M16), 0, 1, 0);
		Frame(BUTTON_ATTACK, 0);
		CHECK(raised[EV_NOAMMO] == 1 && Selected(0, WP_M16) && cg.weaponForcedSelectTime == 0);
		Frame(0, IN_Y);
		CHECK(Selected(1, WP_SHOTGUN));
		/* The same with the knife pressed on the next command of the shot's
		 * client frame: the knife is in ps.weapon when the event is played,
		 * and the gun the event is about is still the slot's, the
		 * underbarrel (MW2's ps.weapon, which an offhand never replaces), so
		 * nothing is held and its rifle is selected. (A frag or flash press
		 * only starts the gun's drop on that command.) */
		{
			int from;
			Setup(0, qtrue);
			SetAmmo(0, BG_WeaponMagSize(WP_M16), 0, 1, 0);
			from = PS->eventSequence;
			memset(&move.cmd, 0, sizeof(move.cmd));
			move.cmd.buttons = BUTTON_ATTACK; move.cmd.weapon = cmdValue;
			move.cmd.serverTime = PS->commandTime + msec;
			Pmove(&move);
			BG_UpdateActivate(PS, &use, move.cmd.buttons, qfalse, qfalse);
			move.cmd.buttons = BUTTON_MELEE;
			move.cmd.serverTime = PS->commandTime + msec;
			Pmove(&move);
			BG_UpdateActivate(PS, &use, move.cmd.buttons, qfalse, qfalse);
			cg.time += 2 * msec;
			snapshot.ps = *PS;
			CHECK(PS->eventSequence - from <= MAX_PS_EVENTS);
			CHECK(PS->weapon != BG_ActiveSlotWeapon(PS) && BG_ActiveSlotWeapon(PS) == WP_MASTERKEY);
			CHECK(BG_BaseWeapon(PS->weapon) == PS->weapon);
			PlayEvents(from);
			CHECK(raised[EV_FIRE_WEAPON] == 1 && raised[EV_NOAMMO] == 1);
			CHECK(cg.weaponForcedSelectTime == 0 && Selected(0, WP_M16));
		}

		/* Nowhere to go holds Y off all the same, and stamps no selection.
		 * Once it is over a Y takes the empty rifle: Y asks no ammo. */
		Setup(1, qfalse);
		SetAmmo(1, 0, 0, 0, 0);
		SetAmmo(0, 0, 0, BG_WeaponMagSize(WP_MASTERKEY), 4);
		f = cg.weaponSelectTime;
		Frame(BUTTON_ATTACK, 0);
		t = cg.time;
		CHECK(raised[EV_NOAMMO] == 1 && Selected(1, WP_SHOTGUN) && cg.weaponSelectTime == f);
		CHECK(cg.weaponForcedSelectTime == t + CG_EMPTY_SWITCH_LOCKOUT);
		YThroughLockout(1, WP_SHOTGUN, t);
		Frame(0, IN_Y);
		CHECK(Selected(0, WP_M16) && cg.weaponSelectTime == cg.time - msec);
		Settle();
		CHECK(PS->weapon == WP_M16 && PS->ammo[WP_M16] == 0);

		/* A respawn starts with no lockout. */
		Setup(1, qfalse);
		SetAmmo(1, 1, 0, 0, 0);
		Frame(BUTTON_ATTACK, 0);
		CHECK(cg.time < cg.weaponForcedSelectTime);
		CG_Respawn();
		CHECK(cg.weaponForcedSelectTime == 0);
	}
}

int main(void) {
	TestMasterkeyToItsRifle(); TestToTheOtherGun(); TestNothingToGoTo();
	TestHeldTrigger(); TestNotRaised(); TestOwnLiveViewOnly(); TestYLockout();
	printf("PASS: %i empty-gun switch checks\n", checks);
	return 0;
}
