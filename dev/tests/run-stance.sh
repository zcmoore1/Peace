#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
# MW2's stance button and prone through the real Pmove (dev/STANCE.md).
# STANCE_TEST_FLAGS=-DMISSIONPACK runs the other numbering.
${CC:-cc} -std=c99 -O0 -g -ffunction-sections -fdata-sections ${STANCE_TEST_FLAGS:-} \
    dev/tests/stance_test.c code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c \
    code/qcommon/q_math.c code/qcommon/q_shared.c -Wl,--gc-sections -lm -o out/tests/stance-test
out/tests/stance-test
# The same through the real server: the hit volumes in G_ShotTrace, the
# prone view clamps in SetClientViewAngle (g_weapon.c, g_client.c, g_active.c).
game="code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c"
common="code/qcommon/q_math.c code/qcommon/q_shared.c"
${CC:-cc} -std=c99 -O0 -g -ffunction-sections -fdata-sections ${STANCE_TEST_FLAGS:-} \
    dev/tests/stance_server_test.c code/game/g_active.c code/game/g_combat.c code/game/g_weapon.c \
    code/game/g_missile.c code/game/g_client.c code/game/g_utils.c code/game/g_items.c code/game/g_insertion.c \
    code/game/g_cmds.c code/game/bg_pmove.c $game $common -Wl,--gc-sections -lm -o out/tests/stance-server-test
out/tests/stance-server-test
