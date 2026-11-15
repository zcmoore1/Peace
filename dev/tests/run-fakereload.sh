#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
# FAKERELOAD_TEST_FLAGS can enable ASan/UBSan or -DMISSIONPACK.
# This build also runs every own-host case through the REAL Com_Frame and
# loopback (common.c, net_chan.c; fake_reload_engine.c) and checks it against
# the harness's model of it. GNU ld's --gc-sections drops the parts of
# common.c that reach engine code it does not build; the .cmd builds the
# model alone, as MSVC's linker resolves every external of every object first.
${CC:-cc} -std=c99 -O1 -g -ffunction-sections -fdata-sections -DFAKERELOAD_ENGINE ${FAKERELOAD_TEST_FLAGS:-} \
 dev/tests/fake_reload_test.c dev/tests/fake_reload_pmove.c dev/tests/fake_reload_picture.c code/cgame/cg_weappicture.c code/client/cl_timedelta.c \
 dev/tests/fake_reload_engine.c code/qcommon/common.c code/qcommon/net_chan.c code/qcommon/msg.c code/qcommon/huffman.c \
 code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c code/qcommon/q_math.c code/qcommon/q_shared.c \
 -Wl,--gc-sections -lm -o out/tests/fake-reload-test
out/tests/fake-reload-test
