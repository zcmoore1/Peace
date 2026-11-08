# Masterkey: plugging in the model

The Masterkey is MW2's underbarrel shotgun, `m4_shotgun_attach_mp`: the alt
weapon of a rifle with the Shotgun attachment. The gameplay is done and
playable. Until a model exists it is drawn with the stock shotgun model. This
folder is where the model goes in.

## What the model is

A first-person viewmodel exported the same way as the SPAS and the M300: arms
and the rifle-with-underbarrel skinned into one IQM.

| File | What it is |
|---|---|
| `assets/baseq3/models/weapons2/masterkey/view.iqm` | the viewmodel. cgame uses it as soon as it exists (`cg_viewModelBase`, `cg_weapons.c`) |
| `assets/baseq3/models/weapons2/masterkey/view.cfg` | the clip table, **generated**: do not edit it by hand except to move frame starts |
| `assets/baseq3/models/weapons2/masterkey/view.sights` | the ADS sight config, same format as `spas12/view.sights`. Its `iron` pose is the eye in the model's own coordinates, so it can only be written once the model exists; until then the gun aims at the default zoom (50). Give it MW2's `m4_shotgun_attach_mp` numbers, which are in `weapon.json`: `timing 200 200` (adsTransInTime/OutTime) and `zoomfov 55` (adsZoomFov) |
| `assets/baseq3/scripts/peace_masterkey.shader` | map the IQM's baked material names to textures, as `peace_spas.shader` does |

Material names are baked into the IQM, so export with
`models/weapons2/masterkey/...` paths (see the SPAS notes in `CLAUDE.md`).
Export triangles clockwise for Quake's culling, the same as the SPAS importer
(`dev/tools/import_spas.py`).

## The clips

Every clip lasts exactly its gameplay timer (MW2's numbers in `weapon.json`).
The generated `view.cfg` lays them out back to back at `model_fps` (30). Either
export frames in that layout, or keep each clip's name and length and edit only
the first number (its start frame) on each line.

| Clip | Length | MW2 anim it stands for | What happens in it |
|---|---|---|---|
| `idle` | 1000 ms (picture only, loops) | `viewmodel_M4_shotgun_shotty_idle` | at rest |
| `raise` | 829 ms | `..._shotty_pullout` | drawn from another slot or back from the killstreak |
| `drop` | 829 ms | `..._shotty_putaway` | put away to the other slot or the killstreak |
| `alt_raise` | 730 ms | `viewmodel_M4_shotgun_bullet_2_shotty` | the rifle switching to the shotgun (D-pad left). Nothing is put down first |
| `fire` | 760 ms | `..._shotty_fire` + `..._shotty_rechamber` | the shot is the first 330 ms, the pump is the remaining 430. The shell is chambered 1 ms into the pump |
| `reload_start` | 1000 ms | `..._shotty_reload_start` | opens, and loads the first shell at 600 ms |
| `reload_loop` | 750 ms | `..._shotty_reload_loop` | one shell in at 550 ms, repeated |
| `reload_end` | 1350 ms | `..._shotty_reload_end` | closes and pumps |
| `sprint_in` / `sprint_loop` / `sprint_out` | 300 / 699 / 300 ms | `..._shotty_sprint_*` | sprint carry |

The rifle's own switch back from the shotgun (MW2's
`viewmodel_M4_shotgun_shotty_2_bullet`, 730 ms) belongs to the rifle's
viewmodel as its `alt_raise` clip. The rifle has no viewmodel yet, so it uses
its ordinary raise, fitted to the 730 ms.

If a clip is missing, nothing breaks: `alt_raise` falls back to `raise`, and
every other clip reads as no animation.

## One number to set from the model

`reload_end_close_ms` in `weapon.json` is where the reload end's pump closes.
MW2 keeps that in the animation's notetracks (`weap_shotattach_chamber_plr`),
not in its weapon file, so it is a placeholder (659 ms) until the model's
notetrack gives it. Set it, then:

```
python3 dev/tools/build_masterkey.py
```

That rewrites `code/game/bg_masterkey_data.h` and `view.cfg`. After that,
rebuild with the usual build command. `dev/tests/run-masterkey.sh` runs the
generator's `--check` and every Masterkey test.

## What not to change

The timings are MW2's weapon data, and the gameplay timers and clip lengths
are the same numbers. Retime the animation to the clip, never the clip to the
animation. Changing a timer means changing `weapon.json`, which changes the
gameplay too, and the tests read it back from there.
