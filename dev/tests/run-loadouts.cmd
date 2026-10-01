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
cl %test_flags% /Feout\build\loadout-tests\menu.exe dev\tests\class_menu_test.c code\game\bg_loadout.c %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\loadout-tests\menu.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\loadout-tests\wire.exe dev\tests\action_wire_test.c code\qcommon\msg.c code\qcommon\huffman.c %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\loadout-tests\wire.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\loadout-tests\pose.exe dev\tests\weapon_pose_test.c code\game\bg_pmove.c %test_game% %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\loadout-tests\pose.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\loadout-tests\pose-vm.exe dev\tests\pose_vm_test.c code\qcommon\vm.c %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\loadout-tests\pose-vm.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
