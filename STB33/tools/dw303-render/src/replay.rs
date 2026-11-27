//! Exact capture timeline replay through the instrument engine.
use crate::{BLOCK, json, write_wav};
use dw303_engine::{Dw303Engine, params};
use ravet_foundation::{
    AudioBuffer, AudioLayout, Event, EventData, MidiMessage, ProcessBlock, ProcessConfig,
    Processor, Transport,
};
use std::{io::Write, path::Path};

type Error = Box<dyn std::error::Error + Send + Sync>;
struct Replay {
    rate: f64,
    frames: usize,
    /// VERIFY: capture onset timestamps include interface/MIDI jitter and the hardware attack.
    latency: i64,
    events: Vec<(usize, EventData)>,
}
fn integer(v: &json::Value) -> Result<usize, Error> {
    let n = v.number()?;
    if n < 0.0 || n.fract() != 0.0 || n > u32::MAX as f64 {
        return Err("invalid sample count/byte".into());
    }
    Ok(n as usize)
}
impl Replay {
    fn read(text: &str, override_rate: Option<f64>) -> Result<Self, Error> {
        let root = json::parse(text)?;
        let source_rate = root.get("sample_rate")?.number()?;
        let rate = override_rate.unwrap_or(source_rate);
        if !(44100.0..=192000.0).contains(&rate)
            || !(1.0..=384000.0).contains(&source_rate)
            || rate.fract() != 0.0
        {
            return Err("invalid replay sample rate".into());
        }
        let scale = rate / source_rate;
        let frames = (integer(root.get("frames")?)? as f64 * scale).round() as usize;
        if frames == 0 || frames > (u32::MAX as usize - 48) / 4 {
            return Err("empty capture or capture exceeds RIFF size".into());
        }
        let settings = root.get("settings")?;
        let mut events = Vec::new();
        for (name, id) in [
            ("tuning", params::TUNING),
            ("cutoff", params::CUTOFF),
            ("resonance", params::RESONANCE),
            ("env_mod", params::ENV_MOD),
            ("decay", params::DECAY),
            ("accent", params::ACCENT),
            ("volume", params::VOLUME),
        ] {
            let n = settings.get(name)?.number()?;
            if !(0.0..=1.0).contains(&n) {
                return Err(format!("invalid knob {name}").into());
            }
            events.push((
                0,
                EventData::ParameterValue {
                    id,
                    value: n as f32,
                },
            ));
        }
        let wave = match settings.get("waveform")?.string()? {
            "saw" => 0.0,
            "square" => 1.0,
            _ => return Err("unknown waveform".into()),
        };
        for (id, value) in [
            (params::WAVEFORM, wave),
            (params::TEMPO, root.get("tempo_bpm")?.number()? as f32),
            (params::SYNC_MODE, 0.0),
            (params::RUN, 0.0),
        ] {
            events.push((0, EventData::ParameterValue { id, value }));
        }
        let mapping = root.get("midi_mapping")?;
        if integer(mapping.get("accent_velocity_threshold")?)? != 100
            || mapping.get("slide")?.string()? != "overlap"
        {
            return Err("MIDI mapping differs from engine threshold 100 / overlap slide".into());
        }
        let channel = integer(mapping.get("channel")?)?;
        if !(1..=16).contains(&channel) {
            return Err("mapping channel must be 1..16".into());
        }
        let mut first_on = None;
        for e in root.get("midi_schedule")?.array()? {
            let raw: Vec<u8> = e
                .get("bytes")?
                .array()?
                .iter()
                .map(|v| {
                    let n = integer(v)?;
                    u8::try_from(n).map_err(|_| "invalid MIDI byte".into())
                })
                .collect::<Result<_, Error>>()?;
            let m = MidiMessage::from_bytes(&raw)?;
            if raw[0] < 0xf0 && usize::from(raw[0] & 15) != channel - 1 {
                return Err("MIDI bytes disagree with mapping channel".into());
            }
            let sample = integer(e.get("sample")?)?;
            if raw[0] & 0xf0 == 0x90 && raw.get(2).is_some_and(|v| *v > 0) {
                first_on = Some(first_on.map_or(sample, |s: usize| s.min(sample)));
            }
            events.push(((sample as f64 * scale).round() as usize, EventData::Midi(m)));
        }
        events.sort_by_key(|e| e.0); // stable ordering preserves same-sample MIDI order
        let onset = integer(root.get("onset_sample")?)?;
        let latency =
            ((onset as f64 - first_on.ok_or("no note-on")? as f64) * scale).round() as i64;
        Ok(Self {
            rate,
            frames,
            latency,
            events,
        })
    }
    fn block_events(&self, start: usize, frames: usize, next: &mut usize, out: &mut Vec<Event>) {
        out.clear();
        while *next < self.events.len() && self.events[*next].0 < start + frames {
            let (sample, data) = self.events[*next];
            out.push(Event {
                frame_offset: sample - start,
                data,
            });
            *next += 1;
        }
    }
    fn render(&self) -> Result<Vec<f32>, Error> {
        let mut engine = Dw303Engine::new();
        // TD-3 comparisons use the circuit-calibrated level (no fixed output gain).
        engine.set_reference_level(true);
        engine.prepare(ProcessConfig::new(
            self.rate,
            BLOCK,
            self.events.len().max(64),
            AudioLayout::MONO_INSTRUMENT,
        )?)?;
        let extra = if self.latency < 0 {
            self.latency.unsigned_abs() as usize
        } else {
            0
        };
        let total = self.frames.checked_add(extra).ok_or("capture too long")?;
        let mut output = vec![0.0; self.frames];
        let mut buffer = [0.0; BLOCK];
        let mut events = Vec::with_capacity(self.events.len());
        let mut next = 0;
        for start in (0..total).step_by(BLOCK) {
            let frames = BLOCK.min(total - start);
            self.block_events(start, frames, &mut next, &mut events);
            let mut outputs = [&mut buffer[..frames]];
            let audio = AudioBuffer::separate(&[], &mut outputs, frames)?;
            let mut block = ProcessBlock::new(audio, &events, Transport::default());
            if let ravet_foundation::ProcessStatus::Fault(e) = engine.process(&mut block) {
                return Err(format!("engine fault: {e:?}").into());
            }
            for (i, &sample) in buffer[..frames].iter().enumerate() {
                if !sample.is_finite() {
                    return Err("nonfinite engine output".into());
                }
                let destination = (start + i) as i64 + self.latency;
                if destination >= 0 && (destination as usize) < self.frames {
                    output[destination as usize] = sample;
                }
            }
        }
        Ok(output)
    }
}
pub(crate) fn one(sidecar: &Path, output: &Path, rate: Option<f64>) -> Result<(), Error> {
    let r = Replay::read(&std::fs::read_to_string(sidecar)?, rate)?;
    let samples = r.render()?;
    write_wav(
        output.to_str().ok_or("non-UTF8 output path")?,
        r.rate as u32,
        &samples,
        true,
    )?;
    Ok(())
}
pub(crate) fn batch(
    directory: &Path,
    output: &Path,
    rate: Option<f64>,
    jobs: Option<usize>,
    skip_existing: bool,
) -> Result<(), Error> {
    std::fs::create_dir_all(output)?;
    let mut paths = std::fs::read_dir(directory)?
        .map(|e| e.map(|e| e.path()))
        .collect::<Result<Vec<_>, _>>()?;
    paths.retain(|p| p.extension().is_some_and(|e| e == "json"));
    paths.sort();
    // Existing WAVs are cache hits; the caller must use a hash-specific directory.
    paths.retain(|p| {
        !skip_existing
            || !output
                .join(p.file_name().unwrap_or_default())
                .with_extension("wav")
                .exists()
    });
    let workers = jobs
        .unwrap_or(std::thread::available_parallelism()?.get())
        .max(1)
        .min(paths.len().max(1));
    let counter = std::sync::atomic::AtomicUsize::new(0);
    let errors = std::sync::Mutex::new(Vec::new());
    std::thread::scope(|scope| {
        for _ in 0..workers {
            scope.spawn(|| {
                loop {
                    let i = counter.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    let Some(path) = paths.get(i) else {
                        break;
                    };
                    let out = output
                        .join(path.file_name().unwrap_or_default())
                        .with_extension("wav");
                    let temporary = out.with_extension("wav.tmp");
                    let result = one(path, &temporary, rate).and_then(|()| {
                        std::fs::rename(&temporary, &out)?;
                        Ok(())
                    });
                    if let Err(e) = result {
                        let _ = std::fs::remove_file(&temporary);
                        if let Ok(mut list) = errors.lock() {
                            list.push(format!("{}: {e}", path.display()));
                        }
                    }
                }
            });
        }
    });
    let errors = errors.into_inner().map_err(|_| "batch lock poisoned")?;
    if !errors.is_empty() {
        return Err(errors.join("\n").into());
    }
    writeln!(
        std::io::stdout(),
        "rendered {} captures using {workers} workers",
        paths.len()
    )?;
    Ok(())
}
#[cfg(test)]
mod tests {
    use super::*;
    fn fixture() -> String {
        r#"{"sample_rate":96000,"frames":1024,"onset_sample":517,"tempo_bpm":120,"settings":{"tuning":0.5,"cutoff":1,"resonance":0,"env_mod":0,"decay":0,"accent":0,"volume":1,"waveform":"saw"},"midi_mapping":{"channel":15,"accent_velocity_threshold":100,"slide":"overlap"},"midi_schedule":[{"sample":511,"bytes":[158,48,80]},{"sample":768,"bytes":[142,48,0]}]}"#.into()
    }
    #[test]
    fn replay_exact_samples_and_latency() {
        let r = Replay::read(&fixture(), None).unwrap();
        let mut next = 0;
        let mut events = Vec::new();
        let mut absolute = Vec::new();
        for start in (0..1024).step_by(BLOCK) {
            r.block_events(start, BLOCK, &mut next, &mut events);
            for e in &events {
                if let EventData::Midi(_) = e.data {
                    absolute.push(start + e.frame_offset);
                }
            }
        }
        assert_eq!(absolute, vec![511, 768]);
        assert_eq!(r.latency, 6);
        let shifted = r.render().unwrap();
        let unshifted = Replay { latency: 0, ..r }.render().unwrap();
        assert_eq!(&shifted[6..], &unshifted[..1018]);
        assert!(shifted[..517].iter().all(|v| *v == 0.0));
        assert!(shifted[517..].iter().any(|v| *v != 0.0));
    }
    #[test]
    fn rescale_and_mapping_validation() {
        let r = Replay::read(&fixture(), Some(48000.0)).unwrap();
        assert_eq!(r.frames, 512);
        assert_eq!(r.latency, 3);
        assert!(
            Replay::read(&fixture().replace("\"channel\":15", "\"channel\":14"), None).is_err()
        );
    }
    #[test]
    fn negative_latency_clock_and_same_sample_order() {
        let fixture = fixture().replace("\"onset_sample\":517", "\"onset_sample\":507")
            .replace("{\"sample\":511,\"bytes\":[158,48,80]}",
                     "{\"sample\":511,\"bytes\":[250]},{\"sample\":511,\"bytes\":[248]},{\"sample\":511,\"bytes\":[158,48,80]}");
        let r = Replay::read(&fixture, None).unwrap();
        assert_eq!(r.latency, -4);
        let bytes: Vec<Vec<u8>> = r
            .events
            .iter()
            .filter_map(|(_, e)| {
                if let EventData::Midi(m) = e {
                    Some(m.as_bytes().to_vec())
                } else {
                    None
                }
            })
            .collect();
        assert_eq!(
            bytes,
            vec![vec![250], vec![248], vec![158, 48, 80], vec![142, 48, 0]]
        );
        let output = r.render().unwrap();
        assert!(output[..507].iter().all(|v| *v == 0.0));
        assert!(output[507..].iter().any(|v| *v != 0.0));
    }
    #[test]
    fn batch_threads_produce_identical_wavs() {
        let unique = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let base =
            std::env::temp_dir().join(format!("dw303-replay-{}-{unique}", std::process::id()));
        let inputs = base.join("inputs");
        let outputs = base.join("outputs");
        std::fs::create_dir_all(&inputs).unwrap();
        std::fs::write(inputs.join("a.json"), fixture()).unwrap();
        std::fs::write(inputs.join("b.json"), fixture()).unwrap();
        batch(&inputs, &outputs, None, Some(2), false).unwrap();
        let a = std::fs::read(outputs.join("a.wav")).unwrap();
        assert_eq!(a, std::fs::read(outputs.join("b.wav")).unwrap());
        batch(&inputs, &outputs, None, Some(1), false).unwrap();
        assert_eq!(a, std::fs::read(outputs.join("a.wav")).unwrap());
        assert_eq!(a.len(), 56 + 1024 * 4);
        assert_eq!(&a[36..40], b"fact");
        std::fs::remove_dir_all(base).unwrap();
    }
}
