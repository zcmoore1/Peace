# Semtex

Create a Class → Grenade → Semtex. Equip for the next spawn. Existing classes
retain Frag. Hold `+lethal` (G / right bumper) to pull back and hold; release to
throw. A quick tap queues a throw through preparation. Holding does not cook it.

## Y before and after release

Before `EV_FIRE_WEAPON` commits the throw, Y / `weapnext` cancels to the **same
gun**, preserves the grenade, and uses that gun's quick raise, as any return.
Release and press lethal again to re-arm; holding through cancel never repeats.

The cancel is MW2's offhand state machine's (owner's rule: do what MW2 does).
During the gun's quick drop (MW2's INIT) and the pullback (PREPARE) Y cancels
whatever the buttons (`577daa-577dde`). Once the Semtex is in hand and ready
(HOLD), MW2 throws first (`578f00-578f69`): unless lethal is down on that
command and was on the one before (`PM_OffhandHeld`), that think is the throw,
and only then is Y read. So a release and Y on one command throws, a Y on the
first command after a queued tap's pullback throws, and lethal pressed again on
that first command throws too; Y with lethal still held cancels. A Y the throw
wins is eaten (weapnext keeps the slot while the grenade is out, as MW2's
`59d5f0-59d622` sets only the cancel bit), and the recovery is the ordinary one.
The throwing knife and the Tactical Insertion share this path
(`BG_OffhandHeldToThrow`); pmove names none of them.

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
every grenade, followed by 350ms pullback. After release it shares
`BG_QUICK_ACTION_HOLD` (500ms) and `BG_RAPID_DROP_TIME` (50ms) with frag/flash
(the knife has neither: it ends at `BG_MELEE_TIME`), then the original gun's MW2
quick raise
(`BG_WeaponQuickRaiseTime`; SPAS 750ms, M300 250ms) - a pre-release cancel's
return too. `BG_HolsterTime` and `BG_WeaponDrawTime` supply the same durations
to gameplay and presentation. The pullback, hold and fuse are authored Peace
timings, not measured MW2 values. Normal gun-to-gun swap timings, including
SPAS's 1075ms draw and 829ms holster, retain their normal cancellation rules.

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
nine-bit action values. Missionpack:
`LOADOUT_TEST_FLAGS=-DMISSIONPACK sh dev/tests/run-loadouts.sh`.
