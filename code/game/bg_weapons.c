/*
===========================================================================
Shared weapon definitions. See bg_weapons.h.

NOTE ON THE NUMBERS: the timings below are placeholders. They are in the
range these states take in Modern Warfare 2, but they have not been
measured against it. Replace them with measured values per weapon; nothing
else in the code depends on what they are.
===========================================================================
*/
#include "../qcommon/q_shared.h"
#include "bg_public.h"

// used by every weapon that has no entry of its own. Raise and drop are the
// values Quake 3 always used, and the quick variants match them, so stock
// weapons switch exactly as they did before.
static const bgWeaponDef_t bg_defaultWeaponDef = {
	"default",
	250,	// raiseTime
	200,	// dropTime
	250,	// quickRaiseTime
	200,	// quickDropTime
	200,	// sprintInTime
	200		// sprintOutTime
};

// lives in the WP_SHOTGUN slot until the project has its own weapon list
static const bgWeaponDef_t bg_spas12WeaponDef = {
	"spas12",
	750,	// raiseTime
	500,	// dropTime
	400,	// quickRaiseTime
	250,	// quickDropTime
	250,	// sprintInTime
	250		// sprintOutTime
};

// lives in the WP_RAILGUN slot until the project has its own weapon list
static const bgWeaponDef_t bg_interventionWeaponDef = {
	"intervention",
	900,	// raiseTime
	600,	// dropTime
	450,	// quickRaiseTime
	300,	// quickDropTime
	300,	// sprintInTime
	300		// sprintOutTime
};

/*
===============
BG_WeaponDef
===============
*/
const bgWeaponDef_t *BG_WeaponDef( int weapon ) {
	switch ( weapon ) {
	case WP_SHOTGUN:
		return &bg_spas12WeaponDef;
	case WP_RAILGUN:
		return &bg_interventionWeaponDef;
	default:
		return &bg_defaultWeaponDef;
	}
}
