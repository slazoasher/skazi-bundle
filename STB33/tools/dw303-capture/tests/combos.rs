//! Session 2 coverage, duration and exact-repeat contracts; no hardware opened.
use dw303_capture::{Manifest, session};
use std::{collections::BTreeSet, path::PathBuf, process::Command};

fn manifest() -> Manifest {
    Manifest::parse(include_str!("../../../reference/captures/manifest.toml")).unwrap()
}

#[test]
fn interaction_session_coverage_and_budget() {
    let manifest = manifest();
    let session = session::SessionFile::parse(
        include_str!("../../../reference/captures/sessions/03-combos.toml"),
        &manifest,
    )
    .unwrap();
    assert_eq!(session.ids.len(), 274);
    let captures = session::order(session::select(&manifest, "*", Some(&session)).unwrap());
    let (prompts, seconds) = session::estimate(&captures);
    assert_eq!(prompts, 71);
    assert!((45.0..=50.0).contains(&(seconds / 60.0)));
    assert_eq!(seconds, 2915.0);
    assert_eq!(manifest.mapping.channel, 14);
    let mut grid = BTreeSet::new();
    let mut decay = BTreeSet::new();
    let mut accent = BTreeSet::new();
    let mut tuning = BTreeSet::new();
    for capture in &captures {
        assert!(!capture.clock);
        assert_eq!(capture.tempo, 120.0);
        assert_eq!(capture.settings["volume"].as_float(), Some(1.0));
        assert_eq!(capture.settings["waveform"].as_str(), Some("saw"));
        assert!(capture.notes.iter().all(|note| {
            (24.0..=72.0).contains(&note[1])
                && note[0] >= 0.0
                && (note[0] + note[2]) * 60.0 / capture.tempo <= capture.seconds
        }));
        let position =
            |key: &str| (capture.settings[key].as_float().unwrap() * 100.0).round() as u32;
        let key = capture.notes[0][1] as u32;
        if capture.id.starts_with("combo-") {
            assert_eq!(position("accent"), 0);
            let phrase = capture.id.ends_with("-phrase");
            assert_eq!(capture.notes.len(), if phrase { 8 } else { 1 });
            assert!(
                capture
                    .notes
                    .iter()
                    .all(|n| n[1] == f64::from(key) && n[3] == 80.0)
            );
            if capture.id.starts_with("combo-decay-") {
                assert_eq!(position("cutoff"), 40);
                assert_eq!(position("env_mod"), 60);
                decay.insert((position("decay"), position("resonance"), key, phrase));
            } else {
                assert_eq!(position("decay"), 50);
                grid.insert((
                    position("cutoff"),
                    position("env_mod"),
                    position("resonance"),
                    key,
                    phrase,
                ));
            }
            assert!(
                capture
                    .notes
                    .windows(2)
                    .all(|n| n[0][0] + n[0][2] < n[1][0])
            );
        } else if capture.id.starts_with("accent2-") {
            assert_eq!(position("cutoff"), 40);
            assert_eq!(position("env_mod"), 60);
            assert_eq!(position("decay"), 50);
            let velocity = if capture.id.ends_with("-v80") {
                80
            } else {
                120
            };
            assert!(
                capture
                    .notes
                    .iter()
                    .all(|n| n[1] == 48.0 && n[3] == f64::from(velocity))
            );
            accent.insert((
                position("accent"),
                position("resonance"),
                capture.notes.len(),
                velocity,
            ));
        } else if capture.id.starts_with("tuning2-") {
            assert_eq!(position("cutoff"), 100);
            assert_eq!(position("resonance"), 0);
            assert_eq!(position("env_mod"), 0);
            assert_eq!(position("decay"), 0);
            assert_eq!(position("accent"), 0);
            tuning.insert((position("tuning"), key));
        } else {
            assert!(capture.id.starts_with("repeat-"));
        }
    }
    for resonance in [0, 50, 80, 100] {
        for key in [36, 48] {
            for phrase in [false, true] {
                for cutoff in [20, 40, 60] {
                    for env_mod in [0, 60, 100] {
                        assert!(grid.contains(&(cutoff, env_mod, resonance, key, phrase)));
                    }
                }
                for knob in [0, 25, 75, 100] {
                    assert!(decay.contains(&(knob, resonance, key, phrase)));
                }
                // Mid decay is already covered by the three-way grid.
                assert!(grid.contains(&(40, 60, resonance, key, phrase)));
            }
        }
        for count in [1, 4] {
            for knob in [0, 25, 50, 75, 100] {
                assert!(accent.contains(&(knob, resonance, count, 120)));
            }
            assert!(accent.contains(&(0, resonance, count, 80)));
        }
    }
    for knob in [25, 75] {
        for key in [24, 48, 72] {
            assert!(tuning.contains(&(knob, key)));
        }
    }
}

#[test]
fn repeats_preserve_original_stimulus_and_panel() {
    let manifest = manifest();
    for name in ["classic", "accent-slide", "rests-ties"] {
        let original = manifest
            .captures
            .iter()
            .find(|c| c.id == format!("pattern-{name}"))
            .unwrap();
        for take in 1..=4 {
            let repeat = manifest
                .captures
                .iter()
                .find(|c| c.id == format!("repeat-{name}-{take:02}"))
                .unwrap();
            assert_eq!(repeat.settings, original.settings);
            assert_eq!(repeat.notes, original.notes);
            assert_eq!(repeat.seconds, original.seconds);
            assert_eq!(repeat.tempo, original.tempo);
            assert_eq!(repeat.clock, original.clock);
            assert_eq!(
                repeat.schedule(96000, &manifest.mapping),
                original.schedule(96000, &manifest.mapping)
            );
        }
    }
}

#[test]
fn combos_plan_cli_reports_curated_budget_without_devices() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
    let result = Command::new(env!("CARGO_BIN_EXE_dw303-capture"))
        .arg("plan")
        .arg(root.join("reference/captures/manifest.toml"))
        .args(["--select", "*", "--session-file"])
        .arg(root.join("reference/captures/sessions/03-combos.toml"))
        .args(["--audio-device", "DO-NOT-OPEN", "--midi-out", "DO-NOT-OPEN"])
        .output()
        .unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    let output = String::from_utf8(result.stdout).unwrap();
    assert!(output.contains("274 captures; 71 panel prompts; estimated 48.6 minutes"));
    assert!(!output.contains("manual pattern setup"));
}
