/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//
#include "g_local.h"

/*

  Items are any object that a player can touch to gain some effect.

  Pickup will return the number of seconds until they should respawn.

  all items should pop when dropped in lava or slime

  Respawnable items don't actually go away when picked up, they are
  just made invisible and untouchable.  This allows them to ride
  movers and respawn appropriately.
*/


#define	RESPAWN_ARMOR		25
#define	RESPAWN_HEALTH		35
#define	RESPAWN_AMMO		40
#define	RESPAWN_HOLDABLE	60
#define	RESPAWN_MEGAHEALTH	35//120
#define	RESPAWN_POWERUP		120


//======================================================================

int Pickup_Powerup( gentity_t *ent, gentity_t *other ) {
	int			quantity;
	int			i;
	gclient_t	*client;

	if ( !other->client->ps.powerups[ent->item->giTag] ) {
		// round timing to seconds to make multiple powerup timers
		// count in sync
		other->client->ps.powerups[ent->item->giTag] = 
			level.time - ( level.time % 1000 );
	}

	if ( ent->count ) {
		quantity = ent->count;
	} else {
		quantity = ent->item->quantity;
	}

	other->client->ps.powerups[ent->item->giTag] += quantity * 1000;

	// give any nearby players a "denied" anti-reward
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		vec3_t		delta;
		float		len;
		vec3_t		forward;
		trace_t		tr;

		client = &level.clients[i];
		if ( client == other->client ) {
			continue;
		}
		if ( client->pers.connected == CON_DISCONNECTED ) {
			continue;
		}
		if ( client->ps.stats[STAT_HEALTH] <= 0 ) {
			continue;
		}

    // if same team in team game, no sound
    // cannot use OnSameTeam as it expects to g_entities, not clients
  	if ( g_gametype.integer >= GT_TEAM && other->client->sess.sessionTeam == client->sess.sessionTeam  ) {
      continue;
    }

		// if too far away, no sound
		VectorSubtract( ent->s.pos.trBase, client->ps.origin, delta );
		len = VectorNormalize( delta );
		if ( len > 192 ) {
			continue;
		}

		// if not facing, no sound
		AngleVectors( client->ps.viewangles, forward, NULL, NULL );
		if ( DotProduct( delta, forward ) < 0.4 ) {
			continue;
		}

		// if not line of sight, no sound
		trap_Trace( &tr, client->ps.origin, NULL, NULL, ent->s.pos.trBase, ENTITYNUM_NONE, CONTENTS_SOLID );
		if ( tr.fraction != 1.0 ) {
			continue;
		}

		// anti-reward
		client->ps.persistant[PERS_PLAYEREVENTS] ^= PLAYEREVENT_DENIEDREWARD;
	}
	return RESPAWN_POWERUP;
}

//======================================================================

#ifdef MISSIONPACK
int Pickup_PersistantPowerup( gentity_t *ent, gentity_t *other ) {
	int		clientNum;
	char	userinfo[MAX_INFO_STRING];
	float	handicap;
	int		max;

	other->client->ps.stats[STAT_PERSISTANT_POWERUP] = ent->item - bg_itemlist;
	other->client->persistantPowerup = ent;

	switch( ent->item->giTag ) {
	case PW_GUARD:
		clientNum = other->client->ps.clientNum;
		trap_GetUserinfo( clientNum, userinfo, sizeof(userinfo) );
		handicap = atof( Info_ValueForKey( userinfo, "handicap" ) );
		if( handicap<=0.0f || handicap>100.0f) {
			handicap = 100.0f;
		}
		max = (int)(2 *  handicap);

		other->health = max;
		other->client->ps.stats[STAT_HEALTH] = max;
		other->client->ps.stats[STAT_MAX_HEALTH] = max;
		other->client->ps.stats[STAT_ARMOR] = max;
		other->client->pers.maxHealth = max;

		break;

	case PW_SCOUT:
		clientNum = other->client->ps.clientNum;
		trap_GetUserinfo( clientNum, userinfo, sizeof(userinfo) );
		handicap = atof( Info_ValueForKey( userinfo, "handicap" ) );
		if( handicap<=0.0f || handicap>100.0f) {
			handicap = 100.0f;
		}
		other->client->pers.maxHealth = handicap;
		other->client->ps.stats[STAT_ARMOR] = 0;
		break;

	case PW_DOUBLER:
		clientNum = other->client->ps.clientNum;
		trap_GetUserinfo( clientNum, userinfo, sizeof(userinfo) );
		handicap = atof( Info_ValueForKey( userinfo, "handicap" ) );
		if( handicap<=0.0f || handicap>100.0f) {
			handicap = 100.0f;
		}
		other->client->pers.maxHealth = handicap;
		break;
	case PW_AMMOREGEN:
		clientNum = other->client->ps.clientNum;
		trap_GetUserinfo( clientNum, userinfo, sizeof(userinfo) );
		handicap = atof( Info_ValueForKey( userinfo, "handicap" ) );
		if( handicap<=0.0f || handicap>100.0f) {
			handicap = 100.0f;
		}
		other->client->pers.maxHealth = handicap;
		memset(other->client->ammoTimes, 0, sizeof(other->client->ammoTimes));
		break;
	default:
		clientNum = other->client->ps.clientNum;
		trap_GetUserinfo( clientNum, userinfo, sizeof(userinfo) );
		handicap = atof( Info_ValueForKey( userinfo, "handicap" ) );
		if( handicap<=0.0f || handicap>100.0f) {
			handicap = 100.0f;
		}
		other->client->pers.maxHealth = handicap;
		break;
	}

	return -1;
}

//======================================================================
#endif

int Pickup_Holdable( gentity_t *ent, gentity_t *other ) {

	other->client->ps.stats[STAT_HOLDABLE_ITEM] = ent->item - bg_itemlist;

	if( ent->item->giTag == HI_KAMIKAZE ) {
		other->client->ps.eFlags |= EF_KAMIKAZE;
	}

	return RESPAWN_HOLDABLE;
}


//======================================================================

void Add_Ammo (gentity_t *ent, int weapon, int count)
{
	ent->client->ps.ammo[weapon] += count;
	if ( ent->client->ps.ammo[weapon] > 200 ) {
		ent->client->ps.ammo[weapon] = 200;
	}
}

int Pickup_Ammo (gentity_t *ent, gentity_t *other)
{
	int		quantity;

	if ( ent->count ) {
		quantity = ent->count;
	} else {
		quantity = ent->item->quantity;
	}

	Add_Ammo (other, ent->item->giTag, quantity);

	return RESPAWN_AMMO;
}

//======================================================================


int Pickup_Weapon (gentity_t *ent, gentity_t *other) {
	playerState_t	*ps = &other->client->ps;
	int				wep = ent->item->giTag;
	int				slot;

	// Equipment pickups replenish their own count; they cannot replace a gun.
	if ( BG_IsActionWeapon( wep ) ) {
		ps->ammo[wep] = wep == WP_KNIFE ? -1 : 3;
		BG_RebuildWeaponMask( ps );
		return g_weaponRespawn.integer;
	}

	{
		bg_weaponInstance_t floor, held;
		if (!BG_WeaponItem(&ent->s, &floor)) return 0;
		slot = BG_MatchingWeaponSlot(ps, &floor);
		if (slot < 0) return 0;
		BG_GetSlot(ps, slot, &held);
		// Scavenging never reloads the held magazine or changes its chamber.
		held.reserve = MIN(BG_WeaponMaxReserve(wep), held.reserve + floor.ammo + floor.reserve);
		BG_PutSlot(ps, slot, &held);
	}

	if ( g_gametype.integer == GT_TEAM ) {
		return g_weaponTeamRespawn.integer ? g_weaponTeamRespawn.integer : -1;
	}
	return g_weaponRespawn.integer ? g_weaponRespawn.integer : -1;
}


//======================================================================

int Pickup_Health (gentity_t *ent, gentity_t *other) {
	int			max;
	int			quantity;

	// small and mega healths will go over the max
#ifdef MISSIONPACK
	if( bg_itemlist[other->client->ps.stats[STAT_PERSISTANT_POWERUP]].giTag == PW_GUARD ) {
		max = other->client->ps.stats[STAT_MAX_HEALTH];
	}
	else
#endif
	if ( ent->item->quantity != 5 && ent->item->quantity != 100 ) {
		max = other->client->ps.stats[STAT_MAX_HEALTH];
	} else {
		max = other->client->ps.stats[STAT_MAX_HEALTH] * 2;
	}

	if ( ent->count ) {
		quantity = ent->count;
	} else {
		quantity = ent->item->quantity;
	}

	other->health += quantity;

	if (other->health > max ) {
		other->health = max;
	}
	other->client->ps.stats[STAT_HEALTH] = other->health;

	if ( ent->item->quantity == 100 ) {		// mega health respawns slow
		return RESPAWN_MEGAHEALTH;
	}

	return RESPAWN_HEALTH;
}

//======================================================================

int Pickup_Armor( gentity_t *ent, gentity_t *other ) {
#ifdef MISSIONPACK
	int		upperBound;

	other->client->ps.stats[STAT_ARMOR] += ent->item->quantity;

	if( other->client && bg_itemlist[other->client->ps.stats[STAT_PERSISTANT_POWERUP]].giTag == PW_GUARD ) {
		upperBound = other->client->ps.stats[STAT_MAX_HEALTH];
	}
	else {
		upperBound = other->client->ps.stats[STAT_MAX_HEALTH] * 2;
	}

	if ( other->client->ps.stats[STAT_ARMOR] > upperBound ) {
		other->client->ps.stats[STAT_ARMOR] = upperBound;
	}
#else
	other->client->ps.stats[STAT_ARMOR] += ent->item->quantity;
	if ( other->client->ps.stats[STAT_ARMOR] > other->client->ps.stats[STAT_MAX_HEALTH] * 2 ) {
		other->client->ps.stats[STAT_ARMOR] = other->client->ps.stats[STAT_MAX_HEALTH] * 2;
	}
#endif

	return RESPAWN_ARMOR;
}

//======================================================================

/*
===============
RespawnItem
===============
*/
void RespawnItem( gentity_t *ent ) {
	if (!ent) {
		return;
	}

	// randomly select from teamed entities
	if (ent->team) {
		gentity_t	*master;
		int	count;
		int choice;

		if ( !ent->teammaster ) {
			G_Error( "RespawnItem: bad teammaster");
		}
		master = ent->teammaster;

		for (count = 0, ent = master; ent; ent = ent->teamchain, count++)
			;

		choice = rand() % count;

		for (count = 0, ent = master; ent && count < choice; ent = ent->teamchain, count++)
			;
	}

	if (!ent) {
		return;
	}

	ent->r.contents = CONTENTS_TRIGGER;
	ent->s.time = level.time;
	ent->s.eFlags &= ~EF_NODRAW;
	ent->r.svFlags &= ~SVF_NOCLIENT;
	trap_LinkEntity (ent);

	if ( ent->item->giType == IT_POWERUP ) {
		// play powerup spawn sound to all clients
		gentity_t	*te;

		// if the powerup respawn sound should Not be global
		if (ent->speed) {
			te = G_TempEntity( ent->s.pos.trBase, EV_GENERAL_SOUND );
		}
		else {
			te = G_TempEntity( ent->s.pos.trBase, EV_GLOBAL_SOUND );
		}
		te->s.eventParm = G_SoundIndex( "sound/items/poweruprespawn.wav" );
		te->r.svFlags |= SVF_BROADCAST;
	}

	if ( ent->item->giType == IT_HOLDABLE && ent->item->giTag == HI_KAMIKAZE ) {
		// play powerup spawn sound to all clients
		gentity_t	*te;

		// if the powerup respawn sound should Not be global
		if (ent->speed) {
			te = G_TempEntity( ent->s.pos.trBase, EV_GENERAL_SOUND );
		}
		else {
			te = G_TempEntity( ent->s.pos.trBase, EV_GLOBAL_SOUND );
		}
		te->s.eventParm = G_SoundIndex( "sound/items/kamikazerespawn.wav" );
		te->r.svFlags |= SVF_BROADCAST;
	}

	// play the normal respawn sound only to nearby clients
	G_AddEvent( ent, EV_ITEM_RESPAWN, 0 );

	ent->nextthink = 0;
}


/*
===============
Touch_Item
===============
*/
static void G_CompleteItemPickup(gentity_t *ent, gentity_t *other, int respawn, qboolean predict);

void Touch_Item (gentity_t *ent, gentity_t *other, trace_t *trace) {
	int			respawn;
	qboolean	predict;

	if (!other->client)
		return;
	if (other->health < 1)
		return;		// dead people can't pickup
	if (!(ent->r.contents & CONTENTS_TRIGGER)) return;
	if (ent->item->giType == IT_WEAPON) {
		bg_weaponInstance_t gun;
		if (!BG_WeaponItem(&ent->s, &gun) || BG_MatchingWeaponSlot(&other->client->ps, &gun) < 0) return;
		if (ent->s.modelindex2 && ent->s.clientNum == other->s.number && level.time < ent->s.time + BG_PICKUP_OWNER_DELAY) return;
	}

	// the same pickup rules are used for client side and server side
	if ( !BG_CanItemBeGrabbed( g_gametype.integer, &ent->s, &other->client->ps ) ) {
		return;
	}

	G_LogPrintf( "Item: %i %s\n", other->s.number, ent->item->classname );

	predict = other->client->pers.predictItemPickup;

	// call the item-specific pickup function
	switch( ent->item->giType ) {
	case IT_WEAPON:
		respawn = Pickup_Weapon(ent, other);
		predict = qfalse;
		break;
	case IT_AMMO:
		respawn = Pickup_Ammo(ent, other);
//		predict = qfalse;
		break;
	case IT_ARMOR:
		respawn = Pickup_Armor(ent, other);
		break;
	case IT_HEALTH:
		respawn = Pickup_Health(ent, other);
		break;
	case IT_POWERUP:
		respawn = Pickup_Powerup(ent, other);
		predict = qfalse;
		break;
#ifdef MISSIONPACK
	case IT_PERSISTANT_POWERUP:
		respawn = Pickup_PersistantPowerup(ent, other);
		break;
#endif
	case IT_TEAM:
		respawn = Pickup_Team(ent, other);
		break;
	case IT_HOLDABLE:
		respawn = Pickup_Holdable(ent, other);
		break;
	default:
		return;
	}

	if ( !respawn ) {
		return;
	}
	G_CompleteItemPickup(ent, other, respawn, predict);
}

static void G_CompleteItemPickup(gentity_t *ent, gentity_t *other, int respawn, qboolean predict) {

	// play the normal pickup sound
	if (predict) {
		G_AddPredictableEvent( other, EV_ITEM_PICKUP, ent->s.modelindex );
	} else {
		G_AddEvent( other, EV_ITEM_PICKUP, ent->s.modelindex );
	}

	// powerup pickups are global broadcasts
	if ( ent->item->giType == IT_POWERUP || ent->item->giType == IT_TEAM) {
		// if we want the global sound to play
		if (!ent->speed) {
			gentity_t	*te;

			te = G_TempEntity( ent->s.pos.trBase, EV_GLOBAL_ITEM_PICKUP );
			te->s.eventParm = ent->s.modelindex;
			te->r.svFlags |= SVF_BROADCAST;
		} else {
			gentity_t	*te;

			te = G_TempEntity( ent->s.pos.trBase, EV_GLOBAL_ITEM_PICKUP );
			te->s.eventParm = ent->s.modelindex;
			// only send this temp entity to a single client
			te->r.svFlags |= SVF_SINGLECLIENT;
			te->r.singleClient = other->s.number;
		}
	}

	// fire item targets
	G_UseTargets (ent, other);

	// wait of -1 will not respawn
	if ( ent->wait == -1 ) {
		ent->r.svFlags |= SVF_NOCLIENT;
		ent->s.eFlags |= EF_NODRAW;
		ent->r.contents = 0;
		ent->unlinkAfterEvent = qtrue;
		return;
	}

	// non zero wait overrides respawn time
	if ( ent->wait ) {
		respawn = ent->wait;
	}

	// random can be used to vary the respawn time
	if ( ent->random ) {
		respawn += crandom() * ent->random;
		if ( respawn < 1 ) {
			respawn = 1;
		}
	}

	// dropped items will not respawn
	if ( ent->flags & FL_DROPPED_ITEM ) {
		ent->freeAfterEvent = qtrue;
	}

	// picked up items still stay around, they just don't
	// draw anything.  This allows respawnable items
	// to be placed on movers.
	ent->r.svFlags |= SVF_NOCLIENT;
	ent->s.eFlags |= EF_NODRAW;
	ent->r.contents = 0;

	// ZOID
	// A negative respawn times means to never respawn this item (but don't 
	// delete it).  This is used by items that are respawned by third party 
	// events such as ctf flags
	if ( respawn <= 0 ) {
		ent->nextthink = 0;
		ent->think = 0;
	} else {
		ent->nextthink = level.time + respawn * 1000;
		ent->think = RespawnItem;
	}
	trap_LinkEntity( ent );
}


const entityState_t *G_FindWeaponPickup(gentity_t *player) {
	const entityState_t *best = NULL;
	float nearest = BG_PICKUP_RANGE * BG_PICKUP_RANGE + 1;
	int i;
	vec3_t eye, end;
	trace_t trace;
	playerState_t *ps = &player->client->ps;
	VectorCopy(ps->origin, eye);
	eye[2] += ps->viewheight;
	for (i = MAX_CLIENTS; i < level.num_entities; i++) {
		gentity_t *item = &g_entities[i];
		float distance;
		if (!item->inuse || !(item->r.contents & CONTENTS_TRIGGER)) continue;
		distance = BG_WeaponPickupDistance(ps, &item->s, level.time);
		if (distance < 0 || distance >= nearest) continue;
		BG_EvaluateTrajectory(&item->s.pos, level.time, end);
		trap_Trace(&trace, eye, NULL, NULL, end, player->s.number, MASK_SOLID);
		if (trace.startsolid || trace.fraction < 1.0f) continue;
		best = &item->s;
		nearest = distance;
	}
	return best;
}

static qboolean G_IsThrowingKnife(const gentity_t *ent) {
	return ent->inuse && ent->s.eType == ET_MISSILE && ent->s.weapon == WP_THROWING_KNIFE;
}

/*
G_FindKnifePickup

MW2's use list for a throwing knife: one moving no faster than
bg_maxGrenadeIndicatorSpeed, within the eye reach and in sight, for a player
who carries knives and has room for one more. No owner or team check. A knife
must first come within the inner radius of the player's feet; one the hint
already names keeps its place out to the eye reach. The lowest
128 * (1 - dot) + distance wins.
*/
static gentity_t *G_FindKnifePickup(gentity_t *player) {
	gclient_t *client = player->client;
	playerState_t *ps = &client->ps;
	gentity_t *best = NULL;
	float bestScore = 0;
	vec3_t eye, forward, origin, delta;
	trace_t trace;
	int i;
	if (client->knifeMax <= 0 || ps->ammo[WP_THROWING_KNIFE] >= client->knifeMax) return NULL;
	VectorCopy(ps->origin, eye);
	eye[2] += ps->viewheight;
	AngleVectors(ps->viewangles, forward, NULL, NULL);
	for (i = MAX_CLIENTS; i < level.num_entities; i++) {
		gentity_t *knife = &g_entities[i];
		float distance, score;
		if (!G_IsThrowingKnife(knife) || VectorLength(knife->s.pos.trDelta) > BG_KNIFE_PICKUP_STILL) continue;
		BG_EvaluateTrajectory(&knife->s.pos, level.time, origin);
		VectorSubtract(origin, ps->origin, delta);
		if (ps->cursorHintEntIndex != i &&
			delta[0] * delta[0] + delta[1] * delta[1] > BG_KNIFE_PICKUP_RADIUS * BG_KNIFE_PICKUP_RADIUS) continue;
		VectorSubtract(origin, eye, delta);
		distance = VectorNormalize(delta);
		if (distance > BG_KNIFE_PICKUP_REACH) continue;
		trap_Trace(&trace, eye, NULL, NULL, origin, player->s.number, MASK_SOLID);
		if (trace.startsolid || trace.fraction < 1.0f) continue;
		score = 128 * (1 - DotProduct(forward, delta)) + distance;
		if (best && score >= bestScore) continue;
		best = knife;
		bestScore = score;
	}
	return best;
}

/*
G_TakeKnife

MW2's Touch_Item for a knife taken by use: one more knife, the pickup sound,
and the knife is freed - which empties every use hint naming it (the sea
lion). The hands are not touched.
*/
static void G_TakeKnife(gentity_t *player, gentity_t *knife) {
	playerState_t *ps = &player->client->ps;
	ps->ammo[WP_THROWING_KNIFE]++;
	BG_RebuildWeaponMask(ps);
	G_AddEvent(player, EV_ITEM_PICKUP, BG_FindItemForWeapon(WP_THROWING_KNIFE) - bg_itemlist);
	G_LogPrintf("Item: %i %s\n", player->s.number, knife->classname);
	G_FreeEntity(knife);
}

/*
G_UseKnife

MW2's Player_ActivateCmd and Player_ActivateHoldCmd for a throwing knife. A
new use press while the last end-frame's hint is a knife takes that knife as
its hold, and is used: it is never a reload. While the button stays held the
knife is taken once it is g_useholdspawndelay old (g_useholdtime is 0), so a
press on a settled knife takes it on that command. A press made while
sprinting or with equipment out uses nothing (BG_UpdateActivate). Returns
whether the hint is a knife, for BG_UpdateActivate; *held says whether a press
is the knife's this command. A knife that only becomes the hint during a hold
for something else takes nothing from it: MW2 holds for what the press chose.
*/
static qboolean G_UseKnife(gentity_t *player, usercmd_t *cmd, qboolean *held) {
	gclient_t *client = player->client;
	playerState_t *ps = &client->ps;
	const int useButtons = BUTTON_USE | BUTTON_USE_RELOAD;
	int old = client->activate.done ? client->activate.buttons : 0;
	qboolean hinted = client->cursorHint && ps->cursorHintEntIndex >= MAX_CLIENTS &&
		ps->cursorHintEntIndex < ENTITYNUM_MAX_NORMAL && G_IsThrowingKnife(&g_entities[ps->cursorHintEntIndex]);
	gentity_t *knife;
	*held = qfalse;
	if (hinted && (cmd->buttons & useButtons & ~old) &&
		!(ps->pm_flags & PMF_SPRINTING) && !BG_OffhandOut(ps)) {
		client->knifeHold = ps->cursorHintEntIndex;
		client->knifeHoldSpawn = g_entities[client->knifeHold].timestamp;
	}
	if (!(cmd->buttons & useButtons) || client->knifeHold == ENTITYNUM_NONE) {
		client->knifeHold = ENTITYNUM_NONE;
		return hinted;
	}
	knife = &g_entities[client->knifeHold];
	if (!G_IsThrowingKnife(knife) || knife->timestamp != client->knifeHoldSpawn) {
		client->knifeHold = ENTITYNUM_NONE;
		return hinted;
	}
	*held = qtrue;
	if (level.time - knife->timestamp >= BG_USE_SPAWN_DELAY) {
		client->knifeHold = ENTITYNUM_NONE;
		if (ps->stats[STAT_HEALTH] > 0 && ps->ammo[WP_THROWING_KNIFE] < client->knifeMax)
			G_TakeKnife(player, knife);
	}
	return hinted;
}

/*
G_UseThink

The bomb, the throwing knife and the floor gun, after a command's Pmove. MW2
uses one thing per use/reload press, and the bomb comes first: a press at a
site or the case is never a floor-gun hold too, and a press already used there
cannot become one. A knife outranks any gun in MW2's use list, so a press on a
knife's hint is the knife's. Returns whether a press is used at once, for
BG_UpdateActivate.
*/
qboolean G_UseThink(gentity_t *player, usercmd_t *cmd) {
	gclient_t *client = player->client;
	qboolean sndUsable = G_SNDUsable(player);
	qboolean knifeHinted, knifeHeld = qfalse;
	G_SNDUse(player, cmd);
	knifeHinted = !sndUsable && G_UseKnife(player, cmd, &knifeHeld);
	if (sndUsable || knifeHeld) client->activate.hold = qfalse;
	if (client->ps.pickupHeld == 3 &&
		(sndUsable || knifeHeld || (client->activate.done && !client->activate.hold)))
		client->ps.pickupHeld = 2;
	G_CommitWeaponPickup(player);
	return sndUsable || knifeHinted;
}

void G_CommitWeaponPickup(gentity_t *player) {
	playerState_t *ps = &player->client->ps;
	bg_weaponInstance_t gun;
	gentity_t *item;
	int slot;
	if ((ps->pickupHeld != 1 && ps->pickupHeld != 3) || ps->pickupTime < BG_PICKUP_HOLD_TIME) return;
	ps->pickupHeld = 2;
	player->client->activate.hold = qfalse; // the hold was used: its release is not a reload
	if (ps->pickupTarget < MAX_CLIENTS || ps->pickupTarget >= level.num_entities) return;
	item = &g_entities[ps->pickupTarget];
	if (!item->inuse || !(item->r.contents & CONTENTS_TRIGGER) ||
		item->s.time != ps->pickupSerial || G_FindWeaponPickup(player) != &item->s ||
		!BG_WeaponItem(&item->s, &gun)) return;
	slot = ps->pickupSlot;
	if (!G_DropWeapon(player, slot)) return; // no room for the outgoing instance
	BG_PutSlot(ps, slot, &gun);
	// A new instance in the slot: the client's selection of it from before
	// this snapshot names the gun just dropped (BG_SlotCommandCurrent).
	ps->slotFlags[slot] ^= BG_GUN_GENERATION;
	if (slot == ps->stats[STAT_ACTIVE_SLOT] && !BG_IsActionWeapon(ps->weapon)) {
		if (!(ps->stats[STAT_LADDER] & (LADDER_ATTACHED | LADDER_HOLSTER)) &&
			ps->weaponstate != WEAPON_LADDER) {
			// MW2's BG_TakePlayerWeapon: the hands are empty until the
			// client's selection names the new gun. What the old gun was
			// doing ends with it.
			ps->weapon = WP_NONE;
			if (ps->weaponstate != WEAPON_DROPPING) {	// a running drop runs on (MW2's take)
				ps->weaponstate = WEAPON_READY;
				ps->weaponTime = 0;
			}
			ps->weaponAnimTime = -1;
			ps->pm_flags &= ~PMF_PENDING_MAG;
		} else {
			ps->weapon = gun.weapon;
		}
	}
	G_LogPrintf("Item: %i %s\n", player->s.number, item->item->classname);
	G_CompleteItemPickup(item, player, g_gametype.integer == GT_TEAM ?
		g_weaponTeamRespawn.integer : g_weaponRespawn.integer, qfalse);
	ps->pickupTarget = ENTITYNUM_NONE;
	// MW2's Touch_Item frees the swapped weapon inside the use, before the
	// end-frame hint pass (a map gun is only hidden to respawn).
	if (item->flags & FL_DROPPED_ITEM) G_FreeEntity(item);
}

/*
G_UpdateCursorHint

MW2's Player_UpdateCursorHints, once per end-frame: the use hint names the
usable thing in reach, and is sticky. With nothing usable in reach, while
sprinting, with equipment out, or dead, it is not written at all, so it keeps
naming the last thing it named, and spawn leaves it 0. Only freeing what it
names (G_ClearCursorHintRefs) turns it into ENTITYNUM_NONE. Pmove reads it.
*/
void G_UpdateCursorHint(gentity_t *player) {
	playerState_t *ps = &player->client->ps;
	const entityState_t *item;
	gentity_t *knife;
	player->client->cursorHint = qfalse;
	if (ps->stats[STAT_HEALTH] <= 0 || ps->pm_type != PM_NORMAL ||
		(ps->pm_flags & PMF_SPRINTING) || BG_OffhandOut(ps)) return;
	if (G_SNDUsable(player)) {
		ps->cursorHintEntIndex = ENTITYNUM_WORLD;	// a site or the bomb: never freed
		player->client->cursorHint = qtrue;
		return;
	}
	knife = G_FindKnifePickup(player);	// a missile outranks any gun (-512)
	item = knife ? &knife->s : G_FindWeaponPickup(player);
	if (!item) return;
	player->client->cursorHint = qtrue;
	ps->cursorHintEntIndex = item->number;
	g_entities[item->number].flags |= FL_CURSOR_HINT;
}

/*
G_ClearCursorHintRefs

MW2's G_FreeEntity: freeing an entity some client's use hint names leaves that
hint naming nothing at all, ENTITYNUM_NONE - not 0, which every life starts on.
*/
void G_ClearCursorHintRefs(gentity_t *ent) {
	int i;
	if (!(ent->flags & FL_CURSOR_HINT)) return;
	for (i = 0; i < level.maxclients; i++) {
		gclient_t *client = g_entities[i].client;
		if (!g_entities[i].inuse || !client) continue;
		if (client->ps.cursorHintEntIndex == ent->s.number) client->ps.cursorHintEntIndex = ENTITYNUM_NONE;
	}
}

//======================================================================

/*
================
LaunchItem

Spawns an item and tosses it forward
================
*/
gentity_t *LaunchItem( gitem_t *item, vec3_t origin, vec3_t velocity ) {
	gentity_t	*dropped;

	dropped = G_Spawn();

	dropped->s.eType = ET_ITEM;
	dropped->s.modelindex = item - bg_itemlist;	// store item number in modelindex
	dropped->s.modelindex2 = 1; // This is non-zero is it's a dropped item

	dropped->classname = item->classname;
	dropped->item = item;
	VectorSet (dropped->r.mins, -ITEM_RADIUS, -ITEM_RADIUS, -ITEM_RADIUS);
	VectorSet (dropped->r.maxs, ITEM_RADIUS, ITEM_RADIUS, ITEM_RADIUS);
	dropped->r.contents = CONTENTS_TRIGGER;

	dropped->touch = Touch_Item;

	G_SetOrigin( dropped, origin );
	dropped->s.pos.trType = TR_GRAVITY;
	dropped->s.pos.trTime = level.time;
	VectorCopy( velocity, dropped->s.pos.trDelta );

	dropped->s.eFlags |= EF_BOUNCE_HALF;
#ifdef MISSIONPACK
	if ((g_gametype.integer == GT_CTF || g_gametype.integer == GT_1FCTF)			&& item->giType == IT_TEAM) { // Special case for CTF flags
#else
	if (g_gametype.integer == GT_CTF && item->giType == IT_TEAM) { // Special case for CTF flags
#endif
		dropped->think = Team_DroppedFlagThink;
		dropped->nextthink = level.time + 30000;
		Team_CheckDroppedItem( dropped );
	} else { // auto-remove after 30 seconds
		dropped->think = G_FreeEntity;
		dropped->nextthink = level.time + 30000;
	}

	dropped->flags = FL_DROPPED_ITEM;

	trap_LinkEntity (dropped);

	return dropped;
}

/*
================
Drop_Item

Spawns an item and tosses it forward
================
*/
gentity_t *Drop_Item( gentity_t *ent, gitem_t *item, float angle ) {
	vec3_t	velocity;
	vec3_t	angles;

	VectorCopy( ent->s.apos.trBase, angles );
	angles[YAW] += angle;
	angles[PITCH] = 0;	// always forward

	AngleVectors( angles, velocity, NULL, NULL );
	VectorScale( velocity, 150, velocity );
	velocity[2] += 200 + crandom() * 50;
	
	return LaunchItem( item, ent->s.pos.trBase, velocity );
}

gentity_t *G_DropWeapon(gentity_t *owner, int slot) {
	bg_weaponInstance_t gun;
	vec3_t angles, velocity;
	gentity_t *item;
	BG_GetSlot(&owner->client->ps, slot, &gun);
	if (BG_WeaponMagSize(gun.weapon) <= 0 || !G_EntitiesFree()) return NULL;
	VectorSet(angles, 0, owner->client->ps.viewangles[YAW], 0);
	AngleVectors(angles, velocity, NULL, NULL);
	VectorScale(velocity, 110, velocity);
	velocity[2] = 150;
	item = LaunchItem(BG_FindItemForWeapon(gun.weapon), owner->client->ps.origin, velocity);
	item->s.time = level.time;
	item->s.clientNum = owner->s.number;
	item->s.generic1 = gun.attachments;
	item->s.time2 = (gun.ammo & 65535) | ((unsigned int)gun.reserve << 16);
	item->s.otherEntityNum2 = gun.flags & BG_GUN_UNCHAMBERED;
	item->s.apos.trType = TR_STATIONARY;
	VectorSet(item->s.apos.trBase, 0, angles[YAW], 90);
	return item;
}


/*
================
Use_Item

Respawn the item
================
*/
void Use_Item( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	RespawnItem( ent );
}

//======================================================================

/*
================
FinishSpawningItem

Traces down to find where an item should rest, instead of letting them
free fall from their spawn points
================
*/
void FinishSpawningItem( gentity_t *ent ) {
	trace_t		tr;
	vec3_t		dest;

	VectorSet( ent->r.mins, -ITEM_RADIUS, -ITEM_RADIUS, -ITEM_RADIUS );
	VectorSet( ent->r.maxs, ITEM_RADIUS, ITEM_RADIUS, ITEM_RADIUS );

	ent->s.eType = ET_ITEM;
	ent->s.modelindex = ent->item - bg_itemlist;		// store item number in modelindex
	ent->s.modelindex2 = 0; // zero indicates this isn't a dropped item

	ent->r.contents = CONTENTS_TRIGGER;
	ent->touch = Touch_Item;
	// using an item causes it to respawn
	ent->use = Use_Item;

	if ( ent->spawnflags & 1 ) {
		// suspended
		G_SetOrigin( ent, ent->s.origin );
	} else {
		// drop to floor
		VectorSet( dest, ent->s.origin[0], ent->s.origin[1], ent->s.origin[2] - 4096 );
		trap_Trace( &tr, ent->s.origin, ent->r.mins, ent->r.maxs, dest, ent->s.number, MASK_SOLID );
		if ( tr.startsolid ) {
			G_Printf ("FinishSpawningItem: %s startsolid at %s\n", ent->classname, vtos(ent->s.origin));
			G_FreeEntity( ent );
			return;
		}

		// allow to ride movers
		ent->s.groundEntityNum = tr.entityNum;

		G_SetOrigin( ent, tr.endpos );
	}

	// team members and targeted items aren't present at start
	if ( ( ent->flags & FL_TEAMMEMBER ) || ent->targetname ) {
		ent->s.eFlags |= EF_NODRAW;
		ent->r.contents = 0;
		return;
	}

	// powerups don't spawn in for a while
	if ( ent->item->giType == IT_POWERUP ) {
		float	respawn;

		respawn = 45 + crandom() * 15;
		ent->s.eFlags |= EF_NODRAW;
		ent->r.contents = 0;
		ent->nextthink = level.time + respawn * 1000;
		ent->think = RespawnItem;
		return;
	}


	trap_LinkEntity (ent);
}


qboolean	itemRegistered[MAX_ITEMS];

/*
==================
G_CheckTeamItems
==================
*/
void G_CheckTeamItems( void ) {

	// Set up team stuff
	Team_InitGame();

	if( g_gametype.integer == GT_CTF ) {
		gitem_t	*item;

		// check for the two flags
		item = BG_FindItem( "Red Flag" );
		if ( !item || !itemRegistered[ item - bg_itemlist ] ) {
			G_Printf( S_COLOR_YELLOW "WARNING: No team_CTF_redflag in map\n" );
		}
		item = BG_FindItem( "Blue Flag" );
		if ( !item || !itemRegistered[ item - bg_itemlist ] ) {
			G_Printf( S_COLOR_YELLOW "WARNING: No team_CTF_blueflag in map\n" );
		}
	}
#ifdef MISSIONPACK
	if( g_gametype.integer == GT_1FCTF ) {
		gitem_t	*item;

		// check for all three flags
		item = BG_FindItem( "Red Flag" );
		if ( !item || !itemRegistered[ item - bg_itemlist ] ) {
			G_Printf( S_COLOR_YELLOW "WARNING: No team_CTF_redflag in map\n" );
		}
		item = BG_FindItem( "Blue Flag" );
		if ( !item || !itemRegistered[ item - bg_itemlist ] ) {
			G_Printf( S_COLOR_YELLOW "WARNING: No team_CTF_blueflag in map\n" );
		}
		item = BG_FindItem( "Neutral Flag" );
		if ( !item || !itemRegistered[ item - bg_itemlist ] ) {
			G_Printf( S_COLOR_YELLOW "WARNING: No team_CTF_neutralflag in map\n" );
		}
	}

	if( g_gametype.integer == GT_OBELISK ) {
		gentity_t	*ent;

		// check for the two obelisks
		ent = NULL;
		ent = G_Find( ent, FOFS(classname), "team_redobelisk" );
		if( !ent ) {
			G_Printf( S_COLOR_YELLOW "WARNING: No team_redobelisk in map\n" );
		}

		ent = NULL;
		ent = G_Find( ent, FOFS(classname), "team_blueobelisk" );
		if( !ent ) {
			G_Printf( S_COLOR_YELLOW "WARNING: No team_blueobelisk in map\n" );
		}
	}

	if( g_gametype.integer == GT_HARVESTER ) {
		gentity_t	*ent;

		// check for all three obelisks
		ent = NULL;
		ent = G_Find( ent, FOFS(classname), "team_redobelisk" );
		if( !ent ) {
			G_Printf( S_COLOR_YELLOW "WARNING: No team_redobelisk in map\n" );
		}

		ent = NULL;
		ent = G_Find( ent, FOFS(classname), "team_blueobelisk" );
		if( !ent ) {
			G_Printf( S_COLOR_YELLOW "WARNING: No team_blueobelisk in map\n" );
		}

		ent = NULL;
		ent = G_Find( ent, FOFS(classname), "team_neutralobelisk" );
		if( !ent ) {
			G_Printf( S_COLOR_YELLOW "WARNING: No team_neutralobelisk in map\n" );
		}
	}
#endif
}

/*
==============
ClearRegisteredItems
==============
*/
void ClearRegisteredItems( void ) {
	memset( itemRegistered, 0, sizeof( itemRegistered ) );

	// players always start with the base weapon
	RegisterItem( BG_FindItemForWeapon( WP_MACHINEGUN ) );
	RegisterItem( BG_FindItemForWeapon( WP_GAUNTLET ) );
#ifdef MISSIONPACK
	if( g_gametype.integer == GT_HARVESTER ) {
		RegisterItem( BG_FindItem( "Red Cube" ) );
		RegisterItem( BG_FindItem( "Blue Cube" ) );
	}
#endif
}

/*
===============
RegisterItem

The item will be added to the precache list
===============
*/
void RegisterItem( gitem_t *item ) {
	if ( !item ) {
		G_Error( "RegisterItem: NULL" );
	}
	itemRegistered[ item - bg_itemlist ] = qtrue;
}


/*
===============
SaveRegisteredItems

Write the needed items to a config string
so the client will know which ones to precache
===============
*/
void SaveRegisteredItems( void ) {
	char	string[MAX_ITEMS+1];
	int		i;
	int		count;

	count = 0;
	for ( i = 0 ; i < bg_numItems ; i++ ) {
		if ( itemRegistered[i] ) {
			count++;
			string[i] = '1';
		} else {
			string[i] = '0';
		}
	}
	string[ bg_numItems ] = 0;

	G_Printf( "%i items registered\n", count );
	trap_SetConfigstring(CS_ITEMS, string);
}

/*
============
G_ItemDisabled
============
*/
int G_ItemDisabled( gitem_t *item ) {

	char name[128];

	Com_sprintf(name, sizeof(name), "disable_%s", item->classname);
	return trap_Cvar_VariableIntegerValue( name );
}

/*
============
G_SpawnItem

Sets the clipping size and plants the object on the floor.

Items can't be immediately dropped to floor, because they might
be on an entity that hasn't spawned yet.
============
*/
void G_SpawnItem (gentity_t *ent, gitem_t *item) {
	G_SpawnFloat( "random", "0", &ent->random );
	G_SpawnFloat( "wait", "0", &ent->wait );

	RegisterItem( item );
	if ( G_ItemDisabled(item) )
		return;

	ent->item = item;
	// some movers spawn on the second frame, so delay item
	// spawns until the third frame so they can ride trains
	ent->nextthink = level.time + FRAMETIME * 2;
	ent->think = FinishSpawningItem;

	ent->physicsBounce = 0.50;		// items are bouncy

	if ( item->giType == IT_POWERUP ) {
		G_SoundIndex( "sound/items/poweruprespawn.wav" );
		G_SpawnFloat( "noglobalsound", "0", &ent->speed);
	}

#ifdef MISSIONPACK
	if ( item->giType == IT_PERSISTANT_POWERUP ) {
		ent->s.generic1 = ent->spawnflags;
	}
#endif
}


/*
================
G_BounceItem

================
*/
void G_BounceItem( gentity_t *ent, trace_t *trace ) {
	vec3_t	velocity;
	float	dot;
	int		hitTime;

	// reflect the velocity on the trace plane
	hitTime = level.previousTime + ( level.time - level.previousTime ) * trace->fraction;
	BG_EvaluateTrajectoryDelta( &ent->s.pos, hitTime, velocity );
	dot = DotProduct( velocity, trace->plane.normal );
	VectorMA( velocity, -2*dot, trace->plane.normal, ent->s.pos.trDelta );

	// cut the velocity to keep from bouncing forever
	VectorScale( ent->s.pos.trDelta, ent->physicsBounce, ent->s.pos.trDelta );

	// check for stop
	if ( trace->plane.normal[2] > 0 && ent->s.pos.trDelta[2] < 40 ) {
		trace->endpos[2] += 1.0;	// make sure it is off ground
		SnapVector( trace->endpos );
		G_SetOrigin( ent, trace->endpos );
		ent->s.groundEntityNum = trace->entityNum;
		return;
	}

	VectorAdd( ent->r.currentOrigin, trace->plane.normal, ent->r.currentOrigin);
	VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
	ent->s.pos.trTime = level.time;
}


/*
================
G_RunItem

================
*/
void G_RunItem( gentity_t *ent ) {
	vec3_t		origin;
	trace_t		tr;
	int			contents;
	int			mask;

	// if its groundentity has been set to none, it may have been pushed off an edge
	if ( ent->s.groundEntityNum == ENTITYNUM_NONE ) {
		if ( ent->s.pos.trType != TR_GRAVITY ) {
			ent->s.pos.trType = TR_GRAVITY;
			ent->s.pos.trTime = level.time;
		}
	}

	if ( ent->s.pos.trType == TR_STATIONARY ) {
		// check think function
		G_RunThink( ent );
		return;
	}

	// get current position
	BG_EvaluateTrajectory( &ent->s.pos, level.time, origin );

	// trace a line from the previous position to the current position
	if ( ent->clipmask ) {
		mask = ent->clipmask;
	} else {
		mask = MASK_PLAYERSOLID & ~CONTENTS_BODY;//MASK_SOLID;
	}
	trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, origin, 
		ent->r.ownerNum, mask );

	VectorCopy( tr.endpos, ent->r.currentOrigin );

	if ( tr.startsolid ) {
		tr.fraction = 0;
	}

	trap_LinkEntity( ent );	// FIXME: avoid this for stationary?

	// check think function
	G_RunThink( ent );

	if ( tr.fraction == 1 ) {
		return;
	}

	// if it is in a nodrop volume, remove it
	contents = trap_PointContents( ent->r.currentOrigin, -1 );
	if ( contents & CONTENTS_NODROP ) {
		if (ent->item && ent->item->giType == IT_TEAM) {
			Team_FreeEntity(ent);
		} else {
			G_FreeEntity( ent );
		}
		return;
	}

	G_BounceItem( ent, &tr );
}
