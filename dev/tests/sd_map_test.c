/* Check the generated Search & Destroy arena against the real engine collision
 * loader and the real player movement. If this passes, the map is not just
 * well-formed: every spawn is clear and lands on a floor, every bomb site can
 * be stood on, and the stairs really walk up to the perch.
 *
 * Only filesystem/allocator/cvar glue is mocked, the same way ladder_test.c
 * does it. Nothing here re-derives a coordinate from the map builder: every
 * position comes out of the BSP's own entity lump. */
#include "../../code/game/bg_pmove.c"
#include "../../code/qcommon/cm_local.h"

static playerState_t ps;
static pmove_t move;
static int checks;

#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s (pos %.1f %.1f %.1f)\n", __FILE__, __LINE__, #x, \
    ps.origin[0], ps.origin[1], ps.origin[2]); exit(1); } } while (0)

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
    (void)label; (void)file; (void)line; (void)preference;
    return calloc(1, size);
}
#else
void *Hunk_Alloc(int size, ha_pref preference) { (void)preference; return calloc(1, size); }
#endif
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

/* --- entities, straight out of the BSP ------------------------------------ */
typedef struct { char classname[64]; char targetname[64]; vec3_t origin; float radius; char site[8]; } ent_t;
static ent_t ents[128];
static int numEnts;

static void ParseEntities(void) {
    char *p = CM_EntityString();
    char *tok;
    char key[MAX_TOKEN_CHARS];
    ent_t *e;

    for (;;) {
        tok = COM_Parse(&p);
        if (!tok[0]) break;
        CHECK(tok[0] == '{' && numEnts < ARRAY_LEN(ents));
        e = &ents[numEnts++];
        memset(e, 0, sizeof(*e));
        for (;;) {
            tok = COM_Parse(&p);
            if (tok[0] == '}') break;
            Q_strncpyz(key, tok, sizeof(key));
            tok = COM_Parse(&p);
            if (!Q_stricmp(key, "classname")) Q_strncpyz(e->classname, tok, sizeof(e->classname));
            else if (!Q_stricmp(key, "origin")) sscanf(tok, "%f %f %f", &e->origin[0], &e->origin[1], &e->origin[2]);
            else if (!Q_stricmp(key, "radius")) e->radius = atof(tok);
            else if (!Q_stricmp(key, "site")) Q_strncpyz(e->site, tok, sizeof(e->site));
            else if (!Q_stricmp(key, "targetname")) Q_strncpyz(e->targetname, tok, sizeof(e->targetname));
        }
    }
}

static ent_t *Named(const char *targetname) {
    int i;
    for (i = 0; i < numEnts; i++) if (!Q_stricmp(ents[i].targetname, targetname)) return &ents[i];
    CHECK(!"missing test marker entity");
    return NULL;
}

static int Count(const char *classname) {
    int i, n = 0;
    for (i = 0; i < numEnts; i++) if (!Q_stricmp(ents[i].classname, classname)) n++;
    return n;
}

/* --- real collision + real movement ---------------------------------------- */
static const vec3_t playerMins = { -15, -15, -24 };
static const vec3_t playerMaxs = { 15, 15, 32 };

static void Trace(trace_t *tr, const vec3_t start, const vec3_t mins,
                  const vec3_t maxs, const vec3_t end, int pass, int mask) {
    vec3_t lo, hi;
    (void)pass;
    VectorCopy(mins, lo); VectorCopy(maxs, hi);
    CM_BoxTrace(tr, start, end, lo, hi, 0, mask, qfalse);
    tr->entityNum = tr->fraction < 1 ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
}
static int Contents(const vec3_t point, int pass) { (void)pass; return CM_PointContents(point, 0); }

static qboolean BoxClear(const vec3_t at) {
    trace_t tr;
    Trace(&tr, at, playerMins, playerMaxs, at, 0, MASK_PLAYERSOLID);
    return !tr.startsolid && !tr.allsolid;
}

static void Reset(const vec3_t at) {
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
    ps.stats[STAT_WEAPONS] = 1 << WP_SHOTGUN;
    ps.stats[STAT_SLOT_PRIMARY] = WP_SHOTGUN;
    VectorCopy(at, ps.origin);
    pm = &move;
}
static void Run(int ms) {
    while (ms > 0) {
        move.cmd.serverTime = ps.commandTime + 8;
        Pmove(&move);
        ms -= 8;
    }
}

/* Drop a player and wait until it has been on the ground for 100ms straight.
 *
 * Not "run 600ms and look": stock id PM_WalkMove keeps |velocity| when it clips
 * against the ground ("don't decrease velocity when going up or down a slope").
 * A purely vertical landing that the 0.25-unit ground probe catches before the
 * slide move touches the floor clips to a tiny upward residue, which that code
 * then renormalises to full speed - a small hop. Any horizontal speed hides it,
 * and it dies out in a bounce or two. It is real engine behaviour and this test
 * must not paper over a map that never settles, so it waits, with a cap. */
static qboolean Settle(void) {
    int t, grounded = 0;
    for (t = 0; t < 3000; t += 8) {
        move.cmd.serverTime = ps.commandTime + 8;
        Pmove(&move);
        grounded = ps.groundEntityNum != ENTITYNUM_NONE ? grounded + 8 : 0;
        if (grounded >= 100) return qtrue;
    }
    return qfalse;
}

/* Every spawn: the player box fits, and a dropped player comes to rest on a
 * floor close to where it started instead of falling or getting stuck. */
static void TestSpawnsLand(void) {
    int i;
    for (i = 0; i < numEnts; i++) {
        const char *c = ents[i].classname;
        vec3_t start;
        if (Q_stricmpn(c, "team_CTF_", 9) && Q_stricmp(c, "info_player_deathmatch")) continue;
        /* the engine lifts a spawn 9 units before placing the player */
        VectorCopy(ents[i].origin, start);
        start[2] += 9;
        CHECK(BoxClear(start));
        Reset(start);
        CHECK(Settle());
        CHECK(fabs(ps.origin[0] - ents[i].origin[0]) < 1 && fabs(ps.origin[1] - ents[i].origin[1]) < 1);
        CHECK(fabs(ps.origin[2] - ents[i].origin[2]) < 2);
    }
}

/* Attackers at one end, defenders at the other, both sites on the defenders'
 * half - and every site can be stood in, with its whole floor pad in reach. */
static void TestSidesAndSites(void) {
    int i, sites = 0;
    float attX = 0, defX = 0;

    CHECK(Count("team_CTF_redspawn") >= 4 && Count("team_CTF_bluespawn") >= 4);
    CHECK(Count("team_CTF_redspawn") == Count("team_CTF_bluespawn"));
    CHECK(Count("team_CTF_redplayer") == Count("team_CTF_redspawn"));
    CHECK(Count("team_CTF_blueplayer") == Count("team_CTF_bluespawn"));
    CHECK(Count("info_player_deathmatch") >= 2);       /* also loads as FFA */
    CHECK(Count("info_player_intermission") == 1);

    for (i = 0; i < numEnts; i++) {
        if (!Q_stricmp(ents[i].classname, "team_CTF_redspawn")) attX += ents[i].origin[0];
        if (!Q_stricmp(ents[i].classname, "team_CTF_bluespawn")) defX += ents[i].origin[0];
    }
    attX /= Count("team_CTF_redspawn");
    defX /= Count("team_CTF_bluespawn");
    CHECK(attX < -1000 && defX > 1000);

    for (i = 0; i < numEnts; i++) {
        ent_t *e = &ents[i];
        vec3_t start;
        if (Q_stricmp(e->classname, "trigger_bombsite")) continue;
        sites++;
        CHECK(e->site[0] == 'A' || e->site[0] == 'B');
        CHECK(e->radius >= 64);
        /* on the defenders' side, and a long way from the attackers */
        CHECK(fabs(e->origin[0] - defX) < fabs(e->origin[0] - attX));
        /* a player can stand on it and is still in reach once settled */
        VectorCopy(e->origin, start);
        start[2] += 9;
        CHECK(BoxClear(start));
        Reset(start);
        CHECK(Settle());
        {
            vec3_t d;
            VectorSubtract(ps.origin, e->origin, d);
            CHECK(VectorLength(d) <= e->radius);
        }
    }
    CHECK(sites == 2);
}

/* The perch. A walker holding forward from the foot of the stairs must end up
 * on the 128-high ledge - proves the risers are under the step height. */
static void TestStairsClimb(void) {
    ent_t *foot = Named("test_stair_foot");
    ent_t *perch = Named("test_perch");
    vec3_t start;
    VectorCopy(foot->origin, start);
    start[2] += 9;
    CHECK(BoxClear(start));
    Reset(start);
    CHECK(Settle());
    move.cmd.angles[YAW] = ANGLE2SHORT(0);      /* facing +X, up the stairs */
    move.cmd.forwardmove = 127;
    {
        int t;
        for (t = 0; t < 4000 && ps.origin[0] < perch->origin[0]; t += 8) Run(8);
        CHECK(ps.origin[0] >= perch->origin[0]);     /* got there at all */
    }
    move.cmd.forwardmove = 0;
    CHECK(Settle());
    CHECK(ps.origin[2] > perch->origin[2] - 2);      /* on the ledge, not under it */
}

/* Nothing walkable leaks out of the hall: a player run hard at each outer wall
 * stays inside it. */
static void TestSealed(void) {
    static const float yaws[] = { 0, 90, 180, 270 };
    int i;
    for (i = 0; i < 4; i++) {
        vec3_t start = { -900, 0, 24 + 9 };
        Reset(start);
        move.cmd.angles[YAW] = ANGLE2SHORT(yaws[i]);
        move.cmd.forwardmove = 127;
        Run(8000);
        CHECK(fabs(ps.origin[0]) < 1536 && fabs(ps.origin[1]) < 768 && ps.origin[2] > 0);
    }
}

int main(void) {
    int checksum;
    CM_LoadMap("assets/baseq3/maps/peace_sd.bsp", qfalse, &checksum);
    ParseEntities();
    CHECK(numEnts > 1);
    TestSidesAndSites();
    TestSpawnsLand();
    TestStairsClimb();
    TestSealed();
    printf("PASS: peace_sd entities, sides, bomb sites, spawn landing, stairs and seal against real collision (%d checks).\n", checks);
    return 0;
}
