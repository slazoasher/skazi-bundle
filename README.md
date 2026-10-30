<div align="center">

![SKAZI Logo](Resources/skazi_logo.png)

# SKAZI Bundle

**All SKAZI plugins by Asher Swissa in one place — one installer for everything.**

Every plugin uses the vendor name **SKAZI**, so DAWs list them together.

[![Windows](https://img.shields.io/badge/Windows-Setup.exe-red?logo=windows)](https://github.com/slazoasher/skazi-bundle/releases/latest)
[![macOS](https://img.shields.io/badge/macOS-PKG-red?logo=apple)](https://github.com/slazoasher/skazi-bundle/releases/latest)
[![Plugins](https://img.shields.io/badge/Plugins-19-blueviolet)](https://github.com/slazoasher/skazi-bundle)
[![Presets](https://img.shields.io/badge/Presets-8%2C812-orange)](https://github.com/slazoasher/skazi-bundle)
[![License](https://img.shields.io/badge/License-Free-green)](https://github.com/slazoasher/skazi-bundle)

</div>

---

## 🎛️ Plugins

| # | Folder | Plugin | Category | Presets |
|---|--------|--------|----------|---------|
| 1 | `SKAZI-Synth` | **SKAZI** | Psy / acid / VA synth | 3,650 |
| 2 | `Bully` | **BULLY** | Wavetable synth | 462 |
| 3 | `TBON303` | **SKAZI 303** | Acid bass line synth | 544 |
| 4 | `Bamba` | **BAMBA** | Riser & uplift maker | 260 |
| 5 | `Tahini` | **TAHINI** | Space delay + reverb | 250 |
| 6 | `Subich` | **SUBICH** | Bass + sub bass synth | 1,000 |
| 7 | `Zhug` | **ZHUG** | Saturator + widener | — |
| 8 | `Salat` | **SALAT** | Glitch multi-FX | 104 |
| 9 | `Kickass` | **KICKASS** | Kick drum synth | 50 |
| 10 | `MidiGod` | **MIDI GOD** | MIDI pattern generator + audio to MIDI | 126 |
| 11 | `Kube` | **KUBE** | Psytrance wavetable synth | 306 |
| 12 | `Quazi` | **QUAZI** | 4-band distortion | 104 |
| 13 | `Shaksuka` | **SHAKSUKA** | Random chop & glitch FX | 509 |
| 14 | `PitaPump` | **PITA PUMP** | Sidechain LFO tool | 105 |
| 15 | `FilterFish` | **The FilterFish** | Filter FX | 800 |
| 16 | `Skacid` | **SKACID** | Acid bassline synth (Special Shigaon Edition) | 129 |
| 17 | `Skilter` | **SKILTER** | Morphing multimode filter | 150 |
| 18 | `Skwide` | **SKWIDE** | Phase-safe stereo imager | 70 |
| 19 | `Skverb` | **SKAZI REVERB** | Mega reverb machine | 75 |

`Common` holds the shared metal skin, knobs, preset bar and A/B code used by SUBICH, BAMBA, TAHINI, ZHUG, SALAT, MIDI GOD and QUAZI.

> Plugin IDs (manufacturer + plugin codes) are unchanged, so existing DAW projects keep loading them.

---

## 📦 Adding a New Plugin

1. Put the plugin folder here with bundle-style CMake (JUCE 8.0.4, vendor **SKAZI**, AU on Mac).
2. Add it to `build_bundle.ps1`, `mac/build_mac.sh`, `mac/welcome.html`, `mac/Uninstall SKAZI Bundle.command`, `BundleInstaller/SetupMain.cpp` and a row at the end of the table above.
3. On the download page (`docs/index.html`), add its card **at the top of the list** (the newest plugin shows first) and update the plugin and preset counts.
4. Push to `main`. CI builds both installers and publishes them as the next release (v1.3, v1.4, …). The page's download buttons link to `releases/latest`, so they serve the new installer as soon as the release is up.

---

## 🪟 Windows — Build the Installer

Needs Visual Studio 2022 (C++ workload), CMake and Git.

```powershell
powershell -ExecutionPolicy Bypass -File build_bundle.ps1
```

**Output:** `dist\SKAZI Bundle Setup.exe`, plus every plugin's VST3 and app in `dist\Plugins`.  
JUCE is downloaded automatically (8.0.4 and 8.0.15).  
`-PluginsOnly` builds just the plugins, `-InstallerOnly` re-packs the last built plugins.

### What the Installer Does

| Step | Description |
|------|-------------|
| 1 | Lets you tick which plugins to install |
| 2 | VST3s go to `C:\Program Files\Common Files\VST3\SKAZI\` |
| 3 | Standalone apps go to `C:\Program Files\SKAZI\` with a SKAZI Start Menu folder |
| 4 | Removes older separate installs (SKAZI 1.2, SHAKSUKA, The FilterFish) so nothing shows up twice |
| 5 | Shows up as **SKAZI Bundle** in "Add or remove programs"; the same exe uninstalls (`/uninstall`) |
| 6 | `/silent` installs everything without a window |

> ⚠️ **SmartScreen:** The installer is not code-signed. Windows shows "Windows protected your PC" — click **More info → Run anyway**.

---

## 🍎 Mac (OS X / macOS)

`mac/build_mac.sh` builds all plugins for Apple Silicon + Intel and makes **`dist/SKAZI Bundle.pkg`**:

| Target | Destination |
|--------|-------------|
| VST3 | `/Library/Audio/Plug-Ins/VST3/SKAZI/` |
| Audio Units | `/Library/Audio/Plug-Ins/Components/` (Logic, GarageBand, Ableton) |
| Apps | `/Applications/SKAZI/` (with **Uninstall SKAZI Bundle.command**) |

Run on a Mac: `bash mac/build_mac.sh`. Without a Mac, push to GitHub — `.github/workflows/build-bundle.yml` builds the Mac `.pkg` and Windows `.exe` on GitHub's machines; download from the run's **Artifacts**.

> ⚠️ **Gatekeeper:** The Mac installer is not signed with an Apple Developer ID. First time macOS blocks it — right-click the `.pkg` → **Open** → **Open** again (or System Settings → Privacy & Security → Open Anyway).

> 💡 Before pushing to GitHub, remove the `SKAZI-Synth/.git` folder from this copy (full history stays in `Documents\ACIDANCE\ACIDANCE`), otherwise GitHub treats it as an empty sub-project.

---

<div align="center">

*Built with JUCE 8 · VST3 / AU · Windows & macOS*

</div>