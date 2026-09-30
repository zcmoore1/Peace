#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
# Optional compiler flags, e.g. LADDER_TEST_DEFINES=-DMISSIONPACK.
${CC:-cc} -std=c99 -O0 -g -ffunction-sections -fdata-sections ${LADDER_TEST_DEFINES:-} \
    dev/tests/ladder_test.c code/game/bg_misc.c code/game/bg_slidemove.c \
    code/qcommon/cm_load.c code/qcommon/cm_trace.c code/qcommon/cm_test.c \
    code/qcommon/cm_patch.c code/qcommon/cm_polylib.c code/qcommon/md4.c \
    code/qcommon/q_math.c code/qcommon/q_shared.c -Wl,--gc-sections -lm -o out/tests/ladder-test
out/tests/ladder-test
