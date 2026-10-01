@echo off
setlocal
rem Run from an x64 Native Tools Command Prompt for Visual Studio.
where cl >nul 2>nul
if errorlevel 1 (
    echo Run this test from an x64 Native Tools Command Prompt for Visual Studio.
    exit /b 1
)
pushd "%~dp0..\.."
if not exist out\build\ladder-tests mkdir out\build\ladder-tests
cl /nologo /TC /W3 /Od /Gy /D_CRT_SECURE_NO_WARNINGS /Foout\build\ladder-tests\ /Feout\build\ladder-tests\ladder_test.exe dev\tests\ladder_test.c code\game\bg_loadout.c code\game\bg_misc.c code\game\bg_slidemove.c code\qcommon\cm_load.c code\qcommon\cm_trace.c code\qcommon\cm_test.c code\qcommon\cm_patch.c code\qcommon\cm_polylib.c code\qcommon\md4.c code\qcommon\q_math.c code\qcommon\q_shared.c /link /OPT:REF
if errorlevel 1 (
    popd
    exit /b 1
)
out\build\ladder-tests\ladder_test.exe
set test_result=%errorlevel%
popd
exit /b %test_result%
