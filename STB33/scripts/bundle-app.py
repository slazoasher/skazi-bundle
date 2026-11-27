#!/usr/bin/env python3
"""Build a local macOS DW-303.app and verify its ad-hoc signature (Python 3.11+)."""
import argparse
import json
import pathlib
import plistlib
import shutil
import subprocess
import sys
import tempfile
import tomllib

NAME = "DW-303"
CRATE = "dw303-standalone"
BUNDLE_ID = "co.djwaudio.dw303.app"


def bundle(profile: str) -> pathlib.Path:
    """Build only the standalone package and assemble its self-contained bundle."""
    if sys.platform != "darwin":
        raise SystemExit("DW-303.app requires macOS and Xcode command-line tools")
    root = pathlib.Path(__file__).resolve().parents[1]
    command = ["cargo", "build", "--offline", "-p", CRATE,
               "--bin", NAME, "--message-format=json-render-diagnostics"]
    if profile == "release":
        command.append("--release")
    result = subprocess.run(command, cwd=root, check=True, stdout=subprocess.PIPE, text=True)
    executable = None
    for line in result.stdout.splitlines():
        message = json.loads(line)
        if (message.get("reason") == "compiler-artifact"
                and message.get("target", {}).get("name") == NAME
                and message.get("executable")):
            executable = pathlib.Path(message["executable"])
    if executable is None:
        raise SystemExit("Cargo did not report the DW-303 executable")
    output = executable.parent.parent / "standalone" / profile
    output.mkdir(parents=True, exist_ok=True)
    artifact = output / f"{NAME}.app"
    version = tomllib.loads((root / "Cargo.toml").read_text())["workspace"]["package"]["version"]
    # Build and sign completely before replacing the previous local bundle.
    with tempfile.TemporaryDirectory(prefix=".dw303-", dir=output) as temporary:
        staged = pathlib.Path(temporary) / artifact.name
        contents = staged / "Contents"
        destination = contents / "MacOS" / NAME
        destination.parent.mkdir(parents=True)
        (contents / "Resources").mkdir()
        shutil.copy2(executable, destination)
        destination.chmod(0o755)
        with (contents / "Info.plist").open("wb") as stream:
            plistlib.dump({
                "CFBundleExecutable": NAME,
                "CFBundleIdentifier": BUNDLE_ID,
                "CFBundleName": NAME,
                "CFBundleDisplayName": NAME,
                "CFBundlePackageType": "APPL",
                "CFBundleVersion": version,
                "CFBundleShortVersionString": version,
                "LSMinimumSystemVersion": "12.0",
                "NSHighResolutionCapable": True,
                "NSPrincipalClass": "NSApplication",
            }, stream)
        (contents / "PkgInfo").write_bytes(b"APPL????")
        subprocess.run(["codesign", "--force", "--sign", "-", str(staged)], check=True)
        subprocess.run(["codesign", "--verify", "--strict", "--verbose=2", str(staged)], check=True)
        if artifact.exists():
            shutil.rmtree(artifact)
        shutil.move(str(staged), artifact)
    return artifact


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=["debug", "release"], default="release")
    print(bundle(parser.parse_args().profile))
