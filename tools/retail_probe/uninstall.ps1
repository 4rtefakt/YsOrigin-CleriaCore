# Removes the Retail Probe from the game folder and puts back the dinput8.dll that
# was there before (the Archipelago mod). Your answers file and the logs are copied
# next to this script first and left in the game folder too.
param([string]$GameDir = 'D:\SteamLibrary\steamapps\common\Ys Origin')
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (Get-Process yso_win -ErrorAction SilentlyContinue) { throw 'Ys Origin is running - close it first.' }

$dst = Join-Path $GameDir 'dinput8.dll'
$backup = Join-Path $GameDir 'dinput8.dll.before-retail-probe'
$marker = Join-Path $GameDir 'retail_probe.installed'
if (-not (Test-Path $marker)) { throw "The probe is not installed in '$GameDir' (no retail_probe.installed) - nothing was changed." }

foreach ($f in 'retail_answers.txt', 'retail_probe.log') {
    $p = Join-Path $GameDir $f
    if (Test-Path $p) { Copy-Item $p (Join-Path $here $f) -Force; Write-Host "Copied $f next to this script." }
}
$isOurs = (Test-Path $dst) -and ((Get-FileHash $dst -Algorithm SHA256).Hash -eq (Get-Content $marker -TotalCount 1))
if ($isOurs) { Remove-Item $dst } else { Write-Host 'dinput8.dll is not the probe any more - left as it is.' }
if (Test-Path $backup) {
    if (-not (Test-Path $dst)) { Move-Item $backup $dst; Write-Host 'The previous dinput8.dll (Archipelago mod) is back.' }
    else { Write-Host "Kept the backup $backup (dinput8.dll was not replaced)." }
}
foreach ($f in 'dinput8_chain.dll', 'retail_probe_tests.ini', 'retail_probe.installed', 'retail_probe.prev.log') {
    $p = Join-Path $GameDir $f
    if (Test-Path $p) { Remove-Item $p }
}
Write-Host 'Uninstalled.'
