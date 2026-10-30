# Website QA follow-up — 27 October 2026

## Resolved in this PR

- Hero sound is off on every page load. An explicit, labelled toggle enables/mutes it; clicking an artist identity is also an explicit play/pause action. Keyboard focus alone never starts playback. The plugin gallery remains silent.
- The installation guide receives focus on opening. Close and Escape restore focus to its trigger.
- The image lightbox makes the background inert and restores focus on Close/Escape. The original PNG is loaded for enlargement, not the compressed thumbnail.
- The opening Hero fills the viewport, with a minimal circular arrow linking to the gallery and moving keyboard focus there. The former Explore the plugins text link was removed. The Hero includes extra bottom breathing room (80px desktop, 56px mobile) beyond the viewport; the scroll cue stays within the opening screen. Short screens can grow to preserve content.
- Hero and all 15 plugin thumbnails use responsive WebP variants with intrinsic dimensions. Reel mode requests a larger candidate. Existing full-size PNG assets remain available. See image-optimization.json for source and variant sizes: source images total 4,967,572 bytes; small variants total 416,680 bytes (about 92% smaller). This is an asset-size comparison, not a measured page-load-speed claim.
- Instrument and effect badges use Netflix red (#E50914) with white text. Small artist captions are 11px. Guide and Reel corners now match the rounded design.

## Verified

Manual interaction checks in the in-app Chromium browser and Chrome:

- Sound starts disabled; enable plays, mute pauses.
- Guide opening focuses Close; Escape returns to the guide trigger.
- Image enlargement works with pointer and Enter; Escape closes it, removes inert and restores the image trigger.
- The intro runs on every document load and crossfades into the Hero logo; reloads use a shorter transition with a subdued red halo. The loading logo matches the Hero logo width and moves from screen center to its destination while crossfading when that destination is visible. Skip and reduced-motion support remain.
- Grid/Reel and keyboard End navigation reach QUAZI as item 15; browsing remains silent (in-app browser).
- 390px mobile: no horizontal overflow; gallery shortcut focuses the gallery. Viewport override reset after checking.
- No JavaScript errors captured during the tested interactions.
- Both standalone scripts and all inline scripts parse; git diff --check passes.

Calculated contrast ratios for selected opaque colour pairs:

| Text | Ratio |
| --- | --- |
| White instrument badge text on Netflix red | 4.79:1 |
| White effect badge text on Netflix red | 4.79:1 |
| Netflix red large heading on card (#171717) | 3.74:1 |
| Card description | 8.74:1 |
| White preset text on Netflix red | 4.79:1 |
| Inactive view selector | 7.05:1 |

These checks cover the revised pairs, not every possible rendered state.

## Still requires external validation

- Firefox and Safari / iOS Safari are not available through this environment's browser controls.
- No live NVDA, VoiceOver or other screen-reader session was run. Keyboard and accessibility-tree inspection are not substitutes for that test.
- A complete accessibility audit, every hover/focus contrast combination, zoom/text resizing, real-device touch and throttled-network performance testing remain release checks.
- Installer execution is outside this website QA pass. No claim of full WCAG certification or all-browser compatibility is made.

Intro playback no longer uses a stored first-visit preference.

## Phone layout and intro polish

- Artist image and audio controls are hidden at widths up to 600px; verified hidden at 390px and visible at 768px. Switching into phone layout disables and stops playback.
- Phone Hero retains downloads, DAW compatibility and guide, with no horizontal overflow in the checked 390px viewport.
- Intro adds layered red orbit rings and a soft logo reveal with a 2.1-second opening dwell and 650ms reload dwell. Reloads stay subdued; Skip and reduced-motion styles remain.

## Responsive hierarchy pass

- Checked 360, 390, 600, 768, 1024 and 1440px viewport widths in Chromium; no horizontal overflow in these checks. Phone artist remains hidden through 600px, visible at 768px.
- Grid uses one column through 650px, two through 1000px, then three. Tablet Hero uses balanced columns, constrained headings and wrapping download controls.
- Card titles and roles lead the reading order; descriptions, subdued feature tags and separated changelog rows use consistent spacing. Full descriptions remain visible. Changelog opening verified on narrow mobile.

## Conversion pass — 30 October 2026

- Sticky site bar (logo, Plugins, Install guide, GitHub) with a download button that appears once the hero buttons scroll away; phones get a fixed bottom download bar instead. The hero's own guide button was removed because the bar's Install guide link replaces it.
- The opening sequence runs once per browser session (sessionStorage key `skazi-intro-seen`), with Skip and reduced-motion support unchanged.
- Hero is capped at one viewport: logo height scales with the screen, the stats strip was folded into a proof line under the lead, and the empty band below the content is gone.
- Download buttons follow the visitor's OS (matching button first and red), show installer size inside the button, and show version and release date underneath. Values are static fallbacks refreshed from the GitHub releases API when reachable. The SmartScreen note (Windows) or right-click-to-open note (Mac) sits directly under the buttons.
- Each of the 13 cards with a sketch has a PREVIEW button playing `audio/<name>.mp3` (encoded from the generated WAVs, about 200 KB each). Starting a card preview stops the artist track and vice versa. The previews are original sketches made for the page, not plugin renders, and the gallery says so.
- Checked in the in-app Chromium at 1440×900 and 375×812: intro once per session, sticky bar and bottom bar, OS swap (simulated Mac), preview play/stop with live-region status, lightbox inert handling, no console errors, no horizontal overflow.

## Hero control, plugin details and post-download notice — 30 October 2026

- Hero artist audio, its toggle and the mono half of the portrait were removed. The portrait is colour only, with SKAZI and ASHER SWISSA as static captions.
- Windows and Mac downloads are one segmented hero control: the visitor's OS is highlighted, hovering the other side previews it, a click makes it the choice and starts that download. The header carries separate Windows and Mac buttons once the hero control scrolls away; phones keep the bottom bar.
- Card sound previews and their MP3 clips were removed. Clicking a card (image, text or the What's new button, Enter on the focused image) opens a dialog with the full screenshot, a What's new section (the card's changelog, or "No changes in this release.") and Highlights. Escape and Close restore focus to the trigger; the page behind is inert. The old image-only lightbox is gone.
- The SmartScreen text now appears as a dismissible notice after a Windows download starts; Mac downloads show the right-click-to-open note. The inline hero notes were removed.

### QA pass (in-app Chromium)

Checked at 1440×900, 1366×768, 1024×768, 768×1024, 375×812 and 360×740: hero fits one viewport on every desktop size, no horizontal overflow at any width, no console errors, every local asset referenced by the page resolves, no duplicate ids, every aria-controls / aria-labelledby / hash link target exists, all images have alt text, both inline scripts and both external scripts parse. Intro plays once per session. Grid and Reel both open the details dialog; Reel arrows still advance. Guide opens from the header and Escape returns focus to the header button. Download clicks were intercepted during testing so no installer was fetched.

### Findings outside this pass

- **The live release is ahead of this page.** GitHub release v1.13 lists 19 plugins; the page and this branch have 15. Missing on the page: SKACID, SKILTER, SKWIDE and SKAZI REVERB. The "15 plugins" and "8,168 presets" claims and the page title are therefore stale against the current installer. This branch has no screenshots or copy for the four, so they were not added.
- Hover-state rendering could not be captured by the browser tooling; the hover swap on the hero control is verified by its CSS and by screenshots taken while the pointer rested on a segment, not by an automated assertion.
- The DAW icons remain seven focusable list items with no action (pre-existing). Firefox and Safari were not available.
- The two artist tracks in docs/audio (skazi-junam.mp3, asher-imani.mp3) are no longer referenced and can be deleted; the generated WAV sketches are untracked and unused.
