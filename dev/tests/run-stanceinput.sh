#!/bin/sh
# MW2's stance button through the real client: the real cl_input.c commands
# and command builder, cmd.c's buffer, cvar.c's cl_stanceHoldTime and
# cl_keys.c's key path, frame by frame at 8, 16 and 33 ms (dev/STANCE.md).
# STANCEINPUT_TEST_FLAGS can enable ASan/UBSan or -DMISSIONPACK.
# No .cmd mirror: this links whole client files, and GNU ld's --gc-sections
# drops the functions that reach engine code it does not build. MSVC's linker
# resolves every external of every object first. Run it under WSL or with any
# gcc or clang.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
client="-Icode/qcommon -Icode/client -Icode/thirdparty/SDL2-2.32.8/include"
${CC:-cc} -std=gnu99 -O0 -g -ffunction-sections -fdata-sections ${STANCEINPUT_TEST_FLAGS:-} \
 dev/tests/stance_input_test.c code/client/cl_input.c code/client/cl_keys.c \
 code/qcommon/cmd.c code/qcommon/cvar.c code/qcommon/q_shared.c code/qcommon/q_math.c $client \
 -Wl,--gc-sections -lm -o out/tests/stance-input-test
out/tests/stance-input-test
