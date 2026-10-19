# Shoot, reload: the fastest NAC and the Fayde

MW2 behaviour for every gun whose action is worked between shots (pump, bolt).
No code names either trick.

## Rules

1. **Reload is taken at once** after a shot, during the blast or the pump/bolt.
   The fire cycle is abandoned; the gun still owes its action.
2. **The reload's delayed action (MW2's).** When a reload starts on a gun
   that still owes its action, the action is settled after the gun's own
   action length (its fire cycle from opening to close, MW2's
   rechamberBoltTime), counted on the reload's clock and capped by the reload
   start's first round or its end. Nothing is placed by hand. It lands like a
   note: the busy lock goes on that think, and the debt is settled in the same
   step as a queued round - so a swap finishing on that think skips both.

The NAC and the Fayde fall out of the existing ordering in `PM_Weapon`, which
is MW2's: a swap always stamps a real holster, a note or delayed action can
release the lock on the same think, and a holster whose lock is gone finishes
before the reload's delayed work. That is why the fastest NAC swaps instantly
yet the gun still owes its whole pump when it comes back.

## The five cases (dev/tests/shotgun_reload_test.c, 8 ms commands)

| | SPAS | M300 |
|---|---|---|
| plain refire, waiting for the pump/bolt | 648 ms | 960 ms |
| no reload, NAC off the pump/bolt (close note) | 504 ms | 784 ms |
| instant reload, NAC on the first round in | 544 ms | 808 ms |
| **instant reload, Y on the settle think: fastest NAC** | **296 ms** | **512 ms** |
| ...and swapping back | full pump | full bolt |
| instant reload, YY one think later: Fayde, fires at | 344 ms | 560 ms |
| instant reload, YY before the settle | pumps again | bolts again |

Times are from the shot. The settle comes out at 283 ms into the SPAS reload
(its pump is 350 ms, capped by the 283 ms reload start) and 500 ms into the
M300's (bolt 620, capped by the 500 ms start). The SPAS Fayde fires at 53% of a
plain refire; MW2's fires at 51%. Retiming a gun's fire cycle or reload start
moves both automatically.
