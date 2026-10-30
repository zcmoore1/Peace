# Ladder lab

Branch: `dev-gpt`. With that branch checked out, run in **x64 Native Tools Command Prompt for VS**:

```bat
cd /d C:\Users\ZackI\Desktop\OneManIW\ProjectPeace\dev2\Peace
git pull origin dev-gpt
cmake --build out\build\x64-Debug --clean-first
```

Then in the game console:

```text
devmap peace_ladder
cg_weaponDebug 1
```

The yellow-railed ladder is directly ahead of the spawn. W/S climb up/down;
A/D move sideways relative to the ladder, regardless of where you look. Approach
or jump into it to grab automatically. Release and press jump again to jump off.
Look is limited to 180 degrees total (90 either side of facing into the ladder).
Climb to the top to step onto the platform; walk or drift past a side to fall off.
At the bottom, holding down returns to normal walking as your feet reach the
floor. Stopping or strafing beside the ladder on the floor leaves your gun up;
press forward to climb again immediately.

The map is an original, tiny room with one ladder and a landing platform. q3dm1
source/BSP is not in this repository, so that retail map was not modified. The
compiled `assets/baseq3/maps/peace_ladder.bsp` and its shaders are copied by the
existing CMake assets target. You need no map compiler to play it. Existing game
assets are still required by the normal client; this is not a standalone game pack.

The platform now also contains **Site A** for Search and Destroy, with separate
attacker/defender spawns. These additions do not change ladder collision.
See `dev/SEARCH_AND_DESTROY.md` for multiplayer and solo objective practice.

## Behavior and ordering

- Shared `bg_pmove.c` handles both prediction and server movement. Horizontal
  hull traces find near-vertical `SURF_LADDER` faces independent of view direction.
- Contact removes only velocity normal to the face. Vertical/sideways momentum
  remains and friction settles it to a hang; there is no gravity while attached.
- A jump preserves sideways motion, pushes away from the face, and restores normal
  airborne movement. It rises with `BG_LADDER_JUMP_SCALE` (0.75) of a ground
  jump's take-off speed, as MW2's Jump_PushOffLadder. Re-grab is armed by geometric separation, not a cooldown.
- At a walkable floor, down/neutral input releases the ladder. The ground probe
  uses the same 0.25-unit reach and slope threshold as ordinary grounding.
  Landing consumes the velocity into the floor before walking, preventing a
  clipped downward slide from being normalized into an upward bounce/re-grab.
  This does not release a midair hang or require a timer to reattach.
- Contact stamps a positive, universal `BG_LADDER_DROP_TIME` and enters ordinary
  `WEAPON_DROPPING`. The existing animation-note pass then runs unchanged. Its
  lock release can finish the transition into `WEAPON_LADDER` before queued ammo
  fill, just as a normal holster can. No NAC branch, reload-time inspection, fake
  reload, artificial desync, or zoomload feature is added.
- The original reload helpers, swap helpers, note runner and their non-ladder
  ordering are unchanged. A ladder just owns the weapon while the hands are busy.
  Leaving raises the selected weapon using the existing deploy logic/timing.
  A prematurely interrupted bolt still owes its entire cycle.
- Gun use, equipment, ADS and sprint are blocked while attached. Real reloads
  still block ADS everywhere. This change does not split visual reload initiation
  from the authoritative reload state or manufacture future desync behavior.
- The existing weapon lower animation is fitted to the short ladder holster.
  The weapon is hidden in `WEAPON_LADDER`; dedicated climbing-hand animations are
  not supplied. The existing still-swap presentation ownership is retained.

Initial tuning lives in `bg_public.h`: 100 ms holster, 4-unit grab reach,
160 units/s climb, friction 5, acceleration 10, and 180 units/s outward jump push.
These are tunable first-pass values, **not measured original-MW2 constants**.
MW2 ladder-stall footage was located, but playback was blocked in the development
environment; the user's stated behavior is the implementation specification.

`STAT_LADDER` uses one signed-short stat (flags plus 12-bit inward yaw).
No extra pm_flags or animation-sequence bits are consumed. Both client and server
must run this build; old clients do not understand the new ladder state.

## Checks and playtest

`dev/tests/run-ladders.cmd` (Windows) or `sh dev/tests/run-ladders.sh` (Linux)
loads the committed BSP into the real engine collision code and drives actual
Pmove at 8/16/33/66 ms. It checks auto-grab from different views, momentum, hang,
traversal, edge slips, top/bottom exits, immediate climb-back, grounded strafing,
falling contact at the base, jump separation, yaw clamp, prediction replay,
mode changes, and reload/bolt note ordering. Existing weapon/sight tests remain.
Linux: repeat with `LADDER_TEST_DEFINES=-DMISSIONPACK` to check the full stat layout.

For the hands-on pass, try running sideways into a grab, falling onto it, a
release/repress jump, a top dismount, and reload contacts just before/on/after
the insertion note. `cg_weaponDebug 1` shows physical attachment separately from
the weapon state/lock; `peacedump` includes the ladder flags and facing.
Repeat client/server tests with latency and packet loss: deterministic snapshot
replay is covered automatically, but actual network feel is not proven by it.

Regenerate the original map with `python dev/tools/build_ladder_map.py`; use
`--check` to verify the committed output. The small BSP uses conservative full-room
visibility, vertex-lit materials, and smooth collision behind decorative rungs.
Only the ladder's front face has `SURF_LADDER`; its top/back and the platform are
ordinary solid surfaces. It contains no retail geometry, textures, or scripts.
