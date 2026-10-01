#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
# LOADOUT_TEST_FLAGS can enable ASan/UBSan; normal CI uses the real native code.
common="code/qcommon/q_math.c code/qcommon/q_shared.c"
game="code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c"
flags="-std=c99 -O0 -g -ffunction-sections -fdata-sections ${LOADOUT_TEST_FLAGS:-}"
${CC:-cc} $flags dev/tests/loadout_test.c $game $common -Wl,--gc-sections -lm -o out/tests/loadout-test
out/tests/loadout-test
${CC:-cc} $flags dev/tests/class_menu_test.c code/game/bg_loadout.c $common -Wl,--gc-sections -lm -o out/tests/class-menu-test
out/tests/class-menu-test
${CC:-cc} $flags dev/tests/action_wire_test.c code/qcommon/msg.c code/qcommon/huffman.c $common -Wl,--gc-sections -lm -o out/tests/action-wire-test
out/tests/action-wire-test
${CC:-cc} $flags dev/tests/weapon_pose_test.c code/game/bg_pmove.c $game $common -Wl,--gc-sections -lm -o out/tests/weapon-pose-test
out/tests/weapon-pose-test
${CC:-cc} $flags dev/tests/pose_vm_test.c code/qcommon/vm.c $common -Wl,--gc-sections -lm -o out/tests/pose-vm-test
out/tests/pose-vm-test
