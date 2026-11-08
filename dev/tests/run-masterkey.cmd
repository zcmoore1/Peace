@echo off
setlocal
rem MW2's underbarrel shotgun (the Masterkey) and the UAV, both weapon numberings.
rem Run from an x64 Native Tools Command Prompt for Visual Studio.
where cl >/dev/null 2>nul
if errorlevel 1 exit /b 1
pushd "%~dp0..\.."
python dev\tools\build_masterkey.py --check
if errorlevel 1 goto fail
if not exist out\build\masterkey-tests mkdir out\build\masterkey-tests
set test_flags=/nologo /TC /W3 /Od /Gy /D_CRT_SECURE_NO_WARNINGS /Foout\build\masterkey-tests\
set test_common=code\game\bg_loadout.c code\game\bg_misc.c code\game\bg_slidemove.c code\qcommon\q_math.c code\qcommon\q_shared.c
cl %test_flags% /Feout\build\masterkey-tests\masterkey_test.exe dev\tests\masterkey_test.c %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\masterkey-tests\masterkey_test.exe
if errorlevel 1 goto fail
cl %test_flags% /DMISSIONPACK /Feout\build\masterkey-tests\masterkey_test_mp.exe dev\tests\masterkey_test.c %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\masterkey-tests\masterkey_test_mp.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\masterkey-tests\killstreak_server_test.exe dev\tests\killstreak_server_test.c code\game\g_killstreak.c code\game\bg_pmove.c %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\masterkey-tests\killstreak_server_test.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\masterkey-tests\radar_test.exe dev\tests\radar_test.c code\qcommon\q_math.c code\qcommon\q_shared.c /link /OPT:REF
if errorlevel 1 goto fail
out\build\masterkey-tests\radar_test.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
