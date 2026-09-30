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
- `dev3b` — **current.** Two-weapon loadout, melee/lethal/tactical, classes,
  sprint transitions, still swap, gamepad, logging

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
- **Never ADS through a reload.** `PM_CheckADS` blocks `WEAPON_RELOADING` on
  purpose. The ZOOMload trick is a client/server desync — the reload animation
  starts client-side and the server never agrees one is happening — not a rule
  being relaxed. Do not reproduce it by loosening the gate.

## Capacity limits

- `pm_flags` — **full.** All 16 networked bits used. A 17th needs the wire
  format widened.
- `STAT_WEAPONS` — bits 0–13 used. Bit 15 must stay unused: `stats[]` round-trip
  as **signed** int16, so a bit-15 weapon reads back negative.
- `weaponAnimSeq` — **full.** 2 bits in `msg.c`; all four values are taken
  (`RSEQ_START/LOOP/END` + `ASEQ_FIRE`). A fifth animation segment has to widen
  the field.
- `weaponstate` — 4 bits, 10 of 16 values used, including `WEAPON_LADDER`.
- `stats[]` — **15 of 16 used in baseq3; all 16 in missionpack.** Every entry round-trips as a SIGNED
  short (`MSG_WriteShort`), so any new bitmask stat has the same bit-15 trap as
  `STAT_WEAPONS`. `STAT_UNCHAMBERED` uses bits 0–13, one per weapon.
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

## Tests

`dev/tests/run-shotgun-reload.cmd` builds and runs `shotgun_reload_test.c`,
which `#include`s `bg_pmove.c` and drives `PM_Weapon` directly — reload
segments, ammo conservation, swap/sprint ordering and the fire cycle. Every
timing in it is read from the shipped tables; never write a literal there, or
retiming a weapon breaks the test without telling you anything.

## Adding a weapon — touches TWO lists

The enum is not enough. A weapon in `STAT_WEAPONS` with no `bg_itemlist` entry
crashes on spawn: `CG_RegisterWeapon` calls `CG_Error`, and `BG_FindItemForWeapon`
calls `Com_Error`. This has already caused one map-load crash. Also check every
`MAX_WEAPONS` table in `bg_pmove.c` — they are positional.
