@echo off
setlocal
rem Run from an x64 Native Tools Command Prompt for Visual Studio.
where cl >nul 2>nul
if errorlevel 1 exit /b 1
pushd "%~dp0..\.."
if not exist out\build\fake-reload-tests mkdir out\build\fake-reload-tests
set test_flags=/nologo /TC /W3 /O1 /Gy /D_CRT_SECURE_NO_WARNINGS /Foout\build\fake-reload-tests\
cl %test_flags% /Feout\build\fake-reload-tests\fake-reload.exe dev\tests\fake_reload_test.c dev\tests\fake_reload_pmove.c dev\tests\fake_reload_picture.c code\cgame\cg_weappicture.c code\game\bg_loadout.c code\game\bg_misc.c code\game\bg_slidemove.c code\qcommon\q_math.c code\qcommon\q_shared.c /link /OPT:REF
if errorlevel 1 goto fail
out\build\fake-reload-tests\fake-reload.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
