# Shoot, reload: the fastest NAC and the Fayde

MW2 behaviour for every gun whose action is worked between shots (pump, bolt).
No code names either trick.

## Rules

1. **Reload is taken at once** after a shot, during the blast or the pump/bolt.
   The fire cycle is abandoned; the gun still owes its action.
2. **The reload takes the action over.** Its start segment carries
   `WNOTE_ACTION_RESET` on the reload's own clock. If the gun still owes its
   action, the note releases the busy lock, and - only while the gun is still
   reloading - clears that debt. A gun that owes nothing is untouched.
   So a Y on that think swaps instantly (the fastest NAC) but the gun comes
   back owing its whole pump or bolt, as in MW2.

The NAC and the Fayde fall out of the existing ordering in `PM_Weapon`: a swap
always stamps a real holster, a note can release the lock on the same think,
and a holster whose lock is gone finishes before anything else.

## The five cases (dev/tests/shotgun_reload_test.c, 8 ms commands)

| | SPAS | M300 |
|---|---|---|
| plain refire, waiting for the pump/bolt | 648 ms | 960 ms |
| no reload, NAC off the pump/bolt (close note) | 504 ms | 784 ms |
| instant reload, NAC on the first round in | 544 ms | 808 ms |
| **instant reload, Y on the reset think: fastest NAC** | **128 ms** | **168 ms** |
| instant reload, YY one think later: Fayde, fires at | 168 ms | 208 ms |
| instant reload, YY before the reset | pumps again (~680 ms) | bolts again (~992 ms) |

Times are from the shot. The reset currently sits where each reload opens the
action (SPAS 120 ms, M300 160 ms into the reload). Moving it later in the data
makes the Fayde slower; the test only requires it to come before the gun's own
close and its first round.
