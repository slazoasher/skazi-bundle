#!/usr/bin/env python3
"""Build the DW-303 CLAP bundle (Python 3.11+). Ad hoc signed on macOS; use
scripts/sign-macos.sh for Developer ID signing and notarisation."""
import argparse
import json
import pathlib
import plistlib
import shutil
import subprocess
import sys
import tomllib

NAME = "DW-303"
CRATE = "dw303-clap"
BUNDLE_ID = "co.djwaudio.dw303.clap"


def bundle(profile: str) -> pathlib.Path:
    root = pathlib.Path(__file__).resolve().parents[1]
    metadata = json.loads(subprocess.check_output(
        ["cargo", "metadata", "--no-deps", "--format-version", "1", "--offline"], cwd=root))
    target = pathlib.Path(metadata["target_directory"])
    command = ["cargo", "build", "-p", CRATE, "--offline"]
    if profile == "release":
        command.append("--release")
    subprocess.run(command, cwd=root, check=True)
    stem = CRATE.replace("-", "_")
    output = target / "clap" / profile
    output.mkdir(parents=True, exist_ok=True)
    artifact = output / f"{NAME}.clap"
    if sys.platform == "darwin":
        if artifact.exists():
            shutil.rmtree(artifact)
        contents = artifact / "Contents"
        executable = contents / "MacOS" / NAME
        executable.parent.mkdir(parents=True)
        shutil.copy2(target / profile / f"lib{stem}.dylib", executable)
        version = tomllib.loads((root / "Cargo.toml").read_text())["workspace"]["package"]["version"]
        with (contents / "Info.plist").open("wb") as destination:
            plistlib.dump({
                "CFBundleExecutable": NAME,
                "CFBundleIdentifier": BUNDLE_ID,
                "CFBundleName": NAME,
                "CFBundlePackageType": "BNDL",
                "CFBundleVersion": version,
                "CFBundleShortVersionString": version,
            }, destination)
        subprocess.run(["codesign", "--force", "--sign", "-", str(artifact)], check=True)
    else:
        library = f"{stem}.dll" if sys.platform == "win32" else f"lib{stem}.so"
        shutil.copy2(target / profile / library, artifact)
    return artifact


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=["debug", "release"], default="release")
    print(bundle(parser.parse_args().profile))
