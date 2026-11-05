# Tactical Insertion

`WP_TACTICAL_INSERTION` is MW2's Tactical Insertion (`flare_mp`): **lethal
equipment**, chosen in the class menu's LETHAL picker beside the frag, Semtex
and throwing knife and thrown with the lethal button (`BUTTON_LETHAL`), as MW2
lists it in `isValidEquipment` and gives it as the equipment perk. A class
carries exactly one (`BG_ClassEquipmentCount`, flare_mp's startAmmo = maxAmmo
1), so a throwing-knife class never has one too. It is never a tactical:
`BG_ValidateClass` refuses it there. Plant it where you stand; your next life
starts there; pick it back up with use.

Numbering: appended after the throwing knife, 17 in baseq3 and 20 in
missionpack (`WP_NUM_WEAPONS` 18 / 21). Its item, `weapon_tacticalinsertion`,
is appended at the end of `bg_itemlist`, so no item index moved.

## MW2 sources

- Weapon data: `weapons/mp/flare_mp` in the owner-accepted IW4 dump
  (holdFireTime 2.63, fireTime 0.6, fireDelay 0.129, holdButtonToThrow 0,
  offhandHoldIsCancelable 1, cookOffHold 0, weaponClass grenade, offhandClass
  Other, startAmmo/maxAmmo/clipSize 1).
- Script: `maps/mp/perks/_perkfunctions.gsc` (TISpawnPosition, monitorTIUse,
  GlowStickUseListener, GlowStickEnemyUseListener, GlowStickDamageListener,
  deleteTI, clearPreviousTISpawnpoint) and `maps/mp/gametypes/_playerlogic.gsc`
  (the spawn on it).
- Executable (`iw4x.exe`): the offhand state machine (OffHandInit 578e70,
  Prepare 577819, the HOLD release test 578f00-578f69, the Y cancel
  59d5f0/577da0), CheckForOffHand's sea lion gate (5791b0-5791ce), the use list
  and its score biases (5fe430, 5fea34-5fea88), Player_ActivateCmd and
  ActivateHoldCmd (5fe250, 5fe350), and G_FreeEntity's hint clear (416250).

## In hand (`bg_pmove.c`, generic)

Nothing in pmove names it. Its rows in the positional tables and the
classifiers in `bg_loadout.c` are its data:

- **Pull.** weaponClass grenade, so `BG_OffhandPreparesAtOnce` is false: from
  a gun at rest the gun goes down on its own quick drop (`BG_HolsterTime`,
  250ms for every MW2 gun), as MW2's OffHandInit; from an empty hand,
  `BG_EMPTY_HAND_OFFHAND_TIME`.
- **Ignite.** Its draw is `BG_INSERTION_IGNITE_MS` (2630, holdFireTime): MW2's
  OffHandPrepare. No release cuts it short: a tap is queued and the TI is
  thrown on the first command after the ignite ends (MW2's PREPARE ends in
  HOLD, which throws on its next think). Held longer it stays in hand for good
  (cookOffHold 0: it never throws itself) until the release
  (`BG_OffhandHeldToThrow`).
- **Cancel.** Y before the release, in the drop, the ignite or held, sends it
  back to the gun on the gun's quick raise with nothing spent or planted
  (offhandHoldIsCancelable 1), exactly as the Semtex. After the release a Y
  only queues the swap behind the recovery. In the drop and the ignite (MW2's
  INIT and PREPARE) Y cancels whatever the buttons; held after the ignite
  (HOLD) MW2 throws before it reads Y (578f2d-578f52), so Y cancels only with
  lethal down on that command and the last (`PM_OffhandHeld`). A release and
  Y on one command plants it, as does a Y - or lethal pressed again - on the
  first command after a queued tap's ignite; the Y is eaten (weapnext keeps
  the slot while it is out, as MW2's 59d5f0-59d622).
- **Sea lion.** The same gate as any non-knife offhand: with no use hint at
  all (`ENTITYNUM_NONE`) and either no gun or the gun's quick raise after
  equipment, the TI comes straight out into its ignite with the gun never
  lowered - so TI, Y, TI, Y is MW2's own instant double cancel on the lethal
  button. From a gun at rest it always drops.
- **Recovery** is Peace's ONE equipment recovery: `BG_QUICK_ACTION_HOLD`
  (500ms) after the throw, the rapid drop (`BG_RAPID_DROP_TIME`, 50ms), then
  the gun's quick raise; no sprint through it. See "Differences" for MW2's.
- `BG_LethalWeapon` returns it for a class that carries it (held, ammo, or
  the mask bit kept through its last throw); `BG_RebuildWeaponMask` owns its
  bit. The fire's `addTime` is the generic equipment hold
  (`BG_IsActionWeapon && !BG_QuickActionInPlace`), not a per-weapon case.

## Planted (`g_insertion.c`, `FireWeapon`)

MW2 throws an invisible flare (fuse 50ms) and ignores it; monitorTIUse plants
the TI from the throw's `grenade_fire`. Peace plants on the TI's
`EV_FIRE_WEAPON` (`FireWeapon` → `G_PlantInsertion`):

1. The player's previous TI is freed (one per player; a new plant replaces it).
2. Nothing is planted - the TI is spent all the same - with no standing spot
   this life, while touching a `trigger_hurt` (MW2's touchingBadTrigger; Peace
   has no radiation triggers), or dead. A TI held at death is never thrown.
3. **The standing spot** is MW2's TISpawnPosition: sampled every server
   end-frame (`ClientEndFrame` → `G_InsertionSample`, MW2 every 50ms) while
   alive, `PM_NORMAL`, on the ground, and with the standing player box at the
   origin not starting in solid (CanSpawn). It lives in the transient part of
   `gclient_t`, so each life starts without one. A TI thrown in the air,
   crouched under a low ceiling or right after a landing uses the last one.
4. **The ground point** is TIGroundPosition: a standing-box trace from 16
   above the spot to 2048 below it, world only (MW2's mask 0x810011:
   `CONTENTS_SOLID | CONTENTS_PLAYERCLIP`, never a player), plus 1. MW2's
   player origin is its feet; Peace's is `MINS_Z` (24) above them, so the
   entity goes at that trace's feet + 1, on the floor (or the crate the spot
   stood on).
5. The entity is `ET_MISSILE`, `WP_TACTICAL_INSERTION`, `TR_STATIONARY`, facing
   `(0, owner's yaw, 0)` (MW2's self.angles). It keeps the spot itself as its
   playerSpawnPos (`pos1`), its owner (`parent`, with `ownerConnect` =
   `pers.enterTime`, so a new connection in the slot is not the owner),
   `s.clientNum` = owner and `s.generic1` = the owner's team (picture only),
   and `timestamp`, which names it to a use hold. It is a small box (16 x 16 x
   8) of `CONTENTS_CORPSE`: in `MASK_SHOT`, so shots, missiles and splash hit
   it, but not in `MASK_PLAYERSOLID`, so it never blocks movement (and
   `s.solid` 0, so prediction agrees). `r.ownerNum` is no one, so no trace
   passes it for its owner. `G_RunMissile` leaves it alone.

## Use (`g_items.c`, `g_insertion.c`)

- **The hint.** A TI is a use-hint candidate within `player_useRadius`
  (`BG_PICKUP_RANGE`, 128) of the eye, in sight, at any facing (MW2 has no
  facing test for a non-trigger usable), for a player who may use it: its
  owner (`CURSOR_HINT_INSERTION`, MW2's MP_PICKUP_TI) or an enemy - in a team
  game the other team, in FFA everyone but the owner -
  (`CURSOR_HINT_INSERTION_ENEMY`, MP_DESTROY_TI). A teammate can do nothing
  with it, so it is never their hint. MW2 has two entities (the glowstick its
  owner may use, the enemyTrigger enemies may use); one entity asked per
  player at hint time and at use time is the same, since every MW2 path that
  deletes one deletes both.
- **Ranking**, MW2's use list, lowest score first: a resting throwing knife (a
  missile, -512) always wins. A TI (a scriptmover, -192) and a floor gun are
  ranked by `G_UseScore`, 128 * (1 - dot) + eye distance, the TI's less
  `BG_INSERTION_HINT_BIAS`: the TI usually, but not always, beats a gun.
- **The hold** is the knife's, MW2's one useHoldEntity (`useHold`,
  `useHoldSpawn`, `useHoldStart`, which replaced the knife-only fields): a new
  use or use/reload press while the hint is a TI latches it; it is used after
  `BG_UseHoldTime` from the press (`+use` at once, `+usereload` 250ms) and not
  in the first `BG_USE_SPAWN_DELAY` after the player's spawn; a press while
  sprinting or with equipment out uses nothing; a taken hold is a use, never a
  reload; a use/reload tap is still the reload; the reload button alone never
  uses it.
- **Owner pickup** is MW2's setTacticalInsertion: the TI's start ammo (1) is
  set with no room test, the mask rebuilt, the pickup sound played, then the
  TI is freed. **Enemy smash** frees it. Either free empties every hint naming
  it, the user's own included: that is the TI's sea lion entry. Only the hint
  pass sets `FL_CURSOR_HINT`; nothing writes `ENTITYNUM_NONE` directly.

Right after a plant the owner, standing on it, is hinted on the live TI as
soon as the gun is back (MW2 freezes the hint while equipment is out): a live
index, no sea lion, until it is picked up or smashed.

## Damage (`g_insertion.c`, `G_Damage`)

MW2's GlowStickDamageListener: health 5000, destroyed once below 4980, i.e.
once more than `BG_INSERTION_DESTROY_DAMAGE` (20) damage has counted. Damage
from an enemy, the owner or the world counts (FFA: anyone's); in a team game a
teammate's, the owner's aside, is given back. Peace keeps the counted damage
in `count` and holds `health` at 5000, so no refund or huge hit can wrap it.
The real `G_Damage` reaches it through its `pain`/`die` callbacks (a
non-client with `takedamage`); bullets, the railgun, the knife and missiles hit
its box, and `G_RadiusDamage` reaches it as any damageable entity. Destroyed,
it is freed at once; the railgun no longer relinks an entity its beam freed.

## Spawn (`ClientSpawn`, `G_InsertionSpawn`)

A player who owns a live TI spawns at its playerSpawnPos facing its yaw
(pitch and roll 0), and the TI is freed first - MW2 deletes it before
`self spawn` - so the spawn frame's hint pass never names it, and anyone else
hinted on it gets `ENTITYNUM_NONE`. The map spawn point's targets do not fire.
The TI is used up; the next life spawns normally. If another live player
stands in that box, the spawn is the ordinary one and the TI is freed
(Peace's choice, modelled on MW2's own invalid-TI fallback: tiValidationCheck
deletes the TI and spawns normally; MW2 itself never telefrags here). A spawn
on the TI telefrags nobody: `ClientSpawn` skips `G_KillBox` for it, so a corpse
lying there (a dead player not yet respawned, linked as `CONTENTS_CORPSE` with
`body_die`, who is no one standing) is spawned over, never gibbed or credited
to the spawner, as MW2's spawn has no telefrag at all. The
owner is found from the entities (`G_FindInsertion`), so nothing has to
survive `ClientSpawn`'s clear.

Death leaves the TI. It is freed on a team change or joining the spectators
(`SetTeam`), on disconnect (`ClientDisconnect`), and in S&D on every round
reset (`SND_StartRound` frees every missile) and on going back to waiting for
players (`SND_WaitForPlayers` frees TIs). **S&D**: it can be planted, picked
up and smashed, but is never spawned on (one life a round; MW2 has no S&D
code for it either).

## Picture (`cgame`)

- The planted TI is `CG_Missile`'s: registered on first sight, upright at its
  yaw (never spun), with a dynamic light `CG_INSERTION_GLOW_HEIGHT` above it -
  green for its owner and the owner's team, red for everyone else
  (`CG_InsertionFriendly`, from `s.clientNum` in FFA and `s.generic1` in a
  team game; MW2's flare_ambient and flare_ambient_green). Picture only.
- The prompt: `CG_UsePromptShown` (which replaced `CG_KnifePromptShown`, the
  same gate for every held usable) and `CG_UseButtonPrompt` give "Hold [X] to
  pick up Tactical Insertion" / "Press [F] to pick up Tactical Insertion" to
  the owner and "... to smash Tactical Insertion" to an enemy.
- HUD: the lethal line reads `TI 1`. `peacedump` names a TI hint and lists
  every TI in the snapshot (owner, team, yaw, position); `cg_weaponDebug` shows
  the hint's type by name.

## Differences from MW2

- **Recovery.** MW2 plants at fireDelay (129ms) after the release and raises
  the gun after fireTime (600ms). Peace plants on the release command and uses
  its ONE equipment recovery (500ms hold, 50ms rapid drop, the gun's quick
  raise), the owner's rule for every piece of equipment.
- **Occupied spawn.** MW2 spawns on the TI regardless; Peace gives a normal
  spawn and frees the TI if another player stands in the box.
- **No splash, dialog or scoring.** MW2's "ti_destroyed" dialog, the
  destroyer's "denied" points and challenges, the 2.5s dummy flare left by
  deleteTI, the tactical_spawn sound, the Bomb Squad model, the head icon and
  the 15s spawn-weighting penalty are not ported. MW2's care-package check
  (tiValidationCheck) has nothing to check: Peace has no care packages.
- **Frame.** MW2's player origin is its feet, Peace's is 24 above them; the
  ground point is translated (the trace itself is MW2's).

## Placeholders

- The planted TI and the item use the flash's grenade model
  (`models/weapons2/grenadel/grenadel.md3`) and icon (`icons/iconw_grenade`).
- In hand it takes the legacy hands-and-model path, as the flash does: no
  ignite animation, no flare viewmodel, no sounds of its own (MW2's
  viewmodel_flare_ignite, tactical_insert_flare_lift/ignite).
- The glow is a plain dynamic light, not MW2's flare fx.

## Capacity

Two `cursorHint_t` values (`CURSOR_HINT_INSERTION`,
`CURSOR_HINT_INSERTION_ENEMY`) make six, so `PS_CURSOR_HINT_BITS` is 3:
**protocol 84, GAME_VERSION -13**. Rebuild the executable, server and every
module together. No `stats[]`, `pm_flags` or `weaponAction` bit is used.

## Tests

- `run-loadouts.sh` (`loadout_test.c`, `TestTacticalInsertion`): numbering in
  both builds, the classifiers and rows, the quick drop from rest and the
  2630 ignite on every class gun, held for good, a queued tap thrown on the
  first command after the ignite, Y cancels in the drop, the ignite and held,
  a post-release Y, the recovery (also in `TestRecovery` beside the frag, flash
  and Semtex), the sea lion chain and its live-hint and at-rest drops, the
  knife's return, the empty hand, and the class (one, lethal only, both
  formats). `TestHoldThrowsBeforeCancel` (the TI, Semtex and throwing knife):
  MW2's HOLD throws before Y - release and Y on one command, Y or a re-press on
  the first command after a tap - while Y with lethal held, Y on the ignite's
  last command and Y in the drop cancel. `class_menu_test.c`: the LETHAL row, its text and count, no
  TACTICAL row, a saved TI class. `missile_draw_test.c`: the rest pose and
  glow colours. `action_wire_test.c`: the 3-bit hint, 0..5 through the codec.
- `run-pickups.sh`: `weapon_pickup_test.c` (owner pickup with every use
  button, no room test, enemy smash in FFA and team games, teammates, the
  reload button and taps, the spawn gate, sprint and equipment, reach and
  sight, a freed hold, ranking against a knife and a gun, and the TI's sea lion
  end to end through the real plant), `cursor_hint_wiring_test.c` (the real
  `ClientEndFrame` samples the spot and names the TI), `use_hint_test.c`
  (prompt text and gate for owner, enemy and teammate), and
  `insertion_test.c` (the real `FireWeapon` plant and ground point, one per
  player, no spot, dead, trigger_hurt, real `G_Damage` thresholds and the
  teammate rule, railgun, machinegun and splash, the spawn on it inside the
  real `ClientSpawn`, the occupied fallback, a corpse on it killed through the
  real `G_Damage` that the spawn neither gibs nor credits (FFA and team games),
  S&D, a reused slot, and cleanup through the real `SetTeam` and
  `ClientDisconnect`).
- `run-snd.sh` (`snd_test.c`): freed by the round reset and by waiting for
  players.
