# Installs what YK Engine needs on Windows (Visual Studio Build Tools with the C++ workload, CMake,
# Ninja), builds the dev preset and optionally starts the editor.
#
#   scripts\setup-windows.bat                 (double-click it, or run it from any terminal)
#   scripts\setup-windows.ps1 -NoBuild        only install the tools
#   scripts\setup-windows.ps1 -NoLaunch       install and build, but do not open the editor
#
# Already-installed tools are detected and skipped. The Visual Studio installer asks for
# administrator rights (a UAC prompt) and downloads several GB, so the first run takes a while.
param(
    [switch]$NoBuild,
    [switch]$NoLaunch
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Step($text) { Write-Host "`n==> $text" -ForegroundColor Cyan }

function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user"
}

function Install-WithWinget($id, $label, $override) {
    Step "Installing $label"
    $arguments = @('install', '--id', $id, '-e', '--accept-package-agreements', '--accept-source-agreements')
    if ($override) { $arguments += @('--override', $override) }
    & winget @arguments
    # 0x8A15002B: already installed, nothing newer to apply.
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) {
        throw "winget could not install $label (exit code $LASTEXITCODE)."
    }
    Update-SessionPath
}

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $path = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($path) { return $path | Select-Object -First 1 }
    return $null
}

if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw 'winget was not found. Install "App Installer" from the Microsoft Store, then run this again.'
}

Step 'Checking tools'
$vs = Find-VisualStudio
$haveCMake = [bool](Get-Command cmake -ErrorAction SilentlyContinue)
$haveNinja = [bool](Get-Command ninja -ErrorAction SilentlyContinue)
Write-Host ("C++ compiler (Visual Studio Build Tools): " + $(if ($vs) { $vs } else { 'missing' }))
Write-Host ("CMake: " + $(if ($haveCMake) { 'found' } else { 'missing' }))
Write-Host ("Ninja: " + $(if ($haveNinja) { 'found' } else { 'missing' }))

if (-not $vs) {
    Install-WithWinget 'Microsoft.VisualStudio.2022.BuildTools' 'Visual Studio Build Tools (C++ workload)' `
        '--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
    $vs = Find-VisualStudio
    if (-not $vs) { throw 'Visual Studio Build Tools did not install the C++ tools. Re-run this script.' }
}
if (-not $haveCMake) { Install-WithWinget 'Kitware.CMake' 'CMake' }
if (-not $haveNinja) { Install-WithWinget 'Ninja-build.Ninja' 'Ninja' }

foreach ($tool in 'cmake', 'ninja') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool was installed but is not on PATH yet. Open a new terminal and run this script again."
    }
}

if ($NoBuild) {
    Write-Host "`nTools are installed. Build later with: scripts\setup-windows.bat" -ForegroundColor Green
    return
}

Step 'Loading the Visual Studio x64 compiler environment'
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "Missing $vcvars" }
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
}

Set-Location $root
Step 'Configuring (the first run downloads Box2D, SDL3 and Dear ImGui)'
cmake --preset dev
if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed.' }

Step 'Building'
cmake --build --preset dev
if ($LASTEXITCODE -ne 0) { throw 'cmake build failed.' }

$editor = Join-Path $root 'build\dev\yk_editor.exe'
if (-not (Test-Path $editor)) { throw "Build finished but $editor was not found." }
Write-Host "`nDone. Editor: $editor" -ForegroundColor Green
Write-Host 'Player: build\dev\yk_player.exe YK-DemoGame'

if (-not $NoLaunch) {
    Step 'Starting the editor'
    Start-Process -FilePath $editor -WorkingDirectory $root
}
