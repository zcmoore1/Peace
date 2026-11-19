/*
===========================================================================
Shared weapon definitions.

Everything in here is read by both the game and cgame modules, so the
predicted weapon state machine in bg_pmove.c and the first-person view
animation in cgame agree on timings without any extra networking.
===========================================================================
*/
#ifndef __BG_WEAPONS_H__
#define __BG_WEAPONS_H__

// First-person weapon animations.
//
// The weapon state machine in bg_pmove.c picks one of these whenever it
// enters a state and stores it in playerState_t.weapAnim. It never looks at
// what the view model is doing; the client simply reacts to the value
// changing. All blending between animations happens in cg_viewanim.c.
typedef enum {
	WANIM_NONE,
	WANIM_IDLE,
	WANIM_RAISE,
	WANIM_DROP,
	WANIM_QUICK_RAISE,
	WANIM_QUICK_DROP,
	WANIM_SPRINT_IN,
	WANIM_SPRINT_LOOP,
	WANIM_SPRINT_OUT,
	WANIM_FIRE,

	WANIM_NUM
} weaponAnim_t;

// flipped every time an animation is (re)started, so that starting the
// animation that is already playing is still visible to the client
#define	WEAPANIM_TOGGLEBIT	128
#define	WEAPANIM_BITS		8		// width of playerState_t.weapAnim on the wire

// Per-weapon timings for the state machine, in milliseconds.
typedef struct {
	const char	*name;

	int			raiseTime;			// WEAPON_RAISING
	int			dropTime;			// WEAPON_DROPPING
	int			quickRaiseTime;		// WEAPON_RAISING_QUICK
	int			quickDropTime;		// WEAPON_DROPPING_QUICK
	int			sprintInTime;		// WEAPON_SPRINT_RAISE
	int			sprintOutTime;		// WEAPON_SPRINT_DROP
} bgWeaponDef_t;

const bgWeaponDef_t *BG_WeaponDef( int weapon );

#endif	// __BG_WEAPONS_H__
