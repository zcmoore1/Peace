@echo off
setlocal
rem Run from an x64 Native Tools Command Prompt for Visual Studio.
rem MW2's stance button and prone through the real Pmove (dev/STANCE.md). The
rem server half (stance_server_test.c) relies on GNU ld's --gc-sections: run
rem run-stance.sh under WSL or with any gcc or clang for it.
where cl >nul 2>nul
if errorlevel 1 (
    echo Run this test from an x64 Native Tools Command Prompt for Visual Studio.
    exit /b 1
)
pushd "%~dp0..\.."
if not exist out\build\stance-tests mkdir out\build\stance-tests
cl /nologo /TC /W3 /Od /Gy /D_CRT_SECURE_NO_WARNINGS %STANCE_TEST_DEFINES% /Foout\build\stance-tests\ /Feout\build\stance-tests\stance_test.exe dev\tests\stance_test.c code\game\bg_loadout.c code\game\bg_misc.c code\game\bg_slidemove.c code\qcommon\q_math.c code\qcommon\q_shared.c /link /OPT:REF
if errorlevel 1 (
    popd
    exit /b 1
)
out\build\stance-tests\stance_test.exe
set test_result=%errorlevel%
popd
exit /b %test_result%
