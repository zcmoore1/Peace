#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
flags="-std=c99 -O0 -g -ffunction-sections -fdata-sections ${PERK_TEST_FLAGS:-}"
common="code/qcommon/q_math.c code/qcommon/q_shared.c"
${CC:-cc} $flags dev/tests/perk_test.c code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c $common -Wl,--gc-sections -lm -o out/tests/perk-test
out/tests/perk-test
${CC:-cc} $flags dev/tests/perk_server_test.c code/game/bg_loadout.c $common -Wl,--gc-sections -lm -o out/tests/perk-server-test
out/tests/perk-server-test
