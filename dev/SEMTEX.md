# Semtex

Create a Class → Grenade → Semtex. Equip for the next spawn. Existing classes
retain Frag. Hold `+lethal` (G / right bumper) to pull back and hold; release to
throw. A quick tap queues a throw through preparation. Holding does not cook it
(semtex_mp's cookOffHold 0); only the frag cooks.

## Y before and after release

Before `EV_FIRE_WEAPON` commits the throw, Y / `weapnext` cancels to the **same
gun**, preserves the grenade, and uses that gun's quick raise, as any return.
Release and press lethal again to re-arm; holding through cancel never repeats.

The cancel is MW2's offhand state machine's (owner's rule: do what MW2 does).
During the gun's quick drop (MW2's INIT) and the pullback (PREPARE) Y cancels
whatever the buttons (`577daa-577dde`). Once the Semtex is in hand and ready
(HOLD), MW2 throws first (`578f00-578f69`): unless an offhand button is down on
that command and one was on the command before (`PM_OffhandHeld`), that think
is the throw, and only then is Y read. The offhand buttons are the lethal and
the tactical, either one (MW2's mask `0xc000`, `578f2d-578f3f`), and the two
commands are asked separately, so holding the tactical button alone keeps the
Semtex in hand too. So a release and Y on one command throws, a Y on the first
command after a queued tap's pullback throws, and lethal pressed again on that
first command throws too; Y with an offhand button still held cancels. A Y the
throw wins is eaten (weapnext keeps the slot while the grenade is out, as MW2's
`59d5f0-59d622` sets only the cancel bit), and the recovery is the ordinary one.

Every offhand waits in hand this way (`BG_OffhandHeldToThrow`: holdButtonToThrow
is 0 for all five), but only an offhand whose data says
`offhandHoldIsCancelable` takes the cancel (`BG_OffhandCancelable`, the table in
`bg_loadout.c`): the Semtex, the throwing knife and the Tactical Insertion. The
frag and the flash cannot be cancelled: their Y is weapnext's ordinary toggle,
which asks for the other slot, and that swap waits for the gun's quick raise
after the throw (MW2's CheckForChangeWeapon refuses every offhand state). The
classifier reads the action's own weapon, never the lethal button. Pmove names
none of them.

After emission, Y updates the selected slot/HUD immediately but queues the
physical swap. The throw completes, the original gun recovers, then the normal
weapon drop starts on the exact frame its raise lock expires. A second Y toggles
the pending slot modulo two and can cancel that queued swap. An emitted
projectile is never recalled or refunded. Ladder ownership overrides equipment.
This queue is no longer a Semtex exception: every knife and grenade defers a Y
the same way (MW2's CheckForChangeWeapon refuses its melee and offhand states),
and YY cannot cancel the gun's quick raise after a cancel or a throw.

`cmd.weapon` is still the desired slot. A separate swap-intent button allows
pre-throw cancel without changing selection. A return-acknowledgement bit handles
stale slot requests. `WA_RECOVERING` pins the physical weapon through post-throw
recovery without overwriting the requested selection - only when the Semtex
left a gun: from an empty hand the hand is back the think it empties, as with
every offhand, and a queued Y draws from it.

## Timings / art

The gun goes away on its MW2 quick drop (`BG_WeaponQuickDropTime`, 250ms), as for
every grenade, followed by the 600ms pullback, semtex_mp's holdFireTime (MW2's
OffHandPrepare; the frag and flash have the same 600). After release it shares
`BG_QUICK_ACTION_HOLD` (500ms) and `BG_RAPID_DROP_TIME` (50ms) with frag/flash
(the knife has neither: it ends at `BG_MELEE_TIME`), then the original gun's MW2
quick raise
(`BG_WeaponQuickRaiseTime`; SPAS 750ms, M300 250ms) - a pre-release cancel's
return too. `BG_HolsterTime` and `BG_WeaponDrawTime` supply the same durations
to gameplay and presentation. The pullback, the hold, the launch and the fuse
are semtex_mp's weapon data (the IW4 dump's `weapons/mp` files); the recovery
is Peace's one equipment recovery, not MW2's fireTime. Normal gun-to-gun swap timings, including
SPAS's 1075ms draw and 829ms holster, retain their normal cancellation rules.

Original low-poly prop and gloved hand: `python3 dev/tools/build_semtex_assets.py`.
The first-person pullback/hold/throw poses are picture-only procedural motion,
a functional first art pass rather than imported MW2 animation.

The server creates a projectile at the throw event. It leaves as MW2's throw
does (4c3690): the view's forward at projectileSpeed 940, projectileSpeedUp 120
straight up, snapped, then the thrower's own velocity along that direction
(4c375f-4c37c3: its component only, which may be negative); the start is
Peace's muzzle point, and it is there at the throw (MW2's 408490: trTime is
level.time, with none of Q3's 50ms prestep). Its 2000ms fuse (semtex_mp's fuseTime 2: MW2's fuse,
5e9400, spends the fuse HOLD wrote into `ps.grenadeTimeLeft`, and no stick
path writes another) starts at the throw, not on impact. Held at death, it is
dropped at the body by MW2's death drop (`G_DropOffhand`, 5df920: 40 above
the feet, up to 160 on each axis, none of the holder's velocity) with its whole
fuse, and sticks where it lands. So is one let go on the very command its
holder dies of a fall: the fall's death comes first in that command's events,
before the throw's (MW2's death in FIRE before fireDelay throws it). It sticks without bouncing or contact damage, follows
rotating movers and players, and stops following freed/respawned hosts. Splash
uses existing grenade damage/radius/team rules. A reconnected player cannot
inherit another connection's blast credit. Existing S&D missile cleanup applies.

## Reload / network constraints

A holster still stamps positive time before `PM_RunWeaponNotes`; a note may
clear that lock. Holster completion returns before queued ammo fill. No NAC
condition or special zero-time holster was introduced.

Actual RELOADING still blocks ADS/fire. No ping check, fake reload, animation-only
reload command, or scripted zoomload was added. Prediction continues to reconcile
with the server; this change does **not** claim to reproduce the historical
zoomload desync. The separate note and busy clocks are unchanged.

Rebuild engine/client/server/all VMs together at the current
`PROTOCOL_VERSION`/`GAME_VERSION` (see CLAUDE.md's capacity limits). Old Peace
builds, stock protocols and old demos are incompatible.
Inventory now has 24 ammo entries and a 32-bit `STAT_WEAPONS`; action state uses
nine bits. This fixes pre-existing missionpack equipment exceeding ammo[16] and
missing positional table rows. The missionpack-only powerup stat moved to the
end so the engine's inventory index is identical in both builds. Other stats
remain signed 16-bit; their bit 15 must not be used.

Run `dev/tests/run-loadouts.cmd` or `sh dev/tests/run-loadouts.sh`. Coverage:
cancel stages, post-throw queued swaps/YY, every class gun, note crossings,
delayed real-codec snapshot replay, attachment/fuse, saved classes and all
ten-bit action values. `TestHoldThrowsBeforeCancel` drives the hold, the
queued release, the cancel allowed (Semtex, throwing knife, TI) or refused
(frag, flash) and the either-button hold for every offhand through the real
`PM_Weapon`; `offhand_server_test.c` (`run-loadouts.sh`) throws each through
the real `ClientThink`, `FireWeapon` and missiles: launch velocity, the start
at level.time, fuse, death drop, a release on a fatal fall. Missionpack:
`LOADOUT_TEST_FLAGS=-DMISSIONPACK sh dev/tests/run-loadouts.sh`.
