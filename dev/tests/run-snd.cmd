@echo off
setlocal
rem Run from an x64 Native Tools Command Prompt for Visual Studio.
where cl >nul 2>nul
if errorlevel 1 exit /b 1
pushd "%~dp0..\.."
if not exist out\build\snd-tests mkdir out\build\snd-tests
set test_flags=/nologo /TC /W3 /Od /Gy /D_CRT_SECURE_NO_WARNINGS /Foout\build\snd-tests\
set test_common=code\qcommon\q_math.c code\qcommon\q_shared.c
cl %test_flags% /Feout\build\snd-tests\snd_test.exe dev\tests\snd_test.c %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\snd-tests\snd_test.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\snd-tests\snd_hud.exe dev\tests\snd_hud_test.c %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\snd-tests\snd_hud.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\snd-tests\snd_menu.exe dev\tests\snd_menu_test.c %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\snd-tests\snd_menu.exe
if errorlevel 1 goto fail
cl %test_flags% /Feout\build\snd-tests\replay_test.exe dev\tests\replay_test.c %test_common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\snd-tests\replay_test.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
