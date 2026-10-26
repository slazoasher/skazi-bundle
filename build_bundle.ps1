# Builds every SKAZI plugin and wraps them into one "SKAZI Bundle Setup.exe".
#
#   powershell -ExecutionPolicy Bypass -File build_bundle.ps1 [-PluginsOnly] [-InstallerOnly]
#
# Output: dist\SKAZI Bundle Setup.exe (plus each plugin's VST3 + app in dist\Plugins)
# Builds live under -BuildRoot (keep it short: Visual Studio fails on paths over 260 chars).
param(
    [string]$BuildRoot = "$env:LOCALAPPDATA\Temp\skz",
    [string]$Juce4 = "",   # JUCE 8.0.4  (SHAKSUKA, PITA PUMP); cloned automatically when empty
    [string]$Juce15 = "",  # JUCE 8.0.15 (SKAZI synth, The FilterFish); cloned automatically when empty
    [switch]$PluginsOnly,
    [switch]$InstallerOnly
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$dist = Join-Path $root "dist"

function Run($exe, $argList) {
    & $exe @argList
    if ($LASTEXITCODE -ne 0) { throw "$exe $($argList -join ' ') failed ($LASTEXITCODE)" }
}

function Get-Juce($dir, $tag) {
    if ($dir) { return $dir }
    $dir = Join-Path $BuildRoot "JUCE-$tag"
    if (-not (Test-Path (Join-Path $dir "CMakeLists.txt"))) {
        Run git @("clone", "-q", "--depth", "1", "--branch", $tag, "https://github.com/juce-framework/JUCE.git", $dir)
    }
    return $dir
}

# folder, CMake target, product name (artefact file names), JUCE flag
$products = @(
    @{ Folder = "SKAZI-Synth"; Target = "SKAZI";      Name = "SKAZI";          Juce = "15"; Flag = "JUCE_DIR" },
    @{ Folder = "Shaksuka";    Target = "Shaksuka";   Name = "SHAKSUKA";       Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "PitaPump";    Target = "PitaPump";   Name = "PITA PUMP";      Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "FilterFish";  Target = "FilterFish"; Name = "The FilterFish"; Juce = "15"; Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Subich";      Target = "Subich";     Name = "SUBICH";         Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Bamba";       Target = "Bamba";      Name = "BAMBA";          Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Tahini";      Target = "Tahini";     Name = "TAHINI";         Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Zhug";        Target = "Zhug";       Name = "ZHUG";           Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Bully";       Target = "Bully";      Name = "BULLY";          Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Kickass";     Target = "Kickass";    Name = "KICKASS";        Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Salat";       Target = "Salat";      Name = "SALAT";          Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "TBON303";     Target = "Skazi303";   Name = "SKAZI 303";      Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "MidiGod";     Target = "MidiGod";    Name = "MIDI GOD";       Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Kube";        Target = "Kube";       Name = "KUBE";           Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" }
)

$stage = Join-Path $BuildRoot "stage"

if (-not $InstallerOnly) {
    $j4 = Get-Juce $Juce4 "8.0.4"
    $j15 = Get-Juce $Juce15 "8.0.15"
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    New-Item -ItemType Directory -Force "$stage\VST3", "$stage\Apps" | Out-Null

    foreach ($p in $products) {
        Write-Host "== Building $($p.Name)"
        $b = Join-Path $BuildRoot ("b-" + $p.Target)
        $juce = if ($p.Juce -eq "4") { $j4 } else { $j15 }
        Run cmake @("-S", (Join-Path $root $p.Folder), "-B", $b, "-A", "x64", "-D$($p.Flag)=$juce")
        Run cmake @("--build", $b, "--config", "Release", "--target", "$($p.Target)_VST3", "$($p.Target)_Standalone", "--parallel")
        $art = Join-Path $b "$($p.Target)_artefacts\Release"
        Copy-Item -Recurse -Force (Join-Path $art "VST3\$($p.Name).vst3") "$stage\VST3\"
        Copy-Item -Force (Join-Path $art "Standalone\$($p.Name).exe") "$stage\Apps\"
    }

    New-Item -ItemType Directory -Force "$dist\Plugins" | Out-Null
    Copy-Item -Recurse -Force "$stage\*" "$dist\Plugins\"
}
if ($PluginsOnly) { return }

Write-Host "== Building the installer"
$ib = Join-Path $BuildRoot "b-installer"
$j4 = Get-Juce $Juce4 "8.0.4"
Run cmake @("-S", (Join-Path $root "BundleInstaller"), "-B", $ib, "-A", "x64", "-DFETCHCONTENT_SOURCE_DIR_JUCE=$j4")
Run cmake @("--build", $ib, "--config", "Release", "--target", "SkaziBundleSetup", "--parallel")

# The payload rides on the end of the exe: [setup.exe][payload.zip][zip size: int64][magic "SKZBNDL1"]
Write-Host "== Packing payload"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = Join-Path $BuildRoot "payload.zip"
if (Test-Path $zip) { Remove-Item $zip }
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)

$exe = Get-ChildItem -Recurse (Join-Path $ib "SkaziBundleSetup_artefacts\Release") -Filter *.exe | Select-Object -First 1
New-Item -ItemType Directory -Force $dist | Out-Null
$out = Join-Path $dist "SKAZI Bundle Setup.exe"
$fs = [IO.File]::Open($out, "Create")
try {
    $bytes = [IO.File]::ReadAllBytes($exe.FullName); $fs.Write($bytes, 0, $bytes.Length)
    $z = [IO.File]::ReadAllBytes($zip); $fs.Write($z, 0, $z.Length)
    $len = [BitConverter]::GetBytes([int64]$z.Length); $fs.Write($len, 0, 8)
    $magic = [Text.Encoding]::ASCII.GetBytes("SKZBNDL1"); $fs.Write($magic, 0, 8)
} finally { $fs.Close() }
Write-Host ("== Done: {0} ({1:N1} MB)" -f $out, ((Get-Item $out).Length / 1MB))
