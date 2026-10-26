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
- **Wild scope.** With a scoped gun (the M300), hold aim, do the fake reload,
  and knife straight after. The knife plays while you stay scoped (a fake
  knife).
- **A knife alone** is never a knife you can scope through. Pressed while the
  gun is still coming back it is nothing (late or nothing, as MW2's knife check
  skips the quick raise). Once the gun is back it is a real knife hip-fired, and
  nothing aimed through a scope (MW2 starts no knife while a scoped gun is
  aimed).
- Every window is precise on your own host and much wider with ping.

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

## Measured (dev/tests/run-fakereload.sh)

The SPAS and the M300 in hand, 125 fps client, sv_fps 20 (MW2's rate). Times
are from the moment the client sees the gun at rest.

Fake reload. What matters is when X is let go: by the moment the gun is fully
up, and as close to it as you can. The X press must also be held across the
server's last snapshot before that moment, which falls anywhere in the last
50ms and which you cannot see, so the window is a success rate. A 64ms tap,
SPAS, every snapshot phase (`FAKERELOAD_PHASES=1`):

| let go of X, before the gun is up | 0 ms  | 8 ms  | 16 ms | 24 ms | 32 ms | 40 ms | 48 ms | 72 ms |
|-----------------------------------|-------|-------|-------|-------|-------|-------|-------|-------|
| own host (0 ms ping)              | 84–100% | 68–80% | 52–64% | 36–48% | 28–32% | 16% | 0% | 0% |
| 50 ms ping                        | 100%  | 100%  | 100%  | 100%  | 100%  | 100%  | 84–100% | 36–52% |
| 100 ms ping and up                | 100%  | 100%  | 100%  | 100%  | 100%  | 100%  | 100%  | 100%  |

Let go after the gun is up (even 8ms) and it is a real reload, at any ping.
So on your own host it is tight but nearly certain if you let go right as the
gun comes up; with ping it is easy. The recovery is the gun's own draw (SPAS
1.5s, M300 0.65s), so "up" is the end of that draw. The harness checks these
edges (`Windows`).

Fake bullet: a shot this long after a 16ms X tap is fake, on both guns:

| 0 ms  | 50 ms | 100 ms | 200 ms |
|-------|-------|--------|--------|
| 8–40  | 8–64  | 8–144  | 8–240  |

Fired first, or on the same command, the shot is real and the reload follows
at once.

Wild scope (M300, aim held): a knife this long after the gun is back:

| after         | 0 ms  | 50 ms | 100 ms | 200 ms |
|---------------|-------|-------|--------|--------|
| knife         | ≤40   | ≤64   | ≤144   | ≤240   |
| frag          | ≤16   | ≤88   | ≤112   | ≤208   |
| Semtex cancel | ≤40   | ≤64   | ≤136   | ≤216   |

These are measured with a packet every frame (loopback and LAN) and 20
snapshots a second. That is also what you get on your own host with the shipped
autoexec: it sets `sv_fps 125`, but a client is sent at most `snaps` snapshots
a second, and `snaps` defaults to 20, MW2's rate. Raising `snaps` to 125
(modelled with `FAKERELOAD_SVFPS=125`) shrinks every own-host window to about
one 8ms command and leaves no wild scope at 0 ping; with ping the windows stay
about the same. Over the internet `cl_maxpackets` (default 30) batches
commands, which shifts the edges by a packet or so (`FAKERELOAD_MAXPACKETS=30`);
every check still holds.

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
  later) needs some ping in Peace: on your own host the shot lands about 53ms
  after X, past the window.
- **Aiming.** MW2's knife rule reads its ADS fraction, so its knife also waits
  for the scope to ease out; Peace has no gameplay ADS fraction and uses the
  aim state.
- **A knife during an ordinary swap.** MW2 refuses a knife in every raise and
  drop; Peace refuses it only while a gun is drawn back from a quick action,
  and a knife asked for during an ordinary holster still cuts it short
  (CLAUDE.md, "Melee is its own kind of swap").
- **A grenade while the knife is out** is lost in Peace; MW2 takes one in every
  melee state.
- **A grenade out of the gun's return** pays the gun's own holster in Peace
  (CLAUDE.md, "Frag/flash keep the gun's holster"); MW2 throws straight out of
  the quick raise, or uses the gun's short quick drop out of the knife.
