/*
===========================================================================
cg_weappicture.c  --  which player state the viewmodel is drawn from

Picture only. Gameplay, aiming, the HUD and the camera never read this.

Normally the viewmodel is drawn from the predicted player state. Prediction is
sometimes corrected: the server never did something the client predicted.
When the correction removes an action of the gun in your hands that you were
already shown (a reload, a shot, a pump), the gun does not un-play it. From
that moment the picture keeps running YOUR OWN commands through the real
Pmove, uncorrected, until that run comes to rest or a real action takes over.
The run only finishes what you were shown: it never detaches into an action
it began itself, and once detached it withholds whatever in your commands
would begin something new. Anything that still begins one hands the picture
back to prediction.

A correction that only removes a swap (a knife or a grenade you never really
got out) is followed at once: a weapon you never held is never shown on its
own, only as the continuation of a gun action already playing.

Nothing here names a trick. Whatever a real desync does to the weapon, this
draws the same way.
===========================================================================
*/

#include "cg_local.h"

#define PICTURE_MAX_DETACH	4000	// a run that never settles is dropped

static playerState_t	pic;			// the picture's own run
static qboolean		picValid, picDetached;
static int			picShot = -1;	// command time the run's gun last began an action since rest
static int			picDetachTime;
static qboolean		picBegan;		// this frame's run began an action of its own

// What this frame's prediction replay saw, so a correction can be judged.
static playerState_t	replayBase;
static int			replayShot = -1;
// The floor gun prediction chose for each recent command, so the picture's run
// reads the reload button exactly as prediction did.
#define PICTURE_ITEMS	64
static struct { int serverTime; const entityState_t *item; } replayItems[PICTURE_ITEMS];

static qboolean CG_WeapPicture_GunAction( const playerState_t *ps ) {
	if ( ps->weapon <= WP_NONE || ps->weapon >= WP_NUM_WEAPONS || BG_IsActionWeapon( ps->weapon ) ) {
		return qfalse;
	}
	return ps->weaponstate == WEAPON_RELOADING || ps->weaponstate == WEAPON_FIRING ||
		ps->weaponstate == WEAPON_BOLTING;
}

// A new action of the held gun, not the pump continuing the same shot.
static qboolean CG_WeapPicture_BeganAction( const playerState_t *before, const playerState_t *after ) {
	if ( !CG_WeapPicture_GunAction( after ) ) return qfalse;
	if ( after->weaponstate == before->weaponstate && after->weapon == before->weapon ) return qfalse;
	if ( before->weaponstate == WEAPON_FIRING && after->weaponstate == WEAPON_BOLTING ) return qfalse;
	return qtrue;
}

// A knife or a grenade asked for on this command.
static qboolean CG_WeapPicture_BeganQuick( const playerState_t *before, const playerState_t *after ) {
	const int mask = WA_TYPE_MASK | WA_FIRED;
	return ( after->weaponAction & WA_TYPE_MASK ) && !( after->weaponAction & WA_FIRED ) &&
		( after->weaponAction & mask ) != ( before->weaponAction & mask );
}

static qboolean CG_WeapPicture_AtRest( const playerState_t *ps ) {
	return ps->weaponstate == WEAPON_READY && !BG_IsActionWeapon( ps->weapon ) &&
		!( ps->weaponAction & ( WA_TYPE_MASK | WA_RETURNING | WA_RECOVERING ) );
}

static qboolean CG_WeapPicture_Same( const playerState_t *a, const playerState_t *b ) {
	return a->weapon == b->weapon && a->weaponstate == b->weaponstate &&
		a->weaponAnimSeq == b->weaponAnimSeq && a->weaponAnimTime == b->weaponAnimTime &&
		a->weaponTime == b->weaponTime &&
		( a->weaponAction & ( WA_TYPE_MASK | WA_FIRED | WA_RETURNING | WA_RECOVERING ) ) ==
		( b->weaponAction & ( WA_TYPE_MASK | WA_FIRED | WA_RETURNING | WA_RECOVERING ) ) &&
		a->stats[STAT_ACTIVE_SLOT] == b->stats[STAT_ACTIVE_SLOT];
}

static void CG_WeapPicture_Follow( const playerState_t *predicted ) {
	pic = *predicted;
	picDetached = qfalse;
	if ( CG_WeapPicture_AtRest( predicted ) ) {
		picShot = -1;
	} else if ( replayShot >= 0 ) {
		picShot = replayShot;
	}
}

void CG_WeapPicture_Reset( void ) {
	picValid = picDetached = qfalse;
	picShot = replayShot = -1;
}

// Prediction restarts from the newest snapshot each frame.
void CG_WeapPicture_BeginReplay( const playerState_t *base ) {
	replayBase = *base;
	replayShot = -1;
}

void CG_WeapPicture_ReplayStep( const playerState_t *before, const playerState_t *after,
	const entityState_t *item ) {
	int slot = after->commandTime & ( PICTURE_ITEMS - 1 );
	replayItems[slot].serverTime = after->commandTime;
	replayItems[slot].item = item;
	if ( CG_WeapPicture_BeganAction( before, after ) ) {
		replayShot = after->commandTime;
	}
}

static const entityState_t *CG_WeapPicture_Item( int serverTime ) {
	int slot = serverTime & ( PICTURE_ITEMS - 1 );
	return replayItems[slot].serverTime == serverTime ? replayItems[slot].item : NULL;
}

// Did the corrected prediction keep the gun action the picture began at 'shot'?
static qboolean CG_WeapPicture_PredictionKept( int shot ) {
	if ( replayShot == shot ) return qtrue;
	if ( shot <= replayBase.commandTime ) {
		// Begun before the snapshot: the server's own state there must show it.
		return CG_WeapPicture_GunAction( &replayBase );
	}
	return qfalse;
}

// Run the picture's own state over every command it has not seen yet.
static void CG_WeapPicture_Advance( const pmove_t *model, int first, int last,
	qboolean (*getCmd)( int, usercmd_t * ) ) {
	pmove_t move;
	usercmd_t cmd;
	playerState_t before;
	int n;

	for ( n = first; n <= last; n++ ) {
		if ( !getCmd( n, &cmd ) || cmd.serverTime <= pic.commandTime ) {
			continue;
		}
		move = *model;
		move.ps = &pic;
		move.cmd = cmd;
		if ( move.pmove_fixed ) {
			move.cmd.serverTime = ( ( move.cmd.serverTime + move.pmove_msec - 1 ) / move.pmove_msec ) * move.pmove_msec;
		}
		move.pickupItem = CG_WeapPicture_Item( move.cmd.serverTime );
		before = pic;
		Pmove( &move );
		if ( picDetached && ( CG_WeapPicture_BeganAction( &before, &pic ) ||
			CG_WeapPicture_BeganQuick( &before, &pic ) ) ) {
			// Finish, never begin: the same command without what began it.
			pic = before;
			move.ps = &pic;
			move.cmd.buttons &= ~( BUTTON_ATTACK | BUTTON_RELOAD | BUTTON_MELEE | BUTTON_LETHAL |
				BUTTON_TACTICAL | BUTTON_WEAPON_SWAP );
			move.cmd.weapon = BG_SlotCommand( &pic, pic.stats[STAT_ACTIVE_SLOT] );
			Pmove( &move );
		}
		if ( CG_WeapPicture_BeganAction( &before, &pic ) || CG_WeapPicture_BeganQuick( &before, &pic ) ) {
			picBegan = qtrue;
			if ( picDetached ) return;
		}
		if ( CG_WeapPicture_BeganAction( &before, &pic ) ) {
			picShot = pic.commandTime;
		} else if ( CG_WeapPicture_AtRest( &pic ) ) {
			picShot = -1;
		}
	}
}

/*
=================
CG_WeapPicture_Update

Called once per frame after prediction. 'model' carries the trace functions
and masks prediction used; commands [first, last] are the client's own.
=================
*/
void CG_WeapPicture_Update( const playerState_t *predicted, const pmove_t *model,
	int first, int last, qboolean (*getCmd)( int, usercmd_t * ) ) {
	playerState_t start;
	int startShot;

	if ( !picValid || predicted->pm_type != PM_NORMAL || predicted->stats[STAT_HEALTH] <= 0 ||
		pic.clientNum != predicted->clientNum ||
		pic.persistant[PERS_SPAWN_COUNT] != predicted->persistant[PERS_SPAWN_COUNT] ||
		( pic.eFlags & EF_TELEPORT_BIT ) != ( predicted->eFlags & EF_TELEPORT_BIT ) ) {
		picValid = qtrue;
		picShot = -1;
		CG_WeapPicture_Follow( predicted );
		return;
	}

	start = pic;
	startShot = picShot;
	picBegan = qfalse;
	CG_WeapPicture_Advance( model, first, last, getCmd );

	if ( !picDetached ) {
		if ( CG_WeapPicture_Same( &pic, predicted ) ) {
			CG_WeapPicture_Follow( predicted );
		} else if ( startShot >= 0 && !CG_WeapPicture_PredictionKept( startShot ) ) {
			// The server never did an action you were shown: let it play out,
			// from where you saw it, beginning nothing of its own.
			pic = start;
			picShot = startShot;
			picDetached = qtrue;
			picDetachTime = predicted->commandTime;
			picBegan = qfalse;
			CG_WeapPicture_Advance( model, first, last, getCmd );
		} else {
			CG_WeapPicture_Follow( predicted );
		}
	}

	if ( picDetached ) {
		// Home again, a real action of your own, one only the run would begin,
		// or a run that never settles.
		if ( ( CG_WeapPicture_AtRest( &pic ) && pic.weapon == predicted->weapon ) ||
			predicted->weaponstate != WEAPON_READY || picBegan ||
			predicted->commandTime - picDetachTime > PICTURE_MAX_DETACH ) {
			CG_WeapPicture_Follow( predicted );
		}
	}
}

// The state the viewmodel draws this frame.
const playerState_t *CG_WeapPicture_State( const playerState_t *predicted ) {
	return picValid && picDetached ? &pic : predicted;
}
