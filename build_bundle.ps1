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
    @{ Folder = "Kube";        Target = "Kube";       Name = "KUBE";           Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Quazi";       Target = "Quazi";      Name = "QUAZI";          Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Skacid";      Target = "Skacid";     Name = "SKACID";         Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Skilter";     Target = "Skilter";    Name = "SKILTER";        Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Skwide";      Target = "Skwide";     Name = "SKWIDE";         Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Skverb";      Target = "Skverb";     Name = "SKAZI REVERB";   Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Squalizer";   Target = "Squalizer";  Name = "S-QUALIZER";     Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Skazon";      Target = "Skazon";     Name = "SKAZON";         Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Skyz";        Target = "Skyz";       Name = "SKY Z";          Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Skatom";      Target = "Skatom";     Name = "SKATOM";         Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Skazilayzer"; Target = "Skazilayzer"; Name = "SKAZILAYZER";   Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" },
    @{ Folder = "Skabotic";    Target = "Skabotic";   Name = "SKABOTIC";       Juce = "4";  Flag = "FETCHCONTENT_SOURCE_DIR_JUCE" }
)

# SKY Z's screen is a web view (WebView2): JUCE finds its SDK in the user's NuGet package folder.
# The package is a zip; unpack it there directly (no PackageManagement prompts on a fresh machine).
function Ensure-WebView2 {
    $pkgs = Join-Path $env:USERPROFILE "AppData\Local\PackageManagement\NuGet\Packages"
    if (Get-ChildItem $pkgs -Filter "Microsoft.Web.WebView2*" -ErrorAction SilentlyContinue) { return }
    Write-Host "== Getting the WebView2 SDK"
    $ver = "1.0.1901.177"
    $dir = Join-Path $pkgs "Microsoft.Web.WebView2.$ver"
    $zip = Join-Path $BuildRoot "webview2.zip"
    New-Item -ItemType Directory -Force $dir, $BuildRoot | Out-Null
    Invoke-WebRequest -Uri "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$ver" -OutFile $zip -UseBasicParsing
    Expand-Archive -Path $zip -DestinationPath $dir -Force
}

# STB33 is Rust + C++ (not JUCE): its own script builds the VST3 and the standalone app with the
# VST3 SDK and WebView2 SDK it expects under one sdk root, inside an MSVC x64 developer environment.
function Build-Stb33 {
    Write-Host "== Building STB33"
    $src = Join-Path $root "STB33"
    $sdk = Join-Path $BuildRoot "sdk33"
    if (-not (Test-Path "$sdk\vst3\CMakeLists.txt")) {
        Run git @("clone", "-q", "--depth", "1", "--recursive", "--branch", "v3.8.0_build_66", "https://github.com/steinbergmedia/vst3sdk.git", "$sdk\vst3")
    }
    if (-not (Test-Path "$sdk\WebView2\build\native\include\WebView2.h")) {
        $zip = Join-Path $BuildRoot "webview2-stb33.zip"
        Invoke-WebRequest -Uri "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.3650.58" -OutFile $zip -UseBasicParsing
        Expand-Archive -Path $zip -DestinationPath "$sdk\WebView2" -Force
    }
    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $py = if (Get-Command py -ErrorAction SilentlyContinue) { "py -3" } else { "python" }
    $out = Join-Path $BuildRoot "t33"
    $cmd = Join-Path $BuildRoot "build-stb33.cmd"
    # vcvars64 also puts Visual Studio's CMake and Ninja on PATH
    Set-Content -Encoding ascii $cmd @(
        "@echo off",
        "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul || exit /b 1",
        "set RAVET_WEBVIEW2_SDK=$sdk\WebView2",
        "set CARGO_TARGET_DIR=$out",
        "cd /d `"$src`"",
        "cargo fetch || exit /b 1",
        "$py `"$src\scripts\bundle-plugins.py`" --sdk-root `"$sdk`""
    )
    Run cmd @("/c", $cmd)
    Copy-Item -Recurse -Force "$out\plugins\VST3\STB33.vst3" "$stage\VST3\"
    Copy-Item -Force "$out\plugins\Standalone\DW-303.exe" "$stage\Apps\STB33.exe"
    Copy-Item -Force "$out\plugins\Standalone\WebView2Loader.dll" "$stage\Apps\"
}

$stage = Join-Path $BuildRoot "stage"

if (-not $InstallerOnly) {
    Ensure-WebView2
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
    Build-Stb33

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
