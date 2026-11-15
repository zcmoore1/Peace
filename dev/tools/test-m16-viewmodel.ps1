param(
    [ValidateSet('opengl1', 'opengl2')][string]$Renderer = 'opengl2',
    [ValidateSet(0, 2)][int]$VmMode = 0,
    [ValidateSet('view', 'masterkey')][string]$Model = 'view',
    [string]$QuakePath = 'C:\Program Files (x86)\Steam\steamapps\common\Quake 3 Arena'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$build = Join-Path $repo 'out/build/x64-Debug/Debug'
$testRoot = Join-Path $repo ("out/build/m16-preview-$Renderer-$VmMode-" + [guid]::NewGuid().ToString('N').Substring(0, 6))
$testGame = Join-Path $testRoot 'baseq3'
New-Item -ItemType Directory -Force -Path $testGame | Out-Null
foreach ($name in @('cgame.dll', 'qagame.dll', 'ui.dll', 'vm', 'models', 'scripts', 'gfx', 'icons', 'sound', 'maps')) {
    Copy-Item -LiteralPath (Join-Path $build "baseq3/$name") -Destination $testGame -Recurse -Force
}
# Model QA can use the last built engine/modules. Overlay only this weapon's
# latest source assets inside the isolated homepath, leaving the live game alone.
Copy-Item -LiteralPath (Join-Path $repo 'assets/baseq3/models/weapons2/m16') -Destination (Join-Path $testGame 'models/weapons2') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $repo 'assets/baseq3/scripts/peace_m16.shader') -Destination (Join-Path $testGame 'scripts') -Force
Copy-Item -LiteralPath (Join-Path $repo 'assets/baseq3/icons/peace_m16.tga') -Destination (Join-Path $testGame 'icons') -Force
$clips = @{}
foreach ($line in Get-Content -LiteralPath (Join-Path $testGame "models/weapons2/m16/$Model.cfg")) {
    $fields = ($line -split '//')[0].Trim() -split '\s+'
    if ($fields.Count -eq 5) { $clips[$fields[0]] = @([int]$fields[1], [int]$fields[2]) }
}
$cfg = [System.Collections.Generic.List[string]]::new()
@'
set sv_pure 0
set bot_enable 0
set g_gametype 0
set com_maxfps 125
set com_fixedtime 8
set cg_fov 90
set cg_draw2D 0
set cg_gun_x 0
set cg_gun_y 0
set cg_gun_z 0
set con_notifytime 0
devmap peace_ladder
wait 700
'@ -split "`r?`n" | ForEach-Object { $cfg.Add($_) }
foreach ($clip in @('idle', 'fire', 'reload_loop', 'sprint_loop')) {
    $cfg.Add("testgun models/weapons2/m16/$Model.iqm")
    $frame = $clips[$clip][0]
    if ($clip -ne 'idle') { $frame += [int]($clips[$clip][1] / 2) }
    for ($i = 0; $i -lt $frame; $i++) { $cfg.Add('nextframe') }
    $cfg.Add('wait 8'); $cfg.Add("screenshotJPEG m16_$clip"); $cfg.Add('wait 2')
}
@'
testmodel
quit
'@ -split "`r?`n" | ForEach-Object { $cfg.Add($_) }
Set-Content -LiteralPath (Join-Path $testGame 'm16_check.cfg') -Value $cfg -Encoding ascii
$gameArgs = @('+set', 'fs_basepath', ('"'+$QuakePath+'"'),
    '+set', 'fs_homepath', ('"'+$testRoot+'"'), '+set', 'net_enabled', '0', '+set', 'logfile', '2',
    '+set', 'vm_game', $VmMode, '+set', 'vm_cgame', $VmMode, '+set', 'vm_ui', $VmMode,
    '+set', 'cl_renderer', $Renderer, '+set', 'r_fullscreen', '0', '+set', 'r_mode', '-1',
    '+set', 'r_customwidth', '1280', '+set', 'r_customheight', '720', '+set', 'r_swapInterval', '0',
    '+set', 's_initsound', '0', '+set', 'in_joystick', '0', '+exec', 'm16_check.cfg')
$process = Start-Process -FilePath (Join-Path $build 'ioquake3.exe') -WorkingDirectory $build -ArgumentList $gameArgs -WindowStyle Hidden -PassThru
Write-Output "M16 preview process $($process.Id), output $testGame"
if (-not $process.WaitForExit(55000)) {
    Stop-Process -Id $process.Id
    throw 'Preview timed out; inspect its isolated qconsole.log.'
}
if ($process.ExitCode -ne 0) { throw "Preview exited $($process.ExitCode)" }
$log = Get-Content -LiteralPath (Join-Path $testGame 'qconsole.log') -Raw
if ($log -match "Can't register model|RE_RegisterModel:|ERROR:|Sys_Error") {
    throw 'Model registration failed; inspect qconsole.log.'
}
$screens = @(Get-ChildItem -LiteralPath (Join-Path $testGame 'screenshots') -Filter 'm16_*.jpg')
if ($screens.Count -ne 4) { throw "Expected 4 screenshots, got $($screens.Count)." }
Write-Output "Captured 4 viewmodel screenshots in $testGame/screenshots"
