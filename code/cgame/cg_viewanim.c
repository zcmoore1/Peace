/*
===========================================================================
First-person weapon animation by blend weights. See cg_viewanim.h.

NOTE ON THE DATA: the animations and blend times in this file are
placeholders authored for this project. They are not Modern Warfare 2's
animations and have not been measured against it. How the weapon looks when
several animations are mixed depends entirely on these poses, so they are
the first thing to replace.

The SPAS-12 placeholders are arranged on purpose so that its three resting
poses sit a third of a turn apart around the barrel:

    idle      upright                      roll    0
    sprint    on its left side             roll +120
    carried   on its right side, held low  roll -120   (start of the raise,
                                                         end of the drop)

Every animation on its own only ever turns the weapon a third of a turn and
back. A blend that goes straight from the carried pose to the sprint pose,
without passing through idle, takes the short way round, which is through
upside down. Sprint, then idle, then carried again completes the turn. That
is the only reason a roll can come out of this data, and with poses that sit
on the same side of the barrel it does not.
===========================================================================
*/
#include "../qcommon/q_shared.h"
#include "../game/bg_public.h"
#include "cg_viewanim.h"

/*
=============================================================================

ANIMATION DATA

=============================================================================
*/

#define	KEYS( k )	( sizeof( k ) / sizeof( k[0] ) ), k

// ---- shared by every weapon ----

static const viewAnimKey_t va_idleKeys[] = {
	{ 0.00f,	{  0.0f,  0.0f,   0.0f },	{   0.0f,   0.0f,   0.0f } }
};

static const viewAnimKey_t va_fireKeys[] = {
	{ 0.00f,	{  0.0f,  0.0f,   0.0f },	{   0.0f,   0.0f,   0.0f } },
	{ 0.04f,	{ -1.6f,  0.0f,   0.2f },	{  -5.0f,   0.0f,   1.5f } },
	{ 0.25f,	{  0.0f,  0.0f,   0.0f },	{   0.0f,   0.0f,   0.0f } }
};

// The wrists turn the weapon onto its left side in the first instant, then
// the arms lift it in front of the chest with the muzzle up and to the left.
static const viewAnimKey_t va_sprintInKeys[] = {
	{ 0.00f,	{  0.0f,  0.0f,   0.0f },	{   0.0f,   0.0f,   0.0f } },
	{ 0.02f,	{  0.0f,  0.1f,   0.3f },	{  -2.0f,   3.0f, 100.0f } },
	{ 0.25f,	{  0.0f,  1.0f,   3.0f },	{ -25.0f,  35.0f, 120.0f } }
};

static const viewAnimKey_t va_sprintLoopKeys[] = {
	{ 0.000f,	{  0.0f,  1.0f,   3.0f },	{ -25.0f,  35.0f, 120.0f } },
	{ 0.175f,	{  0.0f,  1.5f,   3.5f },	{ -28.0f,  38.0f, 123.0f } },
	{ 0.350f,	{  0.0f,  1.0f,   3.0f },	{ -25.0f,  35.0f, 120.0f } },
	{ 0.525f,	{  0.0f,  0.5f,   2.5f },	{ -22.0f,  32.0f, 117.0f } },
	{ 0.700f,	{  0.0f,  1.0f,   3.0f },	{ -25.0f,  35.0f, 120.0f } }
};

static const viewAnimKey_t va_sprintOutKeys[] = {
	{ 0.00f,	{  0.0f,  1.0f,   3.0f },	{ -25.0f,  35.0f, 120.0f } },
	{ 0.18f,	{  0.0f,  0.3f,   0.8f },	{  -8.0f,  10.0f,  90.0f } },
	{ 0.25f,	{  0.0f,  0.0f,   0.0f },	{   0.0f,   0.0f,   0.0f } }
};

static const viewAnimClip_t va_idle			= { "idle",			KEYS( va_idleKeys ),		qfalse };
static const viewAnimClip_t va_fire			= { "fire",			KEYS( va_fireKeys ),		qfalse };
static const viewAnimClip_t va_sprintIn		= { "sprint_in",	KEYS( va_sprintInKeys ),	qfalse };
static const viewAnimClip_t va_sprintLoop	= { "sprint_loop",	KEYS( va_sprintLoopKeys ),	qtrue };
static const viewAnimClip_t va_sprintOut	= { "sprint_out",	KEYS( va_sprintOutKeys ),	qfalse };

// ---- default weapon: short swaps that match the stock 250 / 200 msec ----

static const viewAnimKey_t va_defRaiseKeys[] = {
	{ 0.00f,	{ -4.0f, -1.0f,  -9.0f },	{  35.0f,  -8.0f, -15.0f } },
	{ 0.25f,	{  0.0f,  0.0f,   0.0f },	{   0.0f,   0.0f,   0.0f } }
};

static const viewAnimKey_t va_defDropKeys[] = {
	{ 0.00f,	{  0.0f,  0.0f,   0.0f },	{   0.0f,   0.0f,   0.0f } },
	{ 0.20f,	{ -4.0f, -1.0f,  -9.0f },	{  35.0f,   8.0f,  15.0f } }
};

static const viewAnimClip_t va_defRaise	= { "raise",	KEYS( va_defRaiseKeys ),	qfalse };
static const viewAnimClip_t va_defDrop	= { "drop",		KEYS( va_defDropKeys ),		qfalse };

static const viewAnimSet_t va_defaultSet = {
	"default",
	{ 10.0f, -4.0f, -6.0f },
	{
		NULL,				// WANIM_NONE
		&va_idle,			// WANIM_IDLE
		&va_defRaise,		// WANIM_RAISE
		&va_defDrop,		// WANIM_DROP
		&va_defRaise,		// WANIM_QUICK_RAISE
		&va_defDrop,		// WANIM_QUICK_DROP
		&va_sprintIn,		// WANIM_SPRINT_IN
		&va_sprintLoop,		// WANIM_SPRINT_LOOP
		&va_sprintOut,		// WANIM_SPRINT_OUT
		&va_fire			// WANIM_FIRE
	},
	{
		0,					// WANIM_NONE
		100,				// WANIM_IDLE
		100,				// WANIM_RAISE
		100,				// WANIM_DROP
		100,				// WANIM_QUICK_RAISE
		100,				// WANIM_QUICK_DROP
		200,				// WANIM_SPRINT_IN
		100,				// WANIM_SPRINT_LOOP
		200,				// WANIM_SPRINT_OUT
		30					// WANIM_FIRE
	}
};

// ---- SPAS-12 ----

// Carried low on the right, lying on its right side. The raise lifts it
// still on its side and only turns it upright as it arrives.
static const viewAnimKey_t va_spasRaiseKeys[] = {
	{ 0.00f,	{ -5.0f, -2.0f,  -9.0f },	{  35.0f, -15.0f, -120.0f } },
	{ 0.20f,	{ -3.0f, -1.5f,  -5.5f },	{  22.0f, -10.0f, -115.0f } },
	{ 0.42f,	{  0.3f,  0.0f,   0.5f },	{  -3.0f,   0.0f,  -10.0f } },
	{ 0.60f,	{  0.0f,  0.0f,   0.0f },	{   0.0f,   0.0f,   0.0f } }
};

// the raise backwards: over onto its right side and down to where it is carried
static const viewAnimKey_t va_spasDropKeys[] = {
	{ 0.00f,	{  0.0f,  0.0f,   0.0f },	{   0.0f,   0.0f,   0.0f } },
	{ 0.12f,	{ -0.5f,  0.5f,  -1.0f },	{   6.0f,   5.0f,  -15.0f } },
	{ 0.30f,	{ -3.0f, -1.0f,  -5.5f },	{  22.0f,  -8.0f,  -90.0f } },
	{ 0.45f,	{ -5.0f, -2.0f,  -9.0f },	{  35.0f, -15.0f, -120.0f } }
};

// the quick versions only dip the weapon part of the way
static const viewAnimKey_t va_spasQuickRaiseKeys[] = {
	{ 0.00f,	{ -4.0f, -2.0f,  -8.0f },	{  30.0f, -10.0f, -60.0f } },
	{ 0.12f,	{ -1.5f, -0.5f,  -3.0f },	{  12.0f,  -4.0f, -25.0f } },
	{ 0.25f,	{  0.0f,  0.0f,   0.0f },	{   0.0f,   0.0f,   0.0f } }
};

static const viewAnimKey_t va_spasQuickDropKeys[] = {
	{ 0.00f,	{  0.0f,  0.0f,   0.0f },	{   0.0f,   0.0f,   0.0f } },
	{ 0.08f,	{ -1.0f,  0.0f,  -2.0f },	{  10.0f,  -3.0f, -20.0f } },
	{ 0.20f,	{ -4.0f, -2.0f,  -8.0f },	{  30.0f, -10.0f, -60.0f } }
};

static const viewAnimClip_t va_spasRaise		= { "spas_raise",		KEYS( va_spasRaiseKeys ),		qfalse };
static const viewAnimClip_t va_spasDrop			= { "spas_drop",		KEYS( va_spasDropKeys ),		qfalse };
static const viewAnimClip_t va_spasQuickRaise	= { "spas_quick_raise",	KEYS( va_spasQuickRaiseKeys ),	qfalse };
static const viewAnimClip_t va_spasQuickDrop	= { "spas_quick_drop",	KEYS( va_spasQuickDropKeys ),	qfalse };

static const viewAnimSet_t va_spas12Set = {
	"spas12",
	{ 10.0f, -4.0f, -6.0f },
	{
		NULL,				// WANIM_NONE
		&va_idle,			// WANIM_IDLE
		&va_spasRaise,		// WANIM_RAISE
		&va_spasDrop,		// WANIM_DROP
		&va_spasQuickRaise,	// WANIM_QUICK_RAISE
		&va_spasQuickDrop,	// WANIM_QUICK_DROP
		&va_sprintIn,		// WANIM_SPRINT_IN
		&va_sprintLoop,		// WANIM_SPRINT_LOOP
		&va_sprintOut,		// WANIM_SPRINT_OUT
		&va_fire			// WANIM_FIRE
	},
	{
		0,					// WANIM_NONE
		200,				// WANIM_IDLE
		200,				// WANIM_RAISE
		200,				// WANIM_DROP
		100,				// WANIM_QUICK_RAISE
		100,				// WANIM_QUICK_DROP
		200,				// WANIM_SPRINT_IN
		100,				// WANIM_SPRINT_LOOP
		200,				// WANIM_SPRINT_OUT
		30					// WANIM_FIRE
	}
};

/*
===============
ViewAnim_SetForWeapon
===============
*/
const viewAnimSet_t *ViewAnim_SetForWeapon( int weapon ) {
	switch ( weapon ) {
	case WP_SHOTGUN:
		return &va_spas12Set;
	default:
		return &va_defaultSet;
	}
}

/*
=============================================================================

ROTATION HELPERS

Quaternions are x, y, z, w. An axis is the usual three rows forward, left, up.

=============================================================================
*/

static void VA_QuatIdentity( quat_t q ) {
	q[0] = 0.0f;
	q[1] = 0.0f;
	q[2] = 0.0f;
	q[3] = 1.0f;
}

static float VA_QuatDot( const quat_t a, const quat_t b ) {
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

static void VA_QuatNormalize( quat_t q ) {
	float	len;

	len = sqrt( VA_QuatDot( q, q ) );
	if ( len < 0.0001f ) {
		VA_QuatIdentity( q );
		return;
	}
	len = 1.0f / len;
	q[0] *= len;
	q[1] *= len;
	q[2] *= len;
	q[3] *= len;
}

static void VA_AnglesToQuat( const vec3_t angles, quat_t q ) {
	vec3_t	axis[3];
	float	m00, m01, m02, m10, m11, m12, m20, m21, m22;
	float	trace, s;

	AnglesToAxis( angles, axis );

	// the rows of an axis are the columns of the rotation matrix
	m00 = axis[0][0];	m01 = axis[1][0];	m02 = axis[2][0];
	m10 = axis[0][1];	m11 = axis[1][1];	m12 = axis[2][1];
	m20 = axis[0][2];	m21 = axis[1][2];	m22 = axis[2][2];

	trace = m00 + m11 + m22;
	if ( trace > 0.0f ) {
		s = sqrt( trace + 1.0f ) * 2.0f;
		q[3] = 0.25f * s;
		q[0] = ( m21 - m12 ) / s;
		q[1] = ( m02 - m20 ) / s;
		q[2] = ( m10 - m01 ) / s;
	} else if ( m00 > m11 && m00 > m22 ) {
		s = sqrt( 1.0f + m00 - m11 - m22 ) * 2.0f;
		q[3] = ( m21 - m12 ) / s;
		q[0] = 0.25f * s;
		q[1] = ( m01 + m10 ) / s;
		q[2] = ( m02 + m20 ) / s;
	} else if ( m11 > m22 ) {
		s = sqrt( 1.0f + m11 - m00 - m22 ) * 2.0f;
		q[3] = ( m02 - m20 ) / s;
		q[0] = ( m01 + m10 ) / s;
		q[1] = 0.25f * s;
		q[2] = ( m12 + m21 ) / s;
	} else {
		s = sqrt( 1.0f + m22 - m00 - m11 ) * 2.0f;
		q[3] = ( m10 - m01 ) / s;
		q[0] = ( m02 + m20 ) / s;
		q[1] = ( m12 + m21 ) / s;
		q[2] = 0.25f * s;
	}
	VA_QuatNormalize( q );
}

/*
===============
ViewAnim_QuatToAxis
===============
*/
void ViewAnim_QuatToAxis( const quat_t q, vec3_t axis[3] ) {
	float	x, y, z, w;

	x = q[0];
	y = q[1];
	z = q[2];
	w = q[3];

	axis[0][0] = 1.0f - 2.0f * ( y * y + z * z );
	axis[0][1] = 2.0f * ( x * y + w * z );
	axis[0][2] = 2.0f * ( x * z - w * y );

	axis[1][0] = 2.0f * ( x * y - w * z );
	axis[1][1] = 1.0f - 2.0f * ( x * x + z * z );
	axis[1][2] = 2.0f * ( y * z + w * x );

	axis[2][0] = 2.0f * ( x * z + w * y );
	axis[2][1] = 2.0f * ( y * z - w * x );
	axis[2][2] = 1.0f - 2.0f * ( x * x + y * y );
}

/*
===============
VA_QuatAccumulate

Adds weight * q to the running total. q and -q are the same rotation, so the
one closer to the total is used. Otherwise two nearly identical poses could
cancel each other out.
===============
*/
static void VA_QuatAccumulate( quat_t total, const quat_t q, float weight ) {
	if ( VA_QuatDot( total, q ) < 0.0f ) {
		weight = -weight;
	}
	total[0] += q[0] * weight;
	total[1] += q[1] * weight;
	total[2] += q[2] * weight;
	total[3] += q[3] * weight;
}

/*
=============================================================================

SAMPLING

=============================================================================
*/

/*
===============
ViewAnim_SampleClip

A clip that does not loop holds its last key once it has played through.
===============
*/
void ViewAnim_SampleClip( const viewAnimClip_t *clip, float time, viewAnimPose_t *pose ) {
	const viewAnimKey_t	*a, *b;
	float	length, frac;
	quat_t	qa, qb;
	int		i;

	if ( !clip || clip->numKeys < 1 ) {
		VectorClear( pose->origin );
		VA_QuatIdentity( pose->rot );
		return;
	}

	length = clip->keys[clip->numKeys - 1].time;

	if ( time < 0.0f ) {
		time = 0.0f;
	}
	if ( clip->loop && length > 0.0f ) {
		time -= length * floor( time / length );
	} else if ( time > length ) {
		time = length;
	}

	// find the pair of keys around time
	for ( i = 0 ; i < clip->numKeys - 1 ; i++ ) {
		if ( time < clip->keys[i + 1].time ) {
			break;
		}
	}
	a = &clip->keys[i];
	if ( i >= clip->numKeys - 1 ) {
		b = a;
		frac = 0.0f;
	} else {
		b = &clip->keys[i + 1];
		frac = ( time - a->time ) / ( b->time - a->time );
	}

	pose->origin[0] = a->origin[0] + ( b->origin[0] - a->origin[0] ) * frac;
	pose->origin[1] = a->origin[1] + ( b->origin[1] - a->origin[1] ) * frac;
	pose->origin[2] = a->origin[2] + ( b->origin[2] - a->origin[2] ) * frac;

	VA_AnglesToQuat( a->angles, qa );
	VA_AnglesToQuat( b->angles, qb );
	pose->rot[0] = qa[0] * ( 1.0f - frac );
	pose->rot[1] = qa[1] * ( 1.0f - frac );
	pose->rot[2] = qa[2] * ( 1.0f - frac );
	pose->rot[3] = qa[3] * ( 1.0f - frac );
	VA_QuatAccumulate( pose->rot, qb, frac );
	VA_QuatNormalize( pose->rot );
}

/*
=============================================================================

THE ANIMATOR

=============================================================================
*/

/*
===============
ViewAnim_Reset
===============
*/
void ViewAnim_Reset( viewAnimator_t *va ) {
	memset( va, 0, sizeof( *va ) );
}

/*
===============
ViewAnim_Start

This is the whole rule. The new animation heads for a weight of 1, every
other animation heads for 0, and all of them take blendTime to get there
from wherever their weights are right now. Nothing is cleared, so whatever
was still fading keeps contributing while it fades.
===============
*/
static void ViewAnim_Start( viewAnimator_t *va, int anim, float blendTime ) {
	viewAnimSlot_t	*slot;
	int				i;

	for ( i = 0 ; i < WANIM_NUM ; i++ ) {
		slot = &va->slots[i];

		if ( i == anim ) {
			slot->goalWeight = 1.0f;
			slot->time = 0.0f;
		} else {
			slot->goalWeight = 0.0f;
		}

		if ( blendTime > 0.0f ) {
			slot->goalTime = blendTime;
		} else {
			slot->goalTime = 0.0f;
			slot->weight = slot->goalWeight;
		}
	}
}

/*
===============
ViewAnim_Update

Call once per rendered frame with the predicted player state.

Only the value of weapAnim at the moment of the call is seen. If the weapon
state machine went through several animations since the last call, the ones
in between were never started here.
===============
*/
void ViewAnim_Update( viewAnimator_t *va, int weapon, int weapAnim, int time ) {
	const viewAnimSet_t	*set;
	viewAnimSlot_t		*slot;
	qboolean			fresh;
	float				dt;
	int					anim;
	int					i;

	fresh = qfalse;

	// A different weapon is a different view model. Nothing of the old one is
	// left to blend from, so its first animation starts at full weight.
	// Time running backwards means the level restarted.
	if ( !va->valid || weapon != va->weapon || time < va->time ) {
		ViewAnim_Reset( va );
		va->valid = qtrue;
		va->weapon = weapon;
		va->weapAnim = -1;
		va->time = time;
		fresh = qtrue;
	}

	dt = ( time - va->time ) * 0.001f;
	va->time = time;

	if ( weapAnim != va->weapAnim ) {
		va->weapAnim = weapAnim;

		anim = weapAnim & ~WEAPANIM_TOGGLEBIT;
		if ( anim < 0 || anim >= WANIM_NUM ) {
			anim = WANIM_NONE;
		}

		set = ViewAnim_SetForWeapon( weapon );
		ViewAnim_Start( va, anim, fresh ? 0.0f : set->blendMsec[anim] * 0.001f );
	}

	// move every weight toward its goal and run the playheads
	for ( i = 0 ; i < WANIM_NUM ; i++ ) {
		slot = &va->slots[i];

		if ( slot->goalTime > 0.0f ) {
			if ( dt >= slot->goalTime ) {
				slot->weight = slot->goalWeight;
				slot->goalTime = 0.0f;
			} else {
				slot->weight += ( slot->goalWeight - slot->weight ) * ( dt / slot->goalTime );
				slot->goalTime -= dt;
			}
		}

		if ( slot->weight > 0.0f || slot->goalWeight > 0.0f ) {
			slot->time += dt;
		}
	}
}

/*
===============
ViewAnim_Pose

The weighted mix of every animation that has any weight.
===============
*/
void ViewAnim_Pose( const viewAnimator_t *va, viewAnimPose_t *pose ) {
	const viewAnimSet_t		*set;
	const viewAnimSlot_t	*slot;
	viewAnimPose_t			sample;
	float					total;
	int						i;

	VectorClear( pose->origin );
	pose->rot[0] = 0.0f;
	pose->rot[1] = 0.0f;
	pose->rot[2] = 0.0f;
	pose->rot[3] = 0.0f;
	total = 0.0f;

	if ( va->valid ) {
		set = ViewAnim_SetForWeapon( va->weapon );

		for ( i = 0 ; i < WANIM_NUM ; i++ ) {
			slot = &va->slots[i];
			if ( slot->weight <= 0.0f ) {
				continue;
			}

			ViewAnim_SampleClip( set->clips[i], slot->time, &sample );

			VectorMA( pose->origin, slot->weight, sample.origin, pose->origin );
			VA_QuatAccumulate( pose->rot, sample.rot, slot->weight );
			total += slot->weight;
		}
	}

	if ( total <= 0.0f ) {
		VectorClear( pose->origin );
		VA_QuatIdentity( pose->rot );
		return;
	}

	VectorScale( pose->origin, 1.0f / total, pose->origin );
	VA_QuatNormalize( pose->rot );
}
