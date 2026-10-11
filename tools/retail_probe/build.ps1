# Builds tools/retail_probe -> dist/dinput8.dll (32-bit, Release). Nothing else is built.
# MinHook and Dear ImGui are taken from the Archipelago mod's already-downloaded
# copies when they are there (read only); otherwise CMake downloads them once.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
# The build tree lives outside the repo (it is ~100 MB of objects).
$build = if ($env:RETAIL_PROBE_BUILD) { $env:RETAIL_PROBE_BUILD } else { Join-Path $env:LOCALAPPDATA 'retail_probe_build' }
$deps = 'C:\Users\kores\repos\YsOrigin-Archipelago\mod\build\_deps'
$cfg = @('-A', 'Win32', '-S', $here, '-B', $build)
if (Test-Path "$deps\imgui-src\imgui.cpp") { $cfg += "-DFETCHCONTENT_SOURCE_DIR_IMGUI=$deps\imgui-src" }
if (Test-Path "$deps\minhook-src\src\hook.c") { $cfg += "-DFETCHCONTENT_SOURCE_DIR_MINHOOK=$deps\minhook-src" }
cmake @cfg
if ($LASTEXITCODE) { throw 'cmake configure failed' }
cmake --build $build --config Release -- /m:2 /v:m
if ($LASTEXITCODE) { throw 'build failed' }
New-Item -ItemType Directory -Force "$here\dist" | Out-Null
Copy-Item "$build\Release\dinput8.dll" "$here\dist\dinput8.dll" -Force
Copy-Item "$here\retail_probe_tests.ini" "$here\dist\retail_probe_tests.ini" -Force
Get-Item "$here\dist\dinput8.dll" | Select-Object Name, Length, LastWriteTime
# Check the test list with the DLL's own parser (and the rooms against the game's
# scene list when the CleriaCore assets are there).
$sl = 'C:\Users\kores\repos\YsOrigin-CleriaCore\assets\MAP\SCENELIST.SL'
if (Test-Path $sl) { & "$build\Release\probe_selftest.exe" "$here\retail_probe_tests.ini" $sl }
else { & "$build\Release\probe_selftest.exe" "$here\retail_probe_tests.ini" }
if ($LASTEXITCODE) { throw 'retail_probe_tests.ini has problems (see above)' }
# Offline simulation of the game-side logic (no game involved; see src/simtest.cpp).
& "$build\Release\probe_simtest.exe" "$build\Release\dinput8.dll" "$here\retail_probe_tests.ini"
if ($LASTEXITCODE -eq 3) { Write-Host 'simulation skipped (address range not free in this process)' }
elseif ($LASTEXITCODE) { throw 'the offline simulation failed (see above)' }
