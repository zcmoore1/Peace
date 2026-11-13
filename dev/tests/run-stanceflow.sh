#!/bin/sh
# MW2's stance end to end (dev/STANCE.md): the real client command builder
# (cl_input.c, cmd.c, cvar.c, cl_keys.c), the real Pmove on the server and in
# the real prediction (cg_predict.c), the real cgame events and hint
# (cg_playerstate.c, cg_event.c, cg_draw.c), and cgame's console commands back
# into the client's buffer (dev/STANCE.md).
# STANCEFLOW_TEST_FLAGS can enable ASan/UBSan or -DMISSIONPACK.
# No .cmd mirror: this links whole client and cgame files, and GNU ld's
# --gc-sections drops the functions that reach engine code it does not build.
# MSVC's linker resolves every external of every object first. Run it under
# WSL or with any gcc or clang.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
client="-Icode/qcommon -Icode/client -Icode/thirdparty/SDL2-2.32.8/include"
cc="${CC:-cc} -std=gnu99 -O0 -g -ffunction-sections -fdata-sections ${STANCEFLOW_TEST_FLAGS:-}"
for f in dev/tests/stance_flow_client.c code/client/cl_input.c code/client/cl_keys.c code/qcommon/cmd.c code/qcommon/cvar.c; do
 $cc -c "$f" $client -o out/tests/flow-$(basename "$f" .c).o
done
$cc -DCGAME dev/tests/stance_flow_test.c dev/tests/stance_flow_cgame.c \
 code/cgame/cg_predict.c code/cgame/cg_playerstate.c code/cgame/cg_event.c code/cgame/cg_draw.c \
 code/game/bg_pmove.c code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c \
 code/qcommon/q_math.c code/qcommon/q_shared.c \
 out/tests/flow-stance_flow_client.o out/tests/flow-cl_input.o out/tests/flow-cl_keys.o out/tests/flow-cmd.o out/tests/flow-cvar.o \
 -Wl,--gc-sections -lm -o out/tests/stance-flow-test
out/tests/stance-flow-test
