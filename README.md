# SKAZI Bundle

All SKAZI plugins by Asher Swissa in one place, and one installer for all of them.
Every plugin uses the vendor name **SKAZI**, so DAWs list them together.

| Folder | Plugin | Type |
|---|---|---|
| `SKAZI-Synth` | SKAZI | Psy / acid / VA synth: Virus-style supersaw unison and analog drift, wavetables (load your own WAV), FOLD/PWM/ASYM shaping, arpeggiator, step sequencer with per-step locks, 10 distortion types, full FX rack with FX filter and bass-mono width, PERFORM page (4 macros, A/B morph, kick duck, voice cards), RANDOM that rolls every section, BULLY skin, 3,650 presets |
| `Shaksuka` | SHAKSUKA | Random chop & glitch FX |
| `PitaPump` | PITA PUMP | Sidechain LFO tool |
| `FilterFish` | The FilterFish | Filter FX |
| `Subich` | SUBICH | Bass + sub bass synth: 2 osc, sub, 2 filters, 2 LFOs, wheels, 1000 presets |
| `Bamba` | BAMBA | Riser & uplift maker: synth risers plus a Splice layer that finds your own riser/uplift samples (short/long, stretch to the bars), snare rolls and gated uplifts quantized 1/2 to 1/128 with an automatable RATE knob, mixer with FX sends, pitch up to +6 octaves with its own curve, 1 to 24 bars, 260 presets with impacts and FX in their own section |
| `Tahini` | TAHINI | Space delay + reverb: 7 delay types (ping-pong, stereo, tape, digital, BBD, multi-tap, dual), resonant low/high cut and tape/tube/fuzz/crush drive inside the echo loop, pitch on the repeats (+-24 st, shimmer blend), reverse, tape stops, reverb with duck and freeze, 250 presets |
| `Zhug` | ZHUG | Saturator + widener, 4x oversampled, multiband |
| `Bully` | BULLY | Wavetable synth: 3 osc with dual warp, 2 filters, 10 LFOs, chaos, mod matrix, FX rack, arp, analog character (drift, saturation, boost, spread), SHARP 24 zero-delay-feedback ladder filter, 462 presets |
| `Kickass` | KICKASS | Kick drum synth: two-stage pitch (snap + sweep), hold/decay, 3-band EQ, sample layer with a browser for your own Splice kicks, key track, 50 presets |
| `TBON303` | SKAZI 303 (TBON 303) | Acid bass line synth: 303-style diode ladder filter with accent and slide, sub oscillator, 14 drive types, circuit mods, chorus/delay/reverb/widener, 16-step sequencer with pattern generator, screaming resonance (self-oscillates at the top of the knob), 544 presets incl. 32 HARD ACID |
| `Salat` | SALAT | Glitch multi-FX: tempo-synced chopper (stutter, reverse, gate, tape stop, hold), drive, crush, resonant filter with synced LFO, ring mod, flanger/chorus, ping-pong delay, reverb, MAKE A SALAT randomizer, 104 presets |
| `MidiGod` | MIDI GOD | MIDI pattern generator (instrument + MIDI out): 10 genres (full-on, prog psy, psy-tech, tech/deep/bass house, melodic/hard techno, trance, EDM) x 9 roles (bass, acid, lead, pluck, arp, chords, pad, counter, drums), key/scale/groove controls, drag the loop into the DAW as MIDI, audio to MIDI (melody, bass, drums, chords), 106 presets |
| `Kube` | KUBE | Psytrance wavetable synth: 24 KUBE-only tables (growl, yoy and more), Serum .wav wavetable import (drag and drop), 2 warp slots per oscillator (sync, bend, PWM, asym, mirror, flip, quantize, FM/AM/RM), 16-voice unison with blend, lime-on-black skin, 106 presets |

`Common` holds the shared metal skin, knobs, preset bar and A/B code used by SUBICH, BAMBA, TAHINI, ZHUG, SALAT and MIDI GOD.

Plugin IDs (manufacturer + plugin codes) are unchanged, so existing DAW projects keep loading them.

## Adding a new plugin (one click for everyone)

1. Put the plugin folder here with bundle-style CMake (JUCE 8.0.4, vendor "SKAZI", AU on Mac).
2. Add it to `build_bundle.ps1`, `mac/build_mac.sh`, `mac/welcome.html`, `mac/Uninstall SKAZI Bundle.command`,
   `BundleInstaller/SetupMain.cpp` and a row at the end of the table above.
3. On the download page (`docs/index.html`), add its card **at the bottom of the list** (the newest plugin is always last;
   BULLY stays first) and update the plugin and preset counts.
4. Push to `main`. CI builds both installers and publishes them as the next release (v1.3, v1.4, ...).
   The page's download buttons link to `releases/latest`, so they serve the new installer as soon as the release is up.

## Build the installer (Windows)

Needs Visual Studio 2022 (C++ workload), CMake and Git.

```
powershell -ExecutionPolicy Bypass -File build_bundle.ps1
```

Output: `dist\SKAZI Bundle Setup.exe`, plus every plugin's VST3 and app in `dist\Plugins`.
JUCE is downloaded automatically (8.0.4 and 8.0.15, the versions each plugin was built with).
`-PluginsOnly` builds just the plugins, `-InstallerOnly` re-packs the last built plugins.

## What the installer does

- Lets you tick which plugins to install
- VST3s go to `C:\Program Files\Common Files\VST3\SKAZI\`
- Standalone apps go to `C:\Program Files\SKAZI\` with a SKAZI Start Menu folder (desktop shortcuts optional)
- Removes older separate installs of the same plugins (SKAZI 1.2, SHAKSUKA, The FilterFish) so nothing shows up twice
- Shows up as "SKAZI Bundle" in Windows "Add or remove programs"; the same exe uninstalls (`/uninstall`)
- `/silent` installs everything without a window

The installer is not code-signed, so Windows SmartScreen shows "Windows protected your PC":
click **More info, then Run anyway**.

## Logo

Save the SKAZI logo as `Resources\skazi_logo.png` (and in each plugin's `Resources` folder) and rebuild;
until then a drawn stand-in is used.

## Mac (OS X / macOS)

`mac/build_mac.sh` builds all plugins for Apple Silicon + Intel and makes **`dist/SKAZI Bundle.pkg`**:

- VST3 to `/Library/Audio/Plug-Ins/VST3/SKAZI/`
- Audio Units to `/Library/Audio/Plug-Ins/Components/` (Logic, GarageBand, Ableton)
- Apps to `/Applications/SKAZI/`, with **Uninstall SKAZI Bundle.command**

It must run on a Mac (`bash mac/build_mac.sh`). Without a Mac, push this folder to GitHub:
`.github/workflows/build-bundle.yml` builds the Mac `.pkg` and the Windows `.exe` on GitHub's
machines; download them from the run's **Artifacts**. On a Linux host, run
`bash linux/build_linux.sh` to create both `dist/SKAZI Bundle Linux.tar.gz` and
`dist/SKAZI Bundle Linux.deb`. The `.deb` installs the VST3 plugins under `/usr/lib/vst3`
and the standalone applications under `/usr/lib/skazi-bundle`. CI builds on Ubuntu 22.04
(glibc 2.35), a Debian-family compatibility baseline for newer Ubuntu and Debian systems;
the tarball is available when a distro-specific package is not suitable. Pushing a tag such
as `v1.1` runs all three builds and creates a GitHub Release with the Mac, Windows and
Linux packages.

For Mac builds, `MAC_ARCH=universal` creates one package for Apple Silicon and Intel;
`MAC_ARCH=arm64` or `MAC_ARCH=x86_64` creates an architecture-specific package. The
workflow's **Run workflow** form exposes the same three choices, while tagged releases use
the universal package.

The Mac installer is not signed with an Apple Developer ID, so the first time macOS blocks it:
right-click the `.pkg`, choose **Open**, then **Open** again (or System Settings > Privacy & Security > Open Anyway).

Before pushing to GitHub, remove the `SKAZI-Synth/.git` folder from this copy (the full history stays
in `Documents\ACIDANCE\ACIDANCE`), otherwise GitHub treats it as an empty sub-project.
