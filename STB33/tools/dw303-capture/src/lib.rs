//! Offline capture planning and streaming mono PCM24 serialization.
//! Native devices are opened only by the executable's run/loopback commands.
use std::io::{self, Seek, SeekFrom, Write};
use toml::Value;

/// Measurement session planning and resume validation.
pub mod session;

/// Validated MIDI mapping. VERIFY: TD-03 manual, firmware and velocity threshold.
#[derive(Clone, Debug)]
pub struct Mapping {
    /// Zero-based MIDI wire channel, converted from manifest's 1..16.
    pub channel: u8,
    /// First velocity considered accented by the reference unit.
    pub accent_threshold: u8,
}
/// One sample-grid MIDI message; short messages occupy the indicated prefix.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Scheduled {
    /// Nominal offset from first recorded sample, at the requested sample rate.
    pub sample: u64,
    /// Explicit status and data bytes.
    pub bytes: Vec<u8>,
}
/// One concrete capture with panel settings and beat-based notes.
#[derive(Clone, Debug)]
pub struct Capture {
    /// Filesystem-safe unique identifier.
    pub id: String,
    /// Capture duration excluding one second of pre-roll.
    pub seconds: f64,
    /// Quarter-note tempo in beats/minute.
    pub tempo: f64,
    /// Send Start, 24 PPQN clock and Stop when enabled.
    pub clock: bool,
    /// Panel rotation fractions and waveform string.
    pub settings: Value,
    /// Notes as [start beat, MIDI key, duration beats, velocity].
    pub notes: Vec<[f64; 4]>,
    /// Manual pattern/feature instructions printed before recording.
    pub instructions: String,
}
/// Parsed manifest, retaining complete session metadata for the sidecar.
#[derive(Clone, Debug)]
pub struct Manifest {
    /// Full TOML document.
    pub raw: Value,
    /// MIDI mapping.
    pub mapping: Mapping,
    /// Expanded concrete matrix entries.
    pub captures: Vec<Capture>,
}
fn number(v: &Value) -> Option<f64> {
    v.as_float().or_else(|| v.as_integer().map(|n| n as f64))
}
fn required_number(v: &Value, key: &str) -> Result<f64, String> {
    v.get(key)
        .and_then(number)
        .filter(|n| n.is_finite())
        .ok_or_else(|| format!("missing/invalid {key}"))
}
impl Manifest {
    /// Parse cached TOML with strict bounds; never opens hardware.
    pub fn parse(source: &str) -> Result<Self, String> {
        let raw = toml::from_str::<Value>(source).map_err(|e| e.to_string())?;
        let midi = raw.get("midi").ok_or("missing midi")?;
        let channel = required_number(midi, "channel")?;
        let threshold = required_number(midi, "accent_velocity_threshold")?;
        if !(1.0..=16.0).contains(&channel)
            || channel.fract() != 0.0
            || !(2.0..=127.0).contains(&threshold)
            || threshold.fract() != 0.0
        {
            return Err("invalid MIDI mapping".into());
        }
        if midi.get("slide").and_then(Value::as_str) != Some("overlap") {
            return Err("slide must be overlap".into());
        }
        let mut captures = Vec::new();
        let mut ids = std::collections::HashSet::new();
        for entry in raw
            .get("capture")
            .and_then(Value::as_array)
            .ok_or("missing capture array")?
        {
            let id = entry
                .get("id")
                .and_then(Value::as_str)
                .ok_or("missing id")?
                .to_owned();
            if id.is_empty()
                || !id
                    .bytes()
                    .all(|b| b.is_ascii_alphanumeric() || b == b'-' || b == b'_')
                || !ids.insert(id.clone())
            {
                return Err("invalid/duplicate capture id".into());
            }
            let seconds = required_number(entry, "seconds")?;
            let tempo = required_number(entry, "tempo")?;
            if !(0.1..=3600.0).contains(&seconds) || !(20.0..=400.0).contains(&tempo) {
                return Err("duration/tempo out of bounds".into());
            }
            let settings = entry.get("settings").ok_or("missing settings")?.clone();
            for key in [
                "tuning",
                "cutoff",
                "resonance",
                "env_mod",
                "decay",
                "accent",
                "volume",
            ] {
                if !(0.0..=1.0).contains(&required_number(&settings, key)?) {
                    return Err(format!("invalid knob {key}"));
                }
            }
            if !matches!(
                settings.get("waveform").and_then(Value::as_str),
                Some("saw" | "square")
            ) {
                return Err("invalid waveform".into());
            }
            let mut notes = Vec::new();
            for row in entry
                .get("notes")
                .and_then(Value::as_array)
                .ok_or("missing notes")?
            {
                let a = row.as_array().ok_or("invalid note")?;
                if a.len() != 4 {
                    return Err("note needs four numbers".into());
                }
                let mut note = [0.0; 4];
                for (i, value) in a.iter().enumerate() {
                    note[i] = number(value)
                        .filter(|x| x.is_finite())
                        .ok_or("invalid note number")?;
                }
                if note[0] < 0.0
                    || note[2] <= 0.0
                    || (note[0] + note[2]) * 60.0 / tempo > seconds
                    || !(0.0..=127.0).contains(&note[1])
                    || note[1].fract() != 0.0
                    || !(1.0..=127.0).contains(&note[3])
                    || note[3].fract() != 0.0
                {
                    return Err(format!("note out of bounds in {id}"));
                }
                notes.push(note);
            }
            captures.push(Capture {
                id,
                seconds,
                tempo,
                clock: entry
                    .get("clock")
                    .and_then(Value::as_bool)
                    .ok_or("missing clock")?,
                settings,
                notes,
                instructions: entry
                    .get("instructions")
                    .and_then(Value::as_str)
                    .unwrap_or("")
                    .into(),
            });
        }
        Ok(Self {
            raw,
            mapping: Mapping {
                channel: channel as u8 - 1,
                accent_threshold: threshold as u8,
            },
            captures,
        })
    }
}
impl Capture {
    /// Generate from absolute beat positions, rounding each event independently.
    /// One second pre-roll permits input startup and noise-floor measurement.
    pub fn schedule(&self, rate: u32, mapping: &Mapping) -> Vec<Scheduled> {
        let at = |seconds: f64| ((1.0 + seconds) * f64::from(rate)).round() as u64;
        let mut events = Vec::new();
        if self.clock {
            events.push(Scheduled {
                sample: at(0.0),
                bytes: vec![0xfa],
            });
            let pulses = (self.seconds * self.tempo / 60.0 * 24.0).ceil() as usize;
            for pulse in 0..pulses {
                events.push(Scheduled {
                    sample: at(pulse as f64 * 60.0 / (self.tempo * 24.0)),
                    bytes: vec![0xf8],
                });
            }
        }
        for &[beat, key, length, velocity] in &self.notes {
            events.push(Scheduled {
                sample: at(beat * 60.0 / self.tempo),
                bytes: vec![0x90 | mapping.channel, key as u8, velocity as u8],
            });
            events.push(Scheduled {
                sample: at((beat + length) * 60.0 / self.tempo),
                bytes: vec![0x80 | mapping.channel, key as u8, 0],
            });
        }
        if self.clock {
            events.push(Scheduled {
                sample: at(self.seconds),
                bytes: vec![0xfc],
            });
        }
        // Off before On for adjacent un-slid notes; overlap requires positive length.
        events.sort_by_key(|e| {
            (
                e.sample,
                match e.bytes[0] {
                    0xfa => 0,
                    0x80..=0x8f => 1,
                    0x90..=0x9f => 2,
                    0xf8 => 3,
                    _ => 4,
                },
            )
        });
        events
    }
}
/// JSON serializer for TOML metadata, without an additional JSON dependency.
pub fn json(v: &Value) -> String {
    match v {
        Value::String(s) => quote(s),
        Value::Integer(i) => i.to_string(),
        Value::Float(f) => {
            if f.is_finite() {
                f.to_string()
            } else {
                "null".into()
            }
        }
        Value::Boolean(b) => b.to_string(),
        Value::Datetime(d) => quote(&d.to_string()),
        Value::Array(a) => format!("[{}]", a.iter().map(json).collect::<Vec<_>>().join(",")),
        Value::Table(t) => format!(
            "{{{}}}",
            t.iter()
                .map(|(k, v)| format!("{}:{}", quote(k), json(v)))
                .collect::<Vec<_>>()
                .join(",")
        ),
    }
}
/// Escape a JSON string, including all control characters.
pub fn quote(s: &str) -> String {
    let mut out = String::from("\"");
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c < ' ' => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
    out
}
/// Streaming little-endian mono signed PCM24 WAV writer. No full-file buffer.
pub struct WavWriter<W: Write + Seek> {
    inner: W,
    bytes: u32,
}
impl<W: Write + Seek> WavWriter<W> {
    /// Write a placeholder header; finalize to publish valid RIFF lengths.
    pub fn new(mut inner: W, rate: u32) -> io::Result<Self> {
        if rate == 0 || rate > u32::MAX / 3 {
            return Err(io::Error::other("invalid sample rate"));
        }
        inner.write_all(b"RIFF\0\0\0\0WAVEfmt ")?;
        inner.write_all(&16u32.to_le_bytes())?;
        inner.write_all(&1u16.to_le_bytes())?;
        inner.write_all(&1u16.to_le_bytes())?;
        inner.write_all(&rate.to_le_bytes())?;
        inner.write_all(&(rate * 3).to_le_bytes())?;
        inner.write_all(&3u16.to_le_bytes())?;
        inner.write_all(&24u16.to_le_bytes())?;
        inner.write_all(b"data\0\0\0\0")?;
        Ok(Self { inner, bytes: 0 })
    }
    /// Quantize normalized f32; nonfinite values become zero; peaks are clamped.
    pub fn sample(&mut self, sample: f32) -> io::Result<()> {
        if self.bytes > u32::MAX - 40 {
            return Err(io::Error::other("RIFF limit exceeded"));
        }
        let value = if sample.is_finite() {
            (f64::from(sample.clamp(-1.0, 1.0)) * 8_388_608.0)
                .round()
                .clamp(-8_388_608.0, 8_388_607.0) as i32
        } else {
            0
        };
        self.inner.write_all(&value.to_le_bytes()[..3])?;
        self.bytes += 3;
        Ok(())
    }
    /// Patch lengths and add the required RIFF even-byte padding.
    pub fn finish(mut self) -> io::Result<W> {
        let padding = self.bytes % 2;
        if padding != 0 {
            self.inner.write_all(&[0])?;
        }
        self.inner.seek(SeekFrom::Start(4))?;
        self.inner
            .write_all(&(36 + self.bytes + padding).to_le_bytes())?;
        self.inner.seek(SeekFrom::Start(40))?;
        self.inner.write_all(&self.bytes.to_le_bytes())?;
        self.inner.flush()?;
        Ok(self.inner)
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn fixture(tempo: f64) -> Capture {
        Capture {
            id: "test".into(),
            seconds: 4.0,
            tempo,
            clock: true,
            settings: Value::Table(Default::default()),
            notes: vec![
                [0.0, 48.0, 1.1, 80.0],
                [1.0, 60.0, 1.0, 120.0],
                [2.0, 48.0, 1.0, 80.0],
            ],
            instructions: String::new(),
        }
    }
    #[test]
    fn clock_absolute_and_slide_order() {
        for tempo in [80.0, 120.0, 160.0] {
            let c = fixture(tempo);
            let s = c.schedule(
                96000,
                &Mapping {
                    channel: 2,
                    accent_threshold: 100,
                },
            );
            let clocks: Vec<_> = s.iter().filter(|e| e.bytes == [0xf8]).collect();
            for (i, e) in clocks.iter().enumerate() {
                assert!(
                    (e.sample as f64 - (1.0 + i as f64 * 60.0 / tempo / 24.0) * 96000.0).abs()
                        <= 0.5
                );
            }
            let on = s.iter().position(|e| e.bytes == [0x92, 60, 120]).unwrap();
            let off = s.iter().position(|e| e.bytes == [0x82, 48, 0]).unwrap();
            assert!(on < off);
            let adjacent = s.iter().position(|e| e.bytes == [0x82, 60, 0]).unwrap();
            assert_eq!(s[adjacent + 1].bytes, [0x92, 48, 80]);
        }
    }
    #[test]
    fn wav_bytes_and_padding() {
        let mut w = WavWriter::new(io::Cursor::new(Vec::new()), 96000).unwrap();
        for v in [-1.0, 0.0, 1.0, f32::NAN, 0.5] {
            w.sample(v).unwrap();
        }
        let b = w.finish().unwrap().into_inner();
        assert_eq!(b.len(), 60);
        assert_eq!(&b[44..53], &[0, 0, 128, 0, 0, 0, 255, 255, 127]);
        assert_eq!(u32::from_le_bytes(b[40..44].try_into().unwrap()), 15);
        assert_eq!(u32::from_le_bytes(b[4..8].try_into().unwrap()), 52);
    }
    #[test]
    fn full_manifest_validates() {
        let m = Manifest::parse(include_str!("../../../reference/captures/manifest.toml")).unwrap();
        assert_eq!(m.captures.len(), 3690); // Original matrix + session 2 interactions.
        assert_eq!(m.mapping.channel, 14); // MIDI channel 15, the TD-3 receive channel
    }
    #[test]
    fn minimal_manifest_and_invalid_notes() {
        let full = include_str!("../../../reference/captures/manifest.toml");
        let minimal = full
            .split("[[capture]]")
            .take(2)
            .collect::<Vec<_>>()
            .join("[[capture]]");
        assert_eq!(Manifest::parse(&minimal).unwrap().captures.len(), 1);
        let invalid = minimal.replace("[0, 24, 7, 80]", "[0, 200, 7, 80]");
        assert_ne!(minimal, invalid);
        assert!(Manifest::parse(&invalid).is_err());
        assert!(Manifest::parse(&minimal.replace("pitch-k24-t0", "../escape")).is_err());
        assert!(Manifest::parse(&minimal.replace("cutoff = 1.0", "cutoff = 1.1")).is_err());
    }
    #[test]
    fn clock_disabled_ties_and_rests() {
        let mut c = fixture(120.0);
        c.clock = false;
        c.notes = vec![[0.0, 48.0, 0.5, 80.0], [1.0, 60.0, 0.125, 80.0]];
        let s = c.schedule(
            96000,
            &Mapping {
                channel: 0,
                accent_threshold: 100,
            },
        );
        assert_eq!(s.len(), 4);
        assert_eq!(
            s.iter().map(|e| e.sample).collect::<Vec<_>>(),
            vec![96000, 120000, 144000, 150000]
        );
        assert!(s.iter().all(|e| e.bytes.len() == 3));
    }
    #[test]
    fn rejects_bad_mapping() {
        assert!(
            Manifest::parse("[midi]\nchannel=0\naccent_velocity_threshold=100\nslide='overlap'")
                .is_err()
        );
    }
    #[test]
    fn json_controls() {
        assert_eq!(quote("a\n\"\\\u{1}"), "\"a\\n\\\"\\\\\\u0001\"");
    }
}
