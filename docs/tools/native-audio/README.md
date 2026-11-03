# Original SKAZI voice bank

The studio's Kick, Bass and Acid voices are recordings of the original **KICKASS, SUBICH and SKACID Windows VST3 plugins**, hosted offline with [Spotify Pedalboard](https://spotify.github.io/pedalboard/reference/pedalboard.html). The plugin DSP is not reimplemented in the render script.

The official [v1.15 release](https://github.com/slazoasher/skazi-bundle/releases/tag/v1.15) targets source commit `58dced012df87ad3d977a49cc14685d5a01c1db0`. Its Windows installer SHA256 is `35ff13b662a20f9d100bb82ff06bab4817dcfb13652fe8d4f0e243eb16df0bf9`. The script verifies this hash, extracts only the three VST3 bundles and loads their actual `Contents/x86_64-win/*.vst3` binaries. **It does not execute the installer or install the plugins system-wide.** Binaries and local tools are not shipped with the website.

## Reproduce on Windows

From the repository root, with Python 3.12 and the verified installer downloaded:

```powershell
python -m venv .qa-tools/native-audio
.qa-tools/native-audio/Scripts/python.exe -m pip install -r docs/tools/native-audio/requirements.txt
gh release download v1.15 --repo slazoasher/skazi-bundle --pattern SKAZI.Bundle.Setup.exe --dir .qa-tools/native-release
.qa-tools/native-audio/Scripts/python.exe docs/tools/native-audio/render.py --installer .qa-tools/native-release/SKAZI.Bundle.Setup.exe --work .qa-tools/native-release/rendered --output docs/audio/native
```

The work folder contains individual 24-bit WAV renders and extracted VST3s. The website receives three content-hashed, stereo **48kHz/24-bit FLAC** sprites plus their index/provenance manifest. Total compressed audio is 1,974,441 bytes. FLAC round-trip error was within one 24-bit quantization step; all 128 renders were finite and non-silent. Bank gain is uniform across the entire instrument, preserving recorded velocity relationships. Only final boundary fades and optional common headroom attenuation are applied by the packer.

`patches.json` defines **website-created patches**, not artist-endorsed settings or claimed factory presets. `manifest.json` records actual native parameter values, release/installer provenance, each plugin binary SHA256, asset hashes and every sample region. This release reference is deliberately pinned rather than silently using a future latest installer. Internal random/noise state can make repeated native renders differ; the manifest hashes identify the exact shipped recordings.

## Musical coverage and boundaries

- Kick: three velocity recordings, tuned to MIDI 33 / A1 via native key tracking.
- Bass: MIDI 24–96 at three-semitone intervals and velocities 48/88/120; neighboring velocity layers are crossfaded by the browser.
- Acid: the same note coverage, with separate native normal/accent recordings at MIDI velocities 80/112. SKACID's MIDI mode uses velocity 100 as its accent threshold.
- The browser chooses the closest recorded note (at most one semitone away inside the editor's range), adjusts pitch and gate, and adds browser tone/drive/delay/mix processing. Gate can shorten a recorded voice; it cannot extend the native recorded envelope indefinitely. Slides are browser pitch ramps over recordings; they do not reproduce every native legato/filter state. Live controls are not a full native VST runtime.
- Space and percussion remain the original browser synthesis. TAHINI and SALAT are not yet native processors in this website. The external concept-preview buttons remain explicitly labeled concept audio.

Playback and WAV export use the same bank and event selection. The first Play or export loads the bank; failed loads can be retried and never silently substitute synthetic Kick/Bass/Acid voices. The existing master has low-cut, compression, a soft ceiling and an additional 8Hz DC blocker after nonlinear processing and conservative final output headroom. This last stage removes DC introduced by saturation in dense maximum-control cases.
