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
- **cgame is picture only.** It never touches ammo or weapon state.
- **Never ADS through a reload.** `PM_CheckADS` blocks `WEAPON_RELOADING` on
  purpose. The ZOOMload trick is a client/server desync — the reload animation
  starts client-side and the server never agrees one is happening — not a rule
  being relaxed. Do not reproduce it by loosening the gate.

## Capacity limits (both currently full)

- `pm_flags` — all 16 networked bits used. A 17th needs the wire format widened.
- `STAT_WEAPONS` — bits 0–13 used. Bit 15 must stay unused: `stats[]` round-trip
  as **signed** int16, so a bit-15 weapon reads back negative.

## Adding a weapon — touches TWO lists

The enum is not enough. A weapon in `STAT_WEAPONS` with no `bg_itemlist` entry
crashes on spawn: `CG_RegisterWeapon` calls `CG_Error`, and `BG_FindItemForWeapon`
calls `Com_Error`. This has already caused one map-load crash. Also check every
`MAX_WEAPONS` table in `bg_pmove.c` — they are positional.
