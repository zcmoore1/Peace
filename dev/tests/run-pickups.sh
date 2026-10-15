#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
${CC:-cc} -std=c99 -O0 -g -ffunction-sections -fdata-sections ${PICKUP_TEST_FLAGS:-} \
 dev/tests/weapon_pickup_test.c dev/tests/pickup_pmove_driver.c code/game/g_combat.c \
 code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c \
 code/qcommon/q_math.c code/qcommon/q_shared.c -Wl,--gc-sections -lm -o out/tests/weapon-pickup-test
out/tests/weapon-pickup-test
