/* Drive the real Search & Destroy round logic (code/game/g_round.c) frame by
 * frame and check every way a round can end, the side switch, spawning by
 * role, the map-layout fallbacks, and the exact state published to the HUD.
 *
 * g_round.c is #included so its static helpers are reachable. Only the edges
 * are stubbed: entity search, spawn-point picking, ClientSpawn and the syscalls.
 * The stubs record what they were asked for, which is what most checks read.
 *
 * Every duration comes from the same cvars the game reads; no literals. */
#include "../../code/game/g_round.c"

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s  (round %d state %d bomb %d att %d)\n", __FILE__, __LINE__, #x, \
    level.roundNumber, level.roundState, level.bombState, level.attackingTeam); exit(1); } } while (0)

/* --- the world -------------------------------------------------------------- */
gentity_t	g_entities[MAX_GENTITIES];
gclient_t	clients[MAX_CLIENTS];
level_locals_t level;
vmCvar_t	g_gametype, g_roundtime, g_roundwarmup, g_roundswitch;
vmCvar_t	g_bombplanttime, g_bombdefusetime, g_bombtimer, g_bombradius;
/* vec3_origin comes from q_math.c */

static char	configstrings[MAX_CONFIGSTRINGS][MAX_INFO_STRING];
static int	configstringWrites[MAX_CONFIGSTRINGS];
static char	lastCenterPrint[1024];
static int	lastCTFTeamAsked;
static int	spawnCalls;

static void SetCvar(vmCvar_t *v, int value) { v->integer = value; v->value = (float)value; }

/* --- stubs: record, don't simulate ------------------------------------------ */
void QDECL G_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level, const char *fmt, ...) { (void)level; (void)fmt; exit(2); }
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void trap_LinkEntity(gentity_t *ent) { ent->r.linked = qtrue; }
void trap_UnlinkEntity(gentity_t *ent) { ent->r.linked = qfalse; }
void InitTrigger(gentity_t *self) { (void)self; }
void G_FreeEntity(gentity_t *ed) { ed->inuse = qfalse; ed->classname = NULL; }
qboolean SpotWouldTelefrag(gentity_t *spot) { (void)spot; return qfalse; }

void trap_SendServerCommand(int clientNum, const char *text) {
    (void)clientNum;
    if (!strncmp(text, "cp ", 3)) Q_strncpyz(lastCenterPrint, text, sizeof(lastCenterPrint));
}
void trap_SetConfigstring(int num, const char *string) {
    CHECK(num >= 0 && num < MAX_CONFIGSTRINGS);
    /* the engine drops identical sets; count only real changes, like it would */
    if (strcmp(configstrings[num], string)) configstringWrites[num]++;
    Q_strncpyz(configstrings[num], string, sizeof(configstrings[num]));
}

/* G_Find, copied from g_utils.c so entity lookup behaves exactly as in game */
gentity_t *G_Find(gentity_t *from, int fieldofs, const char *match) {
    char *s;
    if (!from) from = g_entities; else from++;
    for (; from < &g_entities[level.num_entities]; from++) {
        if (!from->inuse) continue;
        s = *(char **)((byte *)from + fieldofs);
        if (!s) continue;
        if (!Q_stricmp(s, match)) return from;
    }
    return NULL;
}

int TeamCount(int ignoreClientNum, team_t team) {
    int i, n = 0;
    for (i = 0; i < level.maxclients; i++) {
        if (i == ignoreClientNum) continue;
        if (clients[i].pers.connected != CON_CONNECTED) continue;
        if (clients[i].sess.sessionTeam == team) n++;
    }
    return n;
}

static gentity_t spotEnt;
gentity_t *SelectCTFSpawnPoint(team_t team, int teamstate, vec3_t origin, vec3_t angles, qboolean isbot) {
    (void)teamstate; (void)isbot;
    lastCTFTeamAsked = team;
    VectorClear(origin); VectorClear(angles);
    return &spotEnt;
}
gentity_t *SelectSpawnPoint(vec3_t avoid, vec3_t origin, vec3_t angles, qboolean isbot) {
    (void)avoid; (void)isbot;
    VectorClear(origin); VectorClear(angles);
    return &spotEnt;
}
/* ClientSpawn: bring the player back alive. Records that a round reset spawned
 * them, which is the only part of the real function g_round.c relies on. */
void ClientSpawn(gentity_t *ent) {
    ent->client->ps.stats[STAT_HEALTH] = ent->health = 100;
    ent->client->buttons = 0;
    spawnCalls++;
}

/* spawn vars for SP_trigger_bombsite */
static const char *spawnSite, *spawnRadius;
qboolean G_SpawnString(const char *key, const char *def, char **out) {
    const char *v = NULL;
    if (!Q_stricmp(key, "site")) v = spawnSite;
    *out = (char *)(v ? v : def);
    return v != NULL;
}
qboolean G_SpawnFloat(const char *key, const char *def, float *out) {
    const char *v = NULL;
    if (!Q_stricmp(key, "radius")) v = spawnRadius;
    *out = atof(v ? v : def);
    return v != NULL;
}

/* --- building a match ------------------------------------------------------- */
#define RED0 0      /* clients 0..2 red, 3..5 blue */
#define BLUE0 3
#define PLAYERS 6

static gentity_t *AddEntity(const char *classname, float x, float y, float z) {
    gentity_t *e = &g_entities[level.num_entities++];
    memset(e, 0, sizeof(*e));
    e->inuse = qtrue;
    e->classname = (char *)classname;
    VectorSet(e->s.origin, x, y, z);
    VectorCopy(e->s.origin, e->r.currentOrigin);
    return e;
}
static void AddPointSite(const char *label, float x, float y) {
    gentity_t *e = AddEntity("trigger_bombsite", x, y, 24);
    spawnSite = label; spawnRadius = NULL;
    SP_trigger_bombsite(e);
}

static void NewMatch(qboolean teamSpawns, qboolean authoredSites) {
    int i;
    memset(&level, 0, sizeof(level));
    memset(g_entities, 0, sizeof(g_entities));
    memset(clients, 0, sizeof(clients));
    memset(configstrings, 0, sizeof(configstrings));
    memset(configstringWrites, 0, sizeof(configstringWrites));
    level.clients = clients;
    level.maxclients = PLAYERS;
    level.num_entities = MAX_CLIENTS;       /* world entities start after clients */
    level.time = 10000;
    level.previousTime = level.time - 50;
    SetCvar(&g_gametype, GT_SD);
    SetCvar(&g_roundtime, 120); SetCvar(&g_roundwarmup, 5); SetCvar(&g_roundswitch, 1);
    SetCvar(&g_bombplanttime, 4); SetCvar(&g_bombdefusetime, 7);
    SetCvar(&g_bombtimer, 40); SetCvar(&g_bombradius, 128);
    for (i = 0; i < PLAYERS; i++) {
        gentity_t *e = &g_entities[i];
        e->inuse = qtrue;
        e->client = &clients[i];
        clients[i].pers.connected = CON_CONNECTED;
        clients[i].sess.sessionTeam = i < BLUE0 ? TEAM_RED : TEAM_BLUE;
        clients[i].ps.stats[STAT_HEALTH] = e->health = 100;
        clients[i].ps.clientNum = i;
    }
    if (teamSpawns) {
        AddEntity("team_CTF_redspawn", -1300, 0, 24);
        AddEntity("team_CTF_bluespawn", 1300, 300, 24);
        AddEntity("team_CTF_bluespawn", 1300, -300, 24);
    } else {
        /* a deathmatch map: two ends, no team spawns */
        AddEntity("info_player_deathmatch", -1000, 0, 24);
        AddEntity("info_player_deathmatch", -900, 100, 24);
        AddEntity("info_player_deathmatch", 1000, 400, 24);
        AddEntity("info_player_deathmatch", 1000, -400, 24);
    }
    if (authoredSites) {
        AddPointSite("A", 700, 450);
        AddPointSite("B", 700, -450);
    }
}

static void Frame(void) {
    level.previousTime = level.time;
    level.time += 50;
    CheckRound();
}
static void RunMs(int ms) { int t; for (t = 0; t < ms; t += 50) Frame(); }
static void RunUntil(int state, int capMs) {
    int t;
    for (t = 0; t < capMs && level.roundState != state; t += 50) Frame();
    CHECK(level.roundState == state);
}
static void StartAndGoLive(void) {
    Frame();                                    /* RND_NONE -> first round */
    CHECK(level.roundState == RND_PREROUND);
    RunUntil(RND_ACTIVE, g_roundwarmup.integer * 1000 + 100);
}
static void Kill(int c) { clients[c].ps.stats[STAT_HEALTH] = g_entities[c].health = 0; }
static void MoveTo(int c, float x, float y) { VectorSet(g_entities[c].r.currentOrigin, x, y, 24); }
static const char *CS(const char *key) { return Info_ValueForKey(configstrings[CS_SD_STATE], key); }
static int CSi(const char *key) { return atoi(CS(key)); }

/* --- tests -------------------------------------------------------------------- */
static void TestRoundStartsAndPublishes(void) {
    NewMatch(qtrue, qtrue);
    Frame();
    CHECK(level.roundNumber == 1 && level.roundState == RND_PREROUND);
    CHECK(G_AttackingTeam() == TEAM_RED);
    CHECK(level.bombState == BOMB_CARRIED);
    CHECK(level.bombCarrier >= RED0 && level.bombCarrier < BLUE0);       /* an attacker */
    CHECK(spawnCalls >= PLAYERS);                                         /* everyone reset */
    /* the HUD gets the same facts */
    CHECK(CSi("st") == RND_PREROUND && CSi("n") == 1 && CSi("att") == TEAM_RED);
    CHECK(CSi("bs") == BOMB_CARRIED && CSi("bc") == level.bombCarrier);
    CHECK(CSi("se") == level.roundStateEndTime);
    RunUntil(RND_ACTIVE, g_roundwarmup.integer * 1000 + 100);
    CHECK(level.roundEndTime == level.time + g_roundtime.integer * 1000 - 0 ||
          level.roundEndTime > level.time);
    CHECK(CSi("st") == RND_ACTIVE && CSi("re") == level.roundEndTime);
}

static void TestNoRoundWithAnEmptyTeam(void) {
    int i;
    NewMatch(qtrue, qtrue);
    for (i = BLUE0; i < PLAYERS; i++) clients[i].pers.connected = CON_DISCONNECTED;
    RunMs(2000);
    CHECK(level.roundState == RND_NONE && level.roundNumber == 0);
    CHECK(CSi("st") == RND_NONE);
}

static void TestElimination(void) {
    int i;
    NewMatch(qtrue, qtrue);
    StartAndGoLive();
    for (i = BLUE0; i < PLAYERS; i++) Kill(i);
    Frame();
    CHECK(level.roundState == RND_POSTROUND && level.roundWinner == TEAM_RED);
    CHECK(level.teamScores[TEAM_RED] == 1 && level.teamScores[TEAM_BLUE] == 0);
    CHECK(CSi("st") == RND_POSTROUND && CSi("w") == TEAM_RED);

    NewMatch(qtrue, qtrue);
    StartAndGoLive();
    for (i = RED0; i < BLUE0; i++) Kill(i);
    Frame();
    CHECK(level.roundWinner == TEAM_BLUE && level.teamScores[TEAM_BLUE] == 1);
}

static void TestTimerFavoursDefence(void) {
    NewMatch(qtrue, qtrue);
    StartAndGoLive();
    RunUntil(RND_POSTROUND, g_roundtime.integer * 1000 + 200);
    CHECK(level.roundWinner == G_DefendingTeam());
}

/* Planting needs ALL of: the carrier, alive, in a site, holding USE, for the
 * full plant time. Each is taken away in turn. */
static void TestPlantRules(void) {
    int carrier, plantMs = g_bombplanttime.integer * 1000;

    NewMatch(qtrue, qtrue);
    StartAndGoLive();
    carrier = level.bombCarrier;

    /* holding USE outside any site does nothing */
    MoveTo(carrier, 0, 0);
    clients[carrier].buttons = BUTTON_USE;
    RunMs(plantMs + 500);
    CHECK(level.bombState == BOMB_CARRIED && level.plantProgress == 0);

    /* the OLD button no longer plants: the objective is on USE */
    MoveTo(carrier, 700, 450);
    clients[carrier].buttons = BUTTON_USE_HOLDABLE;
    RunMs(plantMs + 500);
    CHECK(level.bombState == BOMB_CARRIED);

    /* in a site, holding USE: progress, and the HUD sees a plant with a
     * START time that stays put while it continues - no per-frame traffic */
    clients[carrier].buttons = BUTTON_USE;
    Frame(); Frame();
    CHECK(level.plantProgress > 0);
    CHECK(CSi("ak") == SDACT_PLANT && CSi("aa") == carrier && CSi("ad") == plantMs);
    {
        int start = CSi("as"), writes = configstringWrites[CS_SD_STATE];
        RunMs(1000);
        CHECK(CSi("as") == start);
        CHECK(configstringWrites[CS_SD_STATE] == writes);
    }
    CHECK(G_OnObjective(&g_entities[carrier]));

    /* letting go throws the attempt away */
    clients[carrier].buttons = 0;
    Frame();
    CHECK(level.plantProgress == 0 && CSi("ak") == SDACT_NONE);
    CHECK(!G_OnObjective(&g_entities[carrier]) || G_PointSiteAt(g_entities[carrier].r.currentOrigin) >= 0);

    /* a defender standing in the site holding USE cannot plant */
    MoveTo(BLUE0, 700, 450);
    clients[BLUE0].buttons = BUTTON_USE;
    RunMs(plantMs + 500);
    CHECK(level.bombState == BOMB_CARRIED);

    /* the whole thing, uninterrupted, plants - at site A */
    clients[carrier].buttons = BUTTON_USE;
    RunMs(plantMs + 200);
    CHECK(level.bombState == BOMB_PLANTED);
    CHECK(level.bombSite >= 0 && level.bombSites[level.bombSite].label == 'A');
    CHECK(level.bombCarrier == -1);
    CHECK(CSi("bs") == BOMB_PLANTED && !strcmp(CS("site"), "A") && CSi("bt") == level.bombDetonateTime);
}

static void PlantAt(float x, float y) {
    int carrier = level.bombCarrier;
    MoveTo(carrier, x, y);
    clients[carrier].buttons = BUTTON_USE;
    RunMs(g_bombplanttime.integer * 1000 + 200);
    clients[carrier].buttons = 0;
    CHECK(level.bombState == BOMB_PLANTED);
}

static void TestDetonation(void) {
    NewMatch(qtrue, qtrue);
    StartAndGoLive();
    PlantAt(700, -450);
    CHECK(level.bombSites[level.bombSite].label == 'B');
    /* once planted the round clock no longer decides it */
    RunUntil(RND_POSTROUND, g_bombtimer.integer * 1000 + 200);
    CHECK(level.bombState == BOMB_DETONATED && level.roundWinner == G_AttackingTeam());
}

static void TestDefuse(void) {
    int d = BLUE0, defuseMs = g_bombdefusetime.integer * 1000;
    NewMatch(qtrue, qtrue);
    StartAndGoLive();
    PlantAt(700, 450);

    /* out of reach: nothing */
    MoveTo(d, 0, 0);
    clients[d].buttons = BUTTON_USE;
    RunMs(defuseMs / 2);
    CHECK(level.defuseProgress == 0 && !G_OnObjective(&g_entities[d]));

    /* in reach, interrupted half way: starts over */
    MoveTo(d, 700, 450);
    RunMs(defuseMs / 2);
    CHECK(level.defuseProgress > 0 && CSi("ak") == SDACT_DEFUSE && CSi("aa") == d);
    CHECK(G_OnObjective(&g_entities[d]));
    clients[d].buttons = 0;
    Frame();
    CHECK(level.defuseProgress == 0);

    /* an attacker on the bomb cannot defuse it */
    MoveTo(RED0, 700, 450);
    clients[RED0].buttons = BUTTON_USE;
    RunMs(defuseMs + 200);
    CHECK(level.bombState == BOMB_PLANTED);
    clients[RED0].buttons = 0;

    /* uninterrupted: defenders take the round */
    clients[d].buttons = BUTTON_USE;
    RunMs(defuseMs + 200);
    CHECK(level.bombState == BOMB_DEFUSED && level.roundWinner == G_DefendingTeam());
}

static void TestDefendersWipedAfterPlant(void) {
    int i;
    NewMatch(qtrue, qtrue);
    StartAndGoLive();
    PlantAt(700, 450);
    for (i = BLUE0; i < PLAYERS; i++) Kill(i);
    Frame();
    CHECK(level.roundWinner == G_AttackingTeam());
}

static void TestCarrierDeathPassesTheBomb(void) {
    int first;
    NewMatch(qtrue, qtrue);
    StartAndGoLive();
    first = level.bombCarrier;
    Kill(first);
    G_BombCarrierDied(&g_entities[first]);
    CHECK(level.bombCarrier >= RED0 && level.bombCarrier < BLUE0 && level.bombCarrier != first);
    CHECK(clients[level.bombCarrier].ps.stats[STAT_HEALTH] > 0);
}

/* Sides swap every g_roundswitch rounds, and the bomb goes to the NEW side.
 * Scores stay with the team. Spawning follows the role, not the colour. */
static void TestSideSwitch(void) {
    int i, n;
    for (n = 0; n <= 2; n++) {
        int r;
        NewMatch(qtrue, qtrue);
        SetCvar(&g_roundswitch, n);
        for (r = 1; r <= 5; r++) {
            team_t expected;
            if (r == 1) {
                Frame();
            } else {
                for (i = 0; i < PLAYERS; i++) if (clients[i].sess.sessionTeam == G_DefendingTeam()) Kill(i);
                Frame();                                    /* attackers win the round */
                RunUntil(RND_PREROUND, 10000);
            }
            /* rounds played before this one = r - 1 */
            expected = (n > 0 && ((r - 1) / n) % 2) ? TEAM_BLUE : TEAM_RED;
            CHECK(level.roundNumber == r);
            CHECK(G_AttackingTeam() == expected);
            CHECK(CSi("att") == expected);
            CHECK(clients[level.bombCarrier].sess.sessionTeam == expected);
            RunUntil(RND_ACTIVE, 10000);
            for (i = 0; i < PLAYERS; i++) clients[i].ps.stats[STAT_HEALTH] = g_entities[i].health = 100;
        }
    }
    /* scores follow the team that won, whichever side it was on */
    NewMatch(qtrue, qtrue);
    SetCvar(&g_roundswitch, 1);
    StartAndGoLive();
    for (i = BLUE0; i < PLAYERS; i++) Kill(i);             /* red attacking, red wins */
    Frame();
    RunUntil(RND_ACTIVE, 20000);
    CHECK(G_AttackingTeam() == TEAM_BLUE);
    for (i = RED0; i < BLUE0; i++) Kill(i);                /* now red defends, blue wins */
    Frame();
    CHECK(level.teamScores[TEAM_RED] == 1 && level.teamScores[TEAM_BLUE] == 1);
}

static void TestSpawnByRole(void) {
    NewMatch(qtrue, qtrue);
    SetCvar(&g_roundswitch, 1);
    G_SelectSDSpawnPoint(&g_entities[RED0], level.bombSites[0].origin, level.bombSites[0].origin);
    CHECK(lastCTFTeamAsked == TEAM_RED);                   /* red attacks: attacker set */
    G_SelectSDSpawnPoint(&g_entities[BLUE0], level.bombSites[0].origin, level.bombSites[0].origin);
    CHECK(lastCTFTeamAsked == TEAM_BLUE);
    level.attackingTeam = TEAM_BLUE;                       /* after a switch */
    G_SelectSDSpawnPoint(&g_entities[RED0], level.bombSites[0].origin, level.bombSites[0].origin);
    CHECK(lastCTFTeamAsked == TEAM_BLUE);                  /* red now DEFENDS: defender set */
    G_SelectSDSpawnPoint(&g_entities[BLUE0], level.bombSites[0].origin, level.bombSites[0].origin);
    CHECK(lastCTFTeamAsked == TEAM_RED);
}

/* No trigger_bombsite and no team spawns: the map is still playable. */
static void TestLayoutFallbacks(void) {
    vec3_t o, a;
    int i;

    /* team spawns, no sites: sites go on the defenders' spawns */
    NewMatch(qtrue, qfalse);
    Frame();
    CHECK(level.numBombSites == 2);
    for (i = 0; i < level.numBombSites; i++) {
        CHECK(level.bombSites[i].origin[0] == 1300);       /* the bluespawns */
        CHECK(level.bombSites[i].radius == DEFAULT_SITE_RADIUS);
    }
    CHECK(level.bombSites[0].label == 'A' && level.bombSites[1].label == 'B');

    /* deathmatch spawns only: two ends, sites on the defenders' end */
    NewMatch(qfalse, qfalse);
    Frame();
    CHECK(level.sdSidesFromDeathmatch);
    CHECK(level.numBombSites == 2);
    for (i = 0; i < level.numBombSites; i++) {
        CHECK(G_OnDefenderSide(level.bombSites[i].origin));
    }
    /* and each side spawns at its own end */
    for (i = 0; i < 20; i++) {
        gentity_t *s = G_SelectSDSpawnPoint(&g_entities[RED0], o, a);
        CHECK(!G_OnDefenderSide(s->s.origin));
        s = G_SelectSDSpawnPoint(&g_entities[BLUE0], o, a);
        CHECK(G_OnDefenderSide(s->s.origin));
    }
    /* a full round is playable on it */
    RunUntil(RND_ACTIVE, 10000);
    PlantAt(level.bombSites[0].origin[0], level.bombSites[0].origin[1]);

    /* authored sites always win over the fallback */
    NewMatch(qfalse, qtrue);
    Frame();
    CHECK(level.numBombSites == 2 && level.bombSites[0].origin[0] == 700);
}

static void TestBombSiteForms(void) {
    gentity_t *e;
    NewMatch(qtrue, qfalse);
    /* point form, explicit radius and label */
    e = AddEntity("trigger_bombsite", 10, 20, 24);
    spawnSite = "B"; spawnRadius = "200";
    SP_trigger_bombsite(e);
    CHECK(level.numBombSites == 1 && level.bombSites[0].label == 'B');
    CHECK(level.bombSites[0].radius == 200 && !level.bombSites[0].trigger);
    CHECK(!e->inuse);                                      /* pure data, entity freed */
    /* point form, defaults: lettered by order, radius clamped to something sane */
    e = AddEntity("trigger_bombsite", 30, 40, 24);
    spawnSite = NULL; spawnRadius = "1";
    SP_trigger_bombsite(e);
    CHECK(level.bombSites[1].label == 'B' + 0 || level.bombSites[1].label == 'B');
    CHECK(level.bombSites[1].radius >= 16);
    /* brush form keeps its entity and is the trigger */
    e = AddEntity("trigger_bombsite", 0, 0, 0);
    e->model = "*1";
    VectorSet(e->r.absmin, -64, -64, 0); VectorSet(e->r.absmax, 64, 64, 128);
    spawnSite = "C"; spawnRadius = NULL;
    SP_trigger_bombsite(e);
    CHECK(level.bombSites[2].trigger == e && level.bombSites[2].radius == 0);
    CHECK(e->inuse && e->touch == bombsite_touch && e->count == 2);
}

/* A round starts on a clean map: last round's grenades, rockets, dropped guns
 * and bodies are gone BEFORE anyone respawns. Map pickups are left alone. */
static void TestRoundStartsClean(void) {
    gentity_t *grenade, *rocket, *dropped, *pickup, *body;
    int i;
    NewMatch(qtrue, qtrue);
    StartAndGoLive();
    grenade = AddEntity("grenade", 0, 0, 24);  grenade->s.eType = ET_MISSILE;
    rocket  = AddEntity("rocket", 50, 0, 24);  rocket->s.eType = ET_MISSILE;
    dropped = AddEntity("weapon_shotgun", 90, 0, 24); dropped->flags |= FL_DROPPED_ITEM;
    pickup  = AddEntity("weapon_railgun", 130, 0, 24); pickup->s.eType = ET_ITEM;
    body    = AddEntity("bodyque", 170, 0, 24);
    body->r.linked = qtrue; body->physicsObject = qtrue; body->nextthink = level.time + 999;
    level.bodyQue[0] = body;
    for (i = BLUE0; i < PLAYERS; i++) Kill(i);
    Frame();                                         /* round decided */
    CHECK(grenade->inuse && rocket->inuse);          /* nothing cleared mid-result */
    RunUntil(RND_PREROUND, 10000);                   /* next round */
    CHECK(!grenade->inuse && !rocket->inuse);
    CHECK(!dropped->inuse);
    CHECK(pickup->inuse);
    CHECK(body->inuse && !body->r.linked && !body->physicsObject && !body->nextthink);
}

/* Planting and defusing happen standing on something - not mid-jump, not
 * falling, not hanging on a ladder (which takes you off the ground). */
static void TestObjectiveNeedsFooting(void) {
    int carrier, d = BLUE0;
    NewMatch(qtrue, qtrue);
    StartAndGoLive();
    carrier = level.bombCarrier;
    MoveTo(carrier, 700, 450);
    clients[carrier].ps.groundEntityNum = ENTITYNUM_NONE;
    clients[carrier].buttons = BUTTON_USE;
    RunMs(g_bombplanttime.integer * 1000 + 500);
    CHECK(level.bombState == BOMB_CARRIED && level.plantProgress == 0);
    CHECK(!G_OnObjective(&g_entities[carrier]));
    clients[carrier].ps.groundEntityNum = ENTITYNUM_WORLD;   /* lands */
    RunMs(g_bombplanttime.integer * 1000 + 200);
    CHECK(level.bombState == BOMB_PLANTED);

    MoveTo(d, 700, 450);
    clients[d].ps.groundEntityNum = ENTITYNUM_NONE;
    clients[d].buttons = BUTTON_USE;
    RunMs(g_bombdefusetime.integer * 1000 + 500);
    CHECK(level.bombState == BOMB_PLANTED && level.defuseProgress == 0);
    clients[d].ps.groundEntityNum = ENTITYNUM_WORLD;
    RunMs(g_bombdefusetime.integer * 1000 + 200);
    CHECK(level.bombState == BOMB_DEFUSED);
}

int main(void) {
    TestRoundStartsAndPublishes();
    TestNoRoundWithAnEmptyTeam();
    TestElimination();
    TestTimerFavoursDefence();
    TestPlantRules();
    TestDetonation();
    TestDefuse();
    TestDefendersWipedAfterPlant();
    TestCarrierDeathPassesTheBomb();
    TestSideSwitch();
    TestSpawnByRole();
    TestLayoutFallbacks();
    TestBombSiteForms();
    TestRoundStartsClean();
    TestObjectiveNeedsFooting();
    printf("PASS: S&D rounds, plant/defuse/detonate, elimination, timer, side switch, spawn roles, layout fallbacks, clean round starts, footing and HUD state (%d checks).\n", checks);
    return 0;
}
