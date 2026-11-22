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
Look is limited to MW2's 100 degrees either side of facing into the ladder
(`bg_ladder_yawcap`, `BG_LADDER_YAW_CAP`).
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
- A jump preserves sideways motion, pushes away from the face at MW2's
  `jump_ladderPushVel` (128, `BG_LADDER_JUMP_PUSH`), and restores normal
  airborne movement. It rises with `BG_LADDER_JUMP_SCALE` (0.75) of a ground
  jump's take-off speed, as MW2's Jump_PushOffLadder. MW2 sets the flat velocity
  to the push along the view instead, reflected off the ladder when the view
  faces into it, so the two agree only when looking straight at the ladder.
  Re-grab is armed by geometric separation, not a cooldown.
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
- That holster is MW2's ladder drop: an ordinary weapon change to weapon 0 with
  the gun still in hand and still named by the command (`CheckForChangeWeapon`'s
  ladder branch, iw4x.exe 0x578C72-0x578C78 into 0x578C54-0x578C67). Three
  things follow, and none of them is a ladder rule (`PM_LadderWeapon`,
  `PM_LockGun`, `PM_LadderLeft`):
  - **It waits out a shot.** The branch is behind the change gate
    (0x578C0A-0x578C41, `PM_WeaponChangeOpen`): a ladder grabbed during a shot
    takes the gun on the shot's last think, not before, and ahead of the
    rechamber check (0x44C4D7), so the pump or bolt stays owed. A full-auto gun
    grabbed while firing goes down at the running shot's end instead of firing
    the next one. It begins only on a think the weapon code finds the ladder
    held: a grab and a jump off on one command put nothing down (MW2's jump
    clears PMF_LADDER before `PM_Weapon`, `Jump_PushOffLadder` 0x56DB85).
  - **The held trigger's lock takes it.** A drop ends only at weaponTime 0
    (`ProcessHand` 0x579474, `FinishWeaponChange` 0x576370: weapon 0, here
    `WEAPON_LADDER`). With the trigger down, the pull's shots all fired, the
    command naming the gun and a round in the clip, `WeaponTimeAdjust` makes the
    time 1 on every think and leaves a drop a drop (0x578A94-0x578AE6,
    0x578B25-0x578B28). So after a SPAS, M300 or Masterkey shot with the trigger
    never let go, the holster hangs for as long as it is held: the gun goes
    down on screen and stays down (MW2's drop animation plays once and holds
    its last frame), but it is still in hand. Let go, and that command ends the
    drop: empty hands, the pull over. Full auto is never locked, so its holster
    runs out on its time whatever the trigger. A command naming another gun
    fails the lock too (0x578AC4; Peace's client sends Y on a ladder, MW2's
    does not).
  - **Leaving cancels it, with no raise.** On the think the ladder is found
    left with the holster still going down or hung, it is an ordinary drop of
    the gun the command names, and MW2's double-tap cancel, the one YY takes,
    makes that gun READY with no time (0x578D00-0x578D41, `PM_Weapon_Idle`
    0x5777C7-0x5777D8). The rest of that think is the READY gun's: an owed pump
    or bolt starts, whole (`CheckForRechamber` from READY, 0x575B77), or a held
    trigger fires (`ShouldBeFiring` is a level test, 0x576CC0; the lock was only
    the time). A knife pressed on it comes out, a reload is taken, and
    equipment takes the drop as it is and is prepared on the next command
    (MW2's offhand check runs before the cancel). With the command naming
    another gun the drop runs out the time it had and raises that one.
- A grab with the knife or a grenade out takes nothing from it. MW2's ladder
  drop is a weapon change, and `CheckForChangeWeapon` refuses every melee and
  offhand state and the quick raise after one (iw4x.exe 0x578BC0-0x578C04,
  ladder branch 0x578C72). So the knife lands, a cooking frag's fuse runs on and
  it is thrown on its release or goes off in the hand, a Semtex is thrown or
  Y-cancelled, and the gun comes back on its quick raise. The ladder drop then
  starts on the think after the quick-action latch clears, as a queued swap
  does, if the ladder is still held: the latch's last think is MW2's MELEE_END
  or OFFHAND_END, which the change check, its ladder branch included, refuses
  (0x578BC9-0x578BF2). (The knife's quick raise that ends under a held
  trigger's lock is READY on that think already, 0x578B25-0x578B39, so the drop
  starts on it; so does the knife's own end under the lock, which is READY
  with no quick raise, 0x578B2A-0x578B39.) Jump off first and there is no
  ladder drop at all.
- What that gives with the trigger held from a single-shot gun's shot (owner's
  rule: MW2's behaviour; `ladder_test.c`'s `TestHeldTriggerHolster`). Hold the
  trigger, grab the ladder, hang as long as you like, 3 seconds or 10, and jump
  off still holding: the gun is back on the jump's own command, with no raise.
  Which of the two it does there is decided by where the drop began:

  | the ladder was grabbed | on the ladder | the jump's command, trigger still held |
  |---|---|---|
  | during the shot | the drop begins on the shot's last think; hung; the action owed | the pump or bolt starts, whole |
  | in the action, before the bolt | the drop begins at once; the bolt is never taken; hung; owed | the pump or bolt starts, whole |
  | on the bolt's own think (the Masterkey: the think after the shot's last) | **empty hands at once**, the action owed; nothing hangs | **the gun's whole raise**, which then waits under the lock until the trigger is let go; the pump starts the think after |
  | in the action, after the bolt | the drop begins at once; the rest of the action is given up; hung; chambered | the shot (the pull's second; its own action follows, then the lock) |
  | after the action | hung; chambered | the shot |

  The third row is the one way a held trigger still pays a raise, and it is
  MW2's: a drop begun on the think a weapon delay runs out is finished by that
  delayed action on the same think (its path A, 0x579474, 0x57928C, the
  mechanism of the bolt cancel and the Moni), so the hands are empty before
  any crossing could hang it. It is one think wide; a think earlier or later
  the gun is back at once.
- The original reload helpers, swap helpers, note runner and their non-ladder
  ordering are unchanged. A ladder just owns the weapon while the hands are busy.
  Leaving with the drop ended (`WEAPON_LADDER`) raises the selected weapon using
  the existing deploy logic/timing; a ladder left within the holster's 100ms
  gives the gun back at once, whatever the trigger (above), so a raise, a
  reload (its rounds lost) or a sprint-out cut short by a brief touch ends
  READY, as in MW2 within its 250ms.
  A prematurely interrupted bolt still owes its entire cycle, a grab on the
  close note's own think included.
- Gun use, ADS and sprint are blocked while attached, and no knife or equipment
  starts there (whether MW2's client sends those buttons on a ladder is
  unsettled); one already under way at the grab finishes first, as above. Real
  reloads still block ADS everywhere. This change does not split visual reload initiation
  from the authoritative reload state or manufacture future desync behavior.
- The existing weapon lower animation is fitted to the short ladder holster,
  with a 24-unit lowering of the whole gun over the same time. The weapon is
  hidden in `WEAPON_LADDER`, and just the same in a holster that has run its
  time and hangs under the held trigger's lock (`BG_LadderHolsterDown`): the gun
  is stowed for as long as the trigger is held, though pmove still has it in
  hand. Dedicated climbing-hand animations are not supplied. The existing
  still-swap presentation ownership is retained. When leaving cancels the
  holster the picture never snaps with the gun READY: it comes home up the
  drop it went down, the lowering with it (YY flows). A shot or the action
  started on that command takes the picture instead: from the pose shown when
  the holster was still on screen, and at once out of a hung one, where there
  was no gun on screen to flow from (the gun is in hand, firing or being
  pumped, on that frame).

Tuning lives in `bg_public.h`. MW2's: the 128 units/s jump push
(`jump_ladderPushVel`), its x0.75 rise and the ±100° look (`bg_ladder_yawcap`).
Tunable first-pass values, **not measured original-MW2 constants**: the 100 ms
holster (MW2's ladder drop is the gun's 250ms `quickDropTime`; the 100 is the
owner's call, and MW2's gate, lock and cancel never read the length: it sets
only how soon a drop the lock does not take empties the hands, and the window
in which leaving gives the gun back with the trigger up, 13/7/4 commands at
8/16/33ms against MW2's 32/16/8), 4-unit grab reach, 160 units/s climb,
friction 5 and acceleration 10.
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
falling contact at the base, jump separation, the 128 jump push, the ±100 yaw
clamp, prediction replay, mode changes, reload/bolt note ordering, and grabs with
the knife out, a frag cooking (thrown, cooked out, or the ladder left again
first) and a Semtex held (thrown or Y-cancelled): the fuse runs on, nothing new
starts, and the ladder drop never comes before the think after the quick-action latch clears. Existing weapon/sight tests remain.
MW2's ladder with the trigger held (`TestHeldTriggerHolster` and the tests it
runs) is driven the same way, every command worked out from the game's tables:
the SPAS, the M300 and the Masterkey hung with the pull spent for no time, 3
seconds and 10, the jump firing or starting the pump on its own command; the
grab during the shot, before the bolt (and on the think before the bolt's), on
the bolt's think and on the think after it; the
trigger let go on the ladder and on the jump's command; a full-auto gun never
hung; a holster left with the trigger up; the grab and the jump on one command;
the top climbed off with the trigger held (every way off is the same cancel);
the leaving command's knife, frag, reload and Y; and a hang and its cancel
replayed from a snapshot. The picture is checked in the loadout suite:
`weapon_pose_test.c` (`TestLadderHeldPicture`) and, through the real
`CG_AddViewWeapon`, `equipment_view_test.c` (`TestLadderView`).
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
