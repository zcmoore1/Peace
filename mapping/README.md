# Maps

`box2guns` — a minimal test arena: a sealed, lit square room with two facing
spawns, a rocket launcher and a railgun, and nothing else. Handy for trying
weapon behaviour in isolation.

```
mapping/
  src/box2guns.map        map source (idTech3 brush format)
  levelshots/box2guns.tga menu thumbnail
  scripts/box2guns.arena  menu entry (so the map + thumbnail show in the list)
  tools/compile.sh        q3map2 compile + install
```

## Build and play

A `.map` is source; Quake 3 loads a compiled `.bsp`. Compiling needs `q3map2`
(ships with NetRadiant-custom / GtkRadiant) on your `PATH` and a Quake 3
install whose `baseq3/` provides the textures. Point `Q3_BASEPATH` at the
directory that *contains* `baseq3/`:

```sh
Q3_BASEPATH=/path/to/quake3 mapping/tools/compile.sh box2guns
ioquake3 +set sv_pure 0 +devmap box2guns
```

`compile.sh` runs the q3map2 BSP/VIS/LIGHT stages and installs the
`.bsp`, the `levelshots/box2guns.tga` thumbnail, and `scripts/box2guns.arena`
into your `baseq3`. The arena file makes the map appear in the in-game map
list with its watermelon thumbnail; you can also launch it directly with
`+devmap box2guns`.

## dm_outpost

`dm_outpost` is a small FFA arena source: a central platform reached by
stairs, crate cover and six spawns. `tools/mapgen.py` writes its `.map` and
`tools/make_levelshot.py` its thumbnail; regenerating overwrites hand edits,
so pick one workflow. Its shaders target stock `baseq3` textures, so compile it
with `compile.sh dm_outpost` as above; missing textures show the checkerboard.

The S&D maps (`peace_ladder`, `peace_sd`) do not use this pipeline: they are
written straight to BSP by `dev/tools/build_*_map.py`, with no q3map2 and no
retail textures (see `dev/SEARCH_AND_DESTROY.md`).
