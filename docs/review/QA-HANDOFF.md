# Website handoff — 2 November 2026

The new design is the primary page at `docs/index.html`; `/demo/` uses the same client assets and remains available. This report covers revision 37 of the website. The branch is merged with upstream `main`; its FilterFish changes and promo assets are retained.

## What was verified

- **Stable surfaces:** the sticky header is solid and the dark dialog overlay is unblurred. The DAW and software setup panel no longer animate their entire surfaces on entry. Hovered controls use no brightness filters or lift transforms; software cards keep stable selected/idle backgrounds during rapid pointer movement. Build and WAV save are adjacent 44px icon-only actions with accessible labels and tooltips.

- **24 axe-core 4.14 cases:** Mixer, Pattern, Device, plugin dialog, installation/Windows notice and opening at 1440, 768, 390 and 320 CSS pixels. Zero reported violations for WCAG A/AA tags through 2.2 and best practices. Raw results are in [qa/](qa/).
- **All 19 plugin dialogs:** opened from the new root page; all full-size images loaded. Escape closed them and returned focus. No captured browser warnings, errors or CSP violations.
- **All seven DAW choices:** guidance updates correctly. The software name and all step numbers stay exactly `#e50914`; the software name uses 19px bold text so this exact red meets the large-text contrast threshold.
- **Responsive layout:** no document overflow at 320/390px; the arrangement scrolls within its own container. Hero download SVG centre offset is 0px. The mobile hero separates the logo and portrait. Studio One's white image corners are clipped to the original rounded icon silhouette.
- **Selection and hover:** only the selected arrangement row, its clips and matching mixer strip use the red selection colour. Enabled power buttons remain red to indicate audible channels. Device expansion is at the side, hidden until hover/focus on desktop, available on touch. Social icons turn Netflix red on hover; text remains readable.
- **Transport:** Play → Pause → Resume retains the audio clock; Stop rewinds and hides itself. Preset/tempo changes and builds enter on bar boundaries. Solo, mute, levels and the four shaping controls work.
- **Pattern:** select/add notes, Shift-click/Delete removal, pitch for melodic tracks, per-step velocity/length, acid accent/slide, Clear, Reset and Undo. Clip previews reflect the sequence. Edits reach the live engine and WAV rendering.
- **WAV export:** an actual stereo 44.1kHz/16-bit download was saved and inspected. It contains four bars plus an effects tail and respects mute/solo, notes, levels, tempo and shaping controls. [Example](studio-preview.wav).
- **Audio rendering:** seven native `OfflineAudioContext` renders cover three presets, all controls at maximum, dense maximum-length/velocity edits, explicit note/articulation edits and full mute. All samples were finite; no sample clipping. Peak was about −6dBFS for the default presets and −1.37dBFS in the dense maximum case. Full mute produced digital silence. See [audio-render.json](qa/audio-render.json).
- Local references, IDs, alt attributes, JavaScript syntax, audio lifecycle regression tests, intro lifecycle tests and musical sequence/WAV tests pass.

## Reproducing the checks

Serve `docs/` with a static HTTP server, then open `/demo/qa/` and run **Run all 24 cases**. Open `/demo/qa/audio.html` for the native audio renders. The included axe distribution retains its MPL 2.0 license in `demo/qa/AXE-LICENSE.txt`.

```powershell
node docs/demo/tests/audio-lifecycle.cjs
node docs/demo/tests/opening-lifecycle.cjs
node docs/demo/tests/studio-sequence.cjs
```

The root retains the existing Content Security Policy and social metadata. The studio is an original browser synthesizer; it does not run the native plugins or claim to reproduce their output. The master has a low-cut, compression, a soft saturation ceiling and output headroom. Processing and rendering use the browser's [Web Audio APIs](https://developer.mozilla.org/en-US/docs/Web/API/OfflineAudioContext); gain changes use [scheduled smoothing](https://developer.mozilla.org/en-US/docs/Web/API/AudioParam/setTargetAtTime).

## Limits

Tested in the Chromium-based Codex browser on Windows. These results do not constitute WCAG certification or a claim of release-master audio quality. Native screen readers, Safari/Firefox, physical touch devices, high-contrast OS modes, true-peak/loudness compliance and native installer execution were not tested. Some gradient/image contrast remains a manual-review case; not every cursor-reveal position was measured. Audit states normalize scroll position to avoid sticky-header partial-visibility artefacts. Keyboard focus was separately checked. The Instagram clip remains an external link. No project-wide reuse license is assigned by this update.

## Revision 38 follow-up

- Device artwork now has its own bounded frame. The 44 px expand control is positioned against that frame and retains the dialog, accessible name, keyboard focus and focus return. Verified in Chromium with the TAHINI dialog. [Device screenshot](device-frame-38.png).
- At 320 and 390 px portrait widths, the arrangement remains horizontally scrollable. At 844 × 390 landscape it fits in the visible studio width (777 px), so a small dismissible tip appears only while the studio is visible in portrait. Dismissal lasts the session. It does not lock orientation or prevent playback. [Portrait screenshot](rotate-tip-38.png).
- The mobile menu's old direction.css values used brown-tinted text, background and border colors. Final computed open-state values are neutral: menu text `rgb(255,255,255)`, nav background `rgb(20,20,20)`, links `rgb(238,238,238)`, border `rgb(75,75,75)`. Hover/focus use neutral gray and `#e50914`. [Menu screenshot](mobile-menu-38.png).
- The ticker now uses two equal measured cycles, including the joining gap. It fills wide viewports, measures again after font load or resize, and preserves progress when resized. Hover no longer pauses it. Intersection pausing, visual motion toggle and reduced motion remain. Copy is ordered: 19 FREE PLUGINS → PUBLIC SOURCE → MAKE YOUR SOUND → SKAZI.
- `audio-lifecycle.cjs`, `opening-lifecycle.cjs`, `studio-sequence.cjs`, JS syntax and diff whitespace checks passed. Visual QA used the Chromium-based Codex browser at 320 × 700, 390 × 844, 844 × 390 and default desktop size. Physical device rotation, Safari/Firefox and a frame-by-frame capture of a full ticker loop were not tested.
