//! Render the DW-303 through its real engine at a fixed tempo, write a 24-bit
//! mono WAV, and report the real-time factor. Capture replay writes unclipped
//! float32 WAVs, retaining the sidecar recording length and MIDI timeline.
//!
//!   dw303-render [out.wav] [--seconds 8] [--rate 48000] [--tempo 125]
//!                [--set ID=VALUE ...] [--economy]
//!   dw303-render OUT.wav --replay SIDECAR.json [--rate 96000]
//!   dw303-render --replay-dir SESSION --out-dir DIR [--jobs N] [--skip-existing]
mod json;
mod replay;

use dw303_engine::{Dw303Engine, params};
use ravet_foundation::{
    AudioBuffer, AudioLayout, Event, EventData, ParameterId, ProcessBlock, ProcessConfig,
    Processor, Transport,
};
use std::{io::Write, time::Instant};

const BLOCK: usize = 256;

fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let mut out = String::from("dw303.wav");
    let mut seconds = 8.0_f64;
    let mut rate = 48_000.0_f64;
    let mut settings: Vec<(ParameterId, f32)> = vec![
        (params::SYNC_MODE, 0.0),
        (params::TEMPO, 125.0),
        (params::RUN, 1.0),
    ];
    let mut hold: Option<u8> = None;
    let mut replay_path = None;
    let mut replay_dir = None;
    let mut out_dir = None;
    let mut replay_rate = None;
    let mut jobs = None;
    let mut skip_existing = false;
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--seconds" => seconds = args.next().ok_or("--seconds value")?.parse()?,
            "--rate" => {
                rate = args.next().ok_or("--rate value")?.parse()?;
                replay_rate = Some(rate);
            }
            "--replay" => replay_path = Some(args.next().ok_or("--replay sidecar")?),
            "--replay-dir" => replay_dir = Some(args.next().ok_or("--replay-dir directory")?),
            "--out-dir" => out_dir = Some(args.next().ok_or("--out-dir directory")?),
            "--skip-existing" => skip_existing = true,
            "--jobs" => jobs = Some(args.next().ok_or("--jobs count")?.parse()?),
            "--tempo" => {
                settings.push((params::TEMPO, args.next().ok_or("--tempo value")?.parse()?))
            }
            "--economy" => settings.push((params::QUALITY, 0.0)),
            "--hold" => hold = Some(args.next().ok_or("--hold KEY")?.parse::<u8>()?),
            "--set" => {
                let s = args.next().ok_or("--set ID=VALUE")?;
                let (id, value) = s.split_once('=').ok_or("--set ID=VALUE")?;
                settings.push((ParameterId(id.parse()?), value.parse()?));
            }
            _ if arg.starts_with("--") => return Err(format!("unknown option {arg}").into()),
            _ => out = arg,
        }
    }

    if let Some(directory) = replay_dir {
        if replay_path.is_some() {
            return Err("choose --replay or --replay-dir".into());
        }
        return replay::batch(
            std::path::Path::new(&directory),
            std::path::Path::new(&out_dir.ok_or("--out-dir required")?),
            replay_rate,
            jobs,
            skip_existing,
        );
    }
    if let Some(path) = replay_path {
        return replay::one(
            std::path::Path::new(&path),
            std::path::Path::new(&out),
            replay_rate,
        );
    }

    let mut engine = Dw303Engine::new();
    // Measurements use the circuit-calibrated level (no fixed +18 dB output gain).
    engine.set_reference_level(true);
    engine.prepare(ProcessConfig::new(
        rate,
        BLOCK,
        64,
        AudioLayout::MONO_INSTRUMENT,
    )?)?;
    let initial: Vec<Event> = settings
        .iter()
        .map(|&(id, value)| Event {
            frame_offset: 0,
            data: EventData::ParameterValue { id, value },
        })
        .collect();
    let mut initial = initial;
    if let Some(key) = hold {
        // Sustained MIDI note with the sequencer stopped.
        initial.retain(
            |e| !matches!(e.data, EventData::ParameterValue { id, .. } if id == params::RUN),
        );
        initial.push(Event {
            frame_offset: 0,
            data: EventData::NoteOn {
                channel: 0,
                key,
                velocity: 0.5,
            },
        });
    }

    let total = (seconds * rate) as usize;
    let mut samples = Vec::with_capacity(total);
    let mut buffer = [0.0_f32; BLOCK];
    let start = Instant::now();
    let mut done = 0;
    while done < total {
        let frames = BLOCK.min(total - done);
        let events: &[Event] = if done == 0 { &initial } else { &[] };
        let mut outputs = [&mut buffer[..frames]];
        let audio = AudioBuffer::separate(&[], &mut outputs, frames)?;
        let mut block = ProcessBlock::new(audio, events, Transport::default());
        let status = engine.process(&mut block);
        if let ravet_foundation::ProcessStatus::Fault(e) = status {
            return Err(format!("engine fault: {e:?}").into());
        }
        samples.extend_from_slice(&buffer[..frames]);
        done += frames;
    }
    let elapsed = start.elapsed().as_secs_f64();

    let peak = samples.iter().fold(0.0_f32, |p, s| p.max(s.abs()));
    let rms = (samples.iter().map(|s| f64::from(*s).powi(2)).sum::<f64>() / total as f64).sqrt();
    write_wav(&out, rate as u32, &samples, false)?;
    let d = engine.vcf_diagnostics();
    println!("wrote {out}: {seconds} s at {rate} Hz");
    println!(
        "peak {:.2} dBFS, rms {:.2} dBFS",
        20.0 * peak.log10(),
        20.0 * rms.log10()
    );
    println!(
        "render {elapsed:.3} s = {:.1}x real time ({:.1}% of one core)",
        seconds / elapsed,
        100.0 * elapsed / seconds
    );
    println!("vcf diagnostics: {d:?}");
    Ok(())
}

fn write_wav(path: &str, rate: u32, samples: &[f32], float: bool) -> std::io::Result<()> {
    let width = if float { 4 } else { 3 };
    let bytes = samples
        .len()
        .checked_mul(width)
        .ok_or_else(|| std::io::Error::other("WAV size overflow"))?;
    let data_len =
        u32::try_from(bytes).map_err(|_| std::io::Error::other("WAV exceeds RIFF size"))?;
    let mut f = std::io::BufWriter::new(std::fs::File::create(path)?);
    f.write_all(b"RIFF")?;
    let header_size = if float { 48 } else { 36 };
    let riff_size = data_len
        .checked_add(header_size)
        .ok_or_else(|| std::io::Error::other("WAV exceeds RIFF size"))?;
    f.write_all(&riff_size.to_le_bytes())?;
    f.write_all(b"WAVEfmt ")?;
    f.write_all(&16u32.to_le_bytes())?;
    f.write_all(&(if float { 3u16 } else { 1u16 }).to_le_bytes())?; // IEEE float / PCM
    f.write_all(&1u16.to_le_bytes())?; // mono
    f.write_all(&rate.to_le_bytes())?;
    f.write_all(&(rate * width as u32).to_le_bytes())?;
    f.write_all(&(width as u16).to_le_bytes())?;
    f.write_all(&(width as u16 * 8).to_le_bytes())?;
    if float {
        f.write_all(b"fact")?;
        f.write_all(&4u32.to_le_bytes())?;
        f.write_all(&(samples.len() as u32).to_le_bytes())?;
    }
    f.write_all(b"data")?;
    f.write_all(&data_len.to_le_bytes())?;
    for s in samples {
        if float {
            f.write_all(&s.to_le_bytes())?;
            continue;
        }
        let v = (f64::from(s.clamp(-1.0, 1.0)) * 8_388_607.0).round() as i32;
        f.write_all(&v.to_le_bytes()[..3])?;
    }
    f.flush()
}
