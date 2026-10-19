# Fake reload, fake knife and wild scope

MW2 behaviour, kept as emergent netcode. No code names any of these.

## What the player does

- **Fake reload.** Tap reload (a press, not a hold) as the gun comes back from
  a knife or from equipment (frag, flash, Semtex throw or Semtex cancel). The
  reload animation plays but nothing reloads. You can aim, shoot, or start a
  real reload during it. A held reload is a real reload.
- **Wild scope.** Chain the reload tap straight into a knife tap, then aim.
  The knife plays as well (a fake knife) and you are aimed while it plays.
- **A knife alone** in that moment is late or nothing, never a knife you can
  scope through.
- The window is precise on your own host and much easier on someone else's.

## The two rules that make it

1. **Quick actions are resolved by the server.** When the gun is back from a
   quick action, shared pmove hands it back on that command and the server
   moves the hand-off to its own clock (`BG_ServerReleaseWeapon`, called after
   `Pmove` in `ClientThink_real`). Until then the gun answers only its trigger:
   reload, quick-action and slot-change inputs are masked from `PM_Weapon`.
   Firing is never held, so there is no fake shot. The client predicts the hand-off on the command clock
   and only learns the server's time from the next snapshot. ioquake3 keeps the
   client's command clock just behind the newest snapshot, so the gap is about
   one round trip plus up to one server frame.
2. **The gun never un-plays an action it showed** (`cg_weappicture.c`). When
   prediction is corrected away from a reload, shot or pump of the held gun,
   the viewmodel keeps running the client's own commands through the real
   `Pmove` until that run comes to rest or prediction starts a real action
   (a shot, a reload, a swap, sprint). A correction that only removes a swap is
   followed at once. Aim, camera, HUD and gameplay read the predicted state.

The Semtex pre-release cancel uses the same recovery as every other quick
action: the latch holds until the gun is fully drawn, so sprint is blocked
through that draw and the hand-off happens at its end.

## Measured (dev/tests/run-fakereload.sh)

SPAS in hand, 125 fps client, sv_fps 20. Window = how long after the gun is
visibly back a 16 ms reload tap is still fake.

| after         | 0 ms | 50 ms | 100 ms | 200 ms |
|---------------|------|-------|--------|--------|
| knife         | 48   | 72    | 144    | 240    |
| frag          | 16   | 88    | 112    | 216    |
| Semtex cancel | 40   | 64    | 136    | 200    |

A knife tap a few frames after the reload tap gives the wild scope at every
ping (5 of the 6 tested delays, 8–48 ms; after a frag at 0 ms ping only 1).
Pressed on the very same command, the knife wins and there is no fake reload.

## The cost to ordinary play

Firing is untouched: a shot right as the gun comes back is real and on time.
A reload, knife, grenade or slot change asked for inside the window waits for
the hand-off (a held button) or is lost (a tap), which is the trick itself.
