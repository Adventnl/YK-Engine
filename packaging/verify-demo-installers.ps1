param([Parameter(Mandatory=$true)][string]$Dist)
$ErrorActionPreference = 'Stop'
foreach ($game in @('InvisibleCastle', 'BrothersRun')) {
    $installer = @(Get-ChildItem "$Dist/$game-Demo-*-windows-x64.exe")
    if ($installer.Count -ne 1) { throw "Expected one $game demo installer" }
    $destination = Join-Path $env:RUNNER_TEMP "$game Demo Smoke"
    $process = Start-Process -FilePath $installer[0].FullName -ArgumentList @('/S', "/D=$destination") -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "$game installer failed: $($process.ExitCode)" }
    foreach ($required in @("$game.exe", 'data/project.ykproj', 'licenses/SDL3.txt')) {
        if (!(Test-Path (Join-Path $destination $required))) { throw "$game omitted $required" }
    }
    if (Get-ChildItem $destination -Recurse -Filter 'yk_editor.exe') { throw "$game contains the editor" }
    $env:SDL_VIDEODRIVER = 'dummy'
    $env:SDL_RENDER_DRIVER = 'software'
    $env:SDL_AUDIODRIVER = 'dummy'
    & (Join-Path $destination "$game.exe") --frames 10 --fixed --no-audio
    if ($LASTEXITCODE -ne 0) { throw "Installed $game failed to launch" }
}
