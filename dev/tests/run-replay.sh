#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
${CC:-cc} -std=c99 -O0 -ffunction-sections -fdata-sections ${REPLAY_TEST_FLAGS:-} \
  dev/tests/replay_test.c code/qcommon/q_math.c code/qcommon/q_shared.c \
  -Wl,--gc-sections -lm -o out/tests/replay-test
out/tests/replay-test
