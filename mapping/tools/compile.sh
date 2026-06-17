#!/bin/sh
# Compile a .map source file into a playable .bsp with q3map2 and install it
# into a baseq3 maps/ directory (plus its levelshot, if present).
#
# Requirements (on your machine, not the CI container):
#   * q3map2  - ships with NetRadiant-custom or GtkRadiant; must be on PATH.
#   * A Quake 3 installation whose baseq3/ contains pak0.pk3 (for textures).
#     Point Q3_BASEPATH at the directory that *contains* baseq3/.
#
# Usage:
#   Q3_BASEPATH=/path/to/quake3 ./compile.sh [mapname]
#   (mapname defaults to dm_outpost)
set -e

MAP="${1:-dm_outpost}"
HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../src/$MAP.map"

BASE="${Q3_BASEPATH:-$HOME/.q3a}"
GAME="$BASE/baseq3"

if ! command -v q3map2 >/dev/null 2>&1; then
    echo "error: q3map2 not found on PATH (install NetRadiant-custom or GtkRadiant)" >&2
    exit 1
fi
if [ ! -f "$SRC" ]; then
    echo "error: source map not found: $SRC" >&2
    echo "       run 'python3 mapgen.py' first." >&2
    exit 1
fi
if [ ! -d "$GAME" ]; then
    echo "error: baseq3 not found at $GAME (set Q3_BASEPATH)" >&2
    exit 1
fi

echo ">> BSP   (geometry + entities)"
q3map2 -game quake3 -fs_basepath "$BASE" -meta "$SRC"
echo ">> VIS   (visibility)"
q3map2 -game quake3 -fs_basepath "$BASE" -vis "$SRC"
echo ">> LIGHT (lightmaps)"
q3map2 -game quake3 -fs_basepath "$BASE" -light -fast -filter -super 2 -bounce 4 "$SRC"

mkdir -p "$GAME/maps"
cp "$HERE/../src/$MAP.bsp" "$GAME/maps/$MAP.bsp"
echo ">> installed: $GAME/maps/$MAP.bsp"

# install the menu thumbnail (levelshot) if one exists for this map
SHOT="$HERE/../levelshots/$MAP.tga"
if [ -f "$SHOT" ]; then
    mkdir -p "$GAME/levelshots"
    cp "$SHOT" "$GAME/levelshots/$MAP.tga"
    echo ">> installed: $GAME/levelshots/$MAP.tga"
fi

echo "   play with:  +set sv_pure 0 +devmap $MAP"
