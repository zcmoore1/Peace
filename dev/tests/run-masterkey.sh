#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
flags="-std=c99 -O0 -g -ffunction-sections -fdata-sections ${MASTERKEY_TEST_FLAGS:-}"
common="code/qcommon/q_math.c code/qcommon/q_shared.c"
game="code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c"
${CC:-cc} $flags dev/tests/masterkey_test.c $game $common code/qcommon/msg.c code/qcommon/huffman.c -Wl,--gc-sections -lm -o out/tests/masterkey-test
out/tests/masterkey-test
${CC:-cc} $flags -DMASTERKEY_SERVER_TEST dev/tests/masterkey_test.c dev/tests/pickup_pmove_driver.c code/game/g_combat.c code/game/g_insertion.c $game $common -Wl,--gc-sections -lm -o out/tests/masterkey-server-test
out/tests/masterkey-server-test
${CC:-cc} $flags dev/tests/masterkey_selection_test.c code/cgame/cg_playerstate.c dev/tests/pickup_pmove_driver.c $game $common -Wl,--gc-sections -lm -o out/tests/masterkey-selection-test
out/tests/masterkey-selection-test
${CC:-cc} $flags dev/tests/moni_test.c code/cgame/cg_playerstate.c dev/tests/pickup_pmove_driver.c $game $common -Wl,--gc-sections -lm -o out/tests/moni-test
out/tests/moni-test
${CC:-cc} $flags dev/tests/empty_switch_test.c code/cgame/cg_event.c code/cgame/cg_playerstate.c dev/tests/pickup_pmove_driver.c $game $common -Wl,--gc-sections -lm -o out/tests/empty-switch-test
out/tests/empty-switch-test
# MW2's trigger, held against pressed: the shot limit's lock, the dry fire, the knife's end, the reload's stop
${CC:-cc} $flags dev/tests/trigger_test.c code/cgame/cg_event.c code/cgame/cg_playerstate.c dev/tests/pickup_pmove_driver.c $game $common code/qcommon/msg.c code/qcommon/huffman.c -Wl,--gc-sections -lm -o out/tests/trigger-test
out/tests/trigger-test
# MW2's burst fire (the M16 rifle): the pending burst, the cooldown, the pending pull, every interruption
${CC:-cc} $flags dev/tests/burst_test.c code/cgame/cg_event.c code/cgame/cg_playerstate.c dev/tests/pickup_pmove_driver.c $game $common code/qcommon/msg.c code/qcommon/huffman.c -Wl,--gc-sections -lm -o out/tests/burst-test
out/tests/burst-test
# The SF (the G19's select-fire switch) worked as a bolt (owner's rule): beside the SPAS's pump and the M300's bolt
${CC:-cc} $flags dev/tests/sf_test.c code/cgame/cg_playerstate.c dev/tests/pickup_pmove_driver.c $game $common -Wl,--gc-sections -lm -o out/tests/sf-test
out/tests/sf-test
# What a weapon state's last think does (MW2's ProcessHand), through the real Pmove.
${CC:-cc} $flags dev/tests/state_end_test.c $game $common -Wl,--gc-sections -lm -o out/tests/state-end-test
out/tests/state-end-test
