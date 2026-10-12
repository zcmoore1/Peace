#!/bin/sh
# Search & Destroy: the round logic driven frame by frame, and the generated
# arena checked against the real engine collision and player movement.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
flags="-std=c99 -O0 -g -ffunction-sections -fdata-sections ${SD_TEST_FLAGS:-}"
common="code/qcommon/q_math.c code/qcommon/q_shared.c"
${CC:-cc} $flags -Icode/game -Icode/qcommon dev/tests/sd_round_test.c $common \
    -Wl,--gc-sections -lm -o out/tests/sd-round-test
out/tests/sd-round-test
${CC:-cc} $flags dev/tests/sd_map_test.c \
    code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c \
    code/qcommon/cm_load.c code/qcommon/cm_trace.c code/qcommon/cm_test.c \
    code/qcommon/cm_patch.c code/qcommon/cm_polylib.c code/qcommon/md4.c \
    $common -Wl,--gc-sections -lm -o out/tests/sd-map-test
out/tests/sd-map-test
