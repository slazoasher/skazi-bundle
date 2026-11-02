# Website handoff — 2 November 2026

The new design is the primary page at `docs/index.html`; `/demo/` uses the same client assets and remains available. The initial report covers revision 37; revision 38 and 39 follow-ups are recorded below. The branch is merged with upstream `main`; its FilterFish changes and promo assets are retained.

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

Serve `docs/` with a static HTTP server, then open `/demo/qa/` and run **Run all 28 cases**. Open `/demo/qa/audio.html` for the native audio renders. The included axe distribution retains its MPL 2.0 license in `demo/qa/AXE-LICENSE.txt`.

```powershell
node docs/demo/tests/audio-lifecycle.cjs
node docs/demo/tests/opening-lifecycle.cjs
node docs/demo/tests/studio-sequence.cjs
```

The root retains the existing Content Security Policy and social metadata. The revision 37 studio used original browser synthesis. Revision 42, documented below, introduces recordings rendered from three native plugins while retaining browser processing for the remaining parts. The master has a low-cut, compression, a soft saturation ceiling and output headroom. Processing and rendering use the browser's [Web Audio APIs](https://developer.mozilla.org/en-US/docs/Web/API/OfflineAudioContext); gain changes use [scheduled smoothing](https://developer.mozilla.org/en-US/docs/Web/API/AudioParam/setTargetAtTime).

## Limits

Tested in the Chromium-based Codex browser on Windows. These results do not constitute WCAG certification or a claim of release-master audio quality. Native screen readers, Safari/Firefox, physical touch devices, high-contrast OS modes, true-peak/loudness compliance and native installer execution were not tested. Some gradient/image contrast remains a manual-review case; not every cursor-reveal position was measured. Audit states normalize scroll position to avoid sticky-header partial-visibility artefacts. Keyboard focus was separately checked. The Instagram clip remains an external link. No project-wide reuse license is assigned by this update.

## Revision 38 follow-up

- Device artwork now has its own bounded frame. The 44 px expand control is positioned against that frame and retains the dialog, accessible name, keyboard focus and focus return. Verified in Chromium with the TAHINI dialog. [Device screenshot](device-frame-38.png).
- At 320 and 390 px portrait widths, the arrangement remains horizontally scrollable. At 844 × 390 landscape it fits in the visible studio width (777 px), so a small dismissible tip appears only while the studio is visible in portrait. Dismissal lasts the session. It does not lock orientation or prevent playback. [Portrait screenshot](rotate-tip-38.png).
- The mobile menu's old direction.css values used brown-tinted text, background and border colors. Final computed open-state values are neutral: menu text `rgb(255,255,255)`, nav background `rgb(20,20,20)`, links `rgb(238,238,238)`, border `rgb(75,75,75)`. Hover/focus use neutral gray and `#e50914`. [Menu screenshot](mobile-menu-38.png).
- The ticker now uses two equal measured cycles, including the joining gap. It fills wide viewports, measures again after font load or resize, and preserves progress when resized. Hover no longer pauses it. Intersection pausing, visual motion toggle and reduced motion remain. Copy is ordered: 19 FREE PLUGINS → PUBLIC SOURCE → MAKE YOUR SOUND → SKAZI.
- `audio-lifecycle.cjs`, `opening-lifecycle.cjs`, `studio-sequence.cjs`, JS syntax and diff whitespace checks passed. Visual QA used the Chromium-based Codex browser at 320 × 700, 390 × 844, 844 × 390 and default desktop size. Physical device rotation, Safari/Firefox and a frame-by-frame capture of a full ticker loop were not tested.


## Revision 39 — compact studio and Advanced workspace

- The initial workspace keeps transport, five tracks with synchronized level controls and a compact live master spectrum visible. Detailed Pattern, Mixer and Sound panels are in a closed-by-default **Advanced** disclosure. Only one panel is shown at a time. At the checked desktop size, the initial studio is 685px high versus 1203px with Pattern open (43% shorter). This compares the two new states, not the previous release.
- Clicking a clip opens Pattern for its track. Beat markers and a side inspector make pitch, velocity and length easier to read; the inspector stacks below the internally scrolling pattern on phones. Acid accent/slide, Clear and Reset remain available. Genuine per-track Redo accompanies Undo, with scoped Ctrl/Cmd+Z, Shift+Ctrl/Cmd+Z and Ctrl+Y shortcuts; typing and slider keys are not intercepted.
- The Advanced open/closed state and selected panel are remembered for the visit, including reload while closed. Focus returns to the disclosure when a focused advanced control is collapsed. Hidden controls are absent from the rendered interface/tab order. The Edited indicator reflects musical changes, and compact/mixer levels address the same gain and solo routing.
- Closing Advanced does not reset notes, controls, levels or playback. **29 browser workflow assertions passed**, including actual playback, pause-position preservation, undo/redo branching, mixer synchronization and UI-state restoration. Two actual WAV exports captured the same musical snapshot; output lengths matched with a maximum difference of one 16-bit PCM unit between independent Web Audio renders. The harness allows two units to account for render quantization. [Raw workflow results](qa/compact-workflow-39.json).
- **28 axe-core 4.14 cases, zero reported violations:** compact, Mixer, Pattern, Sound, plugin dialog, installation/Windows notice and opening at 1440/768/390/320px. This rerun includes the new disclosure and larger inline-slider hit targets. [Raw accessibility results](qa/axe-39.json).
- No document overflow at 320, 390, 768 or 1440px. The mobile transport has separate 44px playback, save and build controls with no BPM/export overlap. Timeline controls remain visible while scrolling; the landscape arrangement fits at 844×390 (777px container/content). Small-screen spectrum labels use the two readable endpoints. [Responsive measurements](qa/compact-responsive-39.json).
- Both root and `/demo/` routes load the updated assets and plugin images. No captured warnings/errors in the final root preview. Three existing regression suites, JavaScript syntax and HTML ID/div checks passed. [Compact studio](compact-studio-39.png), [Advanced Pattern](advanced-pattern-39.png), [Mobile](compact-mobile-39.png).

The change preserves the existing browser synthesis and native-audio request in PR 8. Original plugin samples, full draft recovery across reload, and additional mixer routing are later work rather than claims made by this update. The Chromium/physical-device/screen-reader/browser-zoom limits above still apply; these automated results are not WCAG certification.


## Revision 40 — spacing and collision sweep

The Mixer selection stripe previously touched the tab strip because both panels shared the same edge. Advanced tabs now have separated 44px buttons; the dock has an inset gutter, and mixer channels are individual bordered cards with 10px gaps. The first mixer card starts 29px below the tabs on desktop and 25px on phones. [Desktop proof](mixer-spacing-40.png) · [320px proof](mixer-mobile-spacing-40.png).

Track rows now leave at least 6px between their power/solo controls and level sliders. Discrete transport, power/solo, articulation and Pattern editing actions have 8px gaps. The narrowest phones place session utility actions on a separate row to preserve readable presets and 44px controls. Advanced remains closed by default.

- **72 layout cases passed:** compact, paused with Stop visible, help, edited acid Pattern, Mixer and Sound at 320×700, 360×780, 390×844, 650×800, 651×800, 768×1024, 900×900, 901×900, 1024×768, 1280×720, 1440×900 and 844×390.
- The geometry audit checks control/control, text/control and text/text intersections, accounting for intentional internal scrolling and sticky track headers. No intersections above the 1px rounding tolerance and no document overflow were reported. This is a geometric check plus manual visual inspection, not a claim to verify every possible browser/font/hover state. [Raw spacing results](qa/spacing-40.json).
- **28 accessibility cases passed with zero reported violations**, using the existing axe-core 4.14 suite against the new CSS. [Raw accessibility results](qa/axe-40.json). Arrow-key panel navigation was also checked at 320px; focus and selected panel move together.

Reproduce the spacing checks at `/demo/qa/spacing.html` with **Check studio spacing**. Public audio/editing logic is unchanged; the layout harness starts and pauses playback to exercise the conditional Stop button. Testing remains Chromium on Windows rather than a physical-device or cross-browser certification.


## Revision 41 — shared transport and plugin-inspired identity

- A single fixed playback region now holds full-width Play, equal Pause/Stop halves while playing, and equal Resume/Stop halves when paused. Stop returns to full-width Play. The CSS grid interpolates over 260ms; colors and the playback symbol change softly. The surrounding clock, tempo and preset positions remain stable. Desktop region: 176×46px, with two 87×44px targets when split. Mobile region: 96×46px, with two 47×44px targets.
- Hidden Stop remains inert, disabled, `aria-hidden` and outside the tab order during its visual fade-out. Stopping from its keyboard focus returns focus to Play. Existing audio clock and asynchronous-start safeguards remain intact.
- The original SKAZI artwork replaces the small asterisk in the studio title, in a dark badge inspired by the plugin headers. The existing palette, channel layout and Advanced workspace remain. A restrained light sweep appears on the badge only during visible playback, stops offscreen/paused/stopped, and follows the site's visual-motion control. Reduced-motion rules remove these animations/transitions; native reduced-motion preference switching was not emulated in this run.
- **90 transport assertions passed** across 320×700, 390×844, 768×1024, 1280×720 and 1440×900. Actual animation frames verify interpolated opening/closing, equal final halves, stable neighboring controls, no segment overlap, focus, inactive Stop semantics and logo visibility/motion behavior. The QA fixture explicitly scrolls both its outer frame and inner studio into view for IntersectionObserver checks. [Transport results](qa/transport-41.json), reproducible at `/demo/qa/transport.html`.
- **72 layout cases passed** with no reported collisions or document overflow. **28 axe-core cases reported zero violations.** **29 editing/audio/export workflow assertions passed**, including matching export snapshots and equivalent independent WAV renders. [Spacing](qa/spacing-41.json) · [Accessibility](qa/axe-41.json) · [Workflow](qa/compact-workflow-41.json).
- All three Node regression suites passed, including extended transport accessibility-state assertions. Root and `/demo/` load the correct assets and logo. Desktop and mobile screenshots were visually inspected; no captured warnings/errors in the final demo preview.

[Full-width Play](studio-ready-41.png) · [Split playback](studio-playing-41.png) · [Mobile](studio-mobile-41.png).

[Hebrew product evolution plan](DAW-EVOLUTION.md) describes the next stages: durable session recovery, project files and session-wide history; independent bar editing/Piano Roll; original plugin audio and real mixer routing; accessibility and cross-browser usability work. Those future capabilities are explicitly separated from what revision 41 implements. The earlier Chromium/physical-device/native-screen-reader limits still apply.


## Revision 42 — original native plugin voices

Kick, Bass and Acid now use 128 recordings rendered from the actual KICKASS, SUBICH and SKACID Windows VST3 binaries in the official v1.15 release (source commit `58dced012df87ad3d977a49cc14685d5a01c1db0`). The installer was extracted as a ZIP payload without executing it or installing plugins system-wide. Its computed SHA256 matches the digest supplied by GitHub's release API. Binary/asset hashes, all native parameter values and per-sample offsets are recorded in [the manifest](../audio/native/manifest.json).

- **128 native renders:** three Kick velocities; Bass MIDI 24–96 every three semitones at three velocities; Acid at the same pitches with native normal/accent recordings. All renders are finite and non-silent. Three stereo 48kHz/24-bit FLAC sprites total **1,974,441 bytes**. Round-trip decoding differs by at most one 24-bit quantization step. [Render report](../audio/native/render-report.json).
- **Shared playback/export:** the same native-bank selector, velocity crossfades and note index are used live and in OfflineAudioContext. First Play/export loads the bank; concurrent loads share a promise, failed loads can be retried, and Kick/Bass/Acid never silently fall back to the old synthesized voices. The old synthetic implementations for these three channels were removed.
- **Processing boundaries:** native VST3s run during the offline asset-rendering step, not inside the browser. Website-created patches are documented and are not claimed as factory presets or artist-approved settings. Browser processing handles nearby-note repitching, gate, slides, tone/drive/delay/mix; Space and percussion remain browser synthesis. TAHINI and SALAT are not yet native processors in this website. This is stated in the studio help and review guide. External concept-preview buttons remain concept previews.
- **Master correction:** an 8Hz high-pass after nonlinear processing removes DC that appeared in the dense maximum-control render. Final output headroom is conservative. Eight actual offline renders cover the three profiles, maximum controls, dense maximum edits, explicit pitch/velocity/length/articulation edits full mute and alternating full-range pitches. All finite, no sample clipping; full mute is digital silence. Tested default peaks are about −3.59/−3.25/−6.22dBFS, with the dense maximum case about −1.32dBFS. [Raw measurements](qa/audio-render-42.json). These are sample peaks, not a true-peak/LUFS certification.
- A real **stereo 44.1kHz/16-bit WAV export** was downloaded and inspected: 8.261 seconds, four bars plus the effects tail. [Listen to the native-voice sketch](studio-native-preview.wav).
- The workflow export-equivalence check now bounds both peak error (four 16-bit PCM units) and RMS error (one unit) between independent Web Audio effect graphs. Equal musical snapshots and equal output lengths are still required; the precise comparison is reported rather than claiming bit-identical browser DSP.

[Reproduction, pinned dependencies and musical limits](../tools/native-audio/README.md). The renderer uses the original native binaries; it does not recreate their DSP in Python. No installer, plugin binary or Python runtime is committed. Physical devices, Safari/Firefox and native screen readers remain outside this run's verification scope.

Revision 42 regression results: **90 transport assertions, 72 layout cases, 29 workflow assertions, 28 axe-core cases with zero violations, and four Node suites passed**. The full-pitch stress case checks finite output, clipping and DC; its high-frequency adjacent-sample slope is reported rather than judged against the bass-pattern slope limit. [Studio screenshot](studio-native-42.png).
