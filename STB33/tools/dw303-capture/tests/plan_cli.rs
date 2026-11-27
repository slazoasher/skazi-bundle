//! CLI dry runs must not enumerate or open hardware, even with bogus device IDs.
use std::{path::PathBuf, process::Command};
#[test]
fn plan_cli_is_device_free_and_reports_order() {
    let manifest =
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../reference/captures/manifest.toml");
    let result = Command::new(env!("CARGO_BIN_EXE_dw303-capture"))
        .arg("plan")
        .arg(&manifest)
        .args([
            "--select",
            "pitch-k24-t50,pitch-k25-t50",
            "--audio-device",
            "DO-NOT-OPEN",
            "--midi-out",
            "DO-NOT-OPEN",
        ])
        .output()
        .unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    let output = String::from_utf8(result.stdout).unwrap();
    assert!(output.contains("2 captures; 1 panel prompts"));
    assert!(output.find("1/2 pitch-k24-t50").unwrap() < output.find("2/2 pitch-k25-t50").unwrap());
}
#[test]
fn plan_cli_rejects_unknown_ids_without_devices() {
    let manifest =
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../reference/captures/manifest.toml");
    let result = Command::new(env!("CARGO_BIN_EXE_dw303-capture"))
        .arg("plan")
        .arg(manifest)
        .args(["--select", "absent*"])
        .output()
        .unwrap();
    assert!(!result.status.success());
    assert!(String::from_utf8_lossy(&result.stderr).contains("unmatched selector"));
}
