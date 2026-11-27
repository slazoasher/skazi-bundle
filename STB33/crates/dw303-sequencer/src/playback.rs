//! Gate, attack, accent and slide logic. Pitch lag belongs to the analog PitchCv stage.
use crate::{Memory, Octave, Pattern, StageError, StepClock, StepEvent, Time, Track};
use ravet_foundation::{EventData, MidiKind, MidiMessage};

/// Largest step phase at which an edge still starts a note. An edge is the
/// first sample of a new step; its phase is exactly zero only when the step
/// is a whole number of samples (constant tempo). Under tempo changes it is up
/// to one sample's worth (well under 2% of a step even at 1 kHz / 300 BPM), so
/// an exact-zero test dropped every attack (user-reported stutter). Starting
/// playback mid-step (e.g. a host seek to 40% of a step) still does not
/// trigger.
pub const EDGE_PHASE_TOLERANCE: f64 = 0.25;

/// Per-sample integration boundary for the circuit voice.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct VoiceCommand {
    /// Semitone index from lowest C; octave and transpose are already folded in.
    pub note: u8,
    /// Gate voltage logically high.
    pub gate: bool,
    /// One-sample new attack pulse; absent on slides and ties.
    pub trigger: bool,
    /// Accent latch of the sounding pitch entry.
    pub accent: bool,
    /// Enable the analog pitch lag on a connected destination note.
    /// VERIFY: exact original CPU slide-switch timing across step boundaries.
    pub slide: bool,
}
/// Sequencer timing calibration, separate from analog components.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Timing {
    /// VERIFY: nominal unslid gate release at half the step; measure original CPU output.
    pub gate_fraction: f64,
}
impl Timing {
    /// Best-estimate original timing; not yet hardware calibrated.
    pub const NOMINAL: Self = Self { gate_fraction: 0.5 };
}
/// Pattern/track playback and stopped-sequencer MIDI note fallback.
#[derive(Clone, Debug)]
pub struct Sequencer {
    /// Timing master; apply host context and timestamped MIDI clock here.
    pub clock: StepClock,
    /// Monophonic live keyboard. Held notes remain tracked during pattern playback.
    pub notes: MonoNoteInput,
    timing: Timing,
    transpose: i8,
    previous: VoiceCommand,
    track_position: Option<(i64, usize, i64)>,
    invalid: u64,
}
impl Default for Sequencer {
    fn default() -> Self {
        Self::new(Timing::NOMINAL)
    }
}
impl Sequencer {
    /// Construct stopped playback; invalid gate fractions fall back to nominal.
    pub fn new(timing: Timing) -> Self {
        Self {
            clock: StepClock::new(),
            notes: MonoNoteInput::default(),
            timing: if timing.gate_fraction.is_finite()
                && (0.0..=1.0).contains(&timing.gate_fraction)
            {
                timing
            } else {
                Timing::NOMINAL
            },
            transpose: 0,
            previous: VoiceCommand::default(),
            track_position: None,
            invalid: u64::from(
                !timing.gate_fraction.is_finite() || !(0.0..=1.0).contains(&timing.gate_fraction),
            ),
        }
    }
    /// Prepare timing at the circuit/engine sample rate.
    pub fn prepare(&mut self, rate: f64) -> Result<(), StageError> {
        self.clock.prepare(rate)?;
        self.reset();
        Ok(())
    }
    /// Stop and clear all held notes and playback history.
    pub fn reset(&mut self) {
        self.clock.reset();
        self.notes.reset();
        self.previous = VoiceCommand::default();
        self.track_position = None;
    }
    /// Cumulative rejected numeric inputs, including clock diagnostics.
    pub fn invalid_inputs(&self) -> u64 {
        self.invalid.saturating_add(self.clock.invalid_inputs())
    }
    /// Global semitone transpose; output saturates to 0..=127.
    pub fn set_transpose(&mut self, semitones: i8) {
        self.transpose = semitones;
    }
    /// Dispatch a sample-timed foundation event before rendering that sample.
    pub fn event(&mut self, data: EventData) {
        if let EventData::Midi(m) = data {
            self.clock.midi(m);
        }
        self.notes.event(data);
    }
    /// Render one pattern sample. Pattern fields are bounded defensively on the hot path.
    pub fn tick(&mut self, pattern: &Pattern) -> VoiceCommand {
        self.clock.set_triplet(pattern.triplet);
        let event = self.clock.tick();
        self.tick_at(pattern, event)
    }
    /// Render supplied clock state, useful for externally scheduled samples.
    pub fn tick_at(&mut self, pattern: &Pattern, mut event: StepEvent) -> VoiceCommand {
        if !event.phase.is_finite() {
            event.phase = 0.0;
            self.invalid = self.invalid.saturating_add(1);
        }
        event.phase = event.phase.clamp(0.0, 1.0);
        let live = self.notes.tick();
        let result = if event.running {
            command(
                pattern,
                event,
                self.transpose,
                self.timing,
                self.previous.note,
            )
        } else {
            live
        };
        self.previous = result;
        result
    }
    /// Render a repeating track, including mixed straight/triplet patterns.
    /// The clock stays in straight mode; pattern-local beat phases determine edges.
    /// Empty/invalid tracks are silent while running. Global and track transpose add.
    pub fn tick_track(&mut self, memory: &Memory, track: usize) -> VoiceCommand {
        self.tick_track_from(memory, track, 0)
    }
    /// Render a repeating track with measure zero of the clock anchored to a
    /// designated measure. Keep `start_measure` constant throughout a run.
    /// Invalid starting measures fall back to the first measure.
    pub fn tick_track_from(
        &mut self,
        memory: &Memory,
        track: usize,
        start_measure: usize,
    ) -> VoiceCommand {
        self.clock.set_triplet(false);
        let event = self.clock.tick();
        let live = self.notes.tick();
        if !event.running {
            self.previous = live;
            return live;
        }
        let Some(chain) = memory.tracks.get(track) else {
            return self.silence();
        };
        let length = usize::from(chain.length).min(64);
        if length == 0 {
            return self.silence();
        }
        let mut durations = [0.0; 64];
        let mut total = 0.0;
        for (i, entry) in chain.entries.iter().take(length).enumerate() {
            let Ok(index) = entry.pattern.index() else {
                return self.silence();
            };
            let p = &memory.bank.patterns[index];
            durations[i] = f64::from(p.length.clamp(1, 16)) / if p.triplet { 3.0 } else { 4.0 };
            total += durations[i];
        }
        let offset = if start_measure < length {
            durations.iter().take(start_measure).sum::<f64>()
        } else {
            0.0
        };
        let beat = (event.index as f64 + event.phase) / 4.0 + offset;
        let mut local = beat % total;
        if local < 0.0 {
            local += total;
        }
        let mut measure = length - 1;
        for (i, duration) in durations.iter().take(length).enumerate() {
            if local + 1.0e-9 < *duration {
                measure = i;
                break;
            }
            local -= duration;
        }
        let entry = chain.entries[measure];
        let Ok(index) = entry.pattern.index() else {
            return self.silence();
        };
        let pattern = &memory.bank.patterns[index];
        let step = local.max(0.0) * if pattern.triplet { 3.0 } else { 4.0 };
        let position = (step + 1.0e-9) as i64;
        let cycle = beat / total;
        let cycle = if cycle < cycle as i64 as f64 {
            (cycle as i64).saturating_sub(1)
        } else {
            cycle as i64
        };
        let key = (cycle, measure, position);
        let edge = self.track_position != Some(key) || event.resync;
        self.track_position = Some(key);
        let result = track_command(
            memory,
            chain,
            length,
            measure,
            position as usize,
            StepEvent {
                edge,
                index: position,
                phase: (step - position as f64).max(0.0),
                ..event
            },
            self.transpose,
            self.timing,
            self.previous.note,
        );
        self.previous = result;
        result
    }
    /// Render one local sample of a validated arrangement/temporary panel chain.
    /// Measure and event position are bounded defensively; this does not tick the clock.
    /// Preserves slide and tie history between the chain's pattern memories.
    pub fn tick_chain_at(
        &mut self,
        memory: &Memory,
        chain: &Track,
        measure: usize,
        event: StepEvent,
    ) -> VoiceCommand {
        let live = self.notes.tick();
        if !event.running {
            self.previous = live;
            return live;
        }
        let length = usize::from(chain.length).min(64);
        if length == 0 || measure >= length {
            return self.silence();
        }
        for entry in chain.entries.iter().take(length) {
            if entry.pattern.index().is_err() {
                return self.silence();
            }
        }
        let mut event = event;
        if !event.phase.is_finite() {
            event.phase = 0.0;
            self.invalid = self.invalid.saturating_add(1);
        }
        event.phase = event.phase.clamp(0.0, 1.0);
        let result = track_command(
            memory,
            chain,
            length,
            measure,
            event.index.max(0) as usize,
            event,
            self.transpose,
            self.timing,
            self.previous.note,
        );
        self.previous = result;
        result
    }
    /// Last rendered track measure and local rhythm position, zero based.
    /// `None` before playback or for a silent/empty track.
    pub fn track_cursor(&self) -> Option<(usize, usize)> {
        self.track_position
            .map(|(_, measure, step)| (measure, step.max(0) as usize))
    }
    fn silence(&mut self) -> VoiceCommand {
        self.track_position = None;
        self.previous = VoiceCommand {
            note: self.previous.note,
            ..VoiceCommand::default()
        };
        self.previous
    }
}
fn pitch_slot(pattern: &Pattern, position: usize) -> usize {
    pattern
        .time
        .iter()
        .take(position)
        .filter(|&&t| t == Time::Note)
        .count()
        .min(15)
}
fn command(
    pattern: &Pattern,
    e: StepEvent,
    transpose: i8,
    timing: Timing,
    retained_note: u8,
) -> VoiceCommand {
    let length = usize::from(pattern.length.clamp(1, 16));
    let position = e.index.rem_euclid(length as i64) as usize;
    let mut out = VoiceCommand {
        note: retained_note,
        ..VoiceCommand::default()
    };
    if pattern.time[position] == Time::Rest {
        return out;
    }
    let mut sounding = position;
    let mut found = pattern.time[position] == Time::Note;
    // Ties are sustained time entries, not duplicated pitch entries.
    for distance in 1..=16 {
        if found || distance > length {
            break;
        }
        let back = (position + length - distance) % length;
        match pattern.time[back] {
            Time::Rest => break,
            Time::Note => {
                sounding = back;
                found = true;
            }
            Time::Tie => {}
        }
    }
    if !found {
        return out;
    }
    let s = pattern.steps[pitch_slot(pattern, sounding)];
    let base = match s.octave {
        Octave::Down => 0,
        Octave::Mid => 12,
        Octave::Up => 24,
    };
    out.note = (i16::from(s.pitch.min(12)) + base + i16::from(transpose)).clamp(0, 127) as u8;
    out.accent = s.accent;
    let next = pattern.time[(position + 1) % length];
    out.gate = next == Time::Tie || s.slide || e.phase < timing.gate_fraction;
    let previous = (sounding + length - 1) % length;
    let mut incoming = false;
    if pattern.time[previous] != Time::Rest {
        for distance in 1..=16 {
            if distance > length {
                break;
            }
            let back = (sounding + length - distance) % length;
            match pattern.time[back] {
                Time::Rest => break,
                Time::Note => {
                    incoming = pattern.steps[pitch_slot(pattern, back)].slide;
                    break;
                }
                Time::Tie => {}
            }
        }
    }
    // VERIFY: outgoing slide is latched for the source duration, carried to the destination;
    // exact firmware switch timing at a boundary awaits hardware measurement.
    out.slide = incoming;
    out.trigger = e.edge
        && e.phase < EDGE_PHASE_TOLERANCE
        && pattern.time[position] == Time::Note
        && (!incoming || e.resync);
    out
}
// The validated chain is bounded to 64 measures, each with at most 16 time entries.
fn track_pattern<'a>(memory: &'a Memory, chain: &Track, measure: usize) -> &'a Pattern {
    let index = chain.entries[measure].pattern.index().unwrap_or(0);
    &memory.bank.patterns[index]
}
fn prior(
    memory: &Memory,
    chain: &Track,
    length: usize,
    measure: usize,
    position: usize,
) -> (usize, usize) {
    if position > 0 {
        (measure, position - 1)
    } else {
        let m = (measure + length - 1) % length;
        (
            m,
            usize::from(track_pattern(memory, chain, m).length.clamp(1, 16)) - 1,
        )
    }
}
fn preceding_note(
    memory: &Memory,
    chain: &Track,
    length: usize,
    mut cursor: (usize, usize),
) -> Option<(usize, usize)> {
    for _ in 0..1024 {
        cursor = prior(memory, chain, length, cursor.0, cursor.1);
        match track_pattern(memory, chain, cursor.0).time[cursor.1] {
            Time::Rest => return None,
            Time::Note => return Some(cursor),
            Time::Tie => {}
        }
    }
    None
}
#[allow(clippy::too_many_arguments)]
fn track_command(
    memory: &Memory,
    chain: &Track,
    length: usize,
    measure: usize,
    position: usize,
    e: StepEvent,
    transpose: i8,
    timing: Timing,
    retained: u8,
) -> VoiceCommand {
    let p = track_pattern(memory, chain, measure);
    let position = position.min(usize::from(p.length.clamp(1, 16)) - 1);
    let current = p.time[position];
    let silent = VoiceCommand {
        note: retained,
        ..VoiceCommand::default()
    };
    if current == Time::Rest {
        return silent;
    }
    let sounding = if current == Time::Note {
        Some((measure, position))
    } else {
        preceding_note(memory, chain, length, (measure, position))
    };
    let Some((m, pos)) = sounding else {
        return silent;
    };
    let source = track_pattern(memory, chain, m);
    let s = source.steps[pitch_slot(source, pos)];
    let incoming = preceding_note(memory, chain, length, (m, pos)).is_some_and(|(m, pos)| {
        let p = track_pattern(memory, chain, m);
        p.steps[pitch_slot(p, pos)].slide
    });
    let next = if position + 1 < usize::from(p.length.clamp(1, 16)) {
        p.time[position + 1]
    } else {
        track_pattern(memory, chain, (measure + 1) % length).time[0]
    };
    let base = match s.octave {
        Octave::Down => 0,
        Octave::Mid => 12,
        Octave::Up => 24,
    };
    VoiceCommand {
        note: (i16::from(s.pitch.min(12))
            + base
            + i16::from(transpose)
            + i16::from(chain.entries[m].transpose))
        .clamp(0, 127) as u8,
        gate: next == Time::Tie || s.slide || e.phase < timing.gate_fraction,
        trigger: e.edge
            && e.phase < EDGE_PHASE_TOLERANCE
            && current == Time::Note
            && (!incoming || e.resync),
        accent: s.accent,
        slide: incoming,
    }
}

/// Fixed-capacity last-note-priority keyboard (all 16 MIDI channels, 128 keys each).
/// Repeated note-on for the same channel/key replaces that held note.
#[derive(Clone, Debug)]
pub struct MonoNoteInput {
    held: [u16; 2048],
    velocities: [u8; 2048],
    len: usize,
    command: VoiceCommand,
    threshold: u8,
}
impl Default for MonoNoteInput {
    fn default() -> Self {
        Self {
            held: [0; 2048],
            velocities: [0; 2048],
            len: 0,
            command: VoiceCommand::default(),
            threshold: 100,
        }
    }
}
impl MonoNoteInput {
    /// Release all held keys and pending attacks, retaining the accent threshold.
    pub fn reset(&mut self) {
        self.len = 0;
        self.command = VoiceCommand::default();
    }
    /// Velocity at or above this MIDI-unit threshold accents the note (default 100).
    pub fn set_accent_threshold(&mut self, threshold: u8) {
        self.threshold = threshold.min(127);
    }
    /// Accept validated raw MIDI note messages, preserving channel/key identity.
    pub fn midi(&mut self, message: MidiMessage) {
        match message.kind() {
            MidiKind::NoteOn {
                channel,
                key,
                velocity,
            } => self.note_on(channel, key, velocity),
            MidiKind::NoteOff { channel, key, .. } => self.note_off(channel, key),
            MidiKind::ControlChange {
                channel,
                controller: 120 | 123,
                ..
            } => {
                for key in 0..128 {
                    self.note_off(channel, key);
                }
            }
            MidiKind::Reset => self.reset(),
            _ => {}
        }
    }
    /// Accept foundation note events (normalised velocities) and MIDI messages.
    pub fn event(&mut self, data: EventData) {
        match data {
            EventData::Midi(m) => self.midi(m),
            EventData::NoteOn {
                channel,
                key,
                velocity,
            } => {
                if velocity.is_finite() && velocity > 0.0 {
                    self.note_on(channel, key, (velocity.clamp(0.0, 1.0) * 127.0 + 0.5) as u8);
                } else {
                    self.note_off(channel, key);
                }
            }
            EventData::NoteOff { channel, key, .. } => self.note_off(channel, key),
            _ => {}
        }
    }
    fn remove(&mut self, identity: u16) -> bool {
        for i in 0..self.len {
            if self.held[i] == identity {
                self.held.copy_within(i + 1..self.len, i);
                self.velocities.copy_within(i + 1..self.len, i);
                self.len -= 1;
                return true;
            }
        }
        false
    }
    /// Press a channel/key; overlap slides and never retriggers an existing gate.
    pub fn note_on(&mut self, channel: u8, key: u8, velocity: u8) {
        if channel >= 16 || key >= 128 || velocity >= 128 {
            return;
        }
        if velocity == 0 {
            self.note_off(channel, key);
            return;
        }
        let identity = u16::from(channel) * 128 + u16::from(key);
        let overlap = self.len > 0;
        self.remove(identity);
        if self.len < self.held.len() {
            self.held[self.len] = identity;
            self.velocities[self.len] = velocity;
            self.len += 1;
        }
        self.command = VoiceCommand {
            note: key,
            gate: true,
            trigger: !overlap,
            accent: velocity >= self.threshold,
            slide: overlap,
        };
    }
    /// Release a channel/key; return to the most recent remaining key legato.
    pub fn note_off(&mut self, channel: u8, key: u8) {
        if channel >= 16 || key >= 128 {
            return;
        }
        let identity = u16::from(channel) * 128 + u16::from(key);
        let current = self.len > 0 && self.held[self.len - 1] == identity;
        if !self.remove(identity) || !current {
            return;
        }
        if self.len == 0 {
            self.command.gate = false;
            self.command.trigger = false;
            self.command.slide = false;
            self.command.accent = false;
        } else {
            self.command = VoiceCommand {
                note: (self.held[self.len - 1] % 128) as u8,
                gate: true,
                trigger: false,
                accent: self.velocities[self.len - 1] >= self.threshold,
                slide: true,
            };
        }
    }
    /// Return one sample of live voice state and consume its one-sample trigger.
    pub fn tick(&mut self) -> VoiceCommand {
        let command = self.command;
        self.command.trigger = false;
        command
    }
}
