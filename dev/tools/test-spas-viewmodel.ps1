param(
    [ValidateSet('opengl1', 'opengl2')][string]$Renderer = 'opengl2',
    [ValidateSet(0, 2)][int]$VmMode = 0,
    [string]$QuakePath = 'C:\Program Files (x86)\Steam\steamapps\common\Quake 3 Arena'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$build = Join-Path $repo 'out/build/x64-Debug/Debug'
$testRoot = Join-Path $repo "out/build/spas-playtest-$Renderer-$VmMode"
$testGame = Join-Path $testRoot 'baseq3'
if (-not (Test-Path -LiteralPath (Join-Path $QuakePath 'baseq3/pak0.pk3'))) {
    throw 'Set -QuakePath to an existing Quake 3 installation.'
}
New-Item -ItemType Directory -Force -Path $testGame | Out-Null
foreach ($name in @('cgame.dll', 'qagame.dll', 'ui.dll', 'models', 'vm')) {
    Copy-Item -LiteralPath (Join-Path $build "baseq3/$name") -Destination $testGame -Recurse -Force
}
$config = @'
set sv_pure 0
set sv_fps 125
set bot_enable 0
set g_gametype 0
set com_maxfps 60
set com_fixedtime 16
set cg_drawGun 1
set cg_fov 90
set cg_gun_x 0
set cg_gun_y 0
set cg_gun_z 0
set cg_weaponDebug 1
set cg_drawTimer 0
set cg_drawFPS 0
set cg_drawCrosshair 4
set cg_crosshairHealth 0
set cg_drawStatus 0
set cg_draw2D 0
set con_notifytime 0
set cl_noprint 0
devmap q3dm1
wait 150
weapon 3
wait 90
peacedump
screenshotJPEG spas_idle
wait 2
+attack
wait 3
screenshotJPEG spas_fire
wait 2
-attack
wait 90
+reload
wait 6
-reload
wait 13
peacedump
screenshotJPEG spas_reload_start
wait 21
peacedump
screenshotJPEG spas_reload_insert
wait 23
peacedump
screenshotJPEG spas_reload_end
wait 60
peacedump
quit
'@
Set-Content -LiteralPath (Join-Path $testGame 'spas_preview.cfg') -Value $config -Encoding ascii
$argsList = @(
    '+set', 'fs_basepath', ('"' + $QuakePath + '"'),
    '+set', 'fs_homepath', ('"' + $testRoot + '"'),
    '+set', 'net_enabled', '0', '+set', 'logfile', '2',
    '+set', 'vm_game', $VmMode, '+set', 'vm_cgame', $VmMode, '+set', 'vm_ui', $VmMode,
    '+set', 'cl_renderer', $Renderer, '+set', 'r_fullscreen', '0',
    '+set', 'r_mode', '-1', '+set', 'r_customwidth', '1280', '+set', 'r_customheight', '720',
    '+set', 'r_swapInterval', '0', '+set', 's_initsound', '0',
    '+set', 'in_joystick', '0', '+exec', 'spas_preview.cfg'
)
$process = Start-Process -FilePath (Join-Path $build 'ioquake3.exe') -WorkingDirectory $build `
    -ArgumentList $argsList -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(45000)) {
    Stop-Process -Id $process.Id
    throw "Test timed out; inspect $testGame/qconsole.log"
}
if ($process.ExitCode -ne 0) { throw "Game exited with code $($process.ExitCode)" }
$screens = @(Get-ChildItem -LiteralPath (Join-Path $testGame 'screenshots') -Filter 'spas_*.jpg')
if ($screens.Count -ne 5) { throw "Expected 5 screenshots, found $($screens.Count)" }
$log = Get-Content -LiteralPath (Join-Path $testGame 'qconsole.log') -Raw
if ($log -notmatch 'state RELOADING' -or $log -notmatch 'mag 8 / reserve 39') {
    throw 'The input script did not complete a one-shell reload; inspect qconsole.log.'
}
Write-Output "Captured $($screens.Count) screenshots in $testGame/screenshots"
