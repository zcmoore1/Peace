/*
===========================================================================
cg_anim.c  --  weapon viewmodel animation state machine

Drives per-weapon skeletal animation via the pose-blending interface
(trap_R_BuildModelPose / trap_R_BlendModelPoses). The result is stored
in cg.weaponPose and passed to the renderer each frame via
refEntity_t::pose.

Layer stack (evaluated low→high, each blended onto the previous):
  LAYER_BASE      idle / fire cycle; always active
  LAYER_MOVE      raise, drop; active during weapon transitions
  LAYER_SPRINT    sprint-in / sprint-loop / sprint-out
  LAYER_RETAINED  current non-reload animation carried through sprint/holster

Each layer has:
  clip     -- index into the weapon's weapAnimClips_t table
  time     -- current position within the clip (ms)
  weight   -- current blend weight [0..1]
  target   -- weight we're lerping toward
  speed    -- weight lerp speed (units/ms)
===========================================================================
*/

#include "cg_local.h"

// ---------------------------------------------------------------------------
// Clip indices used by the viewmodel config.
// ---------------------------------------------------------------------------
#define WANIM_IDLE          0
#define WANIM_FIRE          1
#define WANIM_RAISE         2
#define WANIM_DROP          3
#define WANIM_RELOAD_START  4       // matches RSEQ_START
#define WANIM_RELOAD_LOOP   5       // matches RSEQ_LOOP  (one shell per pass)
#define WANIM_RELOAD_END    6       // matches RSEQ_END
#define WANIM_SPRINT_IN     7
#define WANIM_SPRINT_LOOP   8
#define WANIM_SPRINT_OUT    9
#define WANIM_FIRST_RAISE    10
#define WANIM_COUNT          11

#define ANIM_LAYER_COUNT    4
#define LAYER_BASE          0
#define LAYER_MOVE          1
#define LAYER_SPRINT        2
#define LAYER_RETAINED      3
#define RETAINED_BLEND_SPEED 0.0125f // 80ms picture release; never a gameplay lock
#define MOVE_BLEND_SPEED     0.025f  // 40ms move-layer crossfade into or out of idle
#define MOVE_RETURN_MS       250.0f  // longest picture return after a cancelled transition

// How the move layer's clip clock is driven. Every mode is picture only: the
// gameplay lock decides readiness, and these only decide where the gun is drawn.
#define MOVE_FIT_NONE        0
#define MOVE_FIT_FORWARD     1   // clip runs to its end over the gameplay lock
#define MOVE_FIT_REVERSE     2   // a draw a holster interrupted runs back to its start
#define MOVE_FIT_RETURN      3   // a cancelled transition eases back to the ready pose

typedef struct {
	int     firstFrame;
	int     numFrames;
	float   framerate;      // frames per second
	qboolean loop;
} weapAnimClip_t;

typedef struct {
	weapAnimClip_t clips[WANIM_COUNT];
} weapAnimDef_t;

typedef struct {
	int     clip;           // WANIM_* index
	float   time;           // ms within clip
	float   startFraction;  // picture-only reversal point for sprint/raise/drop transitions
	float   weight;
	float   targetWeight;
	float   blendSpeed;     // weight units per ms
	int     fit;            // MOVE_FIT_* (move layer only)
	float   returnFrom, returnTo, returnTime, returnLength;	// MOVE_FIT_RETURN, in clip ms / real ms
} animLayer_t;

// Per-weapon animation definitions.
// All zeroed = no IQM animation yet; blend tree falls back to frame-number path.
static weapAnimDef_t bg_weapAnimDefs[WP_NUM_WEAPONS];
static qboolean      bg_weapAnimDefsLoaded = qfalse;

// Active layer state for the local player's viewmodel.
static animLayer_t   cg_weapLayers[ANIM_LAYER_COUNT];
static int           cg_weapAnimWeapon = WP_NONE;   // weapon these layers belong to
static qboolean      cg_retainedActive, cg_retainedDrop, cg_previousSprint;
static float         cg_retainedRate = 1.0f;
static qboolean      cg_animContextValid;
static int           cg_animClient, cg_animSpawn, cg_animTeleport, cg_animSlot, cg_animAttachments;

static int CG_WeapAnim_DrawClip(const weapAnimDef_t *def, const playerState_t *ps) {
	return ps->weaponDraw && def->clips[WANIM_FIRST_RAISE].numFrames > 0 ? WANIM_FIRST_RAISE : WANIM_RAISE;
}

// ---------------------------------------------------------------------------
// Clip definitions.
//
// A clip is four numbers: where it starts in the IQM's frame list, how many
// frames it runs, its playback rate, and whether it loops.
//
// Everything starts zeroed and numFrames 0 means "no data" - that clip is
// skipped and the legacy torso path is used instead. So a weapon can be filled
// in one animation at a time rather than being all-or-nothing.
//
// Duration in ms is numFrames / framerate * 1000. That is the SAME number that
// belongs in the reload note table in bg_pmove.c, so the picture and the
// gameplay agree by construction instead of being tuned to match.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// CG_WeapAnim_Init
// Called once on cgame init.
// ---------------------------------------------------------------------------
void CG_WeapAnim_Init( void ) {
	Com_Memset( bg_weapAnimDefs, 0, sizeof(bg_weapAnimDefs) );
	CG_WeapAnim_Reset();
	bg_weapAnimDefsLoaded = qtrue;
}

// Discard pose ownership at a camera cut without losing registered clips.
void CG_WeapAnim_Reset( void ) {
	Com_Memset( cg_weapLayers,   0, sizeof(cg_weapLayers) );
	cg_weapAnimWeapon = WP_NONE;
	cg_retainedActive = cg_retainedDrop = cg_previousSprint = qfalse;
	cg_animContextValid = qfalse;
}

// ---------------------------------------------------------------------------
// CG_WeapAnim_WeaponChanged
// Reset layer state when the player switches weapons.
// ---------------------------------------------------------------------------
static void CG_WeapAnim_WeaponChanged( int weapon ) {
	Com_Memset( cg_weapLayers, 0, sizeof(cg_weapLayers) );
	cg_retainedActive = cg_retainedDrop = cg_previousSprint = qfalse;
	cg_retainedRate = 1.0f;

	// Start with idle on base layer at full weight.
	cg_weapLayers[LAYER_BASE].clip         = WANIM_IDLE;
	cg_weapLayers[LAYER_BASE].weight       = 1.0f;
	cg_weapLayers[LAYER_BASE].targetWeight = 1.0f;
	cg_weapLayers[LAYER_BASE].blendSpeed   = 10.0f;

	// Start raise on the move layer, blend in fast.
	cg_weapLayers[LAYER_MOVE].clip         = WANIM_RAISE;
	cg_weapLayers[LAYER_MOVE].weight       = 0.0f;
	cg_weapLayers[LAYER_MOVE].targetWeight = 1.0f;
	cg_weapLayers[LAYER_MOVE].blendSpeed   = 8.0f;
	cg_weapLayers[LAYER_SPRINT].blendSpeed = 0.02f;

	cg_weapAnimWeapon = weapon;
}

// ---------------------------------------------------------------------------
// CG_WeapAnim_ClipLength
// Duration of a clip in ms. 0 when the weapon has no animation data yet.
// ---------------------------------------------------------------------------
static float CG_WeapAnim_ClipLength( const weapAnimDef_t *def, int clip ) {
	const weapAnimClip_t *c;

	if ( clip < 0 || clip >= WANIM_COUNT ) {
		return 0.0f;
	}
	c = &def->clips[clip];
	if ( c->numFrames <= 0 || c->framerate <= 0.0f ) {
		return 0.0f;
	}
	return (float)c->numFrames * ( 1000.0f / c->framerate );
}

// Transition clips interpolate between their first and last authored poses.
// There are numFrames-1 intervals between those poses, even though the clip's
// metadata duration includes the final frame. Use that span when mapping a
// normalized transition/reversal, so unequal entry/exit frame counts agree.
static float CG_WeapAnim_LastFrameTime( const weapAnimDef_t *def, int clip ) {
	const weapAnimClip_t *c;
	if ( clip < 0 || clip >= WANIM_COUNT ) return 0.0f;
	c = &def->clips[clip];
	if ( c->numFrames < 2 || c->framerate <= 0.0f ) return 0.0f;
	return (float)(c->numFrames - 1) * (1000.0f / c->framerate);
}

// ---------------------------------------------------------------------------
// CG_WeapAnim_TransitionBusy
//
// Whether a one-shot has reached its end. A retained holster is allowed to
// keep that endpoint; otherwise normal sprint presentation can take over.
// ---------------------------------------------------------------------------
static qboolean CG_WeapAnim_TransitionBusy( const weapAnimDef_t *def, const animLayer_t *l ) {
	float len = CG_WeapAnim_ClipLength( def, l->clip );

	if ( len <= 0.0f ) {
		return qfalse;			// no data - never blocks
	}
	return ( l->weight > 0.0f && l->time < len );
}

// ---------------------------------------------------------------------------
// CG_WeapAnim_PlayLayer
// Point a layer at a clip. Restart on a clip change or reactivation after its
// release; repeated active requests do not stutter the animation.
// ---------------------------------------------------------------------------
static void CG_WeapAnim_PlayLayer( animLayer_t *l, int clip ) {
	if ( l->clip != clip || l->targetWeight <= 0.0f ) {
		l->clip = clip;
		l->time = 0.0f;
		l->startFraction = 0.0f;
	}
	l->targetWeight = 1.0f;
}

static qboolean CG_WeapAnim_Retainable(int clip) {
	return clip != WANIM_IDLE && (clip < WANIM_RELOAD_START || clip > WANIM_RELOAD_END);
}

static qboolean CG_WeapAnim_IsDrawClip(int clip) {
	return clip == WANIM_RAISE || clip == WANIM_FIRST_RAISE;
}

// A holster starts from the pose on screen, never from a clip's first frame.
// Interrupting an unfinished draw takes the gun back down the way it came; a
// holster asked for while a cancelled one is still returning carries on from
// there. Only a gun at rest starts the drop clip over, faded in from idle.
static void CG_WeapAnim_BeginHolster(const weapAnimDef_t *def, animLayer_t *move) {
	float len = CG_WeapAnim_ClipLength(def, move->clip);
	qboolean shown = move->weight > 0.0f && move->targetWeight > 0.0f && len > 0.0f;

	if (shown && CG_WeapAnim_IsDrawClip(move->clip) && move->time < len) {
		move->fit = MOVE_FIT_REVERSE;
		move->startFraction = move->time / len;
	} else if (shown && move->clip == WANIM_DROP) {
		move->fit = MOVE_FIT_FORWARD;
		move->startFraction = move->time / len;
		if (move->startFraction > 1.0f) move->startFraction = 1.0f;
	} else {
		move->clip = WANIM_DROP;
		move->time = 0.0f;
		move->startFraction = 0.0f;
		move->fit = MOVE_FIT_FORWARD;
	}
	move->targetWeight = 1.0f;
}

// Gameplay cancelled the transition (YY): the gun is already ready to fire.
// The picture travels back along the clip it was showing - up the draw, or back
// up the drop - instead of cutting to idle. It eases in and out, and takes the
// time that stretch of clip would naturally take, capped at MOVE_RETURN_MS.
static void CG_WeapAnim_BeginReturn(const weapAnimDef_t *def, const playerState_t *ps, animLayer_t *move) {
	float len = CG_WeapAnim_ClipLength(def, move->clip);
	float from = move->time, natural;

	if (len <= 0.0f || move->weight <= 0.0f || move->targetWeight <= 0.0f) return;
	if (from < 0.0f) from = 0.0f;
	if (from > len) from = len;
	if (move->clip == WANIM_DROP) {
		move->returnTo = 0.0f;
		natural = from / len * BG_WeaponDropTime(ps->weapon);
	} else if (CG_WeapAnim_IsDrawClip(move->clip)) {
		move->returnTo = len;
		natural = (len - from) / len * BG_WeaponDrawTime(ps);
	} else {
		return;
	}
	if (natural < 1.0f) return;		// already at rest: an ordinary release
	move->returnFrom = from;
	move->returnTime = 0.0f;
	move->returnLength = natural < MOVE_RETURN_MS ? natural : MOVE_RETURN_MS;
	move->fit = MOVE_FIT_RETURN;
}

// Sprint does not erase an in-flight action. Carry its presentation forward;
// a holster then owns that same clip until its destination completes. This is
// independent of pmove's busy/note clocks and cannot confer weapon readiness.
static void CG_WeapAnim_UpdateRetention(const weapAnimDef_t *def, const playerState_t *ps) {
	animLayer_t *retained = &cg_weapLayers[LAYER_RETAINED];
	qboolean sprinting = (ps->pm_flags & PMF_SPRINTING) ||
		ps->weaponstate == WEAPON_SPRINT_IN || ps->weaponstate == WEAPON_SPRINTING;
	qboolean dropping = ps->weaponstate == WEAPON_DROPPING;
	qboolean released = qfalse;
	qboolean forbidden = (ps->weaponAction & WA_TYPE_MASK) || BG_IsActionWeapon(ps->weapon) ||
		ps->weaponstate == WEAPON_RELOADING ||
		ps->weaponstate == WEAPON_LADDER || ps->stats[STAT_HEALTH] <= 0;
	int i, source = -1;
	retained->blendSpeed = RETAINED_BLEND_SPEED;

	if (forbidden) {
		cg_retainedActive = qfalse;
		retained->weight = retained->targetWeight = 0;
	} else if (cg_retainedActive) {
		if (dropping) cg_retainedDrop = qtrue;
		if ((cg_retainedDrop && !dropping) || (!cg_retainedDrop && !sprinting) ||
			(!dropping && !def->clips[retained->clip].loop &&
			 !CG_WeapAnim_TransitionBusy(def, retained))) {
			cg_retainedActive = qfalse;
			retained->targetWeight = 0;
			released = qtrue;
		}
	}

	if (!forbidden && !released && !cg_retainedActive && retained->targetWeight <= 0 &&
		((sprinting && !cg_previousSprint) || (dropping && cg_previousSprint))) {
		// Choose the animation the player actually saw, not the newly requested
		// weapon state. Reload clips cannot become a retained source.
		for (i=LAYER_BASE; i<=LAYER_SPRINT; i++) {
			animLayer_t *l = &cg_weapLayers[i];
			// A returning cancel is easing back to rest; it is not an action to carry.
			// A holster still fading in from idle is already the action on screen.
			if (l->fit == MOVE_FIT_RETURN) continue;
			if ((l->weight >= .5f || (i == LAYER_MOVE && l->weight > 0)) && l->targetWeight > 0 &&
				CG_WeapAnim_ClipLength(def, l->clip) > 0) source = i;
		}
		// Initial pullout can coincide with the first rendered frame.
		if ((source < 0 || cg_weapLayers[source].clip == WANIM_IDLE) &&
			ps->weaponstate == WEAPON_RAISING && sprinting) {
			animLayer_t *move = &cg_weapLayers[LAYER_MOVE];
			move->clip = CG_WeapAnim_DrawClip(def, ps);
			move->time = (1.0f - (float)ps->weaponTime / BG_WeaponDrawTime(ps)) *
				CG_WeapAnim_ClipLength(def, move->clip);
			source = LAYER_MOVE;
		}
		// Sprint-to-sprint changes already preserve continuity by reversing the
		// posture layer. Retain across competing owners, not within that layer.
		if (source >= 0 && (source != LAYER_SPRINT || dropping) &&
			CG_WeapAnim_Retainable(cg_weapLayers[source].clip) &&
			CG_WeapAnim_ClipLength(def, cg_weapLayers[source].clip) > 0) {
			int duration = 0;
			float length;
			*retained = cg_weapLayers[source];
			retained->weight = retained->targetWeight = 1;
			retained->blendSpeed = RETAINED_BLEND_SPEED;
			cg_retainedActive = qtrue;
			cg_retainedDrop = dropping;
			cg_retainedRate = 1;
			length = CG_WeapAnim_ClipLength(def, retained->clip);
			switch (retained->clip) {
			case WANIM_RAISE: duration = BG_WeaponRaiseTime(ps->weapon); break;
			case WANIM_FIRST_RAISE: duration = BG_WeaponFirstRaiseTime(ps->weapon); break;
			case WANIM_DROP: duration = BG_WeaponDropTime(ps->weapon); break;
			case WANIM_FIRE: duration = BG_WeaponFireLength(ps->weapon); break;
			case WANIM_SPRINT_IN:
				duration = BG_WeaponSprintInTime(ps->weapon);
				length = CG_WeapAnim_LastFrameTime(def, retained->clip); break;
			case WANIM_SPRINT_OUT:
				duration = BG_WeaponSprintOutTime(ps->weapon);
				length = CG_WeapAnim_LastFrameTime(def, retained->clip); break;
			}
			if (duration > 0) cg_retainedRate = length / duration;
		}
	}
	// Actual fire owns the picture immediately; a cosmetic blend is not a lock.
	if (!cg_retainedActive && ps->weaponstate == WEAPON_FIRING)
		retained->weight = retained->targetWeight = 0;
	cg_previousSprint = sprinting && !forbidden;
}

// ---------------------------------------------------------------------------
// CG_WeapAnim_UpdateLayers
// Called every frame with current weapon state. Updates layer weights and
// clip times based on weaponstate, sprint flag, etc.
// ---------------------------------------------------------------------------
static void CG_WeapAnim_UpdateLayers( const weapAnimDef_t *def, playerState_t *ps, int msec ) {
	animLayer_t *base   = &cg_weapLayers[LAYER_BASE];
	animLayer_t *move   = &cg_weapLayers[LAYER_MOVE];
	animLayer_t *sprint = &cg_weapLayers[LAYER_SPRINT];
	int i;
	int baseClip = WANIM_IDLE;

	// Normal layers follow state; a retained source owns only the picture.
	// Its authored loop flag decides whether to loop or hold the final frame.
	qboolean swapping    = ( ps->weaponstate == WEAPON_RAISING ||
	                         ps->weaponstate == WEAPON_DROPPING );
	CG_WeapAnim_UpdateRetention(def, ps);

	// -- Base layer: swap between idle and fire --
	// Firing and pumping share one clip. On a pump gun the shot and the action
	// working are a single authored animation; the split into two states is a
	// gameplay distinction, not a second piece of art, so the picture just
	// keeps running across the boundary.
	if ( ps->weaponstate == WEAPON_FIRING || ps->weaponstate == WEAPON_BOLTING ) {
		baseClip = WANIM_FIRE;
	} else if ( ps->weaponstate == WEAPON_RELOADING ) {
		// The reload is segmented in pmove, so the picture follows the same
		// segment rather than playing one clip over the whole thing. A magazine
		// gun only ever uses LOOP, so it needs no special handling here.
		if ( ps->weaponAnimSeq == RSEQ_START ) {
			baseClip = WANIM_RELOAD_START;
		} else if ( ps->weaponAnimSeq == RSEQ_END ) {
			baseClip = WANIM_RELOAD_END;
		} else {
			baseClip = WANIM_RELOAD_LOOP;
		}
	}
	CG_WeapAnim_PlayLayer( base, baseClip );

	// -- Move layer: raise / drop transitions --
	// Outside sprint retention, a new transition replaces the old one normally,
	// but always from the pose on screen: YY must flow, never snap.
	if ( !swapping && (ps->weaponstate == WEAPON_READY ||
	                         ps->weaponstate == WEAPON_FIRING ||
	                         ps->weaponstate == WEAPON_BOLTING) ) {
		// Gameplay has released the transition (including a reversed holster).
		// Ready means ready: a cancelled clip only eases the picture home, and a
		// shot takes the picture over at once. Its clip is never a lock.
		if ( ps->weaponstate == WEAPON_READY && move->fit != MOVE_FIT_RETURN &&
		     move->fit != MOVE_FIT_NONE ) {
			CG_WeapAnim_BeginReturn( def, ps, move );
		}
		if ( ps->weaponstate != WEAPON_READY || move->fit != MOVE_FIT_RETURN ) {
			move->fit = MOVE_FIT_NONE;
			move->targetWeight = 0.0f;
		}
		move->blendSpeed = MOVE_BLEND_SPEED;
	} else {
		move->blendSpeed = 8.0f;
		if ( ps->weaponstate == WEAPON_RAISING ) {
			CG_WeapAnim_PlayLayer( move, CG_WeapAnim_DrawClip(def, ps) );
			move->fit = MOVE_FIT_FORWARD;
		} else if ( ps->weaponstate == WEAPON_DROPPING ) {
			if ( move->fit != MOVE_FIT_FORWARD || move->clip != WANIM_DROP ) {
				if ( move->fit != MOVE_FIT_REVERSE ) CG_WeapAnim_BeginHolster( def, move );
			}
			// A gun at rest fades into the drop like it fades out of the draw.
			if ( move->clip == WANIM_DROP ) move->blendSpeed = MOVE_BLEND_SPEED;
		} else {
			// A cancel still easing home must not pop when the next state arrives.
			if ( move->fit == MOVE_FIT_RETURN ) move->blendSpeed = MOVE_BLEND_SPEED;
			move->fit = MOVE_FIT_NONE;
			move->targetWeight = 0.0f;
		}
	}

	// -- Sprint layer --
	// Retention is independent of this requested posture. Outside retention,
	// ordinary posture changes follow gameplay immediately, including reversal.
	{
		int clip = -1;
		if ( ps->weaponstate == WEAPON_SPRINT_IN ) {
			clip = WANIM_SPRINT_IN;
		} else if ( ps->weaponstate == WEAPON_SPRINTING ) {
			clip = WANIM_SPRINT_LOOP;
		} else if ( ps->weaponstate == WEAPON_SPRINT_OUT ) {
			clip = WANIM_SPRINT_OUT;
		}
		if ( clip < 0 ) {
			sprint->targetWeight = 0.0f;
		} else {
			float start = 0.0f;
			qboolean reverse = sprint->weight > 0.0f &&
				((clip == WANIM_SPRINT_OUT && sprint->clip == WANIM_SPRINT_IN) ||
				 (clip == WANIM_SPRINT_IN && sprint->clip == WANIM_SPRINT_OUT));
			if ( reverse ) {
				float len = CG_WeapAnim_LastFrameTime( def, sprint->clip );
				if ( len > 0.0f ) start = 1.0f - sprint->time / len;
				if ( start < 0.0f ) start = 0.0f;
				if ( start > 1.0f ) start = 1.0f;
			}
			// A still-held sprint may be requested again after a reversed drop.
			// Resume the already-visible entry, never snap its picture to frame 0.
			if (clip == WANIM_SPRINT_IN && sprint->targetWeight <= 0 &&
				((sprint->clip == clip && sprint->time > 0) ||
				 (cg_weapLayers[LAYER_RETAINED].clip == clip && cg_weapLayers[LAYER_RETAINED].weight > 0))) {
				float len = CG_WeapAnim_LastFrameTime(def, clip);
				float time = cg_weapLayers[LAYER_RETAINED].weight > 0 &&
					cg_weapLayers[LAYER_RETAINED].clip == clip ? cg_weapLayers[LAYER_RETAINED].time : sprint->time;
				if (len > 0) start = time / len;
				if (start > 1) start = 1;
				reverse = qtrue;
			}
			CG_WeapAnim_PlayLayer( sprint, clip );
			if ( reverse ) sprint->startFraction = start;
			// Entry/exit clips already interpolate idle <-> carry. Blending their
			// weight again delays a fast entry and makes a short tap snap to carry.
			if ( CG_WeapAnim_ClipLength( def, clip ) > 0.0f ) sprint->weight = 1.0f;
		}
	}

	if (cg_retainedActive) {
		// Hidden transition clips must not leak back out during cancellation.
		move->weight = move->targetWeight = 0;
		move->fit = MOVE_FIT_NONE;
		sprint->weight = sprint->targetWeight = 0;
	}
	// -- Advance clip times and lerp weights --
	for ( i = 0; i < ANIM_LAYER_COUNT; i++ ) {
		animLayer_t *l = &cg_weapLayers[i];

		// Lerp weight toward target.
		float delta = l->targetWeight - l->weight;
		float step  = l->blendSpeed * (float)msec;
		if ( step > 0.0f ) {
			if ( delta > 0.0f ) {
				l->weight += step < delta ? step : delta;
			} else if ( delta < 0.0f ) {
				l->weight -= step < -delta ? step : -delta;
			}
		}

		// Advance clip time (will be clamped/looped in pose sample).
		if ( l->weight > 0.0f ) {
			l->time += (float)msec * (i == LAYER_RETAINED ? cg_retainedRate : 1.0f);
		}
	}

	// Base pictures follow the PREDICTED anim clock that the gameplay notes fire
	// off, so the frame on screen is the frame the notes are being read from. A
	// loop wrap or a cancelled reload cannot leave the viewmodel on a stale
	// client-side stopwatch, because there is no client-side stopwatch.
	if ( ps->weaponstate == WEAPON_RELOADING ) {
		int duration = BG_ReloadSegmentTime( ps, ps->weaponAnimSeq );
		base->time = duration > 0 ? (float)ps->weaponAnimTime / duration *
		             CG_WeapAnim_ClipLength( def, base->clip ) : 0.0f;
	} else if ( ( ps->weaponstate == WEAPON_FIRING ||
	              ps->weaponstate == WEAPON_BOLTING ) &&
	            ps->weaponAnimSeq == ASEQ_FIRE && ps->weaponAnimTime >= 0 ) {
		int duration = BG_WeaponFireLength( ps->weapon );
		base->time = duration > 0 ? (float)ps->weaponAnimTime / duration *
		             CG_WeapAnim_ClipLength( def, base->clip ) : 0.0f;
	} else if ( ps->weaponstate == WEAPON_FIRING ) {
		// Weapon with no modelled fire animation: no predicted clock exists for
		// it, so fall back to real time since the shot.
		base->time = cg.time - cg.predictedPlayerEntity.muzzleFlashTime;
	}
	// Fit the supplied draw/drop clips to the existing gameplay transitions.
	// When another transition interrupts one, the layer ownership rule above
	// still decides whether the previous clip holds the picture.
	// The reversal point only changes the distance the picture travels, exactly as
	// for sprint entry/exit; the lock it is fitted to is pmove's.
	if ( (ps->weaponstate == WEAPON_RAISING && move->clip == CG_WeapAnim_DrawClip(def, ps)) ||
	     (ps->weaponstate == WEAPON_DROPPING && move->fit == MOVE_FIT_FORWARD && move->clip == WANIM_DROP) ||
	     (ps->weaponstate == WEAPON_DROPPING && move->fit == MOVE_FIT_REVERSE) ) {
		int duration = ps->weaponstate == WEAPON_RAISING ?
		               BG_WeaponDrawTime( ps ) :
		               BG_HolsterTime( ps );	// the same rule pmove stamped with
		float fraction = 1.0f - (float)ps->weaponTime / duration;
		if ( fraction < 0 ) fraction = 0;
		if ( fraction > 1 ) fraction = 1;
		if ( move->fit == MOVE_FIT_REVERSE ) {
			fraction = move->startFraction * ( 1.0f - fraction );
		} else {
			fraction = move->startFraction + ( 1.0f - move->startFraction ) * fraction;
		}
		move->time = fraction * CG_WeapAnim_ClipLength( def, move->clip );
	} else if ( move->fit == MOVE_FIT_RETURN ) {
		float s, eased;
		move->returnTime += (float)msec;
		s = move->returnLength > 0.0f ? move->returnTime / move->returnLength : 1.0f;
		if ( s > 1.0f ) s = 1.0f;
		eased = s * s * ( 3.0f - 2.0f * s );	// smoothstep: no jolt at either end
		move->time = move->returnFrom + ( move->returnTo - move->returnFrom ) * eased;
		if ( s >= 1.0f ) {
			// Home: hand back to idle with the same short crossfade a draw ends on.
			move->fit = MOVE_FIT_NONE;
			move->targetWeight = 0.0f;
			move->blendSpeed = MOVE_BLEND_SPEED;
		}
	}
	// Fit sprint entry/exit to the same predicted transition timers. The
	// reversal point affects only the distance the picture travels, never the
	// busy lock. During a swap this is deliberately NOT re-aimed: non-looping
	// entry holds its final pose, while a looping carry keeps running.
	if ( (ps->weaponstate == WEAPON_SPRINT_IN && sprint->clip == WANIM_SPRINT_IN) ||
	     (ps->weaponstate == WEAPON_SPRINT_OUT && sprint->clip == WANIM_SPRINT_OUT) ) {
		int duration = ps->weaponstate == WEAPON_SPRINT_IN ?
			BG_WeaponSprintInTime( ps->weapon ) : BG_WeaponSprintOutTime( ps->weapon );
		float fraction = 1.0f - (float)ps->weaponTime / duration;
		if ( fraction < 0.0f ) fraction = 0.0f;
		if ( fraction > 1.0f ) fraction = 1.0f;
		fraction = sprint->startFraction + (1.0f - sprint->startFraction) * fraction;
		sprint->time = fraction * CG_WeapAnim_LastFrameTime( def, sprint->clip );
	}
}

// ---------------------------------------------------------------------------
// CG_WeapAnim_SampleLayer
// Sample a single layer into outPose. Returns qfalse if no IQM data available
// (falls back to frame-number path in CG_AddViewWeapon).
// ---------------------------------------------------------------------------
static qboolean CG_WeapAnim_SampleFrame( const weapAnimDef_t *def,
                                       const animLayer_t *layer, refEntity_t *entity ) {
	const weapAnimClip_t *clip;
	float totalMs, frameDuration, clipTime;
	int   frame, oldframe;
	float backlerp;

	if ( layer->clip < 0 || layer->clip >= WANIM_COUNT )
		return qfalse;

	clip = &def->clips[layer->clip];
	if ( clip->numFrames <= 0 || clip->framerate <= 0.0f )
		return qfalse;

	frameDuration = 1000.0f / clip->framerate;
	totalMs       = (float)clip->numFrames * frameDuration;

	clipTime = layer->time;
	if ( clip->loop ) {
		// fmod equivalent
		while ( clipTime >= totalMs ) clipTime -= totalMs;
		while ( clipTime <  0.0f   ) clipTime += totalMs;
	} else {
		if ( clipTime >= totalMs ) clipTime = totalMs - 0.001f;
		if ( clipTime < 0.0f    ) clipTime = 0.0f;
	}

	oldframe = (int)( clipTime / frameDuration );
	frame = oldframe + 1;
	if ( frame >= clip->numFrames ) {
		frame = clip->loop ? 0 : clip->numFrames - 1;
	}
	backlerp = 1.0f - ( (clipTime / frameDuration) - (int)(clipTime / frameDuration) );
	entity->frame = clip->firstFrame + frame;
	entity->oldframe = clip->firstFrame + oldframe;
	entity->backlerp = backlerp;
	return qtrue;
}

static qboolean CG_WeapAnim_SampleLayer( qhandle_t hModel, const weapAnimDef_t *def,
                                      const animLayer_t *layer, modelPose_t *outPose ) {
	refEntity_t sample;
	if ( !CG_WeapAnim_SampleFrame( def, layer, &sample ) ) {
		return qfalse;
	}
	return trap_R_BuildModelPose( hModel, sample.frame, sample.oldframe, sample.backlerp, outPose );
}

// ---------------------------------------------------------------------------
// CG_WeapAnim_BuildPose
// Main entry point. Evaluates all layers and writes the blended result into
// cg.weaponPose. Returns qfalse if no IQM data is available for this weapon,
// in which case the caller should fall back to the legacy frame path.
// ---------------------------------------------------------------------------
qboolean CG_WeapAnim_BuildPose( playerState_t *ps, qhandle_t hModel, int msec ) {
	const weapAnimDef_t *def;
	modelPose_t layerPose;
	int i;
	qboolean anyLayer = qfalse;

	if ( ps->weapon <= WP_NONE || ps->weapon >= WP_NUM_WEAPONS )
		return qfalse;
	if ( !bg_weapAnimDefsLoaded )
		CG_WeapAnim_Init();

	// Reset layer state on weapon change.
	if ( ps->weapon != cg_weapAnimWeapon || (cg_animContextValid &&
		(ps->clientNum != cg_animClient || ps->persistant[PERS_SPAWN_COUNT] != cg_animSpawn ||
		 (ps->eFlags & EF_TELEPORT_BIT) != cg_animTeleport ||
		 ps->stats[STAT_ACTIVE_SLOT] != cg_animSlot ||
		 ps->slotAttachments[ps->stats[STAT_ACTIVE_SLOT]] != cg_animAttachments)) )
		CG_WeapAnim_WeaponChanged( ps->weapon );
	cg_animContextValid = qtrue;
	cg_animClient = ps->clientNum;
	cg_animSpawn = ps->persistant[PERS_SPAWN_COUNT];
	cg_animTeleport = ps->eFlags & EF_TELEPORT_BIT;
	cg_animSlot = ps->stats[STAT_ACTIVE_SLOT];
	cg_animAttachments = ps->slotAttachments[cg_animSlot];

	def = &bg_weapAnimDefs[ps->weapon];

	CG_WeapAnim_UpdateLayers( def, ps, msec );

	// Evaluate layers low→high, blending each onto the accumulated pose.
	for ( i = 0; i < ANIM_LAYER_COUNT; i++ ) {
		animLayer_t *l = &cg_weapLayers[i];

		if ( l->weight <= 0.001f )
			continue;

		if ( !CG_WeapAnim_SampleLayer( hModel, def, l, &layerPose ) )
			continue;

		if ( !anyLayer ) {
			// First active layer: copy directly into output pose.
			cg.weaponPose = layerPose;
			anyLayer = qtrue;
		} else {
			// Blend onto accumulated pose.
			modelPose_t blended;
			trap_R_BlendModelPoses( &blended,
			                        &cg.weaponPose, 1.0f - l->weight,
			                        &layerPose,     l->weight );
			cg.weaponPose = blended;
		}
	}

	return anyLayer;
}

// Apply the pose to the actual IQM entity. Both renderers support IQM frame
// sampling; GL1 currently has no external-pose implementation, so keep the
// active layers' frames as a crossfaded fallback without changing that renderer.
static qboolean CG_WeapAnim_SampleLayers(const weapAnimDef_t *def, refEntity_t *entity) {
	qboolean sampled = qfalse;
	int i;
	for (i=0; i<ANIM_LAYER_COUNT; i++) {
		animLayer_t *layer = &cg_weapLayers[i];
		refEntity_t source;
		if (layer->weight <= .001f || !CG_WeapAnim_SampleFrame(def, layer, &source)) continue;
		if (sampled && layer->weight < .999f) {
			// Frame-only renderers have one interpolation pair, not a full pose
			// stack. Crossfade nearest source/destination samples instead of
			// abruptly changing clips at weight .5 (the old QVM/GL1 pop).
			entity->frame = entity->backlerp >= .5f ? entity->oldframe : entity->frame;
			entity->oldframe = source.backlerp >= .5f ? source.oldframe : source.frame;
			entity->backlerp = layer->weight;
		} else {
			entity->frame = source.frame;
			entity->oldframe = source.oldframe;
			entity->backlerp = source.backlerp;
		}
		sampled = qtrue;
	}
	return sampled;
}

qboolean CG_WeapAnim_HoldsDrop(void) {
	return cg_retainedActive && cg_retainedDrop;
}

qboolean CG_WeapAnim_Apply( playerState_t *ps, refEntity_t *entity, int msec ) {
	if ( ps->weapon <= WP_NONE || ps->weapon >= WP_NUM_WEAPONS ) return qfalse;
	entity->pose = CG_WeapAnim_BuildPose( ps, entity->hModel, msec ) ? &cg.weaponPose : NULL;
#ifdef Q3_VM
	// A nested VM pointer in refEntity_t is not translated by the renderer
	// syscall. Use ordinary IQM frames in QVM builds, just as GL1 does.
	entity->pose = NULL;
#endif
	return CG_WeapAnim_SampleLayers(&bg_weapAnimDefs[ps->weapon], entity);
}

// ---------------------------------------------------------------------------
// CG_WeapAnim_RegisterClips
// Fills in the clip table for one weapon from the text config that sits beside
// its viewmodel. Data, not code: the frame ranges belong to whoever exported
// the model, so changing an export must never mean editing cgame.
//
// Format is one clip per line, "name firstFrame numFrames fps loop", and any
// clip may be omitted - a missing clip has zero length, which every consumer
// here already reads as "no animation for this".
//
// hModel of 0 or a NULL path clears the table, so the weapon falls back to the
// legacy torso-mapped frame path instead of inheriting a previous weapon's.
// ---------------------------------------------------------------------------
void CG_WeapAnim_RegisterClips( int weapon, qhandle_t hModel, const char *cfgPath ) {
	// Indexed by WANIM_*; keep in step with the defines at the top of the file.
	static const char *names[WANIM_COUNT] = {
		"idle", "fire", "raise", "drop",
		"reload_start", "reload_loop", "reload_end",
		"sprint_in", "sprint_loop", "sprint_out", "first_raise"
	};
	weapAnimDef_t *def;
	fileHandle_t   file;
	char           buffer[4096], *cursor, *token;
	int            length, clip, field;
	float          values[4];

	if ( weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS ) {
		return;
	}
	if ( !bg_weapAnimDefsLoaded ) {
		CG_WeapAnim_Init();
	}
	def = &bg_weapAnimDefs[weapon];
	Com_Memset( def, 0, sizeof(*def) );

	if ( !hModel || !cfgPath || !cfgPath[0] ) {
		return;
	}

	length = trap_FS_FOpenFile( cfgPath, &file, FS_READ );
	if ( length < 0 ) {
		return;
	}
	if ( length >= sizeof(buffer) ) {
		trap_FS_FCloseFile( file );
		CG_Printf( "^3Animation config %s is too large\n", cfgPath );
		return;
	}
	trap_FS_Read( buffer, length, file );
	trap_FS_FCloseFile( file );
	buffer[length] = '\0';

	cursor = buffer;
	while ( ( token = COM_Parse( &cursor ) )[0] ) {
		for ( clip = 0; clip < WANIM_COUNT; clip++ ) {
			if ( !Q_stricmp( token, names[clip] ) ) {
				break;
			}
		}
		for ( field = 0; field < 4; field++ ) {
			token = COM_ParseExt( &cursor, qfalse );
			if ( !token[0] ) {
				break;
			}
			values[field] = atof( token );
		}
		// One bad line invalidates the whole table rather than leaving a
		// half-filled one: a clip pointing at frames that are not there draws
		// garbage, and silently drawing garbage is worse than no animation.
		if ( clip == WANIM_COUNT || field != 4 || values[0] < 0 ||
		     values[1] < 1 || values[2] <= 0 ) {
			CG_Printf( "^3Invalid animation config %s\n", cfgPath );
			Com_Memset( def, 0, sizeof(*def) );
			return;
		}
		def->clips[clip].firstFrame = (int)values[0];
		def->clips[clip].numFrames  = (int)values[1];
		def->clips[clip].framerate  = values[2];
		def->clips[clip].loop       = values[3] != 0;
	}
}
