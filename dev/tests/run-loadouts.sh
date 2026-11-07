#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p out/tests
# LOADOUT_TEST_FLAGS can enable ASan/UBSan; normal CI uses the real native code.
common="code/qcommon/q_math.c code/qcommon/q_shared.c"
game="code/game/bg_loadout.c code/game/bg_misc.c code/game/bg_slidemove.c"
flags="-std=c99 -O0 -g -ffunction-sections -fdata-sections ${LOADOUT_TEST_FLAGS:-}"
${CC:-cc} $flags dev/tests/loadout_test.c $game $common -Wl,--gc-sections -lm -o out/tests/loadout-test
out/tests/loadout-test
${CC:-cc} $flags dev/tests/semtex_test.c $game code/qcommon/msg.c code/qcommon/huffman.c $common -Wl,--gc-sections -lm -o out/tests/semtex-test
out/tests/semtex-test
# Projectile boundary harness is basegame; both complete modules also build in CI.
${CC:-cc} $flags -UMISSIONPACK dev/tests/semtex_missile_test.c code/game/bg_misc.c code/game/bg_loadout.c $common -Wl,--gc-sections -lm -o out/tests/semtex-missile-test
out/tests/semtex-missile-test
${CC:-cc} $flags -UMISSIONPACK dev/tests/knife_missile_test.c code/game/bg_misc.c code/game/bg_loadout.c $common -Wl,--gc-sections -lm -o out/tests/knife-missile-test
out/tests/knife-missile-test
# A knife stuck in a player, through the real g_client.c body queue, g_utils.c and g_items.c.
${CC:-cc} $flags -UMISSIONPACK dev/tests/knife_corpse_test.c code/game/g_missile.c code/game/g_client.c code/game/g_utils.c code/game/g_items.c code/game/g_insertion.c code/game/bg_pmove.c $game $common -Wl,--gc-sections -lm -o out/tests/knife-corpse-test
out/tests/knife-corpse-test
# A body and a podium model start with no ring of their own (CopyToBodyQue,
# SpawnModelOnVictoryPad), or clients would replay the player's last shots.
${CC:-cc} $flags dev/tests/body_ring_test.c code/game/g_client.c code/game/g_utils.c $game $common -Wl,--gc-sections -lm -o out/tests/body-ring-test
out/tests/body-ring-test
# MW2's offhands through the real server: ClientThink, ClientEvents, FireWeapon,
# the missiles, the cook-out's suicide and the death drop (g_active.c, g_combat.c).
${CC:-cc} $flags dev/tests/offhand_server_test.c code/game/g_active.c code/game/g_combat.c code/game/g_weapon.c code/game/g_missile.c code/game/g_client.c code/game/g_utils.c code/game/g_items.c code/game/g_insertion.c code/game/g_killstreak.c code/game/g_cmds.c code/game/bg_pmove.c $game $common -Wl,--gc-sections -lm -o out/tests/offhand-server-test
out/tests/offhand-server-test
${CC:-cc} $flags dev/tests/class_menu_test.c code/q3_ui/ui_atoms.c code/q3_ui/ui_qmenu.c code/q3_ui/ui_mfield.c code/game/bg_loadout.c $common -Wl,--gc-sections -lm -o out/tests/class-menu-test
out/tests/class-menu-test
${CC:-cc} $flags dev/tests/action_wire_test.c code/qcommon/msg.c code/qcommon/huffman.c $common -Wl,--gc-sections -lm -o out/tests/action-wire-test
out/tests/action-wire-test
# MW2's event rings end to end: the real Pmove and BG_PlayerStateToEntityState,
# the codec, and the cgame's snapshot transition and event playback.
${CC:-cc} $flags dev/tests/event_ring_test.c dev/tests/event_ring_wire.c dev/tests/fake_reload_pmove.c code/cgame/cg_event.c code/cgame/cg_kick.c code/cgame/cg_weapons.c code/cgame/cg_playerstate.c code/qcommon/msg.c code/qcommon/huffman.c $game $common -Wl,--gc-sections -lm -o out/tests/event-ring-test
out/tests/event-ring-test
${CC:-cc} $flags dev/tests/weapon_pose_test.c dev/tests/pickup_pmove_driver.c $game $common -Wl,--gc-sections -lm -o out/tests/weapon-pose-test
out/tests/weapon-pose-test
${CC:-cc} $flags dev/tests/pose_vm_test.c code/client/cl_vm.c code/qcommon/vm_ptr.c -Wl,--gc-sections -lm -o out/tests/pose-vm-test
out/tests/pose-vm-test
# Missiles as cgame draws them: the real CG_Missile, lerp and CG_RegisterWeapon.
${CC:-cc} $flags dev/tests/missile_draw_test.c code/cgame/cg_weapons.c code/game/bg_misc.c $common -Wl,--gc-sections -lm -o out/tests/missile-draw-test
out/tests/missile-draw-test
${CC:-cc} $flags dev/tests/equipment_view_test.c code/cgame/cg_anim.c dev/tests/pickup_pmove_driver.c $game $common -Wl,--gc-sections -lm -o out/tests/equipment-view-test
out/tests/equipment-view-test
