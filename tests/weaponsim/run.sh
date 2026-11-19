#!/bin/sh
# Builds the offline weapon simulator and runs its self test.
#
#   tests/weaponsim/run.sh                 build and self test
#   tests/weaponsim/run.sh summary yy      any other weaponsim command line
#
# The simulator is built from the real shared movement code and the real view
# animator, so it only needs a C compiler. It does not need game data.
set -e

here=$(cd "$(dirname "$0")" && pwd)
code="$here/../../code"
out="${WEAPONSIM_OUT:-$here/../../build/weaponsim}"

mkdir -p "$(dirname "$out")"

${CC:-cc} -std=gnu99 -O1 -g -Wall -DCGAME \
	-o "$out" \
	"$here/weaponsim.c" \
	"$code/game/bg_pmove.c" \
	"$code/game/bg_slidemove.c" \
	"$code/game/bg_misc.c" \
	"$code/game/bg_weapons.c" \
	"$code/cgame/cg_viewanim.c" \
	"$code/qcommon/q_math.c" \
	"$code/qcommon/q_shared.c" \
	-lm

if [ $# -eq 0 ]; then
	exec "$out" selftest
else
	exec "$out" "$@"
fi
