#!/bin/sh
# MW2's stance in the real cgame: the respawn reset in the real
# CG_PredictPlayerState over the real Pmove, the unpredicted eye's lerp, and
# MW2's invalid-command hint through the real arm (cg_event.c) and draw
# (cg_draw.c) (dev/STANCE.md).
# STANCECGAME_TEST_FLAGS can enable ASan/UBSan or -DMISSIONPACK.
# No .cmd mirror: this links whole cgame files, and GNU ld's --gc-sections
# drops the functions that reach code it does not build. MSVC's linker
# resolves every external of every object first. Run it under WSL or with any
# gcc or clang.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
${CC:-cc} -std=c99 -O0 -g -ffunction-sections -fdata-sections ${STANCECGAME_TEST_FLAGS:-} \
 dev/tests/stance_cgame_test.c code/cgame/cg_predict.c code/cgame/cg_draw.c code/cgame/cg_event.c \
 code/game/bg_pmove.c code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c \
 code/qcommon/q_math.c code/qcommon/q_shared.c -Wl,--gc-sections -lm -o out/tests/stance-cgame-test
out/tests/stance-cgame-test
