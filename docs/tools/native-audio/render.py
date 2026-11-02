"""Render the official Windows VST3 instruments into lossless, indexed voice banks.

Does not run the installer, modify plugin DSP or copy the DSP into Python.
See README.md for release provenance, reproduction and sampling limitations.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
from zipfile import ZipFile
import mido
import numpy as np
import pedalboard
import soundfile as sf

RELEASE = "v1.15"
COMMIT = "58dced012df87ad3d977a49cc14685d5a01c1db0"
INSTALLER_SHA = "35ff13b662a20f9d100bb82ff06bab4817dcfb13652fe8d4f0e243eb16df0bf9"
RATE = 48000

def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installer", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if sha(args.installer) != INSTALLER_SHA:
        raise ValueError("Installer differs from the verified official v1.15 asset")
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)
    patches = json.loads(Path(__file__).with_name("patches.json").read_text())
    with ZipFile(args.installer) as archive:
        prefixes = tuple("VST3/" + p["plugin"] + ".vst3/" for p in patches.values())
        for name in archive.namelist():
            if name.startswith(prefixes):
                if not (work / name).resolve().is_relative_to(work):
                    raise ValueError("Unsafe archive member")
                archive.extract(name, work)
    manifest = {
        "version": 1, "sampleRate": RATE, "encoding": "FLAC PCM_24",
        "source": {"repository": "https://github.com/slazoasher/skazi-bundle", "release": RELEASE,
                   "commit": COMMIT, "installerSha256": INSTALLER_SHA,
                   "host": "Spotify Pedalboard " + pedalboard.__version__, "blockSize": 256},
        "description": "Native VST3 recordings using documented website patches; browser pitch/gate/mix processing remains sample-based.",
        "voices": {},
    }
    report = []
    for track, patch in patches.items():
        name = patch["plugin"]
        binary = work / "VST3" / (name + ".vst3") / "Contents/x86_64-win" / (name + ".vst3")
        plugin = pedalboard.load_plugin(str(binary))
        if not plugin.is_instrument:
            raise ValueError(name + " is not an instrument")
        for key, value in patch["parameters"].items():
            if key not in plugin.parameters:
                raise ValueError("Unknown native parameter " + key)
            setattr(plugin, key, value)
        parameter_state = {}
        for key, parameter in plugin.parameters.items():
            value = getattr(plugin, key)
            text = str(value)
            parameter_state[key] = text if parameter.range[0] is None else (text == "True" if text in ("True", "False") else float(text))
        note_spec = patch["notes"]
        notes = note_spec if isinstance(note_spec, list) else range(note_spec["from"], note_spec["to"] + 1, note_spec["step"])
        renders, index = [], []
        gate, tail = patch["gateSeconds"], patch["tailSeconds"]
        for note in notes:
            for velocity in patch["velocities"]:
                audio = plugin([
                    mido.Message("note_on", note=note, velocity=velocity, time=0.05),
                    mido.Message("note_off", note=note, velocity=0, time=0.05 + gate),
                ], duration=0.05 + gate + tail, sample_rate=RATE, buffer_size=256, reset=True)
                audio = np.asarray(audio[:, round(0.05 * RATE):], dtype=np.float32).copy()
                if not np.isfinite(audio).all() or np.max(np.abs(audio)) < 1e-5:
                    raise ValueError(f"Invalid or silent {name} note {note} velocity {velocity}")
                # Boundary fades only: keep each native voice's attack, envelope and timbre.
                audio[:, -96:] *= np.linspace(1, 0, 96, dtype=np.float32)
                renders.append(audio)
                index.append({"midi": note, "velocity": velocity, "accent": track == "acid" and velocity >= 100,
                              "frames": audio.shape[1], "gateFrames": round(gate * RATE)})
        raw_peak = max(float(np.max(np.abs(a))) for a in renders)
        gain = min(1.0, 10 ** (-3 / 20) / raw_peak)
        parts, cursor = [], 0
        for audio, item in zip(renders, index):
            audio *= gain
            item["offsetFrames"] = cursor
            cursor += audio.shape[1]
            parts.extend([audio, np.zeros((2, 576), dtype=np.float32)])
            cursor += 576
            sf.write(work / f"{name}_{item['midi']}_{item['velocity']}.wav", audio.T, RATE, subtype="PCM_24")
        bank = np.concatenate(parts, axis=1)
        temporary = args.output / (track + ".flac")
        sf.write(temporary, bank.T, RATE, format="FLAC", subtype="PCM_24")
        digest = sha(temporary)
        filename = track + "-" + digest[:12] + ".flac"
        destination = args.output / filename
        temporary.replace(destination)
        decoded, rate = sf.read(destination, dtype="float32", always_2d=True)
        max_delta = float(np.max(np.abs(decoded.T - bank)))
        if rate != RATE or max_delta > 2 ** -22:
            raise ValueError("FLAC roundtrip exceeds 24-bit quantization tolerance")
        manifest["voices"][track] = {"plugin": name, "patch": patch["name"], "file": filename,
            "sha256": digest, "bytes": destination.stat().st_size, "frames": bank.shape[1],
            "nativeBinarySha256": sha(binary), "gain": gain, "parameters": parameter_state, "samples": index}
        report.append({"track": track, "samples": len(index), "seconds": bank.shape[1] / RATE,
                       "bytes": destination.stat().st_size, "rawPeak": raw_peak, "gain": gain,
                       "peakDb": 20 * np.log10(float(np.max(np.abs(bank)))), "flacMaxError": max_delta,
                       "finite": bool(np.isfinite(bank).all())})
        print(json.dumps(report[-1]), flush=True)
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf8")
    (args.output / "render-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")

if __name__ == "__main__":
    main()
