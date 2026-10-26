/* Fake reload, fake bullet and friends, end to end over a modelled network.
 *
 * Nothing in the game names these tricks. They come from MW2's use/reload:
 *  - use/reload (+usereload, the gamepad's X) is a server button. After each
 *    command the server's use code (BG_UpdateActivate) turns a press with
 *    nothing to use into a reload request bit on the playerState, and the
 *    weapon takes the bit as a reload press on the next command. The weapon
 *    does not read the bit during a quick action, and the server clears it
 *    after every command. The client never sets it: it gets it from a snapshot
 *    and replays its own commands over it;
 *  - the viewmodel never un-plays an action of the held gun that prediction
 *    later takes back (cg_weappicture.c).
 * A plain reload (+reload) is read by the weapon itself on both sides, so it
 * is predicted exactly and none of this happens with it.
 *
 * The network is ioquake3's: the server ticks level.time in sv_fps frames and
 * snapshots each frame; the client keeps cl.serverTime just behind the newest
 * snapshot (CL_SetCGameTime / CL_AdjustTimeDelta) and stamps commands with it;
 * the server runs each command on arrival (ClientThink_real clamps), then the
 * use code. Both sides run the REAL Pmove; the client draws with the REAL
 * cg_anim through the REAL picture rule. Gameplay timings come from the
 * shipped tables; only the network (frame rate, ping, offsets) is set here.
 *
 * FAKERELOAD_PROBE=1 prints the outcome of every tap time instead of checking.
 * FAKERELOAD_PHASES=1 prints the fake reload success rate against when X is
 * let go, over every snapshot phase.
 * FAKERELOAD_TRACE="action rtt-index tap knife-delay" (FAKERELOAD_GUN=m300 for
 * the rifle) prints one reload-then-knife chain frame by frame, both sides.
 * FAKERELOAD_SVFPS sets the server frame and snapshot rate (MW2's is 20, the
 * default, which is also what a client gets with ioquake3's default snaps 20).
 * FAKERELOAD_MAXPACKETS batches commands as cl_maxpackets does over the
 * internet; by default every frame sends, as on loopback and LAN. */
#include "../../code/qcommon/q_shared.h"
#include "../../code/game/bg_public.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void FakeReloadModel(pmove_t *move);
void FakeReloadPmove(pmove_t *move);
void FakeReloadPictureInit(void);
int  FakeReloadPicture(playerState_t *ps, int now, int msec);
int  FakeReloadIsReloadClip(int clip);
void CG_WeapPicture_Reset(void);
void CG_WeapPicture_BeginReplay(const playerState_t *base);
void CG_WeapPicture_ReplayStep(const playerState_t *before, const playerState_t *after, const entityState_t *item);
void CG_WeapPicture_Update(const playerState_t *predicted, const pmove_t *model, int first, int last,
	qboolean (*getCmd)(int, usercmd_t *));
const playerState_t *CG_WeapPicture_State(const playerState_t *predicted);

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level, const char *fmt, ...) { (void)level; fprintf(stderr, "Com_Error %s\n", fmt); exit(1); }

static int SV_FRAME = 50;	/* sv_fps 20, MW2's; FAKERELOAD_SVFPS overrides */
#define CL_FRAME	8		/* 125 fps, one command per frame */
#define MAXQ		4096
static int GUN = WP_SHOTGUN;	/* the gun in hand; the SPAS and the M300 have authored clips */
#define X			BUTTON_USE_RELOAD
#define R			BUTTON_RELOAD
#define TAP			16

enum { ACT_KNIFE, ACT_FRAG, ACT_SEMTEX_CANCEL, ACT_NONE, ACT_COUNT };
static const char *actName[ACT_COUNT] = { "knife", "frag", "semtex cancel", "gun at rest" };
#define NONE (-100000)
typedef struct {
	int action, button;					/* the reload button: X (use/reload) or R (+reload) */
	int reloadAt, reloadLen, knifeAt, knifeLen, adsAt, fireAt, fireLen;	/* ms after the gun is back */
	int reload2At, reload2Len;
} script_t;
typedef struct {
	int anchor;							/* client time the gun was seen back */
	int predReload, svReload, predReloadCmd, svReloadCmd;
	int svKnife, svShot, predShot, predShotCmd, svShotCmd;
	int reloadPicMs, knifePicMs, adsUnderReloadMs, adsUnderKnifeMs, detachedMs;
	int knifePicWithoutGunAction;		/* a knife drawn detached, with no gun action shown since rest */
	int detachedWhileFiring;			/* the picture kept the fake running over a real shot */
	int converged;						/* client prediction equals the server at the end */
	int svMinAmmo;						/* the server's fewest rounds in the magazine */
	int predHadReload, predHadKnife;	/* prediction showed one since the gun last rested */
	int unpredictedPic;					/* the picture drew a reload or knife prediction never had */
} outcome_t;

static script_t S;
static outcome_t O;
static playerState_t svPs, clPred;
static bg_activate_t svUse;
static int levelTime, oneWay, startPress, startShift, base, startAmmo, curNow, recordFrom, lastPacket;
static int maxPackets;	/* 0: a packet every frame (loopback, LAN); else cl_maxpackets */
#define PENDING (-1)
static struct { int arrive; usercmd_t cmd; } cq[MAXQ];
static struct { int arrive, serverTime; playerState_t ps; } sq[MAXQ];
static usercmd_t cmds[MAXQ];
static int cqHead, cqTail, sqHead, sqTail, numCmds;
static int clDelta, clServerTime, clOldServerTime, clExtrap, clHaveSnap, clNewSnap, clSnapTime;
static playerState_t clSnapPs;
static int pictureDetached, sawGunActionInPicture;

static int Buttons(int now) {
	int b = 0, t;
	if (S.action == ACT_KNIFE && now >= startPress && now < startPress + TAP) b |= BUTTON_MELEE;
	if (S.action == ACT_FRAG && now >= startPress && now < startPress + TAP) b |= BUTTON_LETHAL;
	if (S.action == ACT_SEMTEX_CANCEL) {
		/* hold until the Semtex is out, then Y cancels it back to the gun */
		int out = BG_HolsterTime(&svPs) + BG_WeaponRaiseTime(WP_SEMTEX) + 200;
		if (now >= startPress && now < startPress + out + 100) b |= BUTTON_LETHAL;
		if (now >= startPress + out && now < startPress + out + CL_FRAME) b |= BUTTON_WEAPON_SWAP;
	}
	if (base == NONE) return b;
	t = now - base;
	if (t >= S.reloadAt && t < S.reloadAt + S.reloadLen) b |= S.button;
	if (t >= S.reload2At && t < S.reload2At + S.reload2Len) b |= S.button;
	if (t >= S.knifeAt && t < S.knifeAt + S.knifeLen) b |= BUTTON_MELEE;
	if (S.adsAt != NONE && t >= S.adsAt) b |= BUTTON_ADS;
	if (t >= S.fireAt && t < S.fireAt + S.fireLen) b |= BUTTON_ATTACK;
	return b;
}

static void ServerThink(usercmd_t *ucmd) {
	pmove_t pm; int msec;
	if (ucmd->serverTime > levelTime + 200) ucmd->serverTime = levelTime + 200;
	if (ucmd->serverTime < levelTime - 1000) ucmd->serverTime = levelTime - 1000;
	msec = ucmd->serverTime - svPs.commandTime;
	if (msec < 1) return;
	if (msec > 200) svPs.commandTime = ucmd->serverTime - 200;
	memset(&pm, 0, sizeof(pm)); pm.ps = &svPs; pm.cmd = *ucmd;
	FakeReloadPmove(&pm);
	if (curNow >= recordFrom) {	/* every command, so a one-command shot is seen */
		if (svPs.weaponstate == WEAPON_RELOADING && !O.svReload) { O.svReload = 1; O.svReloadCmd = svPs.commandTime - svPs.weaponAnimTime; }
		if (svPs.weapon == WP_KNIFE || (svPs.weaponAction & (WA_TYPE_MASK | WA_FIRED)) == WA_MELEE) O.svKnife = 1;
		if (svPs.weapon == GUN && svPs.ammo[GUN] < O.svMinAmmo) O.svMinAmmo = svPs.ammo[GUN];
		if (svPs.weapon == GUN && svPs.weaponstate == WEAPON_FIRING && !O.svShot) { O.svShot = 1; O.svShotCmd = svPs.commandTime - svPs.weaponAnimTime; }
	}
	if (getenv("FAKERELOAD_TRACE") && base != NONE)
		printf("      SV cmd st%d msec%d b%06x -> s%d wt%d A%d act%03x\n", ucmd->serverTime, msec, ucmd->buttons,
			svPs.weaponstate, svPs.weaponTime, !!(svPs.pm_flags & PMF_ADS), svPs.weaponAction & 0x107);
	BG_UpdateActivate(&svPs, &svUse, ucmd->buttons, qfalse, qfalse);	/* nothing to use here */
}

static void AdjustTimeDelta(int realtime) {
	int newDelta = clSnapTime - realtime, dd = abs(newDelta - clDelta);
	clNewSnap = 0;
	if (dd > 500) { clDelta = newDelta; clOldServerTime = clServerTime = clSnapTime; }
	else if (dd > 100) clDelta = (clDelta + newDelta) >> 1;
	else if (clExtrap) { clExtrap = 0; clDelta -= 2; } else clDelta++;
}
static qboolean GetCmd(int n, usercmd_t *out) {
	if (n < 0 || n >= numCmds) return qfalse;
	*out = cmds[n]; return qtrue;
}
static void Predict(void) {
	int i; pmove_t model;
	clPred = clSnapPs;
	CG_WeapPicture_BeginReplay(&clPred);
	for (i = 0; i < numCmds; i++) {
		pmove_t pm; playerState_t before;
		if (cmds[i].serverTime <= clPred.commandTime) continue;
		memset(&pm, 0, sizeof(pm)); pm.ps = &clPred; pm.cmd = cmds[i];
		before = clPred;
		FakeReloadPmove(&pm);
		CG_WeapPicture_ReplayStep(&before, &clPred, NULL);
	}
	memset(&model, 0, sizeof(model)); FakeReloadModel(&model);
	CG_WeapPicture_Update(&clPred, &model, numCmds > 64 ? numCmds - 64 : 0, numCmds - 1, GetCmd);
}

static int GunAtRest(const playerState_t *ps) {
	return ps->weapon == GUN && ps->weaponstate == WEAPON_READY &&
		!(ps->weaponAction & (WA_TYPE_MASK | WA_RETURNING | WA_RECOVERING));
}
static void Record(const playerState_t *pic, int clip) {
	int detached = pic != &clPred;
	if (clPred.weaponstate == WEAPON_RELOADING && !O.predReload) { O.predReload = 1; O.predReloadCmd = clPred.commandTime - clPred.weaponAnimTime; }
	if (clPred.weapon == GUN && clPred.weaponstate == WEAPON_FIRING && !O.predShot) { O.predShot = 1; O.predShotCmd = clPred.commandTime - clPred.weaponAnimTime; }
	/* The picture never shows a reload or a knife that prediction never had. */
	if (GunAtRest(&clPred) && GunAtRest(pic)) O.predHadReload = O.predHadKnife = 0;
	if (clPred.weapon == GUN && clPred.weaponstate == WEAPON_RELOADING) O.predHadReload = 1;
	if (clPred.weapon == WP_KNIFE || (clPred.weaponAction & (WA_TYPE_MASK | WA_FIRED)) == WA_MELEE) O.predHadKnife = 1;
	if ((pic->weapon == GUN && FakeReloadIsReloadClip(clip) && !O.predHadReload) ||
		(pic->weapon == WP_KNIFE && !O.predHadKnife)) O.unpredictedPic = 1;
	if (pic->weapon == GUN && FakeReloadIsReloadClip(clip)) {
		O.reloadPicMs += CL_FRAME;
		if (clPred.pm_flags & PMF_ADS) O.adsUnderReloadMs += CL_FRAME;
	}
	/* Has the picture shown the held gun doing something since it last rested? */
	if (GunAtRest(pic)) sawGunActionInPicture = 0;
	if (pic->weapon == GUN && (pic->weaponstate == WEAPON_RELOADING ||
		pic->weaponstate == WEAPON_FIRING || pic->weaponstate == WEAPON_BOLTING)) sawGunActionInPicture = 1;
	if (pic->weapon == WP_KNIFE) {
		O.knifePicMs += CL_FRAME;
		if (clPred.weapon == GUN && (clPred.pm_flags & PMF_ADS)) O.adsUnderKnifeMs += CL_FRAME;
		if (detached && !sawGunActionInPicture) O.knifePicWithoutGunAction = 1;
	}
	if (detached) O.detachedMs += CL_FRAME;
	if (detached && clPred.weaponstate == WEAPON_FIRING) O.detachedWhileFiring = 1;
	pictureDetached = detached;
}

static void Run(int rtt, const script_t *sc, int at) {
	bg_class_t cl = *BG_Class(0);
	int now, end = 9000, seen = 0;
	memset(&O, 0, sizeof(O)); O.anchor = NONE; O.svMinAmmo = 1000; S = *sc; base = at;
	/* From well before the gun is back, so a press that changes when it comes back is still seen. */
	recordFrom = base != NONE ? base - 400 : 1 << 30; lastPacket = -1000;
	oneWay = rtt / 2; startPress = 1500 + startShift;
	cqHead = cqTail = sqHead = sqTail = numCmds = 0;
	clDelta = clServerTime = clOldServerTime = clExtrap = clHaveSnap = clNewSnap = 0;
	pictureDetached = sawGunActionInPicture = 0;
	CG_WeapPicture_Reset();
	memset(&svPs, 0, sizeof(svPs)); memset(&svUse, 0, sizeof(svUse));
	cl.slot[0] = GUN; cl.slot[1] = WP_MACHINEGUN;
	cl.lethal = S.action == ACT_SEMTEX_CANCEL ? WP_SEMTEX : WP_FRAG; cl.lethalCount = 2;
	BG_ApplyClass(&svPs, &cl);
	svPs.stats[STAT_HEALTH] = 100; svPs.pm_type = PM_NORMAL; svPs.weapon = GUN; svPs.weaponstate = WEAPON_READY;
	svPs.ammo[GUN] = svPs.slotAmmo[SLOT_PRIMARY] = startAmmo = BG_WeaponMagSize(GUN) - 2;
	svPs.viewheight = 26; svPs.origin[2] = 24.125f; svPs.groundEntityNum = ENTITYNUM_WORLD;
	levelTime = 1000;
	for (now = 0; now < end; now++) {
		curNow = now;
		while (cqHead < cqTail && cq[cqHead].arrive != PENDING && cq[cqHead].arrive <= now) ServerThink(&cq[cqHead++].cmd);
		while (sqHead < sqTail && sq[sqHead].arrive <= now) {
			if (!clHaveSnap) { clHaveSnap = 1; clSnapTime = sq[sqHead].serverTime; clSnapPs = sq[sqHead].ps;
				clDelta = clSnapTime - now; clOldServerTime = clSnapTime; }
			else if (sq[sqHead].serverTime > clSnapTime) { clSnapTime = sq[sqHead].serverTime; clSnapPs = sq[sqHead].ps; clNewSnap = 1; }
			sqHead++;
		}
		if (now % SV_FRAME == 0) {
			levelTime += SV_FRAME;
			sq[sqTail].arrive = now + oneWay; sq[sqTail].serverTime = levelTime; sq[sqTail].ps = svPs; sqTail++;
		}
		if (now % CL_FRAME == 0 && clHaveSnap) {
			usercmd_t *c = &cmds[numCmds++];
			const playerState_t *pic; playerState_t copy; int clip;
			memset(c, 0, sizeof(*c));
			c->serverTime = clServerTime ? clServerTime : clSnapTime;
			c->buttons = Buttons(now); c->weapon = BG_SLOT_COMMAND + SLOT_PRIMARY;
			cq[cqTail].arrive = PENDING; cq[cqTail].cmd = *c; cqTail++;
			if (!maxPackets || now - lastPacket >= 1000 / maxPackets) {	/* CL_ReadyToSendPacket */
				int q;
				for (q = cqTail - 1; q >= cqHead && cq[q].arrive == PENDING; q--) cq[q].arrive = now + oneWay;
				lastPacket = now;
			}
			clServerTime = now + clDelta;
			if (clServerTime < clOldServerTime) clServerTime = clOldServerTime;
			clOldServerTime = clServerTime;
			if (now + clDelta >= clSnapTime - 5) clExtrap = 1;
			if (clNewSnap) AdjustTimeDelta(now);
			Predict();
			if (!seen && clPred.weapon != GUN) seen = 1;
			if (S.action == ACT_NONE ? now >= startPress : seen && GunAtRest(&clPred)) {
				if (O.anchor == NONE) O.anchor = now;
			}
			pic = CG_WeapPicture_State(&clPred);
			copy = *pic; clip = FakeReloadPicture(&copy, now, CL_FRAME);
			if (base != NONE ? now >= recordFrom : O.anchor != NONE) Record(pic, clip);
			if (getenv("FAKERELOAD_TRACE") && base != NONE && now >= base - 120 && now <= base + 400)
				printf("t%+5d st%d b%06x | cl w%2d s%2d A%d act%03x ws%d | pic w%2d s%2d %s | sv w%2d s%2d A%d act%03x wf%d | snap ct%d\n",
					now - base, c->serverTime, Buttons(now), clPred.weapon, clPred.weaponstate, !!(clPred.pm_flags & PMF_ADS), clPred.weaponAction & 0x107,
					clPred.weapFlags, pic->weapon, pic->weaponstate, pic != &clPred ? "DET" : "   ",
					svPs.weapon, svPs.weaponstate, !!(svPs.pm_flags & PMF_ADS), svPs.weaponAction & 0x107, svPs.weapFlags, clSnapPs.commandTime);
		}
	}
	CHECK(O.anchor != NONE);
	CHECK(!O.unpredictedPic);
	O.converged = clPred.weapon == svPs.weapon && clPred.weaponstate == svPs.weaponstate &&
		clPred.ammo[GUN] == svPs.ammo[GUN] && CG_WeapPicture_State(&clPred) == &clPred;
}

static script_t Script(int action, int button) {
	script_t s = { 0, 0, NONE, 0, NONE, 0, NONE, NONE, 0, NONE, 0 };
	s.action = action; s.button = button;
	return s;
}
static int Fake(void) { return O.predReload && !O.svReload && O.reloadPicMs >= BG_ReloadSegmentTime(&svPs, RSEQ_START); }
static int FakeBullet(void) { return O.predShot && !O.svShot && O.svReload && O.svMinAmmo >= startAmmo && O.converged; }

static int anchorAt[ACT_COUNT][4];
static const int rtts[] = { 0, 50, 100, 200 };

/* When the client sees the gun back, with no other input. */
static void MeasureAnchors(void) {
	int a, r;
	for (a = 0; a < ACT_COUNT; a++) for (r = 0; r < 4; r++) {
		script_t s = Script(a, X); Run(rtts[r], &s, NONE);
		anchorAt[a][r] = a == ACT_NONE ? startPress : O.anchor;
	}
}

static char Classify(void) {
	if (Fake()) return 'F';
	if (O.predReload && !O.svReload) return 'f';	/* predicted, too short to show */
	if (O.svReload) return 'R';
	return '.';
}

static void Probe(void) {
	int a, r, t, d, len, button;
	for (button = 0; button < 2; button++) for (len = TAP; len <= 112; len += 32) {
		printf("%s tap %dms, press t = -400..200 ms from the gun at rest: F fake reload, R real, . nothing\n",
			button ? "+reload" : "use/reload", len);
		for (a = 0; a < ACT_NONE; a++) for (r = 0; r < 4; r++) {
			printf("  %-13s RTT %3d: ", actName[a], rtts[r]);
			for (t = -400; t <= 200; t += 8) {
				script_t s = Script(a, button ? R : X); s.reloadAt = t; s.reloadLen = len;
				Run(rtts[r], &s, anchorAt[a][r]);
				putchar(Classify());
			}
			putchar('\n');
		}
	}
	printf("fake bullet: tap at 0, fire tap at d = -40..300; B fake bullet, S shot, R reload only\n");
	for (button = 0; button < 2; button++) for (r = 0; r < 4; r++) {
		printf("  %-10s RTT %3d: ", button ? "+reload" : "use/reload", rtts[r]);
		for (d = -40; d <= 300; d += 8) {
			script_t s = Script(ACT_NONE, button ? R : X);
			s.reloadAt = 0; s.reloadLen = TAP; s.fireAt = d; s.fireLen = TAP;
			Run(rtts[r], &s, anchorAt[ACT_NONE][r]);
			putchar(FakeBullet() ? 'B' : O.svShot && O.predShot ? 'S' : O.svReload ? 'R' : O.predShot ? 'b' : '.');
		}
		putchar('\n');
	}
	printf("knife tap d = 8..48ms after a 48ms use/reload tap at t = -240..96, ADS after: W fake knife aimed, K knife\n");
	for (a = 0; a < ACT_NONE; a++) for (r = 0; r < 4; r++) {
		printf("  %-13s RTT %3d: ", actName[a], rtts[r]);
		for (t = -240; t <= 96; t += 8) {
			char c = '.';
			for (d = CL_FRAME; d <= 48; d += CL_FRAME) {
				script_t s = Script(a, X);
				s.reloadAt = t; s.reloadLen = 48; s.knifeAt = t + 48 + d; s.knifeLen = TAP; s.adsAt = t + 48 + d + CL_FRAME;
				Run(rtts[r], &s, anchorAt[a][r]);
				if (!O.svKnife && O.knifePicMs > 0 && O.adsUnderKnifeMs > 0) c = 'W';
				else if (c == '.' && O.svKnife) c = 'K';
			}
			putchar(c);
		}
		putchar('\n');
	}
}

/* The press times (8ms steps from -400) of a use/reload tap that give a fake reload. */
static int FakeWindow(int a, int r, int len, int *first, int *last) {
	int t, n = 0;
	*first = *last = NONE;
	for (t = -400; t <= 200; t += 8) {
		script_t s = Script(a, X); s.reloadAt = t; s.reloadLen = len;
		Run(rtts[r], &s, anchorAt[a][r]);
		CHECK(O.converged);
		if (Fake()) { n++; if (*first == NONE) *first = t; *last = t; }
		/* Pressed once the gun is at rest, it is an ordinary reload. */
		if (t >= 0) CHECK(O.svReload && !O.detachedMs);
	}
	return n;
}

static void FakeReloads(void) {
	int a, r, x, len, first, last, count[ACT_NONE][4], firstAt[ACT_NONE][4], lastAt[ACT_NONE][4];
	for (a = 0; a < ACT_NONE; a++) for (r = 0; r < 4; r++) {
		script_t s;
		/* A use/reload TAP as the gun comes back plays the reload and reloads
		 * nothing: the press reaches the client in a snapshot taken while the
		 * server's gun was still away, and the client replays it onto the gun's
		 * return; the server dropped it a command after it was made. */
		for (len = TAP; len <= 112; len += 32) {
			x = FakeWindow(a, r, len, &first, &last);
			CHECK(x > 0);
			if (len == 48) { count[a][r] = x; firstAt[a][r] = first; lastAt[a][r] = last; }
		}
		/* The window grows with ping. */
		if (r) CHECK(count[a][r] >= count[a][r - 1]);
		/* A HOLD is a real reload: the server asks again on every command. */
		s = Script(a, X); s.reloadAt = firstAt[a][r]; s.reloadLen = 600; Run(rtts[r], &s, anchorAt[a][r]);
		CHECK(O.svReload && O.converged);
		/* During the fake you can aim (no gate is loosened: the server is not reloading) ... */
		s = Script(a, X); s.reloadAt = firstAt[a][r]; s.reloadLen = 48; s.adsAt = 48; Run(rtts[r], &s, anchorAt[a][r]);
		CHECK(Fake() && O.adsUnderReloadMs > 0);
		/* ... shoot, and the shot takes the picture over ... */
		s = Script(a, X); s.reloadAt = firstAt[a][r]; s.reloadLen = 48; s.fireAt = 400; s.fireLen = TAP;
		Run(rtts[r], &s, anchorAt[a][r]);
		CHECK(!O.svReload && O.svShot && O.converged && O.detachedMs > 0 && !O.detachedWhileFiring);
		/* ... or start a real reload. */
		s = Script(a, X); s.reloadAt = firstAt[a][r]; s.reloadLen = 48; s.reload2At = 400; s.reload2Len = 48;
		Run(rtts[r], &s, anchorAt[a][r]);
		CHECK(O.svReload && O.detachedMs > 0 && O.svReloadCmd - O.predReloadCmd >= 300 && O.converged);
		/* A plain reload is the weapon's own button: predicted, never fake. */
		for (x = -400; x <= 200; x += 8) {
			s = Script(a, R); s.reloadAt = x; s.reloadLen = 48; Run(rtts[r], &s, anchorAt[a][r]);
			CHECK(!Fake() && O.svReload == O.predReload && !O.detachedMs && O.converged);
		}
		/* No fake shot: nothing holds the trigger. A fire tap anywhere around
		 * the return is a real shot on the same command. */
		for (x = -200; x <= 200; x += 8) {
			s = Script(a, X); s.fireAt = x; s.fireLen = TAP; Run(rtts[r], &s, anchorAt[a][r]);
			if (O.predShot) CHECK(O.svShot && O.svShotCmd == O.predShotCmd && !O.detachedMs && O.converged);
			if (x >= 0) CHECK(O.predShot);
		}
		/* A knife alone is never a knife you can scope through: while the gun is
		 * coming back it is nothing, once it is back it is a knife hip-fired
		 * and nothing aimed through a scope. */
		for (x = -200; x <= 200; x += 8) {
			s = Script(a, X); s.knifeAt = x; s.knifeLen = TAP; s.adsAt = x + 8; Run(rtts[r], &s, anchorAt[a][r]);
			CHECK(!O.knifePicWithoutGunAction && O.converged);
			CHECK(O.svKnife || !O.detachedMs);
			s = Script(a, X); s.knifeAt = x; s.knifeLen = TAP; s.adsAt = -400; Run(rtts[r], &s, anchorAt[a][r]);
			CHECK(!O.knifePicWithoutGunAction && O.converged && (O.svKnife || !O.detachedMs));
			/* Once the gun is back and aimed (a command after it is back). */
			if (x >= 16) CHECK(O.svKnife == !BG_WeaponScopeOverlay(GUN) && (O.svKnife || !O.knifePicMs));
			/* While it is still coming back, a knife is nothing (MW2's knife
			 * check skips the quick raise, the command it ends on included). */
			if (x <= 0) CHECK(!O.svKnife && !O.knifePicMs);
		}
		printf("  %-4s fake reload after %-13s RTT %3d: a 48ms tap pressed %4d..%4dms before the gun is at rest (%d of 8ms steps)\n",
			GUN == WP_M40A3 ? "M300" : "SPAS", actName[a], rtts[r], -firstAt[a][r], -lastAt[a][r], count[a][r]);
	}
}

/* The fake bullet: a use/reload tap just before a shot. The server takes the
 * reload first and never fires; the client, not yet told, shows the shot. */
static void FakeBullets(void) {
	int r, x, bullets[4];
	for (r = 0; r < 4; r++) {
		int button, lastBullet = 0;
		bullets[r] = 0;
		for (button = 0; button < 2; button++) for (x = -40; x <= 300; x += 8) {
			script_t s = Script(ACT_NONE, button ? R : X);
			s.reloadAt = 0; s.reloadLen = TAP; s.fireAt = x; s.fireLen = TAP;
			Run(rtts[r], &s, anchorAt[ACT_NONE][r]);
			CHECK(O.converged && O.svReload);
			if (button || x <= 0) {
				/* +reload, or a trigger pulled first: what the client shows, happened */
				CHECK(O.predShot == O.svShot && O.svShotCmd == O.predShotCmd);
				if (button) CHECK(O.predReloadCmd == O.svReloadCmd);	/* use/reload's is a round trip late */
				/* Shoot, then reload at once. Pressed together, +reload beats the
				 * trigger; use/reload is not the weapon's button, so the shot goes. */
				CHECK(O.svShot == (x < 0 || (!button && x == 0)));
			} else if (FakeBullet()) {
				bullets[r]++; lastBullet = x;
			} else {
				CHECK(!O.predShot && !O.svShot);	/* later: the reload is on screen first */
			}
		}
		CHECK(bullets[r] > 0 && (!r || bullets[r] >= bullets[r - 1]));
		printf("  %-4s fake bullet RTT %3d: a shot 8..%dms after the use/reload tap\n",
			GUN == WP_M40A3 ? "M300" : "SPAS", rtts[r], lastBullet);
	}
}

/* The wild scope. MW2 starts no knife while a scoped gun is aimed. Through a
 * fake reload the client is not aiming - it thinks the gun is reloading - so a
 * knife pressed then starts on the client and not on the server: the gun stays
 * scoped, and the picture, which never un-plays the reload, plays that knife.
 * Aiming is held throughout, as the scope must already be in on the server. */
static void WildScopes(void) {
	int a, r, t, e;
	for (a = 0; a < ACT_NONE; a++) for (r = 0; r < 4; r++) {
		int lastKnife = NONE, fakeTaps = 0;
		for (t = -400; t <= -48; t += 8) {
			script_t s = Script(a, X);
			int shown;
			s.reloadAt = t; s.reloadLen = 48; s.adsAt = t; Run(rtts[r], &s, anchorAt[a][r]);
			if (!Fake()) continue;
			fakeTaps++;
			shown = O.reloadPicMs;
			/* A knife pressed after the gun is back, while the fake reload plays. */
			for (e = 0; e <= 1500; e += e < 400 ? 8 : 100) {
				s.knifeAt = e; s.knifeLen = TAP; Run(rtts[r], &s, anchorAt[a][r]);
				CHECK(O.converged);
				/* On the command the gun is back, aim is not in yet: a real knife. */
				if (O.svKnife) { CHECK(e < 16); continue; }
				if (O.knifePicMs > 0 && O.adsUnderKnifeMs > 0) {
					CHECK(!O.svReload);
					if (lastKnife == NONE || e > lastKnife) lastKnife = e;
				} else {
					/* refused on both sides: the reload you saw plays out in full */
					CHECK(!O.knifePicMs && O.reloadPicMs == shown);
				}
			}
		}
		CHECK(fakeTaps > 0);
		/* Snapshots every 8ms on your own host leave no room for it at all. */
		CHECK(lastKnife != NONE || (rtts[r] == 0 && SV_FRAME < 50));
		if (lastKnife == NONE) printf("  M300 wild scope after %-13s RTT %3d: none\n", actName[a], rtts[r]);
		else printf("  M300 wild scope after %-13s RTT %3d: a knife up to %3dms after the gun is back\n",
			actName[a], rtts[r], lastKnife);
	}
	/* Without the fake reload, a knife while scoped is nothing, on both sides. */
	for (a = 0; a < ACT_NONE; a++) for (r = 0; r < 4; r++) for (e = 16; e <= 400; e += 8) {
		script_t s = Script(a, X);
		s.adsAt = -400; s.knifeAt = e; s.knifeLen = TAP; Run(rtts[r], &s, anchorAt[a][r]);
		CHECK(O.converged && !O.svKnife && !O.knifePicMs && !O.detachedMs);
	}
}

/* A correction that removes only a swap - a knife the server never started -
 * is followed at once: the picture never shows a weapon you never held. No
 * network desync in this model removes a lone knife, so drive the picture
 * directly: one frame predicts the knife, the next is corrected without it. */
static void TestSwapOnlyCorrection(void) {
	playerState_t rest, first, corrected;
	pmove_t pm, model;
	script_t idle = Script(ACT_NONE, X);
	Run(0, &idle, NONE);
	rest = svPs;
	CHECK(GunAtRest(&rest));
	numCmds = 2; memset(cmds, 0, 2 * sizeof(cmds[0]));
	cmds[0].serverTime = rest.commandTime + 8; cmds[0].buttons = BUTTON_MELEE; cmds[0].weapon = BG_SLOT_COMMAND;
	cmds[1].serverTime = rest.commandTime + 16; cmds[1].weapon = BG_SLOT_COMMAND;
	memset(&model, 0, sizeof(model)); FakeReloadModel(&model);
	CG_WeapPicture_Reset();
	CG_WeapPicture_BeginReplay(&rest);
	first = rest; memset(&pm, 0, sizeof(pm)); pm.ps = &first; pm.cmd = cmds[0]; FakeReloadPmove(&pm);
	CG_WeapPicture_ReplayStep(&rest, &first, NULL);
	CG_WeapPicture_Update(&first, &model, 0, 0, GetCmd);
	CG_WeapPicture_Update(&first, &model, 0, 0, GetCmd);
	CHECK((first.weaponAction & WA_TYPE_MASK) == WA_MELEE && CG_WeapPicture_State(&first) == &first);
	/* the server refused it: the corrected replay has no knife */
	CG_WeapPicture_BeginReplay(&rest);
	corrected = rest; cmds[0].buttons = 0;
	memset(&pm, 0, sizeof(pm)); pm.ps = &corrected; pm.cmd = cmds[0]; FakeReloadPmove(&pm);
	CG_WeapPicture_ReplayStep(&rest, &corrected, NULL);
	memset(&pm, 0, sizeof(pm)); pm.ps = &corrected; pm.cmd = cmds[1]; FakeReloadPmove(&pm);
	CG_WeapPicture_ReplayStep(&rest, &corrected, NULL);
	CG_WeapPicture_Update(&corrected, &model, 0, 1, GetCmd);
	CHECK(!(corrected.weaponAction & WA_TYPE_MASK) && CG_WeapPicture_State(&corrected) == &corrected);
}

/* A detached picture only finishes what you saw. If its own run would begin
 * something that no input of yours began - here the empty gun reloading
 * itself after a shot the server never fired - it hands back to prediction
 * instead of drawing it. */
static void TestPictureHandsBack(void) {
	playerState_t rest, base2, shot, pred, before;
	pmove_t pm, model;
	script_t idle = Script(ACT_NONE, X);
	int i, j, detached = 0, handed = 0;
	Run(0, &idle, NONE);
	rest = svPs; rest.ammo[GUN] = 1;
	CHECK(GunAtRest(&rest));
	memset(cmds, 0, sizeof(cmds));
	for (i = 0; i < 600; i++) { cmds[i].serverTime = rest.commandTime + 8 * (i + 1); cmds[i].weapon = BG_SLOT_COMMAND; }
	cmds[0].buttons = BUTTON_ATTACK;
	memset(&model, 0, sizeof(model)); FakeReloadModel(&model);
	CG_WeapPicture_Reset();
	/* frame 1: prediction fires the last round */
	numCmds = 1;
	CG_WeapPicture_BeginReplay(&rest);
	shot = rest; memset(&pm, 0, sizeof(pm)); pm.ps = &shot; pm.cmd = cmds[0]; FakeReloadPmove(&pm);
	CG_WeapPicture_ReplayStep(&rest, &shot, NULL);
	CG_WeapPicture_Update(&shot, &model, 0, 0, GetCmd);
	CG_WeapPicture_Update(&shot, &model, 0, 0, GetCmd);
	CHECK(shot.weaponstate == WEAPON_FIRING && shot.ammo[GUN] == 0 && CG_WeapPicture_State(&shot) == &shot);
	/* then the server's snapshot: it never fired, and prediction rests */
	base2 = rest; base2.commandTime = cmds[0].serverTime;
	for (i = 1; i < 600 && !handed; i++) {
		const playerState_t *pic;
		numCmds = i + 1;
		CG_WeapPicture_BeginReplay(&base2);
		pred = base2;
		for (j = 1; j <= i; j++) {
			before = pred; memset(&pm, 0, sizeof(pm)); pm.ps = &pred; pm.cmd = cmds[j]; FakeReloadPmove(&pm);
			CG_WeapPicture_ReplayStep(&before, &pred, NULL);
		}
		CG_WeapPicture_Update(&pred, &model, 0, i, GetCmd);
		pic = CG_WeapPicture_State(&pred);
		if (pic != &pred) { detached = 1; CHECK(pic->weaponstate != WEAPON_RELOADING); }
		else if (detached) handed = 1;
	}
	CHECK(detached && handed && GunAtRest(&pred));
}

/* How often a use/reload tap released d ms before the gun is at rest gives a
 * fake reload, over every phase of the server's snapshots against the quick
 * action (the player cannot see that phase, so this is the success rate). */
static int SuccessRate(int a, int r, int d, int hold, int step) {
	int ok = 0, n = 0, phase;
	for (phase = 0; phase < 50; phase += step) {
		script_t s = Script(a, X);
		int at;
		startShift = phase;
		Run(rtts[r], &s, NONE); at = O.anchor;
		s.reloadAt = -d - hold; s.reloadLen = hold;
		Run(rtts[r], &s, at);
		CHECK(O.converged);
		ok += Fake(); n++;
	}
	startShift = 0;
	return 100 * ok / n;
}
static void Phases(void) {
	const int ds[] = { -8, 0, 8, 16, 24, 32, 40, 48, 56, 72 };
	int a, r, i, hold = 64;
	printf("fake reload success over snapshot phases: %dms X tap released d ms before the gun is at rest\n", hold);
	for (a = 0; a < ACT_NONE; a++) for (r = 0; r < 4; r++) {
		printf("  %-13s RTT %3d:", actName[a], rtts[r]);
		for (i = 0; i < (int)(sizeof(ds) / sizeof(ds[0])); i++)
			printf("  d%-2d %3d%%", ds[i], SuccessRate(a, r, ds[i], hold, 2));
		putchar('\n');
	}
}
/* The owner's MW2: tight on your own host but doable almost every time if you
 * let go as the gun comes up, and easy with ping. The player cannot see the
 * snapshot phase, so this is over all of them. */
static void Windows(void) {
	int a;
	for (a = 0; a < ACT_NONE; a++) {
		CHECK(SuccessRate(a, 0, 0, 64, 5) >= 80);
		CHECK(SuccessRate(a, 0, 48, 64, 5) == 0);
		CHECK(SuccessRate(a, 0, -8, 64, 5) == 0);	/* let go after it is up: a real reload */
		CHECK(SuccessRate(a, 1, 40, 64, 5) == 100);
		CHECK(SuccessRate(a, 2, 72, 64, 5) == 100);
	}
}

int main(void) {
	FakeReloadPictureInit();
	if (getenv("FAKERELOAD_SVFPS")) SV_FRAME = 1000 / atoi(getenv("FAKERELOAD_SVFPS"));
	if (getenv("FAKERELOAD_MAXPACKETS")) maxPackets = atoi(getenv("FAKERELOAD_MAXPACKETS"));
	MeasureAnchors();
	if (getenv("FAKERELOAD_PROBE")) { Probe(); return 0; }
	if (getenv("FAKERELOAD_PHASES")) { Phases(); return 0; }
	if (getenv("FAKERELOAD_TRACE")) {
		int a, r, t, d; script_t sc;
		sscanf(getenv("FAKERELOAD_TRACE"), "%d %d %d %d", &a, &r, &t, &d);
		if (getenv("FAKERELOAD_GUN") && !strcmp(getenv("FAKERELOAD_GUN"), "m300")) { GUN = WP_M40A3; MeasureAnchors(); }
		sc = Script(a, X); sc.reloadAt = t; sc.reloadLen = 48; sc.adsAt = t; sc.knifeAt = t + 48 + d; sc.knifeLen = TAP;
		Run(rtts[r], &sc, anchorAt[a][r]);
		return 0;
	}
	TestSwapOnlyCorrection();
	TestPictureHandsBack();
	Windows();
	FakeReloads(); FakeBullets();
	GUN = WP_M40A3; MeasureAnchors();
	FakeReloads(); FakeBullets(); WildScopes();
	printf("PASS: %d fake reload, fake bullet and wild scope checks\n", checks);
	return 0;
}
