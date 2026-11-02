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
// cl_timedelta.c -- how the client's clock drifts toward the server's
//
// This is MW2's CL_AdjustTimeDelta (iw4x.exe 0x5a1ad0-0x5a1c3c), as a pure
// function of its inputs so the fake reload harness drives the real rule.
// The client's clock is cls.realtime + delta. Its target sits one snapshot
// interval plus TIMEDELTA_BACKOFF behind the newest snapshot, so the client
// draws, stamps commands and predicts (from cgame's time-selected nextSnap)
// on a base that age. ioquake3 targets the newest snapshot itself.

#include "../qcommon/q_shared.h"
#include "cl_timedelta.h"

/*
=================
CL_TimeDeltaTarget

The delta MW2 drifts toward: newest - realtime - (newest - prev) - 16
(0x5a1ad0-0x5a1aee). prev is the snapshot before the newest
(cl.prevSnapServerTime, saved as the newest is copied in, 0x5ac0ce).
=================
*/
static int CL_TimeDeltaTarget( int newest, int prev, int realtime ) {
	return newest - realtime - ( newest - prev ) - TIMEDELTA_BACKOFF;
}

/*
=================
CL_TimeDeltaGap

How far the delta is from its target, as MW2 measures it before choosing a
reset, a fast or a slow adjustment (0x5a1af8-0x5a1b14, 0x5a1b88). Behind the
target the gap is the whole difference. Ahead of it, one snapshot interval of
lead is free: the gap is what lies beyond the interval, and none at all
within it, unless the interval itself is not sane (more than
TIMEDELTA_RESET, or negative: the compare is unsigned).
=================
*/
int CL_TimeDeltaGap( int delta, int newest, int prev, int realtime ) {
	int		interval = newest - prev;
	int		gap = CL_TimeDeltaTarget( newest, prev, realtime ) - delta;

	if ( gap <= 0 ) {
		return -gap;
	}
	if ( (unsigned)interval > (unsigned)TIMEDELTA_RESET ) {
		return gap;
	}
	gap -= interval;
	return gap > 0 ? gap : 0;
}

/*
=================
CL_DriftTimeDelta

The new delta after a snapshot arrives (0x5a1ad0):
- more than TIMEDELTA_RESET off: reset to newest - realtime - 16 (0x5a1b96);
- more than TIMEDELTA_FAST off: halve the difference (0x5a1bf0);
- otherwise, after a frame that extrapolated: 2 ms back (0x5a1b36), clearing
  *extrapolated;
- otherwise 1 ms toward the target, or none on it (0x5a1c02-0x5a1c3c).
The reset also puts the client clock on the newest snapshot; the caller does
that (CL_AdjustTimeDelta). extrapolated is NULL when the slow drift is off:
MW2 runs it only at timescale 1 (0x5a1b16-0x5a1b23), and leaves the flag set.
=================
*/
int CL_DriftTimeDelta( int delta, int newest, int prev, int realtime, qboolean *extrapolated ) {
	int		target = CL_TimeDeltaTarget( newest, prev, realtime );
	int		gap = CL_TimeDeltaGap( delta, newest, prev, realtime );

	if ( gap > TIMEDELTA_RESET ) {
		return newest - realtime - TIMEDELTA_BACKOFF;
	}
	if ( gap > TIMEDELTA_FAST ) {
		return ( delta + target ) >> 1;
	}
	if ( !extrapolated ) {
		return delta;
	}
	if ( *extrapolated ) {
		*extrapolated = qfalse;
		return delta - 2;
	}
	if ( target > delta ) {
		return delta + 1;
	}
	if ( target < delta ) {
		return delta - 1;
	}
	return delta;
}
