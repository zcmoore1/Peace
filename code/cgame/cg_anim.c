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
  LAYER_SPRINT    sprint-in / sprint-loop; blends in when sprinting
  LAYER_ADDITIVE  reserved for procedural additive (recoil, sway)

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
// Clip indices -- keep in sync with weapAnimClips_t in cg_anim.h
// ---------------------------------------------------------------------------
#define WANIM_IDLE          0
#define WANIM_FIRE          1
#define WANIM_RAISE         2
#define WANIM_DROP          3
#define WANIM_RELOAD        4
#define WANIM_SPRINT_IN     5
#define WANIM_SPRINT_LOOP   6
#define WANIM_SPRINT_OUT    7
#define WANIM_COUNT         8

#define ANIM_LAYER_COUNT    4
#define LAYER_BASE          0
#define LAYER_MOVE          1
#define LAYER_SPRINT        2
#define LAYER_ADDITIVE      3

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
	float   weight;
	float   targetWeight;
	float   blendSpeed;     // weight units per ms
} animLayer_t;

// Per-weapon animation definitions.
// All zeroed = no IQM animation yet; blend tree falls back to frame-number path.
static weapAnimDef_t bg_weapAnimDefs[WP_NUM_WEAPONS];
static qboolean      bg_weapAnimDefsLoaded = qfalse;

// Active layer state for the local player's viewmodel.
static animLayer_t   cg_weapLayers[ANIM_LAYER_COUNT];
static int           cg_weapAnimWeapon = WP_NONE;   // weapon these layers belong to

// ---------------------------------------------------------------------------
// CG_WeapAnim_Init
// Called once on cgame init. Placeholder: no clips defined until IQM assets land.
// ---------------------------------------------------------------------------
void CG_WeapAnim_Init( void ) {
	Com_Memset( bg_weapAnimDefs, 0, sizeof(bg_weapAnimDefs) );
	Com_Memset( cg_weapLayers,   0, sizeof(cg_weapLayers) );
	cg_weapAnimWeapon = WP_NONE;
	bg_weapAnimDefsLoaded = qtrue;
}

// ---------------------------------------------------------------------------
// CG_WeapAnim_WeaponChanged
// Reset layer state when the player switches weapons.
// ---------------------------------------------------------------------------
static void CG_WeapAnim_WeaponChanged( int weapon ) {
	Com_Memset( cg_weapLayers, 0, sizeof(cg_weapLayers) );

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

// ---------------------------------------------------------------------------
// CG_WeapAnim_TransitionBusy
//
// A transition clip OWNS its layer until it has played out. This is the general
// rule that produces the "still swap": the weapon state machine is free to move
// on underneath - start a holster, defer a sprint - but the picture does not
// restart mid-transition. So a swap requested during a raise runs its full
// timer while the raise keeps playing, and the next thing you see is the new
// weapon's raise. No drop is ever drawn, and nothing here knows what a still
// swap is; it is just a clip refusing to be cut short.
//
// Returns qfalse when the weapon has no animation data, so the fallback path
// behaves exactly as before.
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
// Point a layer at a clip. Restarts the clip only when it actually changes,
// so a repeated request does not stutter the animation.
// ---------------------------------------------------------------------------
static void CG_WeapAnim_PlayLayer( animLayer_t *l, int clip ) {
	if ( l->clip != clip ) {
		l->clip = clip;
		l->time = 0.0f;
	}
	l->targetWeight = 1.0f;
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

	// -- Base layer: swap between idle and fire --
	if ( ps->weaponstate == WEAPON_FIRING ) {
		base->clip = WANIM_FIRE;
	} else if ( ps->weaponstate == WEAPON_RELOADING ) {
		base->clip = WANIM_RELOAD;
	} else {
		base->clip = WANIM_IDLE;
	}

	// -- Move layer: raise / drop transitions --
	// Only re-aimed while the current transition has finished. THIS is the
	// still swap: press swap during a raise and the state machine holsters on
	// its normal timer, but the raise owns the layer and no drop is drawn.
	if ( !CG_WeapAnim_TransitionBusy( def, move ) ) {
		if ( ps->weaponstate == WEAPON_RAISING ) {
			CG_WeapAnim_PlayLayer( move, WANIM_RAISE );
		} else if ( ps->weaponstate == WEAPON_DROPPING ) {
			CG_WeapAnim_PlayLayer( move, WANIM_DROP );
		} else {
			move->targetWeight = 0.0f;
		}
	}

	// -- Sprint layer --
	// Sprint blend is driven by weaponTime during the stow (like NAC): if the
	// stow is instant the sprint blend also completes instantly for free.
	// For now wire to a simple ps->pm_flags sprint check (placeholder until
	// PMF_SPRINTING exists).
	// Same ownership rule. Note pmove already refuses to enter the sprint carry
	// during a raise, so a held sprint is deferred there too - the picture and
	// the state machine agree without either one being told about the other.
	if ( !CG_WeapAnim_TransitionBusy( def, sprint ) ) {
		if ( ps->weaponstate == WEAPON_SPRINT_IN ) {
			CG_WeapAnim_PlayLayer( sprint, WANIM_SPRINT_IN );
		} else if ( ps->weaponstate == WEAPON_SPRINTING ) {
			CG_WeapAnim_PlayLayer( sprint, WANIM_SPRINT_LOOP );
		} else if ( ps->weaponstate == WEAPON_SPRINT_OUT ) {
			CG_WeapAnim_PlayLayer( sprint, WANIM_SPRINT_OUT );
		} else {
			sprint->targetWeight = 0.0f;
		}
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
			l->time += (float)msec;
		}
	}
}

// ---------------------------------------------------------------------------
// CG_WeapAnim_SampleLayer
// Sample a single layer into outPose. Returns qfalse if no IQM data available
// (falls back to frame-number path in CG_AddViewWeapon).
// ---------------------------------------------------------------------------
static qboolean CG_WeapAnim_SampleLayer( qhandle_t hModel, const weapAnimDef_t *def,
                                          const animLayer_t *layer, modelPose_t *outPose ) {
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

	frame    = clip->firstFrame + (int)( clipTime / frameDuration );
	backlerp = 1.0f - ( (clipTime / frameDuration) - (int)(clipTime / frameDuration) );
	oldframe = frame > clip->firstFrame ? frame - 1 : clip->firstFrame;

	return trap_R_BuildModelPose( hModel, frame, oldframe, backlerp, outPose );
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

	if ( !bg_weapAnimDefsLoaded )
		CG_WeapAnim_Init();

	// Reset layer state on weapon change.
	if ( ps->weapon != cg_weapAnimWeapon )
		CG_WeapAnim_WeaponChanged( ps->weapon );

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

// ---------------------------------------------------------------------------
// CG_WeapAnim_RegisterClips
// Called from CG_RegisterWeapon once IQM assets exist. Fills in the clip
// table for the given weapon from a simple text config.
// Placeholder: no-op until assets land. The frame ranges below are zeroed
// stubs that will be filled in per-weapon.
// ---------------------------------------------------------------------------
void CG_WeapAnim_RegisterClips( int weapon, qhandle_t hModel ) {
	weapAnimDef_t *def = &bg_weapAnimDefs[weapon];
	(void)hModel;
	Com_Memset( def, 0, sizeof(*def) );
	// Frame ranges will be populated here once IQM weapon models are in place.
	// Example (Kar98k - values TBD from actual export):
	//   def->clips[WANIM_IDLE].firstFrame  = 0;
	//   def->clips[WANIM_IDLE].numFrames   = 60;
	//   def->clips[WANIM_IDLE].framerate   = 30.0f;
	//   def->clips[WANIM_IDLE].loop        = qtrue;
	//   ... etc.
}
