# Maps

`box2guns` — a minimal test arena: a sealed, lit square room with two facing
spawns, a rocket launcher and a railgun, and nothing else. Handy for trying
weapon behaviour in isolation.

```
mapping/
  src/box2guns.map        map source (idTech3 brush format)
  levelshots/box2guns.tga menu thumbnail
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

`compile.sh` runs the q3map2 BSP/VIS/LIGHT stages and installs both
`maps/box2guns.bsp` and `levelshots/box2guns.tga` into your `baseq3`.

The full map-authoring pipeline (generators for geometry and levelshots,
plus more maps) lives on the `claude/quake3-mod-maps-67wm8k` branch.
