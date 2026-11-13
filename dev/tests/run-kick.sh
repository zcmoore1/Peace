#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
# KICK_TEST_FLAGS can enable ASan/UBSan or -DMISSIONPACK.
# No .cmd mirror: this links the whole cl_input.c and cl_cgame.c, and GNU ld's
# --gc-sections drops the functions that reach engine code it does not build
# (136 externals stay unresolved without it). MSVC's linker resolves every
# external of every object first, and the QVM half needs q3lcc and q3asm.
# Run it under WSL or with any gcc or clang.
client="-Icode/qcommon -Icode/client -Icode/thirdparty/SDL2-2.32.8/include"
${CC:-cc} -std=gnu99 -O0 -g -ffunction-sections -fdata-sections ${KICK_TEST_FLAGS:-} -c dev/tests/kick_client.c $client -o out/tests/kick-client.o
${CC:-cc} -std=gnu99 -O0 -g -ffunction-sections -fdata-sections ${KICK_TEST_FLAGS:-} -c code/client/cl_input.c $client -o out/tests/kick-cl-input.o
${CC:-cc} -std=gnu99 -O0 -g -ffunction-sections -fdata-sections ${KICK_TEST_FLAGS:-} -c code/client/cl_cgame.c $client -o out/tests/kick-cl-cgame.o
${CC:-cc} -std=gnu99 -O0 -g -ffunction-sections -fdata-sections ${KICK_TEST_FLAGS:-} -DCGAME \
 dev/tests/kick_test.c dev/tests/kick_pmove.c dev/tests/kick_stubs.c code/cgame/cg_kick.c code/cgame/cg_event.c code/cgame/cg_playerstate.c \
 code/cgame/cg_killcam.c code/cgame/cg_syscalls.c code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c \
 code/qcommon/q_math.c code/qcommon/q_shared.c \
 out/tests/kick-client.o out/tests/kick-cl-input.o out/tests/kick-cl-cgame.o \
 -Wl,--gc-sections -lm -o out/tests/kick-test
out/tests/kick-test

# The same kick across the QVM boundary, as a client runs cgame.qvm (vm_cgame 2,
# or any pure server): q3lcc and q3asm build the real cg_kick.c with the real
# cg_syscalls.asm, the real vm.c and vm_interpreted.c run it, and the real
# CL_CgameSystemCalls and CL_FinishMove answer. Q3LCC_DIR can name an existing
# build's tools directory (q3lcc, q3cpp, q3rcc, q3asm).
q=out/tests/kickqvm
mkdir -p $q
if [ -n "${Q3LCC_DIR:-}" ]; then
 t=$Q3LCC_DIR
else
 cmake -S cmake/tools -B $q/tools -DSOURCE_DIR="$PWD/code" -DCMAKE_MODULE_PATH="$PWD/cmake" \
  -DCMAKE_MINIMUM_REQUIRED_VERSION=3.25 -DCMAKE_BUILD_TYPE=Release > $q/tools.log
 cmake --build $q/tools >> $q/tools.log
 t=$q/tools/Release
fi
lccdefs=-DCGAME
case "${KICK_TEST_FLAGS:-}" in *-DMISSIONPACK*) lccdefs="$lccdefs -DMISSIONPACK";; esac
asm=""
for f in dev/tests/kick_qvm_cgame.c code/cgame/cg_kick.c code/qcommon/q_math.c code/qcommon/q_shared.c code/game/bg_lib.c; do
 $t/q3lcc $lccdefs -o $q/$(basename "$f" .c).asm "$f"
 asm="$asm $q/$(basename "$f" .c).asm"
done
$t/q3asm -o $q/kick.qvm $asm code/cgame/cg_syscalls.asm
cc="${CC:-cc} -std=gnu99 -O0 -g -ffunction-sections -fdata-sections ${KICK_TEST_FLAGS:-}"
for f in code/qcommon/vm.c code/qcommon/vm_interpreted.c code/qcommon/vm_ptr.c; do
 $cc -c "$f" $client -o $q/$(basename "$f" .c).o
done
$cc dev/tests/kick_qvm_test.c dev/tests/qvm_engine.c dev/tests/kick_qvm_stubs.c code/qcommon/q_math.c code/qcommon/q_shared.c $client \
 $q/vm.o $q/vm_interpreted.o $q/vm_ptr.o out/tests/kick-cl-input.o out/tests/kick-cl-cgame.o \
 -Wl,--gc-sections -lm -o out/tests/kick-qvm-test
out/tests/kick-qvm-test $q/kick.qvm

# The viewmodel's skeletal pose across the same QVM boundary: the real
# cg_anim.c, built as a shipped cgame.qvm builds it (Q3_VM), hands its
# cg.weaponPose to the engine through the real cg_syscalls.asm,
# vm_interpreted.c, CL_CgameSystemCalls and cl_vm.c; and the bounds check
# refuses a pose running past the QVM's memory.
pasm=""
for f in dev/tests/pose_qvm_cgame.c code/cgame/cg_anim.c code/game/bg_pmove.c code/game/bg_loadout.c code/game/bg_misc.c \
 code/game/bg_slidemove.c code/qcommon/q_math.c code/qcommon/q_shared.c code/game/bg_lib.c; do
 $t/q3lcc $lccdefs -o $q/pose-$(basename "$f" .c).asm "$f"
 pasm="$pasm $q/pose-$(basename "$f" .c).asm"
done
$t/q3asm -o $q/pose.qvm $pasm code/cgame/cg_syscalls.asm
$cc -DQVM_STUBS_POSE -c dev/tests/kick_qvm_stubs.c -o $q/pose-qvm-stubs.o
$cc dev/tests/pose_qvm_test.c dev/tests/qvm_engine.c code/client/cl_vm.c code/qcommon/q_math.c code/qcommon/q_shared.c $client \
 $q/vm.o $q/vm_interpreted.o $q/vm_ptr.o $q/pose-qvm-stubs.o out/tests/kick-cl-input.o out/tests/kick-cl-cgame.o \
 -Wl,--gc-sections -lm -o out/tests/pose-qvm-test
out/tests/pose-qvm-test $q/pose.qvm
