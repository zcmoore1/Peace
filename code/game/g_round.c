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

/*
==================
G_AttackingTeam / G_DefendingTeam

Which side carries and plants the bomb, and which defends. Fixed for now
(red attacks, blue defends); a halftime side-switch will swap these later.
==================
*/
team_t G_AttackingTeam( void ) {
	return TEAM_RED;
}

team_t G_DefendingTeam( void ) {
	return TEAM_BLUE;
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
		ClientSpawn( ent );
	}
}

/*
==================
G_StartRound

Begin a new round: advance the round counter, respawn everyone and enter the
frozen pre-round phase. Combat goes live once the pre-round timer elapses.
==================
*/
static void G_StartRound( void ) {
	int	warmupMsec;

	level.roundNumber++;
	level.roundState = RND_PREROUND;
	level.roundWinner = -1;
	level.roundEndTime = 0;

	warmupMsec = ( g_roundwarmup.integer > 0 ) ? g_roundwarmup.integer * 1000 : 1000;
	level.roundStateEndTime = level.time + warmupMsec;

	G_RoundRespawnAll();

	// reset the bomb and hand it to a fresh attacker for the new round
	level.bombState = BOMB_NONE;
	level.bombCarrier = -1;
	level.bombDetonateTime = 0;
	level.plantProgress = 0;
	level.plantTouchTime = 0;
	level.defuseProgress = 0;
	G_AssignBombCarrier();

	trap_SendServerCommand( -1, va( "cp \"Round %i\nGet ready...\"", level.roundNumber ) );
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
	VectorCopy( carrier->r.currentOrigin, level.bombOrigin );
	level.bombDetonateTime = level.time + timerMsec;
	level.bombCarrier = -1;
	level.plantProgress = 0;
	level.defuseProgress = 0;

	trap_SendServerCommand( -1, "cp \"The bomb has been planted!\"" );
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
		inSite = ( level.plantTouchTime != 0 && ( level.time - level.plantTouchTime ) <= 250 );

		if ( inSite && carrier->client &&
			carrier->client->ps.stats[STAT_HEALTH] > 0 &&
			( carrier->client->buttons & BUTTON_USE_HOLDABLE ) ) {
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
		G_RoundWin( G_AttackingTeam(), "Bomb detonated" );
		return;
	}

	// defuse: any living defender within reach, holding USE, makes progress
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
		if ( !( cl->buttons & BUTTON_USE_HOLDABLE ) ) {
			continue;
		}
		VectorSubtract( ent->r.currentOrigin, level.bombOrigin, delta );
		if ( VectorLength( delta ) <= (float)g_bombradius.integer ) {
			defusing = qtrue;
			break;
		}
	}

	defuseMsec = ( g_bombdefusetime.integer > 0 ) ? g_bombdefusetime.integer * 1000 : 7000;
	if ( defusing ) {
		level.defuseProgress += ( level.time - level.previousTime );
		if ( level.defuseProgress >= defuseMsec ) {
			level.bombState = BOMB_DEFUSED;
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
}

/*QUAKED trigger_bombsite (.5 .5 .5) ?
Search & destroy plant zone. The attacking bomb carrier plants here by
standing inside and holding USE. Place one or more per map (e.g. A and B).
*/
void SP_trigger_bombsite( gentity_t *ent ) {
	InitTrigger( ent );
	ent->touch = bombsite_touch;
	trap_LinkEntity( ent );
}

/*
==================
CheckRound

Drives the Search & Destroy round state machine; called once per frame from
G_RunFrame, after CheckTournament (so warmup has been resolved) and before
CheckExitRules (so an awarded round is seen by the match-end check this frame).
==================
*/
void CheckRound( void ) {
	int	aliveAtt, aliveDef;

	if ( !G_RoundBasedGametype() ) {
		return;
	}

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
