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
// cl_timedelta.h -- the client's clock against the server's (MW2's time base)

#ifndef CL_TIMEDELTA_H
#define CL_TIMEDELTA_H

// MW2's client clock (iw4x.exe CL_AdjustTimeDelta 0x5a1ad0, CL_SetCGameTime
// 0x5031c6): it sits one snapshot interval plus TIMEDELTA_BACKOFF behind the
// newest snapshot, and a frame at or past newest - TIMEDELTA_EXTRAPOLATE
// counts as extrapolated.
#define	TIMEDELTA_BACKOFF		16		// 0x5a1aee
#define	TIMEDELTA_EXTRAPOLATE	8		// 0x5031ed
#define	TIMEDELTA_RESET			500		// 0x5a1b0a, 0x5a1b8a
#define	TIMEDELTA_FAST			100		// 0x5a1bcc

int CL_TimeDeltaGap( int delta, int newest, int prev, int realtime );
int CL_DriftTimeDelta( int delta, int newest, int prev, int realtime, qboolean *extrapolated );

#endif
