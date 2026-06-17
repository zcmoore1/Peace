# Maps

Custom maps for this Quake 3 mod. Maps here are authored as `.map` **source**
(brush geometry + entities in idTech3 text format) and compiled to playable
`.bsp` with `q3map2`.

```
mapping/
  src/                generated/authored .map source (compile these)
  levelshots/         menu thumbnails (<map>.tga)
  scripts/            .arena files (make a map show in the menu list)
  tools/
    mapgen.py         programmatic .map generator (geometry lives here)
    make_levelshot.py procedural levelshot (thumbnail) generator
    compile.sh        q3map2 BSP -> VIS -> LIGHT pipeline + install
  README.md
```

A map needs a `scripts/<map>.arena` file to appear in the in-game map list
(and thus to show its levelshot). `compile.sh` installs it alongside the
`.bsp` and levelshot. Without one, launch from the console with `\devmap <map>`.

## Maps

| Name         | Gametype | Notes                                                        |
|--------------|----------|-------------------------------------------------------------|
| `dm_outpost` | FFA / DM | Small modern-military arena. Central rocket-launcher platform reached by stairs, crate cover, perimeter weapons/health. 6 spawns. |

## Why a generator instead of an editor?

Brush geometry is just text, but every face is a plane defined by three points
whose winding order must make the normal point *out* of the solid — get one
backwards and the tools reject the brush. `mapgen.py` builds boxes with the
winding verified by a cross-product check at generation time, so the output is
always valid. It keeps map layout reviewable as code and diffable in git.

You can still open the generated `.map` in GtkRadiant / NetRadiant-custom to
art-pass it by hand; just don't re-run the generator over hand edits (it
overwrites). For ongoing edits, pick one workflow.

## Regenerate the source

```sh
python3 mapping/tools/mapgen.py          # writes mapping/src/*.map
```

No dependencies beyond Python 3.

## Compile to .bsp

Compilation needs tools and assets that are **not** in this repo (and can't be
— the textures are copyrighted Quake 3 content):

1. Install **q3map2** — it ships with
   [NetRadiant-custom](https://github.com/Garux/netradiant-custom) or
   GtkRadiant. Make sure `q3map2` is on your `PATH`.
2. Have a Quake 3 install whose `baseq3/pak0.pk3` provides the textures.
   Point `Q3_BASEPATH` at the directory that *contains* `baseq3/`.

```sh
Q3_BASEPATH=/path/to/quake3 mapping/tools/compile.sh dm_outpost
```

This runs the three q3map2 stages (BSP, VIS, LIGHT) and copies the result to
`$Q3_BASEPATH/baseq3/maps/dm_outpost.bsp`.

## Play

```sh
ioquake3 +set sv_pure 0 +devmap dm_outpost
```

(`sv_pure 0` lets the engine load a loose `.bsp` that isn't packed in a `.pk3`.)

## Textures

The shader names in `mapgen.py` (`base_wall/metaltech07`, `base_floor/...`,
etc.) target stock `baseq3`. To use the mod's own texture set, edit the shader
constants near the top of the map-build section in `mapgen.py` and regenerate.
Missing textures are non-fatal: q3map2 still compiles and the engine shows a
checkerboard placeholder, so the map remains playable while art is in progress.
