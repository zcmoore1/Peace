param(
    [ValidateSet('opengl1', 'opengl2')][string]$Renderer = 'opengl2',
    [ValidateSet(0, 2)][int]$VmMode = 0,
    [string]$QuakePath = 'C:\Program Files (x86)\Steam\steamapps\common\Quake 3 Arena'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$build = Join-Path $repo 'out/build/x64-Debug/Debug'
$testRoot = Join-Path $repo ("out/build/masterkey-playtest-$Renderer-$VmMode-" + [guid]::NewGuid().ToString('N').Substring(0, 6))
$testGame = Join-Path $testRoot 'baseq3'
New-Item -ItemType Directory -Force -Path $testGame | Out-Null
foreach ($name in @('cgame.dll', 'qagame.dll', 'ui.dll', 'vm', 'models', 'scripts', 'gfx', 'icons', 'sound', 'maps')) {
    Copy-Item -LiteralPath (Join-Path $build "baseq3/$name") -Destination $testGame -Recurse -Force
}
$cfg = @'
set sv_pure 0
set sv_fps 125
set bot_enable 0
set g_gametype 0
set com_maxfps 60
set com_fixedtime 0
set cg_fov 90
set cg_draw2D 1
set cg_weaponDebug 0
set con_notifytime 0
set peace_loadout "2|18|3|12|1|13|2|0|0|0|0|0|M16 Test"
devmap peace_ladder
wait 220
peacedump
screenshotJPEG m16_ready
+ads
wait 35
screenshotJPEG m16_ads
-ads
wait 20
+attack
wait 3
-attack
wait 20
peacedump
weapalt
wait 180
peacedump
screenshotJPEG masterkey_ready
+attack
wait 3
-attack
wait 36
peacedump
screenshotJPEG masterkey_pump
wait 120
+reload
wait 3
-reload
wait 60
peacedump
screenshotJPEG masterkey_reload
wait 200
peacedump
weapalt
wait 180
peacedump
screenshotJPEG m16_return
quit
'@
Set-Content -LiteralPath (Join-Path $testGame 'masterkey_check.cfg') -Value $cfg -Encoding ascii
$gameArgs = @('+set', 'fs_basepath', ('"'+$QuakePath+'"'),
    '+set', 'fs_homepath', ('"'+$testRoot+'"'), '+set', 'net_enabled', '0', '+set', 'logfile', '2',
    '+set', 'vm_game', $VmMode, '+set', 'vm_cgame', $VmMode, '+set', 'vm_ui', $VmMode,
    '+set', 'cl_renderer', $Renderer, '+set', 'r_fullscreen', '0', '+set', 'r_mode', '-1',
    '+set', 'r_customwidth', '1280', '+set', 'r_customheight', '720', '+set', 'r_swapInterval', '0',
    '+set', 's_initsound', '0', '+set', 'in_joystick', '0', '+exec', 'masterkey_check.cfg')
$process = Start-Process -FilePath (Join-Path $build 'ioquake3.exe') -WorkingDirectory $build -ArgumentList $gameArgs -WindowStyle Hidden -PassThru
Write-Output "Masterkey playtest process $($process.Id), output $testGame"
if (-not $process.WaitForExit(55000)) {
    Stop-Process -Id $process.Id
    throw 'Playtest timed out; inspect its isolated qconsole.log.'
}
if ($process.ExitCode -ne 0) { throw "Playtest exited $($process.ExitCode)" }
$log = Get-Content -LiteralPath (Join-Path $testGame 'qconsole.log') -Raw
if ($log -match "Can't register model|RE_RegisterModel:|ERROR:|Sys_Error" -or
    $log -notmatch 'weapon\s+19\s+state BOLTING' -or
    $log -notmatch 'ammo\s+mag 4 / reserve 15' -or
    $log -notmatch 'weapon\s+18\s+state READY') {
    throw 'Mode switch, shot or shell reload check failed; inspect qconsole.log.'
}
$screens = @(Get-ChildItem -LiteralPath (Join-Path $testGame 'screenshots') -Filter '*.jpg')
if ($screens.Count -ne 6) { throw "Expected 6 screenshots, got $($screens.Count)." }
Write-Output "PASS: rifle, underbarrel switch, pump, one-shell reload and return. Screenshots: $testGame/screenshots"
