@echo off
setlocal
rem Search & Destroy round logic and the generated arena.
rem Run from an x64 Native Tools Command Prompt for Visual Studio.
where cl >nul 2>nul
if errorlevel 1 (
    echo Run this test from an x64 Native Tools Command Prompt for Visual Studio.
    exit /b 1
)
pushd "%~dp0..\.."
if not exist out\build\sd-tests mkdir out\build\sd-tests
cl /nologo /TC /W3 /Od /Gy /D_CRT_SECURE_NO_WARNINGS /Icode\game /Icode\qcommon /Foout\build\sd-tests\ /Feout\build\sd-tests\sd_round_test.exe dev\tests\sd_round_test.c code\qcommon\q_math.c code\qcommon\q_shared.c /link /OPT:REF
if errorlevel 1 goto fail
out\build\sd-tests\sd_round_test.exe
if errorlevel 1 goto fail
cl /nologo /TC /W3 /Od /Gy /D_CRT_SECURE_NO_WARNINGS /Foout\build\sd-tests\ /Feout\build\sd-tests\sd_map_test.exe dev\tests\sd_map_test.c code\game\bg_loadout.c code\game\bg_misc.c code\game\bg_slidemove.c code\qcommon\cm_load.c code\qcommon\cm_trace.c code\qcommon\cm_test.c code\qcommon\cm_patch.c code\qcommon\cm_polylib.c code\qcommon\md4.c code\qcommon\q_math.c code\qcommon\q_shared.c /link /OPT:REF
if errorlevel 1 goto fail
out\build\sd-tests\sd_map_test.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
