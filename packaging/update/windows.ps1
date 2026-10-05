param(
    [Parameter(Mandatory = $true)][string]$CurrentVersion,
    [Parameter(Mandatory = $true)][string]$InstallRoot
)

$ErrorActionPreference = 'Stop'
try {
    $release = Invoke-RestMethod -Uri 'https://api.github.com/repos/Adventnl/YK-Engine/releases/latest' `
        -Headers @{ 'User-Agent' = 'YK-Engine-Updater'; 'Accept' = 'application/vnd.github+json' } `
        -TimeoutSec 5
    if ($release.tag_name -notmatch '^v([0-9]+\.[0-9]+\.[0-9]+(?:\.[0-9]+)?)$') {
        throw 'The latest release has an invalid version tag.'
    }
    $latest = $Matches[1]
    if ([version]$latest -le [version]$CurrentVersion) { exit 0 }

    $filename = "YKEngine-$latest-windows-x64.exe"
    $installerAsset = @($release.assets | Where-Object { $_.name -ceq $filename })
    $sumsAsset = @($release.assets | Where-Object { $_.name -ceq 'SHA256SUMS.txt' })
    if ($installerAsset.Count -ne 1 -or $sumsAsset.Count -ne 1) {
        throw 'The release is missing its Windows installer or checksums.'
    }
    $folder = Join-Path $env:LOCALAPPDATA "YKEngine\Updates\$latest"
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    $installer = Join-Path $folder $filename
    $sums = Join-Path $folder 'SHA256SUMS.txt'
    Invoke-WebRequest -Uri $sumsAsset[0].browser_download_url -OutFile $sums -TimeoutSec 60
    Invoke-WebRequest -Uri $installerAsset[0].browser_download_url -OutFile $installer -TimeoutSec 300
    $matchingLine = @(Get-Content $sums | Where-Object { $_ -match ('^([a-fA-F0-9]{64})\s+\*?' + [regex]::Escape($filename) + '$') })
    if ($matchingLine.Count -ne 1) { throw 'The installer has no unique checksum.' }
    $expected = [regex]::Match($matchingLine[0], '^[a-fA-F0-9]{64}').Value
    $actual = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash
    if ($actual -ine $expected) { throw 'The downloaded installer failed its SHA-256 check.' }

    # NSIS replaces the existing installation after the editor exits. The elevation prompt is
    # required when the user installed into Program Files.
    Start-Process -FilePath $installer -Verb RunAs -ArgumentList @('/S', "/D=$InstallRoot") | Out-Null
    exit 10
} catch {
    # A network or installer failure must not prevent the already-installed editor from opening.
    Write-Error $_ -ErrorAction Continue
    exit 0
}
