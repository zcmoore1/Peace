#!/bin/sh
# MW2's client-side bullets end to end: the real server (ClientThink, Pmove,
# FireWeapon, G_Damage) and the real client (CG_PredictPlayerState, the
# playerstate and entity events, CG_FireWeapon) over one world.
# Every bullet's G_Damage is wrapped (--wrap) so the test sees where the real
# bullet code dealt it (the hit volumes, D7).
# BULLETS_TEST_FLAGS can enable ASan/UBSan or -DMISSIONPACK. BULLETS_DEBUG=1
# prints the server's and the client's marks side by side, BULLETS_TRACE=1 each
# command's and frame's events.
# No .cmd mirror: this links whole game and cgame files, and GNU ld's
# --gc-sections drops the functions that reach code it does not build (29
# externals stay unresolved without it). MSVC's linker resolves every external
# of every object first. Run it under WSL or with any gcc or clang.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
${CC:-cc} -std=c99 -O0 -g -ffunction-sections -fdata-sections ${BULLETS_TEST_FLAGS:-} \
 dev/tests/bullets_test.c dev/tests/bullets_server.c dev/tests/bullets_client.c dev/tests/bullets_world.c \
 code/game/g_active.c code/game/g_combat.c code/game/g_weapon.c code/game/g_missile.c code/game/g_client.c \
 code/game/g_utils.c code/game/g_items.c code/game/g_insertion.c code/game/g_cmds.c \
 code/cgame/cg_predict.c code/cgame/cg_playerstate.c code/cgame/cg_event.c code/cgame/cg_kick.c code/cgame/cg_weapons.c \
 code/game/bg_pmove.c code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c \
 code/qcommon/q_math.c code/qcommon/q_shared.c -Wl,--gc-sections -Wl,--wrap=G_Damage -lm -o out/tests/bullets-test
out/tests/bullets-test
