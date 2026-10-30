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
/* The engine's Q_SnapVector rounds to nearest, as MW2's does; q_shared's macro truncates. */
void trap_SnapVector(float *v) { v[0] = rintf(v[0]); v[1] = rintf(v[1]); v[2] = rintf(v[2]); }
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

static void TestBottomExit(int dt) {
    float bottom;
    Reset(111, 0, 100);
    Tick(dt);
    CHECK(Attached());
    move.cmd.forwardmove = -127;
    Run(1200, dt);
    CHECK(!Attached() && ps.groundEntityNum == ENTITYNUM_WORLD);
    CHECK(ps.origin[0] < 100); /* down becomes normal backpedaling on the floor */
    Run(BG_WeaponRaiseTime(WP_SHOTGUN), dt);
    CHECK(ps.weaponstate == WEAPON_READY);

    /* Contact while standing still must not re-holster on every think. */
    Reset(111, 0, 24.125f); /* collision hull rests an epsilon above the floor */
    Run(200, dt);
    CHECK(!Attached() && ps.weaponstate == WEAPON_READY);
    bottom = ps.origin[2];
    move.cmd.rightmove = 127;
    Run(100, dt);
    CHECK(!Attached() && ps.origin[1] < -1);

    /* No cooldown: forward climbs straight back up from the same contact. */
    move.cmd.rightmove = 0;
    move.cmd.forwardmove = 127;
    Run(200, dt);
    CHECK(Attached() && ps.origin[2] > bottom + 5);

    /* An airborne, no-input grab still slides; only floor contact releases. */
    Reset(111, 0, 35);
    ps.velocity[2] = -200;
    Tick(1);
    CHECK(Attached());
    Run(300, dt);
    CHECK(!Attached() && ps.groundEntityNum == ENTITYNUM_WORLD);
    Run(BG_WeaponRaiseTime(WP_SHOTGUN) + BG_LADDER_DROP_TIME, dt);
    CHECK(!Attached() && ps.weaponstate == WEAPON_READY);
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

/* A ladder ends a sprint, and with sprint held it waits for the button to
 * come up before the next one (MW2, CoD4's PM_EndSprint). */
static void TestLadderEndsSprint(void) {
    int t, sprinted = 0;
    Reset(-150, 0, 24.125f); move.cmd.buttons = BUTTON_SPRINT; move.cmd.forwardmove = 127;
    for (t = 0; t < 400 && !Attached(); t++) { Tick(8); sprinted |= !Attached() && (ps.pm_flags & PMF_SPRINTING); }
    CHECK(sprinted && Attached());
    CHECK(!(ps.pm_flags & PMF_SPRINTING) && (ps.weapFlags & PWF_SPRINT_RELEASE));
    for (t = 0; t < 600; t++) { Tick(8); CHECK(!(ps.pm_flags & PMF_SPRINTING)); }
    CHECK(!Attached() && ps.groundEntityNum != ENTITYNUM_NONE);
    move.cmd.buttons = 0; Tick(8); move.cmd.buttons = BUTTON_SPRINT; Tick(8);
    CHECK(ps.pm_flags & PMF_SPRINTING);
}
/* A jump ends a sprint but never makes its button come up first (MW2's
 * Jump_Start clears sprintButtonUpRequired): with sprint held it is back as
 * soon as jump is let go, in the air. Holding jump keeps one from starting,
 * on the floor as well. */
static void TestJumpSprint(void) {
    int t;
    Reset(-300, -230, 24.125f); move.cmd.angles[YAW] = ANGLE2SHORT(90);
    move.cmd.buttons = BUTTON_SPRINT; move.cmd.forwardmove = 127;
    for (t = 0; t < 6; t++) Tick(8);
    CHECK((ps.pm_flags & PMF_SPRINTING) && ps.groundEntityNum != ENTITYNUM_NONE);
    move.cmd.upmove = 127; Tick(8); Tick(8);
    CHECK(ps.groundEntityNum == ENTITYNUM_NONE && !(ps.pm_flags & PMF_SPRINTING));
    CHECK(!(ps.weapFlags & PWF_SPRINT_RELEASE));
    move.cmd.upmove = 0; Tick(8); Tick(8);
    CHECK(ps.groundEntityNum == ENTITYNUM_NONE && (ps.pm_flags & PMF_SPRINTING));
    Reset(-300, -230, 24.125f); move.cmd.angles[YAW] = ANGLE2SHORT(90);
    move.cmd.buttons = BUTTON_SPRINT; move.cmd.upmove = 127;
    for (t = 0; t < 200 && !(t > 10 && ps.groundEntityNum != ENTITYNUM_NONE); t++) Tick(8);
    CHECK(ps.groundEntityNum != ENTITYNUM_NONE && (ps.pm_flags & PMF_JUMP_HELD));
    move.cmd.forwardmove = 127; for (t = 0; t < 5; t++) Tick(8);
    CHECK(!(ps.pm_flags & PMF_SPRINTING));
    move.cmd.upmove = 0; Tick(8); Tick(8);
    CHECK(ps.pm_flags & PMF_SPRINTING);
}
static float FlatSpeed(void) { return sqrt(ps.velocity[0] * ps.velocity[0] + ps.velocity[1] * ps.velocity[1]); }
/* MW2's landing, on real collision: a jump's touchdown keeps 0.67 of the
 * velocity (PM_CrashLand), the first walking think 0.65 more and starts an
 * 1800ms slowdown (Jump_ApplySlowdown), whose friction and next jump are
 * scaled by BG_JumpLandFactor; a jump within BG_JUMP_WAIT of the last is
 * refused, and the press is spent. */
static void TestLanding(void) {
    float air, v, top; int t, landed;
    Reset(-300, -240, 24.125f); move.cmd.angles[YAW] = ANGLE2SHORT(90);
    move.cmd.forwardmove = 127;
    for (t = 0; t < 40; t++) Tick(8);
    CHECK(ps.groundEntityNum != ENTITYNUM_NONE && fabs(FlatSpeed() - ps.speed) < 2);
    v = ps.origin[2];
    move.cmd.upmove = 127; Tick(8); move.cmd.upmove = 0;
    CHECK(ps.groundEntityNum == ENTITYNUM_NONE && ps.jumpLand == -1 &&
          ps.jumpWait > 0 && ps.velocity[2] > 0);
    /* MW2's jump_height, whatever the speed: BG_JUMP_HEIGHT, and a little more
     * at 8ms commands, where rounding the speed every think lifts it, as
     * MW2's at 125fps (6.4 u/s of gravity a think rounds back up by 0.4). */
    for (t = 0, top = ps.origin[2]; t < 200 && ps.groundEntityNum == ENTITYNUM_NONE; t++) {
        air = FlatSpeed(); Tick(8); if (ps.origin[2] > top) top = ps.origin[2];
    }
    CHECK(top - v > BG_JUMP_HEIGHT + 1.5f && top - v < BG_JUMP_HEIGHT + 3);
    CHECK(ps.groundEntityNum != ENTITYNUM_NONE && ps.jumpLand == -1);
    CHECK(fabs(FlatSpeed() - air * 0.67f) < 2);               /* touchdown */
    v = FlatSpeed(); Tick(8);
    CHECK(ps.jumpLand == BG_JUMP_LAND_TIME && FlatSpeed() < v * 0.65f + 10);
    for (t = 0; t < 60; t++) Tick(8);                          /* about half a second */
    CHECK(FlatSpeed() < ps.speed * 0.85f && ps.origin[1] < 250);
    move.cmd.forwardmove = 0;
    for (t = 0; t < 300 && ps.jumpLand; t++) Tick(8);
    CHECK(ps.jumpLand == 0 && t * 8 <= BG_JUMP_LAND_TIME - 60 * 8 + 8);
    /* A jump right after a landing is lower, by the slowdown's factor. */
    Reset(-300, -230, 24.125f); move.cmd.angles[YAW] = ANGLE2SHORT(90);
    move.cmd.upmove = 127; Tick(8); move.cmd.upmove = 0;
    for (t = 0; t < 200 && ps.groundEntityNum == ENTITYNUM_NONE; t++) Tick(8);
    landed = ps.groundEntityNum != ENTITYNUM_NONE;
    CHECK(landed && ps.jumpWait == 0);
    Tick(8); CHECK(ps.jumpLand == BG_JUMP_LAND_TIME);
    move.cmd.upmove = 127; Tick(8); move.cmd.upmove = 0;
    CHECK(ps.groundEntityNum == ENTITYNUM_NONE &&
          ps.velocity[2] < BG_JumpVelocity(&ps) / sqrt(BG_JumpLandFactor(BG_JUMP_LAND_TIME - 8)) + 1 &&
          ps.velocity[2] > BG_JumpVelocity(&ps) / sqrt(2.5f) - 10);
    /* That low hop lands inside the wait: the press there is spent, held or
     * not, and only a fresh press after the wait jumps. */
    for (t = 0; t < 200 && ps.groundEntityNum == ENTITYNUM_NONE; t++) Tick(8);
    CHECK(ps.groundEntityNum != ENTITYNUM_NONE && ps.jumpWait > 0);
    for (t = 0; t < 30; t++) { move.cmd.upmove = 127; Tick(8); CHECK(ps.groundEntityNum != ENTITYNUM_NONE); }
    CHECK(ps.jumpWait == 0);
    move.cmd.upmove = 0; Tick(8); move.cmd.upmove = 127; Tick(8); move.cmd.upmove = 0;
    CHECK(ps.groundEntityNum == ENTITYNUM_NONE);
    /* A press made in the air is spent by touchdown (MW2's Jump_Check reads
     * the command before): held through the landing it never jumps. */
    for (t = 0; t < 300; t++) Tick(8);
    CHECK(ps.groundEntityNum != ENTITYNUM_NONE && ps.jumpWait == 0 && ps.jumpLand == 0);
    move.cmd.upmove = 127; Tick(8); move.cmd.upmove = 0; Tick(8); Tick(8);
    CHECK(ps.groundEntityNum == ENTITYNUM_NONE);
    move.cmd.upmove = 127;
    for (t = 0; t < 200 && ps.groundEntityNum == ENTITYNUM_NONE; t++) Tick(8);
    CHECK(ps.groundEntityNum != ENTITYNUM_NONE && ps.jumpWait == 0);
    for (t = 0; t < 30; t++) { Tick(8); CHECK(ps.groundEntityNum != ENTITYNUM_NONE); }
    move.cmd.upmove = 0; Tick(8); move.cmd.upmove = 127; Tick(8); move.cmd.upmove = 0;
    CHECK(ps.groundEntityNum == ENTITYNUM_NONE);
    /* A landing a step above the take-off cuts to 0.5 and slows for 1200ms. */
    for (t = 0; t < 2; t++) {
        Reset(-300, -230, 24.125f); Tick(8);
        CHECK(ps.groundEntityNum != ENTITYNUM_NONE);
        ps.jumpLand = -1; ps.jumpOriginZ = ps.origin[2] - (t ? BG_JUMP_LAND_STEP : 0);
        ps.velocity[0] = 200; move.cmd.forwardmove = 0; Tick(8);
        CHECK(ps.jumpLand == (t ? BG_JUMP_LAND_HIGH : BG_JUMP_LAND_TIME));
        CHECK(ps.velocity[0] < (t ? 0.5f : 0.65f) * 200 + 0.5f && ps.velocity[0] > (t ? 0.5f : 0.65f) * 200 - 30);
    }
    /* A ladder clears the jump state. */
    Reset(-150, 0, 24.125f); move.cmd.forwardmove = 127;
    for (t = 0; t < 400 && !Attached(); t++) Tick(8);
    CHECK(Attached());
    ps.jumpLand = 900; ps.jumpOriginZ = 1; Tick(8);
    CHECK(Attached() && ps.jumpLand == 0 && ps.jumpOriginZ == 0);
    move.cmd.forwardmove = 0;
}
/* MW2's step down (PM_StepSlideMove): from the ground, a drop of up to 9
 * units is walked, not flown - so stairs and curbs cost no landing cut and
 * keep a landing's slowdown. Higher drops still fly. Brushes are the engine's
 * own temporary box models, so the clipping is Q3's. */
/* A box may stand for a player (its entity number, CONTENTS_BODY), for water
 * (pointcontents only), or carry one sloped face along y. A sloped box is
 * traced as CM_TraceThroughBrush traces a brush: its six axial planes are
 * the bevels, so the box clip is exact. */
typedef struct { vec3_t mn, mx, n; float d; int ent, contents; qboolean sloped; } stairBox_t;
static stairBox_t stairBoxes[24]; static int numStairBoxes;
static void AddStairBox(float x0, float y0, float z0, float x1, float y1, float z1) {
    stairBox_t *b = &stairBoxes[numStairBoxes++];
    CHECK(numStairBoxes <= ARRAY_LEN(stairBoxes));
    memset(b, 0, sizeof(*b)); b->ent = ENTITYNUM_WORLD; b->contents = CONTENTS_SOLID;
    VectorSet(b->mn, x0, y0, z0); VectorSet(b->mx, x1, y1, z1);
}
static void AddPlayerBox(int ent, float x0, float y0, float z0, float x1, float y1, float z1) {
    AddStairBox(x0, y0, z0, x1, y1, z1);
    stairBoxes[numStairBoxes - 1].ent = ent; stairBoxes[numStairBoxes - 1].contents = CONTENTS_BODY;
}
static void AddWaterBox(float x0, float y0, float z0, float x1, float y1, float z1) {
    AddStairBox(x0, y0, z0, x1, y1, z1); stairBoxes[numStairBoxes - 1].contents = CONTENTS_WATER;
}
/* Solid below the face through (y = 0, zAtY0) whose normal is (0, ny, nz). */
static void AddSlopeBox(float x0, float y0, float z0, float x1, float y1, float z1,
    float ny, float nz, float zAtY0) {
    stairBox_t *b = &stairBoxes[numStairBoxes];
    AddStairBox(x0, y0, z0, x1, y1, z1);
    VectorSet(b->n, 0, ny, nz); VectorNormalize(b->n);
    b->d = b->n[2] * zAtY0; b->sloped = qtrue;
}
static void SlopeBoxTrace(trace_t *t, const stairBox_t *b, const vec3_t start, const vec3_t mins,
    const vec3_t maxs, const vec3_t end) {
    float enter = -1, leave = 1; int i, k, clip = -1; qboolean startout = qfalse, getout = qfalse;
    memset(t, 0, sizeof(*t)); t->fraction = 1;
    for (i = 0; i < 7; i++) {
        vec3_t n = { 0, 0, 0 }; float d, off = 0, d1, d2, f;
        if (i < 6) { n[i >> 1] = (i & 1) ? -1 : 1; d = (i & 1) ? -b->mn[i >> 1] : b->mx[i >> 1]; }
        else { VectorCopy(b->n, n); d = b->d; }
        for (k = 0; k < 3; k++) off += n[k] * (n[k] < 0 ? maxs[k] : mins[k]);
        d1 = DotProduct(start, n) - (d - off); d2 = DotProduct(end, n) - (d - off);
        if (d2 > 0) getout = qtrue;
        if (d1 > 0) startout = qtrue;
        if (d1 > 0 && (d2 >= SURFACE_CLIP_EPSILON || d2 >= d1)) return;
        if (d1 <= 0 && d2 <= 0) continue;
        if (d1 > d2) {
            f = (d1 - SURFACE_CLIP_EPSILON) / (d1 - d2); if (f < 0) f = 0;
            if (f > enter) { enter = f; clip = i; VectorCopy(n, t->plane.normal); }
        } else {
            f = (d1 + SURFACE_CLIP_EPSILON) / (d1 - d2); if (f > 1) f = 1;
            if (f < leave) leave = f;
        }
    }
    if (!startout) { t->startsolid = qtrue; if (!getout) { t->allsolid = qtrue; t->fraction = 0; } return; }
    if (enter < leave && enter > -1 && clip >= 0) t->fraction = enter < 0 ? 0 : enter;
}
static void StairTrace(trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs,
    const vec3_t end, int pass, int mask) {
    int i, k, ent = ENTITYNUM_NONE, contents = 0; trace_t t; vec3_t lo, hi; (void)pass;
    memset(tr, 0, sizeof(*tr)); tr->fraction = 1; VectorCopy(end, tr->endpos);
    VectorCopy(mins, lo); VectorCopy(maxs, hi);
    for (i = 0; i < numStairBoxes; i++) {
        if (!(stairBoxes[i].contents & mask)) continue;
        if (stairBoxes[i].sloped) {
            SlopeBoxTrace(&t, &stairBoxes[i], start, lo, hi, end);
            for (k = 0; k < 3; k++) t.endpos[k] = start[k] + t.fraction * (end[k] - start[k]);
        } else {
            clipHandle_t h = CM_TempBoxModel(stairBoxes[i].mn, stairBoxes[i].mx, qfalse);
            CM_BoxTrace(&t, start, end, lo, hi, h, mask, qfalse);
        }
        if (t.allsolid) tr->allsolid = qtrue;
        if (t.startsolid) tr->startsolid = qtrue;
        if (t.fraction < tr->fraction) {
            qboolean a = tr->allsolid, st = tr->startsolid; *tr = t; tr->allsolid = a; tr->startsolid = st;
            ent = stairBoxes[i].ent; contents = stairBoxes[i].contents;
        }
    }
    tr->entityNum = tr->fraction < 1 ? ent : tr->startsolid ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
    if (tr->fraction < 1) tr->contents = contents;
}
static int StairContents(const vec3_t p, int pass) {
    int i, c = 0; (void)pass;
    for (i = 0; i < numStairBoxes; i++) {
        const stairBox_t *b = &stairBoxes[i];
        if (p[0] >= b->mn[0] && p[0] <= b->mx[0] && p[1] >= b->mn[1] && p[1] <= b->mx[1] &&
            p[2] >= b->mn[2] && p[2] <= b->mx[2] && (!b->sloped || DotProduct(p, b->n) <= b->d)) c |= b->contents;
    }
    return c;
}
static void StairReset(float x, float y, float z) {
    Reset(x, y, z); move.trace = StairTrace; move.pointcontents = StairContents;
}
/* Step events of one think, as cgame's eye smoothing reads them: n[0] up,
 * n[1] EV_STEP_DOWN_4, n[2] EV_STEP_DOWN_8. */
static void StepTick(int dt, int n[3]) {
    int i, seq = ps.eventSequence;
    Tick(dt);
    CHECK(ps.eventSequence - seq <= MAX_PS_EVENTS);
    for (i = seq; i < ps.eventSequence; i++) {
        int ev = ps.events[i & (MAX_PS_EVENTS - 1)] & ~EV_EVENT_BITS;
        n[0] += ev >= EV_STEP_4 && ev <= EV_STEP_16;
        n[1] += ev == EV_STEP_DOWN_4; n[2] += ev == EV_STEP_DOWN_8;
    }
}
static void TestStairs(void) {
    const float risers[] = { 4, 6, 8, 16 };
    int r, k, t, lifts, cuts;
    for (r = 0; r < ARRAY_LEN(risers); r++) {
        numStairBoxes = 0;
        AddStairBox(-1000, -1000, -1000, 1000, 0, 0);
        for (k = 1; k <= 12; k++) AddStairBox(-1000, 16 * (k - 1), -1000, 1000, 16 * k, -risers[r] * k);
        AddStairBox(-1000, 16 * 12, -1000, 1000, 4000, -risers[r] * 12);
        Reset(0, -200, 24.125f); move.trace = StairTrace; move.pointcontents = StairContents;
        move.cmd.angles[YAW] = ANGLE2SHORT(90); move.cmd.forwardmove = 127;
        for (t = 0; t < 400 && ps.origin[1] < -64; t++) Tick(8);
        CHECK(ps.groundEntityNum != ENTITYNUM_NONE);
        lifts = cuts = 0;
        for (t = 0; t < 2000 && ps.origin[1] < 16 * 12 + 32; t++) {
            int wasAir = ps.groundEntityNum == ENTITYNUM_NONE; float before = FlatSpeed();
            Tick(8);
            if (!wasAir && ps.groundEntityNum == ENTITYNUM_NONE) lifts++;
            if (wasAir && ps.groundEntityNum != ENTITYNUM_NONE && FlatSpeed() < before * 0.75f) cuts++;
        }
        CHECK(ps.origin[1] >= 16 * 12 + 32);
        if (risers[r] <= 8) CHECK(lifts == 0 && cuts == 0);
        else CHECK(lifts > 0);
    }
    /* A landing's slowdown survives walking down a 6-unit curb. */
    numStairBoxes = 0;
    AddStairBox(-1000, -1000, -1000, 1000, 0, 0);
    AddStairBox(-1000, 0, -1000, 1000, 4000, -6);
    Reset(0, -250, 24.125f); move.trace = StairTrace; move.pointcontents = StairContents;
    move.cmd.angles[YAW] = ANGLE2SHORT(90); move.cmd.forwardmove = 127;
    for (t = 0; t < 20; t++) Tick(8);
    move.cmd.upmove = 127; Tick(8); move.cmd.upmove = 0;
    for (t = 0; t < 200 && ps.jumpLand <= 0; t++) Tick(8);
    CHECK(ps.jumpLand > 0 && ps.origin[1] < 0);
    for (t = 0; t < 200 && ps.origin[1] < 40; t++) { Tick(8); CHECK(ps.jumpLand > 0 || ps.origin[1] >= 40); }
    move.cmd.forwardmove = 0;
    /* The same stairs are walked up without leaving the ground. */
    numStairBoxes = 0;
    AddStairBox(-1000, -1000, -1000, 1000, 0, 0);
    for (k = 1; k <= 12; k++) AddStairBox(-1000, 16 * (k - 1), -1000, 1000, 16 * k, -16 * k);
    AddStairBox(-1000, 16 * 12, -1000, 1000, 4000, -16 * 12);
    Reset(0, 16 * 12 + 100, 24.125f - 16 * 12); move.trace = StairTrace; move.pointcontents = StairContents;
    move.cmd.angles[YAW] = ANGLE2SHORT(-90); move.cmd.forwardmove = 127;
    for (t = 0; t < 4; t++) Tick(8);
    CHECK(ps.groundEntityNum != ENTITYNUM_NONE);
    lifts = 0;
    for (t = 0; t < 2000 && ps.origin[1] > -32; t++) { Tick(8); if (ps.groundEntityNum == ENTITYNUM_NONE) lifts++; }
    CHECK(ps.origin[1] <= -32 && lifts == 0 && fabs(ps.origin[2] - 24.125f) < 0.5f);
    move.cmd.forwardmove = 0;
}
/* MW2's air step (Jump_GetStepHeight): in the air only a jump steps up, and
 * only to its own height above the take-off. A fall never climbs the ledge. */
static void AirStepV(int jumpLand, float originAbove, float ledgeAbove, float vz, int fmove) {
    numStairBoxes = 0;
    AddStairBox(-1000, -1000, -1000, 1000, 0, 0);
    AddStairBox(-1000, 0, -1000, 1000, 1000, 100);
    Reset(0, -15.5f, 24.125f); move.trace = StairTrace; move.pointcontents = StairContents;
    ps.origin[2] = 100 + 24 - ledgeAbove;
    ps.groundEntityNum = ENTITYNUM_NONE; VectorSet(ps.velocity, 0, 200, vz);
    ps.jumpLand = jumpLand; ps.jumpOriginZ = ps.origin[2] - originAbove;
    move.cmd.angles[YAW] = ANGLE2SHORT(90); move.cmd.forwardmove = fmove;
    Tick(8);
    move.cmd.forwardmove = 0;
}
static void AirStep(int jumpLand, float originAbove, float ledgeAbove) { AirStepV(jumpLand, originAbove, ledgeAbove, 0, 127); }
static void TestAirStep(void) {
    const float jumpHeight = BG_JUMP_HEIGHT;
    int t; float z;
    AirStep(-1, 4, 10);                  /* a jump reaches the lip: up onto it */
    CHECK(ps.origin[1] > -15 && ps.origin[2] > 124);
    AirStep(0, 4, 10);                   /* a fall into the same lip does not climb it */
    CHECK(ps.origin[1] < -15 && ps.origin[2] < 115);
    AirStep(-1, jumpHeight - 5, 10);     /* past the jump's height: 5 of 10, no step */
    CHECK(ps.origin[1] < -15 && ps.origin[2] < 115);
    AirStep(-1, jumpHeight - 15, 10);    /* inside it: 15 of 10 */
    CHECK(ps.origin[1] > -15 && ps.origin[2] > 124);
    /* Pressed along a wall off a drop, the step's push down finds nothing:
     * never STEPSIZE_DOWN more than the lift in one move. (That only the
     * lift is undone is TestStepRules' lip: here both slides are the same.) */
    numStairBoxes = 0;
    AddStairBox(-1000, -1000, -1000, 1000, 0, 0);
    AddStairBox(-1000, 0, -1000, 1000, 1000, -200);
    AddStairBox(16, -1000, -1000, 1000, 1000, 200);
    Reset(0.5f, -40, 24.125f); move.trace = StairTrace; move.pointcontents = StairContents;
    move.cmd.angles[YAW] = ANGLE2SHORT(80); move.cmd.forwardmove = 127;
    Tick(8); CHECK(ps.groundEntityNum != ENTITYNUM_NONE);
    for (t = 0; t < 400 && ps.groundEntityNum != ENTITYNUM_NONE; t++) {
        z = ps.origin[2]; Tick(8);
        CHECK(ps.origin[2] > z - 0.5f || ps.groundEntityNum == ENTITYNUM_NONE);
        if (ps.groundEntityNum == ENTITYNUM_NONE) CHECK(ps.origin[2] > z - 1);
    }
    CHECK(ps.groundEntityNum == ENTITYNUM_NONE && ps.origin[1] > 15);
    move.cmd.forwardmove = 0;
}
/* MW2's progress check (PM_StepSlideMove 0x4b1bbe): the step is kept only if
 * it got farther along the move than the unstepped slide, which is otherwise
 * pushed down instead. A lifted box meeting a header the player fits under
 * never stops the walk: along a wall under a lintel 8 above the head,
 * standing or crouched, or down a 30 degree ramp (a step on every move) into
 * an opening 2 to STEPSIZE above the head. */
static void TestStepProgress(void) {
    const int frames[] = { 8, 33, 66 };
    const float clear[] = { 2, STEPSIZE / 2, STEPSIZE }, ny = 0.5f, nz = 0.866f;
    int i, c, t;
    for (i = 0; i < ARRAY_LEN(frames); i++) {
        for (c = 0; c < 2; c++) {
            numStairBoxes = 0;
            AddStairBox(-1000, -1000, -1000, 2000, 1000, 0);
            AddStairBox(-1000, PLAYER_WIDTH + 1, -1000, 2000, 1000, 1000);
            AddStairBox(100, -1000, -MINS_Z + (c ? CROUCH_HEIGHT : DEFAULT_HEIGHT) + 8, 2000, 1000, 1000);
            StairReset(0, 0, 24.125f);
            move.cmd.angles[YAW] = ANGLE2SHORT(20); move.cmd.forwardmove = 127; move.cmd.upmove = c ? -127 : 0;
            for (t = 0; t < 3000 / frames[i]; t++) Tick(frames[i]);
            CHECK(ps.origin[0] > 100 + PLAYER_WIDTH && ps.groundEntityNum != ENTITYNUM_NONE);
        }
        move.cmd.upmove = 0;
        for (c = 0; c < ARRAY_LEN(clear); c++) {
            /* the head where the front face meets the opening at y = 100 */
            float head = -MINS_Z + DEFAULT_HEIGHT - ny / nz * (100 - 2 * PLAYER_WIDTH);
            numStairBoxes = 0;
            AddSlopeBox(-1000, -1000, -2000, 1000, 2000, 2000, ny, nz, 0);
            AddStairBox(-1000, 100, head + clear[c], 1000, 2000, 3000);
            StairReset(0, -100, -MINS_Z + ny / nz * (100 + PLAYER_WIDTH) + 1);
            move.cmd.angles[YAW] = ANGLE2SHORT(90); move.cmd.forwardmove = 127;
            for (t = 0; t < 2000 / frames[i]; t++) Tick(frames[i]);
            CHECK(ps.origin[1] > 200 && ps.groundEntityNum != ENTITYNUM_NONE);
        }
    }
    move.cmd.forwardmove = 0;
}
/* MW2's Jump_IsPlayerAboveMax: a jump's step never ends at its own height
 * above the take-off, so at no frame rate does a run-up jump end on a ledge
 * its arc does not reach. Jump_ClampVelocity: what the step gains comes off
 * the rise left, so a step onto ground rising away (a walkable slope) keeps
 * no more upward speed than takes it to BG_JUMP_HEIGHT. */
static void TestJumpStepCap(void) {
    const int frames[] = { 8, 16, 33, 66 };
    const float over[] = { 0.25f, 1, 2 }, feet = 26;
    int i, h, t; float d, room, peak, top;
    for (i = 0; i < ARRAY_LEN(frames); i++) {
        /* What the jump's own arc reaches at this frame rate (TestLanding
         * pins it); a ledge above that is reached only by a step. */
        numStairBoxes = 0;
        AddStairBox(-1000, -2000, -1000, 1000, 2000, 0);
        StairReset(0, 0, 24.125f); Tick(frames[i]);
        move.cmd.upmove = 127; Tick(frames[i]); move.cmd.upmove = 0;
        for (t = 0, peak = 0; t < 2000 / frames[i] && ps.groundEntityNum == ENTITYNUM_NONE; t++) {
            Tick(frames[i]); if (ps.origin[2] - ps.jumpOriginZ > peak) peak = ps.origin[2] - ps.jumpOriginZ;
        }
        CHECK(peak > BG_JUMP_HEIGHT - 1 && peak < BG_JUMP_HEIGHT + 3);
        if (peak < BG_JUMP_HEIGHT) peak = BG_JUMP_HEIGHT;
        /* full speed at the take-off, the jump a quarter unit later each run */
        for (h = 0; h < ARRAY_LEN(over); h++) {
            for (d = 0; d < 160; d += 0.25f) {
                top = 24.125f + MINS_Z + peak + over[h];
                numStairBoxes = 0;
                AddStairBox(-1000, -2000, -1000, 1000, 0, 0);
                AddStairBox(-1000, 0, -1000, 1000, 1000, top);
                StairReset(0, -d - PLAYER_WIDTH - 1, 24.125f); ps.velocity[1] = ps.speed;
                move.cmd.angles[YAW] = ANGLE2SHORT(90); move.cmd.forwardmove = 127;
                Tick(frames[i]); CHECK(ps.groundEntityNum != ENTITYNUM_NONE);
                move.cmd.upmove = 127; Tick(frames[i]); move.cmd.upmove = 0;
                CHECK(ps.jumpLand == -1);
                for (t = 0; t < 2000 / frames[i] && ps.groundEntityNum == ENTITYNUM_NONE; t++) Tick(frames[i]);
                CHECK(ps.groundEntityNum != ENTITYNUM_NONE && ps.origin[2] < top);
            }
        }
    }
    numStairBoxes = 0;
    AddStairBox(-1000, -1000, -1000, 1000, 0, 0);
    AddSlopeBox(-1000, 0, -1000, 1000, 1000, 2000, -0.6f, 0.8f, 30);
    StairReset(0, -15.5f, 24.125f + feet);
    ps.jumpLand = -1; ps.jumpOriginZ = 24.125f; ps.jumpWait = BG_JUMP_WAIT; ps.pm_flags |= PMF_JUMP_HELD;
    VectorSet(ps.velocity, 0, ps.speed, sqrt(BG_JumpVelocity(&ps) * BG_JumpVelocity(&ps) - 2 * ps.gravity * feet));
    move.cmd.angles[YAW] = ANGLE2SHORT(90); move.cmd.forwardmove = 0;
    Tick(8);
    room = ps.jumpOriginZ + BG_JUMP_HEIGHT - ps.origin[2];
    CHECK(ps.origin[1] > -15 && room > 0 && ps.velocity[2] > 0);
    CHECK(ps.velocity[2] <= sqrt(2 * ps.gravity * room) + 1);
}
/* MW2's step slowdown (PM_StepSlideMove 0x4b1d19): a walked step keeps
 * 0.2 + 0.8 * (1 - height / STEPSIZE) of the speed, up a stair or down one;
 * flat ground keeps all of it. */
static void TestStepSlowdown(void) {
    const int frames[] = { 8, 33 };
    const float riser = 8;
    int i, up, k, t; float z;
    for (i = 0; i < ARRAY_LEN(frames); i++) {
        for (up = 0; up < 2; up++) {
            float r = up ? riser : -riser;
            numStairBoxes = 0;
            AddStairBox(-1000, -1000, -1000, 1000, 0, 0);
            for (k = 1; k <= 6; k++) AddStairBox(-1000, 16 * (k - 1), -1000, 1000, 16 * k, r * k);
            AddStairBox(-1000, 96, -1000, 1000, 2000, r * 6);
            StairReset(0, -200, 24.125f);
            move.cmd.angles[YAW] = ANGLE2SHORT(90); move.cmd.forwardmove = 127;
            for (t = 0; t < 400 && ps.origin[1] < -100; t++) Tick(frames[i]);
            CHECK(ps.groundEntityNum != ENTITYNUM_NONE && fabs(FlatSpeed() - ps.speed) < 1);
            for (t = 0; t < 400; t++) {
                z = ps.origin[2]; Tick(frames[i]);
                if (fabs(ps.origin[2] - z) > 0.5f) break;
                CHECK(fabs(FlatSpeed() - ps.speed) < 1);
            }
            CHECK(fabs(ps.origin[2] - z - r) < 0.5f && ps.groundEntityNum != ENTITYNUM_NONE);
            CHECK(fabs(FlatSpeed() - ps.speed * (STEP_SPEED_KEEP + (1 - STEP_SPEED_KEEP) * (1 - riser / STEPSIZE))) < 1);
        }
    }
    move.cmd.forwardmove = 0;
}
/* MW2 has neither mode, so the step's push down must hold neither to the
 * ground: a water jump from a pool's floor leaves it as a jump does, and
 * flight takes off from the floor with up held. */
static void TestWaterJumpAndFlight(void) {
    const int frames[] = { 8, 16, 33, 50 };
    int i, t, jumped;
    for (i = 0; i < ARRAY_LEN(frames); i++) {
        numStairBoxes = 0;
        AddStairBox(-1000, -1000, -1000, 16, 1000, 0);
        AddStairBox(16, -1000, -1000, 1000, 1000, 36);
        AddWaterBox(-1000, -1000, -1000, 16, 1000, 40);
        StairReset(-100, 0, 24.125f); move.cmd.forwardmove = 127;
        for (t = jumped = 0; t < 3000 / frames[i]; t++) {
            Tick(frames[i]);
            if (ps.pm_flags & PMF_TIME_WATERJUMP) jumped++;
            else if (!jumped) CHECK(ps.groundEntityNum != ENTITYNUM_NONE && ps.origin[2] < 24.25f);
        }
        CHECK(jumped > 0);
        CHECK(ps.origin[0] > 16 + PLAYER_WIDTH && ps.origin[2] > 36 - MINS_Z && ps.groundEntityNum != ENTITYNUM_NONE);
        move.cmd.forwardmove = 0;
        numStairBoxes = 0;
        AddStairBox(-1000, -1000, -1000, 1000, 1000, 0);
        StairReset(0, 0, 24.125f); Tick(frames[i]);
        CHECK(ps.groundEntityNum != ENTITYNUM_NONE);
        ps.powerups[PW_FLIGHT] = 1; move.cmd.upmove = 127;
        Run(1000, frames[i]);
        CHECK(ps.origin[2] > 24.125f + STEPSIZE && ps.velocity[2] > 0 && ps.groundEntityNum == ENTITYNUM_NONE);
        move.cmd.upmove = 0;
    }
}
/* MW2's PM_ProjectVelocity on the step's landing: a jump that steps onto a
 * flat ledge while still rising lays the rise along the ground, keeping the
 * speed's length; a falling one keeps its ground speed. */
static void TestStepProjection(void) {
    const float vz = 120;
    float rise;
    AirStepV(-1, 4, 10, vz, 0);
    rise = vz - ps.gravity * 8 * 0.001f;    /* what the 8ms think's gravity leaves */
    CHECK(ps.origin[1] > -15 && ps.origin[2] > 124 && ps.velocity[2] == 0);
    CHECK(fabs(FlatSpeed() - sqrt(200 * 200 + rise * rise)) < 1);
    AirStepV(-1, 4, 10, -vz, 0);
    CHECK(ps.origin[1] > -15 && ps.origin[2] > 124 && fabs(ps.velocity[2]) < 1 && fabs(FlatSpeed() - 200) < 1);
}
/* MW2's PM_GroundTrace: another player is never ground. Dropped onto one's
 * head the player rests there in the air (no landing), and a jump press
 * there does not jump. */
static void TestPlayerNotGround(void) {
    const int frames[] = { 8, 16, 33 };
    int i, t;
    for (i = 0; i < ARRAY_LEN(frames); i++) {
        numStairBoxes = 0;
        AddStairBox(-1000, -1000, -1000, 1000, 1000, 0);
        AddPlayerBox(1, -PLAYER_WIDTH, -PLAYER_WIDTH, 0, PLAYER_WIDTH, PLAYER_WIDTH, DEFAULT_HEIGHT - MINS_Z);
        StairReset(0, 0, 200);
        for (t = 0; t < 2000 / frames[i]; t++) { Tick(frames[i]); CHECK(ps.groundEntityNum == ENTITYNUM_NONE); }
        CHECK(fabs(ps.origin[2] - (DEFAULT_HEIGHT - 2 * MINS_Z)) < 1);
        move.cmd.upmove = 127;
        for (t = 0; t < 500 / frames[i]; t++) {
            Tick(frames[i]);
            CHECK(ps.groundEntityNum == ENTITYNUM_NONE && ps.jumpLand != -1 && ps.velocity[2] <= 0);
        }
        move.cmd.upmove = 0;
    }
}
/* MW2's Jump_PushOffLadder: a jump off a ladder keeps BG_LADDER_JUMP_SCALE of
 * a jump's take-off speed, so it rises that squared of BG_JUMP_HEIGHT. */
static void TestLadderJumpHeight(int dt) {
    const float rise = BG_JUMP_HEIGHT * BG_LADDER_JUMP_SCALE * BG_LADDER_JUMP_SCALE;
    float v, top; int t;
    Reset(111, 0, 180);
    v = BG_JumpVelocity(&ps) * BG_LADDER_JUMP_SCALE;
    Tick(dt); CHECK(Attached());
    Tick(dt);
    move.cmd.upmove = 127; Tick(dt); move.cmd.upmove = 0;
    CHECK(!Attached() && ps.jumpLand == -1);
    CHECK(ps.velocity[2] <= v + 0.5f && ps.velocity[2] >= v - ps.gravity * dt * 0.001f - 1);
    for (t = 0, top = ps.origin[2]; t < 2000 / dt && ps.velocity[2] > 0; t++) { Tick(dt); if (ps.origin[2] > top) top = ps.origin[2]; }
    CHECK(fabs(top - ps.jumpOriginZ - rise) < 3);  /* the 8ms rounding adds what TestLanding allows */
}
/* MW2's Jump_Start ends any running timer (pm_time = 0): a hop right after a
 * landing fast enough to start Q3's landing timer has no timer left, so a
 * ceiling clips it at once instead of holding it pressed under it. */
static void TestJumpEndsTimers(void) {
    const int frames[] = { 8, 16 };
    const float ceiling = -MINS_Z + DEFAULT_HEIGHT + 12;
    int i, t, pressed;
    for (i = 0; i < ARRAY_LEN(frames); i++) {
        numStairBoxes = 0;
        AddStairBox(-1000, -1000, -1000, 1000, 1000, 0);
        StairReset(0, 0, 24.125f + BG_JUMP_HEIGHT);
        for (t = 0; t < 200 && ps.groundEntityNum == ENTITYNUM_NONE; t++) Tick(frames[i]);
        CHECK(ps.groundEntityNum != ENTITYNUM_NONE && (ps.pm_flags & PMF_TIME_LAND) && ps.pm_time > 0 && ps.jumpWait == 0);
        AddStairBox(-1000, -1000, ceiling, 1000, 1000, 1000);
        move.cmd.upmove = 127; Tick(frames[i]); move.cmd.upmove = 0;
        CHECK(ps.groundEntityNum == ENTITYNUM_NONE && ps.jumpLand == -1);
        CHECK(!(ps.pm_flags & PMF_ALL_TIMES) && ps.pm_time == 0);
        for (t = pressed = 0; t < 2000 / frames[i] && ps.groundEntityNum == ENTITYNUM_NONE; t++) {
            Tick(frames[i]);
            pressed += ps.origin[2] + DEFAULT_HEIGHT > ceiling - 0.5f && ps.velocity[2] > 0;
        }
        CHECK(pressed == 0 && ps.groundEntityNum != ENTITYNUM_NONE);
    }
}
/* MW2's Jump_Check: no jump from a stance that cannot stand (crouched under a
 * ceiling too low to stand under). The press is spent: walking out from
 * under it still holding jump never jumps; a fresh press does. */
static void TestCrouchedJump(void) {
    int t;
    numStairBoxes = 0;
    AddStairBox(-1000, -1000, -1000, 1000, 1000, 0);
    AddStairBox(-1000, -1000, -MINS_Z + CROUCH_HEIGHT + 8, 100, 1000, 1000);
    StairReset(0, 0, 24.125f); move.cmd.upmove = -127;
    for (t = 0; t < 60; t++) Tick(8);
    CHECK((ps.pm_flags & PMF_DUCKED) && ps.groundEntityNum != ENTITYNUM_NONE);
    move.cmd.upmove = 127; Tick(8);
    CHECK((ps.pm_flags & PMF_DUCKED) && (ps.pm_flags & PMF_JUMP_HELD));
    CHECK(ps.groundEntityNum != ENTITYNUM_NONE && ps.jumpLand != -1 && ps.velocity[2] <= 0);
    move.cmd.forwardmove = 127;
    for (t = 0; t < 600 && ps.origin[0] < 100 + PLAYER_WIDTH + 32; t++) { Tick(8); CHECK(ps.groundEntityNum != ENTITYNUM_NONE); }
    CHECK(!(ps.pm_flags & PMF_DUCKED) && ps.origin[0] >= 100 + PLAYER_WIDTH + 32);
    move.cmd.upmove = 0; Tick(8); move.cmd.upmove = 127; Tick(8); move.cmd.upmove = move.cmd.forwardmove = 0;
    CHECK(ps.groundEntityNum == ENTITYNUM_NONE && ps.jumpLand == -1 && ps.velocity[2] > 0);
}
static void WalkY(float yaw) { move.cmd.angles[YAW] = ANGLE2SHORT(yaw); move.cmd.forwardmove = 127; }
/* MW2's step rules (PM_StepSlideMove 0x4b1810) one at a time, each against
 * the mutation it rules out. */
static void TestStepRules(void) {
    const int frames[] = { 8, 33, 66 };
    int t, i, r, n[3];
    /* A push-down that finds nothing undoes only the lift (0x4b1ba1), never
     * the whole stepped move: met fast, a thin lip on a cliff's edge is
     * cleared. */
    numStairBoxes = 0;
    AddStairBox(-1000, -1000, -1000, 1000, 0, 0);
    AddStairBox(-1000, 0, -1000, 1000, 4, 10);
    AddStairBox(-1000, 4, -1000, 1000, 1000, -500);
    StairReset(0, -15.875f, 24.125f); WalkY(90); move.cmd.forwardmove = 0;
    Tick(8); CHECK(ps.groundEntityNum != ENTITYNUM_NONE);
    move.cmd.forwardmove = 127; VectorSet(ps.velocity, 0, 2000, 0); Tick(50);
    CHECK(ps.origin[1] > 4 + PLAYER_WIDTH && fabs(ps.origin[2] - 24.125f) < 0.01f);
    /* Never onto a player: walking off a curb above one does not step down
     * onto it, and one low enough to step onto blocks instead. */
    numStairBoxes = 0;
    AddStairBox(-1000, -1000, -1000, 1000, 0, 0);
    AddPlayerBox(3, -1000, 0, -1000, 1000, 1000, -8);
    StairReset(0, -20, 24.125f); WalkY(90);
    for (t = 0; t < 100 && ps.origin[1] < PLAYER_WIDTH; t++) Tick(8);
    CHECK(ps.origin[1] >= PLAYER_WIDTH && ps.groundEntityNum != 3 && ps.origin[2] > 24);
    numStairBoxes = 0;
    AddStairBox(-1000, -1000, -1000, 1000, 1000, 0);
    AddPlayerBox(3, -1000, 0, -1000, 1000, 30, 10);
    StairReset(0, -20, 24.125f); WalkY(90);
    for (t = 0; t < 30; t++) Tick(8);
    CHECK(ps.origin[1] < -PLAYER_WIDTH + 1 && ps.origin[2] < 25);
    /* A ground step lands on ground as steep as STEP_LAND_NORMAL: off a curb
     * onto a 0.5 slope, and up onto a block whose top is one. */
    numStairBoxes = 0;
    AddStairBox(-1000, -1000, -1000, 1000, 0, 0);
    AddSlopeBox(-1000, 0, -1000, 1000, 1000, -2, 0.866f, 0.5f, -2);
    StairReset(0, -20, 24.125f); WalkY(90);
    for (t = 0; t < 100 && ps.origin[1] < PLAYER_WIDTH; t++) Tick(8);
    CHECK(ps.origin[1] >= PLAYER_WIDTH && ps.origin[2] < 24);
    numStairBoxes = 0;
    AddStairBox(-1000, -1000, -1000, 1000, 1000, 0);
    AddSlopeBox(-1000, 0, -1000, 1000, 1000, 40, -0.866f, 0.5f, 10);
    StairReset(0, -20, 24.125f); WalkY(90);
    for (t = 0; t < 20; t++) Tick(8);
    CHECK(ps.origin[1] > -PLAYER_WIDTH && ps.origin[2] > 34);
    /* A jump's step lands only on walkable ground: not onto that slope. */
    numStairBoxes = 0;
    AddStairBox(-1000, -1000, -1000, 1000, 0, 0);
    AddSlopeBox(-1000, 0, -1000, 1000, 1000, 140, -0.866f, 0.5f, 100);
    StairReset(0, -15.5f, 124 - 10); WalkY(90);
    ps.groundEntityNum = ENTITYNUM_NONE; VectorSet(ps.velocity, 0, 200, 0);
    ps.jumpLand = -1; ps.jumpOriginZ = ps.origin[2] - 4;
    Tick(8);
    CHECK(ps.origin[1] < -PLAYER_WIDTH && ps.origin[2] < 115);
    /* Step events measure what the step added to the unstepped slide
     * (0x4b1d4a), so a ramp is never a stair, at any frame rate. */
    for (i = 0; i < ARRAY_LEN(frames); i++) {
        numStairBoxes = 0;
        AddSlopeBox(-1000, -1000, -2000, 1000, 1000, 2000, -0.75f, 1, 0);
        StairReset(0, 0, 24.125f + 11.25f + 0.5f); WalkY(90); move.cmd.forwardmove = 0;
        for (t = 0; t < 10 && ps.groundEntityNum == ENTITYNUM_NONE; t++) Tick(8);
        CHECK(ps.groundEntityNum != ENTITYNUM_NONE);
        move.cmd.forwardmove = 127; n[0] = n[1] = n[2] = 0;
        /* ...and costs no speed: only a walked step does (|dz| over 0.5). */
        for (t = 0; t < 1000 / frames[i]; t++) {
            StepTick(frames[i], n);
            if (t * frames[i] >= 500) CHECK(fabs(VectorLength(ps.velocity) - ps.speed) < 1);
        }
        move.cmd.angles[YAW] = ANGLE2SHORT(-90);
        for (t = 0; t < 1000 / frames[i]; t++) {
            StepTick(frames[i], n);
            if (t * frames[i] >= 500) CHECK(fabs(VectorLength(ps.velocity) - ps.speed) < 1);
        }
        CHECK(ps.groundEntityNum != ENTITYNUM_NONE && ps.origin[1] < 100 && !n[0] && !n[1] && !n[2]);
    }
    /* Walking down a 4- or 8-unit stair smooths the eye down each riser with
     * its own event, EV_STEP_DOWN_4 or EV_STEP_DOWN_8. */
    for (r = 4; r <= 8; r += 4) {
        numStairBoxes = 0;
        AddStairBox(-1000, -1000, -1000, 1000, 0, 0);
        for (t = 1; t <= 6; t++) AddStairBox(-1000, 16 * (t - 1), -1000, 1000, 16 * t, -r * t);
        AddStairBox(-1000, 96, -1000, 1000, 2000, -r * 6);
        StairReset(0, -60, 24.125f); WalkY(90);
        n[0] = n[1] = n[2] = 0;
        for (t = 0; t < 300 && ps.origin[1] < 130; t++) StepTick(8, n);
        CHECK(!n[0] && n[r == 4 ? 1 : 2] == 6 && n[r == 4 ? 2 : 1] == 0 && ps.groundEntityNum != ENTITYNUM_NONE);
    }
    move.cmd.forwardmove = 0;
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
        TestLadderJumpHeight(steps[i]);
        TestBottomExit(steps[i]);
        TestPredictionReplay(steps[i]);
    }
    TestWeaponOrdering();
    TestViewAndModes();
    TestLadderEndsSprint();
    TestJumpSprint();
    TestLanding();
    TestStairs();
    TestAirStep();
    TestStepProgress();
    TestJumpStepCap();
    TestStepSlowdown();
    TestWaterJumpAndFlight();
    TestStepProjection();
    TestPlayerNotGround();
    TestJumpEndsTimers();
    TestCrouchedJump();
    TestStepRules();
    for (i = 0; i < numAllocations; i++) free(allocations[i]);
    printf("PASS: ladder BSP collision, momentum, traversal, detach, yaw, weapon ordering, prediction replay, steps and jumps (%d checks).\n", checks);
    return 0;
}
