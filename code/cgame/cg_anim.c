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
#define WANIM_COUNT         10

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
	int baseClip = WANIM_IDLE;

	// IW4 still swap. When a weapon swap collides with the sprint carry, the
	// SPRINT animation wins and the swap is never drawn.
	//
	// This is deliberately NOT configurable and must not be made configurable.
	// The other CoD family (Treyarch) lets the swap anim take priority instead;
	// that is a different game and Peace is not it. This behaviour is load
	// bearing for how the weapon system is meant to feel, so it is expressed as
	// a rule in code rather than a cvar that could be archived to the wrong
	// value and quietly change the feel of the game.
	//
	// Two consequences fall out of "hold the layer and stop re-aiming it", and
	// neither is coded for directly:
	//   - Swapping out of the sprint-IN transition holds that clip's final
	//     frame - the last pose before the full sprint would have begun -
	//     because CG_WeapAnim_SampleLayer clamps a NON-LOOPING clip to its end.
	//   - Swapping out of the full sprint carry keeps the carry running, because
	//     that clip is LOOPING and the same clamp does not apply.
	// The clip's loop flag decides which of the two happens. There is no branch.
	//
	// The weapon state machine is untouched either way: the holster still runs
	// its full BG_WeaponDropTime, so this changes the tell, never the timing.
	qboolean swapping    = ( ps->weaponstate == WEAPON_RAISING ||
	                         ps->weaponstate == WEAPON_DROPPING );
	qboolean sprintHolds = ( swapping && sprint->weight > 0.0f );

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
	// Only re-aimed while the current transition has finished. THIS is the
	// still swap: press swap during a raise and the state machine holsters on
	// its normal timer, but the raise owns the layer and no drop is drawn.
	if ( sprintHolds ) {
		move->targetWeight = 0.0f;			// swap anim suppressed
	} else if ( !CG_WeapAnim_TransitionBusy( def, move ) ) {
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
	if ( sprintHolds ) {
		sprint->targetWeight = 1.0f;		// hold whatever it was already playing
	} else if ( !CG_WeapAnim_TransitionBusy( def, sprint ) ) {
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

	// Base pictures follow the PREDICTED anim clock that the gameplay notes fire
	// off, so the frame on screen is the frame the notes are being read from. A
	// loop wrap or a cancelled reload cannot leave the viewmodel on a stale
	// client-side stopwatch, because there is no client-side stopwatch.
	if ( ps->weaponstate == WEAPON_RELOADING ) {
		int duration = BG_WeaponReloadSegLength( ps->weapon, ps->weaponAnimSeq );
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
	if ( (ps->weaponstate == WEAPON_RAISING && move->clip == WANIM_RAISE) ||
	     (ps->weaponstate == WEAPON_DROPPING && move->clip == WANIM_DROP) ) {
		int duration = ps->weaponstate == WEAPON_RAISING ?
		               BG_WeaponRaiseTime( ps->weapon ) : BG_WeaponDropTime( ps->weapon );
		float fraction = 1.0f - (float)ps->weaponTime / duration;
		if ( fraction < 0 ) fraction = 0;
		if ( fraction > 1 ) fraction = 1;
		move->time = fraction * CG_WeapAnim_ClipLength( def, move->clip );
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

// Apply the pose to the actual IQM entity. Both renderers support IQM frame
// sampling; GL1 currently has no external-pose implementation, so keep the
// highest active layer's frames as its fallback without changing that renderer.
qboolean CG_WeapAnim_Apply( playerState_t *ps, refEntity_t *entity, int msec ) {
	qboolean sampled = qfalse;
	int i;
	if ( ps->weapon <= WP_NONE || ps->weapon >= WP_NUM_WEAPONS ) return qfalse;
	entity->pose = CG_WeapAnim_BuildPose( ps, entity->hModel, msec ) ? &cg.weaponPose : NULL;
#ifdef Q3_VM
	// A nested VM pointer in refEntity_t is not translated by the renderer
	// syscall. Use ordinary IQM frames in QVM builds, just as GL1 does.
	entity->pose = NULL;
#endif
	for ( i = 0; i < ANIM_LAYER_COUNT; i++ ) {
		if ( cg_weapLayers[i].weight < 0.5f ) continue;
		if ( CG_WeapAnim_SampleFrame( &bg_weapAnimDefs[ps->weapon], &cg_weapLayers[i], entity ) ) {
			sampled = qtrue;
		}
	}
	return sampled;
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
		"sprint_in", "sprint_loop", "sprint_out"
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
