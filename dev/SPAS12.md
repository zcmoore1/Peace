# SPAS-12 on dev-gpt

- The existing animated SPAS mesh now wears a 1024px gunmetal/polymer diffuse
  skin. Its triangle winding is corrected for Quake's culling; the old export
  rendered inside surfaces and hid the front sight post during ADS. UVs,
  vertices, skin weights and animation frames are preserved.
- Normal holster takes **829ms** and draw **1075ms** (YY cancels the draw), and grabbing a ladder
  still uses the shared 100ms holster. YY and reload-note cancellation retain
  their existing shared state-machine behavior.
  These normal swap lengths match the [MW2 multiplayer SPAS weapon data](https://github.com/FreeTheTech101/IW4-Dump-Files/blob/master/weapons/mp/spas12_mp).
  The existing custom first deployment includes a pump/check and takes 1708ms;
  it is separate from the normal raise.
- Pump completion eases from the last rendered pose over **100ms**. YY follows
  the current draw/drop clip back to ready, with an eased return capped at
  **250ms** and a 40ms fade to idle. Interrupting a draw reverses that same clip
  from its current position. These are presentation only: holding fire still
  fires on the second Y when the gun is chambered and gameplay permits it.
- The synthetic holster still follows the draw backwards, but samples that
  path with ease-out progress `f*(2-f)`. This gives the start of a quick YY
  noticeable movement instead of lingering in the draw's near-still final
  poses. Its endpoints, 26-frame range and **829ms** duration stay unchanged;
  no YY impulse or firing delay is added. Both the Blender importer and the
  stdlib animation baker preserve this curve.
- For the sprint glide: start sprint and press Y quickly, end the sprint (a
  sprint latches, as in MW2: tap sprint again, aim, or let go of forward),
  then press Y again. The gun comes back to ready exactly as it does when a
  sprint stops, using the existing transition return: from the carry (entry
  finished, or the carry loop) it plays the **250ms** sprint-out; a partial
  entry runs back over the same 250ms, as a sprint tap reverses it; an exit
  already under way keeps its pace. The authored easing is used once, on a
  linear clock, so it leaves the carry at the exit's pace and settles into
  ready as gently. Gameplay is ready immediately; firing takes over the
  picture at once. Aiming during the return looks like aiming out of a
  sprint: the 150ms aim finishes while the gun is still settling.
- Sprint can begin or continue in the air, as in MW2. Its entry/carry/exit
  animations use the same weapon states as on the ground. Holding jump ends a
  sprint (MW2), and a held sprint button sprints again once jump is let go.
- A swap, sprint or ladder grab on the exact bolt-close think keeps the instant
  transition but abandons chamber completion. Returning requires the **entire
  pump from zero**. Going one think late keeps the completed chambering.
- Aim progress survives a weapon change while the outgoing ADS permission is
  still live. An instantaneous aimed SPAS-to-M300 swap (the NAC off the pump)
  shows one frame of the M300 scope;
  the following movement update denies ADS during the raise and clears aim.
  This is an observed simulation state, not a forced frame: a render that skips
  over that update shows no delayed scope flash.
- `cg_sight 0` (default) uses the rear aperture and front post. `cg_sight 1`
  selects the existing reflex attachment. Full aim ignores hip offsets.
- The ADS blend now consumes the actual teleport-bit change once. The old
  prediction-owned teleport flag can remain set during synchronous play,
  `cg_nopredict`, or demos, repeatedly resetting aim to hipfire despite ADS
  being allowed by gameplay. This fix changes presentation only.

## Texture asset

`assets/baseq3/models/weapons2/spas12/gun_skin.jpg` is an original texture made
with the built-in image-generation tool using the source SMD's UV layout as a
guide, then encoded as a 1024px JPEG for the engine. No texture was downloaded.
The model's two named source atlases are absent; the other included BMPs have
different layouts. Hands remain the existing flat material.

`assets/baseq3/scripts/peace_spas.shader` explicitly maps the IQM's existing
gun material to this skin, so an old loose `gun.tga` in a player's build folder
cannot take precedence. The neutral fallback remains available. The importer
preserves existing skins instead of overwriting them when the rig is exported.
It now serializes clockwise triangle indices for Quake while keeping Blender's
own preview in its expected counterclockwise order. The shipped IQM is repaired
with the same index conversion, and its embedded drop rate matches `view.cfg`.

`python dev/tests/spas_asset_test.py` checks every shipped surface's orientation
against its outward normals, index bounds, and agreement between IQM clip
metadata and the runtime clip table. CI runs this alongside the movement,
weapon-state, sight, loadout, and native/QVM build checks.

Generation prompt (built-in tool, UV guide supplied as the edit target):

> Production diffuse UV texture atlas for an existing SPAS-12 game viewmodel.
> Preserve every UV island boundary and position exactly; do not rearrange,
> rotate, scale, or add shapes. Gray islands: dark graphite parkerized gunmetal,
> fine granular phosphate finish, restrained edge wear and delicate longitudinal
> scratches. Dark pump island: almost-black matte molded polymer with fine
> stipple and soft wear. Red shell island: muted crimson plastic and brass cap.
> Uniform flat albedo lighting, no cast shadows or perspective. Remove triangle
> wireframe lines; triangulation edges are not physical seams. Fill unused
> background with dark gunmetal for filtering. No text, logos, labels, stamps,
> drawn screws, or new holes; the model already supplies its physical details.

## Sprint animation

Sprint now has three distinct clips: a **200ms sprint-in transition**, a
**600ms repeating carry loop**, and a **250ms sprint-out**. Both hands keep
their existing grip as the shotgun turns into a low, horizontal carry across
the body, muzzle to the left. Vertical running bounce dominates the loop;
small lateral sway and roll keep it from looking rigid. The carry is centered
across the lower view. The trigger-side elbow turns around the wrist while
the hand keeps its grip; the loose reload shell/holder stays behind the gun.
This keeps the arm ends and spare shell out of view without pushing the entire
weapon to the right. This is original animation on the supplied
rig, taking its direction from the requested MW2 SPAS/Intervention carry.

Entry and exit follow the existing predicted weapon timers. A short sprint
tap reverses the partial transition; ending the sprint (it latches: a fresh
press, aiming or letting go of forward) does not wait for a whole carry loop
to finish. Swapping preserves the existing ownership rule:
sprint-in plays to its last frame and holds, while the full carry keeps
looping. No movement speeds, gameplay locks, reload notes, swap logic, or
client/server state fields changed.

Tune `assets/source/spas12/sprint.json`, then run:

```sh
python dev/tools/build_spas_sprint.py
python dev/tools/build_spas_sprint.py --check
python dev/tests/spas_asset_test.py
sh dev/tests/run-loadouts.sh
```

The bake rebuilds source animation channels without cumulative quantization,
preserving geometry, UVs, bind skeleton, skin weights and original clip ranges.
Non-sprint clip-table rates and comments remain hand-editable. The Blender
importer calls the same bake after exporting geometry; its `.blend` preview
still contains the supplied SMD actions, with sprint authored in the JSON.
CI checks reproducibility, endpoint/cycle continuity, and sprint/swap poses.

## Playtest

With `dev` checked out, run in **x64 Native Tools Command Prompt for VS**:

```bat
cd /d C:\Users\ZackI\Desktop\OneManIW\ProjectPeace\dev2\Peace
git pull origin dev
cmake --build out\build\x64-Debug --clean-first
```

Choose a class with SPAS-12, launch `devmap peace_ladder`, and hold ADS. Try
`cg_sight 0` and `cg_sight 1`, a full swap, YY during the slower holster, and
descending the ladder to the floor. `peacedump` reports state and drop time.
Hold forward and tap sprint: watch the entry settle into the repeating carry,
then tap sprint again (or aim) to return to hip/ADS. Also try swapping during
entry, and swapping during the full loop. The first swap should hold the final
entry pose; the second should keep moving throughout the holster.
Rebuild both engine and game modules together as usual.
