/*
===========================================================================
MW2's UAV killstreak.

Earned: BG_UAV_KILLS enemy kills in one life (mp/killstreakTable.csv: uav 3),
once a life (_killstreaks.gsc checkKillstreakReward's lastEarnedStreak), and
kept through death until used (self.pers["killstreaks"]).

Used: pmove pulls the trigger out when the selection's killstreak toggle asks
for it (PM_SelectedWeapon) and spends one on the command it is in hand, MW2's
weapon_change; its EV_USE_KILLSTREAK lands here.

Shown: for level.radarViewTime (BG_UAV_TIME) on the radar of the owner's team
(the owner alone outside team games): every compassRadarUpdateTime, or
compassFastRadarUpdateTime with two or more up (_uav.gsc's "fast_radar"), each
enemy's position is sent to them as a ping (the "uav" server command), which
cgame shows for compassRadarPingFadeTime.
===========================================================================
*/
#include "g_local.h"

#define MAX_UAVS 32
#define MAX_UAV_PINGS 48	// keeps one sweep's command inside a server command

typedef struct {
	int		expire;		// level.time it leaves; 0 for a free entry
	int		team;		// the team whose radar it is, TEAM_FREE outside team games
	int		owner;		// the client who called it in
} uav_t;

static uav_t	uavs[MAX_UAVS];
static int		nextSweep[MAX_CLIENTS];	// 0 while the client has no radar

void G_ClearUAVs( void ) {
	int i;
	memset( uavs, 0, sizeof( uavs ) );
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( nextSweep[i] && g_entities[i].client && g_entities[i].client->pers.connected == CON_CONNECTED ) {
			trap_SendServerCommand( i, "uav 0" );
		}
		nextSweep[i] = 0;
	}
}

static qboolean G_UAVCovers( const uav_t *uav, const gclient_t *client, int clientNum ) {
	if ( !uav->expire ) return qfalse;
	if ( g_gametype.integer >= GT_TEAM ) return client->sess.sessionTeam == uav->team;
	return clientNum == uav->owner;
}

static qboolean G_IsEnemy( const gclient_t *viewer, int viewerNum, const gentity_t *other ) {
	if ( !other->inuse || !other->client || other->s.number == viewerNum ) return qfalse;
	if ( other->client->pers.connected != CON_CONNECTED ) return qfalse;
	if ( other->client->sess.sessionTeam == TEAM_SPECTATOR || other->health <= 0 ) return qfalse;
	if ( g_gametype.integer >= GT_TEAM ) return other->client->sess.sessionTeam != viewer->sess.sessionTeam;
	return qtrue;
}

/*
==============
G_KillstreakKill

A kill by a living player of an enemy counts toward this life's streak.
==============
*/
void G_KillstreakKill( gentity_t *attacker, gentity_t *victim ) {
	gclient_t *client;
	if ( !attacker || !attacker->client || !victim || !victim->client || attacker == victim ) return;
	if ( attacker->health <= 0 ) return;
	if ( g_gametype.integer >= GT_TEAM && OnSameTeam( attacker, victim ) ) return;
	client = attacker->client;
	client->killstreakKills++;
	if ( client->killstreakKills != BG_UAV_KILLS ) return;
	client->pers.killstreakUAVs++;
	client->ps.ammo[WP_KILLSTREAK_UAV] = client->pers.killstreakUAVs;
	BG_RebuildWeaponMask( &client->ps );
	trap_SendServerCommand( attacker - g_entities, "cp \"^3UAV ready\n^7D-pad right to call it in\"" );
}

/*
==============
G_UseKillstreak

The trigger came out (EV_USE_KILLSTREAK): pmove has already spent the UAV.
==============
*/
void G_UseKillstreak( gentity_t *ent ) {
	gclient_t *client = ent->client;
	int i, slot = -1;
	if ( !client ) return;
	client->pers.killstreakUAVs = client->ps.ammo[WP_KILLSTREAK_UAV];
	for ( i = 0; i < MAX_UAVS; i++ ) {
		if ( !uavs[i].expire ) {
			slot = i;
			break;
		}
	}
	if ( slot < 0 ) return;
	uavs[slot].expire = level.time + BG_UAV_TIME;
	uavs[slot].team = g_gametype.integer >= GT_TEAM ? client->sess.sessionTeam : TEAM_FREE;
	uavs[slot].owner = ent - g_entities;
	for ( i = 0; i < level.maxclients; i++ ) {
		gclient_t *other = &level.clients[i];
		if ( other->pers.connected != CON_CONNECTED ) continue;
		if ( G_UAVCovers( &uavs[slot], other, i ) ) {
			trap_SendServerCommand( i, "cp \"^2UAV online\"" );
			// _uav.gsc notifies uav_update at the launch: the radar starts now
			nextSweep[i] = level.time;
		} else if ( other->sess.sessionTeam != TEAM_SPECTATOR ) {
			trap_SendServerCommand( i, "cp \"^1Enemy UAV online\"" );
		}
	}
}

static void G_SendSweep( int clientNum, int interval ) {
	gclient_t *viewer = &level.clients[clientNum];
	char cmd[MAX_STRING_CHARS];
	int i, pings = 0;
	Com_sprintf( cmd, sizeof( cmd ), "uav %i", interval );
	for ( i = 0; i < level.maxclients && pings < MAX_UAV_PINGS; i++ ) {
		gentity_t *other = &g_entities[i];
		if ( !G_IsEnemy( viewer, clientNum, other ) ) continue;
		Q_strcat( cmd, sizeof( cmd ), va( " %i %i %i", (int)other->r.currentOrigin[0],
			(int)other->r.currentOrigin[1], (int)AngleNormalize360( other->client->ps.viewangles[YAW] ) ) );
		pings++;
	}
	trap_SendServerCommand( clientNum, cmd );
}

/*
==============
G_UpdateUAVs

Each server frame: retire UAVs that have left, and sweep every radar due.
==============
*/
void G_UpdateUAVs( void ) {
	int i, j;
	for ( j = 0; j < MAX_UAVS; j++ ) {
		if ( uavs[j].expire && level.time >= uavs[j].expire ) uavs[j].expire = 0;
	}
	for ( i = 0; i < level.maxclients; i++ ) {
		gclient_t *client = &level.clients[i];
		int count = 0, interval;
		if ( client->pers.connected != CON_CONNECTED ) {
			nextSweep[i] = 0;
			continue;
		}
		for ( j = 0; j < MAX_UAVS; j++ ) {
			if ( G_UAVCovers( &uavs[j], client, i ) ) count++;
		}
		if ( !count ) {
			if ( nextSweep[i] ) trap_SendServerCommand( i, "uav 0" );
			nextSweep[i] = 0;
			continue;
		}
		if ( !nextSweep[i] ) nextSweep[i] = level.time;
		if ( level.time < nextSweep[i] ) continue;
		interval = count > 1 ? BG_FAST_RADAR_UPDATE_TIME : BG_RADAR_UPDATE_TIME;
		G_SendSweep( i, interval );
		nextSweep[i] = level.time + interval;
	}
}
