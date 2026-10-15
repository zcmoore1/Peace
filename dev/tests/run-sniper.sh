#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
${CC:-cc} -std=c99 -O0 -g -ffunction-sections -fdata-sections ${SNIPER_TEST_FLAGS:-} \
 dev/tests/sniper_test.c code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c \
 code/qcommon/q_math.c code/qcommon/q_shared.c -Wl,--gc-sections -lm -o out/tests/sniper-test
out/tests/sniper-test
