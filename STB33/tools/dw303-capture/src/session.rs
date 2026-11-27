//! Device-free selection, panel ordering, estimates and conservative resume checks.
use crate::{Capture, Manifest, json, quote};
use std::{collections::HashSet, fs, io::Read, path::Path};
use toml::Value;

/// Automatic inter-capture settling interval, in seconds.
/// VERIFY: one second is an operator estimate, not measured circuit settling.
pub const SETTLE_SECONDS: f64 = 1.0;
/// Estimated operator time per panel confirmation, in seconds.
/// VERIFY: actual adjustment time depends on the operator and panel template.
pub const PROMPT_SECONDS: f64 = 15.0;
const PANEL: [&str; 8] = [
    "tuning",
    "cutoff",
    "resonance",
    "env_mod",
    "decay",
    "accent",
    "volume",
    "waveform",
];

/// Explicit manifest IDs for a curated measurement session.
#[derive(Debug)]
pub struct SessionFile {
    /// Human-readable name, shown in plans.
    pub name: String,
    /// Unique, exact manifest IDs. Manifest order governs ties.
    pub ids: Vec<String>,
}
impl SessionFile {
    /// Parse `name = "..."` and `captures = ["id", ...]`; reject unknown IDs.
    pub fn parse(source: &str, manifest: &Manifest) -> Result<Self, String> {
        let value = toml::from_str::<Value>(source).map_err(|e| e.to_string())?;
        let name = value
            .get("name")
            .and_then(Value::as_str)
            .ok_or("missing session name")?
            .to_owned();
        let mut seen = HashSet::new();
        let ids = value
            .get("captures")
            .and_then(Value::as_array)
            .ok_or("missing session captures")?
            .iter()
            .map(|v| {
                let id = v.as_str().ok_or("session ID must be a string")?;
                if !manifest.captures.iter().any(|c| c.id == id) || !seen.insert(id.to_owned()) {
                    return Err(format!("unknown/duplicate session ID: {id}"));
                }
                Ok(id.to_owned())
            })
            .collect::<Result<Vec<_>, String>>()?;
        if ids.is_empty() {
            return Err("empty session file".into());
        }
        Ok(Self { name, ids })
    }
}
// Bounded wildcard matcher: '*' and '?' only; plain selectors are ID prefixes.
fn matches(id: &str, pattern: &str) -> bool {
    if !pattern.contains(['*', '?']) {
        return id.starts_with(pattern);
    }
    let mut row = vec![false; id.len() + 1];
    row[0] = true;
    for p in pattern.bytes() {
        let mut next = vec![false; row.len()];
        if p == b'*' {
            next[0] = row[0];
        }
        for (i, b) in id.bytes().enumerate() {
            next[i + 1] = if p == b'*' {
                row[i + 1] || next[i]
            } else {
                row[i] && (p == b'?' || p == b)
            };
        }
        row = next;
    }
    row[id.len()]
}
/// Select a union of comma-separated prefixes/globs, intersected with optional IDs.
/// Each selector must match at least one manifest entry. Never opens devices.
pub fn select<'a>(
    manifest: &'a Manifest,
    selection: &str,
    session: Option<&SessionFile>,
) -> Result<Vec<&'a Capture>, String> {
    let patterns: Vec<_> = selection.split(',').map(str::trim).collect();
    for p in &patterns {
        if p.is_empty() || !manifest.captures.iter().any(|c| matches(&c.id, p)) {
            return Err(format!("empty/unmatched selector: {p}"));
        }
    }
    let selected: Vec<_> = manifest
        .captures
        .iter()
        .filter(|c| {
            patterns.iter().any(|p| matches(&c.id, p))
                && session.is_none_or(|s| s.ids.contains(&c.id))
        })
        .collect();
    if selected.is_empty() {
        return Err("selection is empty after session-file intersection".into());
    }
    Ok(selected)
}
/// Number of panel controls changed (waveform counts as one).
pub fn distance(a: &Capture, b: &Capture) -> usize {
    PANEL
        .iter()
        .filter(|key| {
            let a = &a.settings[**key];
            let b = &b.settings[**key];
            match (super::number(a), super::number(b)) {
                (Some(a), Some(b)) => a != b,
                _ => a != b,
            }
        })
        .count()
}
/// Group identical panels, preserving manifest order inside each group, then
/// greedily choose the nearest unused group. Start at the first selected entry;
/// equal distances choose the group's first manifest entry. This is a heuristic,
/// not a globally optimal travelling-salesperson solution.
pub fn order(selected: Vec<&Capture>) -> Vec<&Capture> {
    let mut groups: Vec<Vec<&Capture>> = Vec::new();
    for capture in selected {
        if let Some(group) = groups.iter_mut().find(|g| distance(g[0], capture) == 0) {
            group.push(capture);
        } else {
            groups.push(vec![capture]);
        }
    }
    let mut result = Vec::new();
    let mut current = None;
    while !groups.is_empty() {
        let index = current.map_or(0, |previous| {
            groups
                .iter()
                .enumerate()
                .min_by_key(|(_, g)| distance(previous, g[0]))
                .map_or(0, |(i, _)| i)
        });
        let group = groups.remove(index);
        current = Some(group[0]);
        result.extend(group);
    }
    result
}
/// Panel prompts and estimated seconds, including pre/post-roll, settling and
/// 15 seconds per panel prompt. Excludes one-time metadata/pattern setup.
pub fn estimate(captures: &[&Capture]) -> (usize, f64) {
    let prompts = captures
        .iter()
        .enumerate()
        .filter(|(i, c)| *i == 0 || distance(captures[i - 1], c) != 0)
        .count();
    (
        prompts,
        captures.iter().map(|c| c.seconds + 1.5).sum::<f64>()
            + captures
                .windows(2)
                .filter(|pair| distance(pair[0], pair[1]) == 0)
                .count() as f64
                * SETTLE_SECONDS
            + prompts as f64 * PROMPT_SECONDS,
    )
}
/// Stable dry-run report suitable for printing without native device enumeration.
pub fn plan(captures: &[&Capture]) -> String {
    let (prompts, seconds) = estimate(captures);
    let mut report = format!(
        "{} captures; {prompts} panel prompts; estimated {:.1} minutes (15 s/prompt, 1 s settle; excludes warmup/metadata).\n",
        captures.len(),
        seconds / 60.0
    );
    for (i, capture) in captures.iter().enumerate() {
        let changes = if i == 0 {
            PANEL.len()
        } else {
            distance(captures[i - 1], capture)
        };
        report.push_str(&format!(
            "{}/{} {} ({:.1} s; {changes} panel changes{})\n",
            i + 1,
            captures.len(),
            capture.id,
            capture.seconds,
            if capture.clock && capture.notes.is_empty() {
                "; manual pattern setup"
            } else {
                ""
            }
        ));
    }
    report
}
/// Changed controls only; the initial panel lists every control.
/// VERIFY: clock labels assume 300-degree travel from 7 o'clock to 5 o'clock.
pub fn panel_changes(previous: Option<&Capture>, capture: &Capture) -> Vec<String> {
    fn label(value: &Value) -> String {
        if let Some(n) = super::number(value) {
            let minutes = ((7.0 + 10.0 * n) * 60.0).round() as u32 % 720;
            let hour = minutes / 60;
            format!(
                "{:.2}/10 (~{}:{:02})",
                n * 10.0,
                if hour == 0 { 12 } else { hour },
                minutes % 60
            )
        } else {
            value.as_str().unwrap_or("?").into()
        }
    }
    PANEL
        .iter()
        .filter_map(|key| {
            let new = &capture.settings[*key];
            if previous.is_some_and(|p| {
                let old = &p.settings[*key];
                old == new
                    || super::number(old)
                        .zip(super::number(new))
                        .is_some_and(|(a, b)| a == b)
            }) {
                return None;
            }
            Some(format!(
                "{key}: {} → {}",
                previous.map_or_else(|| "unset".into(), |p| label(&p.settings[*key])),
                label(new)
            ))
        })
        .collect()
}
/// Skip only valid complete PCM24 mono files matching this ID, settings and
/// requested rate/duration. Partial, corrupt and invalid pairs remain pending.
/// Existing sidecars produced before the `complete` field are supported.
pub fn complete(out: &Path, capture: &Capture, rate: u32) -> bool {
    fn check(out: &Path, capture: &Capture, rate: u32) -> Option<()> {
        let text = fs::read_to_string(out.join(format!("{}.json", capture.id))).ok()?;
        let fields = json_fields(&text)?;
        let field = |key: &str| {
            fields
                .iter()
                .find(|(k, _)| k == key)
                .map(|(_, v)| v.as_str())
        };
        let frames = ((capture.seconds + 1.5) * f64::from(rate)).round() as u64;
        if field("capture_id")? != quote(&capture.id)
            || field("valid")? != "true"
            || field("sample_rate")?.parse::<u32>().ok()? != rate
            || field("frames")?.parse::<u64>().ok()? != frames
            || field("settings")? != json(&capture.settings)
            || field("complete").is_some_and(|v| v != "true")
        {
            return None;
        }
        let mut wav = fs::File::open(out.join(format!("{}.wav", capture.id))).ok()?;
        let mut header = [0; 44];
        wav.read_exact(&mut header).ok()?;
        let u32_at = |i| u32::from_le_bytes(header[i..i + 4].try_into().ok().unwrap_or([0; 4]));
        let bytes = frames.checked_mul(3)?;
        if &header[..4] != b"RIFF"
            || &header[8..20] != b"WAVEfmt \x10\0\0\0"
            || header[20..24] != [1, 0, 1, 0]
            || u32_at(24) != rate
            || u32_at(28) as u64 != u64::from(rate) * 3
            || header[32..36] != [3, 0, 24, 0]
            || &header[36..40] != b"data"
            || u64::from(u32_at(40)) != bytes
            || u64::from(u32_at(4)) != 36 + bytes + bytes % 2
            || wav.metadata().ok()?.len() != 44 + bytes + bytes % 2
        {
            return None;
        }
        Some(())
    }
    check(out, capture, rate).is_some()
}
// Validate JSON syntax with a bounded recursive scanner, retaining top-level raw
// fields. No registry JSON dependency; escaped strings are parsed using TOML's
// compatible basic-string escapes (JSON slash escapes are normalised).
fn json_number(token: &str) -> bool {
    let bytes = token.as_bytes();
    let mut at = usize::from(bytes.first() == Some(&b'-'));
    match bytes.get(at) {
        Some(b'0') => at += 1,
        Some(b'1'..=b'9') => {
            at += 1;
            while bytes.get(at).is_some_and(u8::is_ascii_digit) {
                at += 1;
            }
        }
        _ => return false,
    }
    if bytes.get(at) == Some(&b'.') {
        at += 1;
        let start = at;
        while bytes.get(at).is_some_and(u8::is_ascii_digit) {
            at += 1;
        }
        if at == start {
            return false;
        }
    }
    if bytes.get(at).is_some_and(|b| matches!(b, b'e' | b'E')) {
        at += 1;
        if bytes.get(at).is_some_and(|b| matches!(b, b'+' | b'-')) {
            at += 1;
        }
        let start = at;
        while bytes.get(at).is_some_and(u8::is_ascii_digit) {
            at += 1;
        }
        if at == start {
            return false;
        }
    }
    at == bytes.len() && token.parse::<f64>().is_ok_and(f64::is_finite)
}
fn json_fields(text: &str) -> Option<Vec<(String, String)>> {
    struct Scanner<'a> {
        bytes: &'a [u8],
        at: usize,
    }
    impl Scanner<'_> {
        fn ws(&mut self) {
            while self.bytes.get(self.at).is_some_and(u8::is_ascii_whitespace) {
                self.at += 1;
            }
        }
        fn take(&mut self, byte: u8) -> Option<()> {
            self.ws();
            if self.bytes.get(self.at) != Some(&byte) {
                return None;
            }
            self.at += 1;
            Some(())
        }
        fn string(&mut self) -> Option<()> {
            self.take(b'"')?;
            loop {
                let b = *self.bytes.get(self.at)?;
                self.at += 1;
                match b {
                    b'"' => return Some(()),
                    b'\\' => {
                        let escaped = *self.bytes.get(self.at)?;
                        self.at += 1;
                        if escaped == b'u' {
                            for _ in 0..4 {
                                if !self.bytes.get(self.at)?.is_ascii_hexdigit() {
                                    return None;
                                }
                                self.at += 1;
                            }
                        } else if !b"\"\\/bfnrt".contains(&escaped) {
                            return None;
                        }
                    }
                    0..=31 => return None,
                    _ => {}
                }
            }
        }
        fn value(&mut self, depth: usize) -> Option<()> {
            if depth > 32 {
                return None;
            }
            self.ws();
            match *self.bytes.get(self.at)? {
                b'"' => self.string(),
                b'{' | b'[' => {
                    let object = self.bytes[self.at] == b'{';
                    self.at += 1;
                    self.ws();
                    let end = if object { b'}' } else { b']' };
                    if self.bytes.get(self.at) == Some(&end) {
                        self.at += 1;
                        return Some(());
                    }
                    loop {
                        if object {
                            self.string()?;
                            self.take(b':')?;
                        }
                        self.value(depth + 1)?;
                        self.ws();
                        if self.bytes.get(self.at) == Some(&end) {
                            self.at += 1;
                            return Some(());
                        }
                        self.take(b',')?;
                    }
                }
                _ => {
                    let start = self.at;
                    while self
                        .bytes
                        .get(self.at)
                        .is_some_and(|b| !b.is_ascii_whitespace() && !b",]}".contains(b))
                    {
                        self.at += 1;
                    }
                    let token = std::str::from_utf8(&self.bytes[start..self.at]).ok()?;
                    if matches!(token, "true" | "false" | "null") || json_number(token) {
                        Some(())
                    } else {
                        None
                    }
                }
            }
        }
    }
    let mut scanner = Scanner {
        bytes: text.as_bytes(),
        at: 0,
    };
    scanner.take(b'{')?;
    let mut fields = Vec::new();
    loop {
        scanner.ws();
        let start = scanner.at;
        scanner.string()?;
        let key: Value = toml::from_str(&format!(
            "k={}",
            text[start..scanner.at].replace("\\/", "/")
        ))
        .ok()?;
        let key = key.get("k")?.as_str()?.to_owned();
        if fields.iter().any(|(k, _)| *k == key) {
            return None;
        }
        scanner.take(b':')?;
        scanner.ws();
        let start = scanner.at;
        scanner.value(0)?;
        fields.push((key, text[start..scanner.at].to_owned()));
        scanner.ws();
        if scanner.bytes.get(scanner.at) == Some(&b'}') {
            scanner.at += 1;
            break;
        }
        scanner.take(b',')?;
    }
    scanner.ws();
    (scanner.at == text.len()).then_some(fields)
}

/// Preserve an incomplete existing pair in `incomplete/ID-attempt-N/` before
/// retrying. Without resume, refuse collisions. Complete captures must be
/// filtered before calling this; no existing measurement is overwritten.
pub fn prepare_paths(out: &Path, capture: &Capture, resume: bool) -> std::io::Result<()> {
    let paths = [
        out.join(format!("{}.wav", capture.id)),
        out.join(format!("{}.json", capture.id)),
    ];
    if !paths.iter().any(|p| p.exists()) {
        return Ok(());
    }
    if !resume {
        return Err(std::io::Error::other(format!(
            "{} already exists; use --resume or a new output directory",
            capture.id
        )));
    }
    let archive = out.join("incomplete");
    fs::create_dir_all(&archive)?;
    for attempt in 1..=10000 {
        let directory = archive.join(format!("{}-attempt-{attempt}", capture.id));
        match fs::create_dir(&directory) {
            Ok(()) => {
                for path in &paths {
                    if path.exists() {
                        fs::rename(
                            path,
                            directory.join(
                                path.file_name()
                                    .ok_or_else(|| std::io::Error::other("filename missing"))?,
                            ),
                        )?;
                    }
                }
                return Ok(());
            }
            Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => {}
            Err(e) => return Err(e),
        }
    }
    Err(std::io::Error::other("too many archived attempts"))
}

#[cfg(test)]
mod tests {
    use super::*;
    fn manifest() -> Manifest {
        Manifest::parse(include_str!("../../../reference/captures/manifest.toml")).unwrap()
    }
    fn fixture(id: &str, cutoff: f64, resonance: f64) -> Capture {
        Capture { id: id.into(), seconds: 0.1, tempo: 120.0, clock: false,
            settings: toml::from_str::<Value>(&format!("tuning=0.5\ncutoff={cutoff}\nresonance={resonance}\nenv_mod=0.0\ndecay=0.0\naccent=0.0\nvolume=0.7\nwaveform='saw'")).unwrap(),
            notes: vec![], instructions: String::new() }
    }
    #[test]
    fn grouping_reduces_prompts_and_greedy_uses_nearest() {
        let captures = [
            fixture("a", 0.0, 0.0),
            fixture("b", 1.0, 1.0),
            fixture("c", 0.0, 0.0),
            fixture("d", 1.0, 0.0),
            fixture("e", 1.0, 1.0),
        ];
        let naive: Vec<_> = captures.iter().collect();
        let ordered = order(naive.clone());
        assert_eq!(
            ordered.iter().map(|c| c.id.as_str()).collect::<Vec<_>>(),
            ["a", "c", "d", "b", "e"]
        );
        assert_eq!(estimate(&ordered).0, 3);
        assert_eq!(estimate(&naive).0, 5);
        assert!(estimate(&ordered).1 < estimate(&naive).1);
        assert_eq!(
            panel_changes(Some(&captures[0]), &captures[2]),
            Vec::<String>::new()
        );
        assert_eq!(
            panel_changes(Some(&captures[0]), &captures[3]),
            ["cutoff: 0.00/10 (~7:00) → 10.00/10 (~5:00)"]
        );
        let mut numeric = captures[0].clone();
        numeric
            .settings
            .as_table_mut()
            .unwrap()
            .insert("cutoff".into(), Value::Integer(0));
        assert_eq!(distance(&numeric, &captures[0]), 0);
    }
    #[test]
    fn session_files_and_selection() {
        let manifest = manifest();
        let essentials = SessionFile::parse(
            include_str!("../../../reference/captures/sessions/01-essentials.toml"),
            &manifest,
        )
        .unwrap();
        let grid = SessionFile::parse(
            include_str!("../../../reference/captures/sessions/02-vcf-grid.toml"),
            &manifest,
        )
        .unwrap();
        assert_eq!(grid.ids.len(), 2772);
        assert!(grid.ids.iter().all(|id| id.starts_with("vcf-grid-")));
        assert_eq!(essentials.ids.len(), 349); // keys above 72 removed (TD-3 range)
        let captures = order(select(&manifest, "*", Some(&essentials)).unwrap());
        assert!((45.0..=60.0).contains(&(estimate(&captures).1 / 60.0)));
        assert_eq!(
            select(&manifest, "pitch-k24-t5?,wave-square", None)
                .unwrap()
                .len(),
            7
        );
        assert!(select(&manifest, "missing*", None).is_err());
        assert!(select(&manifest, "pitch,", None).is_err());
        assert!(select(&manifest, "wave-", Some(&grid)).is_err());
        for source in [
            "name='x'\ncaptures=['missing']",
            "name='x'\ncaptures=[]",
            "name='x'\ncaptures=['loopback','loopback']",
        ] {
            assert!(SessionFile::parse(source, &manifest).is_err());
        }
    }
    #[test]
    fn plan_report() {
        let captures = [fixture("a", 0.0, 0.0), fixture("b", 0.0, 0.0)];
        let plan = plan(&captures.iter().collect::<Vec<_>>());
        assert!(plan.contains("2 captures; 1 panel prompts; estimated 0.3 minutes"));
        assert!(plan.contains("1/2 a (0.1 s; 8 panel changes)"));
        assert!(plan.contains("2/2 b (0.1 s; 0 panel changes)"));
        assert_eq!(estimate(&[]), (0, 0.0));
    }
    #[test]
    fn resume_checks_complete_valid_pair_and_preserves_partial() {
        let path = std::env::temp_dir().join(format!(
            "dw303-resume-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir(&path).unwrap();
        let capture = fixture("resume", 0.0, 0.0);
        let rate = 100;
        assert!(!complete(&path, &capture, rate));
        let mut wav =
            crate::WavWriter::new(fs::File::create(path.join("resume.wav")).unwrap(), rate)
                .unwrap();
        for _ in 0..160 {
            wav.sample(0.0).unwrap();
        }
        wav.finish().unwrap();
        assert!(!complete(&path, &capture, rate));
        let metadata = format!(
            "{{\"capture_id\":\"resume\",\"sample_rate\":100,\"frames\":160,\"valid\":true,\"complete\":true,\"settings\":{}}}",
            json(&capture.settings)
        );
        let sidecar = path.join("resume.json");
        fs::write(&sidecar, &metadata).unwrap();
        assert!(complete(&path, &capture, rate));
        assert!(!complete(&path, &capture, 200));
        for invalid in [
            metadata.replace("\"valid\":true", "\"valid\":false"),
            metadata.replace("\"complete\":true", "\"complete\":false"),
            metadata.replace("\"frames\":160", "\"frames\":159"),
            metadata.replace("\"frames\":160", "\"frames\":+160"),
            metadata.replace("\"frames\":160", "\"frames\":0160"),
            metadata.replace("\"valid\":true", "\"valid\":true,\"valid\":true"),
            metadata.replace("\"resume\"", "\"other\""),
            format!("{metadata}trailing"),
            metadata[..metadata.len() - 1].to_owned(),
        ] {
            fs::write(&sidecar, invalid).unwrap();
            assert!(!complete(&path, &capture, rate));
        }
        fs::write(&sidecar, &metadata).unwrap();
        let wav = path.join("resume.wav");
        let contents = fs::read(&wav).unwrap();
        fs::write(&wav, &contents[..contents.len() - 1]).unwrap();
        assert!(!complete(&path, &capture, rate));
        assert!(prepare_paths(&path, &capture, false).is_err());
        prepare_paths(&path, &capture, true).unwrap();
        assert!(!wav.exists());
        assert!(!sidecar.exists());
        assert_eq!(
            fs::read(path.join("incomplete/resume-attempt-1/resume.json")).unwrap(),
            metadata.as_bytes()
        );
        fs::remove_dir_all(&path).unwrap();
    }
}
