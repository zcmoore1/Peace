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
yet the gun still owes its whole pump when it comes back. The fire cycle's own
close note works the same way: it releases the lock on its think but its
chambering commits with the fill, so the NAC off the pump or bolt is instant
and also comes back owing the whole action; one think later the chambering is
kept, and the swap is an ordinary holster.

## The cases (dev/tests/shotgun_reload_test.c, 8 ms commands)

| | SPAS | M300 |
|---|---|---|
| plain refire, waiting for the pump/bolt | 656 ms | 968 ms |
| no reload, NAC off the pump/bolt (close note) | 512 ms | 792 ms |
| ...and swapping back | full pump | full bolt |
| instant reload, NAC on the first round in | 544 ms | 808 ms |
| **instant reload, Y on the settle think: fastest NAC** | **296 ms** | **512 ms** |
| ...and swapping back | full pump | full bolt |
| instant reload, YY one think later: Fayde, fires at | 344 ms | 560 ms |
| instant reload, YY before the settle | pumps again | bolts again |

Times are from the shot. The settle comes out at 283 ms into the SPAS reload
(its pump is 350 ms, capped by the 283 ms reload start) and 500 ms into the
M300's (bolt 620, capped by the 500 ms start). The SPAS Fayde fires at 52% of a
plain refire; MW2's fires at 51%. Retiming a gun's fire cycle or reload start
moves both automatically. The two rows without a reload include MW2's READY
think between the shot and its action: the shot ends READY on its last think
and the pump or bolt starts from READY on the next (`ShouldBeFiring`, then
`CheckForRechamber`), a think later than when Peace ran the shot straight into
the action. The rows with an instant reload start it out of the shot, so they
did not move. Every shot here is a fresh pull: the SPAS and the M300 are single
shot (MW2's fireType), so a trigger held from the shot starts the pump or bolt
on the shot's last think and fires nothing until it is let go; let go on the
action's last command and pulled on the next, it fires at the plain refire.
YY, the Fayde's included, readies the gun with its time zeroed, which lets a
held trigger fire at once (MW2's `PM_Weapon_Idle`).
