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
CheckRound

Drives the Search & Destroy round state machine; called once per frame from
G_RunFrame, after CheckTournament (so warmup has been resolved) and before
CheckExitRules (so an awarded round is seen by the match-end check this frame).
==================
*/
void CheckRound( void ) {
	int	aliveRed, aliveBlue;

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
		aliveRed  = G_CountAliveOnTeam( TEAM_RED );
		aliveBlue = G_CountAliveOnTeam( TEAM_BLUE );

		if ( aliveRed == 0 && aliveBlue == 0 ) {
			// mutual wipe in the same frame
			G_RoundWin( TEAM_SPECTATOR, "Round draw" );
		} else if ( aliveBlue == 0 ) {
			G_RoundWin( TEAM_RED, "Red wins the round" );
		} else if ( aliveRed == 0 ) {
			G_RoundWin( TEAM_BLUE, "Blue wins the round" );
		} else if ( level.roundEndTime && level.time >= level.roundEndTime ) {
			// time expired. with no bomb objective yet, decide on survivors;
			// once planting exists the defenders win a timed-out round instead.
			if ( aliveRed > aliveBlue ) {
				G_RoundWin( TEAM_RED, "Time up - Red survives" );
			} else if ( aliveBlue > aliveRed ) {
				G_RoundWin( TEAM_BLUE, "Time up - Blue survives" );
			} else {
				G_RoundWin( TEAM_SPECTATOR, "Time up - round draw" );
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
