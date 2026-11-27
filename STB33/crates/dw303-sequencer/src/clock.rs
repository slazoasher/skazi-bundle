//! Sample-position based clocks; block boundaries never reset a continuous timeline.
use crate::StageError;
use ravet_foundation::{MidiKind, MidiMessage, Transport};

/// Timing master.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum ClockSource {
    /// Standalone tempo.
    #[default]
    Internal,
    /// Block-start host quarter-note position.
    Host,
    /// MIDI real-time 24 PPQN stream.
    Midi,
}
/// Timing state for the sample about to be rendered.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct StepEvent {
    /// Master is running.
    pub running: bool,
    /// A new subdivision starts on this sample (also true on resynchronisation).
    pub edge: bool,
    /// Timeline was started, sought or looped; discard previous gate history.
    pub resync: bool,
    /// Absolute subdivision index, including negative host pre-roll.
    pub index: i64,
    /// Position within this subdivision in 0..1.
    pub phase: f64,
}
/// Allocation-free clock. Call MIDI handlers before `tick` at their sample offset.
#[derive(Clone, Debug)]
pub struct StepClock {
    rate: f64,
    tempo: f64,
    source: ClockSource,
    running: bool,
    triplet: bool,
    sample: u64,
    anchor_sample: u64,
    anchor_beat: f64,
    last: Option<i64>,
    resync: bool,
    midi_pulse: u64,
    midi_started: bool,
    pulse_sample: u64,
    pulse_interval: f64,
    midi_edge: bool,
    invalid: u64,
}
impl Default for StepClock {
    fn default() -> Self {
        Self::new()
    }
}
impl StepClock {
    /// Construct a stopped 120 BPM clock at 48 kHz.
    pub fn new() -> Self {
        Self {
            rate: 48_000.0,
            tempo: 120.0,
            source: ClockSource::Internal,
            running: false,
            triplet: false,
            sample: 0,
            anchor_sample: 0,
            anchor_beat: 0.0,
            last: None,
            resync: false,
            midi_pulse: 0,
            midi_started: false,
            pulse_sample: 0,
            pulse_interval: 1000.0,
            midi_edge: false,
            invalid: 0,
        }
    }
    /// Configure sample rate and reset timing history.
    pub fn prepare(&mut self, sample_rate: f64) -> Result<(), StageError> {
        if !sample_rate.is_finite() || !(1_000.0..=1_536_000.0).contains(&sample_rate) {
            return Err(StageError::InvalidSampleRate);
        }
        self.rate = sample_rate;
        self.reset();
        Ok(())
    }
    /// Stop, rewind and clear pulse/timeline history; retain configuration.
    pub fn reset(&mut self) {
        self.running = false;
        self.sample = 0;
        self.anchor_sample = 0;
        self.anchor_beat = 0.0;
        self.last = None;
        self.resync = false;
        self.midi_pulse = 0;
        self.midi_started = false;
        self.pulse_sample = 0;
        self.pulse_interval = self.rate * 60.0 / (self.tempo * 24.0);
        self.midi_edge = false;
    }
    /// Select a master, stopping and rewinding it when the selection changes.
    pub fn set_source(&mut self, source: ClockSource) {
        if self.source != source {
            self.source = source;
            self.reset();
        }
    }
    /// Current timing master.
    pub fn source(&self) -> ClockSource {
        self.source
    }
    /// Running state, including MIDI start waiting for its first clock pulse.
    pub fn is_running(&self) -> bool {
        self.running
    }
    /// Number of rejected non-finite/invalid numeric clock inputs.
    pub fn invalid_inputs(&self) -> u64 {
        self.invalid
    }
    fn beat(&self) -> f64 {
        self.anchor_beat
            + self.sample.wrapping_sub(self.anchor_sample) as f64 * self.tempo / (60.0 * self.rate)
    }
    /// Set finite positive BPM (up to 1000), retaining fractional step position.
    pub fn set_tempo(&mut self, bpm: f64) {
        if !bpm.is_finite() || !(0.0..=1000.0).contains(&bpm) || bpm == 0.0 {
            self.invalid = self.invalid.saturating_add(1);
            return;
        }
        self.anchor_beat = self.beat();
        self.anchor_sample = self.sample;
        self.tempo = bpm;
    }
    /// Set three rather than four subdivisions per quarter; reset edge history.
    pub fn set_triplet(&mut self, triplet: bool) {
        if self.triplet != triplet {
            self.triplet = triplet;
            self.last = None;
            self.resync = true;
        }
    }
    /// Start from beat zero. In MIDI mode, the first following pulse starts step zero.
    pub fn start(&mut self) {
        self.reset();
        self.running = true;
        self.resync = true;
    }
    /// Stop without advancing the retained position.
    pub fn stop(&mut self) {
        if self.running {
            self.anchor_beat = self.beat();
            self.anchor_sample = self.sample;
        }
        self.running = false;
        self.midi_edge = false;
    }
    /// Resume the retained position; sounding history is reconstructed.
    pub fn resume(&mut self) {
        self.anchor_sample = self.sample;
        self.running = true;
        self.resync = true;
        self.last = None;
    }
    /// Apply block-start host context before rendering its first sample.
    /// Missing fields retain existing values. Explicit discontinuities clear gate history.
    /// Tempo ramps and within-block host loops must be split by the caller.
    pub fn begin_host_block(&mut self, transport: Transport) {
        if self.source != ClockSource::Host {
            return;
        }
        let was_running = self.running;
        let running = transport.playing.unwrap_or(was_running);
        let mut jump = transport.discontinuity || running != was_running;
        let position = match transport.beat_position {
            Some(b) if b.is_finite() && b.abs() < 1.0e12 => Some(b),
            Some(_) => {
                self.invalid = self.invalid.saturating_add(1);
                None
            }
            None => None,
        };
        if let Some(b) = position {
            jump |= (b - self.beat()).abs() > 1.0e-7;
        }
        let mut tempo_change = false;
        if let Some(bpm) = transport.tempo {
            if bpm.is_finite() && bpm > 0.0 && bpm <= 1000.0 {
                tempo_change = bpm != self.tempo;
                if tempo_change {
                    self.set_tempo(bpm);
                }
            } else {
                self.invalid = self.invalid.saturating_add(1);
            }
        }
        if jump || tempo_change {
            self.anchor_beat = position.unwrap_or(self.anchor_beat);
            self.anchor_sample = self.sample;
        }
        self.running = running;
        if jump {
            self.last = None;
            self.resync = running;
        }
    }
    /// Accept MIDI real-time messages; non-clock messages are ignored.
    pub fn midi(&mut self, message: MidiMessage) {
        if self.source != ClockSource::Midi {
            return;
        }
        match message.kind() {
            MidiKind::Start => self.start(),
            MidiKind::Stop => self.stop(),
            MidiKind::Continue => self.resume(),
            MidiKind::TimingClock if self.running => {
                if self.midi_started {
                    let interval = self.sample.wrapping_sub(self.pulse_sample);
                    if interval > 0 {
                        self.pulse_interval = interval as f64;
                    }
                    self.midi_pulse = self.midi_pulse.wrapping_add(1);
                } else {
                    self.midi_started = true;
                }
                self.pulse_sample = self.sample;
                let pulses = if self.triplet { 8 } else { 6 };
                self.midi_edge = self.midi_pulse.is_multiple_of(pulses);
            }
            _ => {}
        }
    }
    /// Produce timing for one sample and advance exactly one sample while running.
    /// MIDI edges occur only on received pulses, never on extrapolated time.
    pub fn tick(&mut self) -> StepEvent {
        let mut out = StepEvent::default();
        if self.running && (self.source != ClockSource::Midi || self.midi_started) {
            let (index, phase) = if self.source == ClockSource::Midi {
                let pulses = if self.triplet { 8 } else { 6 };
                let fraction = (self.sample.wrapping_sub(self.pulse_sample) as f64
                    / self.pulse_interval)
                    .min(1.0);
                (
                    (self.midi_pulse / pulses) as i64,
                    ((self.midi_pulse % pulses) as f64 + fraction) / pulses as f64,
                )
            } else {
                let position = self.beat() * if self.triplet { 3.0 } else { 4.0 };
                // Snap roundoff at mathematical sample-aligned boundaries only.
                let integral = floor(position + 1.0e-9);
                (integral, (position - integral as f64).max(0.0))
            };
            out = StepEvent {
                running: true,
                edge: if self.source == ClockSource::Midi {
                    self.midi_edge || self.last.is_none()
                } else {
                    self.last != Some(index)
                },
                resync: self.resync,
                index,
                phase,
            };
            self.last = Some(index);
            self.resync = false;
            self.midi_edge = false;
        }
        if self.running {
            self.sample = self.sample.wrapping_add(1);
        }
        out
    }
}
fn floor(x: f64) -> i64 {
    let i = x as i64;
    if x < i as f64 { i.saturating_sub(1) } else { i }
}
