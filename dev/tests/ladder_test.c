/* Exercise real prediction/weapon code against the shipped BSP, using the real
 * engine collision loader and traces. Only filesystem/allocator/cvar glue is
 * mocked; no ladder detection or physics is duplicated in this test. */
#include "../../code/game/bg_pmove.c"
#include "../../code/qcommon/cm_local.h"

static playerState_t ps;
static pmove_t move;
static int checks;
static void *allocations[128];
static int numAllocations;

#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s (pos %.2f %.2f %.2f, vel %.2f %.2f %.2f, ladder %d, weapon %d, lock %d)\n", \
    __FILE__, __LINE__, #x, ps.origin[0], ps.origin[1], ps.origin[2], ps.velocity[0], ps.velocity[1], ps.velocity[2], \
    ps.stats[STAT_LADDER], ps.weaponstate, ps.weaponTime); exit(1); } } while (0)

void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_DPrintf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level, const char *fmt, ...) {
    va_list args;
    (void)level;
    va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args);
    exit(1);
}
void trap_SnapVector(float *v) { SnapVector(v); }
#ifdef HUNK_DEBUG
void *Hunk_AllocDebug(int size, ha_pref preference, char *label, char *file, int line) {
    (void)label; (void)file; (void)line;
#else
void *Hunk_Alloc(int size, ha_pref preference) {
#endif
    void *p = calloc(1, size);
    (void)preference;
    CHECK(p && numAllocations < ARRAY_LEN(allocations));
    allocations[numAllocations++] = p;
    return p;
}
#ifdef ZONE_DEBUG
void *Z_MallocDebug(int size, char *label, char *file, int line) {
    (void)label; (void)file; (void)line;
    return calloc(1, size);
}
#else
void *Z_Malloc(int size) { return calloc(1, size); }
#endif
void Z_Free(void *p) { free(p); }
void *Hunk_AllocateTempMemory(int size) { return malloc(size); }
void Hunk_FreeTempMemory(void *p) { free(p); }
long FS_ReadFile(const char *name, void **buffer) {
    FILE *f = fopen(name, "rb");
    long size;
    CHECK(f != NULL);
    fseek(f, 0, SEEK_END); size = ftell(f); rewind(f);
    *buffer = malloc(size);
    CHECK(*buffer && fread(*buffer, 1, size, f) == size);
    fclose(f);
    return size;
}
void FS_FreeFile(void *buffer) { free(buffer); }
cvar_t *Cvar_Get(const char *name, const char *value, int flags) {
    static cvar_t vars[8];
    static int count;
    cvar_t *v = &vars[count++];
    (void)name; (void)flags;
    CHECK(count <= ARRAY_LEN(vars));
    v->integer = atoi(value); v->value = atof(value);
    return v;
}

static void Trace(trace_t *tr, const vec3_t start, const vec3_t mins,
                  const vec3_t maxs, const vec3_t end, int pass, int mask) {
    vec3_t lo, hi;
    (void)pass;
    VectorCopy(mins, lo); VectorCopy(maxs, hi);
    CM_BoxTrace(tr, start, end, lo, hi, 0, mask, qfalse);
    tr->entityNum = tr->fraction < 1 ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
}
static int Contents(const vec3_t point, int pass) {
    (void)pass;
    return CM_PointContents(point, 0);
}
static void Reset(float x, float y, float z) {
    memset(&ps, 0, sizeof(ps));
    memset(&move, 0, sizeof(move));
    memset(&pml, 0, sizeof(pml));
    move.ps = &ps;
    move.cmd.weapon = ps.weapon = WP_SHOTGUN;
    move.trace = Trace; move.pointcontents = Contents;
    move.tracemask = MASK_PLAYERSOLID;
    ps.pm_type = PM_NORMAL;
    ps.speed = 320; ps.gravity = 800;
    ps.groundEntityNum = ENTITYNUM_NONE;
    ps.weaponstate = WEAPON_READY;
    ps.weaponAnimTime = -1;
    ps.stats[STAT_HEALTH] = ps.stats[STAT_MAX_HEALTH] = 100;
    ps.stats[STAT_WEAPONS] = (1 << WP_SHOTGUN) | (1 << WP_MACHINEGUN);
    ps.stats[STAT_SLOT_PRIMARY] = WP_SHOTGUN;
    ps.stats[STAT_SLOT_SECONDARY] = WP_MACHINEGUN;
    ps.ammo[WP_SHOTGUN] = 3; ps.ammoReserve[WP_SHOTGUN] = 10;
    ps.ammo[WP_MACHINEGUN] = 10; ps.ammoReserve[WP_MACHINEGUN] = 60;
    VectorSet(ps.origin, x, y, z);
    pm = &move;
}
static void Tick(int dt) {
    move.cmd.serverTime = ps.commandTime + dt;
    Pmove(&move);
}
static void Run(int ms, int step) {
    while (ms > 0) {
        int dt = ms < step ? ms : step;
        Tick(dt); ms -= dt;
    }
}
static qboolean Attached(void) { return !!(ps.stats[STAT_LADDER] & LADDER_ATTACHED); }

static void TestGrabAndHang(int dt) {
    int yaw;
    for (yaw = 0; yaw < 360; yaw += 45) {
        Reset(111, 0, 220);
        move.cmd.angles[YAW] = ANGLE2SHORT(yaw);
        VectorSet(ps.velocity, 80, 40, -100);
        Tick(dt);
        CHECK(Attached());
        CHECK(ps.velocity[0] == 0);
        CHECK(ps.velocity[1] > 0 && ps.velocity[2] < 0);
        CHECK(ps.origin[1] > 0 && ps.origin[2] < 220);
        CHECK(fabs(AngleSubtract(ps.viewangles[YAW], LADDER_FACING(ps.stats[STAT_LADDER]))) <= 90.01f);
        CHECK(ps.weaponstate == WEAPON_DROPPING);
        CHECK(ps.weaponTime == BG_LADDER_DROP_TIME);
        Run(BG_LADDER_DROP_TIME, dt);
        CHECK(ps.weaponstate == WEAPON_LADDER);
        Run(1000, dt);
        CHECK(Attached() && VectorLength(ps.velocity) == 0);
        {
            vec3_t held;
            VectorCopy(ps.origin, held);
            Run(200, dt);
            CHECK(VectorCompare(held, ps.origin));
        }
    }
    Reset(106, 0, 220);
    ps.velocity[0] = 300;
    Run(64, dt);
    CHECK(Attached()); /* airborne contact part-way through movement */
    Reset(111, 140, 220); /* platform wall, not ladder */
    Run(64, dt);
    CHECK(!Attached());
    Reset(-368, 0, 220); /* ordinary room wall */
    Tick(dt);
    CHECK(!Attached());
}

static void TestTraverseAndExit(int dt) {
    Reset(111, 0, 180);
    Tick(dt);
    move.cmd.forwardmove = 127;
    Run(240, dt);
    CHECK(ps.origin[2] > 190 && Attached());
    move.cmd.forwardmove = -127;
    Run(480, dt);
    CHECK(ps.origin[2] < 190 && Attached());
    move.cmd.forwardmove = 0; move.cmd.rightmove = 127;
    Run(180, dt);
    CHECK(ps.origin[1] < -5 && Attached());
    Run(800, dt);
    CHECK(!Attached()); /* walk off side without a jump button */

    Reset(111, 45, 180);
    ps.velocity[1] = 500;
    Run(160, dt);
    CHECK(!Attached() && ps.origin[1] > 72); /* momentum can leave the edge */
    CHECK(ps.velocity[1] > 0 && ps.velocity[2] < 0);

    Reset(111, 0, 300);
    move.cmd.forwardmove = 127;
    Run(1000, dt);
    CHECK(ps.origin[0] > 155 && ps.origin[2] >= 343);
    CHECK(!Attached()); /* climbed onto the platform, not stuck at its lip */
}

static void TestJump(int dt) {
    Reset(111, 0, 180);
    ps.pm_flags |= PMF_JUMP_HELD;
    move.cmd.upmove = 127;
    Tick(dt);
    CHECK(Attached()); /* holding the approach jump is not a new jump press */
    move.cmd.upmove = 0; Tick(dt);
    move.cmd.upmove = 127; Tick(dt);
    CHECK(!Attached());
    CHECK(ps.velocity[0] < 0 && ps.velocity[2] > 0);
    CHECK(ps.pm_flags & PMF_JUMP_HELD);
    move.cmd.upmove = 0;
    Run(160, dt);
    CHECK(!Attached());
    VectorSet(ps.origin, 111, 0, 180);
    VectorClear(ps.velocity);
    Tick(dt);
    CHECK(Attached()); /* actual separation re-arms the next grab */

    Reset(111, 0, 180);
    Tick(1);
    move.cmd.upmove = 127; Tick(1);
    CHECK(!Attached() && (ps.stats[STAT_LADDER] & LADDER_JUMP_OFF));
    VectorClear(ps.velocity);
    move.cmd.upmove = 0; Tick(1);
    CHECK(!Attached()); /* still within reach: must not immediately re-stick */
}

static int Note(int weapon, int seq, weaponNote_t kind) {
    const bg_weaponNote_t *n = PM_SegNotes(weapon, seq);
    for (; n->note != WNOTE_NONE; n++) if (n->note == kind) return n->time;
    CHECK(0);
    return 0;
}
static void WeaponTick(int dt) { pml.msec = dt; PM_Weapon(); pml.ladderGrabbed = qfalse; }
static void TestWeaponOrdering(void) {
    int weapon, offset;
    for (weapon = WP_MACHINEGUN; weapon <= WP_SHOTGUN; weapon++) {
        for (offset = -1; offset <= 1; offset++) {
            int note = Note(weapon, RSEQ_LOOP, WNOTE_MAG_IN);
            Reset(111, 0, 180);
            move.cmd.weapon = ps.weapon = weapon;
            ps.weaponstate = WEAPON_RELOADING;
            ps.weaponAnimSeq = RSEQ_LOOP;
            ps.weaponAnimTime = note + offset - 1;
            ps.weaponTime = BG_WeaponReloadSegLength(weapon, RSEQ_LOOP) - ps.weaponAnimTime;
            Tick(1); /* actual movement contact, then the normal weapon pass */
            CHECK(Attached());
            CHECK(ps.weaponstate == (offset == 0 ? WEAPON_LADDER : WEAPON_DROPPING));
            CHECK(ps.weaponTime == (offset == 0 ? 0 : BG_LADDER_DROP_TIME));
            CHECK(ps.ammo[weapon] == (weapon == WP_SHOTGUN ? 3 : 10));
            CHECK(ps.ammoReserve[weapon] == (weapon == WP_SHOTGUN ? 10 : 60));
            CHECK(!(ps.pm_flags & PMF_PENDING_MAG));
            if (offset != 0) WeaponTick(BG_LADDER_DROP_TIME);
            CHECK(ps.weaponstate == WEAPON_LADDER && ps.weaponAnimTime == -1);
            move.cmd.buttons = BUTTON_ATTACK | BUTTON_RELOAD | BUTTON_ADS | BUTTON_SPRINT | BUTTON_MELEE;
            move.cmd.weapon = weapon == WP_SHOTGUN ? WP_MACHINEGUN : WP_SHOTGUN;
            WeaponTick(BG_LADDER_DROP_TIME);
            CHECK(ps.weaponstate == WEAPON_LADDER && ps.weapon == weapon);
            PM_CheckADS(); PM_CheckSprint();
            CHECK(!(ps.pm_flags & (PMF_ADS | PMF_SPRINTING)));
            ps.stats[STAT_LADDER] = 0;
            move.cmd.buttons = 0;
            WeaponTick(1);
            CHECK(ps.weaponstate == WEAPON_RAISING && ps.weapon == move.cmd.weapon);
            CHECK(ps.weaponTime == BG_WeaponRaiseTime(ps.weapon));
        }
    }
    /* An interrupted bolt still owes its complete cycle; only its own close
       note may clear the debt, including on the exact grab think. */
    for (offset = -1; offset <= 0; offset++) {
        Reset(111, 0, 180);
        ps.weaponstate = WEAPON_BOLTING;
        ps.weaponAnimSeq = ASEQ_FIRE;
        ps.weaponAnimTime = Note(WP_SHOTGUN, ASEQ_FIRE, WNOTE_BOLT_CLOSED) + offset - 1;
        ps.weaponTime = BG_WeaponFireLength(WP_SHOTGUN) - ps.weaponAnimTime;
        ps.stats[STAT_UNCHAMBERED] = 1 << WP_SHOTGUN;
        ps.stats[STAT_LADDER] = LADDER_ATTACHED;
        WeaponTick(1);
        CHECK(!!ps.stats[STAT_UNCHAMBERED] == (offset != 0));
        if (offset) WeaponTick(BG_LADDER_DROP_TIME);
        ps.stats[STAT_LADDER] = 0;
        WeaponTick(1);
        WeaponTick(BG_WeaponRaiseTime(WP_SHOTGUN));
        CHECK(ps.weaponstate == WEAPON_READY);
        WeaponTick(1);
        CHECK(ps.weaponstate == (offset ? WEAPON_BOLTING : WEAPON_READY));
    }
}

static void TestViewAndModes(void) {
    int yaw, mode;
    for (yaw = 0; yaw < 360; yaw++) {
        Reset(111, 0, 180);
        ps.stats[STAT_LADDER] = LADDER_ATTACHED |
            ((ANGLE2SHORT(yaw) >> 4) << LADDER_YAW_SHIFT);
        CHECK((short)ps.stats[STAT_LADDER] == ps.stats[STAT_LADDER]);
        move.cmd.angles[YAW] = ANGLE2SHORT(yaw + 120);
        PM_UpdateViewAngles(&ps, &move.cmd);
        CHECK(fabs(AngleSubtract(ps.viewangles[YAW], LADDER_FACING(ps.stats[STAT_LADDER]))) <= 90.01f);
        move.cmd.angles[YAW] -= ANGLE2SHORT(10);
        PM_UpdateViewAngles(&ps, &move.cmd);
        CHECK(fabs(AngleSubtract(ps.viewangles[YAW], LADDER_FACING(ps.stats[STAT_LADDER])) - 80) < 0.02f);
    }
    for (mode = PM_NOCLIP; mode <= PM_SPINTERMISSION; mode++) {
        Reset(111, 0, 180);
        Tick(8); CHECK(Attached());
        ps.pm_type = mode;
        Tick(8); CHECK(!Attached());
    }
    Reset(111, 0, 180);
    ps.stats[STAT_HEALTH] = 0;
    Tick(8); CHECK(!Attached());

    Reset(111, 0, 180);
    move.cmd.buttons = BUTTON_ATTACK | BUTTON_RELOAD | BUTTON_ADS | BUTTON_SPRINT | BUTTON_USE_HOLDABLE;
    move.cmd.forwardmove = 127;
    Tick(8);
    CHECK(ps.weaponstate == WEAPON_DROPPING && ps.weaponTime == BG_LADDER_DROP_TIME);
    Run(BG_LADDER_DROP_TIME, 8);
    CHECK(ps.weaponstate == WEAPON_LADDER && ps.ammo[WP_SHOTGUN] == 3);
    CHECK(!(ps.eFlags & EF_FIRING));
    CHECK(!(ps.pm_flags & (PMF_ADS | PMF_SPRINTING | PMF_USE_ITEM_HELD)));
}

static void TestPredictionReplay(int dt) {
    playerState_t start, expected;
    int i, j;
    Reset(111, 0, 180);
    Tick(dt);
    start = ps;
    for (j = 0; j < 2; j++) {
        ps = start;
        /* Simulate signed-short stat snapshot serialization. */
        for (i = 0; i < MAX_STATS; i++) ps.stats[i] = (short)ps.stats[i];
        for (i = 0; i < 50; i++) {
            move.cmd.forwardmove = i < 20 ? 127 : 0;
            move.cmd.rightmove = i >= 20 && i < 35 ? 127 : 0;
            move.cmd.upmove = i == 35 ? 127 : 0;
            Tick(dt);
        }
        if (!j) expected = ps;
        else CHECK(memcmp(&expected, &ps, sizeof(ps)) == 0);
    }
}

int main(void) {
    int checksum, i;
    const int steps[] = {8, 16, 33, 66};
    CM_LoadMap("assets/baseq3/maps/peace_ladder.bsp", qfalse, &checksum);
    CHECK(CM_NumInlineModels() == 1 && CM_NumClusters() == 1);
    for (i = 0; i < ARRAY_LEN(steps); i++) {
        TestGrabAndHang(steps[i]);
        TestTraverseAndExit(steps[i]);
        TestJump(steps[i]);
        TestPredictionReplay(steps[i]);
    }
    TestWeaponOrdering();
    TestViewAndModes();
    for (i = 0; i < numAllocations; i++) free(allocations[i]);
    printf("PASS: ladder BSP collision, momentum, traversal, detach, yaw, weapon ordering and prediction replay (%d checks).\n", checks);
    return 0;
}
