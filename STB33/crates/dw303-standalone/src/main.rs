//! DW-303 standalone host (macOS, Windows, Linux). Device lifecycle and state
//! I/O stay on the main thread.
// Release builds on Windows are GUI applications without a console window.
#![cfg_attr(all(windows, not(debug_assertions)), windows_subsystem = "windows")]
mod audio;
mod json;
mod protocol;
mod state;
use audio::{Audio, FRAMES, Shared};
use dw303_engine::{
    link::{Display, PanelLink},
    params,
};
use json::{Value, object};
use ravet_device::{
    OutputStream, PreparedOutput,
    cpal::{self, traits::DeviceTrait},
};
use ravet_webview::{Callbacks, StandaloneOptions, StandaloneWindow};
use std::{cell::RefCell, rc::Rc, sync::Arc};
type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
struct Host {
    shared: Arc<Shared>,
    idle: Option<PreparedOutput<Audio>>,
    stream: Option<OutputStream>,
    midi: Vec<ravet_midi_device::MidiInputStream>,
    outputs: Vec<Value>,
    midis: Vec<Value>,
    status: String,
    error: Option<String>,
    display: Display,
    gesture: Option<u32>,
    fixture: bool,
}
impl Host {
    fn new(fixture: bool) -> Result<Self> {
        let mut values = *params::schema()?.defaults().values();
        values[9] = 0.;
        let link = Arc::new(PanelLink::new());
        let mut warning = None;
        if !fixture {
            match state::read(&state::path()?) {
                Ok((v, m)) => {
                    values = v;
                    values[11] = 0.;
                    link.load_memory(m)
                }
                Err(e) => {
                    if !e
                        .downcast_ref::<std::io::Error>()
                        .is_some_and(|e| e.kind() == std::io::ErrorKind::NotFound)
                    {
                        warning = Some(format!("State restore failed: {e}"))
                    }
                }
            }
        }
        let shared = Shared::new(values, link);
        let idle = Some(PreparedOutput::new(
            Audio::new(shared.clone(), Vec::new(), 3),
            48000,
            2,
            FRAMES,
        )?);
        let mut host = Self {
            shared,
            idle,
            stream: None,
            midi: Vec::new(),
            outputs: Vec::new(),
            midis: Vec::new(),
            status: "Choose Audio/MIDI settings and press Start".into(),
            error: warning,
            display: Display::default(),
            gesture: None,
            fixture,
        };
        host.refresh()?;
        Ok(host)
    }
    fn refresh(&mut self) -> Result<()> {
        if self.fixture {
            self.outputs = vec![object([
                ("id", "fixture".into()),
                ("name", "Offline fixture".into()),
                ("channels", Value::Number(2.)),
                ("rates", Value::Array(vec![Value::Number(48000.)])),
                ("default_rate", Value::Number(48000.)),
                ("buffers", Value::Array(vec![Value::Number(512.)])),
            ])];
            self.midis = Vec::new();
            return Ok(());
        }
        let mut outputs = Vec::new();
        for d in ravet_device::output_devices()? {
            let device = ravet_device::output_device(Some(&d.id))?.ok_or("Output disappeared")?;
            let default = device.default_output_config()?;
            let channels = default.channels();
            let ranges = d
                .configurations
                .iter()
                .filter(|r| {
                    r.sample_format() == cpal::SampleFormat::F32 && r.channels() == channels
                })
                .collect::<Vec<_>>();
            if ranges.is_empty() || channels > 64 {
                continue;
            }
            let rates = [44100, 48000, 88200, 96000, 176400, 192000]
                .into_iter()
                .chain(std::iter::once(default.sample_rate()))
                .collect::<std::collections::BTreeSet<_>>()
                .into_iter()
                .filter(|rate| {
                    ranges
                        .iter()
                        .any(|r| r.min_sample_rate() <= *rate && *rate <= r.max_sample_rate())
                })
                .map(|v| Value::Number(f64::from(v)))
                .collect();
            let buffers = [32, 64, 128, 256, 512, 1024, 2048, 4096]
                .into_iter()
                .filter(|frames| {
                    ranges.iter().any(|r| match r.buffer_size() {
                        cpal::SupportedBufferSize::Range { min, max } => {
                            min <= frames && frames <= max
                        }
                        cpal::SupportedBufferSize::Unknown => true,
                    })
                })
                .map(|v| Value::Number(f64::from(v)))
                .collect();
            outputs.push(object([
                ("id", d.id.into()),
                ("name", d.name.into()),
                ("channels", Value::Number(f64::from(channels))),
                ("rates", Value::Array(rates)),
                (
                    "default_rate",
                    Value::Number(f64::from(default.sample_rate())),
                ),
                ("buffers", Value::Array(buffers)),
            ]));
        }
        self.outputs = outputs;
        // A machine without a MIDI backend (for example Linux without the ALSA
        // sequencer) still plays from the panel; it just lists no MIDI inputs.
        self.midis = ravet_midi_device::input_ports()
            .map(|ports| {
                ports
                    .into_iter()
                    .map(|p| object([("id", p.id.into()), ("name", p.name.into())]))
                    .collect()
            })
            .unwrap_or_default();
        Ok(())
    }
    fn stop(&mut self) -> Result<()> {
        if let Some(stream) = self.stream.take() {
            let _ = stream.pause();
            drop(stream);
        }
        for stream in self.midi.drain(..) {
            drop(stream.close())
        }
        if self.idle.is_none() {
            self.idle = Some(PreparedOutput::new(
                Audio::new(self.shared.clone(), Vec::new(), 3),
                48000,
                2,
                FRAMES,
            )?)
        }
        self.render_idle();
        self.status = "Audio stopped".into();
        Ok(())
    }
    fn render_idle(&mut self) {
        if let Some(renderer) = &mut self.idle {
            let mut samples = [0.; 960];
            renderer.render(&mut samples);
        }
    }
    fn start(&mut self, m: &Value) -> Result<()> {
        if self.stream.is_some() {
            return Err("Stop audio before changing devices".into());
        }
        let output = m["output"]
            .as_str()
            .filter(|s| !s.is_empty())
            .ok_or("Choose an output")?;
        let rate = u32::try_from(m["sample_rate"].as_u64().ok_or("Invalid sample rate")?)?;
        let buffer = u32::try_from(m["buffer_size"].as_u64().ok_or("Invalid buffer size")?)?;
        let ids = m["midis"]
            .as_array()
            .filter(|a| a.len() <= 16)
            .ok_or("Select at most 16 MIDI inputs")?;
        let mut unique = std::collections::BTreeSet::new();
        for id in ids {
            let id = id.as_str().ok_or("Invalid MIDI ID")?;
            if !unique.insert(id) {
                return Err("Duplicate MIDI input".into());
            }
        }
        if self.fixture {
            if output != "fixture" || rate != 48000 || !matches!(buffer, 0 | 512) || !ids.is_empty()
            {
                return Err("Invalid fixture settings".into());
            }
            channel_mask(&m["output_pairs"], 2)?;
            self.status = "Offline fixture prepared".into();
            return Ok(());
        }
        // "default" = the system's current default output (used for first launch).
        let device = ravet_device::output_device((output != "default").then_some(output))?
            .ok_or("Output unavailable")?;
        let channels = device.default_output_config()?.channels();
        let mask = channel_mask(&m["output_pairs"], usize::from(channels))?;
        let supported = device.supported_output_configs()?.any(|r| {
            r.sample_format() == cpal::SampleFormat::F32
                && r.channels() == channels
                && r.min_sample_rate() <= rate
                && rate <= r.max_sample_rate()
                && (buffer == 0
                    || match r.buffer_size() {
                        cpal::SupportedBufferSize::Range { min, max } => {
                            *min <= buffer && buffer <= *max
                        }
                        cpal::SupportedBufferSize::Unknown => true,
                    })
        });
        if !supported || channels > 64 || !(8000..=192000).contains(&rate) || buffer > 65536 {
            return Err("Unsupported audio configuration".into());
        }
        self.render_idle();
        let mut receivers = Vec::new();
        let mut streams = Vec::new();
        for id in unique {
            let (stream, receiver) = ravet_midi_device::open_input(id, 256)?;
            streams.push(stream);
            receivers.push(receiver)
        }
        let config = cpal::StreamConfig {
            channels,
            sample_rate: rate,
            buffer_size: if buffer == 0 {
                cpal::BufferSize::Default
            } else {
                cpal::BufferSize::Fixed(buffer)
            },
        };
        // The idle renderer is not processing while the live owner is prepared.
        let stream = match ravet_device::open_output(
            &device,
            config,
            FRAMES,
            Audio::new(self.shared.clone(), receivers, mask),
        ) {
            Ok(stream) => stream,
            // The device could not run at the requested rate (another app
            // holds it, or a virtual device runs at its own rate). Retry once
            // at the rate it really runs at rather than playing off pitch.
            Err(ravet_device::OpenError::SampleRateMismatch { actual, .. })
                if actual != rate && m["retried_rate"] != Value::Bool(true) =>
            {
                let Value::Object(mut retry) = m.clone() else {
                    return Err("Invalid settings".into());
                };
                retry.insert("sample_rate".into(), Value::Number(f64::from(actual)));
                retry.insert("retried_rate".into(), Value::Bool(true));
                self.render_idle();
                return self.start(&Value::Object(retry));
            }
            Err(e) => return Err(e.into()),
        };
        stream.play()?;
        self.idle = None;
        self.midi = streams;
        self.stream = Some(stream);
        self.status = format!("Audio running at {rate} Hz");
        // Remember the working configuration so the next launch starts audio
        // automatically (best effort: a failed save only loses auto-start).
        if let Ok(path) = audio_settings_path() {
            let _ = std::fs::create_dir_all(path.parent().unwrap_or(&path));
            let mut saved = m.clone();
            if let Value::Object(fields) = &mut saved {
                fields.remove("retried_rate");
            }
            let _ = std::fs::write(&path, saved.to_string());
        }
        Ok(())
    }

    /// Start audio at launch: the last working settings if their device is
    /// still present, otherwise the system default output at its current
    /// nominal sample rate (so no rate change is requested). Failures leave
    /// the app running with the reason in the status line.
    fn auto_start(&mut self) {
        let saved = audio_settings_path()
            .and_then(|path| Ok(std::fs::read_to_string(path)?))
            .ok()
            .and_then(|text| json::parse(&text).ok());
        if let Some(saved) = saved
            && self.start(&saved).is_ok()
        {
            return;
        }
        let default_rate = ravet_device::output_device(None)
            .ok()
            .flatten()
            .and_then(|d| d.default_output_config().ok())
            .map(|c| c.sample_rate());
        let Some(rate) = default_rate else {
            self.status = "No default audio output; choose Audio/MIDI settings".into();
            return;
        };
        let message = object([
            ("type", "action".into()),
            ("action", "start".into()),
            ("output", "default".into()),
            ("sample_rate", Value::Number(f64::from(rate))),
            ("buffer_size", Value::Number(0.)),
            ("output_pairs", Value::Array(vec![Value::Number(0.)])),
            ("midis", Value::Array(Vec::new())),
        ]);
        if let Err(e) = self.start(&message) {
            self.status =
                format!("Audio did not start automatically: {e}. Open Audio/MIDI settings.");
        }
    }
    fn handle(&mut self, text: &str) -> Result<()> {
        let m = json::parse(text)?;
        if m["type"] == "action" {
            match m["action"].as_str() {
                Some("refresh") => self.refresh()?,
                Some("stop") => self.stop()?,
                Some("start") => self.start(&m)?,
                Some("panic") => {
                    self.shared.push(params::RUN, 0.)?;
                    self.stop()?
                }
                _ => return Err("Unknown settings action".into()),
            }
            return Ok(());
        }
        if self.shared.link.load_panic_message(text) {
            return Ok(());
        }
        if self.shared.link.load_pattern_message(text) {
            return Ok(());
        }
        if let Some(action) = protocol::parse_panel(text)? {
            self.shared
                .link
                .push(action)
                .map_err(|_| "Panel queue full; repeat the edge")?;
            return Ok(());
        }
        if m.as_object().is_none_or(|o| o.len() != 3) {
            return Err("Invalid parameter message".into());
        }
        let id = u32::try_from(m["id"].as_u64().ok_or("Invalid parameter ID")?)?;
        let index = params::DESCRIPTORS
            .iter()
            .position(|d| d.id.0 == id)
            .ok_or("Unknown parameter")?;
        let value = m["value"].as_f64().ok_or("Invalid parameter value")? as f32;
        if !value.is_finite() {
            return Err("Invalid value".into());
        }
        match m["type"].as_str() {
            Some("begin") if self.gesture.is_none() => self.gesture = Some(id),
            Some("end") if self.gesture == Some(id) => self.gesture = None,
            Some("value") if self.gesture == Some(id) => {
                self.shared.push(params::DESCRIPTORS[index].id, value)?
            }
            Some("edit") if self.gesture.is_none() => {
                self.shared.push(params::DESCRIPTORS[index].id, value)?
            }
            _ => return Err("Invalid gesture sequence".into()),
        }
        Ok(())
    }
    fn snapshot(&mut self) -> Value {
        if let Some(d) = self.shared.link.display() {
            self.display = d
        }
        let d = self.display;
        // Genuine failures are faults; late buffers (CoreAudio overload xruns)
        // are reported as an informational dropout count (RAVET 944011d).
        let shown_status = match self.stream.as_ref().map(|s| s.statistics()) {
            Some(s) if s.panics != 0 || s.processor_faults != 0 || s.backend_errors != 0 => {
                "Audio fault; stop and select the device again".to_string()
            }
            Some(s) if s.xruns != 0 => format!(
                "{} · {} dropout{}",
                self.status,
                s.xruns,
                if s.xruns == 1 { "" } else { "s" }
            ),
            _ => self.status.clone(),
        };
        let params = Value::Object(
            params::DESCRIPTORS
                .iter()
                .zip(self.shared.values())
                .map(|(d, v)| (d.id.0.to_string(), v.into()))
                .collect(),
        );
        object([("params",params),("leds",Value::Object(protocol::panel_lamps(d))),("selection",object([("mode",d.mode.into()),("group",d.group.into()),("track",d.track.into()),("section",if d.section_b{"B"}else{"A"}.into()),("pattern",(d.pattern+1).into()),("step",(d.step+1).into()),("editMode",d.edit_mode.into())])),("error",self.error.clone().map(Value::from).unwrap_or(Value::Null)),("pending",self.shared.pending().into()),("running",self.stream.is_some().into()),("status",shown_status.into()),("io",object([("audio",true.into()),("input",false.into()),("midi",true.into()),("outputs",Value::Array(self.outputs.clone())),("inputs",Value::Array(Vec::new())),("midis",Value::Array(self.midis.clone())),("note","Stop audio to change settings. Notes play when RUN is stopped; SYNC 2 follows MIDI clock.".into())]))])
    }
}
/// Last working Audio/MIDI settings (the exact Start message), beside state.bin.
fn audio_settings_path() -> Result<std::path::PathBuf> {
    Ok(state::path()?.with_file_name("audio.json"))
}
fn channel_mask(pairs: &Value, channels: usize) -> Result<u64> {
    let pairs = pairs
        .as_array()
        .filter(|p| !p.is_empty() && p.len() <= 32)
        .ok_or("Select an output pair")?;
    let mut mask = 0u64;
    for p in pairs {
        let first = usize::try_from(p.as_u64().ok_or("Invalid output pair")?)?;
        if first >= channels || first >= 64 || first % 2 != 0 || mask & (1u64 << first) != 0 {
            return Err("Invalid or duplicate output pair".into());
        }
        mask |= 1u64 << first;
        if first + 1 < channels {
            mask |= 1u64 << (first + 1)
        }
    }
    Ok(mask)
}
struct Bridge(Rc<RefCell<Host>>);
impl Callbacks for Bridge {
    fn message(&mut self, text: &str) {
        let mut h = self.0.borrow_mut();
        match h.handle(text) {
            Ok(()) => h.error = None,
            Err(e) => {
                h.status = e.to_string();
                h.error = Some(e.to_string())
            }
        }
    }
    fn snapshot(&mut self) -> Option<String> {
        Some(self.0.borrow_mut().snapshot().to_string())
    }
}
fn main() -> Result<()> {
    let args = std::env::args().skip(1).collect::<Vec<_>>();
    let fixture = args == ["--ui-test"];
    if !args.is_empty() && !fixture {
        return Err("Usage: DW-303 [--ui-test]".into());
    }
    let host = Rc::new(RefCell::new(Host::new(fixture)?));
    let window = StandaloneWindow::new(StandaloneOptions {
        title: "STB33 by Asher Swissa".into(),
        width: 1227.,
        height: 701.,
        lock_aspect_ratio: true,
        io_settings: true,
        hidden: fixture,
    })?;
    let html = ravet_surface::embed_web_controls(include_str!("../../../editor/index.html"))
        .ok_or("Missing editor placeholders")?;
    let editor = window.embed(&html, Bridge(host.clone()))?;
    editor.set_visible(true);
    if fixture {
        for _ in 0..80 {
            window.pump(0.01);
            host.borrow_mut().render_idle();
            if window.eval("typeof window.ravetUpdate==='function' && !!document.getElementById('io-settings') && !!window.RavetControls")?{break}
        }
        if !window.eval("typeof window.ravetUpdate==='function' && !!document.getElementById('io-settings') && !!window.RavetControls")?{return Err("Editor/settings failed to load".into())}
        if !window.open_settings() {
            return Err("Settings failed to open".into());
        }
        window.eval("window.webkit.messageHandlers.ravet.postMessage({type:'edit',id:2,value:0.73});window.webkit.messageHandlers.ravet.postMessage({type:'panel',action:'button',key:'run-stop',edge:'press'});window.webkit.messageHandlers.ravet.postMessage({type:'panel',action:'button',key:'run-stop',edge:'release'});true")?;
        for _ in 0..20 {
            window.pump(0.01);
            host.borrow_mut().render_idle()
        }
        let h = host.borrow();
        if (h.shared.values()[1] - 0.73).abs() > 1e-6
            || !h.shared.link.display().is_some_and(|d| d.running)
        {
            return Err("Editor messages did not reach engine".into());
        }
        drop(h);
        window.request_close();
        println!("Hidden editor, settings, parameter and RUN fixture passed (no devices opened).");
    } else {
        host.borrow_mut().auto_start();
        while window.state().is_some_and(|s| !s.closed) {
            window.pump(0.01);
            host.borrow_mut().render_idle();
        }
    }
    host.borrow_mut().stop()?;
    if !fixture {
        let h = host.borrow();
        state::write(&state::path()?, h.shared.values(), &h.shared.link.memory())?
    }
    drop(editor);
    drop(window);
    Ok(())
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn selected_pattern_clear_chord_reaches_memory_and_playback() {
        let mut h = Host::new(true).unwrap();
        let index = 3 * 16; // IV A1
        let mut memory = h.shared.link.memory();
        memory.bank.patterns[index].length = 5;
        memory.bank.patterns[index].triplet = true;
        memory.bank.patterns[index].time = [dw303_sequencer::Time::Note; 16];
        h.shared.link.load_memory(memory);
        h.handle(r#"{"type":"panel","action":"mode","value":3}"#)
            .unwrap();
        h.handle(r#"{"type":"panel","action":"pattern-group","value":6}"#)
            .unwrap();
        h.render_idle();
        // PATT.GROUP alone does not change the selected pattern.
        let selection = h.snapshot()["selection"].clone();
        assert_eq!(selection["group"].as_u64(), Some(0));
        assert_eq!(selection["editMode"].as_u64(), Some(0));
        let before = h.shared.link.memory();
        // These are the four WebKit messages emitted by Shift-click PATTERN CLEAR.
        // Use the physical white key, not the Editor-only Number alias.
        for (key, edge) in [
            ("key-c", "press"),
            ("clear", "press"),
            ("clear", "release"),
            ("key-c", "release"),
        ] {
            h.handle(&format!(
                r#"{{"type":"panel","action":"button","key":"{key}","edge":"{edge}"}}"#
            ))
            .unwrap();
            // Edges may land in separate audio blocks.
            h.render_idle();
        }
        // The chord's pattern key adopts the knob's group: IV A1.
        let selection = h.snapshot()["selection"].clone();
        assert_eq!(selection["group"].as_u64(), Some(3));
        assert_eq!(selection["pattern"].as_u64(), Some(1));
        let after = h.shared.link.memory();
        let cleared = after.bank.patterns[index];
        assert_eq!(cleared.steps, before.bank.patterns[index].steps);
        assert_eq!(cleared.length, 16);
        assert!(!cleared.triplet);
        assert_eq!(cleared.time, [dw303_sequencer::Time::Rest; 16]);
        let mut expected = before;
        expected.bank.patterns[index] = cleared;
        assert_eq!(after, expected);
        h.handle(r#"{"type":"panel","action":"button","key":"run-stop","edge":"press"}"#)
            .unwrap();
        h.handle(r#"{"type":"panel","action":"button","key":"run-stop","edge":"release"}"#)
            .unwrap();
        let mut samples = [0.; 960];
        // More than two complete patterns at the default 120 BPM.
        for _ in 0..500 {
            h.idle.as_mut().unwrap().render(&mut samples);
            assert!(samples.iter().all(|&s| s == 0.));
        }
        assert!(h.shared.link.display().unwrap().running);
        assert_eq!(h.shared.link.memory(), after);
    }
    #[test]
    fn panel_protocol_and_parameter_gestures() {
        let mut h = Host::new(true).unwrap();
        assert!(h.handle(r#"{"type":"edit","id":99,"value":0}"#).is_err());
        assert!(h.handle(r#"{"type":"value","id":2,"value":0.9}"#).is_err());
        h.handle(r#"{"type":"begin","id":2,"value":0.5}"#).unwrap();
        h.handle(r#"{"type":"value","id":2,"value":0.9}"#).unwrap();
        h.handle(r#"{"type":"end","id":2,"value":0.9}"#).unwrap();
        h.handle(r#"{"type":"panel","action":"button","key":"run-stop","edge":"press"}"#)
            .unwrap();
        h.handle(r#"{"type":"panel","action":"button","key":"run-stop","edge":"release"}"#)
            .unwrap();
        h.render_idle();
        assert_eq!(h.shared.values()[1], 0.9);
        assert_eq!(h.shared.values()[9], 0.);
        assert!(h.shared.link.display().unwrap().running);
        assert!(
            h.handle(r#"{"type":"panel","action":"mode","value":4}"#)
                .is_err()
        );
        assert!(channel_mask(&json::parse("[0,0]").unwrap(), 2).is_err());
        let snapshot = h.snapshot();
        assert!(snapshot["params"].as_object().unwrap().len() == 12);
        assert!(snapshot["leds"].as_object().unwrap().contains_key("run"));
    }
}
