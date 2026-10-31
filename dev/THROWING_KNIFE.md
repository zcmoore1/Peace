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
  rapid drop, then the gun's quick raise (OFFHAND_END). From an empty hand it
  goes back to an empty hand. With no knives left, `BG_LethalWeapon` falls back
  to the frag, which has none, so a press asks for nothing.
- **Out of a busy gun.** The pull also comes straight out of a reload, a shot
  or a pump/bolt. A reload keeps only rounds whose fill landed before the
  press; a note due on the press think is lost (MW2's same-think hand step).
  The reload restarts from 0, and an open action stays owed in
  `STAT_UNCHAMBERED` and is worked from 0 when the gun is back.

## In flight and at rest (`g_missile.c`)

`fire_throwingknife` is MW2's G_FireGrenade for class 9:
- **Launch.** `BG_THROWING_KNIFE_SPEED` along the aim, plus the thrower's own
  speed along it. It falls under gravity and spins end over end at
  `BG_THROWING_KNIFE_SPIN` from 120 degrees back. cgame draws it from `apos`,
  lerped with its origin by `CG_CalcEntityLerpPositions`, and registers its
  model on first sight (`CG_Missile`).
- **Impact** (`ThrowingKnifeImpact`, MW2's MissileImpact):
  - It hurts only above `g_minGrenadeDamageSpeed` (`BG_THROWING_KNIFE_HIT_SPEED`).
  - **A player it hits carries it**, at any speed (`stickToPlayers`, the
    owner's choice). It is buried by MW2's bury branch (0x5E53E0): pitched by
    the surface plus or minus 15 degrees, 1.5 units in and 4.5 back along
    itself. It rides the victim, re-placed each frame with pos and apos
    `TR_INTERPOLATE` and no velocity (MW2's 0x43B9D0). When they respawn,
    `CopyToBodyQue` moves it to their corpse in place (MW2's ClonePlayer).
    The corpse lets go when it starts to sink (`BodySink`) or its body-queue
    slot is reused, and a carrier that respawns without a corpse, disconnects
    or is freed lets go too. The carry is Semtex's (`G_CarryMissile`,
    `G_FollowHost`); only knives move to corpses.
  - Off anything else its velocity is reflected and scaled
    (`BG_THROWING_KNIFE_BOUNCE`). It sticks, along the throw one unit off the
    surface, if it came in head-on (the reflection within
    `BG_THROWING_KNIFE_STICK_DOT` of the normal) or settles on a floor below
    `grenadeRestThreshold`. Otherwise it bounces. `G_Damage` gets a copy of
    the velocity, so a damageable surface (a corpse) does not slow it.
  - A trace that started in solid sends no impact event (MW2, 5e65f1); such a
    knife is lost to its flight lifetime.
- **Rest.** A stopped knife on anything but the world falls again when nothing
  lies `THROWING_KNIFE_SUPPORT` (4.635) behind it: a mover that moved off, a
  corpse that sank, or a carrier that let go (MW2, 5e8c64-5e8d48). The thrower
  is always the trace's pass entity, so they are never hit by their own knife.
- **Lifetime.** A knife still flying after `BG_THROWING_KNIFE_LIFE` is freed.
  A stopped knife never expires, and nothing that touches it moves it.
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
  (`knifeMax`, the class's count). There is no owner or team check, and a
  knife in a player or corpse counts (it has no velocity of its own), so it can
  be taken out of them, as in MW2. The lowest `128 * (1 - dot) + distance`
  wins, and a knife outranks any gun.
- **Use.** A use or use/reload press while the last end-frame's hint is a knife
  is a use and never a reload. MW2's `g_useholdtime` is 0, so the knife is taken
  on that command once it is `g_useholdspawndelay` old (500ms). A press on a
  younger knife holds for it. A press while sprinting or with equipment out
  does nothing. Walking over a knife never collects it. A knife that only
  becomes the hint during a hold for a floor gun takes nothing from that hold:
  MW2 holds for what the press chose.
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
- **World stick orientation.** Only a player hit uses MW2's bury branch; a
  wall or floor stick keeps Peace's own placement (one unit out, along the
  throw) rather than MW2's bury and lie-flat branches.
- **Sinking corpse.** A knife in a sinking corpse hangs about 1.5s where the
  body's box was, then falls when the body unlinks.
- **Semtex on corpses.** MW2's ClonePlayer moves everything linked to a player;
  Peace moves only knives, so a Semtex stays where the player died. Owner's call.
- **Still test.** The candidate test reads `|trDelta|` as MW2 is documented to;
  whether MW2 re-bases it every frame is unverified, so a knife falling after a
  carrier lets go may count as still.
- **Same-think pullback.** MW2 skips the pull when the press lands on the
  think a gun's delayed action expires; Peace still plays the pull there.
- **Empty HUD label.** With no knives left the HUD label reads `LETHAL`.

## Tests

- `dev/tests/knife_missile_test.c` (in `run-loadouts.*`) covers launch, spin,
  stick, bounce, settle, the damage threshold, the player carry and bury, let
  go, the support rule, the corpse hand-off, a start in solid, the flight
  lifetime, the stopped knife and the world bound.
- `knife_corpse_test.c` (`run-loadouts.sh`) links the real `CopyToBodyQue`,
  `BodySink`, `G_FreeEntity` and use code: a knife in a player is the hint,
  moves to the corpse, is taken out of it, or falls when the corpse sinks, is
  reused or its carrier disconnects.
- `missile_draw_test.c` drives the real `CG_Missile` and snapshot lerp: first
  sight registration, a carried knife lerping with its carrier, flight and
  rest, and an unchanged Semtex.
- `TestThrowingKnife` (loadout suite) covers the pull, hold, release, tap,
  ammo at the throw and the pull out of a quick raise; `TestKnifeOutOfReload`,
  `TestKnifeOutOfReloadSettle` and `TestKnifeOutOfAction` the pull out of a busy
  gun on the SPAS and the M300; `TestLethalFromEmptyHand` every lethal from an
  empty hand.
- `TestKnifePickup` and `TestKnifeSeaLion` (pickup suite) cover the hint,
  reach, speed, sight, room, the spawn delay, use-only pickup, the knife
  outranking a gun, a knife arriving during a gun hold, busy presses, entity
  reuse, and the end-to-end sea lion.
- `cursor_hint_wiring_test.c` checks that the real `ClientEndFrame` names a
  knife and that the real `G_FreeEntity` empties the hint.
