#!/bin/sh
# MW2's silent shot end to end: the real game module, sv_snapshot.c and the
# codec on the server; the real CL_FinishMove and cgame on the client; a shot,
# then the throwing knife's press, swept against the server's frames.
# SILENTSHOT_TEST_FLAGS can enable ASan/UBSan or -DMISSIONPACK.
# SILENTSHOT_TRACE="spas|m300 knife-ms phase [rtt maxpackets shift]" prints one case
# (SILENTSHOT_FRAMES=1 adds the frames around the shot, SILENTSHOT_SVTRACE=1 and
# SILENTSHOT_CLTRACE=1 the server's and the client's shot traces);
# SILENTSHOT_PROBE=1 prints every case; SILENTSHOT_SVFPS sets sv_fps.
# No .cmd mirror: this links whole game, cgame and client files, and GNU ld's
# --gc-sections drops the functions that reach engine code it does not build
# (149 externals stay unresolved without it). MSVC's linker resolves every
# external of every object first. Run it under WSL or with any gcc or clang.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests/silentshot
cc="${CC:-cc} -std=gnu99 -O2 -g -ffunction-sections -fdata-sections ${SILENTSHOT_TEST_FLAGS:-}"
client="-Icode/qcommon -Icode/client -Icode/thirdparty/SDL2-2.32.8/include"
o=out/tests/silentshot
# The game module, apart: its trap_SendConsoleCommand takes (when, text), cgame's (text).
game=""
for f in dev/tests/silent_shot_server.c code/game/g_active.c code/game/g_combat.c code/game/g_weapon.c \
 code/game/g_missile.c code/game/g_client.c code/game/g_utils.c code/game/g_items.c code/game/g_insertion.c \
 code/game/g_cmds.c; do
 b=$(basename "$f" .c)
 $cc -Dtrap_SendConsoleCommand=trap_SendConsoleCommand_game -c "$f" -o $o/game-$b.o
 game="$game $o/game-$b.o"
done
# The client engine: CL_FinishMove and CL_SetUserCmdKickAngles, as the view kick test builds them.
$cc -c dev/tests/kick_client.c $client -o $o/kick-client.o
$cc -c code/client/cl_input.c $client -o $o/cl-input.o
$cc -c code/client/cl_cgame.c $client -o $o/cl-cgame.o
$cc dev/tests/silent_shot_test.c dev/tests/silent_shot_engine.c dev/tests/silent_shot_client.c dev/tests/bullets_world.c \
 code/cgame/cg_predict.c code/cgame/cg_playerstate.c code/cgame/cg_event.c code/cgame/cg_weapons.c \
 code/cgame/cg_kick.c code/cgame/cg_killcam.c code/client/cl_timedelta.c \
 code/qcommon/msg.c code/qcommon/huffman.c \
 code/game/bg_pmove.c code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c \
 code/qcommon/q_math.c code/qcommon/q_shared.c \
 $game $o/kick-client.o $o/cl-input.o $o/cl-cgame.o \
 -Wl,--gc-sections -lm -o out/tests/silent-shot-test
out/tests/silent-shot-test
