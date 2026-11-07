/* The real network codec (msg.c) for event_ring_test.c, in its own TU:
 * qcommon.h and the cgame's headers do not mix. */
#include "../../code/qcommon/q_shared.h"
#include "../../code/qcommon/qcommon.h"

cvar_t *cl_shownet;

/* One snapshot's entity delta, written by the server and read by a client. */
void WireEntity( const entityState_t *from, const entityState_t *to, entityState_t *out ) {
	static byte data[MAX_MSGLEN];
	entityState_t f = *from, t = *to;
	msg_t msg;
	MSG_Init( &msg, data, sizeof( data ) );
	MSG_WriteDeltaEntity( &msg, &f, &t, qtrue );
	if ( msg.overflowed ) Com_Error( ERR_FATAL, "WireEntity: overflowed" );
	MSG_BeginReading( &msg );
	MSG_ReadDeltaEntity( &msg, &f, out, MSG_ReadBits( &msg, GENTITYNUM_BITS ) );
}

/* The playerstate's delta, the same way. */
void WirePlayerstate( const playerState_t *from, const playerState_t *to, playerState_t *out ) {
	static byte data[MAX_MSGLEN];
	playerState_t f = *from, t = *to;
	msg_t msg;
	MSG_Init( &msg, data, sizeof( data ) );
	MSG_WriteDeltaPlayerstate( &msg, &f, &t );
	if ( msg.overflowed ) Com_Error( ERR_FATAL, "WirePlayerstate: overflowed" );
	MSG_BeginReading( &msg );
	MSG_ReadDeltaPlayerstate( &msg, &f, out );
}
