# Throwing knife

`WP_THROWING_KNIFE` is MW2's throwing knife (`throwingknife_mp`, weaponClass 9)
as a lethal choice beside the frag and Semtex. All behaviour comes from MW2's
executable. The art is a placeholder: the frag's grenade model is held and
thrown until a knife model lands. Swapping the art means editing only the
item's model in `bg_misc.c` and the missile model in `cg_weapons.c`.

## In hand (`bg_pmove.c`)

- **Pull.** MW2's OffHandInit calls OffHandPrepare in the same call for
  weaponClass 9, so the knife comes out with the gun never lowered, use hint
  or not: `BG_OffhandPreparesAtOnce` feeds the same `prepare` gate as the sea
  lion. The pull is `BG_THROWING_KNIFE_PULL_MS` (`iHoldFireTime`).
- **Hold and throw.** The knife stays raised while the lethal button is held
  and is thrown on release, as the Semtex is. A tap throws on the first command
  after the pull. The knife is spent at the throw, not at the press.
- **Recovery** is the ONE quick-action recovery: `BG_QUICK_ACTION_HOLD`, the
  rapid drop, then the gun's quick raise (OFFHAND_END). With no knives left,
  `BG_LethalWeapon` falls back to the frag, which has none, so a press asks
  for nothing.

## In flight and at rest (`g_missile.c`)

`fire_throwingknife` is MW2's G_FireGrenade for class 9:
- **Launch.** `BG_THROWING_KNIFE_SPEED` along the aim, plus the thrower's own
  speed along it. It falls under gravity and spins end over end at
  `BG_THROWING_KNIFE_SPIN` from 120 degrees back (`apos`; cgame draws the
  knife from `apos` in flight and once stuck).
- **Impact** (`ThrowingKnifeImpact`, MW2's MissileImpact):
  - It hurts only above `g_minGrenadeDamageSpeed` (`BG_THROWING_KNIFE_HIT_SPEED`).
  - Its velocity is reflected and scaled (`BG_THROWING_KNIFE_BOUNCE`).
  - It sticks, buried along the throw one unit off the surface, if it came in
    head-on (the reflection within `BG_THROWING_KNIFE_STICK_DOT` of the normal)
    or settles on a floor below `grenadeRestThreshold`. Otherwise it bounces.
- **Lifetime.** A knife still flying after `BG_THROWING_KNIFE_LIFE` is freed.
  A stopped knife never expires and nothing that touches it moves it.
- **World bound (Peace's, not MW2's).** MW2's engine never frees a stopped
  knife; any cleanup it has is script. To stay clear of entity exhaustion,
  Peace frees the oldest once `BG_THROWING_KNIFE_WORLD_MAX` knives are out and
  another is thrown. S&D also frees every missile between rounds.

## Pickup (`g_items.c`)

- **Use hint.** In the end-frame pass, a knife is a candidate when it is:
  - moving no faster than `bg_maxGrenadeIndicatorSpeed`;
  - in sight;
  - within the eye reach (`player_throwbackOuterRadius`), and first within the
    flat inner radius of the feet (`player_throwbackInnerRadius`) unless the
    hint already names it.

  The player must carry knives this life and have room for one more
  (`knifeMax`, the class's count). There is no owner or team check. The lowest
  `128 * (1 - dot) + distance` wins, and a knife outranks any gun.
- **Use.** A use or use/reload press while the last end-frame's hint is a knife
  is a use and never a reload. MW2's `g_useholdtime` is 0, so the knife is taken
  on that command once it is `g_useholdspawndelay` old (500ms). A press on a
  younger knife holds for it. A press while sprinting or with equipment out
  does nothing. Walking over a knife never collects it.
- **Take.** One more knife and the pickup sound. The hands are not touched.
  The knife is then freed, which empties every use hint that named it. That is
  how the throwing knife enters the sea lion: throw it, pick it back up, and
  equipment pressed in a later quick raise comes straight out until a gun in
  reach becomes the hint.

## Placeholders (weapon data, not in the executable)

These values are fastfile data and must come from the owner:
- `BG_THROWING_KNIFE_SPEED` (1200);
- `BG_THROWING_KNIFE_DAMAGE` (135, one hit);
- `BG_THROWING_KNIFE_BOUNCE` (0.4);
- `BG_THROWING_KNIFE_PULL_MS` (150);
- the quick-action timings in the `MAX_WEAPONS` tables.

`holdButtonToThrow` is taken as 0 (it throws on release).

Not yet done:
- **Sticking in players.** A knife that hits a player falls from the hit
  point instead of sticking (`stickToPlayers` and corpse carry).
- **Empty HUD label.** With no knives left the HUD label reads `LETHAL`.

## Tests

- `dev/tests/knife_missile_test.c` (in `run-loadouts.*`) covers launch, spin,
  stick, bounce, settle, the damage threshold, the pass-through drop, the
  flight lifetime, the stopped knife and the world bound.
- `TestThrowingKnife` (loadout suite) covers the pull, hold, release, tap,
  ammo at the throw and the pull out of a quick raise.
- `TestKnifePickup` and `TestKnifeSeaLion` (pickup suite) cover the hint,
  reach, speed, sight, room, the spawn delay, use-only pickup, the knife
  outranking a gun, busy presses, entity reuse, and the end-to-end sea lion.
- `cursor_hint_wiring_test.c` checks that the real `ClientEndFrame` names a
  knife and that the real `G_FreeEntity` empties the hint.
