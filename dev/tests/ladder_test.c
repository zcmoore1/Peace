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
    float air, v; int t, landed;
    Reset(-300, -240, 24.125f); move.cmd.angles[YAW] = ANGLE2SHORT(90);
    move.cmd.forwardmove = 127;
    for (t = 0; t < 40; t++) Tick(8);
    CHECK(ps.groundEntityNum != ENTITYNUM_NONE && fabs(FlatSpeed() - ps.speed) < 2);
    move.cmd.upmove = 127; Tick(8); move.cmd.upmove = 0;
    CHECK(ps.groundEntityNum == ENTITYNUM_NONE && ps.jumpLand == -1 &&
          ps.jumpWait > 0 && ps.velocity[2] > 0);
    for (t = 0; t < 200 && ps.groundEntityNum == ENTITYNUM_NONE; t++) { air = FlatSpeed(); Tick(8); }
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
          ps.velocity[2] < JUMP_VELOCITY / sqrt(BG_JumpLandFactor(BG_JUMP_LAND_TIME - 8)) + 1 &&
          ps.velocity[2] > JUMP_VELOCITY / sqrt(2.5f) - 10);
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
typedef struct { vec3_t mn, mx; } stairBox_t;
static stairBox_t stairBoxes[24]; static int numStairBoxes;
static void AddStairBox(float x0, float y0, float z0, float x1, float y1, float z1) {
    VectorSet(stairBoxes[numStairBoxes].mn, x0, y0, z0);
    VectorSet(stairBoxes[numStairBoxes].mx, x1, y1, z1); numStairBoxes++;
}
static void StairTrace(trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs,
    const vec3_t end, int pass, int mask) {
    int i; trace_t t; vec3_t lo, hi; (void)pass;
    memset(tr, 0, sizeof(*tr)); tr->fraction = 1; VectorCopy(end, tr->endpos);
    VectorCopy(mins, lo); VectorCopy(maxs, hi);
    for (i = 0; i < numStairBoxes; i++) {
        clipHandle_t h = CM_TempBoxModel(stairBoxes[i].mn, stairBoxes[i].mx, qfalse);
        CM_BoxTrace(&t, start, end, lo, hi, h, mask, qfalse);
        if (t.allsolid) tr->allsolid = qtrue;
        if (t.startsolid) tr->startsolid = qtrue;
        if (t.fraction < tr->fraction) {
            qboolean a = tr->allsolid, st = tr->startsolid; *tr = t; tr->allsolid = a; tr->startsolid = st;
        }
    }
    tr->entityNum = (tr->fraction < 1 || tr->startsolid) ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
    if (tr->fraction < 1) tr->contents = CONTENTS_SOLID;
}
static int StairContents(const vec3_t p, int pass) { (void)p; (void)pass; return 0; }
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
static void AirStep(int jumpLand, float originAbove, float ledgeAbove) {
    numStairBoxes = 0;
    AddStairBox(-1000, -1000, -1000, 1000, 0, 0);
    AddStairBox(-1000, 0, -1000, 1000, 1000, 100);
    Reset(0, -15.5f, 24.125f); move.trace = StairTrace; move.pointcontents = StairContents;
    ps.origin[2] = 100 + 24 - ledgeAbove;
    ps.groundEntityNum = ENTITYNUM_NONE; VectorSet(ps.velocity, 0, 200, 0);
    ps.jumpLand = jumpLand; ps.jumpOriginZ = ps.origin[2] - originAbove;
    move.cmd.angles[YAW] = ANGLE2SHORT(90); move.cmd.forwardmove = 127;
    Tick(8);
    move.cmd.forwardmove = 0;
}
static void TestAirStep(void) {
    const float jumpHeight = (float)(JUMP_VELOCITY * JUMP_VELOCITY) / (2 * DEFAULT_GRAVITY);
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
     * only the lift is undone, never STEPSIZE_DOWN more in one move. */
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
int main(void) {
    int checksum, i;
    const int steps[] = {8, 16, 33, 66};
    CM_LoadMap("assets/baseq3/maps/peace_ladder.bsp", qfalse, &checksum);
    CHECK(CM_NumInlineModels() == 1 && CM_NumClusters() == 1);
    for (i = 0; i < ARRAY_LEN(steps); i++) {
        TestGrabAndHang(steps[i]);
        TestTraverseAndExit(steps[i]);
        TestJump(steps[i]);
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
    for (i = 0; i < numAllocations; i++) free(allocations[i]);
    printf("PASS: ladder BSP collision, momentum, traversal, detach, yaw, weapon ordering and prediction replay (%d checks).\n", checks);
    return 0;
}
