# Fake reload, fake bullet and wild scope

MW2 behaviour, emergent from MW2's own use/reload. No code names any of these.

## What the player does

All of these need **use/reload** (`+usereload`, the gamepad's X, as on console
MW2). The plain reload key (`+reload`, PC MW2's R) is the weapon's own button:
it is predicted exactly and never fakes anything, as in MW2, where the trick
scripts do every one of these with `+usereload`.

- **Fake reload (zoomload).** Tap X (a press, not a hold) as the gun comes back
  from a knife or from equipment (frag, flash, Semtex throw or Semtex cancel),
  releasing it by the time the gun is back. The reload animation plays but
  nothing reloads. You can aim, shoot, or start a real reload during it. A
  held X is a real reload.
- **Fake bullet.** Tap X just before a shot. The gun shows the shot, but the
  shot never happened and no round is spent: the server reloaded instead.
  Bullets are drawn by the clients (MW2's `sv_clientSideBullets`), so the
  shooter also sees its tracer and impact, blood on a player included, with no
  damage, as in MW2. Only the picture is new; the window below did not move
  (`dev/tests/run-bullets.sh` checks the drawn shot, `run-fakereload.sh` the
  windows).
- **Wild scope.** With a scoped gun (the M300), hold aim, do the fake reload,
  and knife straight after. The knife plays while you stay scoped (a fake
  knife).
- **A knife alone** is never a knife you can scope through. Pressed while the
  gun is still coming back it is nothing (late or nothing, as MW2's knife check
  skips the quick raise). Once the gun is back it is a real knife hip-fired, and
  nothing aimed through a scope (MW2 starts no knife while a scoped gun is
  aimed).
- On your own host every window is tight, with MW2's sure zone: let go of X
  within about 16ms of the gun coming up and the fake reload always happens
  (MW2's client clock, below). With ping every window is much wider.
- **The silent shot** is the same family on another set of MW2's pieces: shoot,
  then press the throwing knife within a server frame, and the round's
  killcam shows no shot and a kicked-over view, while the shooter, the
  opponents and the server all got it. It comes from the four-event rings,
  client-side bullets and the view kick, through the same client and server
  clocks; `dev/SILENT_SHOT.md` has it and its windows.

## The rules that make it

1. **Use/reload is a server button.** It is never a reload press in pmove
   (pmove reads it only to hold for a floor gun). After each
   command's `Pmove`, the server's use code (`BG_UpdateActivate`, MW2's
   `Player_UpdateActivate`, from `ClientThink_real`) clears the
   `PWF_USE_RELOAD` bit on `ps.weapFlags`, then sets it for a press that found
   nothing to use, and again on every command while that press is held. The
   weapon takes the bit as a reload press on the next command, but does not
   read it during a quick action, from the press to the end of the recovery
   draw, as MW2's reload check skips its melee and offhand states.
2. **The client never sets the bit.** It arrives in a snapshot, and the client
   replays its own commands over it. A tap made while the server's gun is still
   away reaches the client in a snapshot taken then; the client's replay
   carries it through the rest of the quick action and starts a reload when its
   gun is back. The server cleared it a command after the tap: the fake reload.
   A tap just before a shot is taken by the server first (it reloads, and a
   reload beats the trigger); the client learns of it a round trip later and
   has already shown the shot: the fake bullet.
3. **No knife while a scoped gun is aimed** (MW2's melee rule;
   `BG_WeaponScopeOverlay`, from the rifle's `scope_overlay`). During a fake
   reload the client thinks the gun is reloading, so it is not aiming, and its
   knife starts. The server is not reloading, so it is aimed through the scope
   and refuses the knife: the wild scope.
4. **The gun never un-plays an action it showed** (`cg_weappicture.c`). When
   prediction is corrected away from a reload, shot or pump of the held gun that
   you were shown, the viewmodel keeps running the client's own commands through
   the real `Pmove`, withholding whatever would begin something new (it only
   finishes what you saw), until that run comes to rest or prediction starts a
   real action. It never detaches into an action that only its own run began.
   A correction that only removes a swap is followed at once. Aim, camera, HUD
   and gameplay read the predicted state.

## The clock (MW2's)

How wide these windows are is set by one thing: how old the snapshot is that
the client replays its own commands over. Peace's client keeps MW2's time
base, read from MW2's executable (`iw4x.exe`), in the engine:

- **The target.** `cl.serverTime = cls.realtime + delta`, and the delta drifts
  toward *newest snapshot - (newest - previous snapshot) - 16ms*
  (`CL_AdjustTimeDelta` 0x5a1ad0; `CL_DriftTimeDelta` in
  `code/client/cl_timedelta.c`, with the previous snapshot's time kept as each
  new one is copied in, `cl.prevSnapServerTime`, 0x5ac0ce). ioquake3's clock
  aimed at the newest snapshot itself.
- **The drift.** One millisecond a snapshot toward the target; two back after a
  frame that came within 8ms of the newest snapshot (`CL_SetCGameTime`'s
  extrapolation guard, 0x5031ed; ioquake3's was 5); half the gap when it is
  more than 100ms; a reset to newest - 16ms beyond 500ms. Ahead of the target,
  one snapshot interval of lead is free before a gap counts. No time nudge
  (`cl_timeNudge` stays 0). The first snapshot starts the clock exactly on it,
  as MW2's `CL_FirstSnapshot` (0x5a1cd0) does, and the drift takes it back to
  the target in about two seconds. Demos never drift.
- **The base.** cgame reads snapshots in order and keeps the two either side of
  its time; prediction starts from the later one, `cg.nextSnap`
  (`CG_PredictPlayerState`, as MW2 at 0x48f1cc), which is the newest snapshot
  only for the last part of each interval.

At sv_fps 20 on your own host the clock runs 19..68ms behind the newest
snapshot and prediction starts from 31..82ms back (`TestClockBase`). The 16ms
is a floor that no snapshot rate takes away: with snapshots every 8ms the
clock is still 24ms behind and the base 38..40ms old. That age, and nothing
else, is the window: no gameplay timer, bit or picture rule changed for it.

The cost, as in MW2: `cg.time`, other players and command timestamps sit
about 11ms further behind than under ioquake3's clock. Peace has no antilag,
so moving targets need about 11ms more lead.

## Measured (dev/tests/run-fakereload.sh)

The SPAS and the M300 in hand, 125 fps client, sv_fps 20 (MW2's fixed 50ms
frame, and the shipped autoexec's). Times are from the moment the client sees
the gun at rest.

Re-measured at protocol 86, with MW2's event rings, the view kick and
client-side bullets in: every number on this page is unchanged (the suite's
summary, `FAKERELOAD_PHASES=1` and `FAKERELOAD_MAXPACKETS=30`, compared line
for line with the build before them). None of them touches `Pmove`, the
use/reload bit or the clocks; the kick moves only the aim, and the knife's
press adds events, not time.

Fake reload. What matters is when X is let go: by the moment the gun is fully
up, and as close to it as you can. The X press must also be held across the
snapshot the client predicts from as the gun comes up, which you cannot see,
so the window is a success rate. A 64ms tap, SPAS, every snapshot phase, after
a knife, a frag and a Semtex cancel (`FAKERELOAD_PHASES=1`):

| let go of X, before the gun is up | 0 ms | 8 ms | 16 ms   | 24 ms  | 32 ms  | 40 ms  | 48 ms  | 56 ms  | 64 ms   | 72 ms  | 80 ms  | 88 ms  |
|-----------------------------------|------|------|---------|--------|--------|--------|--------|--------|---------|--------|--------|--------|
| own host (0 ms ping)              | 100% | 100% | 84%     | 68%    | 48–52% | 32–36% | 16–20% | 0–12%  | 0%      | 0%     | 0%     | 0%     |
| 50 ms ping                        | 100% | 100% | 100%    | 100%   | 100%   | 100%   | 100%   | 100%   | 84%     | 68%    | 48–60% | 32–48% |
| 100 ms ping and up                | 100% | 100% | 100%    | 100%   | 100%   | 100%   | 100%   | 100%   | 100%    | 100%   | 100%   | 100%   |

(Measured with MW2's offhand prepares: the frag and Semtex pull for 600ms,
holdFireTime, and are thrown or cancelled from HOLD. The frag rows moved by a
snapshot phase or so from the old 150ms/350ms pulls; the windows are still
counted from the gun at rest.)

Let go after the gun is up (even 8ms) and it is a real reload, at any ping.
So on your own host it is certain if you let go within the clock's 16ms of
the gun coming up, likely up to about 40ms early, and gone by about 64ms: the
chance falls across one snapshot interval, as the base's age runs over one.
With ping it is easy. The recovery is the gun's MW2 quick raise (SPAS 0.75s,
M300 0.25s), so "up" is the end of that raise. The harness checks these edges
on both guns by formula from the clock (`Windows`): 100% for every release up
to 16ms - one frame, within 20 points of the straight fall at 16ms + half an
interval, and 0% from 16ms + an interval + two frames.

A 48ms tap at one snapshot phase fakes when pressed this many ms before the
gun is at rest (the suite's summary; `FAKERELOAD_PROBE=1` prints every tap
time and tap length on the SPAS, and shows `+reload` never fakes):

| after         | gun  | 0 ms     | 50 ms     | 100 ms    | 200 ms    |
|---------------|------|----------|-----------|-----------|-----------|
| knife         | SPAS | 104–48   | 128–48    | 200–48    | 296–48    |
| knife         | M300 | 104–48   | 128–48    | 152–48    | 256–48    |
| frag          | SPAS | 72–48    | 152–48    | 176–48    | 272–48    |
| frag          | M300 | 80–48    | 152–48    | 176–48    | 256–48    |
| Semtex cancel | SPAS | 96–48    | 120–48    | 200–48    | 296–48    |
| Semtex cancel | M300 | 104–48   | 128–48    | 200–48    | 248–48    |

Fake bullet: a shot this long after a 16ms X tap is fake, on both guns, from
the very next command:

| 0 ms  | 50 ms | 100 ms | 200 ms |
|-------|-------|--------|--------|
| 8–64  | 8–88  | 8–160  | 8–256  |

Fired first, or on the same command, the shot is real and the reload follows
at once.

The 8ms is one command at 125 fps. The window opens one command after X at
any frame rate: 16ms at `com_maxfps 60`, about 12ms at MW2's default 85. MW2
is the same. Its server runs `Pmove` (0x493686) and fires the bullet that
command (the event loop, 0x5d7000) before `Player_UpdateActivate` (0x4939a1,
the only writer of the flag) looks at X. The flag is read on the next command
(0x5786E3). So X and RT in one command give a real shot in both games. The
input side matches too. IW4x polls the pad once a frame and sends X and RT in
the same pass, in that order (`Controller/Engine/Key.cpp`). Its trigger engages
at 13% of travel, Peace's at 15% (`joy_threshold`). The IW4x fake shot script
below leaves two frames between X and the shot.

`cl_debugButtons 1` prints, for each X and RT press, the command it went out
in and the gap between them ("SAME command" means a real shot).

Wild scope (M300, aim held): a knife this long after the gun is back, from
16ms (the command aim is in; a knife before that is a real hip-fired one):

| after         | 0 ms  | 50 ms | 100 ms | 200 ms |
|---------------|-------|-------|--------|--------|
| knife         | ≤56   | ≤80   | ≤152   | ≤256   |
| frag          | ≤32   | ≤104  | ≤128   | ≤232   |
| Semtex cancel | ≤56   | ≤80   | ≤152   | ≤256   |

These are measured with a packet every frame (loopback and LAN) and a
snapshot every 50ms: the shipped autoexec's `sv_fps 20` with the default
`snaps 20`, MW2's rate. (The old autoexec's `sv_fps 125` with `snaps 20` sent
a snapshot on the first 8ms frame after 50ms, every 56ms.) Snapshots every 8ms
(`FAKERELOAD_SVFPS=125`, as with `snaps 125`) narrow the own-host fake reload
to the clock's floor alone: 100% let go up to 16ms early, none from 24ms (50ms
ping: 100% to 64ms); the 16ms floor keeps the sure zone. Over the internet `cl_maxpackets`
(default 30) batches commands, which shifts the edges by a packet or so
(`FAKERELOAD_MAXPACKETS=30`). Every per-tap check still holds then; the
success-rate edges are swept over snapshot phase with a packet every frame
(your own host and LAN ignore `cl_maxpackets`), so `Windows` is skipped there,
and `TestClockBase` allows a batched packet's wait in the base's age.
`FAKERELOAD_CLOCK=1` prints the clock frame by frame.

## The cost to ordinary play (as in MW2)

- On use/reload, the server decides the reload, so online every X reload starts
  about one round trip late on screen. `+reload` is instant but has no tricks.
- Nothing else waits for the server: fire, quick actions and slot changes are
  predicted as before, and a fire tap after a quick action is always real.

## Where Peace and MW2 still differ

- **The IW4x wild scope script** presses knife and X on the same frame while
  the gun is still coming back from equipment, with aim held. That knife is
  lost in Peace, as in MW2's code, so the script gives a zoomload and no knife;
  the order above (fake reload, then knife) gives the wild scope. Which order
  MW2 players use is still open; the pack also ships a modified client.
- **The IW4x fake shot script** (X for one frame at 57 fps, shot two frames
  later) puts the shot about 53ms after X. With MW2's clock that is inside the
  own-host fake bullet window measured above at 125 fps (8–64ms); under
  ioquake3's clock it needed ping. A 57 fps client is not modelled.
- **Aiming.** MW2's knife rule reads its ADS fraction, so its knife also waits
  for the scope to ease out; Peace has no gameplay ADS fraction and uses the
  aim state.
These are read from MW2's own executable (`iw4x.exe`; knife check 0x577670,
offhand check 0x579030, offhand init/end 0x578E00/0x577A10, knife end 0x578D80).

- **The knife's own timing** now agrees: as MW2's, it never drops the gun. It
  is out on the press, hits `BG_MELEE_DELAY` (meleeDelay, 79ms) in, and is over
  at `BG_MELEE_TIME` (meleeTime, 800ms), straight into the quick raise (MW2's
  MeleeInit, MeleeFire, MeleeEnd). It is drawn with its own knife and hand
  (a horizontal slash), not on the gun's model.
  With no drops left either side, its return times much like a Semtex
  cancel's, and the knife rows above sit close to that one's.
