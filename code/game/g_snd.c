/* Search and Destroy. Server-owned round/objective logic, independent of the
 * weapon notetrack/state machine. Uses existing Use stats and configstrings;
 * no extra playerState fields or overloaded weapon states. */
#include "g_local.h"

static vmCvar_t sndRoundTime, sndBombTime, sndPlantTime, sndDefuseTime;
static vmCvar_t sndWinLimit, sndSwitchRounds, sndCountdown, sndRoundPause, sndPractice;
typedef struct { vmCvar_t *value; char *name; char *initial; int flags; } sndCvar_t;
static sndCvar_t sndCvars[] = {
    { &sndRoundTime, "g_sndRoundTime", "150", CVAR_ARCHIVE },
    { &sndBombTime, "g_sndBombTime", "45", CVAR_ARCHIVE },
    { &sndPlantTime, "g_sndPlantTime", "5", CVAR_ARCHIVE },
    { &sndDefuseTime, "g_sndDefuseTime", "5", CVAR_ARCHIVE },
    { &sndWinLimit, "g_sndWinLimit", "4", CVAR_ARCHIVE },
    { &sndSwitchRounds, "g_sndSwitchRounds", "3", CVAR_ARCHIVE },
    { &sndCountdown, "g_sndCountdown", "5", CVAR_ARCHIVE },
    { &sndRoundPause, "g_sndRoundPause", "5", CVAR_ARCHIVE },
    { &sndPractice, "g_sndPractice", "0", CVAR_CHEAT }
};

// MW2's map_restart follows the final killcam by doFinalKillcam's wait 0.1
// (then 0.05s polls until nobody is in the killcam).
#define SND_KILLCAM_RESTART 100

typedef struct {
    sndPhase_t phase;
    // result is also set, with phase still SND_PLANTED, while a detonation or
    // defuse waits out its delay; freezeTime ends the round's last control.
    int round, deadline, carrier, plantedSite, winner, result, freezeTime;
    team_t attacking;
    qboolean spawning, practice;
    int roundMs, bombMs, plantMs, defuseMs, winLimit, switchRounds, pauseMs;
    struct { int killer, time; } lastDeath[TEAM_NUM_TEAMS];
    gentity_t *sites[SND_MAX_SITES], *bomb;
    int numSites;
    vec3_t bombOrigin;
    qboolean alive[MAX_CLIENTS];
    team_t team[MAX_CLIENTS];
    int following[MAX_CLIENTS], buttons[MAX_CLIENTS];
    int holdTarget[MAX_CLIENTS], holdStart[MAX_CLIENTS], holdSeen[MAX_CLIENTS];
    char published[MAX_INFO_STRING];
} sndState_t;
static sndState_t snd;

static int SND_Clamp( int value, int low, int high ) {
    return value < low ? low : (value > high ? high : value);
}

static qboolean SND_Player( int n ) {
    return n >= 0 && n < level.maxclients && g_entities[n].inuse &&
        level.clients[n].pers.connected == CON_CONNECTED &&
        (level.clients[n].sess.sessionTeam == TEAM_RED ||
         level.clients[n].sess.sessionTeam == TEAM_BLUE);
}

static qboolean SND_Alive( int n ) {
    return SND_Player(n) && snd.alive[n] && g_entities[n].health > 0 &&
        level.clients[n].sess.sessionTeam == snd.team[n];
}

static int SND_Count( team_t team, qboolean living ) {
    int n, count = 0;
    for ( n = 0; n < level.maxclients; n++ ) {
        if ( SND_Player(n) && level.clients[n].sess.sessionTeam == team &&
             (!living || SND_Alive(n)) ) count++;
    }
    return count;
}

static void SND_ClearUse( int n ) {
    snd.holdTarget[n] = -1;
    snd.holdStart[n] = snd.holdSeen[n] = 0;
    if ( level.clients[n].ps.stats[STAT_USE_TARGET] <= SND_USE_PLANT ) {
        level.clients[n].ps.stats[STAT_USE_TARGET] = -1;
        level.clients[n].ps.stats[STAT_USE_PROGRESS] = 0;
    }
}

static void SND_Publish( void ) {
    char text[MAX_INFO_STRING];
    Com_sprintf(text, sizeof(text),
        "\\phase\\%i\\round\\%i\\attack\\%i\\deadline\\%i\\carrier\\%i"
        "\\winner\\%i\\result\\%i\\red\\%i\\blue\\%i\\site\\%i"
        "\\x\\%i\\y\\%i\\z\\%i\\goal\\%i\\practice\\%i",
        snd.phase, snd.round, snd.attacking, snd.deadline, snd.carrier,
        snd.winner, snd.result, SND_Count(TEAM_RED,qtrue), SND_Count(TEAM_BLUE,qtrue),
        snd.plantedSite, (int)snd.bombOrigin[0], (int)snd.bombOrigin[1],
        (int)snd.bombOrigin[2], snd.winLimit, snd.practice);
    if ( strcmp(text, snd.published) ) {
        Q_strncpyz(snd.published, text, sizeof(snd.published));
        trap_SetConfigstring(CS_SND, text);
    }
}

void G_SNDInit( void ) {
    int n;
    memset(&snd, 0, sizeof(snd));
    snd.carrier = snd.plantedSite = -1;
    for ( n = 0; n < TEAM_NUM_TEAMS; n++ ) snd.lastDeath[n].killer = -1;
    snd.attacking = TEAM_RED;
    snd.winLimit = 4;
    for ( n = 0; n < MAX_CLIENTS; n++ ) {
        snd.following[n] = snd.holdTarget[n] = -1;
    }
    for ( n = 0; n < ARRAY_LEN(sndCvars); n++ ) {
        trap_Cvar_Register(sndCvars[n].value, sndCvars[n].name,
                           sndCvars[n].initial, sndCvars[n].flags);
    }
    trap_SetConfigstring(CS_SND, "");
    trap_SetConfigstring(CS_SND_SITES, "");
}

void SP_peace_bombsite( gentity_t *ent ) {
    int index;
    if ( g_gametype.integer != GT_SND ) { G_FreeEntity(ent); return; }
    G_SpawnInt("site", "0", &index);
    G_SpawnFloat("radius", "72", &ent->wait);
    if ( index < 0 || index >= SND_MAX_SITES || snd.sites[index] ) {
        G_Printf("SND: invalid or duplicate bomb site %i\n", index);
        G_FreeEntity(ent); return;
    }
    ent->wait = Com_Clamp(32, 160, ent->wait);
    ent->r.svFlags |= SVF_NOCLIENT;
    G_SetOrigin(ent, ent->s.origin);
    snd.sites[index] = ent;
    snd.numSites++;
}

void G_SNDMapReady( void ) {
    int n;
    char sites[MAX_INFO_STRING], key[16], value[64];
    if ( g_gametype.integer != GT_SND ) return;
    level.warmupTime = 0;
    trap_SetConfigstring(CS_WARMUP, "0");
    sites[0] = '\0';
    for ( n = 0; n < SND_MAX_SITES; n++ ) if ( snd.sites[n] ) {
        Com_sprintf(key, sizeof(key), "s%i", n);
        Com_sprintf(value, sizeof(value), "%i %i %i %i",
            (int)snd.sites[n]->r.currentOrigin[0], (int)snd.sites[n]->r.currentOrigin[1],
            (int)snd.sites[n]->r.currentOrigin[2], (int)snd.sites[n]->wait);
        Info_SetValueForKey(sites, key, value);
    }
    trap_SetConfigstring(CS_SND_SITES, sites);
    if ( !snd.numSites ) G_Printf("SND: map has no peace_bombsite; waiting for a supported map.\n");
    snd.bomb = G_Spawn();
    snd.bomb->classname = "snd_bomb";
    snd.bomb->neverFree = qtrue;
    snd.bomb->s.eType = ET_GENERAL;
    snd.bomb->s.modelindex = G_ModelIndex("models/peace/snd_bomb.md3");
    snd.bomb->r.svFlags = SVF_NOCLIENT;
    snd.bomb->r.contents = 0;
    SND_Publish();
}

static void SND_ShowBomb( vec3_t origin ) {
    vec3_t end;
    trace_t tr;
    VectorCopy(origin, snd.bombOrigin);
    // Site origins sit above the floor for use/visibility traces; rest the
    // case itself on the floor instead of making it float at the marker.
    VectorCopy(origin, end); end[2] -= 128;
    trap_Trace(&tr, origin, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID);
    if ( !tr.startsolid && tr.fraction < 1 ) {
        VectorCopy(tr.endpos, snd.bombOrigin); snd.bombOrigin[2] += .5f;
    }
    if ( !snd.bomb ) return;
    G_SetOrigin(snd.bomb, snd.bombOrigin);
    snd.bomb->r.svFlags &= ~SVF_NOCLIENT;
    trap_LinkEntity(snd.bomb);
}

static void SND_HideBomb( void ) {
    if ( !snd.bomb ) return;
    snd.bomb->r.svFlags |= SVF_NOCLIENT;
    trap_UnlinkEntity(snd.bomb);
}

static void SND_DropBomb( gentity_t *ent ) {
    vec3_t end, origin;
    trace_t tr;
    VectorCopy(ent->client->ps.origin, origin);
    VectorCopy(origin, end); end[2] -= 4096;
    trap_Trace(&tr, origin, NULL, NULL, end, ent->s.number, MASK_SOLID);
    if ( !tr.startsolid && tr.fraction < 1 ) {
        VectorCopy(tr.endpos, origin); origin[2] += 2;
    }
    // Never strand the objective in a void/nodrop volume. Returning it to a
    // map-authored site is deterministic and does not reward a suicide.
    if ( tr.startsolid || tr.fraction == 1 ||
         (trap_PointContents(origin, -1) & (CONTENTS_NODROP | CONTENTS_LAVA | CONTENTS_SLIME)) ) {
        int n;
        for ( n = 0; n < SND_MAX_SITES; n++ ) if ( snd.sites[n] ) {
            VectorCopy(snd.sites[n]->r.currentOrigin, origin); break;
        }
    }
    snd.carrier = -1;
    SND_ShowBomb(origin);
    trap_SendServerCommand(-1, "print \"Bomb dropped.\n\"");
}

void G_SNDClientDied( gentity_t *ent ) {
    int n = ent - g_entities;
    if ( g_gametype.integer != GT_SND || n < 0 || n >= level.maxclients ) return;
    // A suicide, disconnect or team change must not replay an earlier kill
    // as the winning shot. player_die records a valid enemy kill afterwards.
    if ( snd.alive[n] ) snd.lastDeath[snd.team[n]].killer = -1;
    if ( snd.carrier == n && snd.phase == SND_LIVE ) SND_DropBomb(ent);
    else if ( snd.carrier == n ) snd.carrier = -1;
    snd.alive[n] = qfalse;
    snd.following[n] = -1;
    SND_ClearUse(n);
}

static void SND_CheckElimination( void );

// Every death in a round comes here from player_die, after G_SNDClientDied.
// An enemy's kill is remembered for the killcam; then the death decides the
// round there and then, as MW2's does (below).
void G_SNDRecordKill( gentity_t *victim, gentity_t *attacker ) {
    int n;
    team_t team;
    if ( g_gametype.integer != GT_SND || (snd.phase != SND_LIVE && snd.phase != SND_PLANTED) ) return;
    if ( attacker && attacker->client && victim && victim->client && attacker != victim ) {
        n = attacker - g_entities;
        team = victim->client->sess.sessionTeam;
        if ( SND_Player(n) && (team == TEAM_RED || team == TEAM_BLUE) &&
             attacker->client->sess.sessionTeam != team ) {
            snd.lastDeath[team].killer = n;
            snd.lastDeath[team].time = level.time;
        }
    }
    SND_CheckElimination();
}

qboolean G_SNDCanDamage( gentity_t *ent ) {
    int n = ent-g_entities;
    if ( g_gametype.integer != GT_SND || !ent->client ) return qtrue;
    // player_die is entered AFTER health reaches zero; eligibility, not the
    // post-damage health value, decides whether this death belongs to a round.
    return (snd.phase == SND_LIVE || snd.phase == SND_PLANTED) && SND_Player(n) &&
        snd.alive[n] && level.clients[n].sess.sessionTeam == snd.team[n];
}

qboolean G_SNDCanRespawn( void ) {
    return g_gametype.integer != GT_SND || snd.phase == SND_WAITING;
}

team_t G_SNDSpawnTeam( team_t team ) {
    if ( g_gametype.integer != GT_SND ) return team;
    return team == snd.attacking ? TEAM_RED : TEAM_BLUE;
}

qboolean G_SNDWaitForRound( gentity_t *ent ) {
    int n = ent - g_entities;
    playerState_t *ps = &ent->client->ps;
    if ( g_gametype.integer != GT_SND || ent->client->sess.sessionTeam == TEAM_SPECTATOR ||
         snd.spawning || snd.phase == SND_WAITING || snd.phase == SND_COUNTDOWN ) return qfalse;
    G_SNDClientDied(ent);
    ps->clientNum = n;
    ps->persistant[PERS_TEAM] = ent->client->sess.sessionTeam;
    ps->stats[STAT_HEALTH] = ent->health = 0;
    ps->pm_type = PM_FREEZE;
    ps->pm_flags &= ~PMF_FOLLOW;
    ps->weapon = WP_NONE;
    ps->weaponAnimTime = -1;
    ent->takedamage = qfalse;
    ent->r.contents = 0;
    ent->r.svFlags |= SVF_NOCLIENT;
    trap_UnlinkEntity(ent);
    trap_SendServerCommand(n, "cp \"You will deploy next round.\"");
    return qtrue;
}

void G_SNDClientSpawned( gentity_t *ent ) {
    int n = ent - g_entities;
    if ( g_gametype.integer != GT_SND ) return;
    snd.alive[n] = ent->client->sess.sessionTeam == TEAM_RED || ent->client->sess.sessionTeam == TEAM_BLUE;
    snd.team[n] = ent->client->sess.sessionTeam;
    snd.following[n] = -1;
    snd.buttons[n] = 0;
    SND_ClearUse(n);
    ent->r.svFlags &= ~SVF_NOCLIENT;
    if ( snd.alive[n] ) {
        ent->health = ent->client->ps.stats[STAT_HEALTH] = ent->client->ps.stats[STAT_MAX_HEALTH];
        ent->client->ps.stats[STAT_ARMOR] = 0;
    }
}

static int SND_Follow( int n, int after ) {
    int i, target;
    for ( i = 1; i <= level.maxclients; i++ ) {
        target = (after + i + level.maxclients) % level.maxclients;
        if ( target != n && SND_Alive(target) &&
             level.clients[target].sess.sessionTeam == level.clients[n].sess.sessionTeam ) return target;
    }
    return -1;
}

qboolean G_SNDClientThink( gentity_t *ent, usercmd_t *cmd ) {
    int n = ent - g_entities;
    qboolean alive;
    if ( g_gametype.integer != GT_SND || ent->client->sess.sessionTeam == TEAM_SPECTATOR ||
         snd.phase == SND_WAITING ) return qfalse;
    alive = SND_Alive(n);
    // A round that has ended keeps everyone's full control until its freeze
    // (MW2's endGame: freezePlayerForRoundEnd), with damage and use already off.
    if ( alive && (snd.phase == SND_LIVE || snd.phase == SND_PLANTED ||
         (snd.phase == SND_ROUND_OVER && level.time < snd.freezeTime)) ) return qfalse;
    if ( !alive && (cmd->buttons & BUTTON_ATTACK) && !(snd.buttons[n] & BUTTON_ATTACK) ) {
        snd.following[n] = SND_Follow(n, snd.following[n]);
    }
    snd.buttons[n] = cmd->buttons;
    ent->client->ps.commandTime = cmd->serverTime;
    if ( alive ) {
        ent->client->ps.pm_type = PM_FREEZE;
        PM_UpdateViewAngles(&ent->client->ps, cmd);
    }
    VectorClear(ent->client->ps.velocity);
    return qtrue;
}

qboolean G_SNDClientEndFrame( gentity_t *ent ) {
    int n = ent-g_entities, target, persistent[MAX_PERSISTANT], flags, ping;
    playerState_t *ps = &ent->client->ps;
    if ( g_gametype.integer != GT_SND || snd.phase == SND_WAITING ||
         level.intermissiontime || ent->client->sess.sessionTeam == TEAM_SPECTATOR || SND_Alive(n) ) return qfalse;
    if ( ent->r.linked ) { CopyToBodyQue(ent); trap_UnlinkEntity(ent); }
    ent->r.svFlags |= SVF_NOCLIENT;
    ent->r.contents = 0;
    ent->takedamage = qfalse;
    target = snd.following[n];
    if ( !SND_Alive(target) || level.clients[target].sess.sessionTeam != ent->client->sess.sessionTeam ) {
        target = snd.following[n] = SND_Follow(n, n);
    }
    if ( target >= 0 ) {
        memcpy(persistent, ps->persistant, sizeof(persistent));
        flags = ps->eFlags & (EF_VOTED | EF_TEAMVOTED); ping = ps->ping;
        *ps = level.clients[target].ps;
        memcpy(ps->persistant, persistent, sizeof(persistent));
        ps->persistant[PERS_TEAM] = ent->client->sess.sessionTeam;
        ps->eFlags = (ps->eFlags & ~(EF_VOTED | EF_TEAMVOTED)) | flags;
        ps->pm_flags |= PMF_FOLLOW;
        ps->ping = ping;
    } else {
        ps->clientNum = n;
        ps->pm_flags &= ~PMF_FOLLOW;
        ps->pm_type = PM_FREEZE;
        ps->stats[STAT_HEALTH] = 0;
        ps->weapon = WP_NONE;
    }
    ps->stats[STAT_USE_TARGET] = -1;
    ps->stats[STAT_USE_PROGRESS] = 0;
    return qtrue;
}

static qboolean SND_Near( gentity_t *ent, vec3_t point, float radius ) {
    vec3_t delta, eye;
    trace_t tr;
    VectorSubtract(ent->client->ps.origin, point, delta);
    if ( VectorLengthSquared(delta) > radius*radius ) return qfalse;
    VectorCopy(ent->client->ps.origin, eye); eye[2] += ent->client->ps.viewheight;
    trap_Trace(&tr, eye, NULL, NULL, point, ent->s.number, MASK_SOLID);
    return !tr.startsolid && tr.fraction == 1;
}

// The decisive elimination's death, or now when nobody killed them.
static int SND_EliminationTime( team_t team ) {
    return snd.lastDeath[team].killer >= 0 ? snd.lastDeath[team].time : level.time;
}

// MW2's sd_endGame/endGame. eventTime is the deciding moment: the last death
// of an elimination, else the frame the result came. The game is postgame at
// once (no damage, no use); everyone keeps control for endGame's waitframe and
// freezePlayerForRoundEnd( 1.0 ). The round-winning killcam plays when
// displayRoundEnd's roundEndWait is over, and the next round follows it.
static void SND_EndRound( team_t winner, sndResult_t result, int eventTime ) {
    int n, death;
    qboolean matchWon;
    team_t losing = winner == TEAM_RED ? TEAM_BLUE : TEAM_RED;
    if ( snd.phase != SND_LIVE && snd.phase != SND_PLANTED ) return;
    snd.phase = SND_ROUND_OVER;
    snd.winner = winner; snd.result = result;
    snd.freezeTime = eventTime + ROUNDEND_FREEZE_DELAY;
    snd.deadline = level.time + snd.pauseMs;
    matchWon = (winner == TEAM_RED || winner == TEAM_BLUE) && level.teamScores[winner] + 1 >= snd.winLimit;
    if ( result == SND_ELIMINATION && snd.lastDeath[losing].killer >= 0 ) {
        death = snd.lastDeath[losing].time;
        level.replayEndTime = trap_ReplayStart(snd.lastDeath[losing].killer,
            death - KILLCAM_REWIND, death + KILLCAM_LINGER,
            death + (matchWon ? KILLCAM_MATCH_DELAY : KILLCAM_ROUND_DELAY));
        if ( level.replayEndTime ) snd.deadline = level.replayEndTime + SND_KILLCAM_RESTART;
    }
    snd.carrier = -1;
    if ( winner == TEAM_RED || winner == TEAM_BLUE ) level.teamScores[winner]++;
    for ( n = 0; n < level.maxclients; n++ ) SND_ClearUse(n);
    G_LogPrintf("SND round %i: winner %i, result %i, red %i blue %i\n", snd.round,
        winner, result, level.teamScores[TEAM_RED], level.teamScores[TEAM_BLUE]);
    trap_SendServerCommand(-1, va("cp \"%s\"", winner == TEAM_RED ? "Red wins the round" :
        (winner == TEAM_BLUE ? "Blue wins the round" : "Round drawn")));
    CalculateRanks();
    SND_Publish();
    if ( matchWon ) LogExit("Search and Destroy round limit reached.");
}

// MW2 decides an elimination inside the death: PlayerKilled_internal ->
// removeFromAliveCount -> updateGameEvents -> onDeadEvent -> sd_endGame, whose
// endGame makes the game postgame before anything else can happen
// (_damage.gsc:588, _gamelogic.gsc:2044-2053). So once the last of a team is
// down, nothing later in the same frame - a grenade going off, a missile
// landing, the rest of a blast - can hurt anyone (G_SNDCanDamage) or change
// the result: the first decisive death decides. Not after a detonation or
// defuse (onDeadEvent returns), nor once the deadline is due (it wins ties).
static void SND_CheckElimination( void ) {
    team_t defending = snd.attacking == TEAM_RED ? TEAM_BLUE : TEAM_RED;
    int attackers, defenders;
    if ( (snd.phase != SND_LIVE && snd.phase != SND_PLANTED) || snd.result != SND_NONE ||
         level.time >= snd.deadline || snd.practice ) return;
    attackers = SND_Count(snd.attacking,qtrue); defenders = SND_Count(defending,qtrue);
    if ( !attackers && !defenders && snd.phase == SND_LIVE ) SND_EndRound(TEAM_FREE,SND_DRAW,level.time);
    else if ( !defenders ) SND_EndRound(snd.attacking,SND_ELIMINATION,SND_EliminationTime(defending));
    else if ( !attackers && snd.phase == SND_LIVE ) SND_EndRound(defending,SND_ELIMINATION,SND_EliminationTime(snd.attacking));
}

// The site a carrier can plant at or the bomb a defender can defuse, in range
// and in view; SND_MAX_SITES is the bomb.
static int SND_UseTarget( gentity_t *ent, int *duration ) {
    int n = ent-g_entities, i;
    if ( g_gametype.integer != GT_SND || !SND_Alive(n) || snd.result != SND_NONE ||
         (snd.phase != SND_LIVE && snd.phase != SND_PLANTED) || level.time >= snd.deadline ) return -1;
    if ( snd.phase == SND_LIVE && snd.carrier == n ) {
        for ( i = 0; i < SND_MAX_SITES; i++ ) if ( snd.sites[i] &&
            SND_Near(ent, snd.sites[i]->r.currentOrigin, snd.sites[i]->wait) ) {
            *duration = snd.plantMs; return i;
        }
    } else if ( snd.phase == SND_PLANTED && ent->client->sess.sessionTeam != snd.attacking &&
                SND_Near(ent, snd.bombOrigin, 72) ) {
        *duration = snd.defuseMs; return SND_MAX_SITES;
    }
    return -1;
}

// Something for use or use/reload to use (BG_UpdateActivate).
qboolean G_SNDUsable( gentity_t *ent ) {
    int duration;
    return SND_UseTarget(ent, &duration) >= 0;
}

qboolean G_SNDUse( gentity_t *ent, usercmd_t *cmd ) {
    int n = ent-g_entities, target, duration = 0, elapsed, i;
    playerState_t *ps = &ent->client->ps;
    if ( g_gametype.integer != GT_SND ) return qfalse;
    if ( !SND_Alive(n) || (snd.phase != SND_LIVE && snd.phase != SND_PLANTED) ||
         level.time >= snd.deadline || !(cmd->buttons & (BUTTON_USE | BUTTON_USE_RELOAD)) ||
         (cmd->buttons & (BUTTON_ATTACK | BUTTON_RELOAD | BUTTON_SPRINT | BUTTON_ADS |
             BUTTON_MELEE | BUTTON_LETHAL | BUTTON_TACTICAL | BUTTON_ALTFIRE | BUTTON_TALK)) ||
         cmd->forwardmove || cmd->rightmove || cmd->upmove > 0 ||
         ps->groundEntityNum == ENTITYNUM_NONE || (ps->stats[STAT_LADDER] & LADDER_ATTACHED) ||
         ps->weaponstate != WEAPON_READY || (ps->weaponAction & WA_TYPE_MASK) ||
         VectorLengthSquared(ps->velocity) > 25*25 ) {
        SND_ClearUse(n); return qfalse;
    }
    target = SND_UseTarget(ent, &duration);
    if ( target < 0 ) { SND_ClearUse(n); return qfalse; }
    if ( snd.holdTarget[n] != target || level.time-snd.holdSeen[n] > 200 ) {
        snd.holdTarget[n] = target; snd.holdStart[n] = level.time;
    }
    snd.holdSeen[n] = level.time;
    elapsed = level.time-snd.holdStart[n];
    ps->stats[STAT_USE_TARGET] = target == SND_MAX_SITES ? SND_USE_DEFUSE : SND_USE_PLANT;
    ps->stats[STAT_USE_PROGRESS] = SND_Clamp(elapsed*1000/duration, 0, 1000);
    if ( elapsed < duration ) return qtrue;
    if ( target == SND_MAX_SITES ) {
        // sd.gsc bombDefused: the bomb stops and the defuse is spent; play
        // goes on, eliminations ignored, until the round ends 1.5s later.
        snd.result = SND_DEFUSED;
        snd.deadline = level.time + SND_DEFUSE_DELAY;
        for ( i = 0; i < level.maxclients; i++ ) SND_ClearUse(i);
        trap_SendServerCommand(-1, "cp \"Bomb defused\"");
        G_LogPrintf("SND defuse: %i\n", n);
        SND_Publish();
    } else {
        snd.phase = SND_PLANTED;
        snd.carrier = -1; snd.plantedSite = target;
        snd.deadline = level.time + snd.bombMs;
        SND_ShowBomb(snd.sites[target]->r.currentOrigin);
        for ( i = 0; i < level.maxclients; i++ ) SND_ClearUse(i);
        trap_SendServerCommand(-1, va("cp \"Bomb planted at %c\"", 'A'+target));
        G_LogPrintf("SND plant: %i site %i\n", n, target);
        SND_Publish();
    }
    return qtrue;
}

static qboolean SND_EnoughPlayers( void ) {
    if ( !snd.numSites ) return qfalse;
    return (SND_Count(TEAM_RED,qfalse) && SND_Count(TEAM_BLUE,qfalse)) ||
        (sndPractice.integer && g_cheats.integer &&
         (SND_Count(TEAM_RED,qfalse) || SND_Count(TEAM_BLUE,qfalse)));
}

static void SND_AssignCarrier( void ) {
    int n, candidate;
    snd.carrier = -1;
    for ( n = 0; n < level.maxclients; n++ ) {
        candidate = (n+snd.round-1)%level.maxclients;
        if ( SND_Alive(candidate) && level.clients[candidate].sess.sessionTeam == snd.attacking ) {
            snd.carrier = candidate; SND_HideBomb(); return;
        }
    }
}

static void SND_WaitForPlayers( void ) {
    int n;
    gentity_t *ent;
    trap_ReplayReset(qtrue);
    level.replayEndTime = 0;
    snd.phase = SND_WAITING; snd.carrier = -1; snd.deadline = 0;
    SND_HideBomb();
    // No Tactical Insertion outlives the round it was planted in (MW2's round
    // restart frees everything); SND_StartRound frees every missile.
    for ( n = MAX_CLIENTS; n < level.num_entities; n++ ) {
        ent = &g_entities[n];
        if ( ent->inuse && ent->s.eType == ET_MISSILE && ent->s.weapon == WP_TACTICAL_INSERTION ) G_FreeEntity(ent);
    }
    // Release chase cameras/freeze when a team empties between rounds.
    // Waiting players may explore, but cannot deal damage or plant a bomb.
    for ( n = 0; n < level.maxclients; n++ ) if ( SND_Player(n) ) trap_UnlinkEntity(&g_entities[n]);
    for ( n = 0; n < level.maxclients; n++ ) if ( SND_Player(n) ) ClientSpawn(&g_entities[n]);
}

static void SND_StartRound( void ) {
    int n;
    gentity_t *ent;
    trap_ReplayReset(qtrue);
    level.replayEndTime = 0;
    for ( n = 0; n < TEAM_NUM_TEAMS; n++ ) snd.lastDeath[n].killer = -1;
    snd.spawning = qtrue;
    snd.phase = SND_COUNTDOWN;
    snd.practice = sndPractice.integer && g_cheats.integer;
    snd.roundMs = SND_Clamp(sndRoundTime.integer,10,1800)*1000;
    snd.bombMs = SND_Clamp(sndBombTime.integer,5,300)*1000;
    snd.plantMs = SND_Clamp(sndPlantTime.integer,1,30)*1000;
    snd.defuseMs = SND_Clamp(sndDefuseTime.integer,1,30)*1000;
    snd.winLimit = SND_Clamp(sndWinLimit.integer,1,99);
    snd.switchRounds = SND_Clamp(sndSwitchRounds.integer,1,99);
    snd.pauseMs = SND_Clamp(sndRoundPause.integer,1,30)*1000;
    snd.round++;
    snd.attacking = ((snd.round-1)/snd.switchRounds)%2 ? TEAM_BLUE : TEAM_RED;
    snd.deadline = level.time + SND_Clamp(sndCountdown.integer,1,30)*1000;
    snd.result = SND_NONE; snd.winner = TEAM_FREE;
    snd.carrier = snd.plantedSite = -1;
    SND_HideBomb();
    // Remove previous-round projectiles/drops and corpses. Unlink everyone
    // before any spawn so a previous-round body cannot telefrag a new life.
    for ( n = 0; n < level.maxclients; n++ ) {
        snd.alive[n] = qfalse;
        SND_ClearUse(n);
        if ( SND_Player(n) ) trap_UnlinkEntity(&g_entities[n]);
    }
    for ( n = MAX_CLIENTS; n < level.num_entities; n++ ) {
        ent = &g_entities[n];
        if ( ent->inuse && (ent->s.eType == ET_MISSILE || (ent->flags & FL_DROPPED_ITEM)) ) G_FreeEntity(ent);
    }
    for ( n = 0; n < BODY_QUEUE_SIZE; n++ ) if ( level.bodyQue[n] ) trap_UnlinkEntity(level.bodyQue[n]);
    level.lastKillTime = 0;
    for ( n = 0; n < level.maxclients; n++ ) if ( SND_Player(n) ) ClientSpawn(&g_entities[n]);
    snd.spawning = qfalse;
    // Rotate the initial carrier among connected attackers; bomb is not a
    // third inventory weapon and never consumes a loadout slot.
    SND_AssignCarrier();
    // Solo defending practice still has a retrievable bomb, but planting
    // remains attackers-only. Switch to the attacking team next round to plant.
    if ( snd.carrier < 0 ) {
        for ( n = 0; n < SND_MAX_SITES; n++ ) if ( snd.sites[n] ) {
            SND_ShowBomb(snd.sites[n]->r.currentOrigin); break;
        }
    }
    CalculateRanks();
    SND_Publish();
}

void G_SNDRunFrame( void ) {
    int n;
    team_t defending;
    if ( g_gametype.integer != GT_SND || level.intermissiontime || level.intermissionQueued ) return;
    for ( n = 0; n < ARRAY_LEN(sndCvars); n++ ) trap_Cvar_Update(sndCvars[n].value);
    for ( n = 0; n < level.maxclients; n++ ) {
        if ( snd.holdTarget[n] >= 0 && level.time-snd.holdSeen[n] > 200 ) SND_ClearUse(n);
    }
    if ( snd.phase == SND_WAITING ) {
        if ( SND_EnoughPlayers() ) SND_StartRound();
    } else if ( snd.phase == SND_COUNTDOWN ) {
        if ( !SND_EnoughPlayers() ) {
            snd.round--; SND_WaitForPlayers();
        } else if ( level.time >= snd.deadline ) {
            if ( !SND_Alive(snd.carrier) ||
                 level.clients[snd.carrier].sess.sessionTeam != snd.attacking ) SND_AssignCarrier();
            snd.phase = SND_LIVE; snd.deadline = level.time + snd.roundMs;
            trap_SendServerCommand(-1, "cp \"Search and Destroy\"");
        }
    } else if ( snd.phase == SND_ROUND_OVER ) {
        if ( level.time >= snd.deadline ) {
            if ( SND_EnoughPlayers() ) SND_StartRound();
            else SND_WaitForPlayers();
        }
    } else {
        defending = snd.attacking == TEAM_RED ? TEAM_BLUE : TEAM_RED;
        // Deadline wins ties with late input. A completed plant replaces the
        // round clock; killing the last attacker cannot cancel a planted bomb.
        if ( level.time >= snd.deadline ) {
            if ( snd.result == SND_EXPLODED ) SND_EndRound(snd.attacking, SND_EXPLODED, level.time);
            else if ( snd.result == SND_DEFUSED ) SND_EndRound(defending, SND_DEFUSED, level.time);
            else if ( snd.phase == SND_PLANTED ) {
                // sd.gsc bombPlanted: the bomb goes off and every objective
                // is disabled; the round ends 3s later, play going on with
                // eliminations ignored (onDeadEvent returns).
                gentity_t *effect = G_TempEntity(snd.bombOrigin, EV_MISSILE_MISS);
                effect->s.weapon = WP_ROCKET_LAUNCHER;
                effect->s.eventParm = DirToByte(tv(0,0,1));
                SND_HideBomb();
                snd.result = SND_EXPLODED;
                snd.deadline = level.time + SND_DETONATION_DELAY;
                for ( n = 0; n < level.maxclients; n++ ) SND_ClearUse(n);
                G_LogPrintf("SND detonation: site %i\n", snd.plantedSite);
            } else SND_EndRound(defending, SND_TIME, level.time);
        } else if ( snd.result == SND_NONE ) {
            if ( snd.phase == SND_LIVE && snd.carrier >= 0 && !SND_Alive(snd.carrier) ) {
                SND_DropBomb(&g_entities[snd.carrier]);
            }
            if ( snd.phase == SND_LIVE && snd.carrier < 0 ) {
                for ( n = 0; n < level.maxclients; n++ ) if ( SND_Alive(n) &&
                    level.clients[n].sess.sessionTeam == snd.attacking &&
                    SND_Near(&g_entities[n], snd.bombOrigin, 48) ) {
                    snd.carrier = n; SND_HideBomb();
                    trap_SendServerCommand(n, "cp \"You have the bomb\""); break;
                }
            }
            // A team emptied without a death: a disconnect or a team change.
            SND_CheckElimination();
        }
    }
    SND_Publish();
}
