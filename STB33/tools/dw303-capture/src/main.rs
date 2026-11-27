//! macOS capture operator CLI. All native operations stay off the audio callback.
//! VERIFY: first-sample epoch and independent input latency require hardware
//! calibration; RAVET input capture exposes no hardware timestamps.
//! VERIFY: panel clock positions assume 300-degree travel, 7 to 5 o'clock.
//! VERIFY: onset threshold includes analog attack and must be checked at the
//! actual interface gain, noise level and filter settings.
use dw303_capture::{Capture, Manifest, WavWriter, json, quote, session};
use ravet_device::cpal;
use std::{
    fs::{self, OpenOptions},
    io::{self, BufWriter},
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
        mpsc,
    },
    thread,
    time::{Duration, Instant},
};
type Error = Box<dyn std::error::Error + Send + Sync>;

fn option<'a>(args: &'a [String], name: &str) -> Result<&'a str, Error> {
    args.iter()
        .position(|s| s == name)
        .and_then(|i| args.get(i + 1))
        .map(String::as_str)
        .ok_or_else(|| format!("missing {name}").into())
}
fn sleep_until(deadline: Instant, stop: &AtomicBool) -> bool {
    while let Some(left) = deadline.checked_duration_since(Instant::now()) {
        if stop.load(Ordering::Relaxed) {
            return false;
        }
        thread::sleep(left.min(Duration::from_millis(10)));
    }
    !stop.load(Ordering::Relaxed)
}
fn main() -> Result<(), Error> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args.first().map(String::as_str) == Some("list") {
        for d in ravet_device::input_devices()? {
            println!("audio {}: {}", d.id, d.name);
            for c in d.configurations {
                println!("  {c:?}");
            }
        }
        for p in ravet_midi_device::output_ports()? {
            println!("MIDI {}: {}", p.id, p.name);
        }
        return Ok(());
    }
    if args.first().map(String::as_str) == Some("levels") {
        return levels(&args);
    }
    if args.first().map(String::as_str) == Some("midi-scan") {
        return midi_scan(&args);
    }
    match args.first().map(String::as_str) {
        Some("session" | "plan") => {
            return match measurement_session(&args) {
                Err(e) if e.downcast_ref::<io::Error>().is_some_and(|e| e.kind() == io::ErrorKind::Interrupted) => {
                    println!("Session stopped before the next capture.");
                    Ok(())
                },
                result => result,
            };
        },
        Some("run" | "loopback") => {},
        _ => return Err("usage: dw303-capture list | run/loopback MANIFEST --capture ID [device options] | session/plan MANIFEST --select 'PREFIX,GLOB*' [--session-file FILE] [--resume --out DIR] [device options]".into()),
    }
    let manifest = Manifest::parse(&fs::read_to_string(
        args.get(1).ok_or("manifest path required")?,
    )?)?;
    let id = if args[0] == "loopback" {
        "loopback"
    } else {
        option(&args, "--capture")?
    };
    let capture = manifest
        .captures
        .iter()
        .find(|c| c.id == id)
        .ok_or("unknown capture ID")?;
    println!("Capture {id}: {}", capture.instructions);
    for line in session::panel_changes(None, capture) {
        println!("  {line}");
    }
    println!("Warm up >=30 min; confirm manifest metadata. Room temperature C (blank = null):");
    let temperature = temperature(&read_line()?)?;
    println!("Press Enter when panel and routing match.");
    read_line()?;
    record(&args, &manifest, capture, &temperature)
}
fn read_line() -> Result<String, Error> {
    let mut line = String::new();
    if io::stdin().read_line(&mut line)? == 0 {
        return Err("operator input closed".into());
    }
    Ok(line.trim().to_owned())
}
fn temperature(line: &str) -> Result<String, Error> {
    if line.trim().is_empty() {
        return Ok("null".into());
    }
    let value: f64 = line.trim().parse()?;
    if !value.is_finite() {
        return Err("invalid temperature".into());
    }
    Ok(value.to_string())
}
fn record(
    args: &[String],
    manifest: &Manifest,
    capture: &Capture,
    temperature: &str,
) -> Result<(), Error> {
    let id = capture.id.as_str();
    let rate: u32 = option(args, "--rate")?.parse()?;
    if rate != 96000 {
        return Err("protocol requires 96000 Hz".into());
    }
    let channel: usize = option(args, "--channel")?.parse()?;
    let mut latency_ms: f64 = if args.iter().any(|s| s == "--latency-ms") {
        option(args, "--latency-ms")?.parse()?
    } else {
        0.0
    };
    if !latency_ms.is_finite() || latency_ms.abs() > 500.0 {
        return Err("latency compensation must be within +/-500 ms".into());
    }
    if id == "loopback" && args[0] == "session" {
        latency_ms = 0.0;
    }
    if id == "loopback" && latency_ms != 0.0 {
        return Err("loopback requires uncompensated schedule".into());
    }
    let input_latency_ms: Option<f64> = if args.iter().any(|s| s == "--input-latency-ms") {
        Some(option(args, "--input-latency-ms")?.parse()?)
    } else {
        None
    };
    if input_latency_ms.is_some_and(|v| !v.is_finite() || v < 0.0) {
        return Err("invalid input latency estimate".into());
    }
    let audio_id = option(args, "--audio-device")?;
    let info = ravet_device::input_devices()?
        .into_iter()
        .find(|d| d.id == audio_id)
        .ok_or("unknown audio ID")?;
    let config = info
        .configurations
        .iter()
        .find(|c| {
            c.sample_format() == cpal::SampleFormat::F32
                && c.min_sample_rate() <= rate
                && c.max_sample_rate() >= rate
                && channel > 0
                && channel <= usize::from(c.channels())
        })
        .ok_or("device lacks requested f32 rate/channel")?;
    let channels = usize::from(config.channels());
    let midi_name = option(args, "--midi-out")?;
    let ports: Vec<_> = ravet_midi_device::output_ports()?
        .into_iter()
        .filter(|p| p.name == midi_name)
        .collect();
    if ports.len() != 1 {
        return Err("MIDI name absent or ambiguous; choose a unique output name".into());
    }
    let out = std::path::Path::new(option(args, "--out")?);
    fs::create_dir_all(out)?;
    let wav_path = out.join(format!("{id}.wav"));
    let sidecar_path = out.join(format!("{id}.json"));
    // Reserve both paths before starting hardware; never overwrite measurements.
    let wav_file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(wav_path)?;
    let mut sidecar = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(sidecar_path)?;
    let device = ravet_device::input_device(Some(audio_id))?.ok_or("device disappeared")?;
    let (stream, mut consumer) = ravet_device::open_input(
        &device,
        cpal::StreamConfig {
            channels: channels as u16,
            sample_rate: rate,
            buffer_size: cpal::BufferSize::Default,
        },
        65536,
    )?;
    let mut midi = ravet_midi_device::open_output(&ports[0].id)?;
    let schedule = capture.schedule(rate, &manifest.mapping);
    let total = ((capture.seconds + 1.5) * f64::from(rate)).round() as u64;
    let stop = Arc::new(AtomicBool::new(false));
    let writer_stop = stop.clone();
    let (tx, rx) = mpsc::sync_channel(1);
    let writer = thread::spawn(move || -> Result<(u64, Option<u64>, u64), Error> {
        let result = (|| {
            let mut wav = WavWriter::new(BufWriter::new(wav_file), rate)?;
            let mut buffer = vec![0.0; channels * 4096];
            let mut frames = 0u64;
            let mut peak_count = 0;
            let mut onset = None;
            let mut noise_sum = 0.0;
            let mut noise_n = 0u64;
            let mut last_data = Instant::now();
            while frames < total && !writer_stop.load(Ordering::Relaxed) {
                let count = consumer
                    .pop(&mut buffer)
                    .map_err(|e| format!("ring: {e:?}"))?;
                if count == 0 {
                    if last_data.elapsed() > Duration::from_secs(5) {
                        return Err("input stalled".into());
                    }
                    thread::sleep(Duration::from_millis(1));
                    continue;
                }
                last_data = Instant::now();
                if frames == 0 {
                    // VERIFY: arrival minus batch duration estimates first sample epoch;
                    // RAVET supplies no CPAL capture timestamp or input-latency estimate.
                    let epoch = last_data - Duration::from_secs_f64(count as f64 / f64::from(rate));
                    tx.send(epoch)?;
                }
                for frame in buffer[..count * channels]
                    .chunks_exact(channels)
                    .take((total - frames) as usize)
                {
                    let sample = frame[channel - 1];
                    let magnitude = f64::from(sample.abs());
                    if frames < u64::from(rate) / 2 && magnitude.is_finite() {
                        noise_sum += magnitude * magnitude;
                        noise_n += 1;
                    }
                    let threshold = 0.01_f64.max(8.0 * (noise_sum / noise_n.max(1) as f64).sqrt());
                    if frames >= u64::from(rate) / 2 && onset.is_none() && magnitude > threshold {
                        onset = Some(frames);
                    }
                    if magnitude >= 1.0 {
                        peak_count += 1;
                    }
                    wav.sample(sample)?;
                    frames += 1;
                }
            }
            wav.finish()?;
            Ok((frames, onset, peak_count))
        })();
        if result.is_err() {
            writer_stop.store(true, Ordering::Relaxed);
        }
        result
    });
    if let Err(e) = stream.play() {
        stop.store(true, Ordering::Relaxed);
        let _ = writer.join();
        return Err(e.into());
    }
    let epoch = match rx.recv_timeout(Duration::from_secs(5)) {
        Ok(t) => t,
        Err(e) => {
            stop.store(true, Ordering::Relaxed);
            let _ = stream.pause();
            let _ = writer.join();
            return Err(e.into());
        }
    };
    // Dedicated timer thread, no cumulative sleeps: every deadline uses epoch.
    let scheduled = schedule.clone();
    let midi_channel = manifest.mapping.channel;
    let scheduler_stop = stop.clone();
    let scheduler = thread::spawn(move || -> Result<Vec<String>, Error> {
        let result = (|| {
            let mut log = Vec::with_capacity(scheduled.len());
            for event in &scheduled {
                let send_target = event.sample as f64 / f64::from(rate) - latency_ms / 1000.0;
                if !sleep_until(
                    epoch + Duration::from_secs_f64(send_target),
                    &scheduler_stop,
                ) {
                    return Err("capture stopped or input/writer failed".into());
                }
                let actual = Instant::now().duration_since(epoch).as_secs_f64();
                midi.send(&event.bytes)?;
                log.push(format!(
                    "{{\"sample\":{},\"scheduled_seconds\":{},\"send_target_seconds\":{},\"send_seconds\":{},\"bytes\":{:?}}}",
                    event.sample,
                    event.sample as f64 / f64::from(rate),
                    send_target,
                    actual,
                    event.bytes
                ));
            }
            Ok(log)
        })();
        // Always release notes and transport, including failed sends.
        let _ = midi.send(&[0xb0 | midi_channel, 123, 0]);
        let _ = midi.send(&[0xfc]);
        result
    });
    let midi_log = scheduler.join().map_err(|_| "scheduler thread failed")?;
    if midi_log.is_err() {
        stop.store(true, Ordering::Relaxed);
    }
    let written = writer.join().map_err(|_| "writer thread failed")?;
    stream.pause()?;
    let stats = stream.statistics();
    let (frames, onset, clipped) = written?;
    let midi_log = midi_log?;
    let first_on = schedule
        .iter()
        .find(|e| e.bytes[0] & 0xf0 == 0x90)
        .map(|e| e.sample);
    let latency = onset
        .zip(first_on)
        .map(|(a, b)| (a as f64 - b as f64) / f64::from(rate));
    use std::io::Write;
    writeln!(
        sidecar,
        "{{\"capture_id\":{},\"sample_rate\":{rate},\"bits\":24,\"channel\":{channel},\"frames\":{frames},\"settings\":{},\"tempo_bpm\":{},\"clock\":{},\"capture_seconds\":{},\"operator_instructions\":{},\"session\":{},\"room_temperature_c\":{temperature},\"audio_device\":{},\"audio_device_id\":{},\"midi_output\":{},\"midi_output_id\":{},\"midi_mapping\":{},\"first_sample_epoch_method\":\"VERIFY: first ring arrival minus batch duration; no hardware timestamp\",\"latency_compensation_seconds\":{},\"input_latency_estimate_seconds\":{},\"midi_to_audio_latency_estimate_seconds\":{},\"onset_sample\":{},\"clipped_samples\":{clipped},\"dropped_frames\":{},\"backend_errors\":{},\"invalid_buffers\":{},\"complete\":{},\"valid\":{},\"midi_schedule\":[{}]}}",
        quote(id),
        json(&capture.settings),
        capture.tempo,
        capture.clock,
        capture.seconds,
        quote(&capture.instructions),
        json(
            manifest
                .raw
                .get("session")
                .unwrap_or(&toml::Value::Table(Default::default()))
        ),
        quote(&info.name),
        quote(audio_id),
        quote(&ports[0].name),
        quote(&ports[0].id),
        json(&manifest.raw["midi"]),
        latency_ms / 1000.0,
        input_latency_ms.map_or("null".into(), |v| (v / 1000.0).to_string()),
        latency.map_or("null".into(), |v| v.to_string()),
        onset.map_or("null".into(), |v| v.to_string()),
        stats.dropped_frames,
        stats.backend_errors,
        stats.invalid_buffers,
        frames == total,
        stats.dropped_frames == 0
            && stats.backend_errors == 0
            && stats.invalid_buffers == 0
            && frames == total
            && clipped == 0,
        midi_log.join(",")
    )?;
    println!(
        "Recorded {frames} frames; dropped {}, clipped {clipped}; MIDI-to-audio onset estimate {latency:?} s.",
        stats.dropped_frames
    );
    if stats.dropped_frames > 0
        || stats.backend_errors > 0
        || stats.invalid_buffers > 0
        || frames != total
        || clipped > 0
    {
        return Err("capture invalid; inspect sidecar and repeat at calibrated level".into());
    }
    // A capture that sent notes but recorded no onset is probably silent:
    // wrong MIDI channel, routing or gain. Several in a row stop the session
    // instead of recording an hour of silence.
    let sent_notes = schedule.iter().any(|e| e.bytes[0] & 0xf0 == 0x90);
    if sent_notes && onset.is_none() {
        let silent = SILENT_RUN.fetch_add(1, Ordering::Relaxed) + 1;
        println!(
            "WARNING: no note onset detected in {id}; check MIDI channel, routing and input gain."
        );
        if silent >= MAX_SILENT_RUN {
            return Err(format!(
                "{silent} consecutive captures without a note onset; stopping. \
                 Run `dw303-capture midi-scan` and `levels` to check the setup."
            )
            .into());
        }
    } else if sent_notes {
        SILENT_RUN.store(0, Ordering::Relaxed);
    }
    Ok(())
}

/// Consecutive note captures without a detected onset (see `record`).
static SILENT_RUN: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
const MAX_SILENT_RUN: usize = 3;

// Terminal input without unsafe signal handlers or a new registry dependency.
// Ctrl-C is read as a byte; it requests exit AFTER the current complete WAV.
// The guard restores the original terminal mode on normal/error unwinding.
struct Operator {
    mode: String,
    lines: mpsc::Receiver<String>,
    finish: Arc<AtomicBool>,
}
impl Operator {
    fn new() -> Result<Self, Error> {
        use std::{
            io::IsTerminal,
            process::{Command, Stdio},
        };
        if !io::stdin().is_terminal() {
            return Err("session requires an interactive terminal for clean Ctrl-C handling; use plan for dry runs".into());
        }
        let saved = Command::new("stty")
            .arg("-g")
            .stdin(Stdio::inherit())
            .output()?;
        if !saved.status.success() {
            return Err("cannot read terminal mode".into());
        }
        let mode = String::from_utf8(saved.stdout)?.trim().to_owned();
        let status = Command::new("stty")
            .args(["-icanon", "-isig", "min", "1", "time", "0"])
            .stdin(Stdio::inherit())
            .status()?;
        if !status.success() {
            return Err("cannot set terminal mode".into());
        }
        let finish = Arc::new(AtomicBool::new(false));
        let requested = finish.clone();
        let (tx, lines) = mpsc::channel();
        thread::spawn(move || {
            use std::io::Read;
            let mut input = io::stdin().lock();
            let mut byte = [0];
            let mut line = Vec::new();
            while input.read_exact(&mut byte).is_ok() {
                match byte[0] {
                    3 => {
                        requested.store(true, Ordering::Relaxed);
                        println!("\nCtrl-C: finishing the current WAV, then exiting.");
                        let _ = tx.send(String::new());
                        break;
                    }
                    b'\n' | b'\r' => {
                        if tx
                            .send(String::from_utf8_lossy(&line).trim().to_owned())
                            .is_err()
                        {
                            break;
                        }
                        line.clear();
                    }
                    8 | 127 => {
                        line.pop();
                    }
                    b => line.push(b),
                }
            }
        });
        Ok(Self {
            mode,
            lines,
            finish,
        })
    }
    fn line(&self) -> Result<String, Error> {
        let line = self.lines.recv()?;
        if self.finish.load(Ordering::Relaxed) {
            return Err(io::Error::new(
                io::ErrorKind::Interrupted,
                "session stopped before next capture",
            )
            .into());
        }
        Ok(line)
    }
}
impl Drop for Operator {
    fn drop(&mut self) {
        let _ = std::process::Command::new("stty")
            .arg(&self.mode)
            .stdin(std::process::Stdio::inherit())
            .status();
    }
}
fn measurement_session(args: &[String]) -> Result<(), Error> {
    let mut manifest = Manifest::parse(&fs::read_to_string(
        args.get(1).ok_or("manifest path required")?,
    )?)?;
    let curated = if args.iter().any(|a| a == "--session-file") {
        Some(session::SessionFile::parse(
            &fs::read_to_string(option(args, "--session-file")?)?,
            &manifest,
        )?)
    } else {
        None
    };
    let mut captures = session::select(&manifest, option(args, "--select")?, curated.as_ref())?;
    let selected = captures.len();
    if args.iter().any(|a| a == "--resume") {
        let rate = if args.iter().any(|a| a == "--rate") {
            option(args, "--rate")?.parse()?
        } else {
            96000
        };
        let out = std::path::Path::new(option(args, "--out")?);
        captures.retain(|c| !session::complete(out, c, rate));
    }
    let captures = session::order(captures);
    if let Some(curated) = &curated {
        println!("Session: {}", curated.name);
    }
    println!(
        "{} already complete; {} pending.",
        selected - captures.len(),
        captures.len()
    );
    let report = session::plan(&captures);
    if args[0] == "plan" {
        print!("{report}");
    } else {
        println!("{}", report.lines().next().unwrap_or("empty plan"));
    }
    if args[0] == "plan" || captures.is_empty() {
        return Ok(());
    }
    // Validate required options before asking the operator anything. Device
    // capabilities are checked by record, after panel confirmation.
    for key in [
        "--audio-device",
        "--channel",
        "--midi-out",
        "--rate",
        "--out",
    ] {
        option(args, key)?;
    }
    if option(args, "--rate")? != "96000" {
        return Err("protocol requires 96000 Hz".into());
    }
    let out = std::path::Path::new(option(args, "--out")?);
    if !args.iter().any(|a| a == "--resume") {
        for capture in &captures {
            if ["wav", "json"]
                .iter()
                .any(|extension| out.join(format!("{}.{}", capture.id, extension)).exists())
            {
                return Err(format!(
                    "{} already exists; use --resume or a new output directory",
                    capture.id
                )
                .into());
            }
        }
    }
    let ids: Vec<_> = captures.iter().map(|c| c.id.clone()).collect();
    drop(captures);
    let operator = Operator::new()?;
    println!(
        "Warm up >=30 min. Neutralise extra features. Confirm session metadata once (Enter preserves manifest values):"
    );
    for key in [
        "serial",
        "firmware",
        "model",
        "power_supply",
        "warmup_minutes",
        "input_calibration_dbfs_per_volt",
        "interface_response_calibration",
        "features",
        "knob_template",
    ] {
        let table = manifest
            .raw
            .as_table_mut()
            .ok_or("manifest table")?
            .entry("session")
            .or_insert_with(|| toml::Value::Table(Default::default()))
            .as_table_mut()
            .ok_or("session table")?;
        println!(
            "{key} [{}]:",
            table.get(key).map_or_else(|| "unset".into(), json)
        );
        let value = operator.line()?;
        if !value.is_empty() {
            table.insert(key.into(), toml::Value::String(value));
        }
    }
    println!("Room temperature C (blank = null):");
    let temperature = temperature(&operator.line()?)?;
    let manual: Vec<_> = ids
        .iter()
        .filter_map(|id| manifest.captures.iter().find(|c| &c.id == id))
        .filter(|c| c.clock && c.notes.is_empty())
        .collect();
    if !manual.is_empty() {
        let first = &manual[0].instructions;
        if manual.iter().any(|c| &c.instructions != first) {
            return Err("session has different stored patterns; split into sessions with one stored pattern each".into());
        }
        println!(
            "Session setup (once): {first}\nPress Enter after programming the stored pattern and enabling MIDI clock reception."
        );
        operator.line()?;
    }
    let mut previous: Option<&Capture> = None;
    for (i, id) in ids.iter().enumerate() {
        if operator.finish.load(Ordering::Relaxed) {
            break;
        }
        let capture = manifest
            .captures
            .iter()
            .find(|c| &c.id == id)
            .ok_or("capture disappeared")?;
        let remaining: Vec<_> = ids[i..]
            .iter()
            .filter_map(|id| manifest.captures.iter().find(|c| &c.id == id))
            .collect();
        let (_, mut seconds) = session::estimate(&remaining);
        if previous.is_some_and(|p| session::distance(p, capture) == 0) {
            seconds += session::SETTLE_SECONDS - session::PROMPT_SECONDS;
        }
        println!(
            "{}/{} {id}; estimated remaining {:.1} min",
            i + 1,
            ids.len(),
            seconds / 60.0
        );
        let changes = session::panel_changes(previous, capture);
        if !changes.is_empty() {
            for line in changes {
                println!("  {line}");
            }
            println!("Press Enter when changed controls match (legend 0–10; VERIFY clock travel).");
            operator.line()?;
        }
        if !capture.instructions.is_empty() {
            println!("{}", capture.instructions);
        }
        if previous.is_some() && session::distance(previous.ok_or("previous panel")?, capture) == 0
        {
            thread::sleep(Duration::from_secs_f64(session::SETTLE_SECONDS));
        }
        if operator.finish.load(Ordering::Relaxed) {
            break;
        }
        session::prepare_paths(
            std::path::Path::new(option(args, "--out")?),
            capture,
            args.iter().any(|a| a == "--resume"),
        )?;
        record(args, &manifest, capture, &temperature)?;
        previous = Some(capture);
    }
    println!("Session finished. Preserve each WAV and JSON sidecar together.");
    Ok(())
}

/// Routing check: play a repeating test note on every MIDI channel and print
/// per-input-channel peak levels twice a second for `--seconds` (default 8).
///
///   dw303-capture levels --audio-device ID --midi-out NAME [--seconds 8]
fn levels(args: &[String]) -> Result<(), Error> {
    let seconds: f64 = if args.iter().any(|s| s == "--seconds") {
        option(args, "--seconds")?.parse()?
    } else {
        8.0
    };
    let audio_id = option(args, "--audio-device")?;
    let info = ravet_device::input_devices()?
        .into_iter()
        .find(|d| d.id == audio_id)
        .ok_or("unknown audio ID")?;
    let config = info
        .configurations
        .iter()
        .find(|c| {
            c.sample_format() == cpal::SampleFormat::F32
                && c.min_sample_rate() <= 96_000
                && c.max_sample_rate() >= 96_000
        })
        .ok_or("device has no f32 input configuration at 96 kHz")?;
    let channels = usize::from(config.channels());
    let rate = 96_000;
    let device = ravet_device::input_device(Some(audio_id))?.ok_or("device disappeared")?;
    let (stream, mut consumer) = ravet_device::open_input(
        &device,
        cpal::StreamConfig {
            channels: channels as u16,
            sample_rate: rate,
            buffer_size: cpal::BufferSize::Default,
        },
        65536,
    )?;
    stream.play()?;
    let midi_name = option(args, "--midi-out")?;
    let port = ravet_midi_device::output_ports()?
        .into_iter()
        .find(|p| p.name == midi_name)
        .ok_or("MIDI output not found")?;
    let mut midi = ravet_midi_device::open_output(&port.id)?;
    println!("{channels} input channels at {rate} Hz; peak dBFS per input every 0.5 s.");
    println!("A C2 note (key 36, velocity 100) is sent on all 16 MIDI channels each second.");
    let mut buffer = vec![0.0f32; channels * 4096];
    let mut peaks = vec![0.0f32; channels];
    let start = Instant::now();
    let mut next_report = 0.5;
    let mut next_note = 0.0;
    let mut note_on = false;
    let mut received = 0usize;
    while start.elapsed().as_secs_f64() < seconds {
        let t = start.elapsed().as_secs_f64();
        if t >= next_note {
            for ch in 0..16u8 {
                let status = if note_on { 0x80 } else { 0x90 } | ch;
                midi.send(&[status, 36, if note_on { 0 } else { 100 }])?;
            }
            note_on = !note_on;
            next_note += 0.5;
        }
        let count = consumer
            .pop(&mut buffer)
            .map_err(|e| format!("ring: {e:?}"))?;
        received += count;
        for frame in buffer[..count * channels].chunks_exact(channels) {
            for (peak, sample) in peaks.iter_mut().zip(frame) {
                *peak = peak.max(sample.abs());
            }
        }
        if t >= next_report {
            let row: Vec<String> = peaks
                .iter()
                .enumerate()
                .map(|(i, p)| format!("{}:{:>4.0}", i + 1, 20.0 * (p.max(1e-9)).log10()))
                .collect();
            println!("{:>4.1}s frames {:>6} | {}", t, received, row.join(" "));
            received = 0;
            peaks.iter_mut().for_each(|p| *p = 0.0);
            next_report += 0.5;
        }
        if count == 0 {
            thread::sleep(Duration::from_millis(2));
        }
    }
    for ch in 0..16u8 {
        let _ = midi.send(&[0xb0 | ch, 123, 0]);
    }
    Ok(())
}

/// Find the unit's MIDI receive channel and playable key range: for each MIDI
/// channel 1..16, play one note and report the peak on the selected input;
/// then, on the loudest channel, try keys 12..96 in octaves.
///
///   dw303-capture midi-scan --audio-device ID --channel N --midi-out NAME
fn midi_scan(args: &[String]) -> Result<(), Error> {
    let input: usize = option(args, "--channel")?.parse()?;
    let audio_id = option(args, "--audio-device")?;
    let info = ravet_device::input_devices()?
        .into_iter()
        .find(|d| d.id == audio_id)
        .ok_or("unknown audio ID")?;
    let config = info
        .configurations
        .iter()
        .find(|c| {
            c.sample_format() == cpal::SampleFormat::F32
                && c.min_sample_rate() <= 96_000
                && c.max_sample_rate() >= 96_000
        })
        .ok_or("device has no f32 input configuration at 96 kHz")?;
    let channels = usize::from(config.channels());
    if input == 0 || input > channels {
        return Err("input channel out of range".into());
    }
    let device = ravet_device::input_device(Some(audio_id))?.ok_or("device disappeared")?;
    let (stream, mut consumer) = ravet_device::open_input(
        &device,
        cpal::StreamConfig {
            channels: channels as u16,
            sample_rate: 96_000,
            buffer_size: cpal::BufferSize::Default,
        },
        65536,
    )?;
    stream.play()?;
    let midi_name = option(args, "--midi-out")?;
    let port = ravet_midi_device::output_ports()?
        .into_iter()
        .find(|p| p.name == midi_name)
        .ok_or("MIDI output not found")?;
    let mut midi = ravet_midi_device::open_output(&port.id)?;
    let mut buffer = vec![0.0f32; channels * 4096];
    // Peak on the selected input over `seconds`, draining the ring.
    let mut measure = |seconds: f64| -> Result<f32, Error> {
        let start = Instant::now();
        let mut peak = 0.0f32;
        while start.elapsed().as_secs_f64() < seconds {
            let count = consumer
                .pop(&mut buffer)
                .map_err(|e| format!("ring: {e:?}"))?;
            for frame in buffer[..count * channels].chunks_exact(channels) {
                peak = peak.max(frame[input - 1].abs());
            }
            if count == 0 {
                thread::sleep(Duration::from_millis(2));
            }
        }
        Ok(peak)
    };
    let db = |p: f32| 20.0 * p.max(1e-9).log10();
    let floor = measure(0.5)?;
    println!("Input {input} noise floor: {:.1} dBFS", db(floor));
    let mut best = (0u8, 0.0f32);
    for ch in 0..16u8 {
        midi.send(&[0x90 | ch, 36, 100])?;
        let peak = measure(0.6)?;
        midi.send(&[0x80 | ch, 36, 0])?;
        let _ = measure(0.4)?;
        println!("MIDI channel {:>2}: {:>6.1} dBFS", ch + 1, db(peak));
        if peak > best.1 {
            best = (ch, peak);
        }
    }
    if db(best.1) < db(floor) + 20.0 {
        println!("No channel produced a note more than 20 dB above the noise floor.");
        return Ok(());
    }
    println!(
        "Unit answers on MIDI channel {}. Key range on that channel:",
        best.0 + 1
    );
    for key in (12..=96).step_by(12) {
        midi.send(&[0x90 | best.0, key, 100])?;
        let peak = measure(0.6)?;
        midi.send(&[0x80 | best.0, key, 0])?;
        let _ = measure(0.4)?;
        println!("  key {key:>3}: {:>6.1} dBFS", db(peak));
    }
    for ch in 0..16u8 {
        let _ = midi.send(&[0xb0 | ch, 123, 0]);
    }
    Ok(())
}
