# SPAS-12: first source audit

Date: 2026-09-08. Baseline: `33e9877`.

## Agreed scope

- Start with the SPAS-12. Defer the sniper and HUD redesign.
- Preserve the existing NAC swap and sprint behavior, which the user has tested.
- NAC emerges from transition ordering and the shared busy timer. Do not add
  NAC detection, a special state, or a zero holster duration.
- Make small, individually reviewable changes.

The findings below describe the baseline source, not an in-game validation or
a claim about exact MW2 timings. The first implementation and its validation
are recorded separately below. The existing local renderer edit in
`code/renderergl1/tr_model_iqm.c` was left alone.

## Current shotgun foundation

There is no SPAS-specific weapon entry yet. `WP_SHOTGUN` is the existing
shotgun foundation; whether to replace it or add a separate entry remains open.

| Area | Current implementation | Source |
| --- | --- | --- |
| Capacity | 8 loaded shells, maximum reserve 40 | `bg_pmove.c`: `bg_weaponMagSize`, `bg_weaponMaxReserve` |
| Reload start | 500 ms; opening sound event at 120 ms | `bg_pmove.c`: `bg_weaponReloads`, `bgnotes_shell_start` |
| Shell loading | Repeating 550 ms segment; one shell transferred at 250 ms within each loop | `bg_pmove.c`: `bgnotes_shell_loop`, `PM_ReloadFillClip` |
| Reload end | 350 ms; bolt-closed event at 180 ms, with no ammo transfer | `bg_pmove.c`: `bgnotes_shell_end` |
| Reload interruption | Swap/sprint abandon progress; already transferred shells stay in `ammo[]` | `bg_pmove.c`: `PM_Weapon`, `PM_FinishWeaponChange` |
| Firing | One shell consumed per shot; 1000 ms busy timer; held attack repeats | `bg_pmove.c`: `PM_Weapon` |
| Shot pattern | 11 pellets, spread constant 700, base damage 10 per pellet; no distance falloff in pellet code | `bg_public.h`: shotgun constants; `g_weapon.c`: `ShotgunPattern`, `ShotgunPellet` |
| Model | Quake shotgun MD3 and associated effects | `bg_misc.c`: `weapon_shotgun`; `cg_weapons.c`: `CG_RegisterWeapon` |

## Findings to address in separate passes

### 1. Fire cannot stop the shell reload

`PM_Weapon` returns while `WEAPON_RELOADING` before it evaluates attack.
`PM_AdvanceReloadSegments` repeats the loop based only on remaining capacity
and reserve. Holding attack with shells already loaded therefore continues
loading until full or reserve is exhausted, then fires after the end segment.

The user selected stopping the reload and returning naturally to ready before
firing. This is the first implemented change, described below. The remaining
findings are deferred to later passes.

### 2. A pump cycle is not represented independently

The shotgun shot sets `WEAPON_FIRING` and a 1000 ms delay. There is no separate
chamber-ready value or post-shot pump action. `WNOTE_BOLT_CLOSED` currently
belongs to the reload end segment; it is not a post-shot pump implementation.

Consequently, a model and a fire animation alone would not define whether an
interrupted pump must finish after returning to the gun. Decide that behavior
before adding state. Sprint can also replace the firing timer through the
existing sprint transition path; do not silently remove that interaction in a
game intended to preserve selected cancellation tricks.

### 3. Skeletal animation registration is a stub

`CG_WeapAnim_RegisterClips` in `cg_anim.c` zeroes the weapon's clip table and
does not read a config or populate frame ranges. All samples then fail the
empty-clip check, and `CG_AddViewWeapon` uses its legacy animation fallback.
The visible reload includes a procedural weapon tilt.

No SPAS model or animation export was found in the searched source tree or
`out` directory. Asset location and export layout remain to be supplied. A
usable integration needs actual frame ranges, model/skeleton agreement, and
start/loop/end reload clips that follow the existing simulation clock.

### 4. The skeletal base layer does not restart or track reload segments

`CG_WeapAnim_UpdateLayers` changes `base->clip` for idle, firing, and reload
without resetting `base->time`. The time continues accumulating while the
base layer is weighted. A non-looping fire or reload clip can therefore be
sampled at its last frame immediately after a long idle. Held-fire repeats
also lack an animation restart trigger.

There is only one `WANIM_RELOAD` clip, and this layer does not use
`weaponAnimSeq` or `weaponAnimTime`. The existing procedural fallback does use
both. This is a dormant defect in the skeletal path, not evidence that the
user's currently tested NAC is broken. Repair and verify this when connecting
SPAS animation assets, without moving gameplay notes into cgame.

### 5. Shotgun spread bypasses the shared movement/ADS spread calculation

The server's `ShotgunPattern` and client's `CG_ShotgunPattern` both use the
fixed shotgun spread constant. `bg_weaponSpread[WP_SHOTGUN]` is zero and the
pellet code does not call `BG_WeaponSpread`.

Decide the SPAS pellet count, spread, damage falloff, and ADS behavior as a
separate tuning pass. Any change to the event-driven pellet pattern must keep
client impact effects and server damage traces in agreement.

## First implementation: attack ends shell loading

The reload definition now has an `interruptible` property, enabled only for
the existing shotgun. After notes and ammo transfer run, attack moves an
interruptible reload to END if at least one round is loaded. The weapon stays
RELOADING for the full existing 350 ms closing segment, including its time
after the bolt-closed note clears the busy timer. Only then can it fire.

- Previously inserted shells remain loaded. An unfinished insertion is
  abandoned; an insertion whose note crosses on this same think completes.
- With an empty tube, held attack loads one shell before entering END.
- Attack during END does not restart the closing animation.
- Holding attack fires when ready. Releasing it before ready leaves the gun
  idle after closing, as explicitly confirmed by the user. No shot is queued.
- Magazine reloads retain their existing behavior.
- Swap/sprint handling and note ordering are unchanged. They run before this
  new check, which only runs while the weapon is still RELOADING.
- No weapon ID, network field, pump action, or SPAS asset was added.

The native tests in `dev/tests/shotgun_reload_test.c` call the real shared
`PM_Weapon` implementation with controlled inputs. They check the following
cases at 1, 8, 16, 33, and 66 ms steps where applicable:

1. Attack with loaded shells during start, before insertion, after insertion,
   and during the end segment.
2. Attack with an empty tube: no dry-loop deadlock and no shot without ammo.
3. Full tube and final reserve shell: finish without an extra insertion.
4. Ammo conservation: loading transfers reserve to the tube; only firing spends
   a shell. Canceling an uncommitted insertion awards nothing.
5. Ordinary swap and sprint cancellations retain previously loaded shells and
   begin a later reload from its start segment.
6. Confirm preserved NAC behavior around the affected shell note after the
   change; the user's existing successful NAC test is the starting baseline.
7. Magazine reloads remain uninterrupted by attack.

Validation completed: native tests passed (1,390 assertions), and the full
clean Debug build completed all 959 steps, including native game modules and
QVMs. The focused tests do not simulate client/server transport or render the
weapon. In-game feel and agreement between predicted and server state remain
to be checked during playtesting.

Run the focused tests from an x64 Native Tools Command Prompt for Visual Studio:

```bat
dev\tests\run-shotgun-reload.cmd
```

Game build command:

```bat
cd /d C:\Users\ZackI\Desktop\OneManIW\ProjectPeace\dev2\Peace
cmake --build out\build\x64-Debug --clean-first
```

## Next decisions

- Where are the SPAS model/animation exports, if already available?
- Should SPAS replace `WP_SHOTGUN` or be an additional weapon? Reusing the
  current shotgun is the smaller integration, but no inventory choice was
  inferred from the instruction to focus on SPAS.
