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

For a non-matching gun within 128 units of the eye (MW2's player_useRadius) and
unobstructed reach, press use (`+activate`, taken at once) or hold use/reload
(`+usereload`, the gamepad's X) or reload (`+reload`) for
`BG_USE_RELOAD_HOLD_TIME` (250 ms, counted from the press); a shorter tap reloads.
That is MW2's use hold (`g_useholdtime`, 0 in the executable) with IW4x's
controller rule (`gpad_use_hold_time`, 250) for use/reload, shared with the
throwing knife (`BG_UseHoldTime`). `+reload` collecting a gun is Peace's own:
MW2 reads only use and use/reload there. Nothing is collected in the first
`BG_USE_SPAWN_DELAY` (500 ms, MW2's `g_useholdspawndelay`) after the player
spawns. A gun you dropped yourself is not a candidate for its first 1000 ms
(`BG_PICKUP_OWNER_DELAY`, MW2's). The HUD shows the item and progress,
except where a throwing knife is the use hint: a knife outranks any gun, and
its prompt takes the place (`dev/THROWING_KNIFE.md`) unless a hold is
already running on the gun (a plain reload hold, or a use/reload hold the
server gave it).
Swapping the gun in hand empties the hand, as MW2's take does: the new gun is
drawn once your selection names it, about a round trip later (each slot's
generation bit, `BG_SlotCommand`). Collecting a dropped gun your use hint named
frees it and leaves you with no hint at all - MW2's sea lion (see CLAUDE.md).
A map gun is only hidden to respawn, so the hint keeps naming it. A resting
throwing knife outranks any gun as the hint and is taken with the same use hold
(`dev/THROWING_KNIFE.md`); taking it frees it too. Releasing early resets progress
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
- The current `PROTOCOL_VERSION`/`GAME_VERSION` (see CLAUDE.md's capacity
  limits) require rebuilding engine, server, native modules and QVMs together.
  Do not mix these with previous builds.

Run `sh dev/tests/run-pickups.sh`, also with `PICKUP_TEST_FLAGS=-DMISSIONPACK`.
It exercises death selection, empty guns, exact-match scavenging, ammo caps,
partial/restarted holds, the hold shared with the throwing knife and the
player's spawn gate (`TestUseHold`), blocked reach, variable command lengths, replacement
payloads, Y throughout pickup/draw, and duplicate-base attachment variants.
`run-loadouts.sh` covers wire fields and first-draw animation retention alongside
the existing reload/YY/quick-action tests. `peacedump` includes each inventory
instance, hold intent and visible floor payloads for multiplayer diagnostics.
