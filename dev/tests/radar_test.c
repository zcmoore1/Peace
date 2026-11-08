/* MW2's minimap as cgame draws it: the real cg_radar.c, its rectangles
 * captured. The UAV's pings (compassRadarPingFadeTime) and MW2's compass sound
 * pings (an enemy who fired: full for BG_SOUND_PING_HOLD, then faded over
 * compassSoundPingFadeTime, 0x4fe381-0x4fe3d4): where each is drawn, how
 * strong, and for how long. The recording side is in bullets_test.c, through
 * the real CG_FireWeapon. */
#include "../../code/cgame/cg_radar.c"

cg_t cg;
cgs_t cgs;
centity_t cg_entities[MAX_GENTITIES];
vmCvar_t cg_drawStatus;
static snapshot_t snap;
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)

void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level, const char *fmt, ...) { (void)level; (void)fmt; exit(1); }
void QDECL CG_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL CG_Error(const char *fmt, ...) { (void)fmt; exit(1); }

/* every rectangle drawn: the dots are squares, so a dot is any w == h */
typedef struct { float x, y, size, alpha, red; } dot_t;
static dot_t dots[512];
static int dotCount;
void CG_FillRect(float x, float y, float width, float height, const float *color) {
 if (width != height || dotCount >= ARRAY_LEN(dots)) return;
 dots[dotCount].x = x + width * 0.5f; dots[dotCount].y = y + height * 0.5f;
 dots[dotCount].size = width; dots[dotCount].alpha = color[3]; dots[dotCount].red = color[0];
 dotCount++;
}
void CG_DrawRect(float x, float y, float w, float h, float size, const float *color) { (void)x; (void)y; (void)w; (void)h; (void)size; (void)color; }
void CG_DrawSmallStringColor(int x, int y, const char *s, vec4_t color) { (void)x; (void)y; (void)s; (void)color; }
void trap_Cvar_VariableStringBuffer(const char *name, char *buffer, int size) { (void)name; Q_strncpyz(buffer, "0", size); }
int trap_Key_GetBindingName(const char *binding, qboolean gamepad, char *name, int size) { (void)binding; (void)gamepad; (void)name; (void)size; return -1; }
void CG_ReadableKeyName(const char *bind, char *out, int size) { (void)bind; Q_strncpyz(out, "?", size); }
static const char *args[8]; static int argc;
const char *CG_Argv(int arg) { return arg < argc ? args[arg] : ""; }
int trap_Argc(void) { return argc; }

static void Reset(void) {
 memset(&cg, 0, sizeof(cg)); memset(&cgs, 0, sizeof(cgs)); memset(&snap, 0, sizeof(snap));
 memset(cg_entities, 0, sizeof(cg_entities));
 cg_drawStatus.integer = 1;
 cg.snap = &snap; snap.ps.clientNum = 0;
 cg.time = 10000;
 /* the viewer at the origin facing +x: forward is up the minimap */
 VectorClear(cg.refdef.vieworg); VectorClear(cg.refdefViewAngles);
}
/* the enemy-coloured dots drawn by one frame (the player's own and teammates' are not red) */
static int Draw(dot_t *out, int max) {
 int i, n = 0;
 dotCount = 0; CG_DrawRadar();
 for (i = 0; i < dotCount; i++) if (dots[i].red == 1.0f && dots[i].size == RADAR_DOT + 1 && n < max) out[n++] = dots[i];
 return n;
}
/* where a world point falls, from the minimap's own scale */
static void Expect(float wx, float wy, float *x, float *y) {
 float half = RADAR_SIZE * 0.5f - RADAR_DOT, scale = half / BG_COMPASS_MAX_RANGE;
 *x = RADAR_X + RADAR_SIZE * 0.5f + (-wy) * scale;	/* right of a +x facing view is -y */
 *y = RADAR_Y + RADAR_SIZE * 0.5f - wx * scale;
}
#define NEAR(a,b) (fabs((a)-(b)) < 0.01f)

static void TestSoundPing(void) {
 dot_t d[8];
 float ex, ey;
 int t;
 Reset();
 /* an enemy fired 800 units ahead and 400 to the left a moment ago */
 cg.radarSoundTime[3] = cg.time + BG_SOUND_PING_HOLD;
 VectorSet(cg.radarSoundOrigin[3], 800, 400, 24);
 Expect(800, 400, &ex, &ey);
 /* full strength through the hold, where they fired */
 for (t = 0; t <= BG_SOUND_PING_HOLD; t += 50) {
  int stamp = cg.radarSoundTime[3];
  cg.time = stamp - BG_SOUND_PING_HOLD + t;
  CHECK(Draw(d, 8) == 1 && d[0].alpha == 1.0f && NEAR(d[0].x, ex) && NEAR(d[0].y, ey));
 }
 /* then fading straight down over compassSoundPingFadeTime */
 for (t = 50; t < BG_SOUND_PING_FADE_TIME; t += 50) {
  cg.time = cg.radarSoundTime[3] + t;
  CHECK(Draw(d, 8) == 1 && NEAR(d[0].alpha, 1.0f - (float)t / BG_SOUND_PING_FADE_TIME));
 }
 /* and gone at its end, for good */
 cg.time = cg.radarSoundTime[3] + BG_SOUND_PING_FADE_TIME;
 CHECK(Draw(d, 8) == 0 && cg.radarSoundTime[3] == 0);
 /* farther than compassMaxRange: held at the edge in its direction */
 Reset();
 cg.radarSoundTime[4] = cg.time + BG_SOUND_PING_HOLD; VectorSet(cg.radarSoundOrigin[4], BG_COMPASS_MAX_RANGE * 3, 0, 0);
 CHECK(Draw(d, 8) == 1 && NEAR(d[0].x, RADAR_X + RADAR_SIZE * 0.5f) && NEAR(d[0].y, RADAR_Y + RADAR_DOT));
 /* the view turned 90 degrees left: what was ahead is now to the right */
 Reset();
 cg.refdefViewAngles[YAW] = 90;
 cg.radarSoundTime[4] = cg.time + BG_SOUND_PING_HOLD; VectorSet(cg.radarSoundOrigin[4], 1000, 0, 0);
 CHECK(Draw(d, 8) == 1 && d[0].x > RADAR_X + RADAR_SIZE * 0.5f + 1 && NEAR(d[0].y, RADAR_Y + RADAR_SIZE * 0.5f));
 /* several at once, each its own */
 Reset();
 cg.radarSoundTime[1] = cg.time + BG_SOUND_PING_HOLD; VectorSet(cg.radarSoundOrigin[1], 100, 0, 0);
 cg.radarSoundTime[2] = cg.time - 1000; VectorSet(cg.radarSoundOrigin[2], 0, 100, 0);
 CHECK(Draw(d, 8) == 2);
 /* a stamp further ahead than its hold came before cg.time went back (a new
  * map's clock): dropped, never shown for longer than a ping can be */
 Reset();
 cg.radarSoundTime[5] = cg.time + BG_SOUND_PING_HOLD + 1; VectorSet(cg.radarSoundOrigin[5], 100, 0, 0);
 CHECK(Draw(d, 8) == 0 && cg.radarSoundTime[5] == 0);
 /* no status HUD, no minimap */
 Reset(); cg_drawStatus.integer = 0;
 cg.radarSoundTime[1] = cg.time + BG_SOUND_PING_HOLD;
 dotCount = 0; CG_DrawRadar(); CHECK(dotCount == 0);
}

static void TestUAVPings(void) {
 dot_t d[8];
 Reset();
 /* a sweep with two enemies, every compassRadarUpdateTime */
 argc = 8; args[0] = "uav"; args[1] = "4000";
 args[2] = "500"; args[3] = "0"; args[4] = "0";
 args[5] = "0"; args[6] = "-500"; args[7] = "90";
 CG_ParseUAV();
 CHECK(cg.radarPingCount == 2 && cg.radarSweepTime == cg.time && cg.radarInterval == BG_RADAR_UPDATE_TIME);
 CHECK(Draw(d, 8) == 2 && d[0].alpha == 1.0f);
 /* fading over compassRadarPingFadeTime from the sweep */
 cg.time += BG_RADAR_PING_FADE_TIME / 2;
 CHECK(Draw(d, 8) == 2 && NEAR(d[0].alpha, 0.5f));
 cg.time = cg.radarSweepTime + BG_RADAR_PING_FADE_TIME;
 CHECK(Draw(d, 8) == 0);
 /* "uav 0": no radar */
 argc = 2; args[1] = "0"; CG_ParseUAV();
 CHECK(cg.radarSweepTime == 0 && cg.radarPingCount == 0);
}

int main(void) {
 TestSoundPing();
 TestUAVPings();
 printf("PASS: %d minimap checks (UAV pings, sound pings)\n", checks);
 return 0;
}
