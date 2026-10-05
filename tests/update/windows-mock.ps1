param(
    [switch]$Child,
    [ValidateSet('newer', 'same', 'bad-hash')][string]$Scenario = 'newer',
    [string]$Marker
)

$ErrorActionPreference = 'Stop'
$updater = (Resolve-Path (Join-Path $PSScriptRoot '../../packaging/update/windows.ps1')).Path
if (-not $Child) {
    $work = Join-Path $env:TEMP ('yk-updater-test-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null
    try {
        foreach ($case in 'newer', 'same', 'bad-hash') {
            $caseMarker = Join-Path $work "$case.txt"
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath `
                -Child -Scenario $case -Marker $caseMarker
            $expected = if ($case -eq 'newer') { 10 } else { 0 }
            if ($LASTEXITCODE -ne $expected) { throw "$case returned $LASTEXITCODE, expected $expected" }
            if ((Test-Path $caseMarker) -ne ($case -eq 'newer')) {
                throw "$case did not handle the installer as expected"
            }
        }
    } finally {
        Remove-Item -LiteralPath $work -Recurse -Force
    }
    exit 0
}

$global:fakeInstaller = Join-Path (Split-Path $Marker -Parent) "fake-$Scenario.exe"
$env:LOCALAPPDATA = Split-Path $Marker -Parent
[IO.File]::WriteAllText($global:fakeInstaller, 'stand-in installer')
$global:hash = (Get-FileHash -LiteralPath $global:fakeInstaller -Algorithm SHA256).Hash
$global:caseMarker = $Marker
$global:badHash = $Scenario -eq 'bad-hash'
function Invoke-RestMethod {
    return [pscustomobject]@{
        tag_name = 'v0.3.2'
        assets = @(
            [pscustomobject]@{ name = 'YKEngine-0.3.2-windows-x64.exe'; browser_download_url = 'mock:installer' },
            [pscustomobject]@{ name = 'SHA256SUMS.txt'; browser_download_url = 'mock:sums' }
        )
    }
}
function Invoke-WebRequest {
    param($Uri, $OutFile, $TimeoutSec)
    if ($Uri -eq 'mock:installer') {
        Copy-Item -LiteralPath $global:fakeInstaller -Destination $OutFile
    } else {
        $hash = if ($global:badHash) { '0' * 64 } else { $global:hash }
        [IO.File]::WriteAllText($OutFile, "$hash  YKEngine-0.3.2-windows-x64.exe`n")
    }
}
function Start-Process {
    param($FilePath, $Verb, $ArgumentList)
    [IO.File]::WriteAllText($global:caseMarker, $FilePath)
}
$current = if ($Scenario -eq 'same') { '0.3.2' } else { '0.3.1' }
& $updater $current (Join-Path $env:TEMP 'YK Engine') 2>$null
exit $LASTEXITCODE
