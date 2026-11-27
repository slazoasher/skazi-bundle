#!/usr/bin/env python3
"""Build every DW-303 package for this platform (Python 3.11+) and stage it under
target/plugins without installing anything.

macOS: VST3, AU, AAX and LV2 through native/CMakeLists.txt, plus the CLAP
plug-in and standalone app, ad hoc signed; installer/build-mac-installer.sh
makes the signed release. Windows (x64, MSVC developer prompt): VST3, CLAP and
the standalone app, each with WebView2Loader.dll beside it. Linux (x86_64 or
arm64): VST3, CLAP, LV2 and the standalone app. installer/ has the Windows and
Linux packaging scripts that consume this layout."""
import argparse
import ctypes
import json
import os
import pathlib
import platform
import plistlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
TRIPLES = {"arm64": "aarch64-apple-darwin", "x86_64": "x86_64-apple-darwin"}
# macOS release builds run against the local registry cache; fresh Windows and
# Linux machines may still need to fetch crates.
OFFLINE = ["--offline"] if sys.platform == "darwin" else []


def run(*args, **kwargs):
    subprocess.run([str(a) for a in args], cwd=ROOT, check=True, **kwargs)


def cargo_target() -> pathlib.Path:
    metadata = json.loads(subprocess.check_output(
        ["cargo", "metadata", "--no-deps", "--format-version", "1", "--offline"], cwd=ROOT))
    return pathlib.Path(metadata["target_directory"])


def lipo(output: pathlib.Path, inputs: list[pathlib.Path]):
    output.parent.mkdir(parents=True, exist_ok=True)
    run("lipo", "-create", *inputs, "-output", output)


def replace(source: pathlib.Path, destination: pathlib.Path):
    if destination.exists():
        shutil.rmtree(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination, symlinks=True)


def executable(bundle: pathlib.Path) -> pathlib.Path:
    name = plistlib.loads((bundle / "Contents/Info.plist").read_bytes())["CFBundleExecutable"]
    return bundle / "Contents/MacOS" / name


def host_triple() -> str:
    for line in subprocess.check_output(["rustc", "-vV"], text=True).splitlines():
        if line.startswith("host: "):
            return line.removeprefix("host: ").strip()
    raise SystemExit("rustc -vV reported no host triple")


def third_party(sdk: pathlib.Path, triple: str) -> str:
    """Licence texts for every non-workspace crate in the shipped packages and
    for the external SDKs compiled into the bundles."""
    metadata = json.loads(subprocess.check_output(
        ["cargo", "metadata", "--format-version", "1", *OFFLINE, "--filter-platform", triple], cwd=ROOT))
    packages = {p["id"]: p for p in metadata["packages"]}
    nodes = {n["id"]: n for n in metadata["resolve"]["nodes"]}
    roots = [p["id"] for p in packages.values() if p["name"] in ("dw303-native", "dw303-clap", "dw303-standalone")]
    seen, stack = set(), list(roots)
    while stack:
        current = stack.pop()
        if current in seen:
            continue
        seen.add(current)
        for dep in nodes[current]["deps"]:
            if any(kind["kind"] is None for kind in dep["dep_kinds"]):
                stack.append(dep["pkg"])
    sections = ["DW-303 is licensed under the GNU General Public License v3.0 or later (LICENSE.txt).",
                "RAVET (https://github.com/mo0kid/ravet) is licensed under the GNU General Public License v3.0."]
    for package in sorted((packages[i] for i in seen), key=lambda p: (p["name"], p["version"])):
        if package["source"] is None:
            continue  # DW-303 and RAVET workspace crates
        directory = pathlib.Path(package["manifest_path"]).parent
        texts = sorted(f for f in directory.iterdir()
                       if f.is_file() and f.name.upper().startswith(("LICENSE", "LICENCE", "COPYING", "NOTICE")))
        # Crates that ship no licence file: record the SPDX choice and authors.
        body = "\n\n".join(f.read_text(errors="replace").strip() for f in texts) or (
            f"License: {package['license']}\nAuthors: {', '.join(package['authors']) or 'see repository'}\n"
            f"Source: {package.get('repository') or 'https://crates.io/crates/' + package['name']}")
        sections.append(f"{package['name']} {package['version']} ({package['license']})\n\n{body}")
    licences = [("Steinberg VST3 SDK", "vst3/LICENSE.txt")]
    if "apple" in triple:
        licences += [("Apple AudioUnitSDK", "AudioUnitSDK/LICENSE.txt"), ("LV2", "lv2/COPYING")]
    elif "windows" in triple:
        licences += [("Microsoft Edge WebView2 SDK (WebView2Loader.dll)", "WebView2/LICENSE.txt")]
    else:
        licences += [("LV2", "lv2/COPYING")]
    for title, path in licences:
        if (sdk / path).exists():
            sections.append(f"{title}\n\n{(sdk / path).read_text(errors='replace').strip()}")
    if "apple" in triple:
        sections.append("Avid AAX SDK\n\nUsed under the terms of the GNU General Public License v3, as offered in the SDK headers.")
    return ("\n\n" + "=" * 78 + "\n\n").join(sections) + "\n"


def check_aax(bundle: pathlib.Path):
    """SDK entry points only; Pro Tools loading needs a PACE-signed bundle."""
    library = ctypes.CDLL(str(executable(bundle)))
    for name in ("ACFRegisterPlugin", "ACFRegisterComponent", "ACFGetClassFactory",
                 "ACFCanUnloadNow", "ACFStartup", "ACFShutdown", "ACFGetSDKVersion"):
        getattr(library, name)
    version = ctypes.c_uint64()
    library.ACFGetSDKVersion.argtypes = [ctypes.POINTER(ctypes.c_uint64)]
    if library.ACFGetSDKVersion(ctypes.byref(version)) != 0 or not version.value:
        raise SystemExit("AAX SDK version entry point failed")
    print(f"AAX: SDK entry points present ({version.value:#x})")


def native_static_libs(env: dict, target_args: list) -> str:
    """System libraries the Rust static library needs, as rustc reports them.
    Cargo replays the note for a fresh build, so this is cheap after the first run."""
    result = subprocess.run(
        ["cargo", "rustc", "--release", "-p", "dw303-native", *target_args, "--crate-type", "staticlib",
         "--", "--print", "native-static-libs"],
        cwd=ROOT, env=env, capture_output=True, text=True)
    if result.returncode:
        sys.stderr.write(result.stderr)
        raise SystemExit("cargo rustc for dw303-native failed")
    for line in result.stderr.splitlines():
        if "native-static-libs:" in line:
            return line.split("native-static-libs:", 1)[1].strip()
    raise SystemExit("rustc did not report native-static-libs for dw303-native")


def main_portable(args, parser):
    """Windows and Linux: VST3 (+ LV2 on Linux) through CMake, CLAP and standalone from
    Cargo. `--cross-windows` builds the Windows x64 set on Linux with MinGW-w64."""
    cross = args.cross_windows
    windows = sys.platform == "win32" or cross
    sdk = args.sdk_root.resolve()
    env = os.environ.copy()
    if not windows:
        # WebKitGTK's DMA-BUF/GBM path paints nothing on some drivers (seen with
        # NVIDIA under XWayland in REAPER); software painting is per view and
        # leaves the host's own rendering alone.
        env.setdefault("RAVET_WEB_LINUX_SOFTWARE_RENDERING", "1")
    required = [sdk / "vst3/CMakeLists.txt"]
    webview2 = pathlib.Path(env.setdefault("RAVET_WEBVIEW2_SDK", str(sdk / "WebView2")))
    loader = webview2 / "build/native/x64/WebView2Loader.dll"
    if windows:
        required += [webview2 / "build/native/include/WebView2.h", loader]
    for path in required:
        if not path.exists():
            parser.error(f"missing external SDK file: {path}")
    has_lv2 = not windows and (sdk / "lv2/include/lv2/core/lv2.h").exists()
    target = cargo_target()
    output = target / ("plugins-windows" if cross else "plugins")
    exe = ".exe" if windows else ""
    triple = "x86_64-pc-windows-gnu" if cross else host_triple()
    target_args = ["--target", triple] if cross else []
    released = target / triple / "release" if cross else target / "release"
    toolchain = []
    if cross:
        toolchain = [f"-DCMAKE_TOOLCHAIN_FILE={ROOT / 'installer/windows/mingw-w64.cmake'}"]
        env.setdefault("CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER", "x86_64-w64-mingw32-gcc")
        # Self-contained binaries: RAVET's C++ takes libstdc++ from its static
        # archive (rustc would otherwise link it dynamically), and crt-static
        # does the same for libgcc and winpthread.
        gcc_lib = pathlib.Path(subprocess.check_output(
            ["x86_64-w64-mingw32-g++", "-print-file-name=libstdc++.a"], text=True).strip()).parent
        env.setdefault("CXXSTDLIB_x86_64_pc_windows_gnu", "static=stdc++")
        env.setdefault("CARGO_TARGET_X86_64_PC_WINDOWS_GNU_RUSTFLAGS",
                       f"-C target-feature=+crt-static -L native={gcc_lib}")

    def cargo(*arguments):
        subprocess.run(["cargo", *arguments], cwd=ROOT, env=env, check=True)

    cargo("build", "--release", *target_args, "-p", "dw303-native", "-p", "dw303-clap")
    cargo("build", "--release", *target_args, "-p", "dw303-standalone", "--bin", "DW-303")
    libraries = native_static_libs(env, target_args)
    rust = released / ("dw303_native.lib" if windows and not cross else "libdw303_native.a")

    build = target / ("native-release-windows" if cross else "native-release")
    # Ninja when available; Linux falls back to Makefiles (same output layout).
    if shutil.which("ninja"):
        generator = "Ninja"
    elif windows and not cross:
        parser.error("Ninja is required on Windows (it ships with Visual Studio's CMake tools)")
    else:
        generator = "Unix Makefiles"
    subprocess.run(["cmake", "-S", str(ROOT / "native"), "-B", str(build), "-G", generator, "-DCMAKE_BUILD_TYPE=Release",
                    f"-DDW303_SDK_ROOT={sdk}", f"-DDW303_RUST_LIBRARY={rust}", f"-DDW303_RUST_LINK_LIBS={libraries}",
                    f"-DDW303_WEBVIEW2_LOADER={loader}", *toolchain], cwd=ROOT, env=env, check=True)
    targets = ["DW303VST3"] + (["DW303LV2"] if has_lv2 else [])
    if args.check:
        targets += ["validator"] + (["dw303-lv2-smoke"] if has_lv2 else [])
    subprocess.run(["cmake", "--build", str(build), "--target", *targets, "--parallel", str(os.cpu_count() or 4)],
                   cwd=ROOT, env=env, check=True)

    if output.exists():
        shutil.rmtree(output)
    if cross or sys.platform == "win32":
        # Standard bundle layout, which the SDK builds only on a Windows host.
        bundle = output / "VST3/STB33.vst3"
        binary = bundle / "Contents/x86_64-win"
        binary.mkdir(parents=True)
        shutil.copy2(build / "VST3/STB33.vst3", binary / "STB33.vst3")
        shutil.copy2(loader, binary / "WebView2Loader.dll")
        (bundle / "Contents/Resources").mkdir()
        shutil.copy2(ROOT / "LICENSE", bundle / "Contents/Resources/LICENSE.txt")
    else:
        replace(build / "VST3/Release/STB33.vst3", output / "VST3/STB33.vst3")
    if has_lv2:
        replace(build / "LV2/dw-303.lv2", output / "LV2/dw-303.lv2")
    (output / "CLAP").mkdir(parents=True)
    if cross:
        shutil.copy2(released / "dw303_clap.dll", output / "CLAP/DW-303.clap")
    else:
        subprocess.run([sys.executable, str(ROOT / "scripts/bundle-clap.py")], cwd=ROOT, env=env, check=True)
        shutil.copy2(target / "clap/release/DW-303.clap", output / "CLAP/DW-303.clap")
    (output / "Standalone").mkdir(parents=True)
    shutil.copy2(released / f"DW-303{exe}", output / "Standalone" / f"DW-303{exe}")
    if windows:
        for kind in ("CLAP", "Standalone"):
            shutil.copy2(loader, output / kind / "WebView2Loader.dll")

    notices = third_party(sdk, triple)
    shutil.copy2(ROOT / "LICENSE", output / "LICENSE.txt")
    (output / "THIRD-PARTY.txt").write_text(notices, encoding="utf-8")
    (output / "VST3/STB33.vst3/Contents/Resources/THIRD-PARTY.txt").write_text(notices, encoding="utf-8")
    if has_lv2:
        (output / "LV2/dw-303.lv2/THIRD-PARTY.txt").write_text(notices, encoding="utf-8")

    if args.check:
        validator = [str(next(p for p in (build / "bin/Release" / f"validator{exe}", build / "bin" / f"validator{exe}")
                              if p.exists()))]
        if cross:
            if not shutil.which("wine"):
                parser.error("--check with --cross-windows runs the validator under Wine; install wine")
            validator.insert(0, "wine")
        subprocess.run([*validator, str(output / "VST3/STB33.vst3")], cwd=ROOT, check=True)
        if has_lv2:
            subprocess.run([str(build / "dw303-lv2-smoke"), str(output / "LV2/dw-303.lv2")], cwd=ROOT, check=True)
    for path in sorted(output.rglob("*")):
        if path.is_file():
            print(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=pathlib.Path, default=ROOT / "target/sdks",
                        help="directory containing vst3/, AudioUnitSDK/, AAX/, lv2/ and (Windows) WebView2/")
    parser.add_argument("--universal", action="store_true", help="arm64 and x86_64 (both Rust targets installed)")
    parser.add_argument("--cross-windows", action="store_true",
                        help="on Linux: build the Windows x64 set with MinGW-w64 into target/plugins-windows")
    parser.add_argument("--check", action="store_true", help="run the VST3 validator, auval-style AU and LV2 hosts and the AAX export check")
    args = parser.parse_args()
    if args.cross_windows and not sys.platform.startswith("linux"):
        parser.error("--cross-windows runs on Linux")
    if sys.platform != "darwin":
        if args.universal:
            parser.error("--universal is macOS only")
        return main_portable(args, parser)
    sdk = args.sdk_root.resolve()
    for required in ("vst3/CMakeLists.txt", "AudioUnitSDK/include/AudioUnitSDK/AudioUnitSDK.h"):
        if not (sdk / required).exists():
            parser.error(f"missing external SDK: {sdk / required}")
    architectures = ["arm64", "x86_64"] if args.universal else [platform.machine()]
    target = cargo_target()
    output = target / "plugins"

    # Rust: the native static library, CLAP dylib and standalone executable per architecture.
    for arch in architectures:
        common = ("cargo", "build", "--offline", "--release", "--target", TRIPLES[arch])
        run(*common, "-p", "dw303-native", "-p", "dw303-clap")
        run(*common, "-p", "dw303-standalone", "--bin", "DW-303")
    built = lambda name: [target / TRIPLES[a] / "release" / name for a in architectures]
    native = target / "universal" / "libdw303_native.a"
    lipo(native, built("libdw303_native.a"))

    build = target / "native-release"
    run("cmake", "-S", ROOT / "native", "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_OSX_ARCHITECTURES={';'.join(architectures)}", f"-DDW303_SDK_ROOT={sdk}",
        f"-DDW303_RUST_LIBRARY={native}")
    targets = ["DW303VST3", "DW303AU"]
    has_aax = (sdk / "AAX/Interfaces/AAX.h").exists()
    has_lv2 = (sdk / "lv2/include/lv2/core/lv2.h").exists()
    targets += ["DW303AAX"] if has_aax else []
    targets += ["DW303LV2"] if has_lv2 else []
    if args.check:
        targets += ["validator", "dw303-au-smoke"] + (["dw303-lv2-smoke"] if has_lv2 else [])
    run("cmake", "--build", build, "--target", *targets, "--parallel")

    staged = {"VST3": build / "VST3/Release/STB33.vst3", "AU": build / "AU/STB33.component"}
    if has_aax:
        staged["AAX"] = build / "AAX/STB33.aaxplugin"
    if has_lv2:
        staged["LV2"] = build / "LV2/dw-303.lv2"
    for kind, source in staged.items():
        replace(source, output / kind / source.name)

    # CLAP and standalone: assemble with the existing scripts, then merge slices.
    run("python3", ROOT / "scripts/bundle-clap.py")
    run("python3", ROOT / "scripts/bundle-app.py")
    for kind, source, binary in (("CLAP", target / "clap/release/DW-303.clap", "libdw303_clap.dylib"),
                                 ("Standalone", target / "standalone/release/DW-303.app", "DW-303")):
        destination = output / kind / source.name
        replace(source, destination)
        lipo(executable(destination), built(binary))

    notices = third_party(sdk, TRIPLES[architectures[0]])
    (output / "THIRD-PARTY.txt").write_text(notices)
    for bundle in output.glob("*/*"):
        resources = bundle if bundle.suffix == ".lv2" else bundle / "Contents/Resources"
        resources.mkdir(parents=True, exist_ok=True)
        (resources / "THIRD-PARTY.txt").write_text(notices)

    for kind in ("VST3", "AU", "AAX", "CLAP", "Standalone"):
        for bundle in (output / kind).glob("*") if (output / kind).exists() else []:
            run("codesign", "--force", "--sign", "-", bundle)
    if has_lv2:
        run("codesign", "--force", "--sign", "-", output / "LV2/dw-303.lv2/dw-303.dylib")

    if args.check:
        run(build / "bin/Release/validator", output / "VST3/STB33.vst3")
        run(build / "dw303-au-smoke", output / "AU/STB33.component")
        if has_lv2:
            run(build / "dw303-lv2-smoke", output / "LV2/dw-303.lv2")
        if has_aax:
            check_aax(output / "AAX/STB33.aaxplugin")
    for path in sorted(output.glob("*/*")):
        print(path)


if __name__ == "__main__":
    main()
