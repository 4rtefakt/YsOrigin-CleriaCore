# Installs the Retail Probe into the Steam copy of Ys Origin.
#
#   .\install.ps1            the probe REPLACES the Archipelago mod (kept as a backup,
#                            put back by uninstall.ps1) - the two are both dinput8.dll
#   .\install.ps1 -KeepAP    experimental, never tried: keep the Archipelago mod loaded
#                            behind the probe (as dinput8_chain.dll)
#   .\install.ps1 -GameDir 'X:\...\Ys Origin'
#
# Only these files are written in the game folder: dinput8.dll, retail_probe_tests.ini,
# retail_probe.installed, dinput8.dll.before-retail-probe (the backup) and, with
# -KeepAP, dinput8_chain.dll. Close the game first.
param(
    [string]$GameDir = 'D:\SteamLibrary\steamapps\common\Ys Origin',
    [switch]$KeepAP
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $here 'dist\dinput8.dll'
if (-not (Test-Path $src)) { throw "dist\dinput8.dll is missing - run build.ps1 first." }
if (-not (Test-Path (Join-Path $GameDir 'yso_win.exe'))) { throw "yso_win.exe not found in '$GameDir' (use -GameDir)." }
if (Get-Process yso_win -ErrorAction SilentlyContinue) { throw 'Ys Origin is running - close it first.' }

$dst = Join-Path $GameDir 'dinput8.dll'
$backup = Join-Path $GameDir 'dinput8.dll.before-retail-probe'
$marker = Join-Path $GameDir 'retail_probe.installed'
$chain = Join-Path $GameDir 'dinput8_chain.dll'
$newHash = (Get-FileHash $src -Algorithm SHA256).Hash

# Is the dinput8.dll in the game folder a probe we installed earlier?
$oursInstalled = $false
if ((Test-Path $dst) -and (Test-Path $marker)) {
    $oursInstalled = ((Get-FileHash $dst -Algorithm SHA256).Hash -eq (Get-Content $marker -TotalCount 1))
}
if ((Test-Path $dst) -and -not $oursInstalled) {
    if (Test-Path $backup) { throw "A backup already exists ($backup) but dinput8.dll is not the probe. Sort that out by hand first - nothing was changed." }
    Copy-Item $dst $backup
    Write-Host "Backed up the existing dinput8.dll (the Archipelago mod) -> dinput8.dll.before-retail-probe"
}
if ($KeepAP) {
    if (-not (Test-Path $backup)) { throw '-KeepAP: there is no other dinput8.dll to keep.' }
    Copy-Item $backup $chain -Force
    Write-Host 'The Archipelago mod stays loaded behind the probe (dinput8_chain.dll). EXPERIMENTAL.'
} elseif (Test-Path $chain) {
    Remove-Item $chain
}
Copy-Item $src $dst -Force
Copy-Item (Join-Path $here 'retail_probe_tests.ini') (Join-Path $GameDir 'retail_probe_tests.ini') -Force
Set-Content -Path $marker -Value $newHash -Encoding ascii
Write-Host "Installed. Start Ys Origin from Steam, load a save, press HOME for the panel."
Write-Host "Answers: $GameDir\retail_answers.txt    Log: $GameDir\retail_probe.log"
Write-Host "Remove with uninstall.ps1 (it puts the Archipelago mod back)."
