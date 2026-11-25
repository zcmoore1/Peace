@echo off
setlocal
rem Run from the x64 Native Tools Command Prompt. Server item tests use run-masterkey.sh (GNU linker).
where cl >nul 2>nul
if errorlevel 1 exit /b 1
pushd "%~dp0..\.."
if not exist out\build\masterkey-tests mkdir out\build\masterkey-tests
set flags=/nologo /TC /W3 /Od /Gy /D_CRT_SECURE_NO_WARNINGS /Foout\build\masterkey-tests\
set common=code\qcommon\q_math.c code\qcommon\q_shared.c
set game=code\game\bg_loadout.c code\game\bg_misc.c code\game\bg_slidemove.c
cl %flags% /Feout\build\masterkey-tests\masterkey.exe dev\tests\masterkey_test.c %game% %common% code\qcommon\msg.c code\qcommon\huffman.c /link /OPT:REF
if errorlevel 1 goto fail
out\build\masterkey-tests\masterkey.exe
if errorlevel 1 goto fail
cl %flags% /Feout\build\masterkey-tests\masterkey-selection.exe dev\tests\masterkey_selection_test.c code\cgame\cg_playerstate.c dev\tests\pickup_pmove_driver.c %game% %common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\masterkey-tests\masterkey-selection.exe
if errorlevel 1 goto fail
cl %flags% /Feout\build\masterkey-tests\moni.exe dev\tests\moni_test.c code\cgame\cg_playerstate.c dev\tests\pickup_pmove_driver.c %game% %common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\masterkey-tests\moni.exe
if errorlevel 1 goto fail
cl %flags% /Feout\build\masterkey-tests\empty-switch.exe dev\tests\empty_switch_test.c code\cgame\cg_event.c code\cgame\cg_playerstate.c dev\tests\pickup_pmove_driver.c %game% %common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\masterkey-tests\empty-switch.exe
if errorlevel 1 goto fail
cl %flags% /Feout\build\masterkey-tests\trigger.exe dev\tests\trigger_test.c code\cgame\cg_event.c code\cgame\cg_playerstate.c dev\tests\pickup_pmove_driver.c %game% %common% code\qcommon\msg.c code\qcommon\huffman.c /link /OPT:REF
if errorlevel 1 goto fail
out\build\masterkey-tests\trigger.exe
if errorlevel 1 goto fail
cl %flags% /Feout\build\masterkey-tests\burst.exe dev\tests\burst_test.c code\cgame\cg_event.c code\cgame\cg_playerstate.c dev\tests\pickup_pmove_driver.c %game% %common% code\qcommon\msg.c code\qcommon\huffman.c /link /OPT:REF
if errorlevel 1 goto fail
out\build\masterkey-tests\burst.exe
if errorlevel 1 goto fail
cl %flags% /Feout\build\masterkey-tests\sf.exe dev\tests\sf_test.c code\cgame\cg_playerstate.c dev\tests\pickup_pmove_driver.c %game% %common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\masterkey-tests\sf.exe
if errorlevel 1 goto fail
cl %flags% /Feout\build\masterkey-tests\state-end.exe dev\tests\state_end_test.c %game% %common% /link /OPT:REF
if errorlevel 1 goto fail
out\build\masterkey-tests\state-end.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
