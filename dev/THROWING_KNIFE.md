# Throwing knife

`WP_THROWING_KNIFE` is MW2's throwing knife (`throwingknife_mp`, weaponClass 9)
as a lethal choice beside the frag and Semtex. All behaviour comes from MW2's
executable. Original Peace art supplies the held, thrown, stuck and recovered
knife, with an original gloved hand and a dedicated silhouette icon. The blade
points along +X for the existing projectile/stick orientation. A hand-model
`tag_weapon` authors the first-person grip without rotating the world model.

`CG_EquipmentView` shares the existing Semtex pull and follow-through, reading
each item's own raise time. It draws the hand plus knife while held, and only
the hand after `WA_FIRED`/`WA_RECOVERING`; it never changes ammo, launch, pickup,
collision or timing. Semtex's untagged hand keeps its existing placement.
The source is `dev/tools/build_throwingknife.py` (stdlib, `--check` supported);
provenance and regeneration are in `assets/source/throwingknife/README.md`.

## In hand (`bg_pmove.c`)

- **Pull.** MW2's OffHandInit calls OffHandPrepare in the same call for
  weaponClass 9, so the knife comes out with the gun never lowered, use hint
  or not: `BG_OffhandPreparesAtOnce` feeds the same `prepare` gate as the sea
  lion. The pull is `BG_THROWING_KNIFE_PULL_MS` (`iHoldFireTime`).
- **Hold and throw.** The knife stays raised while the lethal button is held
  and is thrown on release, as the Semtex is. A tap throws on the first command
  after the pull. The knife is spent at the throw, not at the press.
- **Cancel.** As with the Semtex, Y while the knife is held, before the
  release, sends it back to the gun on the gun's quick raise with nothing thrown
  or spent (`BG_OffhandHeldToThrow`: MW2's holdButtonToThrow and
  offhandHoldIsCancelable, the owner's choice). Pulled again in that raise it
  comes straight out. A Y after the throw only queues the swap.
- **Recovery** is the ONE equipment recovery: `BG_QUICK_ACTION_HOLD`, the
  rapid drop, then the gun's quick raise (OFFHAND_END). (The melee knife has
  no hold or drop: it ends at `BG_MELEE_TIME` straight into the quick raise.) From an empty hand it
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
- **Use.** A use (`+activate`) or use/reload (`+usereload`) press while the
  last end-frame's hint is a knife is held for it, exactly as a floor gun is:
  MW2 runs both through Player_ActivateHoldCmd (0x5FE350). How long is
  `BG_UseHoldTime`, from the press: `+activate` takes it at once (MW2's
  `g_useholdtime`, 0 in the executable); use/reload must be held
  `BG_USE_RELOAD_HOLD_TIME`, 250ms, IW4x's `gpad_use_hold_time` (its controller
  rule, hooked into that same check at 0x5FE396: the "Press and Hold" of X),
  and a shorter tap reloads. The reload button alone never takes a knife. Nothing is used in the first
  `BG_USE_SPAWN_DELAY` (500ms, `g_useholdspawndelay`) after the PLAYER spawns;
  the knife's own age is never asked, so a knife may be taken back as soon as
  it is hinted. A button held before the knife comes to rest asks for the
  reload on every command, and its hold starts on the first command after the
  hint names the knife (MW2 latches a press that used nothing again). The
  command that takes the knife is a use, never a reload (MW2's ActivateCmd
  returning true, 0x5FE34C), and a completed hold is never followed by one.
  Let go early, use/reload asks for the reload, as over a gun. A press while
  sprinting or with equipment out does nothing. Walking over a knife never
  collects it. A knife that only
  becomes the hint during a hold for a floor gun takes nothing from that hold:
  MW2 holds for what the press chose.
- **Prompt.** While the hint is a knife the player can take now, the HUD
  reads "Hold [X] to pick up knife" in the floor gun's prompt place, instead of
  the gun's (a knife outranks any gun). The server says so in `ps.cursorHint`,
  MW2's `playerState.cursorHint` (ps+0x194): the hint's type, cleared at the
  top of every end-frame pass (MW2 4942c8) and set only beside a written index
  (49461f), so unlike the sticky `cursorHintEntIndex` it is live. It is
  `CURSOR_HINT_KNIFE` exactly when a use press on the next command takes the
  knife (out of reach, no room, moving, out of sight, sprinting, equipment
  out, dead or not `PM_NORMAL` all leave it `CURSOR_HINT_NONE`; a usable S&D
  site or bomb makes it `CURSOR_HINT_OBJECTIVE`). As in MW2's pass, the 500ms
  spawn delay does not hide it: a press held through the delay takes the knife.
  cgame (`cg_usehint.c`) only draws: it names the key bound to `+usereload`
  ("Hold", `BG_UseHoldTime`), else the one bound to `+use` ("Press": it takes
  the knife on the press), on the kind of input used last: the engine sets
  `cl_gamepadInUse` to 1 on any pad or joystick button or stick and to 0 on any
  keyboard key, mouse button or mouse movement, as IW4x switches its prompts
  (`CL_NoteInputDevice`). So a pad player reads "Hold [X]" and a keyboard
  player "Press [F]" with the shipped configs, whatever `in_joystick` says. The
  other kind's key is the fallback (`trap_Key_GetBindingName`, the cgame trap
  appended at 114). Pad keys read as their labels (X, LB, RT, LS, D-PAD UP);
  with neither command bound it says to bind `+use`. The device is read every
  frame the prompt shows; binds are looked up when it changes, when the prompt
  appears, and then once a second. The prompt hides a frame early where the
  server would refuse the use (sprinting, equipment out, dead, spectating,
  following), and while a hold already running on a floor gun keeps that gun's
  prompt and bar: a plain reload hold, or a use/reload hold the server gave the
  gun. The server's own `pickupHeld` tells that apart (a hold the knife takes
  is rewritten from 3 to 2 on its first command), so a knife that comes into
  reach during a gun's hold never claims the prompt for a hold that takes the
  gun. The bomb needs no test of its own: wherever a site or the bomb owns the
  use the type is `CURSOR_HINT_OBJECTIVE`. There is no hold bar: the knife's
  hold is the server's (`knifeHoldStart`) and is not networked, and a guessed
  bar would disagree with it when the press came before the hint.
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

- `python3 dev/tests/throwingknife_asset_test.py` checks shipped geometry,
  winding/normals, culling bounds, GL1 limits, grip axes, materials and icon.
- `equipment_view_test.c` (`run-loadouts.*`) reads the shipped grip tag and
  checks the real first-person renderer through pull, hold, cancel and release,
  including unchanged Semtex placement and no playerstate mutation.
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
- `TestUseHold` (pickup suite) pins the one hold: knife and gun taken
  `BG_USE_HOLD_TIME` after the press, the player's spawn gate on both, no
  reload after a completed hold, and the reload button collecting a gun but
  never a knife. `TestKnifePickup` and `TestKnifeSeaLion` cover the hint,
  reach, speed, sight, room, no age gate, a button held before the knife
  rests, use-only pickup, the knife outranking a gun, a knife arriving during
  a gun hold, busy presses, entity reuse, and the end-to-end sea lion with
  use and with use/reload (the owner's chain: take the knife back, then a
  flash pressed in a later quick raise comes straight out).
- `cursor_hint_wiring_test.c` checks that the real `ClientEndFrame` names a
  knife, writes the hint's type, and that the real `G_FreeEntity` empties the
  hint.
- `TestCursorHintType` (pickup suite) checks that `ps.cursorHint` is
  `CURSOR_HINT_KNIFE` exactly when a use press takes the knife, case by case.
- `use_hint_test.c` (`run-pickups.sh`) drives the real `CG_DrawWeaponPickup`,
  the prompt gate, the key-name formatter and the button choice over the real
  engine bind lookup (`cl_keys.c`), the bind refresh, and the trap's number in
  `cg_syscalls.asm`; `action_wire_test.c` round-trips the field.
