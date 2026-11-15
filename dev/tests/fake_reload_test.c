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
 * The network is ioquake3's transport with MW2's client clock and MW2's host:
 * the server ticks level.time in sv_fps frames and snapshots each frame; the
 * client keeps cl.serverTime one snapshot interval plus 16ms behind the newest
 * snapshot (CL_SetCGameTime, and CL_AdjustTimeDelta through the REAL
 * CL_DriftTimeDelta of code/client/cl_timedelta.c) and stamps commands with it;
 * cgame keeps the snapshots either side of that time and predicts from the
 * later one, nextSnap (CG_ProcessSnapshots, CG_PredictPlayerState); the server
 * runs each command on arrival (ClientThink_real clamps), then the use code.
 * At 0ms ping the client is your own host, and everything happens in
 * Com_Frame's order, with each snapshot read a frame after the server sent it
 * (HostFrame); with ping it is a remote server's client, by the millisecond.
 * The server sends snapshots as Peace's does, which is MW2's rule
 * (SV_SnapshotDue): your own host one every server frame, an internet client
 * one every 100ms while the server is an online game (sv_onlineGame, MW2's
 * onlinegame, on by default), else every 50ms.
 * The run-fakereload.sh build runs every own-host case through the REAL
 * Com_Frame and loopback as well (fake_reload_engine.c) and requires the same
 * outcome. Both sides run the REAL Pmove; the client draws with the REAL
 * cg_anim through the REAL picture rule. Gameplay timings come from the
 * shipped tables; only the network (frame rate, ping, offsets) and MW2's clock
 * constants are set here.
 *
 * FAKERELOAD_PROBE=1 prints the outcome of every tap time instead of checking.
 * FAKERELOAD_PHASES=1 prints the fake reload success rate against when X is
 * let go, over every snapshot phase.
 * FAKERELOAD_TRACE="action rtt-index tap knife-delay" (FAKERELOAD_GUN=m300 for
 * the rifle) prints one reload-then-knife chain frame by frame, both sides.
 * FAKERELOAD_SVFPS sets the server frame and snapshot rate (MW2's is 20, the
 * default and the shipped autoexec's sv_fps).
 * FAKERELOAD_CLOCK=1 prints the client's clock frame by frame, with no input.
 * FAKERELOAD_MAXPACKETS batches commands as cl_maxpackets does over the
 * internet; by default every frame sends, as on loopback and LAN (your own
 * host always does).
 * FAKERELOAD_BULLETS=1 prints the fake bullet's success over every snapshot
 * phase at every ping. With it (or the other print modes) FAKERELOAD_CLFRAME
 * sets the client frame (com_maxfps 85 is 12), FAKERELOAD_ONLINEGAME=0 gives
 * an internet client a snapshot every 50ms (sv_onlineGame 0) and
 * FAKERELOAD_HOSTHOLD=0 gives ioquake3's own host, which read its snapshots
 * without the held frame. */
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
int  CL_TimeDeltaGap(int delta, int newest, int prev, int realtime);
int  CL_DriftTimeDelta(int delta, int newest, int prev, int realtime, qboolean *extrapolated);
void CG_WeapPicture_BeginReplay(const playerState_t *base);
void CG_WeapPicture_ReplayStep(const playerState_t *before, const playerState_t *after, const entityState_t *item);
void CG_WeapPicture_Update(const playerState_t *predicted, const pmove_t *model, int first, int last,
	qboolean (*getCmd)(int, usercmd_t *));
const playerState_t *CG_WeapPicture_State(const playerState_t *predicted);

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#ifndef FAKERELOAD_ENGINE	/* else the real ones, common.c's */
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level, const char *fmt, ...) { (void)level; fprintf(stderr, "Com_Error %s\n", fmt); exit(1); }
#endif

static int SV_FRAME = 50;	/* sv_fps 20, MW2's; FAKERELOAD_SVFPS overrides */
#define SV_FRAME_FAST	8	/* sv_fps 125: your own host gets every frame */
static int CL_FRAME = 8;	/* 125 fps, one command per frame; FAKERELOAD_CLFRAME in the print modes */
/* MW2's client clock (iw4x.exe), kept here so the engine is checked against
 * MW2 and not against itself. */
#define CL_TIME_BACKOFF	16	/* target behind newest - interval, CL_AdjustTimeDelta 0x5a1aee */
#define CL_EXTRAP_GUARD	8	/* extrapolating at newest - 8, CL_SetCGameTime 0x5031ed */
#define CL_TIME_RESET	500	/* reset beyond this gap, 0x5a1b8a */
#define CL_TIME_FAST	100	/* halve beyond this gap, 0x5a1bcc */
#define CL_EXTRAP_BACK	2	/* back this much after an extrapolation, 0x5a1b36 */
#define RS_BACKUP	32		/* received snapshots kept (ioquake3's PACKET_BACKUP) */
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
static int clDelta, clServerTime, clOldServerTime, clActive, clNewSnap, clSnapTime, clPrevSnapTime;
static qboolean clExtrap;
/* The client's received snapshots, numbered from 1, and cgame's snap and
 * nextSnap among them (0: none). */
static struct { int serverTime; playerState_t ps; } rs[RS_BACKUP];
static int rsLatest, cgProcessed, cgSnap, cgNext;
static const playerState_t *clBase;		/* what prediction starts from */
static int settle;						/* ms for the clock to reach its target */
static int snapInterval;				/* measured between received snapshots */
/* The clock seen at every client frame once it has settled. */
static struct { int leadMin, leadMax, ageMin, ageMax, intervalMin, intervalMax; } clk;
static int pictureDetached, sawGunActionInPicture;
static int clSeen;						/* the client has seen the gun go away */
static int onlineGame = 1;				/* sv_onlineGame; FAKERELOAD_ONLINEGAME in the print modes */
static int ownHost;						/* this run's client is the server's own host */
static int lastSnapTime;				/* the server's lastSnapshotTime for its one client */
/* SV_ClientSnapshotMsec, on the server's frames: what an internet client waits. */
static int RemoteInterval(void) {
	int msec = onlineGame ? 100 : 50;
	return (msec + SV_FRAME - 1) / SV_FRAME * SV_FRAME;
}
/* The interval the client of this ping gets: your own host every frame. */
static int SnapInterval(int rtt) { return rtt ? RemoteInterval() : SV_FRAME; }
/* The slow drift moves the delta at least 1ms a snapshot, from exactly on the
 * first snapshot to one interval plus the backoff behind. */
static int Settle(int rtt) { return (CL_TIME_BACKOFF + SnapInterval(rtt)) * SnapInterval(rtt); }
#ifdef FAKERELOAD_ENGINE
static int engineRuns;					/* runs on your own host also run through the real Com_Frame */
#endif

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

/* A knife counts from the probe's own press: the quick action under test may
 * itself be a knife, and with a short quick raise it can still be out when
 * recording starts. */
static int ProbeKnifeLive(void) { return S.knifeAt == NONE || base == NONE || curNow - base >= S.knifeAt; }
static int SvKnifeOut(void) { return svPs.weapon == WP_KNIFE || (svPs.weaponAction & (WA_TYPE_MASK | WA_FIRED)) == WA_MELEE; }
static void ServerThink(usercmd_t *ucmd) {
	pmove_t pm; int msec, wasKnife;
	if (ucmd->serverTime > levelTime + 200) ucmd->serverTime = levelTime + 200;
	if (ucmd->serverTime < levelTime - 1000) ucmd->serverTime = levelTime - 1000;
	msec = ucmd->serverTime - svPs.commandTime;
	if (msec < 1) return;
	if (msec > 200) svPs.commandTime = ucmd->serverTime - 200;
	memset(&pm, 0, sizeof(pm)); pm.ps = &svPs; pm.cmd = *ucmd;
	wasKnife = SvKnifeOut();
	FakeReloadPmove(&pm);
	if (curNow >= recordFrom) {	/* every command, so a one-command shot is seen */
		if (svPs.weaponstate == WEAPON_RELOADING && !O.svReload) { O.svReload = 1; O.svReloadCmd = svPs.commandTime - svPs.weaponAnimTime; }
		/* A knife the server starts from a knife press in the window: the
		 * quick action under test may itself be a knife still out then. */
		if (SvKnifeOut() && (!wasKnife || S.knifeAt == NONE) && (ucmd->buttons & BUTTON_MELEE || S.knifeAt == NONE)) O.svKnife = 1;
		if (svPs.weapon == GUN && svPs.ammo[GUN] < O.svMinAmmo) O.svMinAmmo = svPs.ammo[GUN];
		if (svPs.weapon == GUN && svPs.weaponstate == WEAPON_FIRING && !O.svShot) { O.svShot = 1; O.svShotCmd = svPs.commandTime - svPs.weaponAnimTime; }
	}
	if (getenv("FAKERELOAD_TRACE") && base != NONE)
		printf("      SV cmd st%d msec%d b%06x -> s%d wt%d A%d act%03x\n", ucmd->serverTime, msec, ucmd->buttons,
			svPs.weaponstate, svPs.weaponTime, !!(svPs.pm_flags & PMF_ADS), svPs.weaponAction & 0x107);
	BG_UpdateActivate(&svPs, &svUse, ucmd->buttons, qfalse, qfalse);	/* nothing to use here */
}

/* CL_SetCGameTime: the clock, then the drift when a snapshot came in. */
static void SetCGameTime(int realtime) {
	if (!clActive) {	/* CL_FirstSnapshot (MW2 0x5a1cd0): exactly on it */
		clActive = 1; clNewSnap = 0;
		clDelta = clSnapTime - realtime; clOldServerTime = clSnapTime;
	}
	clServerTime = realtime + clDelta;
	if (clServerTime < clOldServerTime) clServerTime = clOldServerTime;
	clOldServerTime = clServerTime;
	if (realtime + clDelta >= clSnapTime - CL_EXTRAP_GUARD) clExtrap = qtrue;
	if (clNewSnap) {	/* CL_AdjustTimeDelta */
		int gap = CL_TimeDeltaGap(clDelta, clSnapTime, clPrevSnapTime, realtime);
		clNewSnap = 0;
		clDelta = CL_DriftTimeDelta(clDelta, clSnapTime, clPrevSnapTime, realtime, &clExtrap);
		if (gap > CL_TIME_RESET) clOldServerTime = clServerTime = clSnapTime;
	}
}
/* CG_ProcessSnapshots with cg.time = cl.serverTime: read snapshots in order,
 * one at a time, until nextSnap is past the clock; predict from nextSnap, or
 * from snap when there is none yet (CG_PredictPlayerState). */
static void ProcessSnapshots(void) {
	if (!cgSnap) {
		if (cgProcessed >= rsLatest) return;
		cgSnap = ++cgProcessed;
	}
	for (;;) {
		if (!cgNext) {
			if (cgProcessed >= rsLatest) break;
			cgNext = ++cgProcessed;
		}
		if (clServerTime >= rs[cgSnap % RS_BACKUP].serverTime && clServerTime < rs[cgNext % RS_BACKUP].serverTime) break;
		cgSnap = cgNext; cgNext = 0;
	}
	clBase = &rs[(cgNext ? cgNext : cgSnap) % RS_BACKUP].ps;
}
static qboolean GetCmd(int n, usercmd_t *out) {
	if (n < 0 || n >= numCmds) return qfalse;
	*out = cmds[n]; return qtrue;
}
static void Predict(void) {
	int i; pmove_t model;
	clPred = *clBase;
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
	if (pic->weapon == WP_KNIFE && ProbeKnifeLive()) {
		O.knifePicMs += CL_FRAME;
		if (clPred.weapon == GUN && (clPred.pm_flags & PMF_ADS)) O.adsUnderKnifeMs += CL_FRAME;
		if (detached && !sawGunActionInPicture) O.knifePicWithoutGunAction = 1;
	}
	if (detached) O.detachedMs += CL_FRAME;
	if (detached && clPred.weaponstate == WEAPON_FIRING) O.detachedWhileFiring = 1;
	pictureDetached = detached;
}

/* CL_ParseSnapshot: keep it, and the time of the one before (MW2 0x5ac0ce) */
static void ReceiveSnapshot(int i) {
	if (!rsLatest || sq[i].serverTime > clSnapTime) {
		rsLatest++;
		rs[rsLatest % RS_BACKUP].serverTime = sq[i].serverTime; rs[rsLatest % RS_BACKUP].ps = sq[i].ps;
		clPrevSnapTime = clSnapTime; clSnapTime = sq[i].serverTime; clNewSnap = 1;
	}
}
/* The snapshot after a server frame (SV_SendClientMessages and
 * SV_SnapshotDue): your own host's every frame, an internet client's once its
 * interval has run out. */
static int Snapshot(void) {
	if (!ownHost && lastSnapTime && levelTime - lastSnapTime < RemoteInterval()) return -1;
	lastSnapTime = levelTime;
	sq[sqTail].arrive = PENDING; sq[sqTail].serverTime = levelTime; sq[sqTail].ps = svPs;
	return sqTail++;
}

/* CL_Frame: a command stamped with the clock (CL_CreateNewCommands, before
 * CL_SetCGameTime), then the clock and cgame's frame. Returns the command. */
static int ClientFrame(int now) {
	usercmd_t *c;
	const playerState_t *pic; playerState_t copy; int clip;
	if (!rsLatest) return -1;	/* nothing until the first snapshot */
	c = &cmds[numCmds++];
	memset(c, 0, sizeof(*c));
	c->serverTime = clServerTime ? clServerTime : clSnapTime;
	c->buttons = Buttons(now); c->weapon = BG_SLOT_COMMAND + SLOT_PRIMARY;
	cq[cqTail].arrive = PENDING; cq[cqTail].cmd = *c;
	SetCGameTime(now);
	ProcessSnapshots();
	Predict();
	if (getenv("FAKERELOAD_CLOCK") && base == NONE)
		printf("clock t%5d lead %3d age %3d delta %d\n", now, clSnapTime - clServerTime, clServerTime - clBase->commandTime, clDelta);
	if (now >= settle) {
		int lead = clSnapTime - clServerTime, age = clServerTime - clBase->commandTime, iv = clSnapTime - clPrevSnapTime;
		if (lead < clk.leadMin) clk.leadMin = lead;
		if (lead > clk.leadMax) clk.leadMax = lead;
		if (age < clk.ageMin) clk.ageMin = age;
		if (age > clk.ageMax) clk.ageMax = age;
		if (iv < clk.intervalMin) clk.intervalMin = iv;
		if (iv > clk.intervalMax) clk.intervalMax = iv;
	}
	if (!clSeen && clPred.weapon != GUN) clSeen = 1;
	if (S.action == ACT_NONE ? now >= startPress : clSeen && GunAtRest(&clPred)) {
		if (O.anchor == NONE) O.anchor = now;
	}
	pic = CG_WeapPicture_State(&clPred);
	copy = *pic; clip = FakeReloadPicture(&copy, now, CL_FRAME);
	if (base != NONE ? now >= recordFrom : O.anchor != NONE) Record(pic, clip);
	if (getenv("FAKERELOAD_TRACE") && base != NONE && now >= base - 120 && now <= base + 400)
		printf("t%+5d st%d b%06x | cl w%2d s%2d A%d act%03x ws%d | pic w%2d s%2d %s | sv w%2d s%2d A%d act%03x wf%d | snap ct%d\n",
			now - base, c->serverTime, Buttons(now), clPred.weapon, clPred.weaponstate, !!(clPred.pm_flags & PMF_ADS), clPred.weaponAction & 0x107,
			clPred.weapFlags, pic->weapon, pic->weaponstate, pic != &clPred ? "DET" : "   ",
			svPs.weapon, svPs.weaponstate, !!(svPs.pm_flags & PMF_ADS), svPs.weaponAction & 0x107, svPs.weapFlags, clBase->commandTime);
	return cqTail++;
}

/* Your own host is ioquake3's Com_Frame (code/qcommon/common.c), one frame a
 * command. Its first event loop gives the client the snapshots its server
 * sent last frame, then gives the server the commands its client sent last
 * frame (the server runs each as it arrives); SV_Frame runs the server frames
 * the elapsed time owes and sends the snapshot; the second event loop gives
 * the client nothing of that snapshot, which waits for the next frame (as
 * MW2's host reads its server thread's snapshot a frame late); CL_Frame makes
 * the next command. A loopback client sends every frame (CL_ReadyToSendPacket)
 * and gets every snapshot (no rate). The engine build runs the same halves
 * through the REAL Com_Frame and loopback (fake_reload_engine.c). */
static int svResidual;
/* How long the host holds its snapshots: a frame, Com_Frame's. 0 (only in
 * the print modes, FAKERELOAD_HOSTHOLD=0) is ioquake3's own Com_Frame, whose
 * second event loop read them at once. */
static int hostHold = -1;
/* SV_Frame: the frames the time owes, then their snapshot. */
static int HostServerFrame(int msec) {
	int ran = 0;
	svResidual += msec;
	while (svResidual >= SV_FRAME) { svResidual -= SV_FRAME; levelTime += SV_FRAME; ran = 1; }
	return ran ? Snapshot() : -1;
}
static void HostFrame(int now) {
	int snap, cmd;
	curNow = now;
	while (sqHead < sqTail && sq[sqHead].arrive <= now) ReceiveSnapshot(sqHead++);	/* first event loop */
	while (cqHead < cqTail && cq[cqHead].arrive <= now) ServerThink(&cq[cqHead++].cmd);
	snap = HostServerFrame(CL_FRAME);
	if (snap >= 0) sq[snap].arrive = now + hostHold;
	while (sqHead < sqTail && sq[sqHead].arrive <= now) ReceiveSnapshot(sqHead++);	/* second */
	cmd = ClientFrame(now);
	if (cmd >= 0) cq[cmd].arrive = now + CL_FRAME;
}

/* A client of a remote server, by the millisecond: the server ticks its own
 * frames, and every packet takes oneWay. */
static void NetworkMs(int now) {
	int cmd;
	curNow = now;
	while (cqHead < cqTail && cq[cqHead].arrive != PENDING && cq[cqHead].arrive <= now) ServerThink(&cq[cqHead++].cmd);
	while (sqHead < sqTail && sq[sqHead].arrive <= now) ReceiveSnapshot(sqHead++);
	if (now % SV_FRAME == 0) {
		int snap;
		levelTime += SV_FRAME;
		if ((snap = Snapshot()) >= 0) sq[snap].arrive = now + oneWay;
	}
	if (now % CL_FRAME == 0 && (cmd = ClientFrame(now)) >= 0) {
		if (!maxPackets || now - lastPacket >= 1000 / maxPackets) {	/* CL_ReadyToSendPacket */
			int q;
			for (q = cmd; q >= cqHead && cq[q].arrive == PENDING; q--) cq[q].arrive = now + oneWay;
			lastPacket = now;
		}
	}
}

#ifdef FAKERELOAD_ENGINE
/* The REAL Com_Frame and loopback run the host's halves (fake_reload_engine.c).
 * They hand over what crosses the loopback, and the harness checks that it
 * arrives exactly when HostFrame says. */
void FakeReloadEngineRun(int frameMsec, int end);
int  FakeReloadEngineLoopback(int frameMsec);
static int engineFrame = NONE, enginePhase;
static void EngineStep(int now, int phase) {
	if (now != engineFrame) { CHECK(now == engineFrame + CL_FRAME || engineFrame == NONE); engineFrame = now; enginePhase = 0; }
	CHECK(phase >= enginePhase); enginePhase = phase; curNow = now;
}
void FakeReloadHostSnapshot(int i, int now) {	/* CL_PacketEvent */
	EngineStep(now, 0);
	CHECK(i == sqHead && sq[i].arrive == now); sqHead++;
	ReceiveSnapshot(i);
}
void FakeReloadHostCommand(int i, int now) {	/* SV_PacketEvent */
	EngineStep(now, 1);
	CHECK(i == cqHead && cq[i].arrive == now); cqHead++;
	ServerThink(&cq[i].cmd);
}
int FakeReloadHostServer(int msec, int now) {	/* SV_Frame */
	int snap;
	EngineStep(now, 2);
	CHECK(msec == CL_FRAME && now % CL_FRAME == 0);
	snap = HostServerFrame(msec);
	if (snap >= 0) sq[snap].arrive = now + hostHold;
	return snap;
}
int FakeReloadHostClient(int msec, int now) {	/* CL_Frame */
	int cmd;
	EngineStep(now, 3);
	CHECK(msec == CL_FRAME);
	cmd = ClientFrame(now);
	if (cmd >= 0) cq[cmd].arrive = now + CL_FRAME;
	return cmd;
}
static int viaEngine;	/* run the host through the real Com_Frame */
#endif

static void Run1(int rtt, const script_t *sc, int at) {
	bg_class_t cl = *BG_Class(0);
	int now, end;
	memset(&O, 0, sizeof(O)); O.anchor = NONE; O.svMinAmmo = 1000; S = *sc; base = at;
	/* From well before the gun is back, so a press that changes when it comes back is still seen. */
	recordFrom = base != NONE ? base - 400 : 1 << 30; lastPacket = -1000;
	/* Pressed once the client's clock has settled, and run as long after. */
	settle = Settle(rtt);
	oneWay = rtt / 2; startPress = (settle > 1500 ? settle : 1500) + startShift; end = startPress + 7500;
	ownHost = rtt == 0; svResidual = 0; clSeen = 0; lastSnapTime = 0;
	cqHead = cqTail = sqHead = sqTail = numCmds = 0;
	clDelta = clServerTime = clOldServerTime = clActive = clNewSnap = clSnapTime = clPrevSnapTime = 0;
	clExtrap = qfalse; rsLatest = cgProcessed = cgSnap = cgNext = 0; clBase = NULL;
	clk.leadMin = clk.ageMin = clk.intervalMin = 1 << 30; clk.leadMax = clk.ageMax = clk.intervalMax = -(1 << 30);
	pictureDetached = sawGunActionInPicture = 0;
	CG_WeapPicture_Reset();
	memset(&svPs, 0, sizeof(svPs)); memset(&svUse, 0, sizeof(svUse));
	cl.slot[0] = GUN; cl.slot[1] = WP_MACHINEGUN;
	cl.lethal = S.action == ACT_SEMTEX_CANCEL ? WP_SEMTEX : WP_FRAG; cl.lethalCount = BG_ClassEquipmentCount(cl.lethal);
	BG_ApplyClass(&svPs, &cl);
	svPs.ammo[cl.lethal] = 2;	/* as before MW2's counts: a scenario may use one before its own */
	svPs.stats[STAT_HEALTH] = 100; svPs.pm_type = PM_NORMAL; svPs.weapon = GUN; svPs.weaponstate = WEAPON_READY;
	svPs.ammo[GUN] = svPs.slotAmmo[SLOT_PRIMARY] = startAmmo = BG_WeaponMagSize(GUN) - 2;
	svPs.viewheight = DEFAULT_VIEWHEIGHT; svPs.origin[2] = 24.125f; svPs.groundEntityNum = ENTITYNUM_WORLD;
	levelTime = 1000;
#ifdef FAKERELOAD_ENGINE
	if (ownHost && viaEngine) {
		engineFrame = NONE;
		FakeReloadEngineRun(CL_FRAME, end);
		CHECK(engineFrame + CL_FRAME >= end && engineFrame < end);
	} else
#endif
	if (ownHost) for (now = 0; now < end; now += CL_FRAME) HostFrame(now);
	else for (now = 0; now < end; now++) NetworkMs(now);
	CHECK(O.anchor != NONE);
	CHECK(!O.unpredictedPic);
	O.converged = clPred.weapon == svPs.weapon && clPred.weaponstate == svPs.weaponstate &&
		clPred.ammo[GUN] == svPs.ammo[GUN] && CG_WeapPicture_State(&clPred) == &clPred;
}

/* In the engine build every run on your own host goes through the REAL
 * Com_Frame too, and must end exactly as HostFrame's did. */
static void Run(int rtt, const script_t *sc, int at) {
#ifdef FAKERELOAD_ENGINE
	if (rtt == 0 && hostHold == CL_FRAME) {
		outcome_t model; playerState_t modelSv, modelCl;
		Run1(rtt, sc, at);
		model = O; modelSv = svPs; modelCl = clPred;
		viaEngine = 1; Run1(rtt, sc, at); viaEngine = 0;
		CHECK(!memcmp(&model, &O, sizeof(O)) && !memcmp(&modelSv, &svPs, sizeof(svPs)) && !memcmp(&modelCl, &clPred, sizeof(clPred)));
		engineRuns++;
		return;
	}
#endif
	Run1(rtt, sc, at);
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
	int a, r, x, len, first, last, count[ACT_NONE][4], held[ACT_NONE][4], firstAt[ACT_NONE][4], lastAt[ACT_NONE][4];
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
			if (len == 112) held[a][r] = x;
		}
		/* The window grows with ping, for a press held across one of the
		 * server's snapshots: online they come every 100ms, so a shorter tap
		 * is seen only at some phases. */
		CHECK(112 > SnapInterval(rtts[r]));
		if (r) CHECK(held[a][r] >= held[a][r - 1]);
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
		int button, lastBullet = 0, firstBullet = NONE;
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
				if (firstBullet == NONE) firstBullet = x;
			} else {
				CHECK(!O.predShot && !O.svShot);	/* later: the reload is on screen first */
			}
		}
		CHECK(bullets[r] > 0 && (!r || bullets[r] >= bullets[r - 1]));
		/* from the very next command after the tap */
		CHECK(firstBullet == CL_FRAME);
		printf("  %-4s fake bullet RTT %3d: a shot 8..%dms after the use/reload tap\n",
			GUN == WP_M40A3 ? "M300" : "SPAS", rtts[r], lastBullet);
	}
}

/* The owner's fake bullet: a one-command X tap, then a one-command trigger
 * pull `gap` ms later, over every phase of the server's frames and snapshots
 * against the tap (the player cannot see that phase). The percentage that
 * fake. */
static int FakeBulletRate(int r, int gap) {
	int ok = 0, n = 0, shift, period = SnapInterval(rtts[r]) * CL_FRAME, wait = Settle(rtts[r]);
	for (shift = 0; shift < period; shift += CL_FRAME) {
		script_t sc = Script(ACT_NONE, X);
		int at;
		startShift = shift;
		at = ((wait > 1500 ? wait : 1500) + shift + CL_FRAME - 1) / CL_FRAME * CL_FRAME;	/* a frame */
		sc.reloadAt = 0; sc.reloadLen = CL_FRAME; sc.fireAt = gap; sc.fireLen = CL_FRAME;
		Run(rtts[r], &sc, at);
		CHECK(O.converged && O.svReload);
		/* on the same command the shot is real (FakeBullet also requires no round spent) */
		if (!gap) CHECK(O.svShot && O.predShot);
		ok += FakeBullet(); n++;
	}
	startShift = 0;
	return 100 * ok / n;
}
/* On your own host: sure while the trigger goes out within the clock's backoff
 * and the host's held frame of X, then falling across one snapshot interval
 * as the base's age runs over one, and gone a frame past that. The held frame
 * moves the whole window exactly one frame later than ioquake3's own host. */
static void FakeBulletWindows(void) {
	int k, gap, expect, keep = hostHold, hold[14], nohold[14];
	if (maxPackets || SV_FRAME < 50 || CL_FRAME != 8) return;	/* the shipped rates, a packet a frame */
	for (k = 0; k < 14; k++) hold[k] = FakeBulletRate(0, k * CL_FRAME);
	hostHold = 0;
	for (k = 1; k < 11; k++) nohold[k] = FakeBulletRate(0, k * CL_FRAME);
	hostHold = keep;
	CHECK(!hold[0]);	/* X and the trigger in one command: a real shot */
	for (k = 1; k * CL_FRAME <= CL_TIME_BACKOFF + hostHold; k++) CHECK(hold[k] == 100);
	gap = CL_TIME_BACKOFF + hostHold + snapInterval / 2;
	k = gap / CL_FRAME; gap = k * CL_FRAME;
	expect = 100 * (CL_TIME_BACKOFF + hostHold + snapInterval - gap) / snapInterval;
	CHECK(hold[k] >= expect - 20 && hold[k] <= expect + 20);
	for (k = (CL_TIME_BACKOFF + hostHold + snapInterval + 2 * CL_FRAME - 1) / CL_FRAME; k < 14; k++) CHECK(!hold[k]);
	/* exactly a frame later than ioquake3's own host, at every gap */
	for (k = 1; k < 11; k++) CHECK(hold[k + 1] == nohold[k]);
	printf("  %-4s fake bullet over every phase, your own host:", GUN == WP_M40A3 ? "M300" : "SPAS");
	for (k = 1; k < 14; k++) { printf(" %d:%d%%", k * CL_FRAME, hold[k]); if (!hold[k]) break; }
	printf("\n       (ioquake3's own host, without the held frame:");
	for (k = 1; k < 11; k++) { printf(" %d:%d%%", k * CL_FRAME, nohold[k]); if (!nohold[k]) break; }
	printf(")\n");
}
/* FAKERELOAD_BULLETS=1: FakeBulletRate for every RTT, at FAKERELOAD_CLFRAME,
 * FAKERELOAD_ONLINEGAME, FAKERELOAD_MAXPACKETS and FAKERELOAD_HOSTHOLD. */
static void FakeBulletTable(void) {
	int r, gap, last = (CL_TIME_BACKOFF + 2 * CL_FRAME + 3 * RemoteInterval() + 200) / CL_FRAME * CL_FRAME;
	printf("fake bullet over every snapshot phase: %dms frames, sv frame %dms, a snapshot every %dms to your own host and %dms online, %s, host hold %dms\n",
		CL_FRAME, SV_FRAME, SV_FRAME, RemoteInterval(), maxPackets ? "cl_maxpackets batching (not your own host)" : "a packet a frame", hostHold);
	for (r = 0; r < 4; r++) {
		printf("  RTT %3d%s:", rtts[r], r ? "" : " (your own host)");
		for (gap = CL_FRAME; gap <= last; gap += CL_FRAME) {
			int rate = FakeBulletRate(r, gap);
			printf(" %d:%d%%", gap, rate);
			fflush(stdout);
			if (!rate) break;
		}
		putchar('\n');
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
		int lastKnife = NONE, firstKnife = NONE, fakeTaps = 0;
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
				if (O.svKnife) { CHECK(e < 2 * CL_FRAME); continue; }
				if (O.knifePicMs > 0 && O.adsUnderKnifeMs > 0) {
					CHECK(!O.svReload);
					if (lastKnife == NONE || e > lastKnife) lastKnife = e;
					if (firstKnife == NONE || e < firstKnife) firstKnife = e;
				} else {
					/* refused on both sides: the reload you saw plays out in full */
					CHECK(!O.knifePicMs && O.reloadPicMs == shown);
				}
			}
		}
		CHECK(fakeTaps > 0);
		/* Snapshots every 8ms on your own host leave it little room (24ms
		 * with the host's held frame, none without). */
		CHECK(lastKnife != NONE || (rtts[r] == 0 && SV_FRAME < 50));
		/* from the command aim is in, the one after the gun is back */
		CHECK(lastKnife == NONE || firstKnife == 2 * CL_FRAME);
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
static int allReal;	/* every run of the last SuccessRate reloaded on the server */
static int Gcd(int a, int b) { return b ? Gcd(b, a % b) : a; }
/* Over a server frame in `step` ms steps; on your own host, where everything
 * happens on a frame, over every frame of one cycle of the server's frames
 * against the client's. */
static int SuccessRate(int a, int r, int d, int hold, int step) {
	int ok = 0, n = 0, phase, span = 50;
	allReal = 1;
	if (rtts[r] == 0) { span = SV_FRAME / Gcd(SV_FRAME, CL_FRAME) * CL_FRAME; step = CL_FRAME; }
	else span = RemoteInterval();	/* every phase of an internet client's snapshots */
	for (phase = 0; phase < span; phase += step) {
		script_t s = Script(a, X);
		int at;
		startShift = phase;
		Run(rtts[r], &s, NONE); at = O.anchor;
		s.reloadAt = -d - hold; s.reloadLen = hold;
		Run(rtts[r], &s, at);
		CHECK(O.converged);
		ok += Fake(); n++;
		allReal &= O.svReload;
	}
	startShift = 0;
	return 100 * ok / n;
}
static void Phases(void) {
	const int ds[] = { -8, 0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88 };
	int a, r, i, hold = 64;
	printf("fake reload success over snapshot phases: %dms X tap released d ms before the gun is at rest\n", hold);
	for (a = 0; a < ACT_NONE; a++) for (r = 0; r < 4; r++) {
		printf("  %-13s RTT %3d:", actName[a], rtts[r]);
		for (i = 0; i < (int)(sizeof(ds) / sizeof(ds[0])); i++)
			printf("  d%-2d %3d%%", ds[i], SuccessRate(a, r, ds[i], hold, 2));
		putchar('\n');
	}
}
/* MW2's window on your own host, from MW2's clock and MW2's host. The client
 * predicts from a snapshot one interval plus CL_TIME_BACKOFF behind, and the
 * host reads each snapshot a frame after its server sent it (hostHold), so a
 * tap let go within the backoff and that frame of the gun being up always
 * fakes; earlier, the chance falls across one snapshot interval, and past the
 * clock's whole lag it is gone. The player cannot see the snapshot phase, so
 * this is over all of them. With ping the base is older still, so the windows
 * only widen. */
static void Windows(void) {
	int a, d, rate, expect;
	/* The phase sweep is of snapshots against a packet every frame (your own
	 * host and LAN ignore cl_maxpackets); batched packets add a phase of their
	 * own that it does not cover. */
	if (maxPackets) return;
	for (a = 0; a < ACT_NONE; a++) {
		/* Online, X must be held across one of the server's snapshots to be
		 * seen at all: held longer than their interval, it always is, and
		 * the windows are wide. (A shorter tap is a gamble on the phase.) */
		CHECK(SuccessRate(a, 1, 40, RemoteInterval() + 2 * CL_FRAME, 5) == 100);
		CHECK(SuccessRate(a, 2, 72, RemoteInterval() + 2 * CL_FRAME, 5) == 100);
		for (d = 0; d <= CL_TIME_BACKOFF + hostHold - CL_FRAME; d += CL_FRAME) CHECK(SuccessRate(a, 0, d, 64, 5) == 100);
		/* the fall, where an interval spans more than a couple of commands */
		if (snapInterval > 2 * CL_FRAME) {
			d = CL_TIME_BACKOFF + hostHold + snapInterval / 2;
			rate = SuccessRate(a, 0, d, 64, 5);
			expect = 100 * (CL_TIME_BACKOFF + hostHold + snapInterval - d) / snapInterval;
			CHECK(rate >= expect - 20 && rate <= expect + 20);
		}
		for (d = CL_TIME_BACKOFF + hostHold + snapInterval + 2 * CL_FRAME; d <= CL_TIME_BACKOFF + hostHold + 2 * snapInterval; d += snapInterval / 2)
			CHECK(SuccessRate(a, 0, d, 64, 5) == 0);
		/* let go after it is up: a real reload */
		CHECK(SuccessRate(a, 0, -CL_FRAME, 64, 5) == 0 && allReal);
	}
}

/* MW2's CL_AdjustTimeDelta (0x5a1ad0), branch by branch, on the engine's own
 * CL_DriftTimeDelta. */
static void TestDriftTimeDelta(void) {
	const int newest = 20000, prev = newest - 50, realtime = 9000;
	int interval = newest - prev, target = newest - realtime - interval - CL_TIME_BACKOFF;
	int reset = newest - realtime - CL_TIME_BACKOFF, odd;
	qboolean ex;
	/* behind the target: the whole gap counts */
	ex = qtrue; CHECK(CL_DriftTimeDelta(target + CL_TIME_RESET + 1, newest, prev, realtime, &ex) == reset && ex);
	ex = qtrue; CHECK(CL_DriftTimeDelta(target + CL_TIME_RESET, newest, prev, realtime, &ex) == target + CL_TIME_RESET / 2 && ex);
	ex = qtrue; CHECK(CL_DriftTimeDelta(target + CL_TIME_FAST + 1, newest, prev, realtime, &ex) == target + (CL_TIME_FAST + 1) / 2 && ex);
	ex = qfalse; CHECK(CL_DriftTimeDelta(target + CL_TIME_FAST, newest, prev, realtime, &ex) == target + CL_TIME_FAST - 1 && !ex);
	/* ahead of it: one interval of lead is free */
	ex = qtrue; CHECK(CL_DriftTimeDelta(target - interval - CL_TIME_RESET - 1, newest, prev, realtime, &ex) == reset && ex);
	odd = target - interval - CL_TIME_RESET;
	ex = qtrue; CHECK(CL_DriftTimeDelta(odd, newest, prev, realtime, &ex) == (odd + target) >> 1 && ex);
	odd = target - interval - CL_TIME_FAST - 1;
	ex = qfalse; CHECK(CL_DriftTimeDelta(odd, newest, prev, realtime, &ex) == (odd + target) >> 1);
	ex = qfalse; CHECK(CL_DriftTimeDelta(target - interval - CL_TIME_FAST, newest, prev, realtime, &ex) == target - interval - CL_TIME_FAST + 1);
	ex = qfalse; CHECK(CL_DriftTimeDelta(target - interval, newest, prev, realtime, &ex) == target - interval + 1);
	/* ... unless the interval is not sane (over the reset, or negative) */
	{
		int prevs[2], i;
		prevs[0] = newest - CL_TIME_RESET - 1; prevs[1] = newest + interval;
		for (i = 0; i < 2; i++) {
			int t = newest - realtime - (newest - prevs[i]) - CL_TIME_BACKOFF;
			ex = qfalse; CHECK(CL_DriftTimeDelta(t - CL_TIME_FAST - 1, newest, prevs[i], realtime, &ex) == (t - CL_TIME_FAST - 1 + t) >> 1);
			ex = qfalse; CHECK(CL_DriftTimeDelta(t - CL_TIME_FAST, newest, prevs[i], realtime, &ex) == t - CL_TIME_FAST + 1);
		}
	}
	/* slow: back after an extrapolation, whichever side, clearing it ... */
	ex = qtrue; CHECK(CL_DriftTimeDelta(target - 1, newest, prev, realtime, &ex) == target - 1 - CL_EXTRAP_BACK && !ex);
	ex = qtrue; CHECK(CL_DriftTimeDelta(target + 1, newest, prev, realtime, &ex) == target + 1 - CL_EXTRAP_BACK && !ex);
	/* ... else one toward the target, or none on it */
	ex = qfalse; CHECK(CL_DriftTimeDelta(target + 1, newest, prev, realtime, &ex) == target);
	ex = qfalse; CHECK(CL_DriftTimeDelta(target - 1, newest, prev, realtime, &ex) == target);
	ex = qfalse; CHECK(CL_DriftTimeDelta(target, newest, prev, realtime, &ex) == target && !ex);
	/* no slow drift without the flag (timescaled): reset and fast still run */
	CHECK(CL_DriftTimeDelta(target + 1, newest, prev, realtime, NULL) == target + 1);
	CHECK(CL_DriftTimeDelta(target + CL_TIME_RESET + 1, newest, prev, realtime, NULL) == reset);
	CHECK(CL_DriftTimeDelta(target + CL_TIME_FAST + 1, newest, prev, realtime, NULL) == target + (CL_TIME_FAST + 1) / 2);
	/* the reset is the gap the caller puts the clock on the newest snapshot for */
	CHECK(CL_TimeDeltaGap(target + CL_TIME_RESET + 1, newest, prev, realtime) > CL_TIME_RESET);
	CHECK(CL_TimeDeltaGap(target - interval - CL_TIME_RESET, newest, prev, realtime) <= CL_TIME_RESET);
}

/* The model of CL_SetCGameTime: a frame at newest - CL_EXTRAP_GUARD counts as
 * extrapolated (MW2 0x5031ed), one a millisecond earlier does not. */
static void TestExtrapolationGuard(void) {
	const int newest = 20000, realtime = 9000;
	int early;
	for (early = 0; early < 2; early++) {
		clActive = 1; clNewSnap = 0; clExtrap = qfalse; clOldServerTime = 0;
		clSnapTime = newest; clDelta = newest - CL_EXTRAP_GUARD - early - realtime;
		SetCGameTime(realtime);
		CHECK(clExtrap == !early && clServerTime == newest - CL_EXTRAP_GUARD - early);
	}
}

/* MW2's clock at rest: from one snapshot interval plus the backoff behind the
 * newest snapshot to the backoff behind it, and the snapshot prediction starts
 * from as old. Snapshots every frame do not take the floor away. */
static void TestClockBase(void) {
	int i, keep = SV_FRAME;
	for (i = 0; i < 2; i++) {
		script_t s = Script(ACT_NONE, X);
		SV_FRAME = i ? SV_FRAME_FAST : keep;
		Run(0, &s, NONE);
		printf("  clock at sv_fps %d: %d..%dms behind the newest snapshot, predicting from %d..%dms back\n",
			1000 / SV_FRAME, clk.leadMin, clk.leadMax, clk.ageMin, clk.ageMax);
		CHECK(clk.intervalMin == SV_FRAME && clk.intervalMax == SV_FRAME);
		CHECK(clk.leadMin >= CL_TIME_BACKOFF && clk.leadMax <= CL_TIME_BACKOFF + clk.intervalMax + CL_FRAME);
		/* plus a command's way to the server, a frame (your own host sends
		 * every frame, whatever cl_maxpackets says), and the frame the host
		 * holds its snapshot for */
		CHECK(clk.ageMin >= CL_TIME_BACKOFF + hostHold && clk.ageMax <= CL_TIME_BACKOFF + clk.intervalMax + 2 * CL_FRAME + hostHold);
		if (!i) snapInterval = clk.intervalMax;	/* the shipped rate, for the windows */
	}
	SV_FRAME = keep;
}

int main(void) {
	FakeReloadPictureInit();
	if (getenv("FAKERELOAD_SVFPS")) SV_FRAME = 1000 / atoi(getenv("FAKERELOAD_SVFPS"));
	if (getenv("FAKERELOAD_MAXPACKETS")) maxPackets = atoi(getenv("FAKERELOAD_MAXPACKETS"));
	/* The print modes only: another frame rate (com_maxfps), sv_onlineGame
	 * 0, and ioquake3's own host, which read its snapshots without the held
	 * frame. */
	if (getenv("FAKERELOAD_BULLETS") || getenv("FAKERELOAD_PHASES") || getenv("FAKERELOAD_PROBE") || getenv("FAKERELOAD_CLOCK")) {
		if (getenv("FAKERELOAD_CLFRAME")) CL_FRAME = atoi(getenv("FAKERELOAD_CLFRAME"));
		if (getenv("FAKERELOAD_ONLINEGAME")) onlineGame = atoi(getenv("FAKERELOAD_ONLINEGAME"));
	}
	hostHold = getenv("FAKERELOAD_HOSTHOLD") && (getenv("FAKERELOAD_BULLETS") || getenv("FAKERELOAD_PHASES") ||
		getenv("FAKERELOAD_PROBE") || getenv("FAKERELOAD_CLOCK")) ? atoi(getenv("FAKERELOAD_HOSTHOLD")) : CL_FRAME;
	if (getenv("FAKERELOAD_CLOCK")) { script_t sc = Script(ACT_NONE, X); Run(0, &sc, NONE); return 0; }
	if (getenv("FAKERELOAD_BULLETS")) { FakeBulletTable(); return 0; }
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
#ifdef FAKERELOAD_ENGINE
	CHECK(FakeReloadEngineLoopback(CL_FRAME));
#endif
	TestDriftTimeDelta();
	TestExtrapolationGuard();
	TestClockBase();
	TestSwapOnlyCorrection();
	TestPictureHandsBack();
	Windows();
	FakeReloads(); FakeBullets(); FakeBulletWindows();
	GUN = WP_M40A3; MeasureAnchors();
	Windows();
	FakeReloads(); FakeBullets(); FakeBulletWindows(); WildScopes();
#ifdef FAKERELOAD_ENGINE
	printf("  %d runs on your own host also run through the real Com_Frame and loopback, identically\n", engineRuns);
#endif
	printf("PASS: %d fake reload, fake bullet and wild scope checks\n", checks);
	return 0;
}
