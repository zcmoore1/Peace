// cg_killcam.c -- final killcam: record recent snapshots and replay the
// match-ending kill from the killer's point of view before the scoreboard.
//
// The whole replay is snapshot based. While the match is live we keep a short
// ring buffer of the snapshots we received (entities + player state). When the
// server tells us the match ended on a kill ("finalkillcam" command), we rewind
// that ring and feed the recorded frames back through the normal rendering /
// interpolation pipeline, with the camera chasing the killer. Because the
// killcam plays back exactly what the server sent, glitches that only ever
// existed in local client prediction never appear in it.

#include "cg_local.h"

#define KCAM_MAX_FRAMES		128		// ring depth; ~6.4s at sv_fps 20
#define KCAM_MAX_ENTS		64		// entities captured per frame
#define KCAM_LINGER			750		// ms to hold on the moment of the kill

typedef struct {
	qboolean		valid;
	int				serverTime;
	int				numEntities;
	entityState_t	entities[KCAM_MAX_ENTS];
	playerState_t	ps;
	byte			areamask[MAX_MAP_AREA_BYTES];
} kcamFrame_t;

typedef struct {
	qboolean		playing;
	int				killerNum;
	int				victimNum;
	int				killTime;			// server time of the kill
	int				startServerTime;	// where playback begins (killTime - rewind)
	int				stopServerTime;		// where playback ends (killTime + linger)
	int				beginCgTime;		// cg.time when playback started

	// recording ring
	int				head;				// next slot to write
	int				count;				// valid frames stored
	int				lastRecordTime;		// serverTime of the last frame recorded
	kcamFrame_t		frames[KCAM_MAX_FRAMES];

	// synthesized snapshots fed to the renderer during playback
	snapshot_t		snap;
	snapshot_t		nextSnap;
} kcam_t;

static kcam_t	kcam;

/*
===============
CG_Killcam_Init
===============
*/
void CG_Killcam_Init( void ) {
	memset( &kcam, 0, sizeof( kcam ) );
}

/*
===============
CG_Killcam_Record

Store the current snapshot into the ring. Called once per new snapshot while
the match is live (never while a killcam is playing).
===============
*/
void CG_Killcam_Record( void ) {
	kcamFrame_t	*f;
	int			i, n;

	if ( kcam.playing || !cg.snap ) {
		return;
	}
	if ( cg.snap->serverTime == kcam.lastRecordTime ) {
		return;		// already captured this snapshot
	}
	kcam.lastRecordTime = cg.snap->serverTime;

	f = &kcam.frames[kcam.head];
	f->valid = qtrue;
	f->serverTime = cg.snap->serverTime;
	f->ps = cg.snap->ps;
	memcpy( f->areamask, cg.snap->areamask, sizeof( f->areamask ) );

	n = cg.snap->numEntities;
	if ( n > KCAM_MAX_ENTS ) {
		n = KCAM_MAX_ENTS;
	}
	f->numEntities = n;
	for ( i = 0; i < n; i++ ) {
		f->entities[i] = cg.snap->entities[i];
	}

	kcam.head = ( kcam.head + 1 ) % KCAM_MAX_FRAMES;
	if ( kcam.count < KCAM_MAX_FRAMES ) {
		kcam.count++;
	}
}

/*
===============
CG_Killcam_Start

Triggered by the "finalkillcam" server command at match end.
===============
*/
void CG_Killcam_Start( int killerNum, int victimNum, int killTime ) {
	if ( kcam.count < 2 ) {
		return;		// nothing recorded to replay
	}
	kcam.playing = qtrue;
	kcam.killerNum = killerNum;
	kcam.victimNum = victimNum;
	kcam.killTime = killTime;
	kcam.startServerTime = killTime - KILLCAM_REWIND;
	kcam.stopServerTime = killTime + KCAM_LINGER;
	kcam.beginCgTime = cg.time;
}

/*
===============
CG_Killcam_Active
===============
*/
qboolean CG_Killcam_Active( void ) {
	return kcam.playing;
}

/*
===============
CG_Killcam_FrameAt

idx 0 is the oldest stored frame, idx (count-1) the newest.
===============
*/
static kcamFrame_t *CG_Killcam_FrameAt( int idx ) {
	int slot = ( kcam.head - kcam.count + idx ) % KCAM_MAX_FRAMES;
	if ( slot < 0 ) {
		slot += KCAM_MAX_FRAMES;
	}
	return &kcam.frames[slot];
}

/*
===============
CG_Killcam_BuildSnap

Copy a recorded frame into a full snapshot_t. Replayed entity events are
cleared so we don't re-fire sounds / effects that already played live.
===============
*/
static void CG_Killcam_BuildSnap( snapshot_t *out, const kcamFrame_t *f ) {
	int i;

	memset( out, 0, sizeof( *out ) );
	out->serverTime = f->serverTime;
	out->ps = f->ps;
	out->numEntities = f->numEntities;
	memcpy( out->areamask, f->areamask, sizeof( out->areamask ) );

	for ( i = 0; i < f->numEntities; i++ ) {
		out->entities[i] = f->entities[i];
		out->entities[i].event = 0;
	}
}

/*
===============
CG_Killcam_Setup

Advance playback and point cg.snap / cg.nextSnap at the synthesized frames so
the normal render path interpolates the replay. Returns qfalse (and ends the
killcam) when playback has run out.
===============
*/
qboolean CG_Killcam_Setup( void ) {
	int			t, i;
	kcamFrame_t	*a, *b;
	kcamFrame_t	*first, *last;

	if ( !kcam.playing ) {
		return qfalse;
	}
	if ( kcam.count < 2 ) {
		kcam.playing = qfalse;
		return qfalse;
	}

	// playback clock advances with real time at 1x speed
	t = kcam.startServerTime + ( cg.time - kcam.beginCgTime );
	if ( t >= kcam.stopServerTime ) {
		kcam.playing = qfalse;
		return qfalse;
	}

	// find the two recorded frames bracketing the playback time
	a = b = NULL;
	for ( i = 0; i < kcam.count - 1; i++ ) {
		kcamFrame_t *f0 = CG_Killcam_FrameAt( i );
		kcamFrame_t *f1 = CG_Killcam_FrameAt( i + 1 );
		if ( f0->serverTime <= t && t < f1->serverTime ) {
			a = f0;
			b = f1;
			break;
		}
	}
	if ( !a ) {
		// clamp to the ends of what we recorded
		first = CG_Killcam_FrameAt( 0 );
		last = CG_Killcam_FrameAt( kcam.count - 1 );
		if ( t < first->serverTime ) {
			a = b = first;
		} else {
			a = b = last;
		}
	}

	CG_Killcam_BuildSnap( &kcam.snap, a );
	CG_Killcam_BuildSnap( &kcam.nextSnap, b );

	cg.snap = &kcam.snap;
	cg.nextSnap = &kcam.nextSnap;
	if ( b->serverTime > a->serverTime ) {
		cg.frameInterpolation =
			(float)( t - a->serverTime ) / ( b->serverTime - a->serverTime );
	} else {
		cg.frameInterpolation = 0.0f;
	}

	// no prediction during replay - just present the recorded killer state
	cg.predictedPlayerState = kcam.snap.ps;

	return qtrue;
}

/*
===============
CG_Killcam_LerpEnt

Interpolated origin / angles of an entity across the two replay snapshots.
Players use TR_INTERPOLATE, so trBase is the snapshot position.
===============
*/
static qboolean CG_Killcam_LerpEnt( int num, vec3_t origin, vec3_t angles ) {
	entityState_t	*a = NULL, *b = NULL;
	float			f = cg.frameInterpolation;
	int				i;

	for ( i = 0; i < kcam.snap.numEntities; i++ ) {
		if ( kcam.snap.entities[i].number == num ) {
			a = &kcam.snap.entities[i];
			break;
		}
	}
	for ( i = 0; i < kcam.nextSnap.numEntities; i++ ) {
		if ( kcam.nextSnap.entities[i].number == num ) {
			b = &kcam.nextSnap.entities[i];
			break;
		}
	}
	if ( !a && !b ) {
		return qfalse;
	}
	if ( !a ) {
		a = b;
	}
	if ( !b ) {
		b = a;
	}

	for ( i = 0; i < 3; i++ ) {
		origin[i] = a->pos.trBase[i] + f * ( b->pos.trBase[i] - a->pos.trBase[i] );
	}
	angles[YAW] = LerpAngle( a->apos.trBase[YAW], b->apos.trBase[YAW], f );
	angles[PITCH] = LerpAngle( a->apos.trBase[PITCH], b->apos.trBase[PITCH], f );
	angles[ROLL] = 0;
	return qtrue;
}

/*
===============
CG_Killcam_CalcView

Place a chase camera behind and above the killer. Mirrors the solid-trace
pull-in used by CG_OffsetThirdPersonView so the camera doesn't clip walls.
===============
*/
void CG_Killcam_CalcView( void ) {
	vec3_t		origin, angles, forward, view;
	trace_t		trace;
	static vec3_t	mins = { -4, -4, -4 };
	static vec3_t	maxs = { 4, 4, 4 };

	if ( kcam.killerNum == kcam.snap.ps.clientNum ) {
		// the killer is the local client - their state lives in ps, not entities
		VectorCopy( kcam.snap.ps.origin, origin );
		VectorCopy( kcam.snap.ps.viewangles, angles );
		origin[2] += kcam.snap.ps.viewheight;
	} else if ( CG_Killcam_LerpEnt( kcam.killerNum, origin, angles ) ) {
		origin[2] += DEFAULT_VIEWHEIGHT;
	} else if ( CG_Killcam_LerpEnt( kcam.victimNum, origin, angles ) ) {
		// killer wasn't in our recorded view; fall back to the victim
		origin[2] += DEFAULT_VIEWHEIGHT;
	} else {
		// nothing to look at - leave the view where it is
		return;
	}

	angles[PITCH] = 0;
	angles[ROLL] = 0;
	AngleVectors( angles, forward, NULL, NULL );

	VectorCopy( origin, view );
	VectorMA( view, -96, forward, view );
	view[2] += 40;

	CG_Trace( &trace, origin, mins, maxs, view, kcam.killerNum, MASK_SOLID );
	if ( trace.fraction != 1.0f ) {
		VectorCopy( trace.endpos, view );
	}

	VectorCopy( view, cg.refdef.vieworg );
	VectorCopy( angles, cg.refdefViewAngles );
	cg.refdefViewAngles[PITCH] = 12;	// look slightly down at the action
}

/*
===============
CG_Killcam_Draw

"FINAL KILLCAM" banner during playback.
===============
*/
void CG_Killcam_Draw( void ) {
	const char	*title = "FINAL KILLCAM";
	float		w;

	if ( !kcam.playing ) {
		return;
	}

	w = BIGCHAR_WIDTH * strlen( title );
	CG_DrawBigString( ( SCREEN_WIDTH - w ) / 2, 64, title, 1.0f );
}
