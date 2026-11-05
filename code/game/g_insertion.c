/* MW2's Tactical Insertion, the server's half (flare_mp; _perkfunctions.gsc's
 * monitorTIUse, GlowStickUseListener, GlowStickEnemyUseListener and
 * GlowStickDamageListener; _playerlogic.gsc's spawn on it). The pull, the
 * ignite, the held release, the Y cancel and the recovery are ordinary
 * equipment in shared pmove: nothing here touches a weapon state. See
 * dev/TACTICAL_INSERTION.md.
 *
 * MW2 throws an invisible flare and ignores it: the TI is planted at the
 * owner's last good standing spot when the throw happens (FireWeapon). One
 * entity stands in for MW2's two (the glowstick its owner picks up, and the
 * enemyTrigger enemies smash): who may do what is asked of the player at hint
 * time and again at use time, and every MW2 path that deletes one deletes
 * both. Every way it ends is G_FreeEntity, which empties any use hint naming it
 * (the sea lion). */
#include "g_local.h"

// MW2's playerPhysicsTrace and CanSpawn mask (0x810011): the world, never a player.
#define INSERTION_MASK	( CONTENTS_SOLID | CONTENTS_PLAYERCLIP )

// The standing player box (PM_CheckDuck's): MW2's CanSpawn and
// TIGroundPosition use the player's whole box.
static void G_InsertionPlayerBox( vec3_t mins, vec3_t maxs ) {
	VectorSet( mins, -PLAYER_WIDTH, -PLAYER_WIDTH, MINS_Z );
	VectorSet( maxs, PLAYER_WIDTH, PLAYER_WIDTH, DEFAULT_HEIGHT );
}

qboolean G_IsInsertion( const gentity_t *ent ) {
	return ent->inuse && ent->s.eType == ET_MISSILE && ent->s.weapon == WP_TACTICAL_INSERTION;
}

/*
G_InsertionOwner

The player who planted it, while that is still the same connection in the
slot (ownerConnect, as a Semtex's kill credit). NULL once they have gone.
*/
gentity_t *G_InsertionOwner( const gentity_t *ti ) {
	gentity_t *owner = ti->parent;
	if ( !owner || !owner->inuse || !owner->client || owner->client->pers.connected != CON_CONNECTED ||
		owner->client->pers.enterTime != ti->ownerConnect ) return NULL;
	return owner;
}

// This player's live Tactical Insertion, if they have one (MW2's self.setSpawnPoint).
gentity_t *G_FindInsertion( gentity_t *player ) {
	int i;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *ti = &g_entities[i];
		if ( G_IsInsertion( ti ) && G_InsertionOwner( ti ) == player ) return ti;
	}
	return NULL;
}

/*
G_FreeInsertion

MW2's clearPreviousTISpawnpoint and the plant's own deleteTI: whatever this
slot planted is freed (one per player). Run on a new plant, a team change or
joining the spectators, and a disconnect. Death leaves it.
*/
void G_FreeInsertion( gentity_t *player ) {
	int i;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *ti = &g_entities[i];
		if ( G_IsInsertion( ti ) && ti->parent == player ) G_FreeEntity( ti );
	}
}

/*
G_InsertionSample

MW2's TISpawnPosition (_perkfunctions.gsc:733-755): while alive, sampled
every server frame (MW2: every 50ms), the player's origin whenever they are on
the ground and could spawn there (CanSpawn: the standing box at the origin
does not start in solid). A TI thrown in the air, crouched under a low
ceiling or just after landing uses the last such spot. ClientEndFrame runs it.
*/
void G_InsertionSample( gentity_t *player ) {
	gclient_t *client = player->client;
	playerState_t *ps = &client->ps;
	vec3_t mins, maxs;
	trace_t tr;
	if ( ps->stats[STAT_HEALTH] <= 0 || ps->pm_type != PM_NORMAL || ps->groundEntityNum == ENTITYNUM_NONE ) return;
	G_InsertionPlayerBox( mins, maxs );
	trap_Trace( &tr, ps->origin, mins, maxs, ps->origin, player->s.number, INSERTION_MASK );
	if ( tr.startsolid || tr.allsolid ) return;
	VectorCopy( ps->origin, client->insertionSpot );
	client->insertionSpotValid = qtrue;
}

/*
G_InsertionHurting

MW2's monitorTIUse plants nothing while its owner touches a trigger_hurt
(_utility.gsc:2652-2669; Peace has no radiation triggers).
*/
static qboolean G_InsertionHurting( gentity_t *player ) {
	int touch[MAX_GENTITIES], i, n;
	vec3_t mins, maxs;
	VectorAdd( player->client->ps.origin, player->r.mins, mins );
	VectorAdd( player->client->ps.origin, player->r.maxs, maxs );
	n = trap_EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );
	for ( i = 0; i < n; i++ ) {
		gentity_t *hit = &g_entities[touch[i]];
		if ( hit->inuse && hit->classname && !Q_stricmp( hit->classname, "trigger_hurt" ) &&
			( hit->r.contents & CONTENTS_TRIGGER ) && trap_EntityContact( mins, maxs, hit ) ) return qtrue;
	}
	return qfalse;
}

/*
Damage: MW2's GlowStickDamageListener (_perkfunctions.gsc:878-908). It has
5000 health and is destroyed once it is below 4980, so once more than 20
damage has counted. In a team game a teammate's damage, the owner's aside, is
given back; anything else counts (FFA: anyone's). Its counted damage is kept
in count and its health held at 5000, so no refund can wrap it.
*/
static void G_InsertionDamaged( gentity_t *ti, gentity_t *attacker, int damage ) {
	gentity_t *owner = G_InsertionOwner( ti );
	ti->health = BG_INSERTION_HEALTH;
	if ( g_gametype.integer >= GT_TEAM && attacker && attacker->client && attacker != owner &&
		attacker->client->sess.sessionTeam == ti->s.generic1 ) return;
	ti->count += damage;
	if ( ti->count > BG_INSERTION_DESTROY_DAMAGE ) G_FreeEntity( ti );
}
static void G_InsertionPain( gentity_t *ti, gentity_t *attacker, int damage ) {
	G_InsertionDamaged( ti, attacker, damage );
}
static void G_InsertionDie( gentity_t *ti, gentity_t *inflictor, gentity_t *attacker, int damage, int mod ) {
	(void)inflictor; (void)mod;
	G_InsertionDamaged( ti, attacker, damage );
}

/*
G_PlantInsertion

MW2's monitorTIUse at the throw (_perkfunctions.gsc:757-799), from the TI's
EV_FIRE_WEAPON: the player's last TI is freed; then, with no standing spot
this life (or touching a trigger_hurt, or dead), nothing is planted - the TI
is spent all the same. Otherwise it goes on the ground below that spot:
TIGroundPosition, a player-box trace from 16 above it to 2048 below, plus 1.
MW2's player origin is its feet; Peace's is MINS_Z above them, so the ground
point is that trace's feet. It faces the owner's yaw (MW2's self.angles:
pitch and roll 0), and keeps the spot itself to spawn on (playerSpawnPos).

It is ET_MISSILE (CG_Missile draws it; S&D's round reset and the killcam
already handle missiles), stationary, shootable and never in a player's way:
CONTENTS_CORPSE is in MASK_SHOT but not MASK_PLAYERSOLID. No trace passes it
for its owner (r.ownerNum is no one), so the owner can shoot it, as in MW2.
*/
gentity_t *G_PlantInsertion( gentity_t *player ) {
	static const vec3_t mins = { -8, -8, 0 }, maxs = { 8, 8, 8 };
	gclient_t *client = player->client;
	vec3_t boxMins, boxMaxs, start, end, ground, angles;
	trace_t tr;
	gentity_t *ti;
	G_FreeInsertion( player );
	if ( !client || !client->insertionSpotValid || client->ps.stats[STAT_HEALTH] <= 0 ||
		G_InsertionHurting( player ) ) return NULL;
	G_InsertionPlayerBox( boxMins, boxMaxs );
	VectorCopy( client->insertionSpot, start );
	start[2] += 16;
	VectorCopy( client->insertionSpot, end );
	end[2] -= BG_INSERTION_GROUND_DROP;
	trap_Trace( &tr, start, boxMins, boxMaxs, end, ENTITYNUM_NONE, INSERTION_MASK );
	VectorCopy( tr.endpos, ground );
	ground[2] += MINS_Z + 1;

	ti = G_Spawn();
	ti->classname = "tacticalinsertion";
	ti->s.eType = ET_MISSILE;
	ti->s.weapon = WP_TACTICAL_INSERTION;
	ti->r.svFlags = SVF_USE_CURRENT_ORIGIN;
	ti->r.ownerNum = ENTITYNUM_NONE;
	ti->parent = player;
	ti->ownerConnect = client->pers.enterTime;
	ti->s.clientNum = player->s.number;			// whose, for cgame's picture only
	ti->s.generic1 = client->sess.sessionTeam;	// the owner's team, likewise (MW2's TI.team)
	ti->timestamp = level.time;					// names it to a use hold (useHoldSpawn)
	VectorCopy( client->insertionSpot, ti->pos1 );	// MW2's playerSpawnPos
	G_SetOrigin( ti, ground );
	VectorSet( angles, 0, client->ps.viewangles[YAW], 0 );
	VectorCopy( angles, ti->s.angles );
	VectorCopy( angles, ti->s.apos.trBase );
	VectorCopy( angles, ti->r.currentAngles );
	ti->s.apos.trType = TR_STATIONARY;
	VectorCopy( mins, ti->r.mins );
	VectorCopy( maxs, ti->r.maxs );
	ti->r.contents = CONTENTS_CORPSE;
	ti->clipmask = 0;
	ti->takedamage = qtrue;
	ti->health = BG_INSERTION_HEALTH;
	ti->count = 0;
	ti->pain = G_InsertionPain;
	ti->die = G_InsertionDie;
	trap_LinkEntity( ti );
	G_LogPrintf( "Insertion: %i planted %i\n", player->s.number, ti->s.number );
	return ti;
}

/*
G_InsertionSpawn

MW2's spawn on it (_playerlogic.gsc:512-535): a player who owns a live TI
spawns at its playerSpawnPos facing its yaw, and the TI is freed before the
spawn (MW2 deletes it before `self spawn`), so the spawn frame's hint pass
never names it. Peace's one addition, modelled on MW2's own invalid-TI
fallback (tiValidationCheck deletes the TI and spawns normally): with another
player standing in that box the TI is freed and the spawn is the ordinary
one. Nobody is telefragged: on a qtrue return ClientSpawn skips G_KillBox
(MW2's spawn on it has none), so a corpse lying there - a dead client not yet
respawned, which is no one standing - is neither gibbed nor credited to the
spawner. Never in S&D, whose one life a round is never spent on it. Returns
whether origin and angles are the TI's.
*/
qboolean G_InsertionSpawn( gentity_t *player, vec3_t origin, vec3_t angles ) {
	gentity_t *ti = G_FindInsertion( player );
	int touch[MAX_GENTITIES], i, n;
	vec3_t mins, maxs;
	qboolean occupied = qfalse;
	if ( !ti || g_gametype.integer == GT_SND ) return qfalse;
	G_InsertionPlayerBox( mins, maxs );
	VectorAdd( ti->pos1, mins, mins );
	VectorAdd( ti->pos1, maxs, maxs );
	n = trap_EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );
	for ( i = 0; i < n; i++ ) {
		gentity_t *hit = &g_entities[touch[i]];
		if ( hit != player && hit->inuse && hit->client && ( hit->r.contents & CONTENTS_BODY ) ) occupied = qtrue;
	}
	if ( !occupied ) {
		VectorCopy( ti->pos1, origin );
		VectorSet( angles, 0, ti->s.angles[YAW], 0 );
	}
	G_FreeEntity( ti );
	return !occupied;
}

/*
G_InsertionHint

What this player may do with it (MW2's setSelfUsable(owner) and
makeEnemyUsable(owner)): its owner picks it up, an enemy smashes it - in a
team game the other team, in FFA everyone but the owner - and a teammate can
do nothing with it, so it is never their hint.
*/
int G_InsertionHint( gentity_t *player, const gentity_t *ti ) {
	if ( G_InsertionOwner( ti ) == player ) return CURSOR_HINT_INSERTION;
	if ( g_gametype.integer >= GT_TEAM && player->client->sess.sessionTeam == ti->s.generic1 )
		return CURSOR_HINT_NONE;
	return CURSOR_HINT_INSERTION_ENEMY;
}

/*
G_FindInsertionHint

MW2's use list for it: within player_useRadius (BG_PICKUP_RANGE) of the eye,
in sight, at any facing (no facing test for a non-trigger usable), for a
player who may use it. The lowest MW2 score wins, a scriptmover's: 128 *
(1 - dot) + eye distance, less BG_INSERTION_HINT_BIAS. *score is that score.
*/
gentity_t *G_FindInsertionHint( gentity_t *player, float *score ) {
	playerState_t *ps = &player->client->ps;
	gentity_t *best = NULL;
	float bestScore = 0, s, distance;
	vec3_t eye;
	trace_t tr;
	int i;
	VectorCopy( ps->origin, eye );
	eye[2] += ps->viewheight;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *ti = &g_entities[i];
		if ( !G_IsInsertion( ti ) || G_InsertionHint( player, ti ) == CURSOR_HINT_NONE ) continue;
		s = G_UseScore( player, ti->r.currentOrigin, &distance ) - BG_INSERTION_HINT_BIAS;
		if ( distance > BG_PICKUP_RANGE ) continue;
		trap_Trace( &tr, eye, NULL, NULL, ti->r.currentOrigin, player->s.number, MASK_SOLID );
		if ( tr.startsolid || tr.fraction < 1.0f ) continue;
		if ( best && s >= bestScore ) continue;
		best = ti;
		bestScore = s;
	}
	if ( score ) *score = bestScore;
	return best;
}

/*
G_UseInsertion

The use, once the hold is done (G_UseThink): the owner's pickup is MW2's
setTacticalInsertion - the TI's start ammo, 1, set with no room test, and the
pickup sound - then deleteTI; an enemy's smash is deleteTI alone. Either free
empties every hint naming it, the user's own included: that is the TI's sea
lion entry. A teammate uses nothing. Returns whether it was used.
*/
qboolean G_UseInsertion( gentity_t *player, gentity_t *ti ) {
	playerState_t *ps = &player->client->ps;
	switch ( G_InsertionHint( player, ti ) ) {
	case CURSOR_HINT_INSERTION:
		ps->ammo[WP_TACTICAL_INSERTION] = BG_ClassEquipmentCount( WP_TACTICAL_INSERTION );
		BG_RebuildWeaponMask( ps );
		G_AddEvent( player, EV_ITEM_PICKUP, BG_FindItemForWeapon( WP_TACTICAL_INSERTION ) - bg_itemlist );
		G_LogPrintf( "Item: %i %s\n", player->s.number, ti->classname );
		G_FreeEntity( ti );
		return qtrue;
	case CURSOR_HINT_INSERTION_ENEMY:
		G_LogPrintf( "Insertion: %i smashed %i\n", player->s.number, ti->s.number );
		G_FreeEntity( ti );
		return qtrue;
	default:
		return qfalse;
	}
}
