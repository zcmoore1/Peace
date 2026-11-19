/*
===========================================================================
First-person weapon animation by blend weights.

Every animation of a view model owns a weight. Starting an animation does
not stop the others: it gives the new one a goal weight of 1 and every other
one a goal weight of 0, and all of them move toward their goals over the
same blend time. The pose that is drawn is the weighted mix of every
animation whose weight is not zero.

When animations are started faster than they can blend, three or more of
them are mixed at once and the weapon passes through poses that none of the
animations contain. Nothing in here knows about any particular input
sequence.

This file only depends on q_shared.h and bg_public.h so that it can also be
built into the offline simulator in tests/weaponsim.
===========================================================================
*/
#ifndef __CG_VIEWANIM_H__
#define __CG_VIEWANIM_H__

// One key of an animation. The pose is relative to the idle pose of the
// view model and expressed in view space: origin is forward, left, up in
// world units, angles are pitch, yaw, roll in degrees.
typedef struct {
	float		time;		// seconds from the start of the animation
	vec3_t		origin;
	vec3_t		angles;
} viewAnimKey_t;

typedef struct {
	const char			*name;
	int					numKeys;
	const viewAnimKey_t	*keys;
	qboolean			loop;
} viewAnimClip_t;

// Everything the animator needs to know about one view model.
typedef struct {
	const char				*name;
	vec3_t					pivot;					// point the pose rotates about (forward, left, up)
	const viewAnimClip_t	*clips[WANIM_NUM];		// NULL plays as the idle pose
	int						blendMsec[WANIM_NUM];	// blend time used when that animation starts
} viewAnimSet_t;

typedef struct {
	float		weight;
	float		goalWeight;
	float		goalTime;		// seconds left until weight reaches goalWeight
	float		time;			// seconds since this animation was last started
} viewAnimSlot_t;

typedef struct {
	qboolean		valid;
	int				weapon;			// view model the slots belong to
	int				weapAnim;		// last playerState_t.weapAnim seen, toggle bit included
	int				time;			// time of the last update in msec
	viewAnimSlot_t	slots[WANIM_NUM];
} viewAnimator_t;

typedef struct {
	vec3_t		origin;		// forward, left, up
	quat_t		rot;		// x, y, z, w
} viewAnimPose_t;

const viewAnimSet_t *ViewAnim_SetForWeapon( int weapon );

void ViewAnim_Reset( viewAnimator_t *va );
void ViewAnim_Update( viewAnimator_t *va, int weapon, int weapAnim, int time );
void ViewAnim_Pose( const viewAnimator_t *va, viewAnimPose_t *pose );

void ViewAnim_SampleClip( const viewAnimClip_t *clip, float time, viewAnimPose_t *pose );
void ViewAnim_QuatToAxis( const quat_t q, vec3_t axis[3] );

#endif	// __CG_VIEWANIM_H__
