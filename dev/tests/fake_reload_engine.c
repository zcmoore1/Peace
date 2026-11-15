/* Your own host through the REAL engine frame, for fake_reload_test.c.
 *
 * The REAL Com_Frame (code/qcommon/common.c) and the REAL loopback
 * (code/qcommon/net_chan.c) run the frames; the host's halves are the
 * harness's: SV_Frame and the server's packets drive its server, CL_Frame and
 * the client's packets its client. Snapshots and commands cross the real
 * loopback as their index in the harness's queues. Time is a virtual clock
 * that only NET_Sleep moves, so Com_Frame's own pacing (com_maxfps) makes
 * every frame exactly one command long. Everything else the engine would call
 * is stubbed below, and fails the test if an error path reaches it.
 *
 * Only the run-fakereload.sh build links this (GNU ld's --gc-sections drops
 * the parts of common.c that reach engine code it does not build). */
#include "../../code/qcommon/q_shared.h"
#include "../../code/qcommon/qcommon.h"
#include <stdio.h>
#include <stdlib.h>

void FakeReloadHostSnapshot(int i, int now);	/* CL_PacketEvent */
void FakeReloadHostCommand(int i, int now);		/* SV_PacketEvent */
int  FakeReloadHostServer(int msec, int now);	/* SV_Frame: the snapshot sent, or -1 */
int  FakeReloadHostClient(int msec, int now);	/* CL_Frame: the command sent, or -1 */

static int virtualClock = 1000000, runBase, quiet;
/* FakeReloadEngineLoopback's log of who got what, in which frame and where in it */
enum { STAGE_FIRST_EVENTS, STAGE_SV_FRAME, STAGE_SECOND_EVENTS, STAGE_CL_FRAME };
static int probing, probeFrame, probeStage, probeLog[8][4], probeCount;
static cvar_t cvZero, cvMaxfps, cvRunning, cvTimescale;
/* common.c's and net_chan.c's, not in qcommon.h */
extern cvar_t *com_timedemo, *com_busyWait, *com_fixedtime, *com_showtrace, *com_maxfps, *showpackets;

static void Fail(const char *what) {
	fprintf(stderr, "FAIL: the engine frame reached %s\n", what);
	exit(1);
}

/* The time and the network. */
int Sys_Milliseconds(void) { return virtualClock; }
void NET_Sleep(int msec) { virtualClock += msec > 0 ? msec : 1; }
void Sys_SendPacket(int length, const void *data, netadr_t to) { (void)length; (void)data; (void)to; Fail("Sys_SendPacket"); }
static void Send(netsrc_t sock, int i) {
	netadr_t to;
	memset(&to, 0, sizeof(to)); to.type = NA_LOOPBACK;
	NET_SendPacket(sock, sizeof(i), &i, to);
}
static int Index(const msg_t *msg) {
	int i;
	if (msg->cursize != sizeof(i)) Fail("a packet of the wrong size");
	memcpy(&i, msg->data, sizeof(i));
	return i;
}

static void Log(int side, int value) {
	if (probeCount < 8) {
		probeLog[probeCount][0] = side; probeLog[probeCount][1] = value;
		probeLog[probeCount][2] = probeFrame; probeLog[probeCount][3] = probeStage;
	}
	probeCount++;
}

/* The host's halves. */
void CL_PacketEvent(netadr_t from, msg_t *msg) {
	if (from.type != NA_LOOPBACK) Fail("a client packet off the loopback");
	if (probing) Log(NS_CLIENT, Index(msg));
	else if (!quiet) FakeReloadHostSnapshot(Index(msg), Sys_Milliseconds() - runBase);
}
void SV_PacketEvent(netadr_t from, msg_t *msg) {
	if (from.type != NA_LOOPBACK) Fail("a server packet off the loopback");
	if (probing) {
		Log(NS_SERVER, Index(msg));
		Send(NS_SERVER, 2);	/* the server answers a client's packet at once */
	} else if (!quiet) FakeReloadHostCommand(Index(msg), Sys_Milliseconds() - runBase);
}
void SV_Frame(int msec) {
	int i;
	if (probing) {
		probeStage = STAGE_SV_FRAME;
		if (probeFrame == 2) Send(NS_SERVER, 3);	/* a snapshot */
		probeStage = STAGE_SECOND_EVENTS;
		return;
	}
	if (quiet) return;
	if ((i = FakeReloadHostServer(msec, Sys_Milliseconds() - runBase)) >= 0) Send(NS_SERVER, i);
}
void CL_Frame(int msec) {
	int i;
	if (probing) {
		probeStage = STAGE_CL_FRAME;
		if (probeFrame == 1) Send(NS_CLIENT, 1);	/* a command */
		return;
	}
	if (quiet) return;
	if ((i = FakeReloadHostClient(msec, Sys_Milliseconds() - runBase)) >= 0) Send(NS_CLIENT, i);
}

static void SetCvars(int frameMsec) {
	com_speeds = com_timedemo = com_dedicated = com_minimized = com_maxfpsMinimized = &cvZero;
	com_unfocused = com_maxfpsUnfocused = com_busyWait = com_altivec = com_fixedtime = &cvZero;
	com_cameraMode = com_showtrace = com_journal = showpackets = cl_packetdelay = sv_packetdelay = &cvZero;
	cvMaxfps.integer = 1000 / frameMsec; com_maxfps = &cvMaxfps;
	cvRunning.integer = 1; com_sv_running = &cvRunning;
	cvTimescale.value = 1; cvTimescale.integer = 1; com_timescale = &cvTimescale;
}

/* Frames at now = 0, frameMsec, ... while now < end. A few quiet frames first
 * take what the last run left in the loopback and settle Com_Frame's pacing. */
void FakeReloadEngineRun(int frameMsec, int end) {
	int i;
	SetCvars(frameMsec);
	quiet = 1;
	for (i = 0; i < 4; i++) Com_Frame();
	quiet = 0;
	runBase = com_frameTime + frameMsec;
	do Com_Frame(); while (com_frameTime - runBase + frameMsec < end);
}

/* Only what SV_Frame sends the client waits: a command sent in a frame's
 * CL_Frame reaches the server in the next frame's first event loop; the
 * server's answer to it (as to a connect or a gamestate request) reaches the
 * client in that same frame's second event loop, as in ioquake3; a snapshot
 * sent in SV_Frame reaches it in the first event loop of the frame after.
 * Returns 0 and prints the log if not. */
int FakeReloadEngineLoopback(int frameMsec) {
	static const int expect[3][4] = {
		{ NS_SERVER, 1, 2, STAGE_FIRST_EVENTS },	/* the command */
		{ NS_CLIENT, 2, 2, STAGE_SECOND_EVENTS },	/* the answer */
		{ NS_CLIENT, 3, 3, STAGE_FIRST_EVENTS },	/* the snapshot */
	};
	int i, ok;
	SetCvars(frameMsec);
	quiet = 1;
	for (i = 0; i < 4; i++) Com_Frame();	/* empty the loopback */
	quiet = 0; probing = 1; probeFrame = probeCount = 0;
	for (i = 0; i < 4; i++) Com_Frame();
	probing = 0;
	ok = probeCount == 3 && !memcmp(probeLog, expect, sizeof(expect));
	if (!ok) for (i = 0; i < probeCount && i < 8; i++)
		fprintf(stderr, "  %s got %d in frame %d, stage %d\n", probeLog[i][0] == NS_CLIENT ? "client" : "server",
			probeLog[i][1], probeLog[i][2], probeLog[i][3]);
	return ok;
}

/* The engine Com_Frame calls that this host has nothing to do in. */
void IN_Frame(void) {	/* the start of each frame, just before its first event loop */
	probeFrame++; probeStage = STAGE_FIRST_EVENTS;
}
void Cbuf_Execute(void) { }
int SV_SendQueuedPackets(void) { return 1 << 20; }
void Sys_Print(const char *msg) { (void)msg; }
void CL_ConsolePrint(char *msg) { (void)msg; }
char *Sys_ConsoleInput(void) { return NULL; }
int c_traces, c_brush_traces, c_patch_traces, c_pointcontents;
int cvar_modifiedFlags;

/* Anything else is a path this test never takes. */
void CL_KeyEvent(int key, qboolean down, unsigned time) { (void)key; (void)down; (void)time; Fail("CL_KeyEvent"); }
void CL_CharEvent(int key) { (void)key; Fail("CL_CharEvent"); }
void CL_MouseEvent(int dx, int dy, int time) { (void)dx; (void)dy; (void)time; Fail("CL_MouseEvent"); }
void CL_JoystickEvent(int axis, int value, int time) { (void)axis; (void)value; (void)time; Fail("CL_JoystickEvent"); }
void Cbuf_AddText(const char *text) { (void)text; Fail("Cbuf_AddText"); }
void Cbuf_ExecuteText(int when, const char *text) { (void)when; (void)text; Fail("Cbuf_ExecuteText"); }
void CL_Disconnect(qboolean showMainMenu) { (void)showMainMenu; Fail("CL_Disconnect (Com_Error)"); }
void CL_Shutdown(char *finalmsg, qboolean disconnect, qboolean quit) { (void)finalmsg; (void)disconnect; (void)quit; Fail("CL_Shutdown (Com_Error)"); }
void CL_FlushMemory(void) { Fail("CL_FlushMemory"); }
void CL_Init(void) { Fail("CL_Init"); }
void CL_CDDialog(void) { Fail("CL_CDDialog"); }
qboolean CL_CDKeyValidate(const char *key, const char *checksum) { (void)key; (void)checksum; Fail("CL_CDKeyValidate"); return qfalse; }
qboolean UI_usesUniqueCDKey(void) { Fail("UI_usesUniqueCDKey"); return qfalse; }
void SV_Shutdown(char *finalmsg) { (void)finalmsg; Fail("SV_Shutdown (Com_Error)"); }
int SV_FrameMsec(void) { Fail("SV_FrameMsec"); return 0; }
void QDECL Sys_Error(const char *error, ...) { (void)error; Fail("Sys_Error"); exit(1); }
cpuFeatures_t Sys_GetProcessorFeatures(void) { return (cpuFeatures_t)0; }
cvar_t *Cvar_Get(const char *name, const char *value, int flags) { (void)name; (void)value; (void)flags; Fail("Cvar_Get"); return NULL; }
void Cvar_Set(const char *name, const char *value) {	/* Com_Error records its code and message first */
	fprintf(stderr, "%s %s\n", name, value);
	if (Q_stricmpn(name, "com_error", 9)) Fail("Cvar_Set");
}
void Cvar_SetValue(const char *name, float value) { (void)name; (void)value; Fail("Cvar_SetValue"); }
char *Cvar_VariableString(const char *name) { (void)name; Fail("Cvar_VariableString"); return NULL; }
void Cvar_WriteVariables(fileHandle_t f) { (void)f; Fail("Cvar_WriteVariables"); }
void Key_WriteBindings(fileHandle_t f) { (void)f; Fail("Key_WriteBindings"); }
void VM_Forced_Unload_Start(void) { Fail("VM_Forced_Unload_Start (Com_Error)"); }
void VM_Forced_Unload_Done(void) { Fail("VM_Forced_Unload_Done"); }
qboolean FS_Initialized(void) { return qfalse; }
int FS_Read(void *buffer, int len, fileHandle_t f) { (void)buffer; (void)len; (void)f; Fail("FS_Read"); return 0; }
int FS_Write(const void *buffer, int len, fileHandle_t f) { (void)buffer; (void)len; (void)f; Fail("FS_Write"); return 0; }
void QDECL FS_Printf(fileHandle_t f, const char *fmt, ...) { (void)f; (void)fmt; Fail("FS_Printf"); }
void FS_FCloseFile(fileHandle_t f) { (void)f; Fail("FS_FCloseFile"); }
void FS_ForceFlush(fileHandle_t f) { (void)f; Fail("FS_ForceFlush"); }
fileHandle_t FS_FOpenFileWrite_HomeConfig(const char *filename) { (void)filename; Fail("FS_FOpenFileWrite_HomeConfig"); return 0; }
fileHandle_t FS_FOpenFileAppend_HomeData(const char *filename) { (void)filename; Fail("FS_FOpenFileAppend_HomeData"); return 0; }
fileHandle_t FS_BaseDir_FOpenFileWrite_HomeState(const char *filename) { (void)filename; Fail("FS_BaseDir_FOpenFileWrite_HomeState"); return 0; }
void FS_Remove_HomeData(const char *homePath) { (void)homePath; Fail("FS_Remove_HomeData"); }
void FS_PureServerSetLoadedPaks(const char *pakSums, const char *pakNames) { (void)pakSums; (void)pakNames; Fail("FS_PureServerSetLoadedPaks"); }
