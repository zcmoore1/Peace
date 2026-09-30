# ADS sights on dev-3b-gpt

The SPAS-12 now raises its actual viewmodel into the sight line. Its iron sights
are the front post and rear aperture already present in the supplied mesh.
A new original low-poly reflex housing and a collimated red dot are included.

## Use

In the console:

- `cg_sight 0`: SPAS iron sights (default).
- `cg_sight 1`: SPAS red dot.

Use the existing ADS button. The setting is archived and currently selects the
first-person sight for supported models; it is not yet a networked loadout
attachment or a third-person weapon modification.

Sight alignment, FOV and sensitivity share the same reversible blend. The
hipfire crosshair (including the dynamic spread bars and stereo crosshair)
hides as a supported sight comes up. Hip offsets, landing motion and weapon
bob blend out; authored firing, bolting, raise and reload clips still play.
Camera movement and gameplay spread are unchanged. Hiding the gun with
`cg_drawGun 0` retains the crosshair.

The SPAS is currently the only weapon with a dedicated IQM viewmodel and a
calibrated sight file. Other weapons retain their existing presentation until
appropriate sight geometry/configuration is supplied. Missing optic assets
fall back to the iron pose. Invalid/missing sight files keep the old
presentation instead of hiding the only aiming reference.

## Asset configuration

`assets/baseq3/models/weapons2/spas12/view.sights` sits beside `view.iqm`.
This is separate from the animation frame table `view.cfg`.

- `iron` / `reddot`: eye X/Y/Z in the model's idle coordinates, then correction
  pitch/yaw/roll in degrees. +X forward, +Y left, +Z up.
- `timing`: aim-in and aim-out milliseconds.
- `fov`: aimed FOV multiplier.
- `optic` / `reticle`: model and shader paths.
- `mount`: parentless joint name/index, translation, pitch/yaw/roll relative to
  that joint. The index must identify the same root joint as the name.
- `lens`: lens plane origin in optic coordinates and usable aperture radius.
  The lens normal is the optic's +X axis.

The iron calibration is derived from the supplied SPAS source mesh after the
importer's axis conversion. The rear aperture is approximately
(10.30, -2.698, -1.046), and the front post is
(24.38, -2.711, -1.196). The authored idle eye sits at X=5 to keep the rear
aperture ahead of the normal near plane. The reflex lens sits farther forward.

The optic follows the same animated root as the gun, using the blended pose
in native GL2 and the sampled frame tag in GL1/QVM. Its aiming dot is a
depth-tested sprite inside the actual lens opening, not a HUD crosshair. The
aim ray must intersect that opening before a dot is drawn. An animation that
moves the lens away also takes the dot out of view.

Rebuild the original optic assets with:

```
node dev/tools/build_sight_assets.js
```

No third-party optic mesh or texture is required. The script emits the same
MD3 and TGA bytes each time; the runtime assets are committed.

## Reload and future zoomload work

Gameplay permission still comes from `PM_CheckADS` / `PMF_ADS`. A real
`WEAPON_RELOADING` state continues to block ADS, including its tail after the
busy lock reaches zero. No weapon-state, ammo, network-format, note-ordering
or reload-timing changes were made.

Sight alignment acts on the final viewmodel transform and never replaces an
animation with idle or checks the visual reload layer to decide permission.
It therefore does not require picture and gameplay state to agree. The
existing animation system still normally follows predicted weapon state:
independent speculative animation lifetime/reconciliation would be future
work. No fake reload, latency window, knife/equipment timing exception or
zoomload detection has been introduced.

## Validation

`dev/tests/sights_test.c` exercises the real sight parser, aim clock, camera
alignment and optic projection with rendering/FS stubs. It checks malformed
configs, reversal, repeated stereo times, weapon/death/time resets, multiple
camera orientations, iron geometry, preserved animation frames, attachment
motion and rejection of dots outside the lens.

The existing reload suite additionally checks that real reload/transition
states deny ADS even with an expired busy lock, while an animation clock by
itself does not deny aiming in READY/FIRING/BOLTING. Existing ammo, cancel and
sprint-ordering cases remain in the suite.

GitHub Actions on this branch runs both native test programs, checks generated
assets for reproducibility, and builds native and QVM game modules. Check the
actual run result for the commit; adding a workflow is not a claim it passed.

On Windows, run from an **x64 Native Tools Command Prompt for VS**:

```bat
dev\tests\run-shotgun-reload.cmd
dev\tests\run-sights.cmd
```

Build the game:

```bat
cd /d C:\Users\ZackI\Desktop\OneManIW\ProjectPeace\dev2\Peace
cmake --build out\build\x64-Debug --clean-first
```

Visual playtest still required: iron post/aperture alignment at your usual FOV
and resolution, red-dot selection, fire/pump cycles, reload ADS denial, sprint,
swaps, ADS release/re-entry, GL1/GL2 and QVM. This headless test does not render
screenshots or simulate network latency.
