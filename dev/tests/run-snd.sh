#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
${CC:-cc} -std=c99 -O0 -ffunction-sections -fdata-sections ${SND_TEST_DEFINES:-} \
  dev/tests/snd_test.c code/qcommon/q_math.c code/qcommon/q_shared.c \
  -Wl,--gc-sections -lm -o out/tests/snd-test
out/tests/snd-test
${CC:-cc} -std=c99 -O0 -ffunction-sections -fdata-sections ${SND_TEST_DEFINES:-} \
  dev/tests/snd_hud_test.c code/qcommon/q_math.c code/qcommon/q_shared.c \
  -Wl,--gc-sections -lm -o out/tests/snd-hud-test
out/tests/snd-hud-test
${CC:-cc} -std=c99 -O0 -ffunction-sections -fdata-sections \
  dev/tests/snd_menu_test.c code/qcommon/q_math.c code/qcommon/q_shared.c \
  -Wl,--gc-sections -lm -o out/tests/snd-menu-test
out/tests/snd-menu-test
