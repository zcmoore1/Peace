# The Masterkey: MW2's underbarrel shotgun

`WP_MASTERKEY` (18 basegame, 21 missionpack) is MW2's `m4_shotgun_attach_mp`:
the alt weapon of a rifle carrying the Shotgun attachment
(`BG_ATTACHMENT_SHOTGUN`, the Machinegun only). It is never a slot of its own.
It rides in the rifle's slot, and the slot remembers which of its two weapons
is in hand (`BG_GUN_ALT`).

Model plug-in: `assets/source/masterkey/README.md`.

## MW2 sources

- Weapon data: `weapons/mp/m4_shotgun_attach_mp`, plus `m4_shotgun_mp`'s
  `altWeapon` and `altRaiseTime`, in the same IW4 dump as the SPAS, frag and
  flare (`assets/source/masterkey/weapon.json` lists every field used). Every
  one of the dump's 181 alt pairings has `altDropTime 0`.
- Executable (`iw4x.exe`; WeaponDef offsets from IW4x's `Structs.hpp`):
  - `PM_BeginWeaponChange` 575f70: no new change during a drop (states 3-5);
    the alt test 479b80; `iAltDropTime` (+0x290) for an alt switch, which is
    weaponstate 5, `DROPPING_ALT`; no alt switch with `pm_flags & 0x4000`
    (sprinting).
  - `PM_FinishWeaponChange` 576370: after `DROPPING_ALT` the incoming
    weapon's `iAltRaiseTime` (WeaponCompleteDef +0x44), weaponstate 2,
    `RAISING_ALTSWITCH`; back on the weapon it started from, ready with no
    raise.
  - `PM_Weapon` 44c380: the change check 578bc0 runs before the per-hand
    expiry pass 579450, so a drop of 0 finishes on the switch command itself.
  - `CheckForChangeWeapon` 578bc0: no change in the shot (FIRING); one is
    taken out of the rechamber and the reload states; a drop whose command
    comes back to the held weapon is called off (YY).
  - The rechamber 575bd8-575c03: `weaponDelay` is `rechamberBoltTime` when it
    is set and under `rechamberTime`, otherwise 1 ms. The Masterkey's is 0,
    so its shell is chambered 1 ms into the pump. The SPAS's is 0.449.
  - The bolt close 575a30 runs in the expiry pass, after the change check.
  - `weapnext` (the cycle at 59ed40): from an alt weapon it selects that
    weapon's own gun (59ed8c-59edd6). From a gun it goes to the next primary,
    landing on its base and never its alt (59eef4: only `altModeSameWeapon`
    guns remember the alt). From an item, a killstreak weapon included, it
    selects `weaponLatestPrimary` (59ede8), which maps an alt to its base
    (59e270).
  - The sprint weapon check 578060: no sprint raise in FIRING, RECHAMBERING, a
    raise or a drop (states 1-7), a melee or an offhand.
  - `PlayerEquippedWeaponState.needsRechamber`: MW2 keeps pump debt per
    weapon, as Peace's `STAT_UNCHAMBERED` does.

## How it plays

- **D-pad left** (`weapalt`, keyboard `3`) toggles the selected rifle and its
  underbarrel. That is MW2's alt switch: nothing is put down, the incoming
  weapon is in hand on the command, and it raises in its `altRaiseTime`
  (730 ms each way; `weaponDraw == BG_DRAW_ALT`). An alt raise is no lock, so
  another alt switch can come straight out of it. While sprinting it is an
  ordinary swap, dropped and raised in full, as in MW2.
- **Y** from the underbarrel is MW2's switch back to its rifle (an alt
  switch). Y from the rifle goes to the other slot, and Y from there comes back
  to the rifle, never the underbarrel. Peace's slot toggle carries this
  (`BG_ToggleSlotWeapon`).
- **Swap or sprint out of its raise, drop and pump** is the same as every
  other gun, through the same code:
  - Y cancels its raise: an alt switch, or a holster from the raise.
  - YY returns its drop to it, ready on that command.
  - A swap is taken out of its pump. When the 1 ms close lands on the swap's
    own think, the holster is instant and the pump stays owed (Peace's NAC).
  - A sprint lowers it into the carry from its pump.
  - A sprint meanwhile holds a raise or drop to its end, then lowers into the
    carry, as MW2's sprint check refuses states 1-5 for every gun. Sprint never
    cuts a raise or drop in Peace or in MW2. If that is what the "sprint swap"
    should do, it is a rule for every gun, not this one (step 1b in
    `PM_Weapon`).
- **Ammo** belongs to the gun: 4 in the tube, 12 spare at spawn (MW2's
  `startAmmo`), at most 24 (`maxAmmo`), in `slotAltAmmo` and `slotAltReserve`,
  with its chamber debt in `BG_GUN_ALT_UNCHAMBERED`. A dropped rifle carries all
  three in its item's `frame` (`BG_WeaponItemAltFrame`). Scavenging the same
  rifle and attachments tops up the underbarrel's reserve too.
- **Reload** is MW2's segmented reload: the start loads a shell of its own at
  600 ms, each loop pass one at 550, and the end pumps. When the start's shell
  fills the tube, the end follows at once (`PM_AdvanceReloadSegments`).
- **Pellets**: 6, each 25 out to 500 units and falling to 0 at 1750, in a 5°
  cone at the hip or 5.5° aimed (MW2's spread-class data). They are
  client-side bullets like every hitscan shot (MW2's sv_clientSideBullets):
  the server traces them for damage only (`weapon_masterkey_fire`), and every
  client draws them from the fire event (`CG_PelletShot`), both through
  `BG_BulletEnd` from the shot's `BG_BulletSeed`. The cone is the gun's row in
  the shared spread table (`MK_HIP_SPREAD`, `MK_ADS_SPREAD_SCALE`), so the
  crosshair sizes to it and movement widens it as it does every gun's.
  `run-bullets.sh` checks that the shooter's pellets land exactly where the
  server's damage traces went, and that others draw them from the entity.
- **View kick**: m4_shotgun_attach_mp's own, 55-65 pitch and -35..35 yaw, centre
  1400, hip and aimed alike (`bg_viewKick`, from `weapon.json`; see
  `dev/VIEW_KICK.md`). The UAV trigger has killstreak_uav_mp's zero row.

## The moni NAC (Shoot, Y, Y, X)

It is not code. It falls out of the rules above:

1. **Shoot** the underbarrel. The shot holds a swap (FIRING) for 330 ms.
2. **Y** asks for the rifle. On the first think the shot allows, the rifle
   is in hand: an alt switch puts nothing down, so no holster has to beat the
   pump. That is why it is guaranteed. The pump is skipped every time, at any
   input speed, as long as Y came before the pump would have ended.
3. **Y** again puts the rifle's raise down for the other slot.
4. **X** is taken out of the other gun's rise, as any reload is.

Whether the shell went in before the switch is MW2's order of operations: its
change check runs before its bolt close, both in MW2 and in `PM_Weapon`. A
switch on the pump's very first think leaves the pump owed, and coming back
works it from the top. That happens when the shot's last think lands exactly
on the open. A later switch finds the shell in. Either way the swap itself
was instant.

## Tests

`dev/tests/run-masterkey.sh` drives the real `PM_Weapon` in both numberings at
think lengths from 1 to 50 ms: MW2's data, the class, the alt switch and its
sprint exception, `weapnext`, the moni NAC (with the SPAS as the contrast),
swap and sprint out of raise, drop and pump, the UAV trigger and its Y, the
instance's ammo through swaps, drops and scavenges, and the reload. Every rule
was mutation-checked. `dev/tests/run-shotgun-reload.cmd`, and its CI line,
runs the Masterkey through every shoot-reload check, because it has a worked
action.
