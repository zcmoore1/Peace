#!/bin/sh
# MW2's camera and player picture through the real cgame.
# - view_test: CG_CalcViewValues (cg_view.c) with the real sights (cg_sights.c
#   reading the shipped .sights files) and the real Pmove for the eye and the
#   aim permission: MW2's FOV, its ADS lerp and sensitivity, and the eye.
# - player_model_test: the real CG_Player (cg_players.c) with the real tag
#   chain (cg_ents.c) and the real gun (cg_weapons.c): every player, corpse and
#   podium model drawn x1.25 about the feet, with every attachment on it.
# VIEW_TEST_FLAGS can enable ASan/UBSan or -DMISSIONPACK.
# No .cmd mirror: these link whole cgame files, and GNU ld's --gc-sections
# drops the functions that reach code they do not build. MSVC's linker
# resolves every external of every object first. Run it under WSL or with any
# gcc or clang.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
${CC:-cc} -std=c99 -O0 -g -ffunction-sections -fdata-sections ${VIEW_TEST_FLAGS:-} \
 dev/tests/view_test.c dev/tests/view_sights.c dev/tests/fake_reload_pmove.c \
 code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c \
 code/qcommon/q_math.c code/qcommon/q_shared.c -Wl,--gc-sections -lm -o out/tests/view-test
out/tests/view-test
${CC:-cc} -std=c99 -O0 -g -ffunction-sections -fdata-sections ${VIEW_TEST_FLAGS:-} \
 dev/tests/player_model_test.c code/cgame/cg_ents.c code/cgame/cg_weapons.c \
 code/game/bg_pmove.c code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c \
 code/qcommon/q_math.c code/qcommon/q_shared.c -Wl,--gc-sections -lm -o out/tests/player-model-test
out/tests/player-model-test
