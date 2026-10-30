# Semtex

Create a Class → Grenade → Semtex. Equip for the next spawn. Existing classes
retain Frag. Hold `+lethal` (G / right bumper) to pull back and hold; release to
throw. A quick tap queues a throw through preparation. Holding does not cook it.

## Y before and after release

Before `EV_FIRE_WEAPON` commits the throw, Y / `weapnext` cancels to the **same
gun**, preserves the grenade, and uses that gun's quick raise, as any return.
Release and press lethal again to re-arm; holding through cancel never repeats.

After emission, Y updates the selected slot/HUD immediately but queues the
physical swap. The throw completes, the original gun recovers, then the normal
weapon drop starts on the exact frame its raise lock expires. A second Y toggles
the pending slot modulo two and can cancel that queued swap. An emitted
projectile is never recalled or refunded. Ladder ownership overrides equipment.

`cmd.weapon` is still the desired slot. A separate swap-intent button allows
pre-throw cancel without changing selection. A return-acknowledgement bit handles
stale slot requests. `WA_RECOVERING` pins the physical weapon through post-throw
recovery without overwriting the requested selection.

## Timings / art

The gun goes away on its MW2 quick drop (`BG_WeaponQuickDropTime`, 250ms), as for
every grenade, followed by 350ms pullback. After release it shares
`BG_QUICK_ACTION_HOLD` (500ms) and `BG_RAPID_DROP_TIME` (50ms) with
melee/frag/flash, then the original gun's MW2 quick raise
(`BG_WeaponQuickRaiseTime`; SPAS 750ms, M300 250ms) - a pre-release cancel's
return too. `BG_HolsterTime` and `BG_WeaponDrawTime` supply the same durations
to gameplay and presentation. The pullback, hold and fuse are authored Peace
timings, not measured MW2 values. Normal gun-to-gun swap timings, including
SPAS's 1500ms draw and holster, retain their normal cancellation rules.

Original low-poly prop and gloved hand: `python3 dev/tools/build_semtex_assets.py`.
The first-person pullback/hold/throw poses are picture-only procedural motion,
a functional first art pass rather than imported MW2 animation.

The server creates a projectile at the throw event; its 2500ms fuse starts on
release, not impact. It sticks without bouncing or contact damage, follows
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

Rebuild engine/client/server/all VMs together: protocol **72**, game version
`baseq3-2`. Old Peace builds, stock protocols and old demos are incompatible.
Inventory now has 24 ammo entries and a 32-bit `STAT_WEAPONS`; action state uses
nine bits. This fixes pre-existing missionpack equipment exceeding ammo[16] and
missing positional table rows. The missionpack-only powerup stat moved to the
end so the engine's inventory index is identical in both builds. Other stats
remain signed 16-bit; their bit 15 must not be used.

Run `dev/tests/run-loadouts.cmd` or `sh dev/tests/run-loadouts.sh`. Coverage:
cancel stages, post-throw queued swaps/YY, every class gun, note crossings,
delayed real-codec snapshot replay, attachment/fuse, saved classes and all
nine-bit action values. Missionpack:
`LOADOUT_TEST_FLAGS=-DMISSIONPACK sh dev/tests/run-loadouts.sh`.
