/* Fake reload and wild scope, end to end over a modelled network.
 *
 * Nothing in the game names these tricks. They come from two rules:
 *  - quick actions are resolved by the server, which hands the gun back on its
 *    own clock (BG_ServerReleaseWeapon); until then the weapon does not answer;
 *  - the viewmodel never un-plays an action of the held gun that prediction
 *    later takes back (cg_weappicture.c).
 *
 * The network is ioquake3's: the server ticks level.time in sv_fps frames and
 * snapshots each frame; the client keeps cl.serverTime just behind the newest
 * snapshot (CL_SetCGameTime / CL_AdjustTimeDelta) and stamps commands with it;
 * the server runs each command on arrival (ClientThink_real clamps) and then
 * the release hook. Both sides run the REAL Pmove; the client draws with the
 * REAL cg_anim through the REAL picture rule. Gameplay timings come from the
 * shipped tables; only the network (frame rate, ping, offsets) is set here. */
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

#define SV_FRAME	50		/* sv_fps 20 */
#define CL_FRAME	8		/* 125 fps, one command per frame */
#define MAXQ		4096
#define GUN			WP_SHOTGUN	/* the gun in hand: it has authored clips */

enum { ACT_KNIFE, ACT_FRAG, ACT_SEMTEX_CANCEL, ACT_COUNT };
static const char *actName[ACT_COUNT] = { "knife", "frag", "semtex cancel" };
#define NONE (-100000)
typedef struct {
	int action;
	int reloadAt, reloadLen, knifeAt, knifeLen, adsAt, fireAt, fireLen;	/* ms after the client sees the gun back */
	int reload2At, reload2Len;
} script_t;
typedef struct {
	int anchor;							/* client time the gun was seen back */
	int predReload, svReload, predReloadCmd, svReloadCmd;
	int svKnife, svShot, predShot, predShotCmd, svShotCmd;
	int reloadPicMs, knifePicMs, adsUnderReloadMs, adsUnderKnifeMs, detachedMs;
	int knifePicWithoutGunAction;		/* a knife drawn from a detached picture with no gun action first */
	int detachedWhileFiring;			/* the picture kept the fake running over a real shot */
	int converged;						/* client prediction equals the server at the end */
} outcome_t;

static script_t S;
static outcome_t O;
static playerState_t svPs, clPred;
static int levelTime, oneWay, startPress;
static struct { int arrive; usercmd_t cmd; } cq[MAXQ];
static struct { int arrive, serverTime; playerState_t ps; } sq[MAXQ];
static usercmd_t cmds[MAXQ];
static int cqHead, cqTail, sqHead, sqTail, numCmds;
static int clDelta, clServerTime, clOldServerTime, clExtrap, clHaveSnap, clNewSnap, clSnapTime;
static playerState_t clSnapPs;
static int pictureDetached, sawGunActionInPicture;

static int Buttons(int now) {
	int b = 0, t;
	if (S.action == ACT_KNIFE && now >= startPress && now < startPress + 16) b |= BUTTON_MELEE;
	if (S.action == ACT_FRAG && now >= startPress && now < startPress + 16) b |= BUTTON_LETHAL;
	if (S.action == ACT_SEMTEX_CANCEL) {
		/* hold until the Semtex is out, then Y cancels it back to the gun */
		int out = BG_HolsterTime(&svPs) + BG_WeaponRaiseTime(WP_SEMTEX) + 200;
		if (now >= startPress && now < startPress + out + 100) b |= BUTTON_LETHAL;
		if (now >= startPress + out && now < startPress + out + CL_FRAME) b |= BUTTON_WEAPON_SWAP;
	}
	if (O.anchor == NONE) return b;
	t = now - O.anchor;
	if (t >= S.reloadAt && t < S.reloadAt + S.reloadLen) b |= BUTTON_RELOAD;
	if (t >= S.reload2At && t < S.reload2At + S.reload2Len) b |= BUTTON_RELOAD;
	if (t >= S.knifeAt && t < S.knifeAt + S.knifeLen) b |= BUTTON_MELEE;
	if (S.adsAt != NONE && t >= S.adsAt) b |= BUTTON_ADS;
	if (t >= S.fireAt && t < S.fireAt + S.fireLen) b |= BUTTON_ATTACK;
	return b;
}

static void ServerThink(usercmd_t *ucmd) {
	pmove_t pm; int msec, before;
	if (ucmd->serverTime > levelTime + 200) ucmd->serverTime = levelTime + 200;
	if (ucmd->serverTime < levelTime - 1000) ucmd->serverTime = levelTime - 1000;
	msec = ucmd->serverTime - svPs.commandTime;
	if (msec < 1) return;
	if (msec > 200) svPs.commandTime = ucmd->serverTime - 200;
	memset(&pm, 0, sizeof(pm)); pm.ps = &svPs; pm.cmd = *ucmd;
	before = svPs.weaponReleaseTime;
	FakeReloadPmove(&pm);
	BG_ServerReleaseWeapon(&svPs, before, levelTime);
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
	if (svPs.weaponstate == WEAPON_RELOADING && !O.svReload) { O.svReload = 1; O.svReloadCmd = svPs.commandTime - svPs.weaponAnimTime; }
	if (svPs.weapon == WP_KNIFE) O.svKnife = 1;
	if (clPred.weapon == GUN && clPred.weaponstate == WEAPON_FIRING && !O.predShot) { O.predShot = 1; O.predShotCmd = clPred.commandTime - clPred.weaponAnimTime; }
	if (svPs.weapon == GUN && svPs.weaponstate == WEAPON_FIRING && !O.svShot) { O.svShot = 1; O.svShotCmd = svPs.commandTime - svPs.weaponAnimTime; }
	if (pic->weapon == GUN && FakeReloadIsReloadClip(clip)) {
		O.reloadPicMs += CL_FRAME;
		if (clPred.pm_flags & PMF_ADS) O.adsUnderReloadMs += CL_FRAME;
	}
	if (detached && !pictureDetached) sawGunActionInPicture = pic->weaponstate == WEAPON_RELOADING ||
		pic->weaponstate == WEAPON_FIRING || pic->weaponstate == WEAPON_BOLTING;
	if (detached && (pic->weaponstate == WEAPON_RELOADING || pic->weaponstate == WEAPON_FIRING)) sawGunActionInPicture = 1;
	if (pic->weapon == WP_KNIFE) {
		O.knifePicMs += CL_FRAME;
		if (clPred.weapon == GUN && (clPred.pm_flags & PMF_ADS)) O.adsUnderKnifeMs += CL_FRAME;
		if (detached && !sawGunActionInPicture) O.knifePicWithoutGunAction = 1;
	}
	if (detached) O.detachedMs += CL_FRAME;
	if (detached && clPred.weaponstate == WEAPON_FIRING) O.detachedWhileFiring = 1;
	pictureDetached = detached;
}

static void Run(int rtt, const script_t *sc) {
	bg_class_t cl = *BG_Class(0);
	int now, end = 9000, seen = 0;
	memset(&O, 0, sizeof(O)); O.anchor = NONE; S = *sc;
	oneWay = rtt / 2; startPress = 1500;
	cqHead = cqTail = sqHead = sqTail = numCmds = 0;
	clDelta = clServerTime = clOldServerTime = clExtrap = clHaveSnap = clNewSnap = 0;
	pictureDetached = sawGunActionInPicture = 0;
	CG_WeapPicture_Reset();
	memset(&svPs, 0, sizeof(svPs));
	cl.slot[0] = GUN; cl.slot[1] = WP_MACHINEGUN;
	cl.lethal = S.action == ACT_SEMTEX_CANCEL ? WP_SEMTEX : WP_FRAG; cl.lethalCount = 2;
	BG_ApplyClass(&svPs, &cl);
	svPs.stats[STAT_HEALTH] = 100; svPs.pm_type = PM_NORMAL; svPs.weapon = GUN; svPs.weaponstate = WEAPON_READY;
	svPs.ammo[GUN] = svPs.slotAmmo[SLOT_PRIMARY] = BG_WeaponMagSize(GUN) - 2;
	svPs.viewheight = 26; svPs.origin[2] = 24.125f; svPs.groundEntityNum = ENTITYNUM_WORLD;
	levelTime = 1000;
	for (now = 0; now < end; now++) {
		while (cqHead < cqTail && cq[cqHead].arrive <= now) ServerThink(&cq[cqHead++].cmd);
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
			cq[cqTail].arrive = now + oneWay; cq[cqTail].cmd = *c; cqTail++;
			clServerTime = now + clDelta;
			if (clServerTime < clOldServerTime) clServerTime = clOldServerTime;
			clOldServerTime = clServerTime;
			if (now + clDelta >= clSnapTime - 5) clExtrap = 1;
			if (clNewSnap) AdjustTimeDelta(now);
			Predict();
			if (!seen && clPred.weapon != GUN) seen = 1;
			if (seen && O.anchor == NONE && GunAtRest(&clPred)) O.anchor = now;
			pic = CG_WeapPicture_State(&clPred);
			copy = *pic; clip = FakeReloadPicture(&copy, now, CL_FRAME);
			if (O.anchor != NONE) Record(pic, clip);
		}
	}
	CHECK(O.anchor != NONE);
	O.converged = clPred.weapon == svPs.weapon && clPred.weaponstate == svPs.weaponstate &&
		clPred.ammo[GUN] == svPs.ammo[GUN] && CG_WeapPicture_State(&clPred) == &clPred;
}

static script_t Script(int action) {
	script_t s = { 0, NONE, 0, NONE, 0, NONE, NONE, 0, NONE, 0 };
	s.action = action;
	return s;
}
static int Fake(void) { return O.predReload && !O.svReload && O.reloadPicMs >= BG_ReloadSegmentTime(&svPs, RSEQ_START); }

int main(void) {
	const int rtts[] = { 0, 50, 100, 200 };
	int a, r, x, d, lastFake[ACT_COUNT][4];
	FakeReloadPictureInit();

	for (a = 0; a < ACT_COUNT; a++) for (r = 0; r < 4; r++) {
		script_t s;
		/* S1: a reload TAP as the gun comes back plays the reload and reloads nothing. */
		s = Script(a); s.reloadAt = 0; s.reloadLen = 16; Run(rtts[r], &s);
		CHECK(Fake() && O.converged);
		/* The window lasts until the client hears the server's hand-off; it grows with ping. */
		lastFake[a][r] = 0;
		for (x = 0; x <= 400; x += 8) {
			s = Script(a); s.reloadAt = x; s.reloadLen = 16; Run(rtts[r], &s);
			if (Fake()) lastFake[a][r] = x;
			CHECK(O.converged);
		}
		if (r) CHECK(lastFake[a][r] >= lastFake[a][r - 1]);
		/* Well after the window, a tap is an ordinary reload. */
		s = Script(a); s.reloadAt = lastFake[a][r] + 200; s.reloadLen = 16; Run(rtts[r], &s);
		CHECK(O.svReload && !O.detachedMs);
		/* A HOLD is a real reload: the server sees it once the gun is handed back. */
		s = Script(a); s.reloadAt = 0; s.reloadLen = 600; Run(rtts[r], &s);
		CHECK(O.svReload && O.converged);
		/* During the fake you can aim (no gate is loosened: the server is not reloading) ... */
		s = Script(a); s.reloadAt = 0; s.reloadLen = 16; s.adsAt = 8; Run(rtts[r], &s);
		CHECK(Fake() && O.adsUnderReloadMs > 0);
		/* ... shoot, and the shot takes the picture over ... */
		s = Script(a); s.reloadAt = 0; s.reloadLen = 16; s.fireAt = lastFake[a][r] + 100; s.fireLen = 16; Run(rtts[r], &s);
		CHECK(!O.svReload && O.svShot && O.converged && O.detachedMs > 0 && !O.detachedWhileFiring);
		/* ... or start a real reload: a second, held press after the fake one. */
		s = Script(a); s.reloadAt = 0; s.reloadLen = 16; s.reload2At = lastFake[a][r] + 100; s.reload2Len = 600;
		Run(rtts[r], &s);
		CHECK(O.svReload && O.detachedMs > 0 && O.svReloadCmd - O.predReloadCmd >= 100 && O.converged);
		/* S4: a knife alone in the window is late or nothing - never a knife you can scope through. */
		for (x = 0; x <= lastFake[a][r] + 100; x += 8) {
			s = Script(a); s.knifeAt = x; s.knifeLen = 16; s.adsAt = x + 8; Run(rtts[r], &s);
			CHECK(!O.knifePicWithoutGunAction && O.converged);
			CHECK(O.svKnife || !O.detachedMs);
		}
	}
	/* S3: wild scope. A reload tap chained straight into a knife tap gives a fake
	 * reload and then a fake knife, and you aim while the knife plays. */
	for (a = 0; a < ACT_COUNT; a++) for (r = 0; r < 4; r++) {
		int wild = 0;
		for (d = CL_FRAME; d <= 48; d += CL_FRAME) {
			script_t s = Script(a);
			s.reloadAt = 0; s.reloadLen = 16; s.knifeAt = d; s.knifeLen = 16; s.adsAt = d + CL_FRAME;
			Run(rtts[r], &s);
			CHECK(O.converged && !O.svReload);
			if (!O.svKnife && O.knifePicMs > 0 && O.adsUnderKnifeMs > 0) wild++;
		}
		printf("  wild scope after %-13s RTT %3d: %d of 6 chain delays; fake reload window 0..%dms\n",
			actName[a], rtts[r], wild, lastFake[a][r]);
		CHECK(wild > 0);
	}
	printf("PASS: %d fake reload / wild scope checks\n", checks);
	return 0;
}
