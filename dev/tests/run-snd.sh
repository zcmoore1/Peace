#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
${CC:-cc} -std=c99 -O0 -ffunction-sections -fdata-sections ${SND_TEST_DEFINES:-} \
  dev/tests/snd_test.c code/qcommon/q_math.c code/qcommon/q_shared.c \
  -Wl,--gc-sections -lm -o out/tests/snd-test
out/tests/snd-test
# MW2's round end through the real server: the round-winning shot, then the
# throwing knife in the 1.05s before the freeze (the real g_snd.c, ClientThink,
# Pmove, FireWeapon, G_Damage, ClientSpawn and the knife's flight). No .cmd
# mirror: like offhand_server_test.c it links whole game files and relies on
# GNU ld's --gc-sections; MSVC resolves every external first.
${CC:-cc} -std=c99 -O0 -ffunction-sections -fdata-sections ${SND_TEST_DEFINES:-} \
  dev/tests/snd_roundend_test.c code/game/g_active.c code/game/g_combat.c code/game/g_weapon.c \
  code/game/g_missile.c code/game/g_client.c code/game/g_utils.c code/game/g_items.c \
  code/game/g_insertion.c code/game/g_cmds.c code/game/g_killstreak.c code/game/bg_pmove.c code/game/bg_loadout.c \
  code/game/bg_misc.c code/game/bg_slidemove.c code/qcommon/q_math.c code/qcommon/q_shared.c \
  -Wl,--gc-sections -lm -o out/tests/snd-roundend-test
out/tests/snd-roundend-test
${CC:-cc} -std=c99 -O0 -ffunction-sections -fdata-sections ${SND_TEST_DEFINES:-} \
  dev/tests/snd_hud_test.c code/qcommon/q_math.c code/qcommon/q_shared.c \
  -Wl,--gc-sections -lm -o out/tests/snd-hud-test
out/tests/snd-hud-test
${CC:-cc} -std=c99 -O0 -ffunction-sections -fdata-sections \
  dev/tests/snd_menu_test.c code/qcommon/q_math.c code/qcommon/q_shared.c \
  -Wl,--gc-sections -lm -o out/tests/snd-menu-test
out/tests/snd-menu-test
# peace_sd, the generated two-site arena, against the real engine collision
# and player movement.
${CC:-cc} -std=c99 -O0 -ffunction-sections -fdata-sections dev/tests/sd_map_test.c \
  code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c \
  code/qcommon/cm_load.c code/qcommon/cm_trace.c code/qcommon/cm_test.c \
  code/qcommon/cm_patch.c code/qcommon/cm_polylib.c code/qcommon/md4.c \
  code/qcommon/q_math.c code/qcommon/q_shared.c -Wl,--gc-sections -lm -o out/tests/sd-map-test
out/tests/sd-map-test
