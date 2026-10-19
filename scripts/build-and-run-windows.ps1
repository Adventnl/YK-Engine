$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build\windows-run'
$editor = Join-Path $build 'Release\yk_editor.exe'

Set-Location $root
Write-Host 'Configuring the current engine source...'
& cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 -DBUILD_TESTING=OFF
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed. Install Visual Studio 2022 with the C++ workload and CMake.' }

Write-Host 'Building the editor...'
& cmake --build $build --config Release --target yk_editor
if ($LASTEXITCODE -ne 0) { throw 'Engine build failed.' }

Write-Host 'Launching YK Engine...'
Start-Process -FilePath $editor -WorkingDirectory $root
