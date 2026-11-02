#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
# GAMEPADAIM_TEST_FLAGS can enable ASan/UBSan.
${CC:-cc} -std=c99 -O1 -g -Wall ${GAMEPADAIM_TEST_FLAGS:-} \
 dev/tests/gamepad_aim_test.c code/client/cl_gamepadaim.c \
 -lm -o out/tests/gamepad-aim-test
out/tests/gamepad-aim-test
