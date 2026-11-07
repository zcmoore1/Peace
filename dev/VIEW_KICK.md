# MW2's view kick

Every MW2 gun kicks the view when it fires, and the kick is part of the
player's aim. Peace does it MW2's way (owner's decision), from MW2's own
code in `iw4x.exe` and the IW4 dump's weapon files. It is the one approved
exception to "cgame is picture only": cgame authors the kick and nothing
else. It never touches ammo, weapon state or anything pmove owns, and pmove
is unchanged.

## How it works

1. **The shot assigns a velocity.** The predicted player's own
   `EV_FIRE_WEAPON` (`cg.predictedPlayerEntity`, MW2's player-view shot,
   `CG_FireWeapon` 4fb2d7) calls `CG_KickFire`. That runs the view part of
   MW2's `BG_WeaponFireRecoil` (0x4a5fe0) on the predicted state:
   - It uses the ADS view kick when the aim clock `cg.adsFrac` is exactly 1,
     and the hip view kick otherwise.
   - Pitch and yaw are each `min + r * (max - min)`, pitch drawn first.
   - `r` comes from MW2's random (0x44c590) on the client-only
     `cg_kickSeed`, which starts at MW2's client seed 0x89abcdef.
   - It **assigns** `kickAVel = (-pitch, yaw, -0.5 * yaw)`: the third axis
     is roll.

   A quick action's fire (the knife's hit, a throw) kicks nothing, because
   MW2 raises no gun fire event for them. A gun with no MW2 def kicks nothing
   either. Another player's shot is an entity event and kicks no one.
2. **cgame runs it out.** `CG_KickAngles` (MW2's 0x596790) runs once a
   frame, after the fire events and the aim clock. It covers the time since
   its last run in steps of at most 5 ms. On each axis:
   - The velocity is pushed back toward 0 at the centre speed.
   - A step back toward 0 is cut to 0.06.
   - An axis that crosses 0 stops there.
   - An axis past 10 degrees is held at 10 with no velocity.

   The centre speed belongs to the weapon the view reads
   (`BG_ViewmodelWeapon`, MW2's `BG_GetViewmodelWeaponIndex`). It is read once
   per frame: ADS above half aimed, hip otherwise, and 2400 for a weapon with
   no MW2 def.
3. **The client adds it into the command.** `trap_SetUserCmdKickAngles` (an
   appended cgame import, `CG_SETUSERCMDKICKANGLES`, QVM stub -116) hands the
   angles to `cl.cgameKickAngles` every frame. It passes three floats by value
   (`pitch, yaw, roll`), which the engine reads as `VMF(1..3)`. A cgame.qvm
   has no C wrapper: its call goes straight through the equ stub with the
   words q3lcc pushes. A `vec3_t` parameter would push a single pointer, and
   a QVM client would get no kick (or junk). `CL_FinishMove` sets
   `cmd.angles = ANGLE2SHORT(cl.viewangles + cl.cgameKickAngles)` on all
   three axes, as MW2's does (5a6740-5a677b). It never adds the kick into
   `cl.viewangles`, so the kick runs out without moving the player's own aim.
   `CL_ShutdownCGame` clears it.
4. **Everything else follows from the aim.**
   - The real `PM_UpdateViewAngles` turns the command into `ps.viewangles`,
     roll included, so the view shows the kick only through the predicted
     view angles.
   - The server aims the shot with those angles.
   - The round-winning killcam records them with the rest of the killer's
     player state, so the killcam replays the tilt with no kick code of its
     own.

**Only the player's own state kicks.** MW2 runs the kick only while
`POF_PLAYER` is set (59a100) and zeroes it otherwise (59a113). In Peace that
means: not a demo, not the killcam, not a followed player. Any kick under way
stops at once. MW2's snapshot transition also drops it whenever the viewed
player or their life changes (0x472ff0, `CG_ViewedPlayerChanged`), so a
respawn starts with no kick.

## The weapon the view reads

MW2's viewmodel weapon is the offhand from its prepare to `OffHandEnd`, and
the gun otherwise. Peace's `ps.weapon` is that offhand over the same span:

- A grenade's quick drop keeps the gun.
- The throwing knife is in hand on its press (`BG_OffhandPreparesAtOnce`).

MW2's knife is no weapon change, so under the knife the view reads the gun it
was written over (`BG_QuickActionInPlace`). From an empty hand it reads
nothing.

## The tilt is emergent

The throwing knife's centre speed is 0 (throwingknife_mp). A knife pulled
just after a shot therefore never brakes the kick it inherits. Pitch, yaw and
roll run to the 10 degree clamp and stay there until the gun comes back:

- An Intervention hip shot normally rolls the view about 0.8 degrees.
- With the knife it rolls 10 degrees.

Nothing names the glitch. The grenades keep the gun's centring through their
quick drop, so only a pitch hang is left. The melee knife keeps the gun's
centring throughout and tilts nothing.

The research's windows (`report-mw2-tilt.txt`) are the time the knife must
come within to grow the roll:

| Gun | Roll window |
|---|---|
| Intervention, hip | 50–55 ms |
| Intervention, ADS | up to 75 ms |
| SPAS | about 22 ms |
| M4 | about 13 ms |

## Data

The data is MW2's view kick from the IW4 dump (weapons/mp). Pitch and yaw are
in degrees per second; the centre speed is in degrees per second squared.

| Peace | MW2 | Hip pitch | Hip yaw | Hip centre | ADS pitch | ADS yaw | ADS centre |
|---|---|---|---|---|---|---|---|
| `WP_MACHINEGUN` | m4_mp | -30..50 | 40..-40 | 1500 | -30..50 | 40..-40 | 1500 |
| `WP_SHOTGUN` | spas12_mp | 85..95 | -45..45 | 1000 | 85..95 | -45..45 | 1000 |
| `WP_ROCKET_LAUNCHER` | rpg_mp = at4_mp | 100..150 | -50..-80 | 1500 | 20..95 | -10..30 | 2500 |
| `WP_M40A3` (M300) | cheytac_mp (Intervention) | 80..100 | -50..-55 | 500 | 30..85 | 70..-75 | 500 |
| frag, flash, Semtex, throwing knife, TI | frag_grenade_mp, flash_grenade_mp, semtex_mp, throwingknife_mp, flare_mp | 0 | 0 | 0 | 0 | 0 | 0 |

- **The table.** It is `bg_viewKick` in `bg_pmove.c`, keyed by weapon and
  never positional. The M300's row comes from `assets/source/m40a3/weapon.json`
  through `dev/tools/build_m40a3.py`.
- **No MW2 counterpart, so no view kick.** These guns have no row: the
  gauntlet, grenade launcher, lightning gun, railgun, plasma gun, BFG and
  grappling hook, plus missionpack's nailgun, prox launcher and chaingun. Their
  shots kick nothing, and their centre speed is MW2's no-def 2400, as is
  `WP_NONE`'s.
- **The knife has no row.** MW2 has no knife weapon.
- **Parts of MW2's formula that never apply here.**
  - Reduced kick: every gun above has ReducedKickBullets 0 and fireDelay 0.
  - The weapFlags recoil scale and pitch times 5: Peace sets neither.
  - perk_fastSnipeScale: Peace has no such perk.
  - The gun kick (weapon sway): not modelled.

## Tests

`dev/tests/run-kick.sh` (`KICK_TEST_FLAGS=-DMISSIONPACK` for the other
numbering) builds `kick_test.c` with the real code it covers:

- `cg_kick.c`
- `cg_event.c` and `cg_playerstate.c`
- cgame's native traps in `cg_syscalls.c`
- `CL_FinishMove` and `CL_SetUserCmdKickAngles`
- full `Pmove` over a floor

It checks:

- Every weapon's data at r 0 and one half.
- kicksim.py's published peaks, times back to 0 and the Intervention trace.
- Real shots through the real Pmove, cgame and `CL_FinishMove`:
  - The throwing knife's tilt, with the recorded `ps.viewangles` held at the
    clamp for the rest of the knife's time in hand, through its throw.
  - The SPAS's tilt and the M300's aimed one.
  - The melee knife and the frag after a shot.
  - The railgun, which has no kick.
- That `cl.viewangles` never moves.
- The demo, killcam and follow gates.
- That another player's shot kicks no one.
- The QVM stub's number.

It then builds `kick_qvm_test.c` with the QVM tools (from `cmake/tools`, or
`Q3LCC_DIR`). q3lcc and q3asm compile the real `cg_kick.c` with the real
`cg_syscalls.asm` and a small driver (`kick_qvm_cgame.c`; only the weapon
tables are stubbed). The real `vm.c` and `vm_interpreted.c` run that QVM, and
the real `CL_CgameSystemCalls` and `CL_FinishMove` answer it. Every frame the
engine must receive exactly the angles cgame holds in VM memory: frames that
run the kick out, a frame with no time to run, and the follow and killcam
frames that zero it. Each command's angles must be the view plus the kick.
