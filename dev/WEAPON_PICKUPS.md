# Weapon instances, death drops and pickup

On death the held weapon slot drops a physical item with its attachment mask,
remaining magazine, reserve and chamber debt. A pending switch does not change
which gun drops. During a quick action, the underlying gun slot drops. Empty
guns also drop. Dropped guns settle on the floor rather than bob/spin like map
powerups, and use the existing 30-second cleanup / round-reset lifecycle.

Walk over an exact weapon + attachment match in either inventory slot to add
the floor gun's magazine and reserve to that slot's reserve (up to capacity).
The floor item is consumed, including when already full. This does not load
the held magazine, clear an open action, change slots or select a weapon.

For a non-matching gun within 64 units and unobstructed reach, hold the reload
button (`+reload`) or use/reload (`+usereload`, the gamepad's X) continuously for
1000 ms. The HUD shows the item and progress. Releasing early resets progress
and supplies the normal reload input: a `+reload` tap is read by the weapon at
once, a use/reload tap is the server's, as in MW2, where a tap of X over a usable
item reloads on release (`BG_UpdateActivate`; `pickupHeld` 3 marks that hold so
the weapon does not also reload). Moving away,
losing visibility or changing target resets progress; completed holds require
release before another pickup. Bomb planting/defusing still uses its own input.
The replaced gun is dropped with its actual contents. Two differently attached
instances of the same base weapon may coexist with separate magazines.

## Selection and first deployment

The hold remembers its replacement slot. Y toggles the latest requested slot,
independently of the hold and of snapshots. Pickup completes into the remembered
slot, without forcing selection back. Thus Y can stow a newly acquired weapon
through ordinary switching, with no named-glitch detection or special state.

An acquired weapon has a one-time first deployment. If it is already stowed,
that deployment remains pending until first drawn. Once drawing starts, it is
consumed even if interrupted. Subsequent raises use the normal animation. The
optional `first_raise` clip participates in generic non-reload sprint retention;
the existing drop cancel releases it normally. It cannot override readiness,
ammo or chamber notes. The SPAS has a draw/check clip composed from the original
draw and pump frames in `assets/source/spas12/first_raise.json`. Other viewmodels
fall back to their ordinary raise until first-deployment artwork is supplied.

`cg_sight 0` (iron) / `cg_sight 1` (red dot) now seed the spawn loadout. They no
longer change every owned gun immediately. The existing class serialization is
unchanged; attachment editing in the class menu is not introduced by this work.
After pickup, the carried gun's networked attachment mask controls its sight.

## Architecture and verification

- Shared pmove owns the hold clock and reload-input arbitration; the server
  revalidates range, visibility, item identity and availability before transfer.
- Server transactions hide consumed items immediately, preventing duplicate
  claims. Clients do not predict inventory grants or generic Q3 autoswitches.
- Slot APIs save/restore the existing weapon-ID-indexed note-engine registers.
  Reload note order, finish-before-fill, YY cancellation, Semtex recovery and
  sprint-retention ownership remain shared mechanics.
- Protocol 74 / game version `-4` requires rebuilding engine, server, native
  modules and QVMs together. Do not mix these with previous builds.

Run `sh dev/tests/run-pickups.sh`, also with `PICKUP_TEST_FLAGS=-DMISSIONPACK`.
It exercises death selection, empty guns, exact-match scavenging, ammo caps,
partial/restarted holds, blocked reach, variable command lengths, replacement
payloads, Y throughout pickup/draw, and duplicate-base attachment variants.
`run-loadouts.sh` covers wire fields and first-draw animation retention alongside
the existing reload/YY/quick-action tests. `peacedump` includes each inventory
instance, hold intent and visible floor payloads for multiplayer diagnostics.
