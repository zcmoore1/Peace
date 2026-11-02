@echo off
setlocal
rem Run from an x64 Native Tools Command Prompt for Visual Studio.
where cl >nul 2>nul
if errorlevel 1 exit /b 1
pushd "%~dp0..\.."
if not exist out\build\gamepad-aim-tests mkdir out\build\gamepad-aim-tests
set test_flags=/nologo /TC /W3 /O1 /D_CRT_SECURE_NO_WARNINGS /Foout\build\gamepad-aim-tests\
cl %test_flags% /Feout\build\gamepad-aim-tests\gamepad-aim.exe dev\tests\gamepad_aim_test.c code\client\cl_gamepadaim.c
if errorlevel 1 goto fail
out\build\gamepad-aim-tests\gamepad-aim.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
