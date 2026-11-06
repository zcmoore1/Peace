@echo off
setlocal
rem Run from an x64 Native Tools Command Prompt for Visual Studio.
where cl >nul 2>nul
if errorlevel 1 exit /b 1
pushd "%~dp0..\.."
if not exist out\build\loadout-tests mkdir out\build\loadout-tests
set test_flags=/nologo /TC /W3 /Od /Gy /D_CRT_SECURE_NO_WARNINGS /Foout\build\loadout-tests\
set test_common=code\qcommon\q_math.c code\qcommon\q_shared.c
set test_game=code\game\bg_loadout.c code\game\bg_misc.c code\game\bg_slidemove.c
cl %test_flags% /Feout\build\loadout-tests\loadout.exe dev\tests\loadout_test.c %test_game% %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\loadout-tests\loadout.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\loadout-tests\semtex.exe dev\tests\semtex_test.c %test_game% code\qcommon\msg.c code\qcommon\huffman.c %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\loadout-tests\semtex.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\loadout-tests\missile.exe dev\tests\semtex_missile_test.c code\game\bg_misc.c code\game\bg_loadout.c %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\loadout-tests\missile.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\loadout-tests\knife-missile.exe dev\tests\knife_missile_test.c code\game\bg_misc.c code\game\bg_loadout.c %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\loadout-tests\knife-missile.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\loadout-tests\menu.exe dev\tests\class_menu_test.c code\q3_ui\ui_atoms.c code\q3_ui\ui_qmenu.c code\q3_ui\ui_mfield.c code\game\bg_loadout.c %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\loadout-tests\menu.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\loadout-tests\wire.exe dev\tests\action_wire_test.c code\qcommon\msg.c code\qcommon\huffman.c %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\loadout-tests\wire.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\loadout-tests\event-ring.exe dev\tests\event_ring_test.c dev\tests\event_ring_wire.c dev\tests\fake_reload_pmove.c code\cgame\cg_event.c code\cgame\cg_kick.c code\cgame\cg_weapons.c code\cgame\cg_playerstate.c code\qcommon\msg.c code\qcommon\huffman.c %test_game% %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\loadout-tests\event-ring.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\loadout-tests\pose.exe dev\tests\weapon_pose_test.c dev\tests\pickup_pmove_driver.c %test_game% %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\loadout-tests\pose.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\loadout-tests\pose-vm.exe dev\tests\pose_vm_test.c code\client\cl_vm.c code\qcommon\vm_ptr.c /link /OPT:REF
if errorlevel 1 goto fail
out\build\loadout-tests\pose-vm.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\loadout-tests\missile-draw.exe dev\tests\missile_draw_test.c code\cgame\cg_weapons.c code\game\bg_misc.c %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\loadout-tests\missile-draw.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\loadout-tests\equipment-view.exe dev\tests\equipment_view_test.c code\cgame\cg_anim.c dev\tests\pickup_pmove_driver.c %test_game% %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\loadout-tests\equipment-view.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
