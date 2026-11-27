<#
.SYNOPSIS
  Builds the DW-303 Windows installer (Inno Setup 6) from the staged bundles.

.DESCRIPTION
  Reads the version from the workspace Cargo.toml ([workspace.package] version),
  stages the plug-ins with scripts/bundle-plugins.py (unless -SkipBuild),
  verifies the staged files, compiles installer/dw-303.iss with ISCC.exe and
  writes target/installer/DW-303-<version>-windows-x64.exe plus a copy named
  dw-303-win-latest.exe (the name the website serves).

  Requires Windows PowerShell 5.1 or PowerShell 7, Python 3.11+ (py launcher)
  and Inno Setup 6.3+. The installer is unsigned; see the SignTool hook in
  installer/dw-303.iss.

.PARAMETER SkipBuild
  Use whatever is already staged under target/plugins.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File installer\build-windows-installer.ps1
#>
[CmdletBinding()]
param(
    [switch]$SkipBuild
)

Set-StrictMode -Version 3
$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
# Cargo's target directory (bundle-plugins.py stages there), honouring CARGO_TARGET_DIR.
$Target = if ($env:CARGO_TARGET_DIR) {
    if ([System.IO.Path]::IsPathRooted($env:CARGO_TARGET_DIR)) { $env:CARGO_TARGET_DIR } else { Join-Path $Root $env:CARGO_TARGET_DIR }
} else { Join-Path $Root 'target' }
$Stage = Join-Path $Target 'plugins'
$Out = Join-Path $Target 'installer'
$Iss = Join-Path $PSScriptRoot 'dw-303.iss'

function Fail([string]$Message) {
    Write-Error $Message -ErrorAction Continue
    exit 1
}

# Version from [workspace.package] in the workspace Cargo.toml.
$Version = $null
$InSection = $false
foreach ($Line in Get-Content -LiteralPath (Join-Path $Root 'Cargo.toml')) {
    if ($Line -match '^\s*\[(.+)\]\s*$') { $InSection = ($Matches[1].Trim() -eq 'workspace.package'); continue }
    if ($InSection -and $Line -match '^\s*version\s*=\s*"([^"]+)"') { $Version = $Matches[1]; break }
}
if (-not $Version) { Fail 'could not read [workspace.package] version from Cargo.toml' }
# Four-part numeric file version: 0.1.0-beta.2 -> 0.1.0.0
$Numeric = @(($Version -split '[-+]')[0].Split('.') | ForEach-Object { [int]$_ })
while ($Numeric.Count -lt 4) { $Numeric += 0 }
$VersionNumeric = ($Numeric[0..3] -join '.')
Write-Host "DW-303 $Version ($VersionNumeric)"

if (-not $SkipBuild) {
    $Python = Get-Command py -ErrorAction SilentlyContinue
    if ($Python) { $PyArgs = @('-3') } else {
        $Python = Get-Command python -ErrorAction SilentlyContinue
        $PyArgs = @()
        if (-not $Python) { Fail 'Python 3 not found (install it with the py launcher)' }
    }
    & $Python.Source @PyArgs (Join-Path $Root 'scripts\bundle-plugins.py')
    if ($LASTEXITCODE -ne 0) { Fail "scripts/bundle-plugins.py failed (exit $LASTEXITCODE)" }
}

# Licence fallback: the repository LICENSE is the GPL text the bundles ship.
$License = Join-Path $Stage 'LICENSE.txt'
if (-not (Test-Path -LiteralPath $License -PathType Leaf) -and (Test-Path -LiteralPath (Join-Path $Root 'LICENSE'))) {
    Copy-Item -LiteralPath (Join-Path $Root 'LICENSE') -Destination $License
}

$Required = @(
    'LICENSE.txt',
    'THIRD-PARTY.txt',
    'VST3\DW-303.vst3\Contents\x86_64-win\DW-303.vst3',
    'VST3\DW-303.vst3\Contents\x86_64-win\WebView2Loader.dll',
    'VST3\DW-303.vst3\Contents\Resources\LICENSE.txt',
    'CLAP\DW-303.clap',
    'CLAP\WebView2Loader.dll',
    'Standalone\DW-303.exe',
    'Standalone\WebView2Loader.dll'
)
$Missing = @($Required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $Stage $_) -PathType Leaf) })
if ($Missing.Count -gt 0) {
    Fail ("missing staged files under ${Stage}:`n  " + ($Missing -join "`n  ") + "`n(run py -3 scripts\bundle-plugins.py)")
}

# ISCC.exe: PATH first, then the default Inno Setup 6 locations.
$Iscc = $null
$OnPath = Get-Command ISCC.exe -ErrorAction SilentlyContinue
if ($OnPath) { $Iscc = $OnPath.Source } else {
    $Candidates = @()
    if (${env:ProgramFiles(x86)}) { $Candidates += Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe' }
    if ($env:ProgramFiles) { $Candidates += Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe' }
    $Iscc = $Candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $Iscc) { Fail 'ISCC.exe not found; install Inno Setup 6 (https://jrsoftware.org/isinfo.php) or add it to PATH' }
Write-Host "Inno Setup: $Iscc"

New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Setup = Join-Path $Out "DW-303-$Version-windows-x64.exe"
if (Test-Path -LiteralPath $Setup) { Remove-Item -LiteralPath $Setup -Force }

& $Iscc /Q "/DAppVersion=$Version" "/DAppVersionNumeric=$VersionNumeric" "/DStageDir=$Stage" "/DOutputDir=$Out" $Iss
if ($LASTEXITCODE -ne 0) { Fail "ISCC failed (exit $LASTEXITCODE)" }
if (-not (Test-Path -LiteralPath $Setup -PathType Leaf)) { Fail "ISCC succeeded but $Setup was not written" }

$Latest = Join-Path $Out 'dw-303-win-latest.exe'
Copy-Item -LiteralPath $Setup -Destination $Latest -Force

Write-Host "Installer: $Setup"
Write-Host "Latest:    $Latest"
Write-Host 'Upload it to the site as downloads/dw-303-win-latest.exe.'
