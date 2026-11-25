/* The G19's select-fire switch is worked as a bolt is (the owner's rule,
 * "Switch NAC", Peace's own: MW2 has no selector). The SPAS's pump, the M300's
 * bolt and the G19's switch, side by side, through the same inputs: the real
 * client selection (cg_weapons.c), the real full Pmove and the server's real
 * use/reload step (BG_UpdateActivate), in moni_test.c's frame order, at 8, 16
 * and 33ms commands. A Y pressed on a frame is first seen by pmove on the next
 * command.
 *
 * Every answer is a command counted from the action's own landmarks, read off
 * a run with nothing else pressed: S its first command (the pump's or the
 * bolt's READY think after the shot, where step 4d starts it; the selector's
 * own command), C the command its close note crosses (the bolt or the pump
 * paid; the click, the mode committed) and E the command it ends READY on.
 * The three guns must give the same answers, and the answers must be the
 * bolt's rule. Where the switch differs from the bolt on purpose, it is the
 * owner's rule (e): an interrupted switch is lost, never owed, so nothing of
 * it is worked again when the gun comes back.
 *
 * The test only presses buttons; every time is read from the game's tables,
 * and nothing in the game names what it finds. */
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
static int forward;	/* the move a command carries (a sprint needs forward) */
static int frozen;	/* the server freezes the player on this command */

typedef struct { int weapon, attachment; const char *name; } actionGun_t;
static const actionGun_t guns[] = {
	{ WP_SHOTGUN, 0, "SPAS pump" },
	{ WP_M40A3, 0, "M300 bolt" },
	{ WP_G19, BG_ATTACHMENT_SWITCH, "G19 switch" }
};
#define GUNS ((int)(sizeof(guns) / sizeof(guns[0])))
static const actionGun_t *G;	/* the gun under test, in slot 0 */
static const char *where = "";	/* the case under test, for a failure */

#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d (%s, %d ms commands, %s): %s\n", \
	__LINE__, G ? G->name : "-", msec, where, #x); exit(1); } } while (0)
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
#define OTHER WP_M16	/* slot 1: the gun Y goes to; weapalt goes to its Masterkey */
#define IN_Y    1
#define IN_ALT  2

static int startMode;
static int perk;	/* the class's first perk: Sleight of Hand times the reload clock, never the actions' */
static int Mode(void) { return ( PS->slotFlags[0] & BG_GUN_AUTO ) != 0; }
static int Owes(void) {
	bg_weaponInstance_t gun;
	BG_GetSlot(PS, 0, &gun);
	return ( gun.flags & BG_GUN_UNCHAMBERED ) != 0;
}
/* What the action closes is committed: the pump or bolt paid, the switch's mode. */
static int Closed(void) { return G->attachment ? Mode() != startMode : !Owes(); }

/* The gun in slot 0, the M16 in slot 1, standing on the floor. The G19 starts
 * a round down, as the pump's and the bolt's shot leaves them, so a reload is
 * open to all three. */
static void Setup(void) {
	bg_class_t cl = *BG_Class(0);
	playerState_t *ps = &snapshot.ps;
	memset(&cg, 0, sizeof(cg)); memset(&snapshot, 0, sizeof(snapshot));
	memset(&move, 0, sizeof(move)); memset(&use, 0, sizeof(use));
	cl.slot[0] = G->weapon; cl.slot[1] = OTHER;
	cl.attachments[0] = G->attachment; cl.attachments[1] = 0;
	cl.lethal = WP_FRAG; cl.lethalCount = BG_ClassEquipmentCount(WP_FRAG);
	cl.perk[0] = perk;
	CHECK(BG_ValidateClass(&cl));
	BG_ApplyClass(ps, &cl);
	BG_SpawnStance(ps);
	ps->stats[STAT_HEALTH] = 100;
	ps->pm_type = PM_NORMAL; ps->speed = BG_PLAYER_SPEED; ps->gravity = 800;
	ps->origin[2] = 24; ps->groundEntityNum = ENTITYNUM_WORLD; ps->commandTime = 1000;
	ps->slotFlags[0] &= ~BG_GUN_FIRST_DRAW; ps->slotFlags[1] &= ~BG_GUN_FIRST_DRAW;
	BG_SelectSlot(ps, 0);
	ps->weapon = BG_ActiveSlotWeapon(ps);
	ps->weaponstate = WEAPON_READY; ps->weaponTime = 0; ps->weaponAnimTime = -1; ps->weaponDraw = WDRAW_NORMAL;
	if (G->attachment) ps->ammo[G->weapon]--;
	cg.snap = &snapshot; cg.predictedPlayerState = *ps; cg.time = 1000;
	CG_Respawn();
	move.ps = PS; move.trace = FloorTrace; move.pointcontents = NoContents;
	move.tracemask = MASK_PLAYERSOLID;
	CG_ValidateWeaponSelect();
	cmdValue = BG_WeaponCommand(&snapshot.ps, cg.weaponSelectSlot, cg.weaponSelectAlternate);
	swapQueued = 0; forward = 0; frozen = 0;
	startMode = Mode();
	CHECK(BG_HasSelector(ps, G->weapon) == (G->attachment != 0));
}

typedef struct { int weapon, state, wt, at, seq, closed, ads, sprint, action, fired, notetrack; } rec_t;
#define MAXREC 1024
static rec_t R[MAXREC];
static int nR;

/* One client frame: bound commands, the command, the server's think, the draw. */
static void Frame(int buttons, int keys) {
	int seq;
	rec_t *r;
	if (keys & IN_Y) CG_NextWeapon_f();
	if (keys & IN_ALT) CG_AltWeapon_f();
	memset(&move.cmd, 0, sizeof(move.cmd));
	move.cmd.forwardmove = forward;
	move.cmd.buttons = buttons | (swapQueued ? BUTTON_WEAPON_SWAP : 0);
	move.cmd.weapon = cmdValue;
	move.cmd.serverTime = PS->commandTime + msec;
	swapQueued = 0;
	if (frozen) PS->pm_flags |= PMF_FROZEN; else PS->pm_flags &= ~PMF_FROZEN;
	seq = PS->eventSequence;
	Pmove(&move);
	BG_UpdateActivate(PS, &use, move.cmd.buttons, qfalse, qfalse);
	cg.time += msec;
	snapshot.ps = *PS;	/* own host: the next snapshot is this state */
	CG_ValidateWeaponSelect();
	cmdValue = BG_WeaponCommand(&snapshot.ps, cg.weaponSelectSlot, cg.weaponSelectAlternate);
	CHECK(nR < MAXREC);
	r = &R[nR++];
	memset(r, 0, sizeof(*r));
	r->weapon = PS->weapon; r->state = PS->weaponstate; r->wt = PS->weaponTime; r->at = PS->weaponAnimTime;
	r->seq = PS->weaponAnimSeq; r->closed = Closed();
	r->ads = (PS->pm_flags & PMF_ADS) != 0; r->sprint = (PS->pm_flags & PMF_SPRINTING) != 0;
	r->action = PS->weaponAction & WA_TYPE_MASK;
	for (; seq < PS->eventSequence; seq++) {
		int ev = PS->events[seq & (MAX_PS_EVENTS - 1)] & ~EV_EVENT_BITS;
		if (ev == EV_FIRE_WEAPON && PS->weapon == G->weapon) r->fired++;
		if (ev == EV_WEAPON_NOTETRACK) r->notetrack++;
	}
}

/* Whole commands a timer of that length keeps a gun busy. */
static int Frames(int ms) { return (ms + msec - 1) / msec; }

/* A script: buttons on a command, keys on its frame (a key's selection is
 * first seen on the next command), buttons held from a command on, a sprint's
 * forward from a command on, and a freeze. Command 0 begins the action: the
 * trigger for the pump and the bolt, the selector for the switch. */
typedef struct { int n, buttons, keys; } press_t;
static press_t script[8];
static int nScript, holdFrom, holdButtons, forwardFrom, frozenFrom, frozenTo;
static void Clear(void) { nScript = 0; holdFrom = forwardFrom = -1; holdButtons = 0; frozenFrom = frozenTo = 0; }
static void Press(int n, int buttons, int keys) {
	script[nScript].n = n; script[nScript].buttons = buttons; script[nScript].keys = keys; nScript++;
}
static void Seen(int n, int keys) { Press(n - 1, 0, keys); }	/* first seen on command n */
static void Script(int len) {
	int n, i, pre = 0;
	Setup();
	for (i = 0; i < nScript; i++) if (script[i].n == -1) pre |= script[i].keys;
	if (pre) Frame(0, pre);	/* the frame before command 0 */
	nR = 0;
	for (n = 0; n < len; n++) {
		int b = n == 0 ? ( G->attachment ? BUTTON_SELECTFIRE : BUTTON_ATTACK ) : 0, k = 0;
		if (holdFrom >= 0 && n >= holdFrom) b |= holdButtons;
		for (i = 0; i < nScript; i++) if (script[i].n == n) { b |= script[i].buttons; k |= script[i].keys; }
		forward = forwardFrom >= 0 && n >= forwardFrom ? 127 : 0;
		frozen = n >= frozenFrom && n < frozenTo;
		Frame(b, k);
	}
	frozen = 0;
}
static int First(int from, int weapon, int state) {
	int n;
	for (n = from; n < nR; n++) if ((weapon < 0 || R[n].weapon == weapon) && (state < 0 || R[n].state == state)) return n;
	return -1;
}

/* The landmarks, from a run with nothing else pressed, checked against the
 * tables: the action starts at its open note (the pump, the bolt) or at 0 (the
 * switch), and its close note and its end are on its own clock. */
static int S, C, E, RUN;
static void Landmarks(void) {
	playerState_t ps;
	int n, start, close, length;
	where = "baseline";
	RUN = Frames(4000);
	Clear(); Script(RUN);
	S = C = E = -1;
	for (n = 0; n < nR; n++) {
		if (S < 0 && R[n].state == WEAPON_BOLTING) S = n;
		if (S >= 0 && C < 0 && R[n].closed) C = n;
		if (S >= 0 && E < 0 && R[n].state != WEAPON_BOLTING) { E = n; break; }
	}
	memset(&ps, 0, sizeof(ps)); ps.weapon = G->weapon;
	ps.weaponAnimSeq = G->attachment ? ASEQ_SELECT_AUTO : ASEQ_FIRE;
	start = G->attachment ? 0 : PickupNoteTime(G->weapon, ASEQ_FIRE, WNOTE_BOLT_OPEN);
	close = PickupNoteTime(G->weapon, ps.weaponAnimSeq, WNOTE_BOLT_CLOSED);
	length = BG_ActionLength(&ps);
	CHECK(start >= 0 && close > start && length > close);
	CHECK(S == (G->attachment ? 0 : Frames(start) + 1));
	CHECK(C == S + Frames(close - start) && E == S + Frames(length - start));
	CHECK(S + 3 < C && C + 3 < E);
	/* the close note's event on C alone; nothing closed before it, all of it after */
	for (n = 0; n <= E; n++) {
		CHECK(R[n].notetrack == (n == C) && R[n].closed == (n >= C) && R[n].fired == (n == 0 && !G->attachment));
		if (n >= S) CHECK(R[n].state == (n < E ? WEAPON_BOLTING : WEAPON_READY) && R[n].weapon == G->weapon);
	}
	CHECK(R[E + 1].state == WEAPON_READY);
}

/* Where an input lands, counted from the landmarks. */
enum { AT_S, AT_S1, AT_MID, AT_C1BEFORE, AT_C, AT_CAFTER, AT_E1BEFORE, AT_E, AT_COUNT };
static const char *atName[AT_COUNT] = { "S", "S+1", "mid-way", "C-1", "C", "C+1", "E-1", "E" };
static int At(int at) {
	switch (at) {
	case AT_S: return S;
	case AT_S1: return S + 1;
	case AT_MID: return (S + C) / 2;
	case AT_C1BEFORE: return C - 1;
	case AT_C: return C;
	case AT_CAFTER: return C + 1;
	case AT_E1BEFORE: return E - 1;
	default: return E;
	}
}

#define ANSWERS 8
typedef void (*case_t)(int at, int k, int answer[ANSWERS]);

/* A change seen on command k (Y, or weapalt to the M16's underbarrel, with or
 * without aim held from command 0). Before C, the change is the whole
 * holster, and the action is not closed (the pump owed, the switch lost). On
 * C it is instant: the next gun is up on C, nothing put down, and the action
 * is still not closed (the switch NAC, as a successful NAC does not complete
 * the pump). After C it is the whole holster, the action closed and kept.
 * Aim stays on through the action, on the change's own command too, so the
 * NAC works aimed. */
static int caseAds, caseKeys;
static void Change(int at, int k, int answer[ANSWERS]) {
	int other = caseKeys == IN_ALT ? WP_MASTERKEY : OTHER, drop = BG_WeaponDropTime(G->weapon), n, up;
	Clear(); Seen(k, caseKeys);
	if (caseAds) { holdFrom = 0; holdButtons = BUTTON_ADS; }
	Script(RUN);
	for (n = S; n < k; n++) CHECK(R[n].weapon == G->weapon && R[n].state == WEAPON_BOLTING);
	if (caseAds) for (n = S; n <= k; n++) CHECK(R[n].ads);
	up = First(k, other, -1);
	answer[0] = R[k].weapon == other && R[k].state == WEAPON_RAISING;
	answer[1] = R[k].weapon == G->weapon && R[k].state == WEAPON_DROPPING && R[k].wt == drop;
	answer[2] = up - k - (answer[0] ? 0 : Frames(drop));
	answer[3] = R[up].closed;
	answer[4] = R[nR - 1].weapon == other && R[nR - 1].state == WEAPON_READY && R[nR - 1].closed;
	answer[5] = caseAds ? R[k].ads && !R[k + 1].ads : 0;
	CHECK(answer[0] == (k == C) && answer[1] == (k != C) && answer[2] == 0);
	CHECK(answer[3] == (k > C) && answer[4] == (k > C));
	if (caseAds) CHECK(answer[5]);
	(void)at;
}

/* A sprint pressed on command k. On S it takes the READY gun, before the
 * action begins (the pump owed; the selector's press, on the same command,
 * spent). Inside the action it never lowers the gun: it is on at once, the
 * action runs to its end, and the gun lowers on E+1, the action closed. */
static void Sprint(int at, int k, int answer[ANSWERS]) {
	int n, lowered;
	Clear(); Press(k, BUTTON_SPRINT, 0); forwardFrom = k;
	Script(E + Frames(1000));
	CHECK(R[k].sprint);
	lowered = First(k, G->weapon, WEAPON_SPRINT_IN);
	answer[0] = lowered - (k == S ? S : E + 1);
	answer[1] = R[lowered].closed;
	CHECK(answer[0] == 0 && answer[1] == (k > S));
	if (k > S) for (n = k; n < E; n++) CHECK(R[n].state == WEAPON_BOLTING && R[n].sprint);
	(void)at;
}

/* A sprint on S+1, then a change seen on command k: the change is the one
 * made without the sprint (Change), and the next gun, raised, lowers the
 * command after its raise ends. */
static void SprintSwap(int at, int k, int answer[ANSWERS]) {
	int drop = BG_WeaponDropTime(G->weapon), up, ready;
	Clear(); Press(S + 1, BUTTON_SPRINT, 0); forwardFrom = S + 1; Seen(k, IN_Y);
	Script(RUN);
	CHECK(R[k - 1].sprint && R[k - 1].state == WEAPON_BOLTING);
	up = First(k, OTHER, -1);
	ready = First(up, OTHER, WEAPON_READY);
	answer[0] = R[k].weapon == OTHER && R[k].state == WEAPON_RAISING;
	answer[1] = R[k].weapon == G->weapon && R[k].state == WEAPON_DROPPING && R[k].wt == drop;
	answer[2] = up - k - (answer[0] ? 0 : Frames(drop));
	answer[3] = R[up].closed;
	answer[4] = ready - up;
	answer[5] = R[ready + 1].weapon == OTHER && R[ready + 1].state == WEAPON_SPRINT_IN;
	CHECK(answer[0] == (k == C) && answer[1] == (k != C) && answer[2] == 0);
	CHECK(answer[3] == (k > C) && answer[5]);
	(void)at;
}

/* YY: Y seen on command k and Y back on k+1. Before C the gun is back in hand
 * at once on k+1 (MW2's double-tap cancel), the action not closed; after C it
 * is back READY, closed. On C the change was instant, so the Y back is a
 * holster of the next gun's raise, and the gun comes back from it. What the
 * gun does once back differs on purpose: a pump or bolt is owed and worked
 * again, whole, from its open note on k+1 (An open action is a debt); a lost
 * switch is not (the owner's rule (e)): the gun stays READY in its old mode. */
static void YY(int at, int k, int answer[ANSWERS]) {
	int n, back, rest;
	Clear(); Seen(k, IN_Y); Seen(k + 1, IN_Y);
	Script(RUN);
	back = First(k + 1, G->weapon, -1);
	rest = First(back, G->weapon, WEAPON_READY);
	answer[0] = back - k;
	answer[1] = R[k].weapon == OTHER;
	answer[2] = R[k + 1].weapon == OTHER && R[k + 1].state == WEAPON_DROPPING ? R[k + 1].wt : -1;
	answer[3] = R[back].closed;
	CHECK(answer[1] == (k == C));
	if (k != C) {
		CHECK(R[k].state == WEAPON_DROPPING && answer[0] == 1 && answer[2] == -1);
		CHECK(R[back].state == (k < C && !G->attachment ? WEAPON_BOLTING : WEAPON_READY));
	} else {
		CHECK(answer[2] > 0 && R[back].state == WEAPON_RAISING);
	}
	CHECK(answer[3] == (k > C));
	if (k <= C && !G->attachment) {
		/* owed: worked again, whole, from the open note, and paid */
		int open = PickupNoteTime(G->weapon, ASEQ_FIRE, WNOTE_BOLT_OPEN);
		int bolting = First(back, G->weapon, WEAPON_BOLTING);
		CHECK(bolting >= 0 && R[bolting].at == open && R[nR - 1].closed);
	} else if (k <= C) {
		/* lost: nothing worked again, the old mode kept */
		for (n = rest; n < nR; n++) CHECK(R[n].state == WEAPON_READY && !R[n].closed && !R[n].notetrack);
	}
}

/* A knife pressed on command k: taken on S (the READY gun, or before the
 * selector's press), lost from S+1 through C (the action's close note still
 * ahead, and C a weapon delay's think: MW2's knife refuses a pending delay,
 * 0x577734, and the think one runs out on, 0x5776ED; the action closes on C
 * all the same), taken after C, cutting the rest short, the action closed. */
static void Knife(int at, int k, int answer[ANSWERS]) {
	Clear(); Press(k, BUTTON_MELEE, 0);
	Script(E + Frames(1500));
	answer[0] = R[k].weapon == WP_KNIFE && R[k].action == WA_MELEE;
	answer[1] = R[k].closed;
	CHECK(answer[0] == (k == S || k > C) && answer[1] == (k >= C));
	if (!answer[0]) CHECK(R[k].weapon == G->weapon && R[k].state == WEAPON_BOLTING && !R[k].action);
	(void)at;
}

/* A frag pressed on command k: the gun's quick drop, and the frag in hand
 * after it, the action not closed; pressed on C the close note's think finishes
 * that drop at once (MW2's path A) and the frag is in hand on C, the action
 * still not closed; after C, closed. */
static void Frag(int at, int k, int answer[ANSWERS]) {
	int in;
	Clear(); Press(k, BUTTON_LETHAL, 0);
	Script(E + Frames(2500));
	in = First(k, WP_FRAG, -1);
	answer[0] = in - k;
	answer[1] = R[in].closed;
	CHECK(in >= 0 && answer[0] == (k == C ? 0 : Frames(BG_WeaponQuickDropTime(G->weapon))));
	CHECK(answer[1] == (k > C));
	(void)at;
}

/* A reload pressed on command k: taken at once, out of the action as out of
 * the READY gun (MW2's PM_BeginWeaponReload takes READY, FIRING and
 * RECHAMBERING), the action closed if its close note came first. */
static void Reload(int at, int k, int answer[ANSWERS]) {
	Clear(); Press(k, BUTTON_RELOAD, 0);
	Script(E + 4);
	answer[0] = R[k].weapon == G->weapon && R[k].state == WEAPON_RELOADING;
	answer[1] = R[k].closed;
	CHECK(answer[0] && answer[1] == (k >= C));
	(void)at;
}

/* The trigger, pulled on command k and held: the action cannot fire, and its
 * end, READY on E, takes the trigger on E. Pulled on E alone it fires on E;
 * pulled and let go before E it fires nothing. */
static void Trigger(int at, int k, int answer[ANSWERS]) {
	int n, first = -1;
	Clear(); holdFrom = k; holdButtons = BUTTON_ATTACK;
	Script(E + 4);
	for (n = 1; n < nR && first < 0; n++) if (R[n].fired) first = n;
	answer[0] = first - E;
	CHECK(answer[0] == 0);
	Clear(); Press(k, BUTTON_ATTACK, 0);
	Script(E + 4);
	for (n = 1, first = -1; n < nR && first < 0; n++) if (R[n].fired) first = n;
	answer[1] = first < 0 ? -1 : first - E;
	CHECK(answer[1] == (k == E ? 0 : -1));
	(void)at;
}

/* The selection seen on C (the NAC), and the close note's event: raised on C
 * whatever takes the gun there, as it is on a pump's or a bolt's NAC. */
static void NacEvent(int at, int k, int answer[ANSWERS]) {
	int n;
	Clear(); Seen(k, IN_Y);
	Script(k + 2);
	for (n = 0; n <= k; n++) CHECK(R[n].notetrack == (n == C));
	answer[0] = R[k].notetrack;
	(void)at;
}

/* Frozen from S+1 through E+4: the action runs on, on its own clock, and ends
 * on E, closed on C. */
static void Freeze(int at, int k, int answer[ANSWERS]) {
	int n;
	Clear(); frozenFrom = k; frozenTo = E + 5;
	Script(E + 8);
	for (n = k; n < E; n++) CHECK(R[n].state == WEAPON_BOLTING && R[n].closed == (n >= C));
	answer[0] = R[E].state == WEAPON_READY && R[E].closed;
	CHECK(answer[0]);
	(void)at;
}

/* Runs a case at the given places for the three guns, and compares them. */
static void SideBySide(const char *name, case_t run, const int *places, int count) {
	int answers[GUNS][ANSWERS], landmark[GUNS][3], g, p;
	for (g = 0; g < GUNS; g++) {
		G = &guns[g]; Landmarks();
		landmark[g][0] = S; landmark[g][1] = C; landmark[g][2] = E;
	}
	for (p = 0; p < count; p++) {
		for (g = 0; g < GUNS; g++) {
			G = &guns[g]; S = landmark[g][0]; C = landmark[g][1]; E = landmark[g][2];
			where = name;
			memset(answers[g], 0, sizeof(answers[g]));
			run(places[p], At(places[p]), answers[g]);
		}
		for (g = 1; g < GUNS; g++) {
			if (memcmp(answers[0], answers[g], sizeof(answers[0]))) {
				fprintf(stderr, "%s at %s, %d ms commands: %s differs from %s\n",
					name, atName[places[p]], msec, guns[g].name, guns[0].name);
			}
			G = &guns[g];
			CHECK(!memcmp(answers[0], answers[g], sizeof(answers[0])));
		}
	}
}

/* The switch NAC and the bolt's, from the hip and aimed, and weapalt's; then
 * back to the gun: a pump or bolt is owed and worked from its open note, a
 * switch is lost (rule (e)): the mode as it was, nothing worked again, and a
 * new press starts a whole switch from 0. */
static void TestReturn(void) {
	int g, n, back;
	for (g = 0; g < GUNS; g++) {
		int landmarks[3];
		G = &guns[g]; Landmarks();
		landmarks[0] = S; landmarks[1] = C; landmarks[2] = E;
		where = "back after the NAC";
		Clear(); Seen(C, IN_Y);
		Script(C + 1);
		CHECK(PS->weapon == OTHER && PS->weaponstate == WEAPON_RAISING && !Closed());
		for (n = 0; n < Frames(3000) && PS->weaponstate != WEAPON_READY; n++) Frame(0, 0);
		CHECK(PS->weapon == OTHER && PS->weaponstate == WEAPON_READY);
		Frame(0, IN_Y);
		for (n = 0; n < Frames(3000) && !(PS->weapon == G->weapon && PS->weaponstate == WEAPON_READY); n++) Frame(0, 0);
		CHECK(PS->weapon == G->weapon && PS->weaponstate == WEAPON_READY && !Closed());
		back = nR;
		Frame(0, 0);
		if (!G->attachment) {
			CHECK(PS->weaponstate == WEAPON_BOLTING &&
				PS->weaponAnimTime == PickupNoteTime(G->weapon, ASEQ_FIRE, WNOTE_BOLT_OPEN));
			continue;
		}
		for (n = 0; n < Frames(2000); n++) Frame(0, 0);
		for (n = back; n < nR; n++) CHECK(R[n].state == WEAPON_READY && !R[n].closed && !R[n].notetrack);
		CHECK(Mode() == startMode);
		/* a new press: the switch whole, from 0, the same way, its click C later */
		back = nR;
		Frame(BUTTON_SELECTFIRE, 0);
		CHECK(PS->weaponstate == WEAPON_BOLTING && PS->weaponAnimTime == 0 && PS->weaponAnimSeq == ASEQ_SELECT_AUTO);
		CHECK(PS->weaponTime == BG_ActionLength(PS));
		for (n = 1; n <= landmarks[2] - landmarks[0]; n++) Frame(0, 0);
		for (n = back; n < nR; n++) {
			CHECK(R[n].closed == (n - back >= landmarks[1]) && R[n].notetrack == (n - back == landmarks[1]));
			CHECK(R[n].state == (n - back < landmarks[2] ? WEAPON_BOLTING : WEAPON_READY));
		}
	}
}

int main(void) {
	static const int rates[] = { 8, 16, 33 };
	static const int changes[] = { AT_S, AT_S1, AT_MID, AT_C1BEFORE, AT_C, AT_CAFTER, AT_E1BEFORE, AT_E };
	static const int alts[] = { AT_C1BEFORE, AT_C, AT_CAFTER };
	static const int sprints[] = { AT_S, AT_S1, AT_MID, AT_C1BEFORE, AT_C, AT_CAFTER, AT_E1BEFORE };
	static const int sprintSwaps[] = { AT_MID, AT_C1BEFORE, AT_C, AT_CAFTER, AT_E1BEFORE };
	static const int yys[] = { AT_S1, AT_MID, AT_C1BEFORE, AT_C, AT_CAFTER };
	static const int equipment[] = { AT_S1, AT_MID, AT_C1BEFORE, AT_C, AT_CAFTER, AT_E1BEFORE };
	static const int inside[] = { AT_S, AT_S1, AT_MID, AT_C1BEFORE, AT_C, AT_CAFTER, AT_E1BEFORE, AT_E };
	static const int triggers[] = { AT_S1, AT_MID, AT_C1BEFORE, AT_C, AT_CAFTER, AT_E1BEFORE, AT_E };
	static const int nac[] = { AT_C };
	static const int freeze[] = { AT_S1 };
	int i, g, k;
	for (i = 0; i < (int)(sizeof(rates) / sizeof(rates[0])); i++) {
		msec = rates[i];
		caseKeys = IN_Y; caseAds = 0;
		SideBySide("Y", Change, changes, ARRAY_LEN(changes));
		caseAds = 1;
		SideBySide("Y with aim held", Change, changes, ARRAY_LEN(changes));
		caseKeys = IN_ALT; caseAds = 0;
		SideBySide("weapalt", Change, alts, ARRAY_LEN(alts));
		SideBySide("sprint", Sprint, sprints, ARRAY_LEN(sprints));
		SideBySide("sprint, then Y", SprintSwap, sprintSwaps, ARRAY_LEN(sprintSwaps));
		SideBySide("YY", YY, yys, ARRAY_LEN(yys));
		SideBySide("frag", Frag, equipment, ARRAY_LEN(equipment));
		SideBySide("trigger", Trigger, triggers, ARRAY_LEN(triggers));
		SideBySide("the close note's event on a NAC", NacEvent, nac, ARRAY_LEN(nac));
		SideBySide("freeze", Freeze, freeze, ARRAY_LEN(freeze));
		/* the knife and the reload on every command of the action, with
		 * Sleight of Hand too: it times a reload's notes, never an action's */
		for (perk = PERK_NONE; perk <= PERK_QUICK_RELOAD; perk += PERK_QUICK_RELOAD) {
			for (g = 0; g < GUNS; g++) {
				int answer[ANSWERS];
				G = &guns[g]; Landmarks();
				for (k = S; k <= E; k++) {
					where = "knife"; Knife(-1, k, answer);
					if (k < E) { where = "reload"; Reload(-1, k, answer); }
				}
			}
			SideBySide("knife", Knife, inside, ARRAY_LEN(inside));
			SideBySide("reload", Reload, inside, ARRAY_LEN(inside) - 1);
		}
		perk = PERK_NONE;
		TestReturn();
	}
	printf("PASS: %d switch-as-a-bolt checks (SPAS pump, M300 bolt and G19 switch side by side)\n", checks);
	return 0;
}
