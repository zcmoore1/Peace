/* The silent shot, end to end over a modelled network, through the real code.
 *
 * In MW2 a shot followed at once by the throwing knife's press is real: the
 * shooter sees and hears it and its kick, opponents hear it, the victim dies.
 * But the round's killcam can show nothing of it and still show the view
 * kicked over. Nothing in the game names this. It emerges from MW2's pieces:
 *  - the playerstate's ring of four predictable events (MW2 0x4bd150). The
 *    knife's press raises four on one think: CheckForOffHand's
 *    EV_SWITCH_OFFHAND, OffHandInit's EV_WEAPON_SWITCH_STARTED and
 *    EV_RESET_ADS, OffHandPrepare's EV_PREP_OFFHAND (579168, 578e10, 578e5e,
 *    5778e6). The killcam records one playerstate a server frame (sv_snapshot.c,
 *    after GAME_RUN_FRAME) and replays it (CG_CheckPlayerstateEvents walks the
 *    last four), so a shot and a press run in one frame leave it no shot;
 *  - client-side bullets (MW2's sv_clientSideBullets): the shot's tracer and
 *    impacts are drawn from that fire event alone, so they go with it. The
 *    killer's own entity is never in the killcam, as it never is in a
 *    snapshot of their own view;
 *  - the view kick (BG_WeaponFireRecoil, CG_KickAngles): the client adds it
 *    into the command's aim, so it is in the recorded ps.viewangles; the
 *    throwing knife's centre speed is 0, so with the knife in hand the kick
 *    runs to its clamp and stays there - the tilt;
 *  - the entity's own ring of four (MW2's es.events), so a live opponent gets
 *    the shot whatever follows it, with the gun that fired.
 *
 * The network is ioquake3's transport with MW2's client clock, as in
 * fake_reload_test.c: the server ticks level.time in sv_fps frames, runs
 * each command when it arrives, records the killcam after each frame and
 * sends each frame's snapshot through the real codec (msg.c); the client
 * keeps cl.serverTime with the REAL CL_DriftTimeDelta, makes a command every
 * 8ms with the REAL CL_FinishMove (kick_client.c), and runs cgame's frame with
 * the REAL CG_ProcessSnapshots, CG_PredictPlayerState, events, CG_FireWeapon
 * and CG_KickAngles (silent_shot_client.c). The server is the REAL game
 * module's ClientThink, ClientEvents, FireWeapon, G_AddEvent,
 * G_AgeEntityEvents and ClientEndFrame (silent_shot_server.c), and the REAL
 * sv_snapshot.c (silent_shot_engine.c). One cgame runs at a time, so each
 * case runs twice: first as the shooter, whose commands (with their kick and
 * arrival times) are kept, then as the opponent watching, live and then the
 * round killcam, fed the same commands. The server's half is the same both
 * times, and that is checked.
 *
 * SILENTSHOT_TRACE="gun knife phase [rtt maxpackets shift]" (gun spas or m300,
 * knife ms after the shot, -1 for none) prints one case frame by frame. SILENTSHOT_PROBE=1 prints every
 * phase's outcome. SILENTSHOT_SVFPS sets sv_fps (20, MW2's, by default). */
#include "../../code/qcommon/q_shared.h"
#include "../../code/game/bg_public.h"
#include "silent_shot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int CL_TimeDeltaGap( int delta, int newest, int prev, int realtime );
int CL_DriftTimeDelta( int delta, int newest, int prev, int realtime, qboolean *extrapolated );

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s (%s)\n", __LINE__, #x, Case()); exit(1); } } while (0)

static int SV_FRAME = 50;	/* sv_fps 20, MW2's and the shipped autoexec's */
#define CL_FRAME		8	/* 125 fps, one command a frame */
#define CL_EXTRAP_GUARD	8	/* MW2's CL_SetCGameTime 0x5031ed, as fake_reload_test.c */
#define CL_TIME_RESET	500	/* MW2's 0x5a1b8a */
#define TAP				16	/* the knife's press, a tap: pull, then throw */
#define SETTLE			600		/* the client runs this long before the shot (its clock still drifting, as in play) */
#define MAXQ			4096
#define NONE			(-100000)
#define CLAMP			( BG_KICK_MAX_ANGLE - 0.01f )	/* a kick axis at its clamp, to ANGLE2SHORT's step */

/* The shooter aims at the victim's middle, 10 degrees down, so every hip shot
 * (the M300's hip spread is wide) ends on the victim or the floor behind. */
static const vec3_t view = { 10, 90, 0 };
enum { ROLE_SHOOTER, ROLE_WATCHER };

typedef struct {
	int gun, knifeAt, phase, rtt, maxPackets;	/* knifeAt: ms after the shot's press, NONE for no press */
	int lethal;			/* the offhand pressed: the throwing knife, or the frag as a control */
	int packetShift;	/* with maxPackets: the first packet waits this long, which places the shot in its packet */
} case_t;
typedef struct {
	/* the server: the same in both runs */
	int svShots, svShotFrame, svShotLevel, svShotHits, svPresses, svPressFrame, tempEffects;
	/* the shooter's own client */
	int ownFire, ownSound, ownFlash, ownImpacts, ownKicked, ownKnifeFrames;
	float ownPeak[3], ownAllPeak[3];	/* the kick on screen with the knife in hand, and at all */
	/* the opponent, live */
	int liveFire, liveSound, liveFlash, liveDrawn;
	int liveFlashFrames, liveFlashWrong, liveFlashUnderOffhand;	/* the shooter's flash as their weapon draws it */
	/* the opponent, watching the round killcam */
	int kcSnaps, kcFire, kcSound, kcFlash, kcDrawn, kcEntityFire, kcKnifeSnaps, kcForeignAngles;
	float kcPeak[3];
	int kcClamped[3], kcUnheld[3];
	/* the watcher as a follower, or out of the round */
	int fwFire, fwPsFire, fwSound, fwImpacts, fwFlesh, fwViewerBleeds;
} outcome_t;

/* The watcher's view: an opponent (the default), a spectator who follows the
 * shooter and switches to the victim followSwitchAt ms after the shot, a
 * player out of the round with no body at their own spot off the line, or a
 * player at (0, 100), in the line of fire (alive, as a control, or out of the
 * round with no body) */
enum { VIEW_OPPONENT, VIEW_FOLLOW, VIEW_OFF_LINE, VIEW_IN_LINE, VIEW_OUT_OF_ROUND };
static int watcherView = VIEW_OPPONENT, followSwitchAt;

static case_t C;
static outcome_t O, shooterRun;
static char caseText[128];
static const char *GunName( int gun ) { return gun == WP_M40A3 ? "m300" : "spas"; }
static const char *Case( void ) {
	Com_sprintf( caseText, sizeof( caseText ), "%s %s %d phase %d rtt %d maxpackets %d shift %d",
		GunName( C.gun ), C.lethal == WP_FRAG ? "frag" : "knife", C.knifeAt, C.phase, C.rtt, C.maxPackets, C.packetShift );
	return caseText;
}

/* ---- the network ---- */
#define PENDING (-1)
static struct { int arrive; usercmd_t cmd; } cq[MAXQ], kept[MAXQ];	/* the shooter's commands; kept: run A's */
static int cqHead, cqTail, keptCount;
static struct { int arrive; ssSnapshot_t snap; } sq[8];	/* snapshots in flight to the local client */
static int sqHead, sqTail;
static int oneWay, lastPacket, levelTime, svFrames, replayEnd, startPress, now;
static int clDelta, clServerTime, clOldServerTime, clActive, clNewSnap, clSnapTime, clPrevSnapTime, clHaveSnap;
static qboolean clExtrap;

/* CL_SetCGameTime: the clock, then the drift when a snapshot came in (fake_reload_test.c's) */
static void SetCGameTime( int realtime ) {
	if ( !clActive ) {
		clActive = 1; clNewSnap = 0;
		clDelta = clSnapTime - realtime; clOldServerTime = clSnapTime;
	}
	clServerTime = realtime + clDelta;
	if ( clServerTime < clOldServerTime ) clServerTime = clOldServerTime;
	clOldServerTime = clServerTime;
	if ( realtime + clDelta >= clSnapTime - CL_EXTRAP_GUARD ) clExtrap = qtrue;
	if ( clNewSnap ) {
		int gap = CL_TimeDeltaGap( clDelta, clSnapTime, clPrevSnapTime, realtime );
		clNewSnap = 0;
		clDelta = CL_DriftTimeDelta( clDelta, clSnapTime, clPrevSnapTime, realtime, &clExtrap );
		if ( gap > CL_TIME_RESET ) clOldServerTime = clServerTime = clSnapTime;
	}
}

static int Buttons( int t ) {
	int b = 0;
	if ( t == startPress ) b |= BUTTON_ATTACK;	/* one command */
	if ( C.knifeAt != NONE && t >= startPress + C.knifeAt && t < startPress + C.knifeAt + TAP ) b |= BUTTON_LETHAL;
	return b;
}

static float Kick( const playerState_t *ps, int axis ) { return AngleNormalize180( ps->viewangles[axis] - view[axis] ); }
static int Traced( void ) { return getenv( "SILENTSHOT_TRACE" ) != NULL; }

static void ServerThink( const usercmd_t *cmd ) {
	ssThink_t think;
	SS_Command( SS_SHOOTER, cmd, &think );
	if ( think.shots ) { O.svShots++; O.svShotFrame = svFrames; O.svShotLevel = levelTime; O.svShotHits = think.hits; }
	if ( think.presses ) { O.svPresses++; O.svPressFrame = svFrames; }
	if ( Traced() && ( think.events || cmd->buttons ) )
		printf( "  now %5d SV think st%d b%x: %d events, shot %d press %d (server frame %d)\n",
			now, cmd->serverTime, cmd->buttons, think.events, think.shots, think.presses, svFrames );
}

static void Observe( int role, const ccFrame_t *f, int *lastSnapTime ) {
	int i;
	if ( Traced() && ( f->gunFire[SS_SHOOTER] || f->gunSounds[SS_SHOOTER] || f->impacts || f->tracers || f->knifeThrows ||
		( f->killcam && f->snapServerTime != *lastSnapTime && f->snapPs.weapon == C.lethal ) ) )
		printf( "  now %5d CL %s%s fire %d (pv %d) sound %d flash %d/%d impacts %d tracers %d smoke %d knife %d kick %.2f %.2f %.2f snap %d w%d view %.2f %.2f %.2f\n",
			now, role == ROLE_SHOOTER ? "shooter" : "watcher", f->killcam ? " KILLCAM" : "", f->gunFire[SS_SHOOTER], f->playerGunFire,
			f->gunSounds[SS_SHOOTER], f->flashes, f->playerFlash, f->impacts, f->tracers, f->smokes, f->knifeThrows,
			f->kickAngles[0], f->kickAngles[1], f->kickAngles[2], f->snapServerTime, f->snapPs.weapon,
			Kick( &f->snapPs, 0 ), Kick( &f->snapPs, 1 ), Kick( &f->snapPs, 2 ) );
	if ( role == ROLE_SHOOTER ) {
		if ( f->killcam ) return;	/* the killer's own replay is not under test */
		CHECK( !f->gunFire[SS_SHOOTER] || f->gunFire[SS_SHOOTER] == f->playerGunFire );	/* their own entity is never sent them */
		/* a flash counts with the gun's event: the knife's throw flashes nothing visible */
		O.ownFire += f->playerGunFire; O.ownSound += f->gunSounds[SS_SHOOTER]; O.ownFlash += f->playerFlash && f->playerGunFire;
		if ( f->playerGunFire ) {
			O.ownImpacts = f->impacts;
			O.ownKicked = !VectorCompare( f->kickAngles, vec3_origin );
		}
		for ( i = 0; i < 3; i++ ) if ( fabs( Kick( &f->predictedPs, i ) ) > fabs( O.ownAllPeak[i] ) ) O.ownAllPeak[i] = Kick( &f->predictedPs, i );
		if ( f->predictedPs.weapon == C.lethal ) {
			O.ownKnifeFrames++;
			for ( i = 0; i < 3; i++ ) if ( fabs( Kick( &f->predictedPs, i ) ) > fabs( O.ownPeak[i] ) ) O.ownPeak[i] = Kick( &f->predictedPs, i );
		}
		return;
	}
	if ( watcherView != VIEW_OPPONENT ) {
		CHECK( !f->killcam );
		O.fwFire += f->gunFire[SS_SHOOTER]; O.fwPsFire += f->playerGunFire; O.fwSound += f->gunSounds[SS_SHOOTER];
		O.fwImpacts += f->impacts; O.fwFlesh += f->fleshImpacts; O.fwViewerBleeds += f->viewerBleeds;
		if ( Traced() && ( f->gunFire[SS_SHOOTER] || f->impacts ) )
			printf( "  now %5d CL view of %d: fire %d (ps %d) sound %d impacts %d (flesh %d, on the viewer %d)\n", now,
				f->snapPs.clientNum, f->gunFire[SS_SHOOTER], f->playerGunFire, f->gunSounds[SS_SHOOTER], f->impacts,
				f->fleshImpacts, f->viewerBleeds );
		return;
	}
	if ( !f->killcam ) {
		CHECK( !f->playerGunFire );	/* the opponent's own playerstate fires nothing */
		O.liveFire += f->gunFire[SS_SHOOTER]; O.liveSound += f->gunSounds[SS_SHOOTER];
		O.liveDrawn += f->impacts + f->tracers + f->smokes;
		/* the flash is the fired gun's: its model, on its own muzzle tag, with
		 * its light, whatever the entity holds by then (MW2's es.weapon is
		 * still the gun) */
		if ( f->flashDrawn[SS_SHOOTER] ) {
			O.liveFlashFrames++;
			if ( f->flashDrawn[SS_SHOOTER] != C.gun || f->flashTag[SS_SHOOTER] != C.gun || f->flashLight[SS_SHOOTER] != C.gun ) O.liveFlashWrong++;
			if ( f->flashHeld[SS_SHOOTER] != C.gun ) O.liveFlashUnderOffhand++;
		}
		O.liveFlash += f->gunFire[SS_SHOOTER] && f->flashDrawn[SS_SHOOTER] == C.gun;
		return;
	}
	/* the round killcam: the killer's archived playerstate, replayed */
	CHECK( f->snapPs.clientNum == SS_SHOOTER );
	O.kcEntityFire += f->gunFire[SS_SHOOTER] - f->playerGunFire;	/* the killer's entity is not in it */
	O.kcFire += f->playerGunFire; O.kcSound += f->gunSounds[SS_SHOOTER]; O.kcFlash += f->playerFlash && f->playerGunFire;
	O.kcDrawn += f->impacts + f->tracers + f->smokes;
	if ( f->snapServerTime == *lastSnapTime ) return;
	*lastSnapTime = f->snapServerTime;
	O.kcSnaps++;
	/* the recorded aim is a command's: the client's view plus its kick, nothing else */
	{
		int k, found = 0;
		for ( k = 0; k < keptCount && !found; k++ ) {
			found = 1;
			for ( i = 0; i < 3; i++ ) if ( f->snapPs.viewangles[i] != SHORT2ANGLE( kept[k].cmd.angles[i] ) ) found = 0;
		}
		/* before the shooter's first command it is the spawn's (SS_Reset: yaw 90) */
		if ( !found && !( f->snapPs.viewangles[0] == 0 && f->snapPs.viewangles[1] == 90 && f->snapPs.viewangles[2] == 0 ) ) O.kcForeignAngles++;
	}
	if ( f->snapPs.weapon != C.lethal ) return;
	O.kcKnifeSnaps++;
	for ( i = 0; i < 3; i++ ) {
		float k = Kick( &f->snapPs, i );
		if ( fabs( k ) > fabs( O.kcPeak[i] ) ) O.kcPeak[i] = k;
		if ( fabs( k ) >= CLAMP ) O.kcClamped[i]++;
		else if ( O.kcClamped[i] ) O.kcUnheld[i]++;	/* left the clamp with the knife still in hand */
	}
}

/* One case, as the shooter (role SHOOTER, keeping its commands) or as the
 * opponent watching (role WATCHER, fed those commands). */
static void Run( int role ) {
	int local = role == ROLE_SHOOTER ? SS_SHOOTER : SS_WATCHER;
	int clientPhase, end, k = 0, lastSnapTime = -1, frameSeen = 0, switched = 0;
	ccFrame_t f;
	memset( &O, 0, sizeof( O ) );
	oneWay = C.rtt / 2; levelTime = 1000; svFrames = 0; replayEnd = 0;
	lastPacket = C.maxPackets ? C.packetShift - 1000 / C.maxPackets : -1000;
	cqHead = cqTail = sqHead = sqTail = 0;
	if ( role == ROLE_SHOOTER ) keptCount = 0;
	clDelta = clServerTime = clOldServerTime = clActive = clNewSnap = clSnapTime = clPrevSnapTime = clHaveSnap = 0;
	clExtrap = qfalse;
	startPress = SETTLE + C.phase;
	clientPhase = startPress % CL_FRAME;
	/* the shooter's commands matter until the killcam stops recording, the
	 * shot's server frame plus KILLCAM_LINGER */
	end = role == ROLE_SHOOTER ? startPress + KILLCAM_LINGER + 150 + C.rtt : 1 << 30;
	SS_Reset( C.gun, C.lethal, levelTime );
	if ( role == ROLE_WATCHER && watcherView == VIEW_FOLLOW ) SS_Follow( SS_WATCHER, SS_SHOOTER );
	if ( role == ROLE_WATCHER && ( watcherView == VIEW_IN_LINE || watcherView == VIEW_OUT_OF_ROUND ) ) SS_Move( SS_WATCHER, 0, 100, 24 );
	if ( role == ROLE_WATCHER && ( watcherView == VIEW_OFF_LINE || watcherView == VIEW_OUT_OF_ROUND ) ) SS_OutOfRound( SS_WATCHER );
	SE_Reset();
	CC_Reset( local, C.gun );
	KickTestClientReset();
	for ( now = 0; now < end; now++ ) {
		/* the server runs each command as it arrives */
		if ( role == ROLE_SHOOTER ) {
			while ( cqHead < cqTail && cq[cqHead].arrive != PENDING && cq[cqHead].arrive <= now ) {
				kept[keptCount] = cq[cqHead]; kept[keptCount++].arrive = now;	/* when it ran */
				ServerThink( &cq[cqHead++].cmd );
			}
		} else {
			while ( k < keptCount && kept[k].arrive <= now ) ServerThink( &kept[k++].cmd );
		}
		/* snapshots reach the client */
		while ( sqHead < sqTail && sq[sqHead & 7].arrive <= now ) {
			const ssSnapshot_t *s = &sq[sqHead & 7].snap;
			if ( !clHaveSnap || s->serverTime > clSnapTime ) {
				clHaveSnap = 1; clPrevSnapTime = clSnapTime; clSnapTime = s->serverTime; clNewSnap = 1;
				CC_Snapshot( s );
			}
			sqHead++;
		}
		/* a server frame: the game's frame, the killcam's record, the snapshot */
		if ( now % SV_FRAME == 0 ) {
			levelTime += SV_FRAME;
			SS_RunFrame( levelTime );
			SE_Frame( levelTime );
			svFrames++;
			if ( SS_TempEffects() ) O.tempEffects++;
			if ( role == ROLE_WATCHER && watcherView == VIEW_FOLLOW && O.svShots && !switched &&
			     levelTime >= O.svShotLevel + followSwitchAt ) {
				SS_Follow( SS_WATCHER, SS_VICTIM );	/* the frame's end takes the victim's view */
				switched = 1;
			}
			if ( role == ROLE_WATCHER && watcherView != VIEW_OPPONENT && now > startPress + 1500 ) break;
			/* the round is won by the shot: S&D's killcam (g_snd.c), from the death */
			if ( watcherView == VIEW_OPPONENT && O.svShots && !replayEnd ) {
				replayEnd = SE_ReplayStart( SS_SHOOTER, O.svShotLevel - KILLCAM_REWIND, O.svShotLevel + KILLCAM_LINGER );
				CHECK( replayEnd > levelTime );
			}
			if ( sqTail - sqHead >= 8 ) { fprintf( stderr, "snapshot queue full\n" ); exit( 1 ); }
			SE_Snapshot( local, &sq[sqTail & 7].snap );
			sq[sqTail & 7].arrive = now + oneWay;
			sqTail++;
			if ( role == ROLE_WATCHER && replayEnd && levelTime > replayEnd + C.rtt + 200 ) break;
			if ( role == ROLE_WATCHER && !replayEnd && now > startPress + 3000 ) break;
		}
		/* a client frame: CL_SendCmd, then the cgame's frame */
		if ( ( now - clientPhase ) % CL_FRAME == 0 && clHaveSnap ) {
			if ( role == ROLE_SHOOTER ) {
				usercmd_t cmd;
				KickTestFinishMove( &cmd, clServerTime ? clServerTime : clSnapTime, view, CC_WeaponCommand() );
				cmd.buttons = Buttons( now );
				CC_StoreCommand( &cmd );
				if ( cqTail >= MAXQ ) { fprintf( stderr, "command queue full\n" ); exit( 1 ); }
				cq[cqTail].arrive = PENDING; cq[cqTail].cmd = cmd; cqTail++;
				if ( !C.maxPackets || now - lastPacket >= 1000 / C.maxPackets ) {	/* CL_ReadyToSendPacket */
					int q;
					for ( q = cqTail - 1; q >= cqHead && cq[q].arrive == PENDING; q-- ) cq[q].arrive = now + oneWay;
					lastPacket = now;
				}
			}
			SetCGameTime( now );
			CC_Frame( clServerTime, &f );
			frameSeen = 1;
			if ( getenv( "SILENTSHOT_FRAMES" ) && now >= startPress - 16 && now <= startPress + 120 )
				printf( "  now %5d frame cl %d snap %d ct %d seq %d w%d ws%d slot%d | pred ct %d seq %d w%d ws%d | fire %d knife %d\n", now, clServerTime,
					f.snapServerTime, f.snapPs.commandTime, f.snapPs.eventSequence, f.snapPs.weapon, f.snapPs.weaponstate,
					f.snapPs.stats[STAT_ACTIVE_SLOT], f.predictedPs.commandTime,
					f.predictedPs.eventSequence, f.predictedPs.weapon, f.predictedPs.weaponstate, f.gunFire[SS_SHOOTER], f.knifeThrows );
			Observe( role, &f, &lastSnapTime );
		}
	}
	CHECK( frameSeen );
}

/* Both runs of a case, and what must always hold. Returns whether the killcam lost the shot. */
static int Case2( void ) {
	int same, lost;
	Run( ROLE_SHOOTER );
	shooterRun = O;
	Run( ROLE_WATCHER );
	/* the server's half was the same both times */
	CHECK( O.svShots == shooterRun.svShots && O.svShotFrame == shooterRun.svShotFrame &&
		O.svShotLevel == shooterRun.svShotLevel && O.svPresses == shooterRun.svPresses &&
		O.svPressFrame == shooterRun.svPressFrame );
	CHECK( O.svShots == 1 && O.svShotHits == shooterRun.svShotHits );	/* the shot is real ... */
	CHECK( O.svShotHits > 0 );					/* ... and hits */
	CHECK( !O.tempEffects && !shooterRun.tempEffects );		/* the server sends no impact or tracer */
	CHECK( O.svPresses == ( C.knifeAt != NONE ) );
	/* the shooter always gets the shot: flash, sound, impacts and its kick */
	CHECK( shooterRun.ownFire == 1 && shooterRun.ownSound == 1 && shooterRun.ownFlash >= 1 );
	/* every impact where the server's damage went (a tracer is cg_tracerChance's
	 * random() < 1, which Q3's random() misses one time in 32768) */
	CHECK( shooterRun.ownImpacts == O.svShotHits );
	CHECK( shooterRun.ownKicked );
	/* a live opponent always gets it, with the gun that fired: its sound, its
	 * bullets, and its flash drawn on the shooter's model */
	CHECK( O.liveFire == 1 && O.liveSound == 1 && O.liveFlash == 1 && O.liveDrawn > 0 );
	CHECK( O.liveFlashFrames >= 1 && !O.liveFlashWrong );
	/* the round killcam: the shot's fire event, and with it the bullet, is
	 * lost exactly when the shot and the knife's press ran in one server
	 * frame, the one the killcam records them in */
	same = C.knifeAt != NONE && O.svShotFrame == O.svPressFrame;
	lost = !O.kcFire;
	if ( C.lethal == WP_FRAG ) {
		/* the control: a frag's press is three events (its pullback waits for
		 * the quick drop), so the ring keeps the shot even in its frame */
		CHECK( !lost && O.kcSound == 1 && O.kcDrawn > 0 );
		CHECK( !O.liveFlashUnderOffhand );	/* the gun is still in hand for its quick drop */
		return 0;
	}
	/* the shot's snapshot shows the knife already exactly when the press ran
	 * in the shot's server frame: the flash is still the gun's */
	CHECK( ( O.liveFlashUnderOffhand > 0 ) == same );
	CHECK( O.kcSnaps > 0 && !O.kcEntityFire );
	CHECK( lost == same );
	CHECK( O.kcFire == !same && O.kcSound == !same && O.kcFlash == !same );
	CHECK( ( O.kcDrawn > 0 ) == !same );
	/* the recorded aim is the commands', kick and all */
	CHECK( !O.kcForeignAngles );
	if ( C.knifeAt != NONE ) CHECK( O.kcKnifeSnaps > 0 && shooterRun.ownKnifeFrames > 0 );
	/* once an axis is at the clamp with the knife in hand, it stays there; the
	 * killcam's is the shooter's own tilt, sampled a server frame at a time */
	CHECK( !O.kcUnheld[0] && !O.kcUnheld[1] && !O.kcUnheld[2] );
	{
		int i;
		for ( i = 0; i < 3; i++ ) CHECK( fabs( O.kcPeak[i] ) <= fabs( shooterRun.ownPeak[i] ) + 0.01f );
	}
	if ( getenv( "SILENTSHOT_PROBE" ) )
		printf( "%s: shot frame %d press frame %d -> killcam %s; peak kick in the killcam with the knife %.2f %.2f %.2f\n",
			Case(), O.svShotFrame, O.svPressFrame, lost ? "LOST" : "shows it", O.kcPeak[0], O.kcPeak[1], O.kcPeak[2] );
	return lost;
}

/* A spectator follows the shooter, sees the shot through the shooter's
 * playerstate, then switches to the victim: the shooter's entity comes back
 * into view with the shot still in its ring (EVENT_VALID_MSEC). MW2's
 * CG_ResetEntity takes a player's ring as played, so the shot plays once,
 * however soon the switch comes. */
static void FollowSwitch( int gun ) {
	static const int switches[] = { 50, 100, 150, 200, 250, 300, 350, 400, 600 };
	int s, p, cases = 0;
	for ( s = 0; s < ARRAY_LEN( switches ); s++ ) {
		for ( p = 0; p < SV_FRAME; p += 5 ) {
			C.gun = gun; C.lethal = WP_THROWING_KNIFE; C.knifeAt = NONE; C.phase = p; C.rtt = 0; C.maxPackets = 0; C.packetShift = 0;
			watcherView = VIEW_OPPONENT; Run( ROLE_SHOOTER ); shooterRun = O;
			watcherView = VIEW_FOLLOW; followSwitchAt = switches[s]; Run( ROLE_WATCHER ); watcherView = VIEW_OPPONENT;
			CHECK( O.svShots == 1 && O.svShotHits == shooterRun.svShotHits && O.svShotHits > 0 );
			CHECK( O.fwPsFire == 1 && O.fwFire == 1 && O.fwSound == 1 );	/* once, from the followed playerstate */
			CHECK( O.fwImpacts == O.svShotHits );	/* one set of bullets, where the damage went */
			cases++;
		}
	}
	printf( "%s, a spectator following the shooter switches to the victim 50-600ms after the shot: the shot played once in all %d cases\n",
		GunName( gun ), cases );
}

/* A viewer out of the round (S&D: dead, no teammate to follow, or waiting
 * for the next round) stands between the shooter and the victim. The server
 * has no body there and its bullets go through, so the drawn ones must too:
 * exactly as they are drawn for the same viewer off the line. (Where they
 * land behind is the entity event's own business: exact only when the entity
 * shows the shot's think, so not every drawn bullet need reach the victim.)
 * The same viewer alive, in the same place, takes them (the control). */
static void OutOfRound( int gun ) {
	int p, cases = 0, inLine = 0, through = 0;
	outcome_t offLine;
	for ( p = 0; p < SV_FRAME; p += 5 ) {
		C.gun = gun; C.lethal = WP_THROWING_KNIFE; C.knifeAt = NONE; C.phase = p; C.rtt = 0; C.maxPackets = 0; C.packetShift = 0;
		watcherView = VIEW_OPPONENT; Run( ROLE_SHOOTER ); shooterRun = O;
		watcherView = VIEW_OFF_LINE; Run( ROLE_WATCHER ); offLine = O;
		CHECK( offLine.fwFire == 1 && !offLine.fwPsFire && !offLine.fwViewerBleeds && offLine.fwImpacts > 0 );
		watcherView = VIEW_IN_LINE; Run( ROLE_WATCHER );
		CHECK( O.fwFire == 1 && !O.fwPsFire && SS_Health( SS_WATCHER ) < 30000 && O.fwViewerBleeds > 0 );
		inLine += O.fwViewerBleeds;
		watcherView = VIEW_OUT_OF_ROUND; Run( ROLE_WATCHER ); watcherView = VIEW_OPPONENT;
		CHECK( O.fwFire == 1 && O.svShotHits > 0 && SS_Health( SS_VICTIM ) < 30000 );
		/* through the absent body: every bullet drawn as if nobody stood there */
		CHECK( !O.fwViewerBleeds && O.fwImpacts == offLine.fwImpacts && O.fwFlesh == offLine.fwFlesh );
		through += O.fwFlesh;
		cases++;
	}
	CHECK( through > 0 );
	printf( "%s, a viewer in the line of fire: alive, drawn %d hits on their own body; out of the round, none, and %d on the victim behind (%d shots)\n",
		GunName( gun ), inLine, through, cases );
}

/* ---- the sweep ---- */
static const int knifeTimes[] = { 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 120, 160, 200 };
static const int pingTimes[] = { 8, 24, 40, 48, 56, 120 };

static void Sweep( int gun, int rtt, int maxPackets, int phaseStep, int report, const int *times, int numTimes ) {
	int n, p, sh, lost, phases, clampedAt[3] = { 0, 0, 0 }, shifts = maxPackets ? 5 : 1;
	float peak[3], ownPeak[3];
	if ( report ) printf( "%s, RTT %d, %s: the killcam loses the shot in (of %d cases: arrival phases%s)\n",
		gun == WP_M40A3 ? "M300" : "SPAS", rtt, maxPackets ? va( "cl_maxpackets %d", maxPackets ) : "a packet every frame",
		shifts * ( ( SV_FRAME + phaseStep - 1 ) / phaseStep ), maxPackets ? " x the shot's place in its packet" : "" );
	for ( n = 0; n < numTimes; n++ ) {
		int clamped[3] = { 0, 0, 0 }, i;
		lost = phases = 0;
		for ( i = 0; i < 3; i++ ) peak[i] = ownPeak[i] = 0;
		for ( sh = 0; sh < shifts; sh++ ) for ( p = 0; p < SV_FRAME; p += phaseStep ) {
			C.gun = gun; C.lethal = WP_THROWING_KNIFE; C.knifeAt = times[n]; C.phase = p; C.rtt = rtt; C.maxPackets = maxPackets;
			C.packetShift = sh * CL_FRAME;
			lost += Case2(); phases++;
			for ( i = 0; i < 3; i++ ) {
				if ( fabs( O.kcPeak[i] ) > fabs( peak[i] ) ) peak[i] = O.kcPeak[i];
				if ( fabs( shooterRun.ownPeak[i] ) > fabs( ownPeak[i] ) ) ownPeak[i] = shooterRun.ownPeak[i];
				if ( O.kcClamped[i] ) clamped[i]++;
			}
		}
		if ( !maxPackets ) {
			/* a packet a frame: lost exactly while the press comes within a
			 * server frame of the shot, (frame - N) of every frame's phases,
			 * at any ping */
			CHECK( abs( lost * SV_FRAME - phases * ( times[n] < SV_FRAME ? SV_FRAME - times[n] : 0 ) ) < SV_FRAME );
		} else {
			/* batched (CL_ReadyToSendPacket: a packet every interval): a press
			 * in the shot's own packet arrives with it and is always lost with
			 * it; one a packet later arrives an interval later, lost as a
			 * packet a frame would lose it at that distance; later, never. The
			 * five shifts put the shot at each frame of its packet. */
			int interval = ( ( 1000 / maxPackets + CL_FRAME - 1 ) / CL_FRAME ) * CL_FRAME;
			int perPacket = interval / CL_FRAME, frames = times[n] / CL_FRAME, u, expected = 0;
			CHECK( shifts == perPacket );
			for ( u = 0; u < perPacket; u++ ) {	/* u: frames from the shot to its packet */
				int packets = frames <= u ? 0 : ( frames - u + perPacket - 1 ) / perPacket;
				int apart = packets * interval;
				if ( apart < SV_FRAME ) expected += ( phases / shifts ) * ( SV_FRAME - apart ) / SV_FRAME;
			}
			CHECK( abs( lost - expected ) <= shifts );
		}
		for ( i = 0; i < 3; i++ ) if ( clamped[i] == phases ) clampedAt[i] = times[n];
		if ( report ) printf( "  knife %3dms after the shot: lost in %3d of %3d (%3d%%); kick with the knife in hand, killcam %6.2f %6.2f %6.2f, own screen %6.2f %6.2f %6.2f (clamped in %d/%d/%d)\n",
			times[n], lost, phases, 100 * lost / phases, peak[0], peak[1], peak[2], ownPeak[0], ownPeak[1], ownPeak[2],
			clamped[0], clamped[1], clamped[2] );
	}
	if ( report ) printf( "  the killcam's recorded aim holds the kick at its clamp with the knife up to: pitch %dms, yaw %dms, roll %dms after the shot\n",
		clampedAt[0], clampedAt[1], clampedAt[2] );
	/* the tilt: a knife a command after the shot leaves the hard kicked axes at the clamp */
	if ( gun == WP_M40A3 ) CHECK( clampedAt[0] >= 8 && clampedAt[1] >= 8 && clampedAt[2] >= 8 );
	if ( gun == WP_SHOTGUN ) CHECK( clampedAt[0] >= 8 );
}

int main( void ) {
	const char *e;
	int guns[2] = { WP_SHOTGUN, WP_M40A3 }, g, r, p;
	if ( ( e = getenv( "SILENTSHOT_SVFPS" ) ) && atoi( e ) > 0 ) SV_FRAME = 1000 / atoi( e );
	if ( ( e = getenv( "SILENTSHOT_TRACE" ) ) ) {
		char name[16];
		memset( &C, 0, sizeof( C ) );
		if ( sscanf( e, "%15s %d %d %d %d %d", name, &C.knifeAt, &C.phase, &C.rtt, &C.maxPackets, &C.packetShift ) < 3 ) {
			fprintf( stderr, "SILENTSHOT_TRACE=\"gun knife phase [rtt maxpackets shift]\"\n" ); return 1;
		}
		C.gun = !strcmp( name, "m300" ) ? WP_M40A3 : WP_SHOTGUN;
		C.lethal = getenv( "SILENTSHOT_FRAG" ) ? WP_FRAG : WP_THROWING_KNIFE;
		if ( C.knifeAt < 0 ) C.knifeAt = NONE;
		printf( "%s\n", Case() );
		printf( "killcam %s\n", Case2() ? "LOST the shot" : "shows the shot" );
		return 0;
	}
	for ( g = 0; g < 2; g++ ) {
		/* no knife: the killcam always shows the shot, the kick runs out at the gun's centre speed */
		for ( p = 0; p < SV_FRAME; p += 5 ) {
			C.gun = guns[g]; C.lethal = WP_THROWING_KNIFE; C.knifeAt = NONE; C.phase = p; C.rtt = 0; C.maxPackets = 0;
			CHECK( !Case2() );
			CHECK( !O.kcKnifeSnaps && !O.kcClamped[0] && !O.kcClamped[1] && !O.kcClamped[2] );
			for ( r = 0; r < 3; r++ ) CHECK( fabs( shooterRun.ownAllPeak[r] ) < CLAMP );	/* the gun brakes its own kick */
			if ( !p ) printf( "%s with no knife: the kick peaks at %.2f %.2f %.2f (pitch yaw roll) and runs out\n",
				guns[g] == WP_M40A3 ? "M300" : "SPAS", shooterRun.ownAllPeak[0], shooterRun.ownAllPeak[1], shooterRun.ownAllPeak[2] );
		}
		/* the frag a command after the shot: the same frame most of the time, never lost */
		{
			int sameFrames = 0;
			for ( p = 0; p < SV_FRAME; p++ ) {
				C.gun = guns[g]; C.lethal = WP_FRAG; C.knifeAt = CL_FRAME; C.phase = p; C.rtt = 0; C.maxPackets = 0;
				CHECK( !Case2() );
				sameFrames += O.svShotFrame == O.svPressFrame;
			}
			CHECK( sameFrames == SV_FRAME - CL_FRAME );
			printf( "%s then the frag 8ms later: the press ran in the shot's server frame in %d of %d phases; the killcam lost the shot in none\n",
				guns[g] == WP_M40A3 ? "M300" : "SPAS", sameFrames, SV_FRAME );
		}
		FollowSwitch( guns[g] );
		OutOfRound( guns[g] );
		Sweep( guns[g], 0, 0, 1, 1, knifeTimes, ARRAY_LEN( knifeTimes ) );	/* LAN: a packet every frame */
		Sweep( guns[g], 0, 30, 5, 1, knifeTimes, ARRAY_LEN( knifeTimes ) );	/* MW2's default cl_maxpackets 30 */
		for ( r = 50; r <= 200; r *= 2 ) Sweep( guns[g], r, 0, 5, 0, pingTimes, ARRAY_LEN( pingTimes ) );	/* ping moves nothing */
	}
	printf( "PASS: %d silent shot checks (server, shooter, live opponent, round killcam)\n", checks );
	return 0;
}
