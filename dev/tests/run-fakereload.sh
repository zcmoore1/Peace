#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
# FAKERELOAD_TEST_FLAGS can enable ASan/UBSan or -DMISSIONPACK.
${CC:-cc} -std=c99 -O1 -g -ffunction-sections -fdata-sections ${FAKERELOAD_TEST_FLAGS:-} \
 dev/tests/fake_reload_test.c dev/tests/fake_reload_pmove.c dev/tests/fake_reload_picture.c code/cgame/cg_weappicture.c \
 code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c code/qcommon/q_math.c code/qcommon/q_shared.c \
 -Wl,--gc-sections -lm -o out/tests/fake-reload-test
out/tests/fake-reload-test
