// Copyright (C) 1999-2005 Id Software, Inc.
//
// g_round.c -- round-based match flow for Search & Destroy (GT_SD).
//
// Unlike the deathmatch gametypes, GT_SD plays out as a sequence of rounds.
// At the start of each round every player is respawned and damage is disabled
// for a short "pre-round" setup phase. Once combat goes live a player who dies
// stays dead until the round ends -- there is no mid-round respawn. A round is
// won by eliminating the opposing team (or, until objective play exists, by
// having more survivors when the round timer expires). The first team to
// g_roundlimit rounds wins the match.
//
// teamScores[TEAM_RED]/[TEAM_BLUE] are reused to hold rounds won, so the
// existing team scoreboard reports the match score with no extra plumbing.

#include "g_local.h"

// How long the post-round result is shown before the next round begins.
#define	POSTROUND_TIME		4000

/*
==================
G_RoundBasedGametype

True for gametypes that use the round / no-respawn flow in this file.
==================
*/
qboolean G_RoundBasedGametype( void ) {
	return ( g_gametype.integer == GT_SD );
}

/*
==================
G_CountAliveOnTeam

Number of connected players on the given team that are currently alive.
==================
*/
int G_CountAliveOnTeam( team_t team ) {
	int			i;
	int			count = 0;
	gclient_t	*cl;

	for ( i = 0; i < level.maxclients; i++ ) {
		cl = &level.clients[i];
		if ( cl->pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( cl->sess.sessionTeam != team ) {
			continue;
		}
		if ( cl->ps.stats[STAT_HEALTH] <= 0 ) {
			continue;
		}
		count++;
	}

	return count;
}

static void G_SDUpdateConfigstring( void );

/*
==================
G_AttackingTeam / G_DefendingTeam

Which side carries and plants the bomb, and which defends. Red attacks the
first round; the sides swap every g_roundswitch rounds (G_StartRound).

Roles are what spawn points follow, not team colour: team_CTF_redspawn are the
ATTACKER spawns and team_CTF_bluespawn the DEFENDER spawns, whoever that is
this round. Otherwise a side switch would leave the bomb sites on the attackers'
side of the map.
==================
*/
team_t G_AttackingTeam( void ) {
	return ( level.attackingTeam == TEAM_BLUE ) ? TEAM_BLUE : TEAM_RED;
}

team_t G_DefendingTeam( void ) {
	return ( G_AttackingTeam() == TEAM_RED ) ? TEAM_BLUE : TEAM_RED;
}

/*
==================
G_AddBombSite

Register a site. Labels run A, B, C... in the order the map spawns them, unless
the entity sets "site".
==================
*/
static int G_AddBombSite( const vec3_t origin, float radius, char label, gentity_t *trigger ) {
	bombSite_t	*site;

	if ( level.numBombSites >= MAX_BOMBSITES ) {
		G_Printf( S_COLOR_YELLOW "WARNING: more than %i bomb sites, ignoring the rest\n", MAX_BOMBSITES );
		return -1;
	}
	site = &level.bombSites[ level.numBombSites ];
	VectorCopy( origin, site->origin );
	site->radius = radius;
	site->label = label ? label : (char)( 'A' + level.numBombSites );
	site->trigger = trigger;
	return level.numBombSites++;
}

/*
==================
G_PointSiteAt

Index of the point site containing pos, or -1. Brush sites are not tested here:
their trigger volume reports itself through bombsite_touch.
==================
*/
static int G_PointSiteAt( const vec3_t pos ) {
	int		i;
	vec3_t	delta;

	for ( i = 0; i < level.numBombSites; i++ ) {
		if ( level.bombSites[i].radius <= 0 ) {
			continue;
		}
		VectorSubtract( pos, level.bombSites[i].origin, delta );
		if ( VectorLength( delta ) <= level.bombSites[i].radius ) {
			return i;
		}
	}
	return -1;
}

/*
==================
G_CollectSpawns

Gather every entity of a classname, up to max.
==================
*/
static int G_CollectSpawns( const char *classname, gentity_t **out, int max ) {
	gentity_t	*spot = NULL;
	int			count = 0;

	while ( count < max && ( spot = G_Find( spot, FOFS(classname), classname ) ) != NULL ) {
		out[count++] = spot;
	}
	return count;
}

/*
==================
G_FurthestPair

The two entities furthest apart. Used to find the two ends of a map that has no
team spawns, and to spread fallback bomb sites as far apart as the map allows.
==================
*/
static qboolean G_FurthestPair( gentity_t **spots, int count, int *a, int *b ) {
	int		i, j;
	float	best = -1, d;
	vec3_t	delta;

	if ( count < 2 ) {
		return qfalse;
	}
	for ( i = 0; i < count; i++ ) {
		for ( j = i + 1; j < count; j++ ) {
			VectorSubtract( spots[i]->s.origin, spots[j]->s.origin, delta );
			d = VectorLength( delta );
			if ( d > best ) {
				best = d;
				*a = i;
				*b = j;
			}
		}
	}
	return qtrue;
}

/*
==================
G_OnDefenderSide

For a map split from its deathmatch spawns: is this point nearer the defenders'
end than the attackers'?
==================
*/
static qboolean G_OnDefenderSide( const vec3_t pos ) {
	vec3_t	toDef, toAtt;

	VectorSubtract( pos, level.sdDefenderEnd, toDef );
	VectorSubtract( pos, level.sdAttackerEnd, toAtt );
	return VectorLength( toDef ) < VectorLength( toAtt );
}

/*
==================
G_SDResolveLayout

Work out, once per map, where the two sides spawn and where the bomb sites are.
Runs lazily because spawns can be picked before the first round starts.

A map built for S&D has team_CTF_red/bluespawn and trigger_bombsite and is used
as authored. Anything less is filled in rather than refused:
  - no team spawns: the deathmatch spawns are split into two ends of the map,
    using the two that lie furthest apart as the anchors
  - no bomb sites: up to two point sites are placed on the defenders' spawns,
    as far apart as possible
so S&D runs on any map with spawn points. Authored sites are always better.
==================
*/
static void G_SDResolveLayout( void ) {
	gentity_t	*spots[64];
	gentity_t	*defenders[64];
	int			count, numDef, i, a, b;

	if ( level.sdLayoutDone ) {
		return;
	}
	level.sdLayoutDone = qtrue;
	if ( level.attackingTeam != TEAM_RED && level.attackingTeam != TEAM_BLUE ) {
		level.attackingTeam = TEAM_RED;
	}

	// --- sides ---
	numDef = G_CollectSpawns( "team_CTF_bluespawn", defenders, 64 );
	count = G_CollectSpawns( "team_CTF_redspawn", spots, 64 );
	if ( numDef == 0 && count == 0 ) {
		count = G_CollectSpawns( "info_player_deathmatch", spots, 64 );
		if ( G_FurthestPair( spots, count, &a, &b ) ) {
			level.sdSidesFromDeathmatch = qtrue;
			VectorCopy( spots[a]->s.origin, level.sdAttackerEnd );
			VectorCopy( spots[b]->s.origin, level.sdDefenderEnd );
			numDef = 0;
			for ( i = 0; i < count; i++ ) {
				if ( G_OnDefenderSide( spots[i]->s.origin ) ) {
					defenders[numDef++] = spots[i];
				}
			}
			G_Printf( "S&D: no team spawns - split %i deathmatch spawns into two sides\n", count );
		} else if ( count == 1 ) {
			defenders[0] = spots[0];
			numDef = 1;
			G_Printf( S_COLOR_YELLOW "S&D: only one spawn point - both teams will share it\n" );
		}
	}

	// --- sites ---
	if ( level.numBombSites > 0 ) {
		G_Printf( "S&D: %i bomb site%s from the map\n", level.numBombSites,
			level.numBombSites == 1 ? "" : "s" );
		return;
	}
	if ( G_FurthestPair( defenders, numDef, &a, &b ) ) {
		G_AddBombSite( defenders[a]->s.origin, DEFAULT_SITE_RADIUS, 'A', NULL );
		G_AddBombSite( defenders[b]->s.origin, DEFAULT_SITE_RADIUS, 'B', NULL );
	} else if ( numDef == 1 ) {
		G_AddBombSite( defenders[0]->s.origin, DEFAULT_SITE_RADIUS, 'A', NULL );
	}
	if ( level.numBombSites ) {
		G_Printf( "S&D: map has no trigger_bombsite - placed %i site%s on the defenders' spawns\n",
			level.numBombSites, level.numBombSites == 1 ? "" : "s" );
	} else {
		G_Printf( S_COLOR_YELLOW "S&D: no bomb sites and no spawns to place them on - rounds are elimination only\n" );
	}
}

/*
==================
G_InitRoundGame

Map load, after every entity has spawned. Resolve the layout and publish the
initial (no round yet) state, so a HUD connecting before the first round still
reads a valid configstring.
==================
*/
void G_InitRoundGame( void ) {
	if ( !G_RoundBasedGametype() ) {
		trap_SetConfigstring( CS_SD_STATE, "" );
		return;
	}
	G_SDResolveLayout();
	level.bombSite = -1;
	level.bombCarrier = -1;
	level.defuser = -1;
	level.roundWinner = -1;
	G_SDUpdateConfigstring();
}

/*
==================
G_SelectSDSpawnPoint

Spawn by ROLE. Attackers use the attacker spawns and defenders the defender
spawns, whichever colour each side is this round.
==================
*/
gentity_t *G_SelectSDSpawnPoint( gentity_t *ent, vec3_t origin, vec3_t angles ) {
	gentity_t	*spots[64];
	gentity_t	*side[64];
	gentity_t	*spot;
	qboolean	attacker;
	int			count, n, i;

	G_SDResolveLayout();
	attacker = ( ent->client->sess.sessionTeam == G_AttackingTeam() );

	if ( !level.sdSidesFromDeathmatch ) {
		return SelectCTFSpawnPoint( attacker ? TEAM_RED : TEAM_BLUE,
			ent->client->pers.teamState.state, origin, angles,
			!!( ent->r.svFlags & SVF_BOT ) );
	}

	count = G_CollectSpawns( "info_player_deathmatch", spots, 64 );
	n = 0;
	for ( i = 0; i < count; i++ ) {
		if ( G_OnDefenderSide( spots[i]->s.origin ) == !attacker && !SpotWouldTelefrag( spots[i] ) ) {
			side[n++] = spots[i];
		}
	}
	if ( !n ) {
		return SelectSpawnPoint( vec3_origin, origin, angles, !!( ent->r.svFlags & SVF_BOT ) );
	}
	spot = side[ rand() % n ];
	VectorCopy( spot->s.origin, origin );
	origin[2] += 9;
	VectorCopy( spot->s.angles, angles );
	return spot;
}

/*
==================
G_GiveBomb

Make the given attacker the bomb carrier.
==================
*/
static void G_GiveBomb( gentity_t *ent ) {
	if ( !ent || !ent->client ) {
		return;
	}
	level.bombCarrier = ent - g_entities;
	level.bombState = BOMB_CARRIED;
	trap_SendServerCommand( level.bombCarrier,
		"cp \"You have the bomb!\nReach a site and hold USE to plant.\"" );
}

/*
==================
G_AssignBombCarrier

Give the bomb to a random living member of the attacking team. Leaves the
carrier unset if the attackers have nobody alive to take it.
==================
*/
static void G_AssignBombCarrier( void ) {
	int			i;
	int			candidates[MAX_CLIENTS];
	int			count = 0;
	team_t		attackers = G_AttackingTeam();
	gclient_t	*cl;

	level.bombCarrier = -1;

	for ( i = 0; i < level.maxclients; i++ ) {
		cl = &level.clients[i];
		if ( cl->pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( cl->sess.sessionTeam != attackers ) {
			continue;
		}
		if ( cl->ps.stats[STAT_HEALTH] <= 0 ) {
			continue;
		}
		candidates[count++] = i;
	}

	if ( count > 0 ) {
		int pick = (int)( random() * count ) % count;
		G_GiveBomb( &g_entities[ candidates[pick] ] );
	}
}

/*
==================
G_BombCarrierDied

Called from player_die. If the dead player was carrying the bomb, hand it to
another living attacker. If none remain the round is left to be decided by
elimination or the round timer.
==================
*/
void G_BombCarrierDied( gentity_t *self ) {
	if ( level.bombState != BOMB_CARRIED ) {
		return;		// not in the carried state (unplanted), nothing to pass
	}
	if ( ( self - g_entities ) != level.bombCarrier ) {
		return;		// the dead player wasn't holding it
	}

	level.plantProgress = 0;
	G_AssignBombCarrier();
	if ( level.bombCarrier == -1 ) {
		trap_SendServerCommand( -1, "cp \"The bomb carrier is down!\"" );
	} else {
		trap_SendServerCommand( -1, "print \"The bomb has been picked up.\n\"" );
	}
}

/*
==================
G_RoundRespawnAll

Bring every playing client back to a fresh team spawn point, alive and at full
health, ready for a new round. Spectators are left alone.
==================
*/
static void G_RoundRespawnAll( void ) {
	int			i;
	gentity_t	*ent;

	for ( i = 0; i < level.maxclients; i++ ) {
		ent = &g_entities[i];
		if ( !ent->inuse || !ent->client ) {
			continue;
		}
		if ( ent->client->pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( ent->client->sess.sessionTeam != TEAM_RED &&
			 ent->client->sess.sessionTeam != TEAM_BLUE ) {
			continue;
		}
		// ClientSpawn handles the dead and the living alike: it relocates the
		// player to a spawn point and resets health, armor and weapon state.
		level.sdRoundRespawn = qtrue;
		ClientSpawn( ent );
		level.sdRoundRespawn = qfalse;
	}
}

/*
==================
G_ClearRoundWorld

A new round starts on a clean map, as in CoD: nothing thrown or fired last
round survives into this one, dropped weapons are gone, and the dead are
cleared away. Runs BEFORE anyone respawns, so a grenade cooked at the end of
the previous round cannot land on a player who has just been placed.
==================
*/
static void G_ClearRoundWorld( void ) {
	int			i;
	gentity_t	*e;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		e = &g_entities[i];
		if ( !e->inuse ) {
			continue;
		}
		if ( e->s.eType == ET_MISSILE || ( e->flags & FL_DROPPED_ITEM ) ) {
			G_FreeEntity( e );
		}
	}
	// body queue entries are never freed, only unlinked (see BodySink)
	for ( i = 0; i < BODY_QUEUE_SIZE; i++ ) {
		e = level.bodyQue[i];
		if ( !e ) {
			continue;
		}
		trap_UnlinkEntity( e );
		e->physicsObject = qfalse;
		e->think = NULL;
		e->nextthink = 0;
	}
}

/*
==================
G_OnFoot

Planting and defusing are done standing on something. Mid-air, falling, or
hanging on a ladder (which takes you off the ground) does not count.
==================
*/
static qboolean G_OnFoot( gentity_t *ent ) {
	return ent->client && ent->client->ps.groundEntityNum != ENTITYNUM_NONE;
}

/*
==================
G_StartRound

Begin a new round: advance the round counter, respawn everyone and enter the
frozen pre-round phase. Combat goes live once the pre-round timer elapses.
==================
*/
static void G_StartRound( void ) {
	int		warmupMsec;
	int		played;
	qboolean	switched = qfalse;

	G_SDResolveLayout();

	level.roundNumber++;
	level.roundState = RND_PREROUND;
	level.roundWinner = -1;
	level.roundEndTime = 0;

	// Swap sides every g_roundswitch rounds PLAYED. Before the respawn, so
	// everyone is placed by their new role, and before the bomb is handed out,
	// so it goes to the new attackers. Scores stay with the team, not the side.
	played = level.roundNumber - 1;
	if ( g_roundswitch.integer > 0 && played > 0 && ( played % g_roundswitch.integer ) == 0 ) {
		level.attackingTeam = ( G_AttackingTeam() == TEAM_RED ) ? TEAM_BLUE : TEAM_RED;
		switched = qtrue;
	}

	warmupMsec = ( g_roundwarmup.integer > 0 ) ? g_roundwarmup.integer * 1000 : 1000;
	level.roundStateEndTime = level.time + warmupMsec;

	G_ClearRoundWorld();
	G_RoundRespawnAll();

	// reset the bomb and hand it to a fresh attacker for the new round
	level.bombState = BOMB_NONE;
	level.bombCarrier = -1;
	level.bombDetonateTime = 0;
	level.plantProgress = 0;
	level.plantTouchTime = 0;
	level.plantTouchSite = -1;
	level.defuseProgress = 0;
	level.defuser = -1;
	level.bombSite = -1;
	G_AssignBombCarrier();

	G_LogPrintf( "SD_RoundStart: %i %s%s\n", level.roundNumber,
		G_AttackingTeam() == TEAM_RED ? "red" : "blue", switched ? " switched" : "" );
	trap_SendServerCommand( -1, va( "cp \"Round %i%s\n%s attack - get ready...\"",
		level.roundNumber, switched ? "  -  Switching sides" : "",
		G_AttackingTeam() == TEAM_RED ? "Red" : "Blue" ) );
}

/*
==================
G_RoundWin

Award the decided round and enter the post-round phase. Pass TEAM_SPECTATOR for
a draw (no team is credited). The match-end check lives in CheckExitRules, which
reads teamScores against g_roundlimit.
==================
*/
static void G_RoundWin( team_t winner, const char *reason ) {
	if ( winner == TEAM_RED || winner == TEAM_BLUE ) {
		level.teamScores[winner]++;
	}
	level.roundWinner = winner;
	level.roundState = RND_POSTROUND;
	level.roundStateEndTime = level.time + POSTROUND_TIME;

	G_LogPrintf( "SD_RoundWin: %s %i %i: %s\n",
		winner == TEAM_RED ? "red" : winner == TEAM_BLUE ? "blue" : "draw",
		level.teamScores[TEAM_RED], level.teamScores[TEAM_BLUE], reason );

	trap_SendServerCommand( -1, va( "print \"%s\n\"", reason ) );
	trap_SendServerCommand( -1, va( "cp \"%s\nRed %i  -  Blue %i\"", reason,
		level.teamScores[TEAM_RED], level.teamScores[TEAM_BLUE] ) );
}

/*
==================
G_BombPlanted

The carrier finished planting at a site: drop the bomb in place and start the
detonation countdown. The carrier is freed to fight; the round is now settled
by detonation versus defuse.
==================
*/
static void G_BombPlanted( gentity_t *carrier, gentity_t *site ) {
	int	timerMsec = ( g_bombtimer.integer > 0 ) ? g_bombtimer.integer * 1000 : 40000;

	level.bombState = BOMB_PLANTED;
	level.bombSite = level.plantTouchSite;
	G_LogPrintf( "SD_Plant: %i %c\n", (int)( carrier - g_entities ),
		level.bombSite >= 0 ? level.bombSites[ level.bombSite ].label : '?' );
	VectorCopy( carrier->r.currentOrigin, level.bombOrigin );
	level.bombDetonateTime = level.time + timerMsec;
	level.bombCarrier = -1;
	level.plantProgress = 0;
	level.defuseProgress = 0;

	trap_SendServerCommand( -1, va( "cp \"The bomb has been planted%s%s!\"",
		level.bombSite >= 0 ? " at " : "",
		level.bombSite >= 0 ? va( "%c", level.bombSites[ level.bombSite ].label ) : "" ) );
	// a planted-bomb world entity (model + beep + the eventual explosion
	// effect) is part of the upcoming cgame stage.
}

/*
==================
G_RunBomb

Advance the planted-bomb timer and any defuse in progress; called every active
frame. Planting progress itself is accumulated in bombsite_touch. May award the
round (defuse -> defenders, detonation -> attackers).
==================
*/
static void G_RunBomb( void ) {
	int			i;
	gclient_t	*cl;
	team_t		defenders = G_DefendingTeam();
	qboolean	defusing = qfalse;
	int			defuseMsec;

	if ( level.bombState == BOMB_CARRIED ) {
		// bombsite_touch stamps plantTouchTime while the carrier overlaps a
		// site. ClientThink and G_RunFrame are separate VM calls with different
		// level.time values, so treat a touch within the last fraction of a
		// second as "still in the site" rather than requiring an exact match.
		gentity_t	*carrier;
		int			plantMsec;
		qboolean	inSite;

		if ( level.bombCarrier < 0 ) {
			level.plantProgress = 0;
			return;
		}

		carrier = &g_entities[ level.bombCarrier ];

		// A point site reports itself the same way a brush trigger does: by
		// stamping plantTouchTime. Everything below is shared, so there is one
		// plant path no matter how the map defines its sites.
		{
			int site = G_PointSiteAt( carrier->r.currentOrigin );
			if ( site >= 0 ) {
				level.plantTouchTime = level.time;
				level.plantTouchSite = site;
			}
		}
		inSite = ( level.plantTouchTime != 0 && ( level.time - level.plantTouchTime ) <= 250 );

		if ( inSite && carrier->client &&
			carrier->client->ps.stats[STAT_HEALTH] > 0 && G_OnFoot( carrier ) &&
			( carrier->client->buttons & BUTTON_USE ) ) {
			level.plantProgress += ( level.time - level.previousTime );
			plantMsec = ( g_bombplanttime.integer > 0 ) ? g_bombplanttime.integer * 1000 : 4000;
			if ( level.plantProgress >= plantMsec ) {
				G_BombPlanted( carrier, NULL );
			}
		} else {
			level.plantProgress = 0;
		}
		return;
	}

	if ( level.bombState != BOMB_PLANTED ) {
		return;
	}

	// detonation: attackers win if the countdown expires before a defuse
	if ( level.time >= level.bombDetonateTime ) {
		level.bombState = BOMB_DETONATED;
		G_LogPrintf( "SD_Detonate\n" );
		G_RoundWin( G_AttackingTeam(), "Bomb detonated" );
		return;
	}

	// defuse: any living defender within reach, holding USE, makes progress
	level.defuser = -1;
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t	*ent = &g_entities[i];
		vec3_t		delta;

		cl = &level.clients[i];
		if ( cl->pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( cl->sess.sessionTeam != defenders ) {
			continue;
		}
		if ( cl->ps.stats[STAT_HEALTH] <= 0 ) {
			continue;
		}
		if ( !( cl->buttons & BUTTON_USE ) || !G_OnFoot( ent ) ) {
			continue;
		}
		VectorSubtract( ent->r.currentOrigin, level.bombOrigin, delta );
		if ( VectorLength( delta ) <= (float)g_bombradius.integer ) {
			defusing = qtrue;
			level.defuser = i;
			break;
		}
	}

	defuseMsec = ( g_bombdefusetime.integer > 0 ) ? g_bombdefusetime.integer * 1000 : 7000;
	if ( defusing ) {
		level.defuseProgress += ( level.time - level.previousTime );
		if ( level.defuseProgress >= defuseMsec ) {
			level.bombState = BOMB_DEFUSED;
			G_LogPrintf( "SD_Defuse: %i\n", level.defuser );
			G_RoundWin( defenders, "Bomb defused" );
		}
	} else {
		level.defuseProgress = 0;
	}
}

/*
==================
bombsite_touch

Runs each frame an entity overlaps a bomb site. Records that the attacking
bomb carrier is currently in a site; G_RunBomb turns that into plant progress.
==================
*/
static void bombsite_touch( gentity_t *self, gentity_t *other, trace_t *trace ) {
	if ( !other->client ) {
		return;
	}
	if ( level.roundState != RND_ACTIVE || level.bombState != BOMB_CARRIED ) {
		return;
	}
	if ( ( other - g_entities ) != level.bombCarrier ) {
		return;		// only the carrier can plant
	}

	// just record that the carrier is standing in a site this instant;
	// G_RunBomb accumulates the actual plant progress (see the note there).
	level.plantTouchTime = level.time;
	level.plantTouchSite = self->count;
}

/*QUAKED trigger_bombsite (.5 .5 .5) ?
Search & destroy plant zone. The attacking bomb carrier plants here by
standing inside and holding USE. Place one or more per map (e.g. A and B).

Two forms:
  brush - the volume is the site. The usual Radiant way.
  point - no brush, just an origin; "radius" sets the reach (default 128).
          For maps built without brush entities, like the generated ones.
"site" names it ("A", "B"); otherwise sites are lettered in spawn order.
*/
void SP_trigger_bombsite( gentity_t *ent ) {
	char	*label;
	float	radius;
	int		index;
	vec3_t	center;

	G_SpawnString( "site", "", &label );

	if ( ent->model && ent->model[0] == '*' ) {
		InitTrigger( ent );
		VectorAdd( ent->r.absmin, ent->r.absmax, center );
		VectorScale( center, 0.5f, center );
		index = G_AddBombSite( center, 0, label[0], ent );
		ent->count = index;
		ent->touch = bombsite_touch;
		trap_LinkEntity( ent );
		return;
	}

	G_SpawnFloat( "radius", va( "%i", DEFAULT_SITE_RADIUS ), &radius );
	if ( radius < 16 ) {
		radius = 16;
	}
	G_AddBombSite( ent->s.origin, radius, label[0], NULL );
	G_FreeEntity( ent );		// a point site is pure data; it needs no entity
}

/*
==================
G_OnObjective

Is this player holding USE ON the objective right now - the carrier in a site,
or a defender within reach of a planted bomb? The objective owns USE: weapon
pickup checks this first so it can never steal the button mid-plant.
==================
*/
qboolean G_OnObjective( gentity_t *ent ) {
	vec3_t	delta;

	if ( !G_RoundBasedGametype() || level.roundState != RND_ACTIVE || !G_OnFoot( ent ) ) {
		return qfalse;
	}
	if ( level.bombState == BOMB_CARRIED && ( ent - g_entities ) == level.bombCarrier ) {
		return ( G_PointSiteAt( ent->r.currentOrigin ) >= 0 ||
		         ( level.plantTouchTime && level.time - level.plantTouchTime <= 250 ) );
	}
	if ( level.bombState == BOMB_PLANTED && ent->client->sess.sessionTeam == G_DefendingTeam() ) {
		VectorSubtract( ent->r.currentOrigin, level.bombOrigin, delta );
		return VectorLength( delta ) <= (float)g_bombradius.integer;
	}
	return qfalse;
}

/*
==================
G_SDUpdateConfigstring

Publish round and bomb state for the HUD. Called every frame, but it only goes
out on the wire when something changes - the engine drops an identical
configstring - and nothing in it changes per frame. Progress is sent as a START
time and a duration, not a fraction: while a plant continues, level.time minus
progress stays constant, so a plant in progress costs no bandwidth at all.
==================
*/
static void G_SDUpdateConfigstring( void ) {
	char	info[MAX_INFO_STRING];
	int		kind = SDACT_NONE, actor = -1, start = 0, duration = 0;

	if ( level.bombState == BOMB_CARRIED && level.plantProgress > 0 ) {
		kind = SDACT_PLANT;
		actor = level.bombCarrier;
		start = level.time - level.plantProgress;
		duration = ( g_bombplanttime.integer > 0 ) ? g_bombplanttime.integer * 1000 : 4000;
	} else if ( level.bombState == BOMB_PLANTED && level.defuseProgress > 0 ) {
		kind = SDACT_DEFUSE;
		actor = level.defuser;
		start = level.time - level.defuseProgress;
		duration = ( g_bombdefusetime.integer > 0 ) ? g_bombdefusetime.integer * 1000 : 7000;
	}

	info[0] = 0;
	Info_SetValueForKey( info, "st", va( "%i", level.roundState ) );
	Info_SetValueForKey( info, "n", va( "%i", level.roundNumber ) );
	Info_SetValueForKey( info, "att", va( "%i", G_AttackingTeam() ) );
	Info_SetValueForKey( info, "se", va( "%i", level.roundStateEndTime ) );
	Info_SetValueForKey( info, "re", va( "%i", level.roundEndTime ) );
	Info_SetValueForKey( info, "w", va( "%i", level.roundWinner ) );
	Info_SetValueForKey( info, "bs", va( "%i", level.bombState ) );
	Info_SetValueForKey( info, "bc", va( "%i", level.bombCarrier ) );
	Info_SetValueForKey( info, "bt", va( "%i", level.bombDetonateTime ) );
	Info_SetValueForKey( info, "site", level.bombSite >= 0
		? va( "%c", level.bombSites[ level.bombSite ].label ) : "" );
	Info_SetValueForKey( info, "ak", va( "%i", kind ) );
	Info_SetValueForKey( info, "aa", va( "%i", actor ) );
	Info_SetValueForKey( info, "as", va( "%i", start ) );
	Info_SetValueForKey( info, "ad", va( "%i", duration ) );
	trap_SetConfigstring( CS_SD_STATE, info );
}

/*
==================
CheckRound

Drives the Search & Destroy round state machine; called once per frame from
G_RunFrame, after CheckTournament (so warmup has been resolved) and before
CheckExitRules (so an awarded round is seen by the match-end check this frame).
==================
*/
static void CheckRoundState( void );

void CheckRound( void ) {
	if ( !G_RoundBasedGametype() ) {
		return;
	}
	CheckRoundState();
	G_SDUpdateConfigstring();
}

static void CheckRoundState( void ) {
	int	aliveAtt, aliveDef;

	// no rounds during global warmup or once intermission is pending
	if ( level.warmupTime != 0 || level.intermissionQueued || level.intermissiontime ) {
		level.roundState = RND_NONE;
		return;
	}

	// both teams must be populated for a round to mean anything
	if ( TeamCount( -1, TEAM_RED ) == 0 || TeamCount( -1, TEAM_BLUE ) == 0 ) {
		level.roundState = RND_NONE;
		return;
	}

	switch ( level.roundState ) {
	case RND_NONE:
		// match is live and both teams are staffed: kick off the first round
		G_StartRound();
		break;

	case RND_PREROUND:
		if ( level.time >= level.roundStateEndTime ) {
			level.roundState = RND_ACTIVE;
			level.roundEndTime = ( g_roundtime.integer > 0 )
				? level.time + g_roundtime.integer * 1000 : 0;
			trap_SendServerCommand( -1, "cp \"FIGHT!\"" );
		}
		break;

	case RND_ACTIVE:
		// advance plant / defuse / detonation first: it may decide the round
		G_RunBomb();
		if ( level.roundState != RND_ACTIVE ) {
			break;		// a defuse or detonation already awarded the round
		}

		aliveAtt = G_CountAliveOnTeam( G_AttackingTeam() );
		aliveDef = G_CountAliveOnTeam( G_DefendingTeam() );

		if ( level.bombState == BOMB_PLANTED ) {
			// once planted, the bomb timer decides the round (see G_RunBomb).
			// the lone exception: wiping the defenders leaves nobody to defuse,
			// so the attackers have effectively won.
			if ( aliveDef == 0 ) {
				G_RoundWin( G_AttackingTeam(), "Defenders eliminated" );
			}
		} else {
			// bomb still in hand: elimination or the round timer decides it
			if ( aliveAtt == 0 ) {
				G_RoundWin( G_DefendingTeam(), "Attackers eliminated" );
			} else if ( aliveDef == 0 ) {
				G_RoundWin( G_AttackingTeam(), "Defenders eliminated" );
			} else if ( level.roundEndTime && level.time >= level.roundEndTime ) {
				G_RoundWin( G_DefendingTeam(), "Time up - bomb not planted" );
			}
		}
		break;

	case RND_POSTROUND:
		if ( level.time >= level.roundStateEndTime ) {
			// CheckExitRules ends the match once a team reaches g_roundlimit;
			// only roll into the next round while the match is still going.
			if ( !level.intermissionQueued && !level.intermissiontime ) {
				G_StartRound();
			}
		}
		break;
	}
}
