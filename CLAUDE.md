# Peace

CoD-style total conversion on ioquake3.

## Build command — give the user EXACTLY this, never reworded, never varied

```
cd /d C:\Users\ZackI\Desktop\OneManIW\ProjectPeace\dev2\Peace
cmake --build out\build\x64-Debug --clean-first
```

Run it in the **x64 Native Tools Command Prompt for VS**. Always include the
`cd`. Do not substitute paths, drop `/d`, or make `--clean-first` conditional —
the user asked for this block verbatim.

When a pull is also needed, put `git pull origin <branch>` between the two lines.

## Branches

- `dev1` — hipfire spread, dynamic crosshair, sprint/ADS first pass
- `dev2` — NAC via a one-frame `WEAPON_RELOAD_END` state (superseded)
- `dev2-standalone` — skips the retail-data boot check (`fs_checkPak0`)
- `dev3` — NAC rebuilt on notetracks + segmented reloads
- `dev3b` — two-weapon loadout, melee/lethal/tactical, classes, sprint
  transitions, still swap, gamepad, logging
- `dev-3b-gpt` — dev3b + iron-sight ADS and reflex optic, ladders, saved
  classes, modulo slot toggle with drop cancellation
- `dev-claude` — **current.** dev-3b-gpt + Search & Destroy and the `peace_sd`
  arena

## Config files

Both go in `out\build\x64-Debug\Debug\baseq3\`:
- `dev/peace-autoexec.cfg` → rename to `autoexec.cfg`
- `dev/peace-gamepad.cfg` → copy as-is, keep the name (autoexec execs it by name)

## Diagnostics

- `qconsole.log` and `peacedump.txt` land in the **homepath**, not the build dir:
  `%APPDATA%\Quake3\baseq3\`
- Logging defaults to `logfile 2` (unbuffered, survives a hard crash) and
  appends, with a `===== session started` banner per run.
- `peacedump` prints the full weapon/loadout state. `F11` = `peacedump; condump`.
- In-game: `cg_weaponDebug 1`, `g_debugMove 1`, `devmap <map>` (not `map`).
- `spawndummy [red|blue]` (server console) adds a stationary training dummy on
  any map. It needs no bot AI and no `.aas` file — Peace's maps have none, and
  botlib refuses a map without one. The mark is the `peace_dummy` userinfo key,
  so it survives `map_restart`. In team games it joins the named or smaller team.
- **Testing S&D alone:** `g_gametype 4`, `devmap peace_sd`, `team red`, then
  `spawndummy blue`. A round needs someone on both teams. Kill the dummy to win
  by elimination, or plant: you attack round 1, sides swap every round.
- `games.log` records `SD_RoundStart`, `SD_Plant`, `SD_Defuse`, `SD_Detonate`
  and `SD_RoundWin` (winner, both scores, reason).

## Design invariants — do not violate

- **The NAC is not code.** It emerges from ordering in `PM_Weapon`: a holster
  ALWAYS stamps `BG_WeaponDropTime`, `WNOTE_MAG_IN` zeroes `weaponTime`, and a
  transition whose lock is already spent finishes that think and `return`s —
  before the queued clip fill. Never add `if (nac)`, a NAC-named function, a
  special state, or a zero drop time.
- **Two clocks, kept separate.** `weaponAnimTime` counts UP and is what notes
  fire off; `weaponTime` counts DOWN and is only a busy lock. Never derive note
  times from `weaponTime`.
- **Reload never resumes.** It always restarts from 0. No partial-reload state.
- **IW4 still swap is a rule, not a setting.** When a swap collides with the
  sprint carry the sprint animation wins. The Treyarch behaviour is explicitly
  unwanted — do not reintroduce it as a cvar or a branch.
- **Bolting is a state, not a phase of firing.** `WEAPON_FIRING` is the shot;
  `WEAPON_BOLTING` is the action being worked. "Bolting" covers bolt, pump,
  cock and lever — they differ only in cycle length and note placement, never
  in code. It cannot fire and CAN be holstered out of. It ends on the anim
  clock, never on the lock.
- **The bolt cancel is the reload cancel.** `WNOTE_BOLT_CLOSED` on the fire
  animation carries no rounds, so it clears the lock and the unchambered bit —
  the same note, in the same place, that opens the reload's window. Never write
  a second cancel.
- **An open action is a debt, and it is never resumed.** Firing sets the
  weapon's bit in `STAT_UNCHAMBERED`; only the close note clears it. A gun that
  reaches `WEAPON_READY` with its bit set re-enters `WEAPON_BOLTING` from
  elapsed 0 — the whole cycle, exactly like a reload, which never resumes
  either. This is what makes the cancel window a window: miss it and the swap
  costs you the full bolt when you come back. Never add a partial-bolt state,
  and never clear the bit anywhere but the note (spawn is the one exception).
- **Clip frame ranges are data.** They live in the `.cfg` beside the model and
  are read by `CG_WeapAnim_RegisterClips`. Re-exporting a model must never mean
  editing C, and no weapon may be named in that loader.
- **cgame is picture only.** It never touches ammo or weapon state.
- **Selection is a slot toggle.** `weapnext` advances the latest requested slot
  modulo `SLOT_COUNT`, not the server's last acknowledged active slot. If an
  unfinished ordinary holster's destination returns to the gun still held,
  shared pmove cancels the drop and reaches the normal fire gates that think.
  There is no YY timing window or extra delay. Initial drops still stamp their
  real times; chamber debt, ammo, sprint and ladder ownership still apply.
- **Never ADS through a reload.** `PM_CheckADS` blocks `WEAPON_RELOADING` on
  purpose. The ZOOMload trick is a client/server desync — the reload animation
  starts client-side and the server never agrees one is happening — not a rule
  being relaxed. Do not reproduce it by loosening the gate.

## Search & Destroy (`GT_SD`, `code/game/g_round.c`)

- **The server owns the round; the HUD only draws it.** Round and bomb state go
  out in one info-string configstring, `CS_SD_STATE` (26). cgame decodes it in
  `CG_ParseSDState` and never infers or predicts round state.
- **Progress is a start time and a duration, never a fraction.** While a plant
  continues `level.time - plantProgress` is constant, so the configstring does
  not change and a plant in progress costs no bandwidth. Do not publish a
  per-frame value.
- **One plant path.** A brush `trigger_bombsite` stamps `plantTouchTime` from
  its touch; a point site (origin + `radius`) stamps the same field from
  `G_PointSiteAt`. Everything after the stamp is shared. Never fork it.
- **Spawns follow the ROLE, not the team colour.** `team_CTF_redspawn` are
  attacker spawns and `team_CTF_bluespawn` defender spawns, whoever that is this
  round (`G_SelectSDSpawnPoint`). Sides swap every `g_roundswitch` rounds played.
- **The objective owns USE.** Plant and defuse are on `BUTTON_USE` (the hold-to-
  interact button, `f`), and `G_CheckWeaponPickup` yields to `G_OnObjective`.
  `BUTTON_USE_HOLDABLE` is the medkit and the respawn button — never the bomb.
- **Damage is blocked in PREROUND and POSTROUND only.** Not `!= RND_ACTIVE`:
  `RND_NONE` means no round is running (warmup, or an empty team) and the round
  rules do not apply — that is what keeps a solo devmap and the dummy usable.
- **Any map is playable.** No team spawns: the deathmatch spawns are split into
  two ends. No sites: point sites go on the defenders' spawns. The console
  reports which at map load (`G_InitRoundGame`). Authored entities always win.
- One life per round: joining mid-round waits for the next one (`ClientSpawn`).
  Players are `PM_FREEZE` through the pre-round — they can look, not move/fire.
- **A round starts on a clean map.** `G_ClearRoundWorld` frees missiles and
  dropped weapons and unlinks bodies BEFORE anyone respawns. Map pickups stay.
- **Plant and defuse need footing** (`G_OnFoot`: `groundEntityNum` set). That
  one rule rules out jumping, falling and ladders — no ladder-specific check.
- **A button held through a respawn is not a press.** When `PMF_RESPAWNED`
  clears, the held melee/lethal/tactical buttons are seeded into
  `weaponAction`'s held mask. Without it every S&D round start threw the
  grenade of anyone still holding RB from the countdown.

## Capacity limits

- `pm_flags` — **full.** All 16 networked bits used. A 17th needs the wire
  format widened.
- `STAT_WEAPONS` — bits 0–13 used. Bit 15 must stay unused: `stats[]` round-trip
  as **signed** int16, so a bit-15 weapon reads back negative.
- `weaponAnimSeq` — **full.** 2 bits in `msg.c`; all four values are taken
  (`RSEQ_START/LOOP/END` + `ASEQ_FIRE`). A fifth animation segment has to widen
  the field.
- `weaponstate` — 4 bits, 10 of 16 values used, including `WEAPON_LADDER`.
- `weaponAction` — a separate 6-bit predicted/networked field: action type (2),
  fired latch (1), previous melee/lethal/tactical buttons (3). It keeps quick
  taps through holster/deploy and prevents held-button repeats. No new stats
  or pm_flags. Client, server and all game modules must be rebuilt together.
- `stats[]` — **14 of 16 used in baseq3 (2 free); 15 of 16 in missionpack (1
  free).** Counted by compiling `STAT_COUNT`, which `bg_stats_fit_network` in
  bg_public.h asserts at build time. Every entry round-trips as a SIGNED
  short (`MSG_WriteShort`), so any new bitmask stat has the same bit-15 trap as
  `STAT_WEAPONS`. `STAT_UNCHAMBERED` uses bits 0–13, one per weapon.
- Configstrings — `CS_SD_STATE` took 26. Still free: 28–31 (13/15/17/19 are
  the second team-vote slots, not free). S&D added no stats, pm_flags or
  playerState fields.
- `STAT_LADDER` packs attachment, holster destination, jump separation latch,
  and 12-bit inward-facing yaw into bits 0–14. No bit 15, no extra pm_flags.
  Ladder contact stamps `BG_LADDER_DROP_TIME` before the unchanged note pass;
  its destination completes before queued ammo fill, just like a normal holster.
  Never add a reload/NAC-specific ladder branch. See `dev/LADDERS.md`.

## Weapon animation assets

- Source SMDs in `assets/source/<model>/`; built IQM + clip table in
  `assets/baseq3/models/weapons2/<model>/`. CMake copies `assets/baseq3` into
  the build's `baseq3` on every build.
- `dev/tools/import_spas.py` runs under `blender --background --python` and
  builds the IQM. **`SOURCE_FPS` is 60**, inferred, not stated by the pack: 38
  fire frames are MW2's ~1.7 shots/sec and 26 draw frames its snappy draw only
  at 60. It will not overwrite a hand-edited `view.cfg`.
- Clip lengths and the gameplay timers in `bg_pmove.c` are the SAME numbers.
  When they disagree the clip is time-warped to fit and reads as jittery.
- Material strings are baked into the IQM (`models/weapons2/spas12/...`), so
  moving an asset folder breaks its textures until re-export.
- Known SPAS gaps: no sprint clips exist (the gun plays idle while sprinting),
  the drop is the draw reversed, and `gun.tga`/`hands.tga` are 2×2 flat
  placeholders — the mesh UV-maps to `mw2_spas12.bmp`/`v_hands.bmp`, neither of
  which is in the source pack.

## Generated maps

`dev/tools/q3bsp.py` writes tiny vertex-lit BSPs with no q3map2 or retail data:
axis-aligned boxes, one worldspawn model (so no brush entities), flat-colour
shaders. `python3 dev/tools/q3bsp.py` proves it by rebuilding the ladder lab
byte for byte. `build_ladder_map.py` and `build_sd_map.py` are generators;
their `--check` flag (run in CI) fails on a stale committed `.bsp`. Edit the
generator, never the output. Test markers are `info_null` (freed at spawn).

## Tests

`dev/tests/run-shotgun-reload.cmd` builds and runs `shotgun_reload_test.c`,
which `#include`s `bg_pmove.c` and drives `PM_Weapon` directly — reload
segments, ammo conservation, swap/sprint ordering and the fire cycle. Every
timing in it is read from the shipped tables; never write a literal there, or
retiming a weapon breaks the test without telling you anything.

`dev/tests/run-loadouts.cmd` (or `.sh`) checks class validation/persistence,
modulo selection, same-update YY firing, reload/pull-out cancellation, tap/hold
actions, all six action bits through the actual network codec, and pose release.
Loadouts UI and class definitions are documented in `dev/LOADOUTS.md`.

`dev/tests/run-sd.cmd` (or `.sh`) drives the real `g_round.c` frame by frame —
every round outcome, plant/defuse rules, side switch, spawn roles, layout
fallbacks and the exact `CS_SD_STATE` the HUD receives — and checks `peace_sd`
against real collision and movement: spawns clear and landing, sites standable,
the stairs climbable, the hall sealed.

Stock id `PM_WalkMove` re-launches a purely vertical landing that the 0.25u
ground probe catches before the slide touches the floor ("don't decrease
velocity on slopes" renormalises the clip residue to full speed). Any horizontal
speed hides it; a fresh spawn can hop once or twice. It is original Q3
behaviour, so map tests wait for the player to settle instead of asserting one
landing. Do not "fix" it in PM_WalkMove — that function is the movement feel.

Do not statically initialize structs with inline char arrays mixed with ints:
q3asm splits byte literals and word data into different segments. Use word-only
source tables and fill editable structs at runtime, as in `bg_loadout.c`.

## Adding a weapon — touches TWO lists

The enum is not enough. A weapon in `STAT_WEAPONS` with no `bg_itemlist` entry
crashes on spawn: `CG_RegisterWeapon` calls `CG_Error`, and `BG_FindItemForWeapon`
calls `Com_Error`. This has already caused one map-load crash. Also check every
`MAX_WEAPONS` table in `bg_pmove.c` — they are positional.
