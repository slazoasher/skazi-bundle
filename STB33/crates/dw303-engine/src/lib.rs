//! The DW-303 processor shared by every plug-in format and the standalone app:
//! sequencer -> circuit voice, with parameters applied sample-accurately.
//!
//! Pattern memory and the original panel editor live on the audio thread.
pub mod factory;
pub mod fx;
pub mod link;
pub mod params;
use link::{Button, Editor, Memory, PanelAction, PanelLink};
use std::sync::Arc;

use dw303_sequencer::{ClockSource, Octave, Pattern, Sequencer, Step, Time, VoiceCommand};
use dw303_vcf::Quality;
use dw303_voice::{Knobs, Voice, VoiceInput};
use ravet_foundation::{
    EventData, ParameterState, PrepareError, ProcessBlock, ProcessConfig, ProcessError,
    ProcessStatus, Processor, Tail,
};

/// MIDI key of the voice's lowest C (sequencer note index 0 = 1.000 V = 32.70 Hz, C1).
pub const LOWEST_C_KEY: u8 = 24;

/// Circuit voice and audio-thread front-panel sequencer.
pub struct Dw303Engine {
    config: Option<ProcessConfig>,
    params: ParameterState<'static, { params::COUNT }>,
    sequencer: Sequencer,
    voice: Voice,
    memory: Memory,
    editor: Editor,
    link: Option<Arc<PanelLink>>,
    memory_generation: u32,
    dirty: bool,
    last_command: VoiceCommand,
    preview_active: bool,
    previews: [VoiceCommand; link::CAPACITY],
    preview_head: usize,
    preview_count: usize,
    /// Fixed digital output gain (linear); 1.0 in reference-level mode.
    output_gain: f64,
    fx: Option<fx::Fx>,
    fx_knobs: fx::FxKnobs,
}

/// Fixed output gain after the circuit model. The circuit is calibrated so
/// that 0 dB matches a TD-3 at VOLUME 10 into a line input at unity gain
/// (about −20 dBFS for a typical note), which is too quiet for an instrument.
/// The VOLUME knob adjusts level from there, as on the hardware.
pub const OUTPUT_GAIN_DB: f64 = 18.0;

impl Dw303Engine {
    /// Output at the circuit's calibrated reference level (no fixed output
    /// gain), as used for TD-3 comparisons by the offline renderer.
    pub fn set_reference_level(&mut self, reference: bool) {
        self.output_gain = if reference {
            1.0
        } else {
            10f64.powf(OUTPUT_GAIN_DB / 20.0)
        };
    }

    /// Construct with factory memory and no shared panel.
    pub fn new() -> Self {
        Self {
            config: None,
            params: params::schema().expect("valid schema").defaults(),
            sequencer: Sequencer::default(),
            voice: Voice::new(),
            memory: factory_memory(),
            editor: Editor::new(),
            link: None,
            memory_generation: 0,
            dirty: true,
            last_command: VoiceCommand::default(),
            preview_active: false,
            previews: [VoiceCommand::default(); link::CAPACITY],
            preview_head: 0,
            preview_count: 0,
            output_gain: 10f64.powf(OUTPUT_GAIN_DB / 20.0),
            fx: None,
            fx_knobs: fx::FxKnobs::default(),
        }
    }

    /// Construct with a shared front-panel exchange.
    pub fn with_link(link: Arc<PanelLink>) -> Self {
        let memory = link.memory();
        Self {
            memory,
            link: Some(link),
            ..Self::new()
        }
    }

    /// Last sequencer/preview command supplied to the voice (MIDI note is pre-offset).
    pub fn last_command(&self) -> VoiceCommand {
        self.last_command
    }

    /// Current parameter values.
    pub fn parameters(&self) -> &ParameterState<'static, { params::COUNT }> {
        &self.params
    }

    /// Selected pattern memory.
    pub fn pattern(&self) -> &Pattern {
        &self.memory.bank.patterns[self.editor.selection().pattern.index().unwrap_or(0)]
    }

    /// Replace the selected pattern.
    pub fn set_pattern(&mut self, pattern: Pattern) {
        self.memory.bank.patterns[self.editor.selection().pattern.index().unwrap_or(0)] = pattern;
        self.dirty = true;
    }

    /// Circuit solver diagnostics.
    pub fn vcf_diagnostics(&self) -> dw303_vcf::VcfDiagnostics {
        self.voice.vcf_diagnostics()
    }

    fn apply(&mut self, data: EventData) {
        match data {
            EventData::ParameterValue { id, value } => {
                if let Some(index) = params::schema().ok().and_then(|s| s.index(id))
                    && let Some(value) = params::DESCRIPTORS[index].range.canonicalize(value)
                {
                    let _ = self.params.set(id, value);
                    if id == params::RUN && (value >= 0.5) != self.editor.running() {
                        self.panel_action(PanelAction::Press(Button::RunStop));
                        self.panel_action(PanelAction::Release(Button::RunStop));
                    }
                    self.update_controls();
                }
            }
            other => {
                self.sequencer.event(other);
                self.reflect_transport();
            }
        }
    }

    /// Push parameter values to the voice and clock.
    fn update_controls(&mut self) {
        let params = self.params;
        let v = |id| f64::from(params.get(id).unwrap_or_default());
        self.voice.set_knobs(Knobs {
            tuning: v(params::TUNING),
            cutoff: v(params::CUTOFF),
            resonance: v(params::RESONANCE),
            env_mod: v(params::ENV_MOD),
            decay: v(params::DECAY),
            accent: v(params::ACCENT),
            volume: v(params::VOLUME),
            square: v(params::WAVEFORM) >= 0.5,
        });
        self.voice.set_glide(v(params::GLIDE), v(params::PITCH_GLIDE));
        self.fx_knobs = fx::FxKnobs {
            attack: v(params::ATTACK),
            decay: v(params::AMP_DECAY),
            sustain: v(params::SUSTAIN),
            release: v(params::RELEASE),
            drive: v(params::DRIVE),
            ott: v(params::OTT),
            reverb: v(params::REVERB),
            width: v(params::WIDTH),
        };
        self.voice.set_quality(if v(params::QUALITY) >= 0.5 {
            Quality::Full
        } else {
            Quality::Economy
        });
        let source = match v(params::SYNC_MODE).round() as i32 {
            0 => ClockSource::Internal,
            1 => ClockSource::Host,
            _ => ClockSource::Midi,
        };
        let changed_source = self.sequencer.clock.source() != source;
        self.sequencer.clock.set_source(source);
        if changed_source && source == ClockSource::Internal && self.editor.running() {
            self.sequencer.clock.start();
        }
        if source == ClockSource::Internal {
            self.sequencer.clock.set_tempo(v(params::TEMPO));
        }
    }

    fn reflect_transport(&mut self) {
        let running = self.sequencer.clock.is_running();
        if running != self.editor.running() {
            let _ = self
                .editor
                .action(&mut self.memory, PanelAction::Press(Button::RunStop));
            let _ = self
                .editor
                .action(&mut self.memory, PanelAction::Release(Button::RunStop));
        }
    }
    fn panel_action(&mut self, action: PanelAction) {
        if let Ok(out) = self.editor.action(&mut self.memory, action) {
            self.dirty |= out.changed;
            if let Some(preview) = out.preview {
                self.preview_active = preview.gate;
                if matches!(
                    action,
                    PanelAction::Mode(_) | PanelAction::Press(Button::RunStop)
                ) {
                    self.preview_head = 0;
                    self.preview_count = 0;
                } else if self.preview_count < link::CAPACITY {
                    self.previews[(self.preview_head + self.preview_count) % link::CAPACITY] =
                        preview;
                    self.preview_count += 1;
                }
            }
            self.editor.apply_transport(&mut self.sequencer, out);
            let _ = self
                .params
                .set(params::RUN, f32::from(self.editor.running()));
        }
    }
    fn panel_block(&mut self) {
        if let Some(link) = self.link.clone() {
            if link.take_panic() {
                // PANIC: stop the internal sequencer, drop queued notes, silence voice and FX tails.
                if self.editor.running() {
                    self.panel_action(PanelAction::Press(Button::RunStop));
                    self.panel_action(PanelAction::Release(Button::RunStop));
                }
                self.preview_active = false;
                self.preview_head = 0;
                self.preview_count = 0;
                self.sequencer.reset();
                self.voice.reset();
                if let Some(fx) = &mut self.fx {
                    fx.clear();
                }
                let _ = self.params.set(params::RUN, 0.0);
            }
            if let Some(memory) = link.adopt_memory(&mut self.memory_generation) {
                self.memory = memory;
                self.preview_active = false;
                self.preview_head = 0;
                self.preview_count = 0;
                self.editor = Editor::new();
                self.sequencer.reset();
                self.voice.reset();
                self.dirty = true;
                let _ = self.params.set(params::RUN, 0.0);
            }
            for _ in 0..link::CAPACITY {
                if self.preview_count == link::CAPACITY {
                    break;
                }
                if let Some(action) = link.pop() {
                    self.panel_action(action);
                } else {
                    break;
                }
            }
            if self.dirty && link.publish_memory(&self.memory) {
                self.dirty = false;
            }
        }
    }

    fn voice_input(&self, command: VoiceCommand, sequencing: bool) -> VoiceInput {
        // Stopped: the command comes from MIDI keys, mapped onto the voice range.
        let note = if sequencing || self.preview_active {
            command.note
        } else {
            command.note.saturating_sub(LOWEST_C_KEY)
        };
        VoiceInput {
            note,
            gate: command.gate,
            trigger: command.trigger,
            accent: command.accent,
            slide: command.slide,
        }
    }
}

impl Default for Dw303Engine {
    fn default() -> Self {
        Self::new()
    }
}

impl Processor for Dw303Engine {
    fn prepare(&mut self, config: ProcessConfig) -> Result<(), PrepareError> {
        let rate = config.sample_rate();
        self.voice
            .prepare(rate)
            .map_err(|_| PrepareError::InvalidSampleRate)?;
        self.sequencer
            .prepare(rate)
            .map_err(|_| PrepareError::InvalidSampleRate)?;

        self.update_controls();
        self.voice.reset();
        self.fx = Some(fx::Fx::new(rate));
        self.config = Some(config);
        Ok(())
    }

    fn process(&mut self, block: &mut ProcessBlock<'_, '_, '_>) -> ProcessStatus {
        let Some(config) = self.config else {
            block.clear_outputs();
            return ProcessStatus::Fault(ProcessError::NotPrepared);
        };
        if let Err(error) = config.validate_block(block) {
            block.clear_outputs();
            return ProcessStatus::Fault(error);
        }
        self.panel_block();
        self.sequencer.clock.begin_host_block(block.transport);
        self.reflect_transport();
        let frames = block.audio.frames();
        let events = block.events;
        let mut next = 0;
        for frame in 0..frames {
            while next < events.len() && events[next].frame_offset == frame {
                self.apply(events[next].data);
                next += 1;
            }
            let sequencing = self.sequencer.clock.is_running();
            let mut command = self.editor.tick(&mut self.sequencer, &self.memory);
            let preview = !sequencing && self.preview_count > 0;
            if self.preview_count > 0 {
                if preview {
                    command = self.previews[self.preview_head];
                }
                self.preview_head = (self.preview_head + 1) % link::CAPACITY;
                self.preview_count -= 1;
            }
            self.last_command = command;
            let mut input = self.voice_input(command, sequencing || preview);
            let (gate, trigger) = (input.gate, input.trigger);
            let knobs = self.fx_knobs;
            if let Some(fx) = &self.fx {
                input.gate |= fx.holding(&knobs);
            }
            let dry = self.voice.tick(input) * self.output_gain;
            let (left, right) = match &mut self.fx {
                Some(fx) => fx.tick(&knobs, dry, gate, trigger),
                None => (dry, dry),
            };
            for channel in 0..block.audio.output_channels() {
                if let Some(out) = block.audio.output_mut(channel) {
                    out[frame] = if channel % 2 == 0 { left } else { right } as f32;
                }
            }
        }
        for event in &events[next..] {
            self.apply(event.data);
        }
        let running = self.sequencer.clock.is_running();
        self.reflect_transport();
        let _ = self.params.set(params::RUN, f32::from(running));
        if let Some(link) = &self.link {
            link.set_host_bpm(block.transport.tempo);
            link.publish_display(&self.editor, &self.memory, running);
        }
        ProcessStatus::Continue
    }

    fn reset(&mut self) {
        self.sequencer.reset();
        if self.editor.running() {
            self.sequencer.clock.start();
        }
        self.voice.reset();
        if let Some(config) = self.config {
            self.fx = Some(fx::Fx::new(config.sample_rate()));
        }
        self.update_controls();
    }

    fn deactivate(&mut self) {
        self.config = None;
    }

    fn tail(&self) -> Tail {
        Tail::Infinite
    }
}

/// Factory pattern in group I, section A, pattern 1.
pub fn demo_pattern() -> Pattern {
    let notes: [(u8, Octave, bool, bool); 16] = [
        (0, Octave::Mid, true, false),
        (0, Octave::Mid, false, false),
        (12, Octave::Mid, false, true),
        (0, Octave::Mid, false, false),
        (3, Octave::Mid, true, false),
        (0, Octave::Mid, false, false),
        (7, Octave::Mid, false, true),
        (5, Octave::Mid, false, false),
        (0, Octave::Mid, true, false),
        (10, Octave::Mid, false, false),
        (0, Octave::Up, false, true),
        (0, Octave::Mid, false, false),
        (3, Octave::Mid, true, false),
        (7, Octave::Down, false, false),
        (5, Octave::Mid, false, true),
        (7, Octave::Mid, true, false),
    ];
    let mut pattern = Pattern::default();
    for (step, (pitch, octave, accent, slide)) in pattern.steps.iter_mut().zip(notes) {
        *step = Step {
            pitch,
            octave,
            accent,
            slide,
        };
    }
    pattern.time = [Time::Note; 16];
    pattern.time[5] = Time::Rest;
    pattern.time[11] = Time::Tie;
    pattern
}

/// Complete factory bank; other patterns and tracks use codec defaults.
pub fn factory_memory() -> Memory {
    let mut memory = Memory::default();
    memory.bank.patterns[0] = demo_pattern();
    factory::install(&mut memory);
    memory
}

#[cfg(test)]
mod tests {
    use super::*;
    use link::Mode;
    use ravet_foundation::{AudioBuffer, AudioLayout, Event, Transport};
    fn tap(link: &PanelLink, button: Button) {
        link.push(PanelAction::Press(button)).unwrap();
        link.push(PanelAction::Release(button)).unwrap();
    }
    fn render(engine: &mut Dw303Engine) {
        let mut left = [0.0; 64];
        let mut right = [0.0; 64];
        let mut outputs = [&mut left[..], &mut right[..]];
        let audio = AudioBuffer::separate(&[], &mut outputs, 64).unwrap();
        let mut block = ProcessBlock::new(audio, &[], Transport::default());
        assert_eq!(engine.process(&mut block), ProcessStatus::Continue);
        assert!(left.iter().chain(&right).all(|s| s.is_finite()));
    }
    #[test]
    fn reactivation_keeps_published_memory_and_preview_edges_trigger() {
        let link = Arc::new(PanelLink::new());
        let mut engine = Dw303Engine::with_link(link.clone());
        engine
            .prepare(ProcessConfig::new(48000.0, 64, 128, AudioLayout::STEREO_INSTRUMENT).unwrap())
            .unwrap();
        let mut pattern = demo_pattern();
        pattern.length = 4;
        engine.set_pattern(pattern);
        render(&mut engine);
        assert_eq!(Dw303Engine::with_link(link.clone()).pattern().length, 4);
        link.push(PanelAction::Mode(Mode::PatternWrite)).unwrap();
        tap(&link, Button::PitchMode);
        tap(&link, Button::Pitch(2));
        engine.panel_block();
        assert!(
            engine
                .previews
                .iter()
                .take(engine.preview_count)
                .any(|p| p.gate && p.trigger && p.note == 14)
        );
    }
    /// STB33 panel: PITCH MODE pressed while a pattern plays stops it and enters pitch write.
    #[test]
    fn pitch_mode_while_running_enters_pitch_write() {
        let link = Arc::new(PanelLink::new());
        let mut engine = Dw303Engine::with_link(link.clone());
        engine
            .prepare(ProcessConfig::new(48000.0, 64, 128, AudioLayout::STEREO_INSTRUMENT).unwrap())
            .unwrap();
        engine.apply(EventData::ParameterValue { id: params::SYNC_MODE, value: 0.0 });
        tap(&link, Button::RunStop);
        for _ in 0..200 {
            render(&mut engine);
        }
        assert!(link.display().unwrap().running, "pattern should be running");
        // What the panel sends for one PITCH MODE click while running, one message per block.
        for action in [
            PanelAction::Press(Button::RunStop),
            PanelAction::Release(Button::RunStop),
            PanelAction::Mode(Mode::PatternWrite),
            PanelAction::Press(Button::PitchMode),
            PanelAction::Release(Button::PitchMode),
        ] {
            link.push(action).unwrap();
            for _ in 0..3 {
                render(&mut engine);
            }
            let d = link.display().unwrap();
            eprintln!("{action:?} -> mode {} edit {} running {}", d.mode, d.edit_mode, d.running);
        }
        for _ in 0..20 {
            render(&mut engine);
        }
        let d = link.display().unwrap();
        eprintln!("mode {} edit {} running {} leds {:?}", d.mode, d.edit_mode, d.running, d.leds.modes);
        assert!(!d.running && d.mode == 3 && d.edit_mode == 1);
    }
    /// STB33 panel: in Pattern Write, PITCH MODE lights its lamp and a key sounds.
    #[test]
    fn pitch_mode_lights_and_key_previews_a_note() {
        let link = Arc::new(PanelLink::new());
        let mut engine = Dw303Engine::with_link(link.clone());
        engine
            .prepare(ProcessConfig::new(48000.0, 64, 128, AudioLayout::STEREO_INSTRUMENT).unwrap())
            .unwrap();
        link.push(PanelAction::Mode(Mode::PatternWrite)).unwrap();
        tap(&link, Button::PitchMode);
        render(&mut engine);
        let leds = link.display().unwrap().leds;
        assert_eq!(leds.modes[1], link::Led::On, "PITCH MODE lamp stays dark");
        link.push(PanelAction::Press(Button::Pitch(4))).unwrap();
        let mut gate = false;
        for _ in 0..20 {
            render(&mut engine);
            gate |= engine.last_command().gate;
        }
        assert!(gate, "key press never reached the voice");
        tap(&link, Button::TimeMode);
        render(&mut engine);
        assert_ne!(link.display().unwrap().leds.modes[2], link::Led::Off, "TIME MODE lamp stays dark");
    }
    #[test]
    fn write_four_steps_then_play_reaches_voice() {
        let link = Arc::new(PanelLink::new());
        let mut engine = Dw303Engine::with_link(link.clone());
        engine
            .prepare(ProcessConfig::new(48000.0, 64, 128, AudioLayout::STEREO_INSTRUMENT).unwrap())
            .unwrap();
        engine.apply(EventData::ParameterValue {
            id: params::SYNC_MODE,
            value: 0.0,
        });
        engine.apply(EventData::ParameterValue {
            id: params::QUALITY,
            value: 0.0,
        });
        link.push(PanelAction::Mode(Mode::PatternWrite)).unwrap();
        link.push(PanelAction::Press(Button::Pitch(0))).unwrap();
        tap(&link, Button::Clear);
        link.push(PanelAction::Release(Button::Pitch(0))).unwrap();
        link.push(PanelAction::Press(Button::Function)).unwrap();
        for _ in 0..4 {
            tap(&link, Button::Step);
        }
        link.push(PanelAction::Release(Button::Function)).unwrap();
        tap(&link, Button::PitchMode);
        for note in [0, 2, 4, 5] {
            tap(&link, Button::Pitch(note));
        }
        tap(&link, Button::TimeMode);
        for _ in 0..4 {
            tap(&link, Button::Down);
        }
        link.push(PanelAction::Mode(Mode::PatternPlay)).unwrap();
        tap(&link, Button::RunStop);
        let mut seen = [false; 4];
        let mut had_gate = false;
        for _ in 0..400 {
            render(&mut engine);
            let c = engine.last_command();
            had_gate |= c.gate;
            for (i, n) in [12, 14, 16, 17].into_iter().enumerate() {
                seen[i] |= c.gate && c.note == n;
            }
        }
        assert!(had_gate && seen.into_iter().all(|s| s));
        assert_eq!(engine.pattern().length, 4);
        assert_eq!(link.memory().bank.patterns[0], *engine.pattern());
        assert_eq!(engine.parameters().get(params::RUN), Some(1.0));
        engine.apply(EventData::ParameterValue {
            id: params::RUN,
            value: 0.0,
        });
        render(&mut engine);
        assert!(!engine.last_command().gate);
        assert!(!link.display().unwrap().running);
    }
    /// Tempo automation while the internal clock runs must not repeat or drop
    /// steps: every step boundary advances exactly once (user report: tempo
    /// changes "stutter").
    #[test]
    fn tempo_automation_keeps_steps_monotonic() {
        const TEMPO_EVERY: usize = 64;
        let rate = 48_000.0;
        let mut engine = Dw303Engine::new();
        engine
            .prepare(ProcessConfig::new(rate, 1, 8, AudioLayout::STEREO_INSTRUMENT).unwrap())
            .unwrap();
        engine.set_pattern(Pattern {
            time: [Time::Note; 16],
            ..Pattern::default()
        });
        let set = |id, value| Event {
            frame_offset: 0,
            data: EventData::ParameterValue { id, value },
        };
        let mut triggers = Vec::new();
        let total = (rate * 4.0) as usize;
        for n in 0..total {
            let t = n as f64 / rate;
            let mut events = vec![];
            if n == 0 {
                events.push(set(params::SYNC_MODE, 0.0));
                events.push(set(params::RUN, 1.0));
            }
            if n % TEMPO_EVERY == 0 {
                // Sweep 120 -> 180 -> 90 BPM, as a knob drag would.
                let bpm = if t < 2.0 {
                    120.0 + 30.0 * t
                } else {
                    180.0 - 45.0 * (t - 2.0)
                };
                events.push(set(params::TEMPO, bpm as f32));
            }
            let mut left = [0.0f32; 1];
            let mut right = [0.0f32; 1];
            let mut outputs = [&mut left[..], &mut right[..]];
            let audio = AudioBuffer::separate(&[], &mut outputs, 1).unwrap();
            let mut block = ProcessBlock::new(audio, &events, Transport::default());
            assert_eq!(engine.process(&mut block), ProcessStatus::Continue);
            if engine.last_command().trigger {
                triggers.push(n);
            }
        }
        // 16th notes between 90 and 180 BPM last 83..167 ms.
        assert!(triggers.len() > 20, "{} triggers", triggers.len());
        for pair in triggers.windows(2) {
            let ms = (pair[1] - pair[0]) as f64 * 1000.0 / rate;
            assert!(
                (75.0..=180.0).contains(&ms),
                "step interval {ms:.1} ms at sample {}",
                pair[1]
            );
        }
    }
}
