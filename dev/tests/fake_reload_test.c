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
 * without the held frame.
 * FAKERELOAD_SKREE=check runs the skree's checks alone (Skrees, below). */
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
	int fire2At, fire2Len;				/* a second pull of the trigger */
	int selectAt, selectLen;			/* the selector (the G19's Select Fire) */
	int swapAt;							/* the frame Y is pressed on: its command carries the
										 * swap button, the next ones slot 1 (cg_weapons.c) */
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
	int svShots, svRoundCmd[4];			/* rounds the server's magazine lost, and the commands the first four left on */
	int predAdsWhileReloading;			/* prediction aimed while prediction was reloading */
} outcome_t;

static script_t S;
static outcome_t O;
static playerState_t svPs, clPred;
static bg_activate_t svUse;
static int levelTime, oneWay, startPress, startShift, base, startAmmo, curNow, recordFrom, lastPacket;
static int runAfter = 7500;	/* how long a run goes on after its press */
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
#define OTHER_GUN	WP_MACHINEGUN		/* slot 1: the gun Y goes to */
static int gunAttachment;				/* the gun's own attachment (else the class's) */
static int fullMag;						/* the gun starts with a full magazine */
static int otherPartial;				/* the slot-1 gun's magazine is not full (the skree) */
/* Command by command, by its index in cmds: the server after running it (the
 * gun's action closed, its magazine), and the client's frame that built it
 * (prediction, the picture, and the time of the last command its base had
 * run). */
typedef struct { int ran, weapon, state, closed, ammo; } svCmd_t;
typedef struct { int predWeapon, predState, picWeapon, picState, detached, baseTime; } clFrame_t;
static svCmd_t svAt[MAXQ];
static clFrame_t clAt[MAXQ];
/* What the gun's action closes is closed: its pump or bolt paid, or, with
 * Select Fire, the SF's mode committed (the G19 starts semi-automatic). */
static int ActionClosed(const playerState_t *ps) {
	bg_weaponInstance_t g;
	BG_GetSlot(ps, SLOT_PRIMARY, &g);
	return gunAttachment == BG_ATTACHMENT_SWITCH ? (g.flags & BG_GUN_AUTO) != 0 : !(g.flags & BG_GUN_UNCHAMBERED);
}

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
	if (t >= S.fire2At && t < S.fire2At + S.fire2Len) b |= BUTTON_ATTACK;
	if (t >= S.selectAt && t < S.selectAt + S.selectLen) b |= BUTTON_SELECTFIRE;
	if (S.swapAt != NONE && t >= S.swapAt && t < S.swapAt + CL_FRAME) b |= BUTTON_WEAPON_SWAP;
	return b;
}

/* A knife counts from the probe's own press: the quick action under test may
 * itself be a knife, and with a short quick raise it can still be out when
 * recording starts. */
static int ProbeKnifeLive(void) { return S.knifeAt == NONE || base == NONE || curNow - base >= S.knifeAt; }
static int SvKnifeOut(void) { return svPs.weapon == WP_KNIFE || (svPs.weaponAction & (WA_TYPE_MASK | WA_FIRED)) == WA_MELEE; }
static void RunOnServer(int index) {
	usercmd_t *ucmd = &cq[index].cmd;
	pmove_t pm; int msec, wasKnife, rounds;
	if (ucmd->serverTime > levelTime + 200) ucmd->serverTime = levelTime + 200;
	if (ucmd->serverTime < levelTime - 1000) ucmd->serverTime = levelTime - 1000;
	msec = ucmd->serverTime - svPs.commandTime;
	if (msec < 1) return;
	if (msec > 200) svPs.commandTime = ucmd->serverTime - 200;
	memset(&pm, 0, sizeof(pm)); pm.ps = &svPs; pm.cmd = *ucmd;
	wasKnife = SvKnifeOut();
	rounds = svPs.weapon == GUN ? svPs.ammo[GUN] : -1;
	FakeReloadPmove(&pm);
	{
		bg_weaponInstance_t g;
		BG_GetSlot(&svPs, SLOT_PRIMARY, &g);
		svAt[index].ran = 1; svAt[index].weapon = svPs.weapon; svAt[index].state = svPs.weaponstate;
		svAt[index].closed = ActionClosed(&svPs); svAt[index].ammo = g.ammo;
	}
	if (curNow >= recordFrom) {	/* every command, so a one-command shot is seen */
		if (svPs.weapon == GUN && svPs.ammo[GUN] < rounds) {	/* a round left the magazine */
			if (O.svShots < 4) O.svRoundCmd[O.svShots] = svPs.commandTime;
			O.svShots++;
		}
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
	if (clPred.weaponstate == WEAPON_RELOADING && (clPred.pm_flags & PMF_ADS)) O.predAdsWhileReloading = 1;
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
	/* Y's selection is first seen on the command after the frame it was pressed on */
	if (base != NONE && S.swapAt != NONE && now - base > S.swapAt)
		c->weapon = BG_WeaponCommand(&rs[rsLatest % RS_BACKUP].ps, SLOT_SECONDARY, qfalse);
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
	clAt[numCmds - 1].predWeapon = clPred.weapon; clAt[numCmds - 1].predState = clPred.weaponstate;
	clAt[numCmds - 1].picWeapon = pic->weapon; clAt[numCmds - 1].picState = pic->weaponstate;
	clAt[numCmds - 1].detached = pic != &clPred; clAt[numCmds - 1].baseTime = clBase->commandTime;
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
	while (cqHead < cqTail && cq[cqHead].arrive <= now) RunOnServer(cqHead++);
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
	while (cqHead < cqTail && cq[cqHead].arrive != PENDING && cq[cqHead].arrive <= now) RunOnServer(cqHead++);
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
	RunOnServer(i);
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
	oneWay = rtt / 2; startPress = (settle > 1500 ? settle : 1500) + startShift; end = startPress + runAfter;
	ownHost = rtt == 0; svResidual = 0; clSeen = 0; lastSnapTime = 0;
	cqHead = cqTail = sqHead = sqTail = numCmds = 0;
	clDelta = clServerTime = clOldServerTime = clActive = clNewSnap = clSnapTime = clPrevSnapTime = 0;
	clExtrap = qfalse; rsLatest = cgProcessed = cgSnap = cgNext = 0; clBase = NULL;
	clk.leadMin = clk.ageMin = clk.intervalMin = 1 << 30; clk.leadMax = clk.ageMax = clk.intervalMax = -(1 << 30);
	pictureDetached = sawGunActionInPicture = 0;
	CG_WeapPicture_Reset();
	memset(svAt, 0, sizeof(svAt)); memset(clAt, 0, sizeof(clAt));
	memset(&svPs, 0, sizeof(svPs)); memset(&svUse, 0, sizeof(svUse));
	cl.slot[0] = GUN; cl.slot[1] = OTHER_GUN;
	if (gunAttachment) { cl.attachments[0] = gunAttachment; cl.attachments[1] = 0; }
	cl.lethal = S.action == ACT_SEMTEX_CANCEL ? WP_SEMTEX : WP_FRAG; cl.lethalCount = BG_ClassEquipmentCount(cl.lethal);
	BG_ApplyClass(&svPs, &cl);
	svPs.ammo[cl.lethal] = 2;	/* as before MW2's counts: a scenario may use one before its own */
	svPs.stats[STAT_HEALTH] = 100; svPs.pm_type = PM_NORMAL; svPs.weapon = GUN; svPs.weaponstate = WEAPON_READY;
	svPs.ammo[GUN] = svPs.slotAmmo[SLOT_PRIMARY] = startAmmo = BG_WeaponMagSize(GUN) - (fullMag ? 0 : 2);
	if (otherPartial) svPs.ammo[OTHER_GUN] = svPs.slotAmmo[SLOT_SECONDARY] = BG_WeaponMagSize(OTHER_GUN) - 2;
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
		static svCmd_t modelSvAt[MAXQ];
		static clFrame_t modelClAt[MAXQ];
		outcome_t model; playerState_t modelSv, modelCl;
		Run1(rtt, sc, at);
		model = O; modelSv = svPs; modelCl = clPred;
		memcpy(modelSvAt, svAt, sizeof(svAt)); memcpy(modelClAt, clAt, sizeof(clAt));
		viaEngine = 1; Run1(rtt, sc, at); viaEngine = 0;
		CHECK(!memcmp(&model, &O, sizeof(O)) && !memcmp(&modelSv, &svPs, sizeof(svPs)) && !memcmp(&modelCl, &clPred, sizeof(clPred)));
		CHECK(!memcmp(modelSvAt, svAt, sizeof(svAt)) && !memcmp(modelClAt, clAt, sizeof(clAt)));
		engineRuns++;
		return;
	}
#endif
	Run1(rtt, sc, at);
}

static script_t Script(int action, int button) {
	script_t s = { 0, 0, NONE, 0, NONE, 0, NONE, NONE, 0, NONE, 0, NONE, 0, NONE, 0, NONE };
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

/* A shot the server never fired, on a gun with rounds left: the detached
 * picture plays the shot you were shown, the READY command after it (MW2's,
 * owing the action, the fire clock parked at the open note) and the whole
 * pump or bolt from there - one fire clip - and hands back only once the gun
 * is at rest. The READY command is not rest, and the action that follows it is
 * not something the picture's own run began. */
static void TestPictureFinishesTheAction(void) {
	playerState_t rest, base2, shot, pred, before;
	pmove_t pm, model;
	script_t idle = Script(ACT_NONE, X);
	int i, j, sawReady = 0, sawAction = 0, handed = 0;
	Run(0, &idle, NONE);
	rest = svPs;
	CHECK(GunAtRest(&rest) && rest.ammo[GUN] > 1);
	memset(cmds, 0, sizeof(cmds));
	for (i = 0; i < 600; i++) { cmds[i].serverTime = rest.commandTime + 8 * (i + 1); cmds[i].weapon = BG_SLOT_COMMAND; }
	cmds[0].buttons = BUTTON_ATTACK;
	memset(&model, 0, sizeof(model)); FakeReloadModel(&model);
	CG_WeapPicture_Reset();
	/* frame 1: prediction fires */
	numCmds = 1;
	CG_WeapPicture_BeginReplay(&rest);
	shot = rest; memset(&pm, 0, sizeof(pm)); pm.ps = &shot; pm.cmd = cmds[0]; FakeReloadPmove(&pm);
	CG_WeapPicture_ReplayStep(&rest, &shot, NULL);
	CG_WeapPicture_Update(&shot, &model, 0, 0, GetCmd);
	CG_WeapPicture_Update(&shot, &model, 0, 0, GetCmd);
	CHECK(shot.weaponstate == WEAPON_FIRING && CG_WeapPicture_State(&shot) == &shot);
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
		CHECK(GunAtRest(&pred));
		if (pic != &pred) {
			if (BG_ShotAwaitsAction(pic)) sawReady = 1;
			if (pic->weaponstate == WEAPON_BOLTING) { CHECK(sawReady); sawAction = 1; }
		} else handed = 1;
	}
	/* handed back at rest, with the whole action drawn and no round spent */
	CHECK(handed && sawReady && sawAction && pred.ammo[GUN] == rest.ammo[GUN]);
}

/* The SF (the G19's select-fire switch) is worked as a bolt (WEAPON_BOLTING,
 * owner's rule), so the picture treats it as an action of the gun: one begun
 * on the command a G19 shot's time runs out, straight out of FIRING, is a new
 * action, not that shot's pump. A correction whose server fired but never
 * started the SF (the selector's press never reached it) then plays the SF you
 * were shown through, its own clip, to its end, and hands back at rest; it
 * never snaps it away because the shot was kept. Driven directly, as
 * TestPictureFinishesTheAction: frame 1 predicts the shot and the SF; frame
 * 2's snapshot is older than the shot, and its replay has the shot and no
 * SF. */
static void TestSFOutOfAShot(void) {
	bg_class_t cl = *BG_Class(0);
	playerState_t rest, pred, before;
	pmove_t pm, model;
	int i, j, k = -1, m, keep = GUN, sawSF = 0, handed = 0;
	GUN = WP_G19;
	memset(&rest, 0, sizeof(rest));
	cl.slot[0] = WP_G19; cl.slot[1] = WP_MACHINEGUN;
	cl.attachments[0] = BG_ATTACHMENT_SWITCH; cl.attachments[1] = 0;
	CHECK(BG_ValidateClass(&cl));
	BG_ApplyClass(&rest, &cl);
	rest.stats[STAT_HEALTH] = 100; rest.pm_type = PM_NORMAL; rest.weapon = WP_G19;
	rest.weaponstate = WEAPON_READY; rest.weaponAnimTime = -1; rest.commandTime = 1000;
	rest.viewheight = DEFAULT_VIEWHEIGHT; rest.origin[2] = 24.125f; rest.groundEntityNum = ENTITYNUM_WORLD;
	CHECK(GunAtRest(&rest) && BG_HasSelector(&rest, WP_G19) && !(rest.slotFlags[SLOT_PRIMARY] & BG_GUN_AUTO));
	memset(cmds, 0, sizeof(cmds));
	for (i = 0; i < 600; i++) { cmds[i].serverTime = rest.commandTime + 8 * (i + 1); cmds[i].weapon = BG_SLOT_COMMAND; }
	cmds[0].buttons = BUTTON_ATTACK;
	memset(&model, 0, sizeof(model)); FakeReloadModel(&model);
	/* the command the shot's time runs out on, the trigger let go */
	pred = rest;
	for (i = 0; i < 100 && k < 0; i++) {
		before = pred; memset(&pm, 0, sizeof(pm)); pm.ps = &pred; pm.cmd = cmds[i]; FakeReloadPmove(&pm);
		if (i > 0 && before.weaponstate == WEAPON_FIRING && pred.weaponstate != WEAPON_FIRING) k = i;
	}
	CHECK(k > 0);
	cmds[k].buttons = BUTTON_SELECTFIRE;
	m = k + 6;
	/* frame 1: prediction fires, and starts the SF straight out of the shot */
	CG_WeapPicture_Reset();
	numCmds = m + 1;
	CG_WeapPicture_BeginReplay(&rest);
	pred = rest;
	for (j = 0; j <= m; j++) {
		before = pred; memset(&pm, 0, sizeof(pm)); pm.ps = &pred; pm.cmd = cmds[j]; FakeReloadPmove(&pm);
		if (j == k) CHECK(before.weaponstate == WEAPON_FIRING && pred.weaponstate == WEAPON_BOLTING &&
			pred.weaponAnimSeq == ASEQ_SELECT_AUTO);
		CG_WeapPicture_ReplayStep(&before, &pred, NULL);
	}
	CG_WeapPicture_Update(&pred, &model, 0, m, GetCmd);
	CG_WeapPicture_Update(&pred, &model, 0, m, GetCmd);
	CHECK(pred.weaponstate == WEAPON_BOLTING && CG_WeapPicture_State(&pred) == &pred);
	/* then the server's: it fired and never started the SF */
	cmds[k].buttons = 0;
	for (i = m + 1; i < 600 && !handed; i++) {
		const playerState_t *pic;
		numCmds = i + 1;
		CG_WeapPicture_BeginReplay(&rest);
		pred = rest;
		for (j = 0; j <= i; j++) {
			before = pred; memset(&pm, 0, sizeof(pm)); pm.ps = &pred; pm.cmd = cmds[j]; FakeReloadPmove(&pm);
			CG_WeapPicture_ReplayStep(&before, &pred, NULL);
		}
		CG_WeapPicture_Update(&pred, &model, 0, i, GetCmd);
		pic = CG_WeapPicture_State(&pred);
		CHECK(pred.weaponstate != WEAPON_BOLTING && !(pred.slotFlags[SLOT_PRIMARY] & BG_GUN_AUTO));
		if (pic != &pred) {
			CHECK(pic->weaponstate == WEAPON_BOLTING || GunAtRest(pic));
			if (pic->weaponstate == WEAPON_BOLTING) { CHECK(pic->weaponAnimSeq == ASEQ_SELECT_AUTO); sawSF = 1; }
		} else {
			CHECK(i > m + 1);	/* never snapped away on the correction */
			handed = 1;
		}
	}
	CHECK(sawSF && handed);
	GUN = keep;
}

/* The shot's READY command is still the shot (BG_ShotAwaitsAction): a correction
 * whose snapshot base lands exactly on it keeps the shot the picture began.
 * No network desync in this model moves a shot by a command (every action
 * after the READY command is longer than any base age it makes), so drive the
 * picture directly, as TestSwapOnlyCorrection does: frame 1 predicts the shot
 * a command late; frame 2's snapshot is the server's shot a command earlier,
 * its base exactly on that shot's READY command and newer than every command,
 * so prediction is that READY command while the picture's own run, a command
 * behind, is still in the shot. Prediction kept the shot: the picture follows
 * it, and does not detach. The timings are the command spacing and the gun's
 * own shot, found by running to the READY command. */
static void TestCorrectionOnTheReadyCommand(void) {
	playerState_t rest, sv, shot, before;
	pmove_t pm, model;
	script_t idle = Script(ACT_NONE, X);
	int i, k;
	Run(0, &idle, NONE);
	rest = svPs;
	CHECK(GunAtRest(&rest) && rest.ammo[GUN] > 1);
	memset(cmds, 0, sizeof(cmds));
	for (i = 0; i < 600; i++) { cmds[i].serverTime = rest.commandTime + CL_FRAME * (i + 1); cmds[i].weapon = BG_SLOT_COMMAND; }
	cmds[1].buttons = BUTTON_ATTACK;
	memset(&model, 0, sizeof(model)); FakeReloadModel(&model);
	CG_WeapPicture_Reset();
	/* frame 1: prediction fires on command 1 */
	numCmds = 2;
	CG_WeapPicture_BeginReplay(&rest);
	shot = rest;
	for (i = 0; i < numCmds; i++) {
		before = shot; memset(&pm, 0, sizeof(pm)); pm.ps = &shot; pm.cmd = cmds[i]; FakeReloadPmove(&pm);
		CG_WeapPicture_ReplayStep(&before, &shot, NULL);
	}
	CG_WeapPicture_Update(&shot, &model, 0, numCmds - 1, GetCmd);
	CG_WeapPicture_Update(&shot, &model, 0, numCmds - 1, GetCmd);
	CHECK(shot.weaponstate == WEAPON_FIRING && CG_WeapPicture_State(&shot) == &shot);
	/* the server fired on command 0: its run to that shot's READY command */
	sv = rest;
	for (k = 0; k < 600; k++) {
		memset(&pm, 0, sizeof(pm)); pm.ps = &sv; pm.cmd = cmds[k];
		if (k == 0) pm.cmd.buttons |= BUTTON_ATTACK;
		FakeReloadPmove(&pm);
		if (BG_ShotAwaitsAction(&sv)) break;
	}
	CHECK(k > 1 && k < 600 && sv.commandTime == cmds[k].serverTime);
	/* frame 2: that snapshot, with no command after it */
	numCmds = k + 1;
	CG_WeapPicture_BeginReplay(&sv);
	CG_WeapPicture_Update(&sv, &model, 0, numCmds - 1, GetCmd);
	CHECK(CG_WeapPicture_State(&sv) == &sv);
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


/* ---- MW2's burstload: the fake reload a burst makes -------------------------
 *
 * Nothing names it; no gun has a case of its own. While a burst is pending
 * MW2's PM_Weapon runs none of its checks (iw4x 0x44C444-0x44C4B3), and the
 * reload check is the only place its Pmove reads or clears the use/reload bit
 * (0x5786E3-0x5786F6). So an X pressed inside a burst is cleared, on the
 * server, only by the use code after the first command X is up on; the
 * client's copy, brought by a snapshot taken while X was down, rides the rest
 * of the burst through the replay and is taken on the command after the last
 * shot leaves, out of the shot (PM_BeginWeaponReload takes FIRING, 0x575E92).
 * X let go before the last shot's command: the server never reloads and the
 * client plays a reload as the burst finishes. X still down on it: a real
 * reload on that same command, on both sides. +reload is the weapon's own
 * button, read by the same check as a level (0x5786CD): never a fake.
 *
 * The gun here is the M16 rifle, a 3-Round Burst in its weapon.json; every
 * length is read from the game (the fire type's limit, the shot's own
 * commands, the reload's length), and every press is counted from the command
 * the burst's last shot left on at that snapshot phase, found by running the
 * phase once with the trigger alone (the client's clock drifts a millisecond
 * a snapshot, so the shots move by a command with the phase).
 *
 * FAKERELOAD_BURSTLOAD=1 prints the windows at 8, 16 and 33ms commands instead
 * of checking (FAKERELOAD_MAXPACKETS and FAKERELOAD_ONLINEGAME apply);
 * FAKERELOAD_BURSTLOAD=check runs these checks alone. */
static int BurstShots(void) {	/* the fire type's limit: the shots of one pull */
	int n = 1;
	while (!BG_ShotLimitReached(BG_WeaponFireType(GUN), n)) n++;
	return n;
}
static void SetFrame(int ms) { CL_FRAME = ms; hostHold = ms; }	/* your own host holds its snapshot a frame */
static int TrigIdx(void) { int i; for (i = 0; i < numCmds; i++) if (cmds[i].buttons & BUTTON_ATTACK) return i; return NONE; }
static int CmdIdx(int serverTime) { int i; for (i = 0; i < numCmds; i++) if (cmds[i].serverTime == serverTime) return i; return NONE; }
static int Lcm(int a, int b) { return a / Gcd(a, b) * b; }
/* The frame the trigger goes down on, `shift` into the snapshots' cycle. */
static int BurstAt(int r, int shift) {
	int wait = Settle(rtts[r]);
	startShift = shift;
	return ((wait > 1500 ? wait : 1500) + shift + CL_FRAME - 1) / CL_FRAME * CL_FRAME;
}
/* One run: the trigger down on the frame at `at` for fireCmds commands; the
 * reload button down on commands press..release-1, counted from the
 * trigger's (never, when release <= press); aim held from command adsCmd. */
static void BurstRun(int r, int at, int button, int press, int release, int fireCmds, int adsCmd) {
	script_t s = Script(ACT_NONE, button);
	s.fireAt = 0; s.fireLen = fireCmds * CL_FRAME;
	if (release > press) { s.reloadAt = press * CL_FRAME; s.reloadLen = (release - press) * CL_FRAME; }
	if (adsCmd != NONE) s.adsAt = adsCmd * CL_FRAME;
	Run(rtts[r], &s, at);
}
/* The snapshot phases a burst is tried over: every frame of one cycle of the
 * snapshots against the client's frames (a quarter of them where a cycle has
 * a hundred, online at 33ms). */
static int BurstSpan(int r) { return Lcm(SnapInterval(rtts[r]), CL_FRAME); }
static int BurstStep(int r) { int n = BurstSpan(r) / CL_FRAME; return n > 50 ? CL_FRAME * (n / 25) : CL_FRAME; }
/* The burst alone at this phase: the pull's shots and no more on the server,
 * the first on the trigger's own command; no reload on either side; the
 * picture never leaves prediction. Returns the command the last shot left on,
 * counted from the trigger's. */
#define BURST_PHASES 128
static int lastShotAt[BURST_PHASES], lastShotKnown[BURST_PHASES];
static int LastShot(int r, int shift) {
	int k = shift / CL_FRAME, t;
	CHECK(k < BURST_PHASES);
	if (!lastShotKnown[k]) {
		int n = BurstShots();
		BurstRun(r, BurstAt(r, shift), X, 0, 0, 1, NONE);
		t = TrigIdx();
		CHECK(n >= 2 && n <= 4 && O.svShots == n && CmdIdx(O.svRoundCmd[0]) == t);
		CHECK(!O.svReload && !O.predReload && !O.detachedMs && O.converged);
		CHECK(O.svMinAmmo == startAmmo - n && svPs.ammo[GUN] == startAmmo - n);
		lastShotAt[k] = CmdIdx(O.svRoundCmd[n - 1]) - t; lastShotKnown[k] = 1;
		CHECK(lastShotAt[k] >= n - 1);
	}
	return lastShotAt[k];
}
static void ForgetBursts(void) { memset(lastShotKnown, 0, sizeof(lastShotKnown)); }
/* A reload predicted that the server never began ... */
static int Appears(void) { return O.predReload && !O.svReload; }
/* ... and drawn for most of its length: the picture kept it. */
static int Sticks(void) { return Appears() && O.reloadPicMs >= BG_WeaponReloadTime(GUN) / 2; }
typedef struct {
	int n, appears, sticks, real, nothing;
	int stickShift;				/* a phase at which it stuck (NONE: none) */
	int shownMin;				/* the least a reload that stuck was drawn for */
	char outcome[BURST_PHASES];	/* each phase's: R real, F a fake that stuck, f one that did not, '.' nothing */
} burstRate_t;
static int Pct(int k, int n) { return 100 * k / n; }
/* What every run obeys, whatever was pressed when (the trigger on command 0;
 * the reload button on commands press..release-1; L the last shot's command):
 *  - the server reloads if and only if the button is down on the first
 *    command its reload check is run again: for X, whose bit the use code
 *    sets after a command, the last shot's own (L); for +reload, read as a
 *    level by the check itself, the one after (L + 1); and that reload begins
 *    on the command after the last shot's, on both sides;
 *  - a reload the server never began begins, in prediction, on that same
 *    command, the bit having ridden the burst (never with +reload);
 *  - with no reload of its own the server fired the pull's shots and no more
 *    and lost those rounds and no others;
 *  - both sides agree at the end, and prediction never aims while reloading. */
static void BurstRules(int button, int press, int release, int L) {
	int n = BurstShots(), t = TrigIdx(), U = cmds[t + L + 1].serverTime;
	qboolean down = release > press && press <= (button == X ? L : L + 1) && release > (button == X ? L : L + 1);
	CHECK(O.converged && !O.predAdsWhileReloading);
	CHECK(O.svShots >= n && CmdIdx(O.svRoundCmd[n - 1]) == t + L);	/* the burst was never held back or cut */
	CHECK(O.svReload == down);
	if (O.svReload) CHECK(O.svReloadCmd == U && O.predReload && O.predReloadCmd == U);
	else CHECK(O.svShots == n && O.svMinAmmo == startAmmo - n && svPs.ammo[GUN] == startAmmo - n);
	if (Appears()) CHECK(button == X && O.predReloadCmd == U);
	if (button == R) CHECK(O.predReload == O.svReload && !O.detachedMs);
}
/* Over every phase: the reload button down from command `press` after the
 * trigger's (mode 0) or for `press` commands (mode 1), first up `up` commands
 * after the last shot's (negative: before it); the trigger down for fireCmds
 * commands. */
static burstRate_t BurstRate(int r, int button, int mode, int press, int up, int fireCmds) {
	burstRate_t q;
	int shift, span = BurstSpan(r), step = BurstStep(r);
	memset(&q, 0, sizeof(q)); q.stickShift = NONE; q.shownMin = 1 << 30;
	for (shift = 0; shift < span; shift += step) {
		int L = LastShot(r, shift), at = BurstAt(r, shift), release = L + up, first = mode ? release - press : press;
		BurstRun(r, at, button, first, release, fireCmds, NONE);
		BurstRules(button, first, release, L);
		q.outcome[q.n++] = O.svReload ? 'R' : Sticks() ? 'F' : Appears() ? 'f' : '.';
		q.real += O.svReload; q.appears += Appears(); q.sticks += Sticks();
		q.nothing += !O.svReload && !O.predReload;
		if (Sticks()) { q.stickShift = shift; if (O.reloadPicMs < q.shownMin) q.shownMin = O.reloadPicMs; }
	}
	startShift = 0;
	return q;
}

/* The burstload at one command length: on your own host, then with ping. */
static void BurstloadsAt(int frame) {
	int r, j, c, n = BurstShots(), reload = BG_WeaponReloadTime(WP_M16);
	burstRate_t q, held, sure;

	SetFrame(frame);
	runAfter = reload + 1500;

	/* ---- your own host ---- */
	ForgetBursts();
	c = LastShot(0, 0) / (n - 1);	/* the commands a shot takes at this phase */
	/* X after the trigger, let go just before the last shot: the reload plays
	 * as the burst finishes, at every phase, and the server reloads nothing
	 * (BurstRules: it begins on the command after the last shot's, and the
	 * server fires its three and keeps its rounds). Sure for a release within
	 * the clock's backoff and the host's held frame of that command, as the
	 * fake reload after a knife is. */
	q = sure = BurstRate(0, X, 0, 1, 0, 1);
	CHECK(sure.appears == sure.n && !sure.real);
	for (j = 1; j * CL_FRAME <= CL_TIME_BACKOFF + hostHold - CL_FRAME; j++) {
		q = BurstRate(0, X, 0, 1, -j, 1);
		CHECK(q.appears == q.n);
	}
	/* earlier, it falls with the snapshot phase, and never rises again ... */
	for (; j <= c * (n - 1) - 2; j++) {
		burstRate_t before = q;
		q = BurstRate(0, X, 0, 1, -j, 1);
		CHECK(q.appears <= before.appears && q.sticks <= before.sticks && !q.real);
		/* ... to nothing at all, on either side, once the release is the
		 * clock's whole lag before it: a tap wholly inside the burst and up
		 * early is lost (the burst is long enough to show that at 8ms) */
		if (j * CL_FRAME >= CL_TIME_BACKOFF + hostHold + snapInterval + 2 * CL_FRAME) CHECK(q.nothing == q.n);
	}
	if (frame == 8) CHECK(q.nothing == q.n);	/* the last one tried: X down on the command after the trigger's alone */
	/* X with the trigger, on its command: the shot goes first (the use code
	 * runs after the command's Pmove), and from there it is the same. */
	q = BurstRate(0, X, 0, 0, 0, 1);
	CHECK(q.appears == q.n && !memcmp(q.outcome, sure.outcome, q.n));
	/* The trigger held throughout changes nothing: the lock and the pending
	 * pull are no part of the reload's read. */
	held = BurstRate(0, X, 0, 1, 0, 3000 / CL_FRAME);
	CHECK(!memcmp(held.outcome, sure.outcome, sure.n));
	/* X still down on the last shot's command is a real reload on the command
	 * after it, out of the shot, on both sides (BurstRules), and so is X held
	 * on: the reload as the burst ends. */
	q = BurstRate(0, X, 0, 1, 1, 1); CHECK(q.real == q.n);
	q = BurstRate(0, X, 0, 1, 2 * c, 1); CHECK(q.real == q.n);
	/* X before the trigger, the magazine not full: the reload, not the burst. */
	{
		int L = LastShot(0, 0);
		BurstRun(0, BurstAt(0, 0), X, -2, L, 1, NONE);
		CHECK(O.svReload && !O.svShots && O.predReload && O.converged);
		startShift = 0;
	}
	/* +reload is the weapon's own button, read by the same check as a level:
	 * down on the command after the last shot's it is a real reload there, on
	 * both sides at once; let go by then it is nothing; never a fake, never a
	 * picture of its own (BurstRules). */
	q = BurstRate(0, R, 0, 1, 2, 1); CHECK(q.real == q.n);
	q = BurstRate(0, R, 0, 1, 1, 1); CHECK(q.nothing == q.n);
	q = BurstRate(0, R, 0, 1, 0, 1); CHECK(q.nothing == q.n);
	q = BurstRate(0, R, 0, 1, -1, 1); CHECK(q.nothing == q.n);
	/* Whether the reload you were shown then plays out is the picture's
	 * rule, and here MW2's viewmodel's: a correction that finds prediction
	 * still in the last shot takes the picture back to it (MW2 restarts its
	 * fire animation over the reload's, WeaponRunXModelAnims 0x59B5E0), one
	 * that finds the gun READY in its cooldown leaves the reload playing. So
	 * the snapshot that brought the bit must stay the base until the last
	 * shot's time has run out: on your own host, where snapshots are closer
	 * together than a shot is long at 8ms, some of the time, never none. */
	CHECK(sure.sticks > 0 && sure.stickShift != NONE);
	if (frame == 8) CHECK(sure.sticks < sure.n && c * CL_FRAME > snapInterval);
	/* one that sticks is drawn from the frame it began to its end */
	CHECK(sure.shownMin >= reload - 2 * CL_FRAME);
	/* ... and you can aim through it: no gate is loosened (prediction, once
	 * corrected, is not reloading, and never aimed while it was) */
	{
		int L = LastShot(0, sure.stickShift), at = BurstAt(0, sure.stickShift);
		BurstRun(0, at, X, 1, L, 1, L + 2 * c);
		BurstRules(X, 1, L, L);
		CHECK(Sticks() && O.adsUnderReloadMs > reload / 2);
		startShift = 0;
	}
	printf("  burstload, %2dms commands, your own host: X let go on the last shot's command plays the reload at %d%% of phases, and it plays out at %d%%\n",
		CL_FRAME, Pct(sure.appears, sure.n), Pct(sure.sticks, sure.n));

	/* ---- with ping ---- */
	for (r = 1; r <= 2; r++) {
		int L, at, span;
		ForgetBursts();
		/* X down from the command after the trigger's to the last shot's:
		 * down across one of the server's snapshots (they come every 100ms,
		 * and the burst is two shot times long), it always appears, and the
		 * base that brought it is old enough to outlast the last shot: it
		 * always plays out. Where the commands make that press a hair
		 * shorter than a snapshot interval (33ms), nearly always. */
		q = BurstRate(r, X, 0, 1, 0, 1);
		span = (LastShot(r, 0) - 1) * CL_FRAME;
		if (span >= RemoteInterval()) CHECK(q.sticks == q.n);
		else CHECK(q.sticks * 10 >= q.n * 9 && q.sticks == q.appears);
		CHECK(q.shownMin >= reload - RemoteInterval() - rtts[r] - 4 * CL_FRAME);
		printf("  burstload, %2dms commands, RTT %3d: X from the command after the trigger's to the last shot's plays out at %d%% of phases",
			CL_FRAME, rtts[r], Pct(q.sticks, q.n));
		/* a tap one shot long is seen only where a snapshot falls in it */
		q = BurstRate(r, X, 1, c, 0, 1);
		CHECK(q.sticks > 0 && q.sticks < q.n);
		printf(", a %dms tap at %d%%\n", c * CL_FRAME, Pct(q.sticks, q.n));
		/* held past the last shot: the real reload as the burst ends */
		q = BurstRate(r, X, 0, 1, 1, 1); CHECK(q.real == q.n);
		/* +reload never fakes, with ping either */
		q = BurstRate(r, R, 0, 1, 2, 1); CHECK(q.real == q.n);
		q = BurstRate(r, R, 0, 1, 1, 1); CHECK(q.nothing == q.n);

		/* Through one that plays out (the first phase's does): */
		L = LastShot(r, 0); at = BurstAt(r, 0);
		runAfter = 2 * reload + 2500;
		/* ... you can aim. No gate is loosened: prediction, once corrected,
		 * is not reloading, and never aimed while it was. */
		BurstRun(r, at, X, 1, L, 1, L + 2 * c);
		BurstRules(X, 1, L, L);
		CHECK(Sticks() && O.adsUnderReloadMs > reload / 2);
		/* ... a pull of the trigger fires a real burst, which takes the picture ... */
		{
			script_t s = Script(ACT_NONE, X);
			s.fireAt = 0; s.fireLen = CL_FRAME; s.reloadAt = CL_FRAME; s.reloadLen = (L - 1) * CL_FRAME;
			s.fire2At = reload / 2; s.fire2Len = CL_FRAME;
			Run(rtts[r], &s, at);
			CHECK(Appears() && O.svShots == 2 * n && O.detachedMs > 0 && !O.detachedWhileFiring && O.converged);
			CHECK(svPs.ammo[GUN] == startAmmo - 2 * n);
			/* ... and X is a real reload. */
			s.fire2At = NONE; s.fire2Len = 0; s.reload2At = reload / 2; s.reload2Len = RemoteInterval();
			Run(rtts[r], &s, at);
			CHECK(O.svReload && O.detachedMs > 0 && O.svReloadCmd - O.predReloadCmd >= reload / 4 && O.converged);
			CHECK(O.svShots == n && svPs.ammo[GUN] == BG_WeaponMagSize(GUN));
		}
		runAfter = reload + 1500;
		startShift = 0;
	}
}
static void Burstloads(void) {
	int keepFrame = CL_FRAME, keepHold = hostHold, keepAfter = runAfter, keepGun = GUN;
	if (maxPackets || SV_FRAME != 50) return;	/* the shipped rates, a packet a frame */
	GUN = WP_M16;
	CHECK(BG_WeaponFireLength(GUN) == 0 && BurstShots() == 3);
	BurstloadsAt(8);
	BurstloadsAt(16);
	BurstloadsAt(33);
	CL_FRAME = keepFrame; hostHold = keepHold; runAfter = keepAfter; GUN = keepGun;
	startShift = 0;
}

static void BurstloadTable(void) {
	static const int frames[] = { 8, 16, 33 };
	int f, r, j;
	for (f = 0; f < 3; f++) {
		SetFrame(frames[f]);
		printf("burstload, %dms commands (%s): X down from the command after the trigger's, first up j commands before the last shot's\n",
			CL_FRAME, maxPackets ? "cl_maxpackets batching online" : "a packet a frame");
		for (r = 0; r < 4; r++) {
			int L, c;
			ForgetBursts();
			L = LastShot(r, 0); c = L / (BurstShots() - 1);
			printf("  RTT %3d%s, %d phases:\n    j        ", rtts[r], r ? "" : " (your own host)", BurstSpan(r) / BurstStep(r));
			for (j = -1; j <= L - 2; j++) printf(" %4d", j);
			printf("\n    appears  ");
			for (j = -1; j <= L - 2; j++) {
				burstRate_t q = BurstRate(r, X, 0, 1, -j, 1);
				if (q.real) printf(" %4s", q.real == q.n ? "real" : "r/f"); else printf(" %4d", Pct(q.appears, q.n));
				fflush(stdout);
			}
			printf("\n    sticks   ");
			for (j = -1; j <= L - 2; j++) {
				burstRate_t q = BurstRate(r, X, 0, 1, -j, 1);
				if (q.real) printf(" %4s", q.real == q.n ? "real" : "r/f"); else printf(" %4d", Pct(q.sticks, q.n));
				fflush(stdout);
			}
			printf("\n    X with the trigger, held: tapped %d%% sticks, a %d-command tap up on the last shot's command %d%%, +reload down on the command after it: %d%% real\n",
				Pct(BurstRate(r, X, 0, 0, 0, 600).sticks, BurstRate(r, X, 0, 0, 0, 600).n), c,
				Pct(BurstRate(r, X, 1, c, 0, 1).sticks, BurstRate(r, X, 1, c, 0, 1).n),
				Pct(BurstRate(r, R, 0, 1, 2, 1).real, BurstRate(r, R, 0, 1, 2, 1).n));
		}
	}
}

/* ---- MW2's skree: the NAC that X takes away ---------------------------------
 *
 * The owner's name for it. Nothing names it; no gun has a case of its own.
 * During a bolt, a pump or the SF (the G19's select-fire switch, worked as a
 * bolt), X pressed at least a frame before the Y whose first command lands on
 * the close note's command: Y reaches the commands a frame after its press, so
 * X's command is two or more before Y's first. X is a reload only the server
 * hears. Its use code sets the bit after X's command (MW2's
 * Player_UpdateActivate, 0x4D78F0, called at 0x4939A1), and the next command
 * takes it out of the action (step 4e; MW2's PM_BeginWeaponReload takes
 * RECHAMBERING). So on Y's command no close note lands, and the change check
 * stamps a whole holster: a plain swap, the pump or bolt still owed (the SF
 * lost, its mode as it was), the reload lost, the magazine as it was. The
 * client, never told of the bit, predicts the close note and the NAC and draws
 * the next gun from Y's command until a snapshot taken after the server ran
 * X's command is its base; then prediction is the old gun going down, and the
 * picture follows at once (nothing detaches). X so early that this snapshot is
 * the base by Y's command draws no next gun, only a plain holster, with the
 * reload start before it once the snapshot is the base on the frame Y is
 * pressed on. X with Y (its command the one before Y's first) or after it is
 * a real NAC on both sides: the change check runs before the reload check
 * (MW2 0x44C4D7, 0x44C4DE), and the bit then finds a gun going down or coming
 * up, which takes no reload (the next gun has rounds to load: its magazine is
 * not full). A G19 with a full magazine has nothing to reload: every X is a
 * real SF NAC.
 *
 * Counted as the burstload is: every command from the action's own (the
 * trigger's, the selector's for the SF) at that snapshot phase, Y first seen
 * on its close note's command C, found by running the phase once with the
 * action alone. What is pinned is gameplay; how the old gun's picture comes
 * back (where its drop clip starts) is the owner's to change.
 *
 * FAKERELOAD_SKREE=check runs these checks alone. */
static const struct { int weapon, attachment, full; const char *name; } skreeGuns[] = {
	{ WP_M40A3, 0, 0, "M300 bolt" },
	{ WP_SHOTGUN, 0, 0, "SPAS pump" },
	{ WP_G19, BG_ATTACHMENT_SWITCH, 0, "G19 SF" },
	{ WP_G19, BG_ATTACHMENT_SWITCH, 1, "G19 SF, full magazine" }
};
#define SKREE_GUNS ((int)(sizeof(skreeGuns) / sizeof(skreeGuns[0])))
/* The action's own command: the trigger's, the selector's for the SF. */
static int ActionIdx(void) {
	int i;
	for (i = 0; i < numCmds; i++) if (cmds[i].buttons & (BUTTON_ATTACK | BUTTON_SELECTFIRE)) return i;
	return NONE;
}
/* One run: the action on the frame at `at`; Y first seen on command y,
 * pressed on the frame before (NONE: no Y); X down on commands x..x+xLen-1
 * (NONE: no X); every command counted from the action's. */
static void SkreeRun(int r, int at, int y, int x, int xLen) {
	script_t s = Script(ACT_NONE, X);
	if (gunAttachment) { s.selectAt = 0; s.selectLen = CL_FRAME; }
	else { s.fireAt = 0; s.fireLen = CL_FRAME; }
	if (y != NONE) s.swapAt = (y - 1) * CL_FRAME;
	if (x != NONE) { s.reloadAt = x * CL_FRAME; s.reloadLen = xLen * CL_FRAME; }
	Run(rtts[r], &s, at);
}
/* The action alone at this phase, as LastShot is for the burst: S, the first
 * command it is under way (the gun BOLTING), and C, the command its close
 * note crosses (the pump or bolt paid, the SF's mode committed), both counted
 * from the action's command; and how old prediction's base got. Nothing
 * reloads or detaches, and the action ends closed. */
static int skreeS[BURST_PHASES], skreeC[BURST_PHASES], skreeAge[BURST_PHASES], skreeKnown[BURST_PHASES];
static int CloseNote(int r, int shift, int *start, int *ageMax) {
	int k = shift / CL_FRAME;
	CHECK(k < BURST_PHASES);
	if (!skreeKnown[k]) {
		int a, i, open = NONE;
		SkreeRun(r, BurstAt(r, shift), NONE, NONE, 0);
		a = ActionIdx();
		CHECK(a != NONE && O.converged && !O.svReload && !O.predReload && !O.detachedMs);
		CHECK(svPs.weapon == GUN && svPs.weaponstate == WEAPON_READY && ActionClosed(&svPs));
		skreeS[k] = skreeC[k] = NONE;
		for (i = a; i < numCmds && skreeC[k] == NONE; i++) {
			if (!svAt[i].ran) continue;
			if (skreeS[k] == NONE && svAt[i].state == WEAPON_BOLTING) skreeS[k] = i - a;
			if (open == NONE && !svAt[i].closed) open = i;
			else if (open != NONE && svAt[i].closed) skreeC[k] = i - a;
		}
		CHECK(skreeS[k] != NONE && skreeC[k] != NONE && skreeS[k] < skreeC[k]);
		skreeAge[k] = clk.ageMax;
		skreeKnown[k] = 1;
	}
	*start = skreeS[k]; *ageMax = skreeAge[k];
	return skreeC[k];
}
static void ForgetSkrees(void) { memset(skreeKnown, 0, sizeof(skreeKnown)); }
/* Y alone at this phase, first seen on the close note's command: the server
 * command by command, which is what the client draws until it knows of X. */
static struct { int ran, weapon, state, serverTime; } yAlone[MAXQ];
typedef struct { int at, a, S, C, ageMax; } skreePhase_t;
static skreePhase_t SkreePhase(int r, int shift) {
	skreePhase_t p;
	int i;
	p.C = CloseNote(r, shift, &p.S, &p.ageMax);
	p.at = BurstAt(r, shift);
	SkreeRun(r, p.at, p.C, NONE, 0);
	p.a = ActionIdx();
	for (i = 0; i < numCmds; i++) {
		yAlone[i].ran = svAt[i].ran; yAlone[i].weapon = svAt[i].weapon; yAlone[i].state = svAt[i].state;
		yAlone[i].serverTime = cmds[i].serverTime;
	}
	return p;
}
typedef struct { int nac, flash, shownMs, reloadShown; } skree_t;
/* What every run with Y first seen on the close note's command obeys (y =
 * a + C, Y's first command; x, X's one command, or NONE):
 *  - Y is a swap: the next gun is in hand at the end, both sides agree, and
 *    the picture never leaves prediction;
 *  - the pump or bolt is still owed (the SF lost, its mode as it was: the
 *    owner's rule) and the gun's magazine is as the action left it;
 *  - the server: a NAC, the next gun in hand on Y's command, unless X's
 *    command is two or more before Y's and there is a round to load. Then it
 *    took the reload out of the action on the command after X's, and on Y's
 *    the change check put the gun down whole: the next gun once the gun's
 *    whole drop time has run, and nothing filled;
 *  - the client: on every frame whose prediction base the server took after
 *    X's command, it draws what the server did on that frame's command; on
 *    every frame before, what the server did with Y alone (it cannot know of
 *    X). So in a NAC it is never corrected; in the server's whole holster it
 *    draws the next gun on Y's command exactly when its base had not yet run
 *    X's command, and then, on the first frame whose base has, the old gun
 *    going down. */
static skree_t SkreeRules(const skreePhase_t *p, int x) {
	skree_t q;
	int i, k, y = p->a + p->C, xTime = x == NONE ? 1 << 30 : cmds[x].serverTime;
	bg_weaponInstance_t g;
	memset(&q, 0, sizeof(q));
	CHECK(ActionIdx() == p->a && O.converged && !O.detachedMs && svPs.weapon == OTHER_GUN);
	BG_GetSlot(&svPs, SLOT_PRIMARY, &g);
	CHECK(!ActionClosed(&svPs) && O.svShots == (gunAttachment ? 0 : 1) && g.ammo == startAmmo - O.svShots);
	CHECK(svAt[y].ran && svAt[y - 1].ran);
	for (i = p->a; i < numCmds; i++) {
		CHECK(cmds[i].serverTime == yAlone[i].serverTime);
		if (!svAt[i].ran || !yAlone[i].ran) continue;
		if (clAt[i].baseTime >= xTime) CHECK(clAt[i].picWeapon == svAt[i].weapon && clAt[i].picState == svAt[i].state);
		else CHECK(clAt[i].picWeapon == yAlone[i].weapon && clAt[i].picState == yAlone[i].state);
	}
	CHECK(yAlone[y].weapon == OTHER_GUN);	/* Y alone is the NAC */
	q.nac = svAt[y].weapon == OTHER_GUN;
	if (x == NONE || x >= y - 1 || fullMag) {
		CHECK(q.nac && !O.svReload && !O.predReload);
		return q;
	}
	/* the reload out of the action, then Y's whole holster */
	CHECK(!q.nac && x >= p->a + p->S && svAt[x].state == WEAPON_BOLTING);
	CHECK(svAt[x + 1].weapon == GUN && svAt[x + 1].state == WEAPON_RELOADING && O.svReload);
	CHECK(svAt[y - 1].state == WEAPON_RELOADING && svAt[y].weapon == GUN && svAt[y].state == WEAPON_DROPPING);
	for (i = y; i < numCmds && svAt[i].weapon == GUN; i++) CHECK(svAt[i].state == WEAPON_DROPPING);
	/* the next gun once the gun's whole drop time has run, within a command */
	CHECK((i - y + 1) * CL_FRAME >= BG_WeaponDropTime(GUN) && (i - y - 1) * CL_FRAME < BG_WeaponDropTime(GUN));
	q.flash = clAt[y].picWeapon == OTHER_GUN;
	if (q.flash) {
		for (k = y; clAt[k].picWeapon == OTHER_GUN; k++) ;
		CHECK(clAt[k].picWeapon == GUN && clAt[k].picState == WEAPON_DROPPING);
		CHECK(clAt[k].baseTime >= xTime && clAt[k - 1].baseTime < xTime);
		q.shownMs = (k - y) * CL_FRAME;
		/* prediction learned of the reload only once Y's command was made */
		CHECK(!O.predReload);
	} else {
		q.reloadShown = clAt[y - 1].baseTime >= xTime;
		CHECK(q.reloadShown == O.predReload);
	}
	return q;
}

/* X a frame before Y (X's command two before Y's first): the server's whole
 * holster at every phase; the client's next gun on Y's command, drawn until
 * its base has run X's command: at least a frame, at least the round trip less
 * those two commands, no longer than the base was ever old less a command,
 * and no longer than MW2's clock lets the base lag (TestClockBase): its
 * backoff, a snapshot interval, and the round trip, or on your own host the
 * frame the host holds its snapshot. Returns how many phases drew it, and the
 * shortest and longest it was drawn. */
static int TestSkreeXAFrameBeforeY(int r, int *shownMin, int *shownMax, int *phases) {
	int shift, n = 0, flashes = 0, span = BurstSpan(r), step = BurstStep(r);
	*shownMin = 1 << 30; *shownMax = -1;
	for (shift = 0; shift < span; shift += step) {
		skreePhase_t p = SkreePhase(r, shift);
		int y = p.a + p.C;
		skree_t q;
		SkreeRun(r, p.at, p.C, p.C - 2, 1);
		q = SkreeRules(&p, y - 2);
		n++;
		CHECK(!q.nac);
		if (!q.flash) continue;
		flashes++;
		CHECK(q.shownMs >= CL_FRAME && q.shownMs >= rtts[r] - 2 * CL_FRAME);
		CHECK(q.shownMs + 2 * CL_FRAME <= p.ageMax + CL_FRAME);
		CHECK(q.shownMs <= CL_TIME_BACKOFF + SnapInterval(rtts[r]) + (rtts[r] ? rtts[r] : hostHold));
		if (q.shownMs < *shownMin) *shownMin = q.shownMs;
		if (q.shownMs > *shownMax) *shownMax = q.shownMs;
	}
	startShift = 0;
	*phases = n;
	return flashes;
}
/* Y alone, X with Y (its command the one before Y's first), and X one and two
 * frames after Y: a real NAC on both sides, at every phase. The next gun has
 * rounds to load (otherPartial), so X during its raise is refused by the
 * gun's state, not by a full magazine. */
static void TestSkreeXWithOrAfterY(int r) {
	int shift, dx, span = BurstSpan(r), step = BurstStep(r);
	for (shift = 0; shift < span; shift += step) {
		skreePhase_t p = SkreePhase(r, shift);
		CHECK(SkreeRules(&p, NONE).nac);
		for (dx = -1; dx <= 1; dx++) {
			SkreeRun(r, p.at, p.C, p.C + dx, 1);
			CHECK(SkreeRules(&p, p.a + p.C + dx).nac);
		}
	}
	startShift = 0;
}
/* The commands before Y's first that X must be for its snapshot to be the
 * base by the frame before Y's: more than the base is ever old, and that
 * frame. */
static int SkreeFar(const skreePhase_t *p) { return (p->ageMax + CL_FRAME - 1) / CL_FRAME + 2; }
/* How many commands after the one it starts on the gun's reload first does
 * something (pays the owed pump or bolt, as MW2's reload delayed action does,
 * or fills a round), at this phase: X on the action's first command, no Y. An
 * X so early that this lands by Y's command is a reload Y cuts short (or, on
 * Y's command, a NAC of its own), not the skree. */
static int ReloadEffect(int r, const skreePhase_t *p) {
	int i, start = NONE;
	SkreeRun(r, p->at, NONE, p->S, 1);
	CHECK(ActionIdx() == p->a && O.svReload && O.converged);
	for (i = p->a + p->S; i < numCmds; i++) {
		if (!svAt[i].ran) continue;
		if (start == NONE) { if (svAt[i].state == WEAPON_RELOADING) start = i; continue; }
		if (svAt[i].closed != svAt[start].closed || svAt[i].ammo != svAt[start].ammo) return i - start;
	}
	CHECK(!"a reload that neither pays the action nor fills a round");
	return 0;
}
/* X long before Y. At each phase, the earlier X is, the sooner the client
 * knows: once X is early enough that the next gun is not drawn, it is not
 * drawn for any earlier X. And X more than the base is ever old before the
 * frame before Y's draws the reload start, then a plain holster, at every
 * phase. With `sweep` every command from two before Y's to that one is tried;
 * else that one alone. Only where that X is inside the action and the reload
 * it starts has done nothing yet by Y's command (ReloadEffect). Returns how
 * many phases were tried and the earliest X tried, and with `sweep` the
 * first X that drew no next gun at the luckiest and the unluckiest phase,
 * all in ms before Y's press (the frame before Y's first command). */
static int TestSkreeXLongBeforeY(int r, int sweep, int *farMs, int *clearMin, int *clearMax) {
	int shift, n = 0, span = BurstSpan(r), step = BurstStep(r);
	*farMs = 0; *clearMin = 1 << 30; *clearMax = -1;
	for (shift = 0; shift < span; shift += step) {
		skreePhase_t p = SkreePhase(r, shift);
		int k, far = SkreeFar(&p), drawn = 1;
		if (p.C - far < p.S || ReloadEffect(r, &p) < far) continue;
		for (k = sweep ? 2 : far; k <= far; k++) {
			skree_t q;
			SkreeRun(r, p.at, p.C, p.C - k, 1);
			q = SkreeRules(&p, p.a + p.C - k);
			CHECK(!q.nac && (!q.flash || drawn));
			if (sweep && drawn && !q.flash) {
				if ((k - 1) * CL_FRAME < *clearMin) *clearMin = (k - 1) * CL_FRAME;
				if ((k - 1) * CL_FRAME > *clearMax) *clearMax = (k - 1) * CL_FRAME;
			}
			drawn = q.flash;
			if (k == far) CHECK(!q.flash && q.reloadShown);
		}
		if ((far - 1) * CL_FRAME > *farMs) *farMs = (far - 1) * CL_FRAME;
		n++;
	}
	startShift = 0;
	return n;
}
/* The G19 with a full magazine: X anywhere in the SF, before Y, with it or
 * after it, is a real SF NAC on both sides: there is nothing to reload. */
static void TestSkreeFullMagazineSF(int r) {
	int shift, dx, span = BurstSpan(r), step = BurstStep(r);
	CHECK(fullMag && gunAttachment == BG_ATTACHMENT_SWITCH);
	for (shift = 0; shift < span; shift += step) {
		skreePhase_t p = SkreePhase(r, shift);
		CHECK(SkreeRules(&p, NONE).nac);
		for (dx = -SkreeFar(&p); dx <= 1; dx++) {
			if (p.C + dx < p.S) continue;
			SkreeRun(r, p.at, p.C, p.C + dx, 1);
			CHECK(SkreeRules(&p, p.a + p.C + dx).nac && startAmmo == BG_WeaponMagSize(GUN));
		}
	}
	startShift = 0;
}

/* The skree at one command length, each gun: on your own host, then with
 * 100ms of ping, where the next gun is drawn longer. */
static void SkreesAt(int frame) {
	int g;
	SetFrame(frame);
	for (g = 0; g < SKREE_GUNS; g++) {
		int r, ownMin = 0, ownMax = 0;
		GUN = skreeGuns[g].weapon; gunAttachment = skreeGuns[g].attachment; fullMag = skreeGuns[g].full;
		for (r = 0; r <= 2; r += 2) {
			int flashes, phases, shownMin, shownMax, far = 0, farPhases, clearMin, clearMax;
			ForgetSkrees();
			if (fullMag) {
				TestSkreeFullMagazineSF(r);
				printf("  skree, %-21s %2dms commands, %s: X anywhere, before Y or after it: a real SF NAC\n",
					skreeGuns[g].name, CL_FRAME, r ? "RTT 100" : "own host");
				continue;
			}
			flashes = TestSkreeXAFrameBeforeY(r, &shownMin, &shownMax, &phases);
			TestSkreeXWithOrAfterY(r);
			farPhases = TestSkreeXLongBeforeY(r, !r, &far, &clearMin, &clearMax);
			/* X a frame before Y draws the next gun at every phase online
			 * and at 8 and 16ms on your own host; at 33ms there, those two
			 * commands are sometimes longer than the base is old, and the
			 * client knows of X by Y's command (SkreeRules) */
			CHECK(flashes > 0);
			if (r || CL_FRAME <= 16) CHECK(flashes == phases);
			/* the long X fits in every action on your own host, and online
			 * in the bolt, the longest */
			if (!r || GUN == WP_M40A3) CHECK(farPhases == phases);
			if (!r) { ownMin = shownMin; ownMax = shownMax; }
			else CHECK(shownMin > ownMin && shownMax > ownMax);	/* longer online */
			printf("  skree, %-21s %2dms commands, %s: X a frame before Y draws the next gun at %3d%% of phases for %3d..%3dms",
				skreeGuns[g].name, CL_FRAME, r ? "RTT 100" : "own host", Pct(flashes, phases), shownMin, shownMax);
			if (!r) printf("; none for X from %d..%dms before Y, by phase", clearMin, clearMax);
			if (farPhases) printf("; X %dms before, the reload, then a plain holster%s\n", far,
				farPhases < phases ? " (at the phases where that is inside the action)" : "");
			else printf("; no X that long before is inside the action and its reload's start\n");
		}
	}
	gunAttachment = fullMag = 0;
}
static void Skrees(void) {
	int keepFrame = CL_FRAME, keepHold = hostHold, keepAfter = runAfter, keepGun = GUN;
	if (maxPackets || SV_FRAME != 50) return;	/* the shipped rates, a packet a frame */
	runAfter = 3000; otherPartial = 1;
	SkreesAt(8);
	SkreesAt(16);
	SkreesAt(33);
	otherPartial = 0;
	CL_FRAME = keepFrame; hostHold = keepHold; runAfter = keepAfter; GUN = keepGun;
	startShift = 0;
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
	if (getenv("FAKERELOAD_BURSTLOAD")) {
		if (!strcmp(getenv("FAKERELOAD_BURSTLOAD"), "check")) {	/* its checks alone */
			TestClockBase(); Burstloads();
			printf("PASS: %d burstload checks\n", checks);
			return 0;
		}
		if (getenv("FAKERELOAD_ONLINEGAME")) onlineGame = atoi(getenv("FAKERELOAD_ONLINEGAME"));
		GUN = WP_M16; runAfter = BG_WeaponReloadTime(GUN) + 1500; BurstloadTable(); return 0;
	}
	if (getenv("FAKERELOAD_SKREE") && !strcmp(getenv("FAKERELOAD_SKREE"), "check")) {	/* its checks alone */
		TestClockBase(); Skrees();
		printf("PASS: %d skree checks\n", checks);
		return 0;
	}
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
	TestPictureFinishesTheAction();
	TestCorrectionOnTheReadyCommand();
	TestSFOutOfAShot();
	Windows();
	FakeReloads(); FakeBullets(); FakeBulletWindows();
	GUN = WP_M40A3; MeasureAnchors();
	TestPictureFinishesTheAction();
	TestCorrectionOnTheReadyCommand();
	Windows();
	FakeReloads(); FakeBullets(); FakeBulletWindows(); WildScopes();
	Burstloads();
	Skrees();
#ifdef FAKERELOAD_ENGINE
	printf("  %d runs on your own host also run through the real Com_Frame and loopback, identically\n", engineRuns);
#endif
	printf("PASS: %d fake reload, fake bullet, burstload, skree and wild scope checks\n", checks);
	return 0;
}
