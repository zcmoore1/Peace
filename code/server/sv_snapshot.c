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

#include "server.h"


/*
=============================================================================

Delta encode a client frame onto the network channel

A normal server packet will look like:

4	sequence number (high bit set if an oversize fragment)
<optional reliable commands>
1	svc_snapshot
4	last client reliable command
4	serverTime
1	lastframe for delta compression
1	snapFlags
1	areaBytes
<areabytes>
<playerstate>
<packetentities>

=============================================================================
*/

/*
=============
SV_EmitPacketEntities

Writes a delta update of an entityState_t list to the message.
=============
*/
static void SV_EmitPacketEntities( clientSnapshot_t *from, clientSnapshot_t *to, msg_t *msg ) {
	entityState_t	*oldent, *newent;
	int		oldindex, newindex;
	int		oldnum, newnum;
	int		from_num_entities;

	// generate the delta update
	if ( !from ) {
		from_num_entities = 0;
	} else {
		from_num_entities = from->num_entities;
	}

	newent = NULL;
	oldent = NULL;
	newindex = 0;
	oldindex = 0;
	while ( newindex < to->num_entities || oldindex < from_num_entities ) {
		if ( newindex >= to->num_entities ) {
			newnum = 9999;
		} else {
			newent = &svs.snapshotEntities[(to->first_entity+newindex) % svs.numSnapshotEntities];
			newnum = newent->number;
		}

		if ( oldindex >= from_num_entities ) {
			oldnum = 9999;
		} else {
			oldent = &svs.snapshotEntities[(from->first_entity+oldindex) % svs.numSnapshotEntities];
			oldnum = oldent->number;
		}

		if ( newnum == oldnum ) {
			// delta update from old position
			// because the force parm is qfalse, this will not result
			// in any bytes being emitted if the entity has not changed at all
			MSG_WriteDeltaEntity (msg, oldent, newent, qfalse );
			oldindex++;
			newindex++;
			continue;
		}

		if ( newnum < oldnum ) {
			// this is a new entity, send it from the baseline
			MSG_WriteDeltaEntity (msg, &sv.svEntities[newnum].baseline, newent, qtrue );
			newindex++;
			continue;
		}

		if ( newnum > oldnum ) {
			// the old entity isn't present in the new message
			MSG_WriteDeltaEntity (msg, oldent, NULL, qtrue );
			oldindex++;
			continue;
		}
	}

	MSG_WriteBits( msg, (MAX_GENTITIES-1), GENTITYNUM_BITS );	// end of packetentities
}



/*
==================
SV_WriteSnapshotToClient
==================
*/
static void SV_WriteSnapshotToClient( client_t *client, msg_t *msg ) {
	clientSnapshot_t	*frame, *oldframe;
	int					lastframe;
	int					i;
	int					snapFlags;

	// this is the snapshot we are creating
	frame = &client->frames[ client->netchan.outgoingSequence & PACKET_MASK ];

	// try to use a previous frame as the source for delta compressing the snapshot
	if ( client->deltaMessage <= 0 || client->state != CS_ACTIVE ) {
		// client is asking for a retransmit
		oldframe = NULL;
		lastframe = 0;
	} else if ( client->netchan.outgoingSequence - client->deltaMessage 
		>= (PACKET_BACKUP - 3) ) {
		// client hasn't gotten a good message through in a long time
		Com_DPrintf ("%s: Delta request from out of date packet.\n", client->name);
		oldframe = NULL;
		lastframe = 0;
	} else {
		// we have a valid snapshot to delta from
		oldframe = &client->frames[ client->deltaMessage & PACKET_MASK ];
		lastframe = client->netchan.outgoingSequence - client->deltaMessage;

		// the snapshot's entities may still have rolled off the buffer, though
		if ( oldframe->first_entity <= svs.nextSnapshotEntities - svs.numSnapshotEntities ) {
			Com_DPrintf ("%s: Delta request from out of date entities.\n", client->name);
			oldframe = NULL;
			lastframe = 0;
		}
	}

	MSG_WriteByte (msg, svc_snapshot);

	// NOTE, MRE: now sent at the start of every message from server to client
	// let the client know which reliable clientCommands we have received
	//MSG_WriteLong( msg, client->lastClientCommand );

	// send over the current server time so the client can drift
	// its view of time to try to match
	if( client->oldServerTime ) {
		// The server has not yet got an acknowledgement of the
		// new gamestate from this client, so continue to send it
		// a time as if the server has not restarted. Note from
		// the client's perspective this time is strictly speaking
		// incorrect, but since it'll be busy loading a map at
		// the time it doesn't really matter.
		MSG_WriteLong (msg, frame->serverTime + client->oldServerTime);
	} else {
		MSG_WriteLong (msg, frame->serverTime);
	}

	// what we are delta'ing from
	MSG_WriteByte (msg, lastframe);

	snapFlags = svs.snapFlagServerBit | frame->snapFlags;
	if ( client->rateDelayed ) {
		snapFlags |= SNAPFLAG_RATE_DELAYED;
	}
	if ( client->state != CS_ACTIVE ) {
		snapFlags |= SNAPFLAG_NOT_ACTIVE;
	}

	MSG_WriteByte (msg, snapFlags);

	// send over the areabits
	MSG_WriteByte (msg, frame->areabytes);
	MSG_WriteData (msg, frame->areabits, frame->areabytes);

	// delta encode the playerstate
	if ( oldframe ) {
		MSG_WriteDeltaPlayerstate( msg, &oldframe->ps, &frame->ps );
	} else {
		MSG_WriteDeltaPlayerstate( msg, NULL, &frame->ps );
	}

	// delta encode the entities
	SV_EmitPacketEntities (oldframe, frame, msg);

	// padding for rate debugging
	if ( sv_padPackets->integer ) {
		for ( i = 0 ; i < sv_padPackets->integer ; i++ ) {
			MSG_WriteByte (msg, svc_nop);
		}
	}
}


/*
==================
SV_UpdateServerCommandsToClient

(re)send all server commands the client hasn't acknowledged yet
==================
*/
void SV_UpdateServerCommandsToClient( client_t *client, msg_t *msg ) {
	int		i;

	// write any unacknowledged serverCommands
	for ( i = client->reliableAcknowledge + 1 ; i <= client->reliableSequence ; i++ ) {
		MSG_WriteByte( msg, svc_serverCommand );
		MSG_WriteLong( msg, i );
		MSG_WriteString( msg, client->reliableCommands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
	}
	client->reliableSent = client->reliableSequence;
}

/*
=============================================================================

Build a client snapshot structure

=============================================================================
*/

typedef struct {
	int		numSnapshotEntities;
	int		snapshotEntities[MAX_SNAPSHOT_ENTITIES];	
} snapshotEntityNumbers_t;

/*
=======================
SV_QsortEntityNumbers
=======================
*/
static int QDECL SV_QsortEntityNumbers( const void *a, const void *b ) {
	int	*ea, *eb;

	ea = (int *)a;
	eb = (int *)b;

	if ( *ea == *eb ) {
		Com_Error( ERR_DROP, "SV_QsortEntityStates: duplicated entity" );
	}

	if ( *ea < *eb ) {
		return -1;
	}

	return 1;
}


/*
===============
SV_AddEntToSnapshot
===============
*/
static void SV_AddEntToSnapshot( svEntity_t *svEnt, sharedEntity_t *gEnt, snapshotEntityNumbers_t *eNums ) {
	// if we have already added this entity to this snapshot, don't add again
	if ( svEnt->snapshotCounter == sv.snapshotCounter ) {
		return;
	}
	svEnt->snapshotCounter = sv.snapshotCounter;

	// if we are full, silently discard entities
	if ( eNums->numSnapshotEntities == MAX_SNAPSHOT_ENTITIES ) {
		return;
	}

	eNums->snapshotEntities[ eNums->numSnapshotEntities ] = gEnt->s.number;
	eNums->numSnapshotEntities++;
}

/*
===============
SV_AddEntitiesVisibleFromPoint
===============
*/
static void SV_AddEntitiesVisibleFromPoint( vec3_t origin, clientSnapshot_t *frame, 
									snapshotEntityNumbers_t *eNums, qboolean portal ) {
	int		e, i;
	sharedEntity_t *ent;
	svEntity_t	*svEnt;
	int		l;
	int		clientarea, clientcluster;
	int		leafnum;
	byte	*clientpvs;
	byte	*bitvector;

	// during an error shutdown message we may need to transmit
	// the shutdown message after the server has shutdown, so
	// specfically check for it
	if ( !sv.state ) {
		return;
	}

	leafnum = CM_PointLeafnum (origin);
	clientarea = CM_LeafArea (leafnum);
	clientcluster = CM_LeafCluster (leafnum);

	// calculate the visible areas
	frame->areabytes = CM_WriteAreaBits( frame->areabits, clientarea );

	clientpvs = CM_ClusterPVS (clientcluster);

	for ( e = 0 ; e < sv.num_entities ; e++ ) {
		ent = SV_GentityNum(e);

		// never send entities that aren't linked in
		if ( !ent->r.linked ) {
			continue;
		}

		if (ent->s.number != e) {
			Com_DPrintf ("FIXING ENT->S.NUMBER!!!\n");
			ent->s.number = e;
		}

		// entities can be flagged to explicitly not be sent to the client
		if ( ent->r.svFlags & SVF_NOCLIENT ) {
			continue;
		}

		// entities can be flagged to be sent to only one client
		if ( ent->r.svFlags & SVF_SINGLECLIENT ) {
			if ( ent->r.singleClient != frame->ps.clientNum ) {
				continue;
			}
		}
		// entities can be flagged to be sent to everyone but one client
		if ( ent->r.svFlags & SVF_NOTSINGLECLIENT ) {
			if ( ent->r.singleClient == frame->ps.clientNum ) {
				continue;
			}
		}
		// entities can be flagged to be sent to a given mask of clients
		if ( ent->r.svFlags & SVF_CLIENTMASK ) {
			if (frame->ps.clientNum >= 32)
				Com_Error( ERR_DROP, "SVF_CLIENTMASK: clientNum >= 32" );
			if (~ent->r.singleClient & (1 << frame->ps.clientNum))
				continue;
		}

		svEnt = SV_SvEntityForGentity( ent );

		// don't double add an entity through portals
		if ( svEnt->snapshotCounter == sv.snapshotCounter ) {
			continue;
		}

		// broadcast entities are always sent
		if ( ent->r.svFlags & SVF_BROADCAST ) {
			SV_AddEntToSnapshot( svEnt, ent, eNums );
			continue;
		}

		// ignore if not touching a PV leaf
		// check area
		if ( !CM_AreasConnected( clientarea, svEnt->areanum ) ) {
			// doors can legally straddle two areas, so
			// we may need to check another one
			if ( !CM_AreasConnected( clientarea, svEnt->areanum2 ) ) {
				continue;		// blocked by a door
			}
		}

		bitvector = clientpvs;

		// check individual leafs
		if ( !svEnt->numClusters ) {
			continue;
		}
		l = 0;
		for ( i=0 ; i < svEnt->numClusters ; i++ ) {
			l = svEnt->clusternums[i];
			if ( bitvector[l >> 3] & (1 << (l&7) ) ) {
				break;
			}
		}

		// if we haven't found it to be visible,
		// check overflow clusters that coudln't be stored
		if ( i == svEnt->numClusters ) {
			if ( svEnt->lastCluster ) {
				for ( ; l <= svEnt->lastCluster ; l++ ) {
					if ( bitvector[l >> 3] & (1 << (l&7) ) ) {
						break;
					}
				}
				if ( l == svEnt->lastCluster ) {
					continue;	// not visible
				}
			} else {
				continue;
			}
		}

		// add it
		SV_AddEntToSnapshot( svEnt, ent, eNums );

		// if it's a portal entity, add everything visible from its camera position
		if ( ent->r.svFlags & SVF_PORTAL ) {
			if ( ent->s.generic1 ) {
				vec3_t dir;
				VectorSubtract(ent->s.origin, origin, dir);
				if ( VectorLengthSquared(dir) > (float) ent->s.generic1 * ent->s.generic1 ) {
					continue;
				}
			}
			SV_AddEntitiesVisibleFromPoint( ent->s.origin2, frame, eNums, qtrue );
		}

	}
}

// A bounded, time-sampled history independent of any recipient's rate or PVS.
// Entity states are shared between recorded viewpoints; each viewpoint retains
// its original visibility and player state. Playback uses the ordinary delta
// snapshot channel, so spectators, late arrivals and distant players see the
// same scene without a burst of reliable commands or a local rewind buffer.
#define REPLAY_SAMPLE_MSEC 25
#define REPLAY_HISTORY_MSEC 6500
#define REPLAY_FRAMES (REPLAY_HISTORY_MSEC / REPLAY_SAMPLE_MSEC + 2)

typedef struct {
	qboolean valid;
	playerState_t ps;
	int areabytes;
	byte areabits[MAX_MAP_AREA_BYTES];
	byte visible[MAX_GENTITIES / 8];
} replayView_t;

typedef struct {
	int time, numEntities;
	replayView_t views[MAX_CLIENTS];
	entityState_t entities[MAX_GENTITIES];
} replayFrame_t;

static struct {
	replayFrame_t *frames;
	int head, count;
	int clientNum, fromTime, toTime, beginTime, endTime;
} replay;

static replayFrame_t *SV_ReplayFrame( int index ) {
	return &replay.frames[(replay.head - replay.count + index + REPLAY_FRAMES) % REPLAY_FRAMES];
}

void SV_ReplayReset( qboolean enabled ) {
	replayFrame_t *frames = replay.frames;
	if ( !enabled && frames ) {
		free(frames);
		frames = NULL;
	} else if ( enabled && !frames ) {
		// Keep bounded replay storage outside the engine's small fixed zone.
		frames = calloc(REPLAY_FRAMES, sizeof(*frames));
		if ( !frames ) Com_Error(ERR_FATAL, "Could not allocate replay history");
	}
	Com_Memset(&replay, 0, sizeof(replay));
	replay.frames = frames;
}

int SV_ReplayStart( int clientNum, int fromTime, int toTime ) {
	int i;
	replayFrame_t *frame;
	if ( !replay.frames || replay.count < 2 || clientNum < 0 ||
	     clientNum >= sv_maxclients->integer || fromTime >= toTime ||
	     toTime > sv.time + 1000 || toTime < sv.time || replay.endTime > sv.time ) return 0;
	// Start at an actual recorded frame, never extend the clip with a frozen
	// opening when the kill happens early in a round or the killer just joined.
	for ( i = 0; i < replay.count; i++ ) {
		frame = SV_ReplayFrame(i);
		if ( frame->time >= fromTime && frame->views[clientNum].valid ) break;
	}
	if ( i == replay.count || frame->time >= toTime ) return 0;
	replay.clientNum = clientNum;
	replay.fromTime = frame->time;
	replay.toTime = toTime;
	// Allow the final sample to be captured before switching to playback.
	replay.beginTime = toTime + REPLAY_SAMPLE_MSEC;
	replay.endTime = replay.beginTime + toTime - replay.fromTime;
	Com_Printf("Replay: client %i source %i..%i playback %i..%i\n", clientNum,
		replay.fromTime, toTime, replay.beginTime, replay.endTime);
	return replay.endTime;
}

void SV_RecordReplayFrame( void ) {
	replayFrame_t *frame, *previous;
	replayView_t *view;
	clientSnapshot_t snapshot;
	snapshotEntityNumbers_t numbers;
	playerState_t *ps;
	vec3_t eye;
	int n, i, e;
	if ( !replay.frames ) return;
	// Pin the complete clip during playback. Only reset/next round resumes
	// capture; real game state continues to advance independently.
	if ( replay.endTime && sv.time > replay.toTime ) return;
	previous = replay.count ? SV_ReplayFrame(replay.count-1) : NULL;
	if ( previous && sv.time - previous->time < REPLAY_SAMPLE_MSEC ) return;
	frame = &replay.frames[replay.head];
	frame->time = sv.time;
	frame->numEntities = sv.num_entities;
	for ( n = 0; n < sv_maxclients->integer; n++ ) {
		view = &frame->views[n];
		Com_Memset(view, 0, sizeof(*view));
		ps = SV_GameClientNum(n);
		if ( svs.clients[n].state == CS_ACTIVE && ps->clientNum == n ) view->ps = *ps;
		else if ( previous && previous->views[n].valid ) {
			if ( svs.clients[n].state != CS_ACTIVE &&
			     (!replay.endTime || n != replay.clientNum) ) continue;
			// An eliminated killer may now be following a teammate (e.g. a
			// grenade kill after death). Keep their own last camera instead.
			view->ps = previous->views[n].ps;
		} else continue;
		view->valid = qtrue;
		Com_Memset(&snapshot, 0, sizeof(snapshot));
		snapshot.ps = view->ps;
		numbers.numSnapshotEntities = 0;
		sv.snapshotCounter++;
		sv.svEntities[n].snapshotCounter = sv.snapshotCounter;
		VectorCopy(view->ps.origin, eye);
		eye[2] += view->ps.viewheight;
		SV_AddEntitiesVisibleFromPoint(eye, &snapshot, &numbers, qfalse);
		view->areabytes = snapshot.areabytes;
		for ( i = 0; i < MAX_MAP_AREA_BYTES; i++ ) view->areabits[i] = ~snapshot.areabits[i];
		for ( i = 0; i < numbers.numSnapshotEntities; i++ ) {
			e = numbers.snapshotEntities[i];
			view->visible[e >> 3] |= 1 << (e & 7);
		}
	}
	// Visibility collection also validates/fixes entity numbers, just as it
	// does for live snapshots. Copy the states after that validation.
	for ( e = 0; e < sv.num_entities; e++ ) frame->entities[e] = SV_GentityNum(e)->s;
	replay.head = (replay.head + 1) % REPLAY_FRAMES;
	if ( replay.count < REPLAY_FRAMES ) replay.count++;
}

static qboolean SV_BuildReplaySnapshot( clientSnapshot_t *out ) {
	replayFrame_t *frame = NULL, *candidate;
	replayView_t *view;
	entityState_t *state;
	int i, offset, sourceTime;
	if ( !replay.endTime || sv.time < replay.beginTime || sv.time >= replay.endTime ) return qfalse;
	offset = replay.beginTime - replay.fromTime;
	sourceTime = sv.time - offset;
	for ( i = 0; i < replay.count; i++ ) {
		candidate = SV_ReplayFrame(i);
		if ( candidate->time > sourceTime ) break;
		if ( candidate->views[replay.clientNum].valid ) frame = candidate;
	}
	if ( !frame ) return qfalse;
	view = &frame->views[replay.clientNum];
	out->serverTime = frame->time + offset;
	out->snapFlags = SNAPFLAG_REPLAY;
	out->ps = view->ps;
	// Rebase absolute clocks onto the live transport timeline. Durations and
	// elapsed weapon/note clocks stay untouched. No live entity or ps is edited.
	out->ps.commandTime += offset;
	if ( out->ps.externalEventTime ) out->ps.externalEventTime += offset;
	for ( i = 0; i < MAX_POWERUPS; i++ ) {
		if ( out->ps.powerups[i] && out->ps.powerups[i] < INT_MAX - offset ) out->ps.powerups[i] += offset;
	}
	out->areabytes = view->areabytes;
	Com_Memcpy(out->areabits, view->areabits, sizeof(out->areabits));
	out->first_entity = svs.nextSnapshotEntities;
	out->num_entities = 0;
	for ( i = 0; i < frame->numEntities; i++ ) {
		if ( !(view->visible[i >> 3] & (1 << (i & 7))) ) continue;
		state = &svs.snapshotEntities[svs.nextSnapshotEntities % svs.numSnapshotEntities];
		*state = frame->entities[i];
		state->pos.trTime += offset;
		state->apos.trTime += offset;
		svs.nextSnapshotEntities++;
		if ( svs.nextSnapshotEntities >= 0x7FFFFFFE ) Com_Error(ERR_FATAL, "svs.nextSnapshotEntities wrapped");
		out->num_entities++;
	}
	return qtrue;
}

/*
=============
SV_BuildClientSnapshot

Decides which entities are going to be visible to the client, and
copies off the playerstate and areabits.

This properly handles multiple recursive portals, but the render
currently doesn't.

For viewing through other player's eyes, clent can be something other than client->gentity
=============
*/
static void SV_BuildClientSnapshot( client_t *client ) {
	vec3_t						org;
	clientSnapshot_t			*frame;
	snapshotEntityNumbers_t		entityNumbers;
	int							i;
	sharedEntity_t				*ent;
	entityState_t				*state;
	svEntity_t					*svEnt;
	sharedEntity_t				*clent;
	int							clientNum;
	playerState_t				*ps;

	// bump the counter used to prevent double adding
	sv.snapshotCounter++;

	// this is the frame we are creating
	frame = &client->frames[ client->netchan.outgoingSequence & PACKET_MASK ];
	frame->serverTime = sv.time;
	frame->snapFlags = 0;

	// clear everything in this snapshot
	entityNumbers.numSnapshotEntities = 0;
	Com_Memset( frame->areabits, 0, sizeof( frame->areabits ) );

  // https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=62
	frame->num_entities = 0;
	
	clent = client->gentity;
	if ( !clent || client->state == CS_ZOMBIE ) {
		return;
	}
	if ( SV_BuildReplaySnapshot(frame) ) return;

	// grab the current playerState_t
	ps = SV_GameClientNum( client - svs.clients );
	frame->ps = *ps;

	// never send client's own entity, because it can
	// be regenerated from the playerstate
	clientNum = frame->ps.clientNum;
	if ( clientNum < 0 || clientNum >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "SV_SvEntityForGentity: bad gEnt" );
	}
	svEnt = &sv.svEntities[ clientNum ];

	svEnt->snapshotCounter = sv.snapshotCounter;

	// find the client's viewpoint
	VectorCopy( ps->origin, org );
	org[2] += ps->viewheight;

	// add all the entities directly visible to the eye, which
	// may include portal entities that merge other viewpoints
	SV_AddEntitiesVisibleFromPoint( org, frame, &entityNumbers, qfalse );

	// if there were portals visible, there may be out of order entities
	// in the list which will need to be resorted for the delta compression
	// to work correctly.  This also catches the error condition
	// of an entity being included twice.
	qsort( entityNumbers.snapshotEntities, entityNumbers.numSnapshotEntities, 
		sizeof( entityNumbers.snapshotEntities[0] ), SV_QsortEntityNumbers );

	// now that all viewpoint's areabits have been OR'd together, invert
	// all of them to make it a mask vector, which is what the renderer wants
	for ( i = 0 ; i < MAX_MAP_AREA_BYTES/4 ; i++ ) {
		((int *)frame->areabits)[i] = ((int *)frame->areabits)[i] ^ -1;
	}

	// copy the entity states out
	frame->num_entities = 0;
	frame->first_entity = svs.nextSnapshotEntities;
	for ( i = 0 ; i < entityNumbers.numSnapshotEntities ; i++ ) {
		ent = SV_GentityNum(entityNumbers.snapshotEntities[i]);
		state = &svs.snapshotEntities[svs.nextSnapshotEntities % svs.numSnapshotEntities];
		*state = ent->s;
		svs.nextSnapshotEntities++;
		// this should never hit, map should always be restarted first in SV_Frame
		if ( svs.nextSnapshotEntities >= 0x7FFFFFFE ) {
			Com_Error(ERR_FATAL, "svs.nextSnapshotEntities wrapped");
		}
		frame->num_entities++;
	}
}

#ifdef USE_VOIP
/*
==================
SV_WriteVoipToClient

Check to see if there is any VoIP queued for a client, and send if there is.
==================
*/
static void SV_WriteVoipToClient(client_t *cl, msg_t *msg)
{
	int totalbytes = 0;
	int i;
	voipServerPacket_t *packet;

	if(cl->queuedVoipPackets)
	{
		// Write as many VoIP packets as we reasonably can...
		for(i = 0; i < cl->queuedVoipPackets; i++)
		{
			packet = cl->voipPacket[(i + cl->queuedVoipIndex) % ARRAY_LEN(cl->voipPacket)];

			if(!*cl->downloadName)
			{
        			totalbytes += packet->len;
	        		if (totalbytes > (msg->maxsize - msg->cursize) / 2)
		        		break;

        			MSG_WriteByte(msg, svc_voipOpus);
        			MSG_WriteShort(msg, packet->sender);
	        		MSG_WriteByte(msg, (byte) packet->generation);
		        	MSG_WriteLong(msg, packet->sequence);
		        	MSG_WriteByte(msg, packet->frames);
        			MSG_WriteShort(msg, packet->len);
        			MSG_WriteBits(msg, packet->flags, VOIP_FLAGCNT);
	        		MSG_WriteData(msg, packet->data, packet->len);
                        }

			Z_Free(packet);
		}

		cl->queuedVoipPackets -= i;
		cl->queuedVoipIndex += i;
		cl->queuedVoipIndex %= ARRAY_LEN(cl->voipPacket);
	}
}
#endif

/*
=======================
SV_SendMessageToClient

Called by SV_SendClientSnapshot and SV_SendClientGameState
=======================
*/
void SV_SendMessageToClient(msg_t *msg, client_t *client)
{
	// record information about the message
	client->frames[client->netchan.outgoingSequence & PACKET_MASK].messageSize = msg->cursize;
	client->frames[client->netchan.outgoingSequence & PACKET_MASK].messageSent = svs.time;
	client->frames[client->netchan.outgoingSequence & PACKET_MASK].messageAcked = -1;

	// send the datagram
	SV_Netchan_Transmit(client, msg);
}


/*
=======================
SV_SendClientSnapshot

Also called by SV_FinalMessage

=======================
*/
void SV_SendClientSnapshot( client_t *client ) {
	byte		msg_buf[MAX_MSGLEN];
	msg_t		msg;

	// build the snapshot
	SV_BuildClientSnapshot( client );

	// bots need to have their snapshots build, but
	// the query them directly without needing to be sent
	if ( client->gentity && client->gentity->r.svFlags & SVF_BOT ) {
		return;
	}

	MSG_Init (&msg, msg_buf, sizeof(msg_buf));
	msg.allowoverflow = qtrue;

	// NOTE, MRE: all server->client messages now acknowledge
	// let the client know which reliable clientCommands we have received
	MSG_WriteLong( &msg, client->lastClientCommand );

	// (re)send any reliable server commands
	SV_UpdateServerCommandsToClient( client, &msg );

	// send over all the relevant entityState_t
	// and the playerState_t
	SV_WriteSnapshotToClient( client, &msg );

#ifdef USE_VOIP
	SV_WriteVoipToClient( client, &msg );
#endif

	// check for overflow
	if ( msg.overflowed ) {
		Com_Printf ("WARNING: msg overflowed for %s\n", client->name);
		MSG_Clear (&msg);
	}

	SV_SendMessageToClient( &msg, client );
}


/*
=======================
SV_SendClientMessages
=======================
*/
void SV_SendClientMessages(void)
{
	int		i;
	client_t	*c;

	// send a message to each connected client
	for(i=0; i < sv_maxclients->integer; i++)
	{
		c = &svs.clients[i];
		
		if(!c->state)
			continue;		// not connected

		if(svs.time - c->lastSnapshotTime < c->snapshotMsec * com_timescale->value)
			continue;		// It's not time yet

		if(*c->downloadName)
			continue;		// Client is downloading, don't send snapshots

		if(c->netchan.unsentFragments || c->netchan_start_queue)
		{
			c->rateDelayed = qtrue;
			continue;		// Drop this snapshot if the packet queue is still full or delta compression will break
		}

		if(!(c->netchan.remoteAddress.type == NA_LOOPBACK ||
		     (sv_lanForceRate->integer && Sys_IsLANAddress(c->netchan.remoteAddress))))
		{
			// rate control for clients not on LAN 
			if(SV_RateMsec(c) > 0)
			{
				// Not enough time since last packet passed through the line
				c->rateDelayed = qtrue;
				continue;
			}
		}

		// generate and send a new message
		SV_SendClientSnapshot(c);
		c->lastSnapshotTime = svs.time;
		c->rateDelayed = qfalse;
	}
}
