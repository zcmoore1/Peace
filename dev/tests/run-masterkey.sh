#!/bin/sh
# MW2's underbarrel shotgun (the Masterkey) and the UAV trigger through the
# real PM_Weapon, in both weapon numberings, plus the generated data's check.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
common="code/qcommon/q_math.c code/qcommon/q_shared.c"
game="code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c"
flags="-std=c99 -O0 -g -ffunction-sections -fdata-sections ${MASTERKEY_TEST_FLAGS:-}"
python3 dev/tools/build_masterkey.py --check
${CC:-cc} $flags -UMISSIONPACK dev/tests/masterkey_test.c $game $common -Wl,--gc-sections -lm -o out/tests/masterkey-test
out/tests/masterkey-test
${CC:-cc} $flags -DMISSIONPACK dev/tests/masterkey_test.c $game $common -Wl,--gc-sections -lm -o out/tests/masterkey-test-mp
out/tests/masterkey-test-mp
# The UAV on the server: the real g_killstreak.c (earning, keeping, sweeps).
${CC:-cc} $flags dev/tests/killstreak_server_test.c code/game/g_killstreak.c code/game/bg_pmove.c $game $common -Wl,--gc-sections -lm -o out/tests/killstreak-server-test
out/tests/killstreak-server-test
# The minimap: the real cg_radar.c (the UAV's pings and MW2's sound pings).
${CC:-cc} $flags dev/tests/radar_test.c $common -Wl,--gc-sections -lm -o out/tests/radar-test
out/tests/radar-test
${CC:-cc} $flags -DMISSIONPACK dev/tests/radar_test.c $common -Wl,--gc-sections -lm -o out/tests/radar-test-mp
out/tests/radar-test-mp
