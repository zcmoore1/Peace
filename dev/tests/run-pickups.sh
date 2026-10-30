#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
${CC:-cc} -std=c99 -O0 -g -ffunction-sections -fdata-sections ${PICKUP_TEST_FLAGS:-} \
 dev/tests/weapon_pickup_test.c dev/tests/pickup_pmove_driver.c code/game/g_combat.c \
 code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c \
 code/qcommon/q_math.c code/qcommon/q_shared.c -Wl,--gc-sections -lm -o out/tests/weapon-pickup-test
out/tests/weapon-pickup-test
${CC:-cc} -std=c99 -O0 -g -ffunction-sections -fdata-sections ${PICKUP_TEST_FLAGS:-} \
 dev/tests/cursor_hint_wiring_test.c code/game/g_active.c code/game/g_utils.c code/game/g_items.c \
 code/game/bg_pmove.c code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c \
 code/qcommon/q_math.c code/qcommon/q_shared.c -Wl,--gc-sections -lm -o out/tests/cursor-hint-wiring-test
out/tests/cursor-hint-wiring-test
