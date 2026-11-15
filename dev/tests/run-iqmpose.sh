#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
# The viewmodel's skeletal pose in each renderer's real tr_model_iqm.c, on the
# shipped viewmodels: a pose skins as the frames it was built from, a blend as
# their lerp, a pose for another model is ignored, and GL1's output (every
# skin, pose, blend and blended skin) is GL2's, bit for bit.
# IQMPOSE_TEST_FLAGS can enable ASan/UBSan; add -fno-sanitize=alignment, as
# stock ioquake3's R_LoadIQM packs the joint parents (ints) straight after the
# joint names.
# No .cmd mirror: it includes a renderer's .c whole, and GNU ld's --gc-sections
# drops the functions that reach renderer code it does not build.
cc="${CC:-cc} -std=gnu99 -O0 -g -ffunction-sections -fdata-sections ${IQMPOSE_TEST_FLAGS:-}"
inc="-Icode/qcommon -Icode/thirdparty/SDL2-2.32.8/include"
models="assets/baseq3/models/weapons2/spas12/view.iqm assets/baseq3/models/weapons2/m40a3/view.iqm"
$cc $inc dev/tests/iqm_pose_test.c code/qcommon/q_math.c code/qcommon/q_shared.c -Wl,--gc-sections -lm -o out/tests/iqm-pose-gl1
$cc -DIQM_POSE_GL2 $inc dev/tests/iqm_pose_test.c code/qcommon/q_math.c code/qcommon/q_shared.c -Wl,--gc-sections -lm -o out/tests/iqm-pose-gl2
out/tests/iqm-pose-gl1 $models > out/tests/iqm-pose-gl1.out
out/tests/iqm-pose-gl2 $models > out/tests/iqm-pose-gl2.out
grep -v '^digest ' out/tests/iqm-pose-gl1.out
grep -v '^digest ' out/tests/iqm-pose-gl2.out
d1=$(sed -n 's/^digest //p' out/tests/iqm-pose-gl1.out)
d2=$(sed -n 's/^digest //p' out/tests/iqm-pose-gl2.out)
if [ -z "$d1" ] || [ "$d1" != "$d2" ]; then
 echo "FAIL: renderergl1's pose output ($d1) is not renderergl2's ($d2)" >&2
 exit 1
fi
echo "PASS: renderergl1 and renderergl2 give the same bits: every skin, pose, blend and blended skin (digest $d1)"
