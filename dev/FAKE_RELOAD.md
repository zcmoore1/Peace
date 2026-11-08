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
- On your own host every window is tight, as on an MW2 host: let go of X
  within about 16ms of the gun coming up and the fake reload always happens;
  pull the trigger within 24ms of X (three commands at 125 fps, two at 85)
  and the fake bullet always happens (MW2's client clock and MW2's host,
  below). With ping every window is much wider, and online the server sends
  a snapshot only every 100ms, as MW2's does, which is most of why it feels so
  much easier there (below). It also means X has to be held across one of
  those snapshots: online, a press held longer than 100ms is sure, a quick tap
  a gamble on where the snapshots fall.
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

## The clock and the host (MW2's)

How wide these windows are is set by one thing: how old the snapshot is that
the client replays its own commands over. Peace's client keeps MW2's time
base, and its host MW2's frame, read from MW2's executable (`iw4x.exe`), in
the engine:

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
- **The host.** A host reads its own server's snapshot one frame after the
  server sent it. MW2 runs its server in a thread of its own, started at boot
  (0x4121e0, thread 0x627040; IW4x keeps it for clients). Its `SV_Frame`
  (0x627610) only adds the frame time to the residual and, at 50ms, signals the
  thread (0x627695) and returns without waiting. The thread then reads the
  clients' packets (0x458160), runs the game frame (0x415cd0) and sends the
  snapshots (0x4517b0), while the main thread goes on to its second event loop
  (0x47dfc6) and the client frame (0x47dfe1). So the host reads each snapshot
  in the next frame's event loop (0x47dd80), and the clock and the base above
  count from then. (The order is the exe's; that the thread never finishes
  first is a race the code makes all but certain: it would have to run the
  packets, the whole game frame and every snapshot in the few instructions
  between the signal and the second event loop.) ioquake3 runs `SV_Frame`
  inline, and its second event loop read the snapshot "without a frame of
  latency". Peace's `Com_Frame` now holds what `SV_Frame` sends the local
  client (`NET_HoldLoopPackets` in `net_chan.c`) and releases it just before
  the next frame's first event loop. Nothing else waits: the commands, the
  server's answers to the client's own packets (connect, gamestate) and every
  remote client keep ioquake3's timing. Over every snapshot phase it moves
  the own-host fake reload and fake bullet windows exactly one client frame
  later (8ms at 125 fps, 12 at 85, 17 at 57), and the wild scope, which
  rides on a fake reload, with them. Everything else that sets these windows
  was already the same in both games (the flag's rules, Pmove then the use
  code, the clock, the base, the packet rule, the 50ms snapshots to a host,
  the controller).

At sv_fps 20 on your own host the clock runs 19..68ms behind the newest
snapshot and prediction starts from 39..90ms back (`TestClockBase`; 31..82ms
before the held frame). The 16ms is a floor that no snapshot rate takes away:
with snapshots every 8ms the clock is still 24ms behind and the base 38..40ms
old. That age, and nothing else, is the window: no gameplay timer, bit or
picture rule changed for it.

The cost, as in MW2: `cg.time`, other players and command timestamps sit
about 11ms further behind than under ioquake3's clock. Peace has no antilag,
so moving targets need about 11ms more lead.

## Measured (dev/tests/run-fakereload.sh)

The SPAS and the M300 in hand, 125 fps client, sv_fps 20 (MW2's fixed 50ms
frame, and the shipped autoexec's). Times are from the moment the client sees
the gun at rest.

Re-measured at protocol 86, with MW2's event rings, the view kick and
client-side bullets in: every number on this page was unchanged (the suite's
summary, `FAKERELOAD_PHASES=1` and `FAKERELOAD_MAXPACKETS=30`, compared line
for line with the build before them). None of them touches `Pmove`, the
use/reload bit or the clocks; the kick moves only the aim, and the knife's
press adds events, not time.

Re-measured with MW2's snapshot rate online (`sv_onlineGame`, below). Your
own host's numbers did not move (a loopback client still gets every
snapshot); every ping row is now an internet client getting one every 100ms,
where before it got one every 50ms.

Re-measured before that with MW2's host (the held frame, above). Only your own
host's numbers moved; a remote client's snapshots are not held, so every ping
row is unchanged. Over every snapshot phase each own-host window now ends
exactly one client frame later than on ioquake3's host (the harness checks
the fake bullet's gap by gap). The harness now runs your own host in
`Com_Frame`'s order, so its server frames fall on client frames and the one
phase the per-tap tables below use moved: some of those cells narrowed though
every all-phase rate widened. Compare the all-phase tables.

Fake reload. What matters is when X is let go: by the moment the gun is fully
up, and as close to it as you can. The X press must also be held across the
snapshot the client predicts from as the gun comes up, which you cannot see,
so the window is a success rate. A 64ms tap (online also X held 112ms, longer
than the 100ms between snapshots), SPAS, every snapshot phase, after a knife,
a frag and a Semtex cancel (`FAKERELOAD_PHASES=1`; the held rows from the
harness's `SuccessRate`):

| let go of X, before the gun is up | 0 ms | 8 ms | 16 ms   | 24 ms  | 32 ms  | 40 ms  | 48 ms  | 56 ms  | 64 ms   | 72 ms  | 80 ms  | 88 ms  |
|-----------------------------------|------|------|---------|--------|--------|--------|--------|--------|---------|--------|--------|--------|
| own host (0 ms ping)              | 100% | 100% | 100%    | 88%    | 72%    | 56%    | 40%    | 24%    | 8%      | 0%     | 0%     | 0%     |
| (ioquake3's host, before)         | 100% | 100% | 88%     | 72%    | 56%    | 40%    | 24%    | 8%     | 0%      | 0%     | 0%     | 0%     |
| 50 ms ping, a 64ms tap            | 60–65% | 60–65% | 60–70% | 60–65% | 65–70% | 65–70% | 65–70% | 65–70% | 60–65% | 70% | 60–70% | 65–70% |
| 50 ms ping, X held 112ms          | 100% | 100% | 100%    | 100%   | 100%   | 100%   | 100%   | 100%   | 100%    | 95%    | 80–85% | 75–80% |
| 100 ms ping, a 64ms tap           | 60%  | 60%  | 60–65%  | 60–65% | 60–65% | 60–70% | 60–65% | 65–70% | 65–70%  | 65–70% | 65–70% | 60–65% |
| 100 ms ping, X held 112ms         | 100% | 100% | 100%    | 100%   | 100%   | 100%   | 100%   | 100%   | 100%    | 100%   | 100%   | 100%   |
| (50 ms ping, 64ms, 50ms snapshots, before) | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 100% | 84% | 68% | 48–60% | 32–48% |

(Measured with MW2's offhand prepares: the frag and Semtex pull for 600ms,
holdFireTime, and are thrown or cancelled from HOLD. The frag rows moved by a
snapshot phase or so from the old 150ms/350ms pulls; the windows are still
counted from the gun at rest. The own-host rows are over every client frame
of one cycle of the server's frames against them, the same for all three
actions; the old table's rows sampled the phase unevenly.)

Let go after the gun is up (even 8ms) and it is a real reload, at any ping.
So on your own host it is certain if you let go within 16ms of the gun coming
up (the clock's 16ms, plus the host's held frame, less the frame the release
goes out in), likely up to about 40ms early, and gone by about 72ms: the
chance falls across one snapshot interval, as the base's age runs over one.
With ping it is easy once X is held across one of the server's snapshots:
online they come every 100ms, so X held 112ms is sure well before the gun is
up, while a quick tap is a 60–70% gamble wherever it is let go (a snapshot
every 50ms made the 64ms tap sure: the row marked before). The recovery is the gun's MW2 quick raise (SPAS 0.75s,
M300 0.25s), so "up" is the end of that raise. The harness checks these edges
on both guns by formula from the clock and the held frame (`Windows`): 100%
for every release up to 16ms + the held frame - one frame, within 20 points
of the straight fall at 16ms + the held frame + half an interval, and 0% from
16ms + the held frame + an interval + two frames; online, X held across a
snapshot (100ms and two frames), 100% let go 40ms early at 50ms ping and 72ms
early at 100ms.

A 48ms tap at one snapshot phase fakes when pressed this many ms before the
gun is at rest (the suite's summary; `FAKERELOAD_PROBE=1` prints every tap
time and tap length on the SPAS, and shows `+reload` never fakes):

| after         | gun  | 0 ms     | 50 ms     | 100 ms    | 200 ms    |
|---------------|------|----------|-----------|-----------|-----------|
| knife         | SPAS | 112–48   | 120–80    | 248–48    | 296–56    |
| knife         | M300 | 64–48    | 128–88    | 248–48    | 296–56    |
| frag          | SPAS | 80–48    | 144–48    | 168–48    | 320–80    |
| frag          | M300 | 88–48    | 152–48    | 176–48    | 224–80    |
| Semtex cancel | SPAS | 104–48   | 176–48    | 200–64    | 280–48    |
| Semtex cancel | M300 | 112–48   | 184–48    | 208–64    | 256–48    |

(Online a 48ms tap is seen only where one of the 100ms snapshots falls inside
it, so the ping columns are spans with holes in them: the suite prints how
many 8ms steps of each span fake. With a snapshot every 50ms online they read
128–48, 128–48, 152–48, 152–48, 120–48 and 128–48 at 50ms ping. Before the held
frame the own-host column read 104, 104, 72, 80, 96 and 104: that one phase
moved, see above. The M300's knife return now lands where that phase is
unkind; over every phase the M300 passes the same `Windows` checks as the
SPAS, sure zone included.)

Fake bullet. What matters is how long after X the trigger goes out. X and
the trigger in one command is a real shot, then the reload; the trigger one
command or more later is fake until the client learns of the reload. Which
snapshot the client predicts from cannot be seen, so this too is a success
rate. A one-command X tap, then a one-command trigger pull this long after
it, over every snapshot phase, your own host, sv_fps 20 (`FAKERELOAD_BULLETS=1`
with `FAKERELOAD_CLFRAME`; `FAKERELOAD_HOSTHOLD=0` gives ioquake3's host):

| your own host | MW2's host (Peace now) | ioquake3's host (Peace before) |
|---|---|---|
| 125 fps (8ms) | 8–24: 100%, 32: 88%, 40: 72%, 48: 56%, 56: 40%, 64: 24%, 72: 8%, 80: 0% | 8–16: 100%, 24: 88%, 32: 72%, 40: 56%, 48: 40%, 56: 24%, 64: 8%, 72: 0% |
| 85 fps (12ms), MW2's default | 12–24: 100%, 36: 92%, 48: 68%, 60: 44%, 72: 20%, 84: 0% | 12: 100%, 24: 92%, 36: 68%, 48: 44%, 60: 20%, 72: 0% |
| 60 fps (16ms), console | 16–32: 100%, 48: 80%, 64: 48%, 80: 16%, 96: 0% | 16: 100%, 32: 80%, 48: 48%, 64: 16%, 80: 0% |
| 57 fps (17ms), the IW4x scripts | 17–34: 100%, 51: 80%, 68: 46%, 85: 12%, 102: 0% | 17: 100%, 34: 80%, 51: 46%, 68: 12%, 85: 0% |

So on your own host it is certain within 24ms (two commands at 85 fps, three
at 125), even odds near 55ms, and gone by about 80ms. The suite checks the
125 fps row on both guns (`FakeBulletWindows`): 0% on the same command, 100%
up to 16ms + the held frame, within 20 points of the straight fall half an
interval on, 0% from an interval and two frames past the sure zone, and every
gap exactly one frame later than ioquake3's host. These match MW2's host as
rebuilt from `iw4x.exe` (its server thread, above) gap for gap.

At one snapshot phase (the suite's summary, both guns, from the very next
command):

| 0 ms  | 50 ms | 100 ms | 200 ms |
|-------|-------|--------|--------|
| 8–56  | 8–136 | 8–160  | 8–208  |

(8–64 at 0 ms before the held frame; that phase moved, see above. With a
snapshot every 50ms online the ping columns read 8–88, 8–160 and 8–256: at one
phase a later snapshot can land either side of the shot. Over every phase
online is wider, the table under "Online" below.)

Fired first, or on the same command, the shot is real and the reload follows
at once.

The 8ms is one command at 125 fps. The window opens one command after X at
any frame rate: 16ms at `com_maxfps 60`, about 12ms at MW2's default 85
(`com_maxfps` is 85 in both, MW2's registered at 0x60ad10). MW2 is the same.
Its server runs `Pmove` (0x493686) and fires the bullet that command (the
event loop, 0x5d7000) before `Player_UpdateActivate` (0x4939a1, the only
writer of the flag) looks at X. The flag is read on the next command
(0x5786E3). So X and RT in one command give a real shot in both games. The
input side matches too. IW4x polls the pad once a frame and sends X and RT in
the same pass, in that order (`Controller/Engine/Key.cpp`). Its trigger engages
at 13% of travel, and so does Peace's (`joy_threshold` 0.13). The IW4x fake shot script
below leaves two frames between X and the shot.

`cl_debugButtons 1` prints, for each X and RT press, the command it went out
in and the gap between them ("SAME command" means a real shot).

Wild scope (M300, aim held): a knife this long after the gun is back, from
16ms (the command aim is in; a knife before that is a real hip-fired one):

| after         | 0 ms  | 50 ms | 100 ms | 200 ms |
|---------------|-------|-------|--------|--------|
| knife         | ≤56   | ≤80   | ≤208   | ≤256   |
| frag          | ≤32   | ≤160  | ≤184   | ≤232   |
| Semtex cancel | ≤56   | ≤128  | ≤152   | ≤272   |

(With a snapshot every 50ms online the ping columns read ≤80, ≤152, ≤256;
≤104, ≤128, ≤232; ≤80, ≤152, ≤256.)

These are measured with a packet every frame (loopback and LAN) and MW2's
snapshot rate: your own host gets one every server frame, 50ms at the
shipped autoexec's `sv_fps 20`; every ping row is an internet client and gets
one every 100ms (`sv_onlineGame 1`). (The old autoexec's `sv_fps 125` sent
your own host a snapshot every 8ms frame.) Snapshots every 8ms
(`FAKERELOAD_SVFPS=125`) narrow the own-host fake reload to the clock's floor
alone: 100% let go up to 16ms early, none from 24ms; the 16ms floor keeps the
sure zone. Over the internet `cl_maxpackets`
(default 30) batches commands, which shifts the edges by a packet or so
(`FAKERELOAD_MAXPACKETS=30`). Every per-tap check still holds then; the
success-rate edges are swept over snapshot phase with a packet every frame
(your own host and LAN ignore `cl_maxpackets`), so `Windows` and
`FakeBulletWindows` are skipped there. The harness's own host sends every
frame whatever it is set to, as ioquake3's loopback does.
`FAKERELOAD_CLOCK=1` prints the clock frame by frame.

### Online: why it feels easier (MW2's snapshot rate)

Even with the held frame, the own-host windows are this tight, as on an MW2
host: a sure fake bullet within 24ms of X, gone by about 80ms. The much easier
fake bullet most MW2 players remember is online play, and comes from two
things your own host does not have:

- **Ping.** The client learns of the reload a round trip later.
- **Half the snapshots.** MW2 replaces each client's `snaps` with one snapshot
  every 100ms when `onlinegame` is set (its default; IW4x's dedicated server
  sets it too), else 50ms (`SV_UserinfoChanged` 0x401950: parsed at
  0x401a21, overwritten at 0x401a8a-0x401aa1; `onlinegame` registered at
  0x404cfd; IW4x `Dedicated.cpp:67`). A loopback or LAN client gets a
  snapshot every server frame whatever that says (`SV_SendMessageToClient`
  0x48fe90, 0x48ff84-0x490033), so a host never sees this. Peace's server now
  does exactly that: `sv_onlineGame` (default 1) sets 100ms or 50ms
  (`SV_ClientSnapshotMsec`, stored by `SV_UserinfoChanged`), a loopback or LAN
  client gets every frame (`SV_SnapshotEveryFrame`; `sv_lanForceRate` 1, as
  shipped), and `snaps` has no say. Before, Peace honoured `snaps` (default
  20, 50ms) for every client. `replay_test.c` checks the rule
  (`TestSnapshotRate`), and the harness's ping rows are internet clients of it.

The fake bullet over every phase at 85 fps with `cl_maxpackets 30`
(`FAKERELOAD_BULLETS=1 FAKERELOAD_CLFRAME=12 FAKERELOAD_MAXPACKETS=30`;
`FAKERELOAD_ONLINEGAME=0` for 50ms):

| | 0 ms (your own host) | 50 ms ping | 100 ms ping |
|---|---|---|---|
| a snapshot every 100ms online (MW2; Peace now, `sv_onlineGame 1`) | 12–24: 100%, 36: 92%, 48: 68%, 60: 44%, 72: 20%, 84: 0% | to 72: 100%, 84: 95%, 108: 75%, 132: 51%, 156: 27%, 180: 6%, 204: 0% | to 120: 100%, 144: 90%, 180: 54%, 204: 30%, 228: 8%, 252: 0% |
| a snapshot every 50ms online (Peace before, `sv_onlineGame 0`) | the same | to 72: 100%, 84: 90%, 96: 72%, 108: 48%, 120: 24%, 144: 0% | to 108: 100%, 132: 90%, 156: 50%, 180: 10%, 192: 0% |

(The own-host column is the same in both rows: a loopback client gets every
snapshot and sends every frame, whatever `cl_maxpackets` says.) The fake
reload online needs X held across one of the 100ms snapshots: held 112ms it
is sure (and wide, above); a 64ms tap fakes 60–70% of the time and a 48ms one
about half, wherever it is let go. For real ping and the online rate against
your own server, connect through `127.0.0.1` or a LAN address rather than
`localhost` (the loopback ignores `cl_packetdelay`/`sv_packetdelay`), set
`sv_lanForceRate 0` so that address counts as an internet one, and set the
delays (cheats; the dev autoexec sets `sv_cheats 1`).

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
  later) puts the shot two or three frames (35–53ms) after X, depending on
  how its `wait`s fall. At 57 fps on your own host a shot two frames after X
  (34ms) is always a fake bullet and three frames after (51ms) 80% of the
  time (the table above); under ioquake3's clock it needed ping.
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
