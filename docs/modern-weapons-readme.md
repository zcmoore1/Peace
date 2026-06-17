# Modern weapons mod — spawn weapon model

This mod replaces the default spawn weapon (the machine gun) with a modern
rifle, an **M16**. Players still spawn with the same weapon slot
(`WP_MACHINEGUN`) and the same hitscan/bullet behaviour — only the **model,
pickup name, and HUD icon** change.

## Current state: PLACEHOLDER

There is no cleanly-licensed M16 mesh in this repo yet, so the spawn weapon is
temporarily wired to a **stock Quake 3 model (the railgun)** that already ships
in every `baseq3/pak0.pk3`. This means the spawn weapon renders immediately for
anyone, with zero downloads and zero licensing risk, while still being renamed
`M16` in the HUD. The railgun has a long, rifle-like silhouette; if you prefer
a more real-world look right now, the **shotgun**
(`models/weapons2/shotgun/shotgun.md3`) is the most modern-looking stock asset.

When you have a real M16 art pack, swap two strings in `code/game/bg_misc.c`
(both marked `PLACEHOLDER`) back to the M16 paths below.

## What was changed in code

`code/game/bg_misc.c` — the `weapon_machinegun` item definition:

| Field        | Original (stock)                             | Placeholder (now)                          | Target (real M16)              |
|--------------|----------------------------------------------|--------------------------------------------|--------------------------------|
| world_model  | `models/weapons2/machinegun/machinegun.md3`  | `models/weapons2/railgun/railgun.md3`      | `models/weapons2/m16/m16.md3`  |
| icon         | `icons/iconw_machinegun`                     | `icons/iconw_railgun`                       | `icons/iconw_m16`              |
| pickup name  | `Machinegun`                                 | `M16`                                       | `M16`                          |

That single `world_model[0]` path drives everything. `CG_RegisterWeapon()` in
`code/cgame/cg_weapons.c` strips the extension and derives the related models
automatically, so the pickup model, first-person view model, muzzle flash,
barrel, and hands model all come from the `models/weapons2/m16/` folder.

The internal enum is still called `WP_MACHINEGUN`. That is just an identifier
used throughout the engine; renaming it would be a large, risky refactor with
no gameplay benefit, so it is intentionally left alone.

## Art assets you must supply

**Important:** this repository is engine + game *source code* only. It contains
no `.md3` meshes or textures — those normally live in `pak0.pk3` and mod
`.pk3` files. The code now *references* an M16, but you must provide the actual
art. Place these files in your mod's data folder (e.g. `baseq3/` or your mod
dir), either loose or inside a `.pk3`:

Required (without these the weapon renders invisible / as a missing model):

- `models/weapons2/m16/m16.md3` — the weapon mesh (used for both the world
  pickup and the first-person view model)
- the texture(s) referenced by that `.md3` (e.g. a `.tga`/`.jpg` skin)
- `icons/iconw_m16.tga` — 64×64 HUD / scoreboard icon

Optional (the engine falls back gracefully if they are absent):

- `models/weapons2/m16/m16_hand.md3` — hands rig; if missing, the shotgun hands
  model is used as a fallback
- `models/weapons2/m16/m16_flash.md3` — muzzle flash; skipped if missing
- `models/weapons2/m16/m16_barrel.md3` — separate barrel tag model; skipped if
  missing

### Where to find a model

Any GPL/CC-licensed M16 (or, failing that, another modern or WW2 rifle)
`.md3` weapon pack will work — just rename its files to the `m16.md3`
convention above, or point the `world_model[0]` path in `bg_misc.c` at whatever
folder/filename your pack actually uses. Common sources are the OpenArena and
ioquake3 community asset repositories. Make sure the license permits
redistribution before shipping it with your mod.

## Optional follow-ups (not done yet)

- Replace the fire sounds in `code/cgame/cg_weapons.c` (`WP_MACHINEGUN` case)
  with realistic M16 audio if you have the `.wav` assets.
- Rename the ammo pickup ("Bullets") / its model if you want it themed too.
