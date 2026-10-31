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

#define	MISSILE_PRESTEP_TIME	50
#define	THROWING_KNIFE_SUPPORT	4.635f	// MW2's resting support trace (0x731bd0)

/*
================
G_BounceMissile

================
*/
void G_BounceMissile( gentity_t *ent, trace_t *trace ) {
	vec3_t	velocity;
	float	dot;
	int		hitTime;

	// reflect the velocity on the trace plane
	hitTime = level.previousTime + ( level.time - level.previousTime ) * trace->fraction;
	BG_EvaluateTrajectoryDelta( &ent->s.pos, hitTime, velocity );
	dot = DotProduct( velocity, trace->plane.normal );
	VectorMA( velocity, -2*dot, trace->plane.normal, ent->s.pos.trDelta );

	if ( ent->s.eFlags & EF_BOUNCE_HALF ) {
		VectorScale( ent->s.pos.trDelta, 0.65, ent->s.pos.trDelta );
		// check for stop
		if ( trace->plane.normal[2] > 0.2 && VectorLength( ent->s.pos.trDelta ) < 40 ) {
			G_SetOrigin( ent, trace->endpos );
			ent->s.time = level.time / 4;
			return;
		}
	}

	VectorAdd( ent->r.currentOrigin, trace->plane.normal, ent->r.currentOrigin);
	VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
	ent->s.pos.trTime = level.time;
}


/*
================
G_ExplodeMissile

Explode a missile without an impact
================
*/
void G_ExplodeMissile( gentity_t *ent ) {
	vec3_t		dir;
	vec3_t		origin;
	gentity_t *attacker = ent->parent;
	if (!attacker || !attacker->inuse || !attacker->client ||
		attacker->client->pers.connected != CON_CONNECTED ||
		(ent->s.weapon == WP_SEMTEX && attacker->client->pers.enterTime != ent->semtexOwnerConnect))
		attacker = &g_entities[ENTITYNUM_WORLD];

	BG_EvaluateTrajectory( &ent->s.pos, level.time, origin );
	SnapVector( origin );
	G_SetOrigin( ent, origin );

	// we don't have a valid direction, so just point straight up
	dir[0] = dir[1] = 0;
	dir[2] = 1;

	ent->s.eType = ET_GENERAL;
	G_AddEvent( ent, EV_MISSILE_MISS, DirToByte( dir ) );

	ent->freeAfterEvent = qtrue;

	// splash damage
	if ( ent->splashDamage ) {
		if( G_RadiusDamage( ent->r.currentOrigin, attacker, ent->splashDamage, ent->splashRadius, ent
			, ent->splashMethodOfDeath ) && attacker->client ) {
			attacker->client->accuracy_hits++;
		}
	}

	trap_LinkEntity( ent );
}


#ifdef MISSIONPACK
/*
================
ProximityMine_Explode
================
*/
static void ProximityMine_Explode( gentity_t *mine ) {
	G_ExplodeMissile( mine );
	// if the prox mine has a trigger free it
	if (mine->activator) {
		G_FreeEntity(mine->activator);
		mine->activator = NULL;
	}
}

/*
================
ProximityMine_Die
================
*/
static void ProximityMine_Die( gentity_t *ent, gentity_t *inflictor, gentity_t *attacker, int damage, int mod ) {
	ent->think = ProximityMine_Explode;
	ent->nextthink = level.time + 1;
}

/*
================
ProximityMine_Trigger
================
*/
void ProximityMine_Trigger( gentity_t *trigger, gentity_t *other, trace_t *trace ) {
	vec3_t		v;
	gentity_t	*mine;

	if( !other->client ) {
		return;
	}

	// trigger is a cube, do a distance test now to act as if it's a sphere
	VectorSubtract( trigger->s.pos.trBase, other->s.pos.trBase, v );
	if( VectorLength( v ) > trigger->parent->splashRadius ) {
		return;
	}


	if ( g_gametype.integer >= GT_TEAM ) {
		// don't trigger same team mines
		if (trigger->parent->s.generic1 == other->client->sess.sessionTeam) {
			return;
		}
	}

	// ok, now check for ability to damage so we don't get triggered through walls, closed doors, etc...
	if( !CanDamage( other, trigger->s.pos.trBase ) ) {
		return;
	}

	// trigger the mine!
	mine = trigger->parent;
	mine->s.loopSound = 0;
	G_AddEvent( mine, EV_PROXIMITY_MINE_TRIGGER, 0 );
	mine->nextthink = level.time + 500;

	G_FreeEntity( trigger );
}

/*
================
ProximityMine_Activate
================
*/
static void ProximityMine_Activate( gentity_t *ent ) {
	gentity_t	*trigger;
	float		r;

	ent->think = ProximityMine_Explode;
	ent->nextthink = level.time + g_proxMineTimeout.integer;

	ent->takedamage = qtrue;
	ent->health = 1;
	ent->die = ProximityMine_Die;

	ent->s.loopSound = G_SoundIndex( "sound/weapons/proxmine/wstbtick.wav" );

	// build the proximity trigger
	trigger = G_Spawn ();

	trigger->classname = "proxmine_trigger";

	r = ent->splashRadius;
	VectorSet( trigger->r.mins, -r, -r, -r );
	VectorSet( trigger->r.maxs, r, r, r );

	G_SetOrigin( trigger, ent->s.pos.trBase );

	trigger->parent = ent;
	trigger->r.contents = CONTENTS_TRIGGER;
	trigger->touch = ProximityMine_Trigger;

	trap_LinkEntity (trigger);

	// set pointer to trigger so the entity can be freed when the mine explodes
	ent->activator = trigger;
}

/*
================
ProximityMine_ExplodeOnPlayer
================
*/
static void ProximityMine_ExplodeOnPlayer( gentity_t *mine ) {
	gentity_t	*player;

	player = mine->enemy;
	player->client->ps.eFlags &= ~EF_TICKING;

	if ( player->client->invulnerabilityTime > level.time ) {
		G_Damage( player, mine->parent, mine->parent, vec3_origin, mine->s.origin, 1000, DAMAGE_NO_KNOCKBACK, MOD_JUICED );
		player->client->invulnerabilityTime = 0;
		G_TempEntity( player->client->ps.origin, EV_JUICED );
	}
	else {
		G_SetOrigin( mine, player->s.pos.trBase );
		// make sure the explosion gets to the client
		mine->r.svFlags &= ~SVF_NOCLIENT;
		mine->splashMethodOfDeath = MOD_PROXIMITY_MINE;
		G_ExplodeMissile( mine );
	}
}

/*
================
ProximityMine_Player
================
*/
static void ProximityMine_Player( gentity_t *mine, gentity_t *player ) {
	if( mine->s.eFlags & EF_NODRAW ) {
		return;
	}

	G_AddEvent( mine, EV_PROXIMITY_MINE_STICK, 0 );

	if( player->s.eFlags & EF_TICKING ) {
		player->activator->splashDamage += mine->splashDamage;
		player->activator->splashRadius *= 1.50;
		mine->think = G_FreeEntity;
		mine->nextthink = level.time;
		return;
	}

	player->client->ps.eFlags |= EF_TICKING;
	player->activator = mine;

	mine->s.eFlags |= EF_NODRAW;
	mine->r.svFlags |= SVF_NOCLIENT;
	mine->s.pos.trType = TR_LINEAR;
	VectorClear( mine->s.pos.trDelta );

	mine->enemy = player;
	mine->think = ProximityMine_ExplodeOnPlayer;
	if ( player->client->invulnerabilityTime > level.time ) {
		mine->nextthink = level.time + 2 * 1000;
	}
	else {
		mine->nextthink = level.time + 10 * 1000;
	}
}
#endif

/*
================
G_MissileImpact
================
*/
static void G_HostAxis(gentity_t *host, vec3_t axis[3]) {
	vec3_t angles;
	VectorCopy(host->r.currentAngles, angles);
	if (host->client) VectorSet(angles, 0, host->client->ps.viewangles[YAW], 0);
	AnglesToAxis(angles, axis);
}

/*
================
G_CarryMissile

The host carries a stopped missile from where it is now: its offset, surface
normal (movedir) and facing are kept in the host's own axis, with the host's
identity, so it rides only the host it was given.
================
*/
static void G_CarryMissile(gentity_t *ent, gentity_t *host, const vec3_t forward) {
	vec3_t offset, axis[3];
	int i;
	ent->enemy = host;
	ent->hostFreeTime = host->freetime;
	ent->hostSpawn = host->client ? host->client->ps.persistant[PERS_SPAWN_COUNT] : 0;
	ent->hostConnect = host->client ? host->client->pers.enterTime : 0;
	G_HostAxis(host, axis);
	VectorSubtract(ent->r.currentOrigin, host->r.currentOrigin, offset);
	for (i=0; i<3; i++) {
		ent->pos1[i] = DotProduct(offset, axis[i]);
		ent->pos2[i] = DotProduct(ent->movedir, axis[i]);
		ent->hostForward[i] = DotProduct(forward, axis[i]);
	}
}

/*
================
G_FollowHost

Where a carried missile now is on its host, with its normal (movedir) and
facing. Once that host is gone - freed, disconnected or respawned - it is let
go and stays where it was last placed.
================
*/
static qboolean G_FollowHost(gentity_t *ent, vec3_t origin, vec3_t forward) {
	gentity_t *host = ent->enemy;
	vec3_t axis[3];
	int i;
	if (!host) return qfalse;
	if (!host->inuse || host->freetime != ent->hostFreeTime ||
		(host->client && (host->client->pers.connected != CON_CONNECTED ||
		host->client->ps.persistant[PERS_SPAWN_COUNT] != ent->hostSpawn ||
		host->client->pers.enterTime != ent->hostConnect))) {
		ent->enemy = NULL; // remain at last contact, never teleport to a respawn
		return qfalse;
	}
	G_HostAxis(host, axis);
	VectorCopy(host->r.currentOrigin, origin);
	VectorClear(ent->movedir);
	VectorClear(forward);
	for (i=0; i<3; i++) {
		VectorMA(origin, ent->pos1[i], axis[i], origin);
		VectorMA(ent->movedir, ent->pos2[i], axis[i], ent->movedir);
		VectorMA(forward, ent->hostForward[i], axis[i], forward);
	}
	return qtrue;
}

static void SemtexStick(gentity_t *ent, trace_t *trace, gentity_t *host) {
	vec3_t origin;
	VectorMA(trace->endpos, 1.0f, trace->plane.normal, origin);
	G_SetOrigin(ent, origin);
	VectorClear(ent->s.pos.trDelta);
	VectorCopy(trace->plane.normal, ent->movedir);
	vectoangles(ent->movedir, ent->s.angles);
	ent->s.angles[PITCH] += 90;
	if (host->inuse && (host->client || host->s.eType == ET_MOVER))
		G_CarryMissile(ent, host, ent->movedir);
	// The fuse began on release, not contact. No impact damage or bounce.
	trap_LinkEntity(ent);
}

static void SemtexFollow(gentity_t *ent) {
	vec3_t origin, forward;
	if (!G_FollowHost(ent, origin, forward)) return;
	G_SetOrigin(ent, origin);
	vectoangles(ent->movedir, ent->s.angles);
	ent->s.angles[PITCH] += 90;
	trap_LinkEntity(ent);
}

/*
================
ThrowingKnifePlace

A stopped knife at origin, facing angles, with no velocity of its own: at rest
(TR_STATIONARY), or re-placed on its carrier each frame (TR_INTERPOLATE, as
MW2's 0x43B9D0 does).
================
*/
static void ThrowingKnifePlace( gentity_t *ent, const vec3_t origin, const vec3_t angles, trType_t trType ) {
	VectorCopy( origin, ent->s.pos.trBase );
	VectorCopy( origin, ent->s.origin );
	VectorCopy( origin, ent->r.currentOrigin );
	VectorClear( ent->s.pos.trDelta );
	ent->s.pos.trType = trType;
	ent->s.pos.trTime = 0;
	ent->s.pos.trDuration = 0;
	VectorCopy( angles, ent->s.apos.trBase );
	VectorCopy( angles, ent->s.angles );
	VectorCopy( angles, ent->r.currentAngles );
	VectorClear( ent->s.apos.trDelta );
	ent->s.apos.trType = trType;
	ent->s.apos.trTime = 0;
	ent->s.apos.trDuration = 0;
	trap_LinkEntity( ent );
}

/*
================
ThrowingKnifeLetGo

The carrier lets go: the knife stays where it is, at rest on what it was in,
and falls from there once nothing is behind it (ThrowingKnifeAtRest).
================
*/
static void ThrowingKnifeLetGo( gentity_t *ent ) {
	ent->enemy = NULL;
	ThrowingKnifePlace( ent, ent->r.currentOrigin, ent->r.currentAngles, TR_STATIONARY );
}

/*
================
ThrowingKnifeImpact

MW2's MissileImpact for the throwing knife ('Knife' stickiness). It hurts only
above g_minGrenadeDamageSpeed. A player it hits carries it (stickToPlayers):
it is buried at the hit as MW2's bury branch does (0x5E53E0) and rides them,
and their corpse after them. Off anything else the hit velocity is reflected
and scaled; the knife then sticks if it came in head-on (the reflection within
~45.6 degrees of the surface normal) or has settled on a floor, and otherwise
bounces. A stopped knife stays until it is picked back up.
================
*/
static void ThrowingKnifeImpact( gentity_t *ent, trace_t *trace, gentity_t *other ) {
	vec3_t	velocity, dir, bounced, origin, angles, forward;
	float	speed, dot;
	int		hitTime = level.previousTime + ( level.time - level.previousTime ) * trace->fraction;

	BG_EvaluateTrajectoryDelta( &ent->s.pos, hitTime, velocity );
	if ( other->takedamage && VectorLength( velocity ) > BG_THROWING_KNIFE_HIT_SPEED ) {
		VectorCopy( velocity, dir );	// G_Damage normalizes its direction in place
		G_Damage( other, ent, ent->parent ? ent->parent : ent, dir, trace->endpos,
			ent->damage, 0, ent->methodOfDeath );
	}
	VectorCopy( trace->plane.normal, ent->movedir );
	ent->restNum = trace->entityNum;
	if ( other->client ) {
		// buried: pitched in by the surface, 1.5 in and 4.5 back along itself
		BG_EvaluateTrajectory( &ent->s.apos, hitTime, angles );
		angles[PITCH] = RAD2DEG( atan2( trace->plane.normal[2], sqrt( trace->plane.normal[0] * trace->plane.normal[0] +
			trace->plane.normal[1] * trace->plane.normal[1] ) ) ) + random() * 30 - 15;
		AngleVectors( angles, forward, NULL, NULL );
		VectorMA( trace->endpos, -1.5f, trace->plane.normal, origin );
		VectorMA( origin, -4.5f, forward, origin );
		ThrowingKnifePlace( ent, origin, angles, TR_INTERPOLATE );
		G_CarryMissile( ent, other, forward );
		ent->nextthink = 0;
		ent->think = 0;
		return;
	}
	dot = DotProduct( velocity, trace->plane.normal );
	VectorMA( velocity, -2 * dot, trace->plane.normal, bounced );
	VectorScale( bounced, BG_THROWING_KNIFE_BOUNCE, bounced );
	speed = VectorLength( bounced );
	if ( ( trace->plane.normal[2] > BG_THROWING_KNIFE_FLOOR_Z && speed < BG_THROWING_KNIFE_REST_SPEED ) ||
	     ( speed > 0 && DotProduct( trace->plane.normal, bounced ) / speed > BG_THROWING_KNIFE_STICK_DOT ) ) {
		VectorMA( trace->endpos, 1.0f, trace->plane.normal, origin );
		vectoangles( velocity, angles );		// buried along the throw
		ThrowingKnifePlace( ent, origin, angles, TR_STATIONARY );
		ent->nextthink = 0;
		ent->think = 0;
		return;
	}
	VectorCopy( bounced, ent->s.pos.trDelta );
	VectorMA( trace->endpos, 0.1f, trace->plane.normal, ent->r.currentOrigin );
	VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
	ent->s.pos.trTime = level.time;
	// MW2 sends no impact event for a trace that started in solid (5e65f1)
	if ( !trace->startsolid ) G_AddEvent( ent, EV_GRENADE_BOUNCE, 0 );
}

/*
================
ThrowingKnifeAtRest

A stopped knife, each frame (MW2's G_RunMissile 0x5E8BE0, 5e8c64-5e8d48). One
carried by a player or corpse is re-placed on it. Any other stays where it is
unless it rests on something other than the world with nothing
THROWING_KNIFE_SUPPORT behind it - a mover that moved off, a corpse that sank
or was reused, a carrier that let go - and then it falls from there. Nothing
it touches moves it. The thrower is never in the way.
================
*/
static qboolean ThrowingKnifeAtRest( gentity_t *ent ) {
	vec3_t	origin, forward, angles, end;
	trace_t	tr;
	if ( G_FollowHost( ent, origin, forward ) ) {
		vectoangles( forward, angles );
		ThrowingKnifePlace( ent, origin, angles, TR_INTERPOLATE );
		return qtrue;
	}
	if ( ent->s.pos.trType != TR_STATIONARY ) ThrowingKnifeLetGo( ent );
	if ( ent->restNum == ENTITYNUM_WORLD ) return qtrue;
	VectorMA( ent->r.currentOrigin, -THROWING_KNIFE_SUPPORT, ent->movedir, end );
	trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, end, ent->r.ownerNum, ent->clipmask );
	if ( tr.fraction != 1 ) return qtrue;
	ent->s.pos.trType = TR_GRAVITY;
	ent->s.pos.trTime = level.time;
	return qfalse;
}

/*
================
G_MoveCarriedMissiles

MW2's ClonePlayer (0x5DE520) and the body queue's reuse: every throwing knife
from carries moves, where it is, to to - the corpse - or with no to is let go.
A Semtex keeps its own rule and stays at its last contact.
================
*/
void G_MoveCarriedMissiles( gentity_t *from, gentity_t *to ) {
	vec3_t	origin, forward, angles;
	int		i;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *knife = &g_entities[i];
		if ( !knife->inuse || knife->s.eType != ET_MISSILE || knife->s.weapon != WP_THROWING_KNIFE ||
			knife->enemy != from || !G_FollowHost( knife, origin, forward ) ) continue;
		vectoangles( forward, angles );
		ThrowingKnifePlace( knife, origin, angles, TR_INTERPOLATE );
		if ( to ) G_CarryMissile( knife, to, forward );
		else ThrowingKnifeLetGo( knife );
	}
}

void G_MissileImpact( gentity_t *ent, trace_t *trace ) {
	gentity_t		*other;
	qboolean		hitClient = qfalse;
#ifdef MISSIONPACK
	vec3_t			forward, impactpoint, bouncedir;
	int				eFlags;
#endif
	other = &g_entities[trace->entityNum];
	if (ent->s.weapon == WP_SEMTEX) {
		SemtexStick(ent, trace, other);
		return;
	}
	if (ent->s.weapon == WP_THROWING_KNIFE) {
		ThrowingKnifeImpact(ent, trace, other);
		return;
	}

	// check for bounce
	if ( !other->takedamage &&
		( ent->s.eFlags & ( EF_BOUNCE | EF_BOUNCE_HALF ) ) ) {
		G_BounceMissile( ent, trace );
		G_AddEvent( ent, EV_GRENADE_BOUNCE, 0 );
		return;
	}

#ifdef MISSIONPACK
	if ( other->takedamage ) {
		if ( ent->s.weapon != WP_PROX_LAUNCHER ) {
			if ( other->client && other->client->invulnerabilityTime > level.time ) {
				//
				VectorCopy( ent->s.pos.trDelta, forward );
				VectorNormalize( forward );
				if (G_InvulnerabilityEffect( other, forward, ent->s.pos.trBase, impactpoint, bouncedir )) {
					VectorCopy( bouncedir, trace->plane.normal );
					eFlags = ent->s.eFlags & EF_BOUNCE_HALF;
					ent->s.eFlags &= ~EF_BOUNCE_HALF;
					G_BounceMissile( ent, trace );
					ent->s.eFlags |= eFlags;
				}
				ent->target_ent = other;
				return;
			}
		}
	}
#endif
	// impact damage
	if (other->takedamage) {
		// FIXME: wrong damage direction?
		if ( ent->damage ) {
			vec3_t	velocity;

			if( LogAccuracyHit( other, &g_entities[ent->r.ownerNum] ) ) {
				g_entities[ent->r.ownerNum].client->accuracy_hits++;
				hitClient = qtrue;
			}
			BG_EvaluateTrajectoryDelta( &ent->s.pos, level.time, velocity );
			if ( VectorLength( velocity ) == 0 ) {
				velocity[2] = 1;	// stepped on a grenade
			}
			G_Damage (other, ent, &g_entities[ent->r.ownerNum], velocity,
				ent->s.origin, ent->damage, 
				0, ent->methodOfDeath);
		}
	}

#ifdef MISSIONPACK
	if( ent->s.weapon == WP_PROX_LAUNCHER ) {
		if( ent->s.pos.trType != TR_GRAVITY ) {
			return;
		}

		// if it's a player, stick it on to them (flag them and remove this entity)
		if( other->s.eType == ET_PLAYER && other->health > 0 ) {
			ProximityMine_Player( ent, other );
			return;
		}

		SnapVectorTowards( trace->endpos, ent->s.pos.trBase );
		G_SetOrigin( ent, trace->endpos );
		ent->s.pos.trType = TR_STATIONARY;
		VectorClear( ent->s.pos.trDelta );

		G_AddEvent( ent, EV_PROXIMITY_MINE_STICK, trace->surfaceFlags );

		ent->think = ProximityMine_Activate;
		ent->nextthink = level.time + 2000;

		vectoangles( trace->plane.normal, ent->s.angles );
		ent->s.angles[0] += 90;

		// link the prox mine to the other entity
		ent->enemy = other;
		ent->die = ProximityMine_Die;
		VectorCopy(trace->plane.normal, ent->movedir);
		VectorSet(ent->r.mins, -4, -4, -4);
		VectorSet(ent->r.maxs, 4, 4, 4);
		trap_LinkEntity(ent);

		return;
	}
#endif

	if (!strcmp(ent->classname, "hook")) {
		gentity_t *nent;
		vec3_t v;

		nent = G_Spawn();
		if ( other->takedamage && other->client ) {

			G_AddEvent( nent, EV_MISSILE_HIT, DirToByte( trace->plane.normal ) );
			nent->s.otherEntityNum = other->s.number;

			ent->enemy = other;

			v[0] = other->r.currentOrigin[0] + (other->r.mins[0] + other->r.maxs[0]) * 0.5;
			v[1] = other->r.currentOrigin[1] + (other->r.mins[1] + other->r.maxs[1]) * 0.5;
			v[2] = other->r.currentOrigin[2] + (other->r.mins[2] + other->r.maxs[2]) * 0.5;

			SnapVectorTowards( v, ent->s.pos.trBase );	// save net bandwidth
		} else {
			VectorCopy(trace->endpos, v);
			G_AddEvent( nent, EV_MISSILE_MISS, DirToByte( trace->plane.normal ) );
			ent->enemy = NULL;
		}

		SnapVectorTowards( v, ent->s.pos.trBase );	// save net bandwidth

		nent->freeAfterEvent = qtrue;
		// change over to a normal entity right at the point of impact
		nent->s.eType = ET_GENERAL;
		ent->s.eType = ET_GRAPPLE;

		G_SetOrigin( ent, v );
		G_SetOrigin( nent, v );

		ent->think = Weapon_HookThink;
		ent->nextthink = level.time + FRAMETIME;

		ent->parent->client->ps.pm_flags |= PMF_GRAPPLE_PULL;
		VectorCopy( ent->r.currentOrigin, ent->parent->client->ps.grapplePoint);

		trap_LinkEntity( ent );
		trap_LinkEntity( nent );

		return;
	}

	// is it cheaper in bandwidth to just remove this ent and create a new
	// one, rather than changing the missile into the explosion?

	if ( other->takedamage && other->client ) {
		G_AddEvent( ent, EV_MISSILE_HIT, DirToByte( trace->plane.normal ) );
		ent->s.otherEntityNum = other->s.number;
	} else if( trace->surfaceFlags & SURF_METALSTEPS ) {
		G_AddEvent( ent, EV_MISSILE_MISS_METAL, DirToByte( trace->plane.normal ) );
	} else {
		G_AddEvent( ent, EV_MISSILE_MISS, DirToByte( trace->plane.normal ) );
	}

	ent->freeAfterEvent = qtrue;

	// change over to a normal entity right at the point of impact
	ent->s.eType = ET_GENERAL;

	SnapVectorTowards( trace->endpos, ent->s.pos.trBase );	// save net bandwidth

	G_SetOrigin( ent, trace->endpos );

	// splash damage (doesn't apply to person directly hit)
	if ( ent->splashDamage ) {
		if( G_RadiusDamage( trace->endpos, ent->parent, ent->splashDamage, ent->splashRadius, 
			other, ent->splashMethodOfDeath ) ) {
			if( !hitClient ) {
				g_entities[ent->r.ownerNum].client->accuracy_hits++;
			}
		}
	}

	trap_LinkEntity( ent );
}

/*
================
G_RunMissile
================
*/
void G_RunMissile( gentity_t *ent ) {
	vec3_t		origin;
	trace_t		tr;
	int			passent;
	if (ent->s.weapon == WP_SEMTEX && ent->s.pos.trType == TR_STATIONARY) {
		SemtexFollow(ent);
		G_RunThink(ent);
		return;
	}
	if (ent->s.weapon == WP_THROWING_KNIFE && ent->s.pos.trType != TR_GRAVITY && ThrowingKnifeAtRest(ent)) {
		G_RunThink(ent);
		return;
	}

	// get current position
	BG_EvaluateTrajectory( &ent->s.pos, level.time, origin );

	// if this missile bounced off an invulnerability sphere
	if ( ent->target_ent ) {
		passent = ent->target_ent->s.number;
	}
#ifdef MISSIONPACK
	// prox mines that left the owner bbox will attach to anything, even the owner
	else if (ent->s.weapon == WP_PROX_LAUNCHER && ent->count) {
		passent = ENTITYNUM_NONE;
	}
#endif
	else {
		// ignore interactions with the missile owner
		passent = ent->r.ownerNum;
	}
	// trace a line from the previous position to the current position
	trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, origin, passent, ent->clipmask );

	if ( tr.startsolid || tr.allsolid ) {
		// make sure the tr.entityNum is set to the entity we're stuck in
		trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, ent->r.currentOrigin, passent, ent->clipmask );
		tr.fraction = 0;
	}
	else {
		VectorCopy( tr.endpos, ent->r.currentOrigin );
	}

	trap_LinkEntity( ent );

	if ( tr.fraction != 1 ) {
		// never explode or bounce on sky
		if ( tr.surfaceFlags & SURF_NOIMPACT ) {
			// If grapple, reset owner
			if (ent->parent && ent->parent->client && ent->parent->client->hook == ent) {
				ent->parent->client->hook = NULL;
			}
			G_FreeEntity( ent );
			return;
		}
		G_MissileImpact( ent, &tr );
		if ( ent->s.eType != ET_MISSILE ) {
			return;		// exploded
		}
	}
#ifdef MISSIONPACK
	// if the prox mine wasn't yet outside the player body
	if (ent->s.weapon == WP_PROX_LAUNCHER && !ent->count) {
		// check if the prox mine is outside the owner bbox
		trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, ent->r.currentOrigin, ENTITYNUM_NONE, ent->clipmask );
		if (!tr.startsolid || tr.entityNum != ent->r.ownerNum) {
			ent->count = 1;
		}
	}
#endif
	// check think function after bouncing
	G_RunThink( ent );
}


//=============================================================================

/*
=================
fire_plasma

=================
*/
gentity_t *fire_plasma (gentity_t *self, vec3_t start, vec3_t dir) {
	gentity_t	*bolt;

	VectorNormalize (dir);

	bolt = G_Spawn();
	bolt->classname = "plasma";
	bolt->nextthink = level.time + 10000;
	bolt->think = G_ExplodeMissile;
	bolt->s.eType = ET_MISSILE;
	bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN;
	bolt->s.weapon = WP_PLASMAGUN;
	bolt->r.ownerNum = self->s.number;
	bolt->parent = self;
	bolt->damage = 20;
	bolt->splashDamage = 15;
	bolt->splashRadius = 20;
	bolt->methodOfDeath = MOD_PLASMA;
	bolt->splashMethodOfDeath = MOD_PLASMA_SPLASH;
	bolt->clipmask = MASK_SHOT;
	bolt->target_ent = NULL;

	bolt->s.pos.trType = TR_LINEAR;
	bolt->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;		// move a bit on the very first frame
	VectorCopy( start, bolt->s.pos.trBase );
	VectorScale( dir, 2000, bolt->s.pos.trDelta );
	SnapVector( bolt->s.pos.trDelta );			// save net bandwidth

	VectorCopy (start, bolt->r.currentOrigin);

	return bolt;
}	

//=============================================================================


/*
=================
fire_grenade
=================
*/
#define SEMTEX_FUSE_TIME 2500

gentity_t *fire_semtex(gentity_t *self, vec3_t start, vec3_t dir) {
	gentity_t *bolt = fire_grenade(self, start, dir);
	bolt->classname = "semtex";
	bolt->s.weapon = WP_SEMTEX;
	bolt->s.eFlags = 0;
	bolt->damage = 0;
	bolt->nextthink = level.time + SEMTEX_FUSE_TIME;
	bolt->semtexOwnerConnect = self->client ? self->client->pers.enterTime : 0;
	return bolt;
}

/*
=================
fire_throwingknife

MW2's G_FireGrenade for weapClass 9: thrown along the view at the knife's
speed plus the thrower's own speed along it, falling under gravity and
spinning end over end (apos) from 120 degrees back. It never explodes: in
flight it lives BG_THROWING_KNIFE_LIFE, and once stopped it stays until a
pickup frees it - or, the oldest of BG_THROWING_KNIFE_WORLD_MAX out, another is thrown.
=================
*/
gentity_t *fire_throwingknife(gentity_t *self, vec3_t start, vec3_t dir) {
	gentity_t	*bolt, *oldest = NULL;
	vec3_t		angles;
	int			i, count = 0;

	// Peace's bound, not MW2's: the exe never frees a stopped knife, and any
	// cleanup it has is script. The oldest goes, freed like any other.
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *knife = &g_entities[i];
		if ( !knife->inuse || knife->s.eType != ET_MISSILE || knife->s.weapon != WP_THROWING_KNIFE ) continue;
		count++;
		if ( !oldest || knife->timestamp < oldest->timestamp ) oldest = knife;
	}
	if ( count >= BG_THROWING_KNIFE_WORLD_MAX ) G_FreeEntity( oldest );

	VectorNormalize( dir );
	bolt = G_Spawn();
	bolt->classname = "throwingknife";
	bolt->nextthink = level.time + BG_THROWING_KNIFE_LIFE;
	bolt->think = G_FreeEntity;
	bolt->s.eType = ET_MISSILE;
	bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN;
	bolt->s.weapon = WP_THROWING_KNIFE;
	bolt->r.ownerNum = self->s.number;
	bolt->parent = self;
	bolt->damage = BG_THROWING_KNIFE_DAMAGE;
	bolt->methodOfDeath = MOD_GAUNTLET;
	bolt->clipmask = MASK_SHOT;
	bolt->target_ent = NULL;
	bolt->timestamp = level.time;		// BG_USE_SPAWN_DELAY counts from here

	bolt->s.pos.trType = TR_GRAVITY;
	bolt->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;
	VectorCopy( start, bolt->s.pos.trBase );
	VectorScale( dir, BG_THROWING_KNIFE_SPEED, bolt->s.pos.trDelta );
	if ( self->client ) {
		VectorMA( bolt->s.pos.trDelta, DotProduct( self->client->ps.velocity, dir ), dir, bolt->s.pos.trDelta );
	}
	SnapVector( bolt->s.pos.trDelta );
	VectorCopy( start, bolt->r.currentOrigin );

	vectoangles( dir, angles );
	angles[PITCH] = AngleNormalize180( angles[PITCH] - 120 );
	bolt->s.apos.trType = TR_LINEAR;
	bolt->s.apos.trTime = level.time;
	VectorCopy( angles, bolt->s.apos.trBase );
	VectorSet( bolt->s.apos.trDelta, BG_THROWING_KNIFE_SPIN, 0, 0 );
	return bolt;
}

gentity_t *fire_grenade (gentity_t *self, vec3_t start, vec3_t dir) {
	gentity_t	*bolt;

	VectorNormalize (dir);

	bolt = G_Spawn();
	bolt->classname = "grenade";
	bolt->nextthink = level.time + 2500;
	bolt->think = G_ExplodeMissile;
	bolt->s.eType = ET_MISSILE;
	bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN;
	bolt->s.weapon = WP_GRENADE_LAUNCHER;
	bolt->s.eFlags = EF_BOUNCE_HALF;
	bolt->r.ownerNum = self->s.number;
	bolt->parent = self;
	bolt->damage = 100;
	bolt->splashDamage = 100;
	bolt->splashRadius = 150;
	bolt->methodOfDeath = MOD_GRENADE;
	bolt->splashMethodOfDeath = MOD_GRENADE_SPLASH;
	bolt->clipmask = MASK_SHOT;
	bolt->target_ent = NULL;

	bolt->s.pos.trType = TR_GRAVITY;
	bolt->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;		// move a bit on the very first frame
	VectorCopy( start, bolt->s.pos.trBase );
	VectorScale( dir, 700, bolt->s.pos.trDelta );
	SnapVector( bolt->s.pos.trDelta );			// save net bandwidth

	VectorCopy (start, bolt->r.currentOrigin);

	return bolt;
}

//=============================================================================


/*
=================
fire_bfg
=================
*/
gentity_t *fire_bfg (gentity_t *self, vec3_t start, vec3_t dir) {
	gentity_t	*bolt;

	VectorNormalize (dir);

	bolt = G_Spawn();
	bolt->classname = "bfg";
	bolt->nextthink = level.time + 10000;
	bolt->think = G_ExplodeMissile;
	bolt->s.eType = ET_MISSILE;
	bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN;
	bolt->s.weapon = WP_BFG;
	bolt->r.ownerNum = self->s.number;
	bolt->parent = self;
	bolt->damage = 100;
	bolt->splashDamage = 100;
	bolt->splashRadius = 120;
	bolt->methodOfDeath = MOD_BFG;
	bolt->splashMethodOfDeath = MOD_BFG_SPLASH;
	bolt->clipmask = MASK_SHOT;
	bolt->target_ent = NULL;

	bolt->s.pos.trType = TR_LINEAR;
	bolt->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;		// move a bit on the very first frame
	VectorCopy( start, bolt->s.pos.trBase );
	VectorScale( dir, 2000, bolt->s.pos.trDelta );
	SnapVector( bolt->s.pos.trDelta );			// save net bandwidth
	VectorCopy (start, bolt->r.currentOrigin);

	return bolt;
}

//=============================================================================


/*
=================
fire_rocket
=================
*/
gentity_t *fire_rocket (gentity_t *self, vec3_t start, vec3_t dir) {
	gentity_t	*bolt;

	VectorNormalize (dir);

	bolt = G_Spawn();
	bolt->classname = "rocket";
	bolt->nextthink = level.time + 15000;
	bolt->think = G_ExplodeMissile;
	bolt->s.eType = ET_MISSILE;
	bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN;
	bolt->s.weapon = WP_ROCKET_LAUNCHER;
	bolt->r.ownerNum = self->s.number;
	bolt->parent = self;
	bolt->damage = 100;
	bolt->splashDamage = 100;
	bolt->splashRadius = 120;
	bolt->methodOfDeath = MOD_ROCKET;
	bolt->splashMethodOfDeath = MOD_ROCKET_SPLASH;
	bolt->clipmask = MASK_SHOT;
	bolt->target_ent = NULL;

	bolt->s.pos.trType = TR_LINEAR;
	bolt->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;		// move a bit on the very first frame
	VectorCopy( start, bolt->s.pos.trBase );
	VectorScale( dir, 900, bolt->s.pos.trDelta );
	SnapVector( bolt->s.pos.trDelta );			// save net bandwidth
	VectorCopy (start, bolt->r.currentOrigin);

	return bolt;
}

/*
=================
fire_grapple
=================
*/
gentity_t *fire_grapple (gentity_t *self, vec3_t start, vec3_t dir) {
	gentity_t	*hook;

	VectorNormalize (dir);

	hook = G_Spawn();
	hook->classname = "hook";
	hook->nextthink = level.time + 10000;
	hook->think = Weapon_HookFree;
	hook->s.eType = ET_MISSILE;
	hook->r.svFlags = SVF_USE_CURRENT_ORIGIN;
	hook->s.weapon = WP_GRAPPLING_HOOK;
	hook->r.ownerNum = self->s.number;
	hook->methodOfDeath = MOD_GRAPPLE;
	hook->clipmask = MASK_SHOT;
	hook->parent = self;
	hook->target_ent = NULL;

	hook->s.pos.trType = TR_LINEAR;
	hook->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;		// move a bit on the very first frame
	hook->s.otherEntityNum = self->s.number; // use to match beam in client
	VectorCopy( start, hook->s.pos.trBase );
	VectorScale( dir, 800, hook->s.pos.trDelta );
	SnapVector( hook->s.pos.trDelta );			// save net bandwidth
	VectorCopy (start, hook->r.currentOrigin);

	self->client->hook = hook;

	return hook;
}


#ifdef MISSIONPACK
/*
=================
fire_nail
=================
*/
#define NAILGUN_SPREAD	500

gentity_t *fire_nail( gentity_t *self, vec3_t start, vec3_t forward, vec3_t right, vec3_t up ) {
	gentity_t	*bolt;
	vec3_t		dir;
	vec3_t		end;
	float		r, u, scale;

	bolt = G_Spawn();
	bolt->classname = "nail";
	bolt->nextthink = level.time + 10000;
	bolt->think = G_ExplodeMissile;
	bolt->s.eType = ET_MISSILE;
	bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN;
	bolt->s.weapon = WP_NAILGUN;
	bolt->r.ownerNum = self->s.number;
	bolt->parent = self;
	bolt->damage = 20;
	bolt->methodOfDeath = MOD_NAIL;
	bolt->clipmask = MASK_SHOT;
	bolt->target_ent = NULL;

	bolt->s.pos.trType = TR_LINEAR;
	bolt->s.pos.trTime = level.time;
	VectorCopy( start, bolt->s.pos.trBase );

	r = random() * M_PI * 2.0f;
	u = sin(r) * crandom() * NAILGUN_SPREAD * 16;
	r = cos(r) * crandom() * NAILGUN_SPREAD * 16;
	VectorMA( start, 8192 * 16, forward, end);
	VectorMA (end, r, right, end);
	VectorMA (end, u, up, end);
	VectorSubtract( end, start, dir );
	VectorNormalize( dir );

	scale = 555 + random() * 1800;
	VectorScale( dir, scale, bolt->s.pos.trDelta );
	SnapVector( bolt->s.pos.trDelta );

	VectorCopy( start, bolt->r.currentOrigin );

	return bolt;
}	


/*
=================
fire_prox
=================
*/
gentity_t *fire_prox( gentity_t *self, vec3_t start, vec3_t dir ) {
	gentity_t	*bolt;

	VectorNormalize (dir);

	bolt = G_Spawn();
	bolt->classname = "prox mine";
	bolt->nextthink = level.time + 3000;
	bolt->think = G_ExplodeMissile;
	bolt->s.eType = ET_MISSILE;
	bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN;
	bolt->s.weapon = WP_PROX_LAUNCHER;
	bolt->s.eFlags = 0;
	bolt->r.ownerNum = self->s.number;
	bolt->parent = self;
	bolt->damage = 0;
	bolt->splashDamage = 100;
	bolt->splashRadius = 150;
	bolt->methodOfDeath = MOD_PROXIMITY_MINE;
	bolt->splashMethodOfDeath = MOD_PROXIMITY_MINE;
	bolt->clipmask = MASK_SHOT;
	bolt->target_ent = NULL;
	// count is used to check if the prox mine left the player bbox
	// if count == 1 then the prox mine left the player bbox and can attack to it
	bolt->count = 0;

	//FIXME: we prolly wanna abuse another field
	bolt->s.generic1 = self->client->sess.sessionTeam;

	bolt->s.pos.trType = TR_GRAVITY;
	bolt->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;		// move a bit on the very first frame
	VectorCopy( start, bolt->s.pos.trBase );
	VectorScale( dir, 700, bolt->s.pos.trDelta );
	SnapVector( bolt->s.pos.trDelta );			// save net bandwidth

	VectorCopy (start, bolt->r.currentOrigin);

	return bolt;
}
#endif
