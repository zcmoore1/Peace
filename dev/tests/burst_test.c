/* MW2's burst fire, through the real code: the real full Pmove, the real
 * CG_EntityEvent for the player's own out-of-ammo events and the real
 * selection handlers (cg_weapons.c), in trigger_test.c's frame order (a
 * frame's bound commands, its command, the server's think, then the draw that
 * plays the new events and sets the next command's weapon), at 8, 16 and 33ms
 * commands.
 *
 * The M16 rifle is MW2's m16_shotgun_mp: fireType 3-Round Burst, fireTime
 * 64ms. MW2 has no burst state: a burst is the shot count (StartFiring,
 * iw4x 0x576F93-0x576FC5). While the count is above 0 and below the fire
 * type's limit and the clip has a round, a burst is pending (BurstFirePending,
 * 0x576A70): the gun fires as each shot's time runs out whatever the trigger
 * does (ShouldBeFiring, 0x576D3F-0x576D62) and PM_Weapon runs none of its
 * checks (0x44C444-0x44C4B3), so nothing asked for inside the burst begins
 * before its last shot has left. On the think the last shot's time runs out
 * the gun is READY with the cooldown, player_burstFireCooldown's 0.2s, on its
 * time (WeaponTimeAdjust, 0x578939-0x5789CD). A fresh press made after the
 * pull's first shot is latched (UpdatePendingTriggerPull, 0x576AD0): at the
 * cooldown's end a held trigger with the latch starts the next burst on that
 * think, one held without it is locked like any single-shot gun's
 * (0x5789DD-0x578B71), and only a pull's first shot clears the latch
 * (0x576FB0).
 *
 * The test presses buttons and sets ammunition (and, where said, the frozen
 * flag and a death, which the server sets). Every time it compares against
 * is read from the game's tables and constants. The latch and the count also
 * go through the real playerstate codec (code/qcommon/msg.c). */
#define MASTERKEY_STUBS_REAL_EVENTS
#include "../../code/cgame/cg_weapons.c"
#include "masterkey_selection_stubs.h"
#include "../../code/game/bg_m16_data.h"
#include "../../code/game/bg_g19_data.h"
#include "../../code/qcommon/qcommon.h"
cvar_t *cl_shownet;

cg_t cg;
cgs_t cgs;
vmCvar_t cg_debugEvents, cg_footsteps, cg_nopredict, cg_synchronousClients;
#ifdef MISSIONPACK
vmCvar_t cg_cameraOrbit, cg_singlePlayerActive;
#endif
static snapshot_t snapshot;
static int checks, swapQueued, msec, cmdValue, forward;
/* the first slot's attachments in the next SetupClass, and where Frame records
   each command's playerstate (TestG19) */
static int classFitted;
static playerState_t *traceTo;
static int traceLen;
#define TRACE_MAX 4096
static char weaponArgument[16];
static pmove_t move;
static bg_activate_t use;
/* A client's replay of its own commands: no use code after them (MW2's client
 * has none; the use/reload bit it holds is whatever a snapshot brought). */
static qboolean clientReplay;
/* this frame's events by type, the fire events by the gun their parm names
   (this frame's and every frame's so far), and the out-of-ammo events so far */
static int raised[EV_STANCE_INVALID + 1], fired[MAX_WEAPONS], firedNow[MAX_WEAPONS], noAmmoEvents;

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
/* The floor, and while ladderWall is set a ladder's face across +x at
 * LADDER_WALL_X, as high as anyone climbs. */
#define LADDER_WALL_X 40
static qboolean ladderWall;
static void WorldTrace(trace_t *tr, const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, int pass, int mask) {
	float s = start[0] + maxs[0], e = end[0] + maxs[0];
	FloorTrace(tr, start, mins, maxs, end, pass, mask);
	if (ladderWall && !tr->startsolid && s <= LADDER_WALL_X && e > LADDER_WALL_X) {
		float f = (LADDER_WALL_X - s) / (e - s) - 0.001f; if (f < 0) f = 0;
		if (f < tr->fraction) {
			memset(&tr->plane, 0, sizeof(tr->plane));
			tr->fraction = f; tr->plane.normal[0] = -1; tr->entityNum = ENTITYNUM_WORLD;
			tr->contents = CONTENTS_SOLID; tr->surfaceFlags = SURF_LADDER;
			tr->endpos[0] = start[0] + f * (end[0] - start[0]); tr->endpos[1] = start[1] + f * (end[1] - start[1]);
			tr->endpos[2] = start[2] + f * (end[2] - start[2]);
		}
	}
}

#define PS (&cg.predictedPlayerState)


/* The rifle in slot 0, in its own mode or the Masterkey's, beside the SPAS. */
static void SetAmmo(int slot, int ammo, int reserve, int altAmmo, int altReserve) {
	bg_weaponInstance_t gun;
	BG_GetSlot(PS, slot, &gun);
	gun.ammo = ammo; gun.reserve = reserve; gun.altAmmo = altAmmo; gun.altReserve = altReserve;
	BG_PutSlot(PS, slot, &gun);
	snapshot.ps = *PS;
}
static int Rounds(void) {
	bg_weaponInstance_t gun;
	BG_GetSlot(PS, 0, &gun);
	return gun.ammo;
}

static void SetupClass(int primary, int secondary, int activeSlot, qboolean alternate) {
	bg_class_t cl = *BG_Class(0);
	playerState_t *ps = &snapshot.ps;
	memset(&cg, 0, sizeof(cg)); memset(&snapshot, 0, sizeof(snapshot));
	memset(&move, 0, sizeof(move)); memset(&use, 0, sizeof(use));
	cl.slot[0] = primary; cl.slot[1] = secondary;
	cl.attachments[0] = classFitted; cl.attachments[1] = 0;
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
	move.ps = PS; move.trace = WorldTrace; move.pointcontents = NoContents; ladderWall = qfalse; clientReplay = qfalse;
	move.tracemask = MASK_PLAYERSOLID;
	CG_ValidateWeaponSelect();
	cmdValue = BG_WeaponCommand(&snapshot.ps, cg.weaponSelectSlot, cg.weaponSelectAlternate);
	swapQueued = 0; noAmmoEvents = 0; forward = 0;
	memset(fired, 0, sizeof(fired));
}
/* The rifle in hand, ready, a full magazine. */
static int mag;
static void Rifle(void) {
	SetupClass(WP_M16, WP_SHOTGUN, 0, qfalse);
	mag = BG_WeaponMagSize(WP_M16);
	CHECK(PS->weapon == WP_M16 && Rounds() == mag && mag > 6 && PS->weaponShotCount == 0);
	CHECK(BG_WeaponFireType(WP_M16) == BG_FIRE_BURST3 && BG_WeaponFireLength(WP_M16) == 0);
}

/* A frame's events: every fire event by the gun its parm names (MW2's fire
 * event is one a shot, 0x57738E, and cgame draws the shot's picture, sound,
 * bullet and kick from each), and the player's own out-of-ammo events through
 * the real CG_EntityEvent. */
static void PlayEvents(int from) {
	int i;
	memset(raised, 0, sizeof(raised)); memset(firedNow, 0, sizeof(firedNow));
	for (i = from; i < PS->eventSequence; i++) {
		int event = PS->events[i & (MAX_PS_EVENTS - 1)] & ~EV_EVENT_BITS;
		int parm = PS->eventParms[i & (MAX_PS_EVENTS - 1)];
		CHECK(event > 0 && event <= EV_STANCE_INVALID);
		raised[event]++;
		if (event == EV_FIRE_WEAPON) { CHECK(parm > WP_NONE && parm < MAX_WEAPONS); fired[parm]++; firedNow[parm]++; }
		if (event != EV_NOAMMO) continue;
		noAmmoEvents++;
		cg.predictedPlayerEntity.currentState.event = PS->events[i & (MAX_PS_EVENTS - 1)];
		cg.predictedPlayerEntity.currentState.eventParm = parm;
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
	move.cmd.forwardmove = forward;
	move.cmd.serverTime = PS->commandTime + msec;
	swapQueued = 0;
	from = PS->eventSequence;
	Pmove(&move);
	if (!clientReplay) BG_UpdateActivate(PS, &use, move.cmd.buttons, qfalse, qfalse);
	cg.time += msec;
	snapshot.ps = *PS;
	PlayEvents(from);
	CG_ValidateWeaponSelect();
	cmdValue = BG_WeaponCommand(&snapshot.ps, cg.weaponSelectSlot, cg.weaponSelectAlternate);
	if (traceTo) { CHECK(traceLen < TRACE_MAX); traceTo[traceLen++] = *PS; }
}
static void Hold(int frames) { while (frames-- > 0) Frame(BUTTON_ATTACK, 0); }
static void Idle(int frames) { while (frames-- > 0) Frame(0, 0); }
static int Frames(int ms) { return (ms + msec - 1) / msec; }
static qboolean Latched(void) { return (PS->weapFlags & PWF_PENDING_PULL) != 0; }
static qboolean Quiet(void) { return !(PS->weaponAction & WA_TYPE_MASK); }

/* The playerstate through the real codec. */
static void Wire(playerState_t *decoded) {
	byte data[MAX_MSGLEN]; msg_t msg; playerState_t zero;
	memset(&zero, 0, sizeof(zero));
	MSG_Init(&msg, data, sizeof(data)); MSG_WriteDeltaPlayerstate(&msg, &zero, PS);
	CHECK(!msg.overflowed); MSG_BeginReading(&msg);
	MSG_ReadDeltaPlayerstate(&msg, &zero, decoded);
}

/* The burst in commands, the first shot's own command 0: a shot takes c of
 * them (its whole fireTime, 0x576E9C, given on the think the last one's ran
 * out), the cooldown begins on 3c and runs out on C. Every length is the
 * game's: the rifle's fire time from its generated data and the cooldown's
 * constant, both checked against what pmove stamps. */
static int c, cool, C;
static void Rate(int ms) {
	msec = ms;
	c = Frames(M16_FIRE_MS); cool = Frames(BG_BURST_FIRE_COOLDOWN); C = 3 * c + cool;
	CHECK(c >= 2 && cool >= 2);	/* commands inside a shot and inside the cooldown */
}
static const int rates[] = { 8, 16, 33 };
#define EACH_RATE for (r = 0; r < 3; r++)

/* The first shot, on command 0. */
static void Pull(void) {
	Frame(BUTTON_ATTACK, 0);
	CHECK(fired[WP_M16] == 1 && firedNow[WP_M16] == 1 && PS->weaponstate == WEAPON_FIRING);
	CHECK(PS->weaponTime == M16_FIRE_MS && PS->weaponShotCount == 1 && Rounds() == mag - 1);
	CHECK(!Latched() && BG_BurstFirePending(PS));	/* the press latched, and the pull's first shot cleared it (0x576FB0) */
}
/* What a burst's command 'cmd' (1 to 3c - 1) must have done whatever is held:
 * the second and third shots leave on c and 2c, and the gun is in its shot. */
static void InBurst(int cmd) {
	CHECK(firedNow[WP_M16] == (cmd == c || cmd == 2 * c) && raised[EV_FIRE_WEAPON] == firedNow[WP_M16]);
	CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_FIRING && Quiet());
	CHECK(PS->weaponShotCount == 1 + cmd / c && BG_BurstFirePending(PS) == (cmd < 2 * c));
	if (firedNow[WP_M16]) CHECK(PS->weaponTime == M16_FIRE_MS);
}
/* Commands 1 to 3c - 1 with 'buttons' down. */
static void RestOfBurst(int buttons) {
	int cmd;
	for (cmd = 1; cmd < 3 * c; cmd++) { Frame(buttons, 0); InBurst(cmd); }
	CHECK(fired[WP_M16] % 3 == 0 && PS->weaponShotCount == 3);
}
/* The cooldown's first command: READY, the whole cooldown on the time. */
static void CooldownBegins(void) {
	CHECK(!raised[EV_FIRE_WEAPON] && PS->weapon == WP_M16 && PS->weaponstate == WEAPON_READY);
	CHECK(PS->weaponTime == BG_BURST_FIRE_COOLDOWN && !BG_BurstFirePending(PS));
}

/* (1) A tap (MW2's BurstFirePending, 0x576A70): the trigger let go after the
 * first shot's command, the other two leave on c and 2c by themselves
 * (ShouldBeFiring, 0x576D3F-0x576D62). On 3c the gun is READY with the
 * cooldown on its time (0x578939-0x5789CD); on C the time has run out with
 * the trigger up, which ends the pull (0x578B51-0x578B67), and the gun is
 * free: a press on C or after it fires on its own command. */
static void TestTap(void) {
	int r, cmd;
	EACH_RATE {
		Rate(rates[r]); Rifle();
		Pull();
		RestOfBurst(0);
		CHECK(fired[WP_M16] == 3 && Rounds() == mag - 3);
		Frame(0, 0);	/* 3c */
		CooldownBegins();
		CHECK(PS->weaponShotCount == 3);
		for (cmd = 3 * c + 1; cmd < C; cmd++) {
			Frame(0, 0);
			CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime > 0 && PS->weaponShotCount == 3 && !raised[EV_FIRE_WEAPON]);
		}
		Frame(0, 0);	/* C */
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 0 && PS->weaponShotCount == 0 && fired[WP_M16] == 3);
		Frame(BUTTON_ATTACK, 0);
		CHECK(firedNow[WP_M16] == 1 && PS->weaponShotCount == 1 && !Latched());
		for (cmd = 1; cmd < 3 * c; cmd++) { Frame(0, 0); InBurst(cmd); }
		CHECK(fired[WP_M16] == 6 && Rounds() == mag - 6);

		/* pressed on C itself: the press is latched before the time step
		   (0x44C40A), so the crossing starts a new pull and the shot leaves
		   on C */
		Rifle(); Pull(); RestOfBurst(0);
		Idle(cool);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime > 0 && fired[WP_M16] == 3);
		Frame(BUTTON_ATTACK, 0);
		CHECK(firedNow[WP_M16] == 1 && PS->weaponShotCount == 1 && !Latched() && PS->weaponTime == M16_FIRE_MS);
	}
}

/* (2) Held through the burst and after it: the same three shots, the same
 * cooldown, and on C the count is at its limit with the trigger down and no
 * fresh press: MW2's lock (0x5789DD-0x578AE6), READY with a time of 1, every
 * command, for as long as it is held. Let go, the next command is free. */
static void TestHeld(void) {
	int r, cmd;
	EACH_RATE {
		Rate(rates[r]); Rifle();
		Pull();
		RestOfBurst(BUTTON_ATTACK);
		Frame(BUTTON_ATTACK, 0);	/* 3c */
		CooldownBegins();
		Hold(cool - 1);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime > 0 && fired[WP_M16] == 3);
		for (cmd = 0; cmd < 3 * cool; cmd++) {	/* C and on */
			Frame(BUTTON_ATTACK, 0);
			CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 1 && PS->weaponShotCount == 3);
			CHECK(fired[WP_M16] == 3 && !Latched());
		}
		Frame(0, 0);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 0 && PS->weaponShotCount == 0);
		Frame(BUTTON_ATTACK, 0);
		CHECK(firedNow[WP_M16] == 1 && fired[WP_M16] == 4 && PS->weaponShotCount == 1);
		for (cmd = 1; cmd < 3 * c; cmd++) { Frame(BUTTON_ATTACK, 0); InBurst(cmd); }
		CHECK(fired[WP_M16] == 6);
	}
}

/* (3) A press during the cooldown (MW2's UpdatePendingTriggerPull, 0x576AD0):
 * the fresh press is latched; with the trigger still down on C the latch
 * keeps the lock off (0x5789DD) and ends the pull (0x578B51-0x578B67), so the
 * next burst's first shot leaves on C exactly, and clears the latch
 * (0x576FB0). Held on from there with no fresh press, that burst's own C is
 * the lock. A press let go again before C fires nothing: the gun is free on
 * C, the latch still set as MW2 leaves it, and the next press fires at once.
 * The latch goes through the real codec. */
static void TestPressInTheCooldown(void) {
	int r, k, cmd;
	playerState_t decoded;
	EACH_RATE {
		int presses[3];
		Rate(rates[r]);
		presses[0] = 3 * c; presses[1] = 3 * c + cool / 2; presses[2] = C - 1;
		for (k = 0; k < 3; k++) {
			int p = presses[k];
			if (k && p == presses[k - 1]) continue;
			Rifle(); Pull(); RestOfBurst(0);
			Idle(p - 3 * c);
			for (cmd = p; cmd < C; cmd++) {
				Frame(BUTTON_ATTACK, 0);
				CHECK(!raised[EV_FIRE_WEAPON] && PS->weaponstate == WEAPON_READY && PS->weaponTime > 0);
				CHECK(Latched() && PS->weaponShotCount == 3);
				Wire(&decoded);
				CHECK((decoded.weapFlags & PWF_PENDING_PULL) && decoded.weaponShotCount == 3);
			}
			Frame(BUTTON_ATTACK, 0);	/* C */
			CHECK(firedNow[WP_M16] == 1 && PS->weaponstate == WEAPON_FIRING && PS->weaponTime == M16_FIRE_MS);
			CHECK(PS->weaponShotCount == 1 && !Latched() && fired[WP_M16] == 4);
			Wire(&decoded);
			CHECK(!(decoded.weapFlags & PWF_PENDING_PULL) && decoded.weaponShotCount == 1);
			for (cmd = 1; cmd < 3 * c; cmd++) { Frame(BUTTON_ATTACK, 0); InBurst(cmd); }
			Frame(BUTTON_ATTACK, 0);
			CooldownBegins();
			Hold(cool - 1);
			for (cmd = 0; cmd < 2 * cool; cmd++) {
				Frame(BUTTON_ATTACK, 0);
				CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 1 && fired[WP_M16] == 6);
			}
		}
		/* pressed and let go again before C */
		for (k = 0; k < 2; k++) {
			int p = k ? C - 2 : 3 * c;
			Rifle(); Pull(); RestOfBurst(0);
			Idle(p - 3 * c);
			Frame(BUTTON_ATTACK, 0);
			CHECK(Latched() && !raised[EV_FIRE_WEAPON]);
			Idle(C - p - 1);
			CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime > 0);
			Frame(0, 0);	/* C */
			CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 0 && PS->weaponShotCount == 0);
			CHECK(fired[WP_M16] == 3 && Latched());
			Idle(3);
			CHECK(fired[WP_M16] == 3);
			Frame(BUTTON_ATTACK, 0);
			CHECK(firedNow[WP_M16] == 1 && PS->weaponShotCount == 1 && !Latched());
		}
	}
}

/* (4) A second press inside the burst: the trigger let go and pulled again
 * before the last shot has left. It adds no shot (three, not four: the burst
 * fires on its count, not on the trigger) and it is latched, so held to C it
 * starts the next burst there. */
static void TestSecondPressInTheBurst(void) {
	int r, k, cmd;
	EACH_RATE {
		int presses[4];
		Rate(rates[r]);
		presses[0] = 2; presses[1] = c; presses[2] = 2 * c; presses[3] = 3 * c - 1;
		for (k = 0; k < 4; k++) {
			int p = presses[k];
			if (p < 2 || (k && p <= presses[k - 1])) continue;
			Rifle(); Pull();
			for (cmd = 1; cmd < 3 * c; cmd++) {
				Frame(cmd >= p ? BUTTON_ATTACK : 0, 0);
				InBurst(cmd);
				CHECK(Latched() == (cmd >= p));
			}
			Frame(BUTTON_ATTACK, 0);
			CooldownBegins();
			Hold(cool - 1);
			CHECK(fired[WP_M16] == 3 && Latched());
			Frame(BUTTON_ATTACK, 0);	/* C */
			CHECK(firedNow[WP_M16] == 1 && PS->weaponShotCount == 1 && !Latched());
		}
	}
}

/* (5) What is asked for inside a burst waits for it (MW2 runs none of its
 * checks while a burst is pending, 0x44C444-0x44C4B3). Each is asked on
 * command 1; the three shots still leave on 0, c and 2c. */

/* Y, the other gun: the change begins on 3c, out of the cooldown's READY (the
 * change check refuses FIRING while the shot has time left, 0x578C31, and
 * takes READY with time, 0x578C0A-0x578C47), and its drop replaces the
 * cooldown. The count goes with the hand until a crossing with the trigger
 * up: the drop's. */
static void TestSwapWaits(void) {
	int r, cmd, drop;
	EACH_RATE {
		Rate(rates[r]); Rifle(); Pull();
		Frame(0, IN_Y);	/* command 1: the next command names the SPAS */
		InBurst(1);
		CHECK(cg.weaponSelect == WP_SHOTGUN);
		for (cmd = 2; cmd < 3 * c; cmd++) {
			Frame(0, 0); InBurst(cmd);
			CHECK(BG_RequestedWeapon(PS, move.cmd.weapon) == WP_SHOTGUN);
		}
		Frame(0, 0);	/* 3c */
		drop = BG_WeaponDropTime(WP_M16);
		CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_DROPPING && PS->weaponTime == drop);
		CHECK(BG_HolsterTime(PS) == drop && PS->weaponShotCount == 3 && fired[WP_M16] == 3);
		Idle(Frames(drop) - 1);
		CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_DROPPING);
		Frame(0, 0);
		CHECK(PS->weapon == WP_SHOTGUN && PS->weaponstate == WEAPON_RAISING && PS->weaponShotCount == 0);
	}
}

/* B, the Masterkey: MW2's alt switch, also on 3c, with no drop (altDropTime
 * 0) and the Masterkey's altRaiseTime. The count, 3, goes with the hand to a
 * Single Shot gun: with the trigger up the raise's end ends the pull; held
 * from the burst's first shot the raise hangs under the lock until it is let
 * go (3 >= 1, 0x5789DD-0x578B28); with a fresh press made inside the burst
 * and held, the latch keeps the lock off and ends the pull at the raise's end
 * (0x5789DD reads it for every fire type), and the Masterkey fires on the
 * next command, which clears it. */
static void TestAltSwitchWaits(void) {
	int r, v, cmd, raise, ends;
	EACH_RATE for (v = 0; v < 3; v++) {	/* 0 the trigger up, 1 held, 2 pressed again inside the burst */
		int held = v == 1 ? BUTTON_ATTACK : 0;
		Rate(rates[r]); Rifle(); Pull();
		Frame(held, IN_ALT);	/* command 1 */
		InBurst(1);
		CHECK(cg.weaponSelect == WP_MASTERKEY);
		if (v == 2) held = BUTTON_ATTACK;
		for (cmd = 2; cmd < 3 * c; cmd++) { Frame(held, 0); InBurst(cmd); }
		CHECK(Latched() == (v == 2));
		Frame(held, 0);	/* 3c */
		raise = BG_WeaponAltRaiseTime(WP_MASTERKEY);
		CHECK(PS->weapon == WP_MASTERKEY && PS->weaponstate == WEAPON_RAISING && PS->weaponDraw == WDRAW_ALT);
		CHECK(PS->weaponTime == raise && raise == MASTERKEY_ALT_RAISE_MS && BG_WeaponAltDropTime(WP_M16) == 0);
		CHECK(PS->weaponShotCount == 3 && fired[WP_M16] == 3 && !BG_BurstFirePending(PS));
		ends = Frames(raise);
		for (cmd = 1; cmd < ends; cmd++) { Frame(held, 0); CHECK(PS->weaponstate == WEAPON_RAISING && PS->weaponTime > 0); }
		Frame(held, 0);	/* the raise's last command */
		if (v == 1) {
			CHECK(PS->weaponstate == WEAPON_RAISING && PS->weaponTime == 1 && PS->weaponShotCount == 3);
			Hold(ends);
			CHECK(PS->weaponstate == WEAPON_RAISING && PS->weaponTime == 1 && !fired[WP_MASTERKEY]);
			Frame(0, 0);
		}
		CHECK(PS->weapon == WP_MASTERKEY && PS->weaponstate == WEAPON_READY && PS->weaponTime == 0);
		CHECK(PS->weaponShotCount == 0 && !fired[WP_MASTERKEY] && Latched() == (v == 2));
		Frame(BUTTON_ATTACK, 0);
		CHECK(fired[WP_MASTERKEY] == 1 && PS->weaponShotCount == 1 && !Latched());
	}
}

/* R: held from command 1, the reload is taken on the command after the last
 * shot's, 2c + 1, out of the shot (PM_BeginWeaponReload takes FIRING,
 * 0x575E8E-0x575EA8): the last shot's time and the cooldown never happen, and
 * the reload begins a new pull (0x575EFE). A tap let go before the last shot
 * is lost. */
static void TestReloadWaits(void) {
	int r, cmd;
	EACH_RATE {
		Rate(rates[r]); Rifle(); Pull();
		for (cmd = 1; cmd <= 2 * c; cmd++) { Frame(BUTTON_RELOAD, 0); InBurst(cmd); }
		Frame(BUTTON_RELOAD, 0);	/* 2c + 1 */
		CHECK(PS->weaponstate == WEAPON_RELOADING && PS->weaponAnimTime == 0 && PS->weaponShotCount == 0);
		CHECK(PS->weaponTime == BG_ReloadSegmentTime(PS, PS->weaponAnimSeq) && fired[WP_M16] == 3 && Rounds() == mag - 3);

		Rifle(); Pull();
		Frame(BUTTON_RELOAD, 0); InBurst(1);
		for (cmd = 2; cmd < 3 * c; cmd++) { Frame(0, 0); InBurst(cmd); }
		Frame(0, 0);
		CooldownBegins();
		Idle(cool + 3);
		CHECK(PS->weaponstate == WEAPON_READY && Rounds() == mag - 3 && PS->weaponShotCount == 0);
	}
}

/* The knife and a frag: a press inside the burst is lost (MW2's melee and
 * offhand checks are skipped, and their fresh-press tests compare with the
 * last step's buttons, so a button still down afterwards is no press). One
 * pressed on the command after the last shot's is taken there, out of the
 * shot, as from any gun. */
static void TestKnifeAndFragWait(void) {
	int r, k, cmd;
	EACH_RATE for (k = 0; k < 2; k++) {
		int button = k ? BUTTON_LETHAL : BUTTON_MELEE;
		Rate(rates[r]);
		/* a tap on command 1 */
		Rifle(); Pull();
		Frame(button, 0); InBurst(1);
		for (cmd = 2; cmd < 3 * c; cmd++) { Frame(0, 0); InBurst(cmd); }
		for (cmd = 3 * c; cmd <= C + 3; cmd++) { Frame(0, 0); CHECK(PS->weapon == WP_M16 && Quiet()); }
		CHECK(fired[WP_M16] == 3 && !fired[WP_KNIFE] && !raised[EV_SWITCH_OFFHAND]);
		/* held from command 1 on */
		Rifle(); Pull();
		for (cmd = 1; cmd < 3 * c; cmd++) { Frame(button, 0); InBurst(cmd); }
		for (cmd = 3 * c; cmd <= C + 3; cmd++) { Frame(button, 0); CHECK(PS->weapon == WP_M16 && Quiet()); }
		CHECK(fired[WP_M16] == 3 && !fired[WP_KNIFE]);
		/* pressed on 2c + 1 */
		Rifle(); Pull();
		for (cmd = 1; cmd <= 2 * c; cmd++) { Frame(0, 0); InBurst(cmd); }
		Frame(button, 0);
		if (k) CHECK((PS->weaponAction & WA_TYPE_MASK) == WA_LETHAL && raised[EV_SWITCH_OFFHAND] == 1);
		else CHECK(PS->weapon == WP_KNIFE && (PS->weaponAction & WA_TYPE_MASK) == WA_MELEE);
		CHECK(PS->weaponShotCount == 3 && fired[WP_M16] == 3);
	}
}

/* The knife's end (MW2's lock on MELEE_FIRE, 0x578B2A-0x578B39) with the
 * burst's count still on the hand. The trigger held from the burst's first
 * shot through the knife: READY in hand under the lock, with no quick raise.
 * The trigger pressed during the knife and held: MW2 latches the press for
 * the gun under its knife (its ps->weapon, 0x576AD3), so the knife's end is
 * not locked and ends the pull; the gun takes its quick raise and fires a
 * burst when it is up. */
static void TestKnifeEnd(void) {
	int r, cmd, quick, f;
	EACH_RATE {
		Rate(rates[r]);
		quick = BG_WeaponQuickRaiseTime(WP_M16);
		Rifle(); Pull();
		for (cmd = 1; cmd <= 2 * c; cmd++) { Frame(BUTTON_ATTACK, 0); InBurst(cmd); }
		Frame(BUTTON_ATTACK | BUTTON_MELEE, 0);
		CHECK(PS->weapon == WP_KNIFE && !Latched());
		Hold(Frames(BG_MELEE_TIME) - 1);
		CHECK(PS->weapon == WP_KNIFE);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_READY && PS->weaponTime == 1 && Quiet());
		Hold(cool);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 1 && fired[WP_M16] == 3 && PS->weaponShotCount == 3);

		Rifle(); Pull();
		for (cmd = 1; cmd <= 2 * c; cmd++) { Frame(0, 0); InBurst(cmd); }
		Frame(BUTTON_MELEE, 0);
		CHECK(PS->weapon == WP_KNIFE);
		Idle(2);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weapon == WP_KNIFE && Latched() && PS->weaponShotCount == 3);
		Hold(Frames(BG_MELEE_TIME) - 4);
		CHECK(PS->weapon == WP_KNIFE);
		Frame(BUTTON_ATTACK, 0);	/* the knife's end */
		CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_RAISING && PS->weaponTime == quick);
		CHECK(PS->weaponShotCount == 0 && Latched());
		for (f = 0; f < 5000 && PS->weaponstate == WEAPON_RAISING; f++) Frame(BUTTON_ATTACK, 0);
		CHECK(f == Frames(quick) && PS->weaponstate == WEAPON_READY && fired[WP_M16] == 3);
		Frame(BUTTON_ATTACK, 0);
		CHECK(firedNow[WP_M16] == 1 && PS->weaponShotCount == 1 && !Latched());
	}
}

/* A sprint: its flag, speed and meter are no weapon check and come on inside
 * the burst (PM_UpdateSprint tests no weapon state), the shots go on, and the
 * gun lowers into the sprint on 3c, out of the cooldown's READY, the
 * sprint-in time replacing the cooldown (MW2's sprint check refuses FIRING,
 * 0x578074, and is skipped while the burst is pending). */
static void TestSprintWaits(void) {
	int r, cmd;
	EACH_RATE {
		Rate(rates[r]); Rifle(); Pull();
		forward = 127;
		for (cmd = 1; cmd < 3 * c; cmd++) {
			Frame(BUTTON_SPRINT, 0); InBurst(cmd);
			CHECK(PS->pm_flags & PMF_SPRINTING);
		}
		Frame(BUTTON_SPRINT, 0);	/* 3c */
		CHECK(PS->weaponstate == WEAPON_SPRINT_IN && PS->weaponTime == BG_WeaponSprintInTime(WP_M16));
		CHECK(fired[WP_M16] == 3 && PS->weaponShotCount == 3 && (PS->pm_flags & PMF_SPRINTING));
		forward = 0;
	}
}

/* Frozen (MW2's PMF_FROZEN): the hand's step returns before it asks whether
 * to fire (0x579505), so a burst waits where it is, still pending, and goes
 * on when the freeze lifts: the second shot on the first command after it
 * (its time long run out), the third c later. */
static void TestFrozen(void) {
	int r, cmd, v;
	EACH_RATE for (v = 0; v < 2; v++) {	/* the trigger up, or held through the freeze */
		int buttons = v ? BUTTON_ATTACK : 0;
		Rate(rates[r]); Rifle(); Pull();
		PS->pm_flags |= PMF_FROZEN;
		for (cmd = 1; cmd <= 3 * c + cool; cmd++) {
			Frame(buttons, 0);
			CHECK(!raised[EV_FIRE_WEAPON] && PS->weapon == WP_M16 && PS->weaponstate == WEAPON_FIRING);
			CHECK(PS->weaponShotCount == 1 && BG_BurstFirePending(PS) && (PS->pm_flags & PMF_FROZEN));
		}
		PS->pm_flags &= ~PMF_FROZEN;
		Frame(buttons, 0);
		CHECK(firedNow[WP_M16] == 1 && PS->weaponShotCount == 2 && PS->weaponTime == M16_FIRE_MS);
		for (cmd = 1; cmd < c; cmd++) { Frame(buttons, 0); CHECK(!raised[EV_FIRE_WEAPON] && PS->weaponstate == WEAPON_FIRING); }
		Frame(buttons, 0);
		CHECK(firedNow[WP_M16] == 1 && PS->weaponShotCount == 3 && fired[WP_M16] == 3);
		for (cmd = 1; cmd < c; cmd++) { Frame(buttons, 0); CHECK(PS->weaponstate == WEAPON_FIRING); }
		Frame(buttons, 0);
		CooldownBegins();
		CHECK(!Latched());	/* held through a freeze is no fresh press */
	}
}

/* Death: MW2's PM_Weapon zeroes the weapon and returns (0x44C38B-0x44C3A7):
 * no further shot, and neither the count nor the latch is written. */
static void TestDeath(void) {
	int r, cmd;
	EACH_RATE {
		Rate(rates[r]); Rifle(); Pull();
		PS->stats[STAT_HEALTH] = 0; PS->pm_type = PM_DEAD;
		for (cmd = 1; cmd <= C + 3; cmd++) { Frame(0, 0); CHECK(!raised[EV_FIRE_WEAPON] && PS->weapon == WP_NONE); }
		CHECK(fired[WP_M16] == 1 && PS->weaponShotCount == 1);
	}
}

/* (6) The clip holding 2 rounds, or 1, at the press: the burst is over with
 * the clip (MW2's BurstFirePending needs a round, 0x576A8A), so the dry
 * shot's crossing is the cooldown, trigger up or down, and on that same
 * command, out of its READY: with a reserve the auto reload (0x578845-
 * 0x578889), which begins a new pull; with none and another gun, the change
 * cgame's out-of-ammo switch asked for when the last round left (MW2's
 * BG_SwitchWeaponsIfEmpty, 0x576320, and CG_OutOfAmmoChange); with nothing
 * anywhere the gun stays READY in its cooldown, and a held trigger dry-fires
 * when it has run out (the lock needs a round, 0x578AD7) and again every
 * BG_DRY_FIRE_TIME, READY throughout, the count kept until the trigger is up
 * on a crossing. */
static void TestShortClip(void) {
	int r, n, v, cmd, dry, f, period;
	EACH_RATE for (n = 2; n >= 1; n--) for (v = 0; v < 2; v++) {
		int buttons = v ? BUTTON_ATTACK : 0;
		Rate(rates[r]);
		dry = n * c;	/* the dry shot's crossing */
		period = Frames(BG_DRY_FIRE_TIME);

		/* a reserve */
		Rifle(); SetAmmo(0, n, 60, 4, 16);
		Frame(BUTTON_ATTACK, 0);
		for (cmd = 1; cmd < dry; cmd++) {
			Frame(buttons, 0);
			CHECK(firedNow[WP_M16] == (cmd % c == 0) && PS->weaponstate == WEAPON_FIRING);
		}
		CHECK(fired[WP_M16] == n && Rounds() == 0 && PS->weaponShotCount == n && !BG_BurstFirePending(PS));
		Frame(buttons, 0);
		CHECK(PS->weaponstate == WEAPON_RELOADING && PS->weaponAnimTime == 0 && PS->weaponShotCount == 0);
		CHECK(PS->weaponTime == BG_ReloadSegmentTime(PS, PS->weaponAnimSeq) && fired[WP_M16] == n && !noAmmoEvents);

		/* no reserve, the SPAS loaded: the empty switch */
		Rifle(); SetAmmo(0, n, 0, 0, 0);
		Frame(BUTTON_ATTACK, 0);
		for (cmd = 1; cmd < dry; cmd++) {
			Frame(buttons, 0);
			CHECK(firedNow[WP_M16] == (cmd % c == 0) && PS->weapon == WP_M16 && PS->weaponstate == WEAPON_FIRING);
		}
		CHECK(fired[WP_M16] == n && noAmmoEvents == 1 && cg.weaponSelect == WP_SHOTGUN);
		Frame(buttons, 0);
		CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_DROPPING && PS->weaponTime == BG_WeaponDropTime(WP_M16));
		CHECK(PS->weaponShotCount == n);
		for (f = 0; f < 5000 && PS->weapon == WP_M16; f++) Frame(buttons, 0);
		CHECK(f == Frames(BG_WeaponDropTime(WP_M16)) && PS->weapon == WP_SHOTGUN && fired[WP_M16] == n);

		/* nothing anywhere */
		Rifle(); SetAmmo(0, n, 0, 0, 0); SetAmmo(1, 0, 0, 0, 0);
		Frame(BUTTON_ATTACK, 0);
		for (cmd = 1; cmd < dry; cmd++) {
			Frame(buttons, 0);
			CHECK(firedNow[WP_M16] == (cmd % c == 0) && PS->weaponstate == WEAPON_FIRING);
		}
		CHECK(fired[WP_M16] == n && noAmmoEvents == 1);
		Frame(buttons, 0);
		CooldownBegins();
		CHECK(PS->weaponShotCount == n);
		for (cmd = 1; cmd < cool; cmd++) {
			Frame(buttons, 0);
			CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime > 0 && !raised[EV_NOAMMO]);
		}
		Frame(buttons, 0);	/* the cooldown runs out */
		if (!v) {
			CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 0 && PS->weaponShotCount == 0 && noAmmoEvents == 1);
			Idle(period + 2);
			CHECK(noAmmoEvents == 1 && fired[WP_M16] == n);
			continue;
		}
		CHECK(raised[EV_NOAMMO] == 1 && PS->weaponstate == WEAPON_READY && PS->weaponTime == BG_DRY_FIRE_TIME);
		CHECK(PS->weaponShotCount == n);
		for (f = 1; f <= 2 * period; f++) {
			Frame(buttons, 0);
			CHECK(raised[EV_NOAMMO] == (f % period == 0) && !raised[EV_FIRE_WEAPON]);
			CHECK(PS->weaponstate == WEAPON_READY && PS->weaponShotCount == n);
		}
		CHECK(noAmmoEvents == 1 + 3 && fired[WP_M16] == n && PS->weapon == WP_M16);
		Idle(period);
		CHECK(PS->weaponShotCount == 0 && PS->weaponTime == 0 && noAmmoEvents == 1 + 3);
	}
}

/* (7) What comes out of those rules; nothing in the game names any of it.
 * YY inside the cooldown zeroes the gun's time with no crossing (MW2's
 * PM_Weapon_Idle, 0x5777A0), so the count stays 3: the next pull fires one
 * round (the count goes to 4, still at the limit), its press, which that shot
 * did not clear (the count was not 0, 0x576FA9), stays latched, and held on
 * it starts a whole burst when that round's cooldown runs out. A trigger held
 * through the YY fires the one round on the YY command, and with no fresh
 * press is locked after its cooldown. Read from MW2's code, not watched in a
 * running MW2. */
static void TestYYInTheCooldown(void) {
	int r, cmd;
	EACH_RATE {
		Rate(rates[r]); Rifle(); Pull(); RestOfBurst(0);
		Frame(0, IN_Y);	/* 3c */
		CooldownBegins();
		Frame(0, IN_Y);	/* this command names the SPAS: the drop begins; the next names the rifle */
		CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_DROPPING);
		Frame(0, 0);
		CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_READY && PS->weaponTime == 0 && PS->weaponShotCount == 3);
		Frame(BUTTON_ATTACK, 0);
		CHECK(firedNow[WP_M16] == 1 && PS->weaponShotCount == BG_SHOT_COUNT_MAX && Latched() && !BG_BurstFirePending(PS));
		for (cmd = 1; cmd < c; cmd++) { Frame(BUTTON_ATTACK, 0); CHECK(!raised[EV_FIRE_WEAPON] && PS->weaponstate == WEAPON_FIRING); }
		Frame(BUTTON_ATTACK, 0);
		CooldownBegins();
		Hold(cool - 1);
		CHECK(fired[WP_M16] == 4 && PS->weaponstate == WEAPON_READY);
		Frame(BUTTON_ATTACK, 0);
		CHECK(firedNow[WP_M16] == 1 && PS->weaponShotCount == 1 && !Latched());
		for (cmd = 1; cmd < 3 * c; cmd++) { Frame(BUTTON_ATTACK, 0); InBurst(cmd); }
		CHECK(fired[WP_M16] == 7);

		Rifle(); Pull(); RestOfBurst(BUTTON_ATTACK);
		Frame(BUTTON_ATTACK, IN_Y);
		CooldownBegins();
		Frame(BUTTON_ATTACK, IN_Y);
		CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_DROPPING && fired[WP_M16] == 3);
		Frame(BUTTON_ATTACK, 0);	/* the YY command */
		CHECK(firedNow[WP_M16] == 1 && PS->weaponShotCount == BG_SHOT_COUNT_MAX && !Latched());
		Hold(c - 1);
		Frame(BUTTON_ATTACK, 0);
		CooldownBegins();
		Hold(cool - 1);
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 1 && fired[WP_M16] == 4);
		Hold(cool);
		CHECK(fired[WP_M16] == 4);
	}
}

/* A count carried from another gun: MW2's count is the hand's, and
 * BurstFirePending reads no weapon state (0x576A70). A Masterkey shell fired
 * and the trigger held until the rifle is the gun in hand (B inside the
 * shot: the alt switch on the shot's last command) leaves the rifle a count
 * of 1, below its limit: a burst is pending through its raise, whose end
 * resets nothing even with the trigger up (0x578B5E), every check is skipped
 * meanwhile, and on the command after the raise ends the rifle fires the two
 * rounds its count leaves, by itself. A sprint begun in the raise does not
 * lower the gun before they have left (the sprint check is one of the
 * skipped ones). Read from MW2's code, not watched in a running MW2. */
static void TestCarriedCount(void) {
	int r, v, cmd, raise, f;
	EACH_RATE for (v = 0; v < 2; v++) {	/* 1: a sprint begun in the raise */
		int buttons = v ? BUTTON_SPRINT : 0;
		Rate(rates[r]);
		SetupClass(WP_M16, WP_SHOTGUN, 0, qtrue);
		CHECK(PS->weapon == WP_MASTERKEY);
		Frame(BUTTON_ATTACK, 0);
		CHECK(fired[WP_MASTERKEY] == 1 && PS->weaponShotCount == 1 && !BG_BurstFirePending(PS));
		Frame(BUTTON_ATTACK, IN_ALT);
		for (f = 0; f < 5000 && PS->weapon != WP_M16; f++) Frame(BUTTON_ATTACK, 0);
		raise = BG_WeaponAltRaiseTime(WP_M16);
		CHECK(f == Frames(PickupNoteTime(WP_MASTERKEY, ASEQ_FIRE, WNOTE_BOLT_OPEN)) - 1);	/* the shot's last command */
		CHECK(PS->weaponstate == WEAPON_RAISING && PS->weaponDraw == WDRAW_ALT && PS->weaponTime == raise);
		CHECK(PS->weaponShotCount == 1 && BG_BurstFirePending(PS));
		if (v) forward = 127;
		for (cmd = 1; cmd < Frames(raise); cmd++) { Frame(buttons, 0); CHECK(PS->weaponstate == WEAPON_RAISING); }
		Frame(buttons, 0);	/* the raise's last command */
		CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_READY && PS->weaponTime == 0);
		CHECK(PS->weaponShotCount == 1 && !fired[WP_M16] && BG_BurstFirePending(PS));
		if (v) CHECK(PS->pm_flags & PMF_SPRINTING);
		Frame(buttons, 0);
		CHECK(firedNow[WP_M16] == 1 && PS->weaponstate == WEAPON_FIRING && PS->weaponShotCount == 2);
		for (cmd = 1; cmd < c; cmd++) { Frame(buttons, 0); CHECK(!raised[EV_FIRE_WEAPON] && PS->weaponstate == WEAPON_FIRING); }
		Frame(buttons, 0);
		CHECK(firedNow[WP_M16] == 1 && PS->weaponShotCount == 3 && fired[WP_M16] == 2);
		for (cmd = 1; cmd < c; cmd++) { Frame(buttons, 0); CHECK(PS->weaponstate == WEAPON_FIRING); }
		Frame(buttons, 0);
		if (v) CHECK(PS->weaponstate == WEAPON_SPRINT_IN && PS->weaponTime == BG_WeaponSprintInTime(WP_M16));
		else CooldownBegins();
		Idle(cool);
		CHECK(fired[WP_M16] == 2);
		forward = 0;
	}
}

/* A ladder grabbed inside the burst (MW2's ladder holster is its change check's
 * ladder branch, 0x578C72, skipped with the rest while a burst is pending,
 * 0x44C444-0x44C4B3): the burst plays out on the ladder, its shots on c and
 * 2c, and the drop begins on the think the last shot's time runs out, the
 * cooldown's first: MW2's change gate refuses the shot while it has time left
 * (0x578C31) and takes the cooldown's READY (0x578C0A-0x578C47). The count
 * and the pending pull are not the ladder's to write (section 3 of the
 * burst's table: kept). The trigger held on the ladder, the hang and the one
 * round on the jump off are ladder_test.c's (TestHeldTriggerBurst). */
static void TestLadder(void) {
	int r, cmd, began;
	EACH_RATE {
		vec3_t mins, maxs;
		Rate(rates[r]); Rifle(); Pull();
		/* at the face (within the ladder's reach of it), walking into it and up it */
		BG_PlayerBox(PS, mins, maxs);
		PS->origin[0] = LADDER_WALL_X - maxs[0] - BG_LADDER_REACH / 2;
		ladderWall = qtrue; forward = 127;
		began = 0;
		for (cmd = 1; cmd <= 3 * c; cmd++) {
			Frame(0, 0);
			if (cmd <= 2 * c) {
				InBurst(cmd);
				CHECK(!(PS->stats[STAT_LADDER] & LADDER_HOLSTER));
			}
			CHECK(PS->stats[STAT_LADDER] & LADDER_ATTACHED);
			if (!began && (PS->stats[STAT_LADDER] & LADDER_HOLSTER)) {
				began = cmd;
				CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_DROPPING && PS->weaponShotCount == 3);
			}
		}
		CHECK(began == 3 * c && fired[WP_M16] == 3 && Rounds() == mag - 3 && !Latched());
		for (cmd = 0; cmd < 5000 && PS->weaponstate != WEAPON_LADDER; cmd++) Frame(0, 0);
		CHECK(PS->weaponstate == WEAPON_LADDER && fired[WP_M16] == 3 && !Latched());
		ladderWall = qfalse; forward = 0;
	}
}

/* X, the server's use/reload button (dev/FAKE_RELOAD.md has the whole of what
 * comes of this over a network). Its request is a bit the server's use code
 * writes after a command (MW2's Player_UpdateActivate, 0x4D78F0) and the
 * weapon's reload check reads and clears (0x5786E3-0x5786F6). MW2 runs no
 * reload check while a burst is pending (0x44C444-0x44C4B3), so Pmove neither
 * reads nor clears the bit then.
 *  - The server: the use code clears the bit after the first command X is up
 *    on. X let go by the last shot's command is nothing at all; X still down
 *    on it is a reload on the command after, out of the shot, the cooldown
 *    never happening.
 *  - A client's replay has no use code: a bit a snapshot brought rides every
 *    pending command untouched and is taken on the command after the last
 *    shot's, where the reload begins (PM_BeginWeaponReload takes FIRING,
 *    0x575E92, and starts a new pull, 0x575EFE). The pending pull is not the
 *    reload's to clear. */
static void TestUseReloadInTheBurst(void) {
	int r, cmd, v;
	EACH_RATE {
		Rate(rates[r]);
		/* the server, X down on commands 1 to 2c - 1: gone before the check runs again */
		Rifle(); Pull();
		for (cmd = 1; cmd < 3 * c; cmd++) {
			Frame(cmd < 2 * c ? BUTTON_USE_RELOAD : 0, 0);
			InBurst(cmd);
			CHECK(!(PS->weapFlags & PWF_USE_RELOAD) == (cmd >= 2 * c));	/* set after every command X is down on */
		}
		Frame(0, 0);
		CooldownBegins();
		Idle(cool + 2);
		CHECK(PS->weaponstate == WEAPON_READY && Rounds() == mag - 3 && fired[WP_M16] == 3);
		/* the server, X still down on the last shot's command */
		Rifle(); Pull();
		for (cmd = 1; cmd <= 2 * c; cmd++) { Frame(BUTTON_USE_RELOAD, 0); InBurst(cmd); CHECK(PS->weapFlags & PWF_USE_RELOAD); }
		Frame(0, 0);	/* 2c + 1 */
		CHECK(PS->weaponstate == WEAPON_RELOADING && PS->weaponAnimTime == 0 && PS->weaponShotCount == 0);
		CHECK(fired[WP_M16] == 3 && Rounds() == mag - 3 && !(PS->weapFlags & PWF_USE_RELOAD));
		/* a client's replay over a snapshot's bit, the trigger let go or held,
		   and with a fresh press inside the burst, which stays latched */
		for (v = 0; v < 3; v++) {
			Rifle(); Pull();
			clientReplay = qtrue;
			PS->weapFlags |= PWF_USE_RELOAD;
			for (cmd = 1; cmd <= 2 * c; cmd++) {
				Frame(v == 1 || (v == 2 && cmd > 1) ? BUTTON_ATTACK : 0, 0);
				InBurst(cmd);
				CHECK(PS->weapFlags & PWF_USE_RELOAD);
			}
			Frame(v ? BUTTON_ATTACK : 0, 0);	/* 2c + 1 */
			CHECK(PS->weaponstate == WEAPON_RELOADING && PS->weaponAnimTime == 0 && PS->weaponShotCount == 0);
			CHECK(!(PS->weapFlags & PWF_USE_RELOAD) && fired[WP_M16] == 3 && Rounds() == mag - 3);
			CHECK(Latched() == (v == 2));
			clientReplay = qfalse;
		}
	}
}

/* The rifle's other numbers that pmove stamps are m16_shotgun_mp's as well,
 * each from its generated data (weapon.json): the reserve it is given with
 * (MW2's startAmmo fills the magazine first, so startAmmo less a magazine,
 * below maxAmmo, the most the reserve holds), its first raise (firstRaiseTime;
 * every later raise the ordinary raiseTime), its reload (reloadTime, the
 * magazine in at reloadAddTime) and the sprint's lower and rise (sprintInTime,
 * sprintOutTime). */
static void TestRifleData(void) {
	int r, f;
	bg_weaponInstance_t gun;
	EACH_RATE {
		Rate(rates[r]);
		SetupClass(WP_SHOTGUN, WP_M16, 0, qfalse);
		BG_GetSlot(PS, 1, &gun);
		CHECK(gun.weapon == WP_M16 && gun.ammo == M16_MAG_SIZE && gun.reserve == M16_START_AMMO - M16_MAG_SIZE);
		CHECK(gun.reserve > 0 && gun.reserve < M16_RESERVE && BG_WeaponMaxReserve(WP_M16) == M16_RESERVE);
		CHECK(BG_WeaponStartReserve(WP_M16) == gun.reserve && gun.altReserve == BG_WeaponMaxReserve(WP_MASTERKEY));
		/* the first raise, then an ordinary one */
		PS->slotFlags[1] |= BG_GUN_FIRST_DRAW;
		Frame(0, IN_Y);
		for (f = 0; f < 5000 && PS->weapon != WP_M16; f++) Frame(0, 0);
		CHECK(PS->weapon == WP_M16 && PS->weaponstate == WEAPON_RAISING && PS->weaponDraw == WDRAW_FIRST);
		CHECK(PS->weaponTime == M16_FIRST_RAISE_MS && M16_FIRST_RAISE_MS != M16_RAISE_MS);
		for (f = 0; f < 5000 && PS->weaponstate == WEAPON_RAISING; f++) Frame(0, 0);
		CHECK(f == Frames(M16_FIRST_RAISE_MS) && PS->weaponstate == WEAPON_READY);
		Frame(0, IN_Y);
		for (f = 0; f < 5000 && PS->weapon != WP_SHOTGUN; f++) Frame(0, 0);
		for (f = 0; f < 5000 && PS->weaponstate != WEAPON_READY; f++) Frame(0, 0);
		Frame(0, IN_Y);
		for (f = 0; f < 5000 && PS->weapon != WP_M16; f++) Frame(0, 0);
		CHECK(PS->weaponstate == WEAPON_RAISING && PS->weaponDraw == WDRAW_NORMAL && PS->weaponTime == M16_RAISE_MS);

		/* the reload */
		Rifle(); SetAmmo(0, 7, 60, 4, 16);
		Frame(BUTTON_RELOAD, 0);
		/* the reload's clock runs from the press's command: the magazine is in
		   Frames(reloadAddTime) commands after it, the gun ready Frames(reloadTime) after it */
		CHECK(PS->weaponstate == WEAPON_RELOADING && PS->weaponAnimTime == 0);
		CHECK(BG_ReloadSegmentTime(PS, RSEQ_LOOP) == M16_RELOAD_MS && BG_ReloadSegmentTime(PS, RSEQ_START) == 0);
		CHECK(BG_WeaponReloadTime(WP_M16) == M16_RELOAD_MS && M16_MAG_IN_MS < M16_RELOAD_MS);
		for (f = 1; f < Frames(M16_MAG_IN_MS); f++) { Frame(0, 0); CHECK(Rounds() == 7 && PS->weaponstate == WEAPON_RELOADING); }
		Frame(0, 0);
		CHECK(Rounds() == mag && PS->ammoReserve[WP_M16] == 60 - (mag - 7) && PS->weaponstate == WEAPON_RELOADING);
		for (f = Frames(M16_MAG_IN_MS) + 1; f < Frames(M16_RELOAD_MS); f++) { Frame(0, 0); CHECK(PS->weaponstate == WEAPON_RELOADING); }
		Frame(0, 0);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 0);

		/* the sprint */
		Rifle();
		forward = 127;
		Frame(BUTTON_SPRINT, 0);
		for (f = 0; f < 5000 && PS->weaponstate != WEAPON_SPRINT_IN; f++) Frame(BUTTON_SPRINT, 0);
		CHECK(PS->weaponstate == WEAPON_SPRINT_IN && PS->weaponTime == M16_SPRINT_IN_MS);
		CHECK(BG_WeaponSprintInTime(WP_M16) == M16_SPRINT_IN_MS && BG_WeaponSprintOutTime(WP_M16) == M16_SPRINT_OUT_MS);
		forward = 0;
	}
}

/* A trigger worked by a program, as the game's bots work theirs
 * (BotPullTrigger, ai_dmq3.c, through the one shared rule,
 * BG_TriggerNeedsRelease): pressed on every think but one that finds the
 * trigger down from the last think with the pull's shots all fired, which
 * lets it up for that think. A bot thinks every 100ms over 50ms server
 * commands; so driven, the burst rifle fires burst after burst and every
 * Single Shot gun shot after shot, where a trigger simply held fires one
 * pull's worth and no more; a full-auto gun is never let go. */
static void TestBotTrigger(void) {
	static const int guns[] = { WP_M16, WP_MASTERKEY, WP_SHOTGUN, WP_M40A3, WP_MACHINEGUN };
	int g, f, v;
	msec = 50;
	for (g = 0; g < ARRAY_LEN(guns); g++) for (v = 0; v < 2; v++) {	/* 1: simply held */
		int gun = guns[g], held = 0, thinkEvery = 2, n = 400, pull = 1, releases = 0;
		int type = BG_WeaponFireType(gun);
		while (type != BG_FIRE_FULLAUTO && !BG_ShotLimitReached(type, pull)) pull++;
		if (gun == WP_M16 || gun == WP_MASTERKEY) SetupClass(WP_M16, WP_SHOTGUN, 0, gun == WP_MASTERKEY);
		else SetupClass(gun, gun == WP_SHOTGUN ? WP_MACHINEGUN : WP_SHOTGUN, 0, qfalse);
		SetAmmo(0, BG_WeaponMagSize(WP_M16), 0, BG_WeaponMagSize(WP_MASTERKEY), 0);	/* no reload comes into it */
		if (gun != WP_M16 && gun != WP_MASTERKEY) SetAmmo(0, BG_WeaponMagSize(gun), 0, 0, 0);
		CHECK(PS->weapon == gun);
		for (f = 0; f < n && PS->ammo[gun] > 0; f++) {
			if (f % thinkEvery == 0) {
				qboolean release = !v && BG_TriggerNeedsRelease(PS, held);
				releases += release;
				held = !release;
			}
			Frame(held ? BUTTON_ATTACK : 0, 0);
		}
		if (type == BG_FIRE_FULLAUTO) {
			CHECK(!releases && fired[gun] == BG_WeaponMagSize(gun));
			CHECK(!BG_TriggerNeedsRelease(PS, qtrue));
		} else if (v) {
			CHECK(fired[gun] == pull && f == n);	/* held: one pull's shots, and the lock */
		} else {
			CHECK(fired[gun] == BG_WeaponMagSize(gun) && releases >= fired[gun] / pull - 1);	/* the whole magazine */
		}
	}
	/* A trigger that is up is pressed whatever the count says: a pull ends
	 * only on a think a state's time runs out with the trigger up, and a gun
	 * at rest has no such think. The rifle after YY inside its cooldown is
	 * READY with no time and its count still at the limit (above): the
	 * program's next think presses, and the rifle fires. */
	msec = rates[0]; Rate(msec);
	Rifle(); Pull(); RestOfBurst(0);
	Frame(0, IN_Y); CooldownBegins();
	Frame(0, IN_Y); Frame(0, 0);
	CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 0 && BG_ShotLimitReached(BG_WeaponFireType(WP_M16), PS->weaponShotCount));
	CHECK(!BG_TriggerNeedsRelease(PS, qfalse) && BG_TriggerNeedsRelease(PS, qtrue));
	Frame(BG_TriggerNeedsRelease(PS, qfalse) ? 0 : BUTTON_ATTACK, 0);
	CHECK(firedNow[WP_M16] == 1 && fired[WP_M16] == 4);
}

/* The G19 beside the burst. Its Select Fire makes its fire type its slot's
 * committed mode (BG_SlotFireType: semi is the weapon's own Single Shot, auto
 * is Full Auto), and neither is one of MW2's burst types (0x576AE9-0x576AF1),
 * so nothing of the burst is the G19's: it latches no pending pull, has no
 * cooldown and is never pending (BG_BurstFirePending), in either mode. What
 * it meets of the burst is what MW2's rules make of a pull left over from a
 * burst gun: the pending pull is read for every fire type (0x5789DD) and
 * cleared only by a pull's first counted shot (0x576FB0; a full-auto shot
 * counts nothing, 0x576F93).
 *
 * Each case starts with the fitted G19 in hand and goes through the real
 * Pmove: the selector for the mode, Y for the rifle, the rifle's burst, Y
 * back. Every time is the G19's generated data (bg_g19_data.h). */
static void Pistol(void) {
	classFitted = BG_ATTACHMENT_SWITCH;
	SetupClass(WP_G19, WP_M16, 0, qfalse);
	classFitted = 0;
	CHECK(PS->weapon == WP_G19 && BG_HasSelector(PS, WP_G19) && !(PS->slotFlags[0] & BG_GUN_AUTO));
	CHECK(BG_WeaponFireType(WP_G19) == BG_FIRE_SINGLESHOT && BG_SlotFireType(PS, WP_G19) == BG_FIRE_SINGLESHOT);
	CHECK(BG_WeaponFireLength(WP_G19) == 0 && !BG_BurstFirePending(PS) && !Latched());
}
/* Y, and on until the other gun is up: READY on its raise's last command. */
static void SwapTo(int weapon, int buttons) {
	int f;
	Frame(buttons, IN_Y);
	for (f = 0; f < 5000 && !(PS->weapon == weapon && PS->weaponstate == WEAPON_READY); f++) {
		Frame(buttons, 0);
		CHECK(!firedNow[WP_G19]);
	}
	CHECK(f < 5000 && PS->weaponTime == 0);
}
/* The selector, from the G19 at rest: the press starts the switch, the click
 * commits the mode G19_CLICK_MS in (the note's command) and raises its event
 * there, and the state ends G19_SWITCH_MS after the press. Nothing fires, and
 * no burst is ever pending. */
static void SwitchMode(qboolean toAuto) {
	int k, click = Frames(G19_CLICK_MS), end = Frames(G19_SWITCH_MS);
	const int *flags = &PS->slotFlags[PS->stats[STAT_ACTIVE_SLOT]];
	CHECK(PS->weapon == WP_G19 && PS->weaponstate == WEAPON_READY && BG_HasSelector(PS, WP_G19));
	CHECK(!!(*flags & BG_GUN_AUTO) == !toAuto && click >= 2 && end > click);
	Frame(BUTTON_SELECTFIRE, 0);
	CHECK(PS->weaponstate == WEAPON_SELECTING && PS->weaponTime == G19_SWITCH_MS && PS->weaponAnimTime == 0);
	CHECK(PS->weaponAnimSeq == (toAuto ? ASEQ_SELECT_AUTO : ASEQ_SELECT_SEMI) && (PS->weapFlags & PWF_SELECTOR_HELD));
	CHECK(!raised[EV_WEAPON_NOTETRACK] && !!(*flags & BG_GUN_AUTO) == !toAuto);
	for (k = 1; k <= end; k++) {
		Frame(0, 0);
		CHECK(!(PS->weapFlags & PWF_SELECTOR_HELD) && raised[EV_WEAPON_NOTETRACK] == (k == click));
		CHECK(!!(*flags & BG_GUN_AUTO) == (toAuto ? k >= click : k < click));
		CHECK(PS->weaponstate == (k < end ? WEAPON_SELECTING : WEAPON_READY));
		if (k < end) CHECK(PS->weaponAnimTime == k * msec);
		CHECK(!BG_BurstFirePending(PS) && !raised[EV_FIRE_WEAPON] && PS->weapon == WP_G19);
	}
	CHECK(PS->weaponAnimTime == -1 && PS->weaponTime == 0);
	CHECK(BG_SlotFireType(PS, WP_G19) == (toAuto ? BG_FIRE_FULLAUTO : BG_FIRE_SINGLESHOT));
}
/* Semi: one shot a pull. A tap; a pull held (the lock, 0x5789DD-0x578AE6);
 * and a second press inside the shot's time, held, which is no pending pull
 * for a gun that is no burst gun (0x576AE9) and so is locked as well. A
 * pending pull left over from the rifle (stale) is cleared by the first of
 * these shots. Three shots. */
static void SemiPulls(qboolean stale) {
	int k, shot = Frames(G19_FIRE_MS), before = fired[WP_G19];
	CHECK(shot >= 3 && PS->weaponstate == WEAPON_READY && PS->weaponShotCount == 0 && Latched() == stale);
	Frame(BUTTON_ATTACK, 0);
	CHECK(firedNow[WP_G19] == 1 && PS->weaponstate == WEAPON_FIRING && PS->weaponTime == G19_FIRE_MS);
	CHECK(PS->weaponShotCount == 1 && !Latched() && !BG_BurstFirePending(PS));
	for (k = 1; k < shot; k++) { Frame(0, 0); CHECK(!raised[EV_FIRE_WEAPON] && PS->weaponTime > 0 && !BG_BurstFirePending(PS)); }
	Frame(0, 0);
	CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 0 && PS->weaponShotCount == 0 && fired[WP_G19] == before + 1);

	Frame(BUTTON_ATTACK, 0);
	CHECK(firedNow[WP_G19] == 1 && PS->weaponShotCount == 1 && !BG_TriggerNeedsRelease(PS, qfalse) && BG_TriggerNeedsRelease(PS, qtrue));
	for (k = 1; k < shot; k++) { Frame(BUTTON_ATTACK, 0); CHECK(!raised[EV_FIRE_WEAPON] && PS->weaponstate == WEAPON_FIRING); }
	for (k = 0; k < 3 * shot; k++) {
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 1 && PS->weaponShotCount == 1);
		CHECK(fired[WP_G19] == before + 2 && !Latched() && !BG_BurstFirePending(PS));
	}
	Frame(0, 0);
	CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 0 && PS->weaponShotCount == 0);

	Frame(BUTTON_ATTACK, 0);
	CHECK(firedNow[WP_G19] == 1 && fired[WP_G19] == before + 3);
	Frame(0, 0);
	for (k = 2; k < shot; k++) { Frame(BUTTON_ATTACK, 0); CHECK(!Latched() && !raised[EV_FIRE_WEAPON] && PS->weaponstate == WEAPON_FIRING); }
	for (k = 0; k < 2 * shot; k++) {
		Frame(BUTTON_ATTACK, 0);
		CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 1 && fired[WP_G19] == before + 3 && !Latched());
	}
	Frame(0, 0);
	CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 0 && PS->weaponShotCount == 0);
}
/* Auto: held, a shot every fire time, none counted (so a pending pull left
 * over from the rifle stays as it was) and a trigger worked by a program is
 * never let go. `count` is the shot count the gun was handed. Three shots. */
static void AutoFire(qboolean stale, int count) {
	int k, shot = Frames(G19_FIRE_MS), before = fired[WP_G19];
	CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 0 && PS->weaponShotCount == count && Latched() == stale);
	for (k = 0; k < 3 * shot; k++) {
		Frame(BUTTON_ATTACK, 0);
		CHECK(firedNow[WP_G19] == (k % shot == 0) && fired[WP_G19] == before + 1 + k / shot);
		CHECK(PS->weaponstate == WEAPON_FIRING && Latched() == stale && !BG_BurstFirePending(PS));
		CHECK(!BG_TriggerNeedsRelease(PS, qtrue) && PS->weaponShotCount == (stale ? 0 : count));
		if (firedNow[WP_G19]) CHECK(PS->weaponTime == G19_FIRE_MS);
	}
	for (k = 0; k < shot; k++) Frame(0, 0);
	CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 0 && PS->weaponShotCount == 0 && fired[WP_G19] == before + 3);
}
static playerState_t g19Trace[2][TRACE_MAX];
static void TestG19(void) {
	int r, stale, mode, k, f, len[2];
	EACH_RATE {
		int shot;
		Rate(rates[r]);
		shot = Frames(G19_FIRE_MS);
		/* (a) A pending pull left over from the rifle, the trigger up when
		 * the G19 arrives: a second press inside the rifle's burst, let go
		 * before the cooldown runs out, stays latched (MW2 leaves it for the
		 * next pull's first shot). The G19 then does, command for command,
		 * what it does with no pending pull at all: the mode switch, auto
		 * (which leaves the pull pending), back to semi (whose first shot
		 * clears it), auto again. Every command's whole playerstate is
		 * compared, the pending pull's own bit aside. */
		for (stale = 0; stale < 2; stale++) {
			Pistol();
			SwapTo(WP_M16, 0);
			Frame(BUTTON_ATTACK, 0);
			CHECK(fired[WP_M16] == 1 && PS->weaponShotCount == 1);
			Frame(0, 0);
			Frame(stale ? BUTTON_ATTACK : 0, 0);	/* command 2, inside the burst */
			CHECK(Latched() == stale && BG_BurstFirePending(PS));
			Idle(C - 2);
			CHECK(fired[WP_M16] == 3 && PS->weaponstate == WEAPON_READY && PS->weaponTime == 0 && PS->weaponShotCount == 0);
			CHECK(Latched() == stale);
			SwapTo(WP_G19, 0);
			CHECK(Latched() == stale && !BG_BurstFirePending(PS) && PS->weaponShotCount == 0);
			traceTo = g19Trace[stale]; traceLen = 0;
			SwitchMode(qtrue);
			AutoFire(stale, 0);
			SwitchMode(qfalse);
			SemiPulls(stale);
			SwitchMode(qtrue);
			AutoFire(qfalse, 0);
			SwitchMode(qfalse);
			SemiPulls(qfalse);
			len[stale] = traceLen; traceTo = NULL;
			CHECK(fired[WP_G19] == 12 && fired[WP_G19] < G19_MAG_SIZE && fired[WP_M16] == 3);	/* no reload comes into it */
		}
		CHECK(len[0] == len[1] && len[0] > 4 * Frames(G19_SWITCH_MS));
		for (f = 0; f < len[0]; f++) {
			playerState_t with = g19Trace[1][f];
			CHECK(!(g19Trace[0][f].weapFlags & PWF_PENDING_PULL));
			with.weapFlags &= ~PWF_PENDING_PULL;
			CHECK(!memcmp(&with, &g19Trace[0][f], sizeof(with)));
		}

		/* (b) The trigger held through the swap, in each mode. Held since the
		 * rifle's first shot (no pending pull), the pull is spent and the
		 * count comes with the hand: semi, the G19's raise hangs under the
		 * lock like any spent pull's, until the trigger is let go; auto is
		 * never locked and fires the command after the raise. Pressed again
		 * inside the burst and held from there (a pending pull), the swap's
		 * drop ends that pull (0x578B55-0x578B67): semi, the pull it latched
		 * is one shot on the command after the raise, and then the lock; auto
		 * fires as auto and leaves the pull pending. The rifle's burst is
		 * never cut by the Y inside it. */
		for (mode = 0; mode < 2; mode++) for (stale = 0; stale < 2; stale++) {
			int raise;
			Pistol();
			if (mode) SwitchMode(qtrue);
			SwapTo(WP_M16, 0);
			Frame(BUTTON_ATTACK, 0);
			Frame(stale ? 0 : BUTTON_ATTACK, 0);
			Frame(BUTTON_ATTACK, IN_Y);	/* command 2 */
			CHECK(Latched() == stale && fired[WP_M16] == 1 + 2 / c);
			for (f = 0; f < 5000 && PS->weapon != WP_G19; f++) Frame(BUTTON_ATTACK, 0);
			raise = PS->weaponTime;
			CHECK(f < 5000 && fired[WP_M16] == 3 && PS->weaponstate == WEAPON_RAISING && raise == BG_WeaponDrawTime(PS));
			CHECK(Latched() == stale && PS->weaponShotCount == (stale ? 0 : 3) && !BG_BurstFirePending(PS));
			CHECK(!!(PS->slotFlags[0] & BG_GUN_AUTO) == mode);
			for (k = 1; k < Frames(raise); k++) { Frame(BUTTON_ATTACK, 0); CHECK(PS->weaponstate == WEAPON_RAISING && PS->weaponTime > 1); }
			if (!mode && !stale) {
				for (k = 0; k < Frames(600); k++) {
					Frame(BUTTON_ATTACK, 0);
					CHECK(PS->weaponstate == WEAPON_RAISING && PS->weaponTime == 1 && !fired[WP_G19] && PS->weaponShotCount == 3);
				}
				Frame(0, 0);
				CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 0 && PS->weaponShotCount == 0 && !fired[WP_G19]);
				SemiPulls(qfalse);
				continue;
			}
			Frame(BUTTON_ATTACK, 0);	/* the raise's last command */
			CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 0 && !fired[WP_G19] && Latched() == stale);
			if (mode) {
				AutoFire(stale, stale ? 0 : 3);
				continue;
			}
			Frame(BUTTON_ATTACK, 0);
			CHECK(firedNow[WP_G19] == 1 && PS->weaponShotCount == 1 && !Latched() && PS->weaponTime == G19_FIRE_MS);
			for (k = 1; k < shot; k++) { Frame(BUTTON_ATTACK, 0); CHECK(!raised[EV_FIRE_WEAPON] && PS->weaponstate == WEAPON_FIRING); }
			for (k = 0; k < 3 * shot; k++) {
				Frame(BUTTON_ATTACK, 0);
				CHECK(PS->weaponstate == WEAPON_READY && PS->weaponTime == 1 && fired[WP_G19] == 1 && !Latched());
			}
			Frame(0, 0);
			SemiPulls(qfalse);
		}
	}
}

int main(void) {
	/* MW2's own number, the one thing here that is no table of Peace's: the
	 * dvar player_burstFireCooldown's default, 0.2s (registered at 0x44921B) */
	CHECK(BG_BURST_FIRE_COOLDOWN == 200);
	TestTap();
	TestHeld();
	TestPressInTheCooldown();
	TestSecondPressInTheBurst();
	TestSwapWaits();
	TestAltSwitchWaits();
	TestReloadWaits();
	TestKnifeAndFragWait();
	TestKnifeEnd();
	TestSprintWaits();
	TestFrozen();
	TestDeath();
	TestShortClip();
	TestYYInTheCooldown();
	TestCarriedCount();
	TestLadder();
	TestUseReloadInTheBurst();
	TestRifleData();
	TestBotTrigger();
	TestG19();
	printf("PASS: %i burst checks at 8, 16 and 33ms commands\n", checks);
	return 0;
}
