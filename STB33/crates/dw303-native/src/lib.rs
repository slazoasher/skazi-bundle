//! DW-303 engine behind a small C ABI for the VST3, AU, AAX and LV2 adapters
//! in `native/`. Values cross the boundary in physical units. State uses the
//! CLAP record, so a pattern memory saved in one format loads in the others.
use dw303_engine::{
    Dw303Engine,
    link::{Memory, PanelLink},
    params,
};
use ravet_foundation::{
    AudioBuffer, AudioLayout, Event, EventData, MidiMessage, ParameterId, ParameterState,
    ProcessBlock, ProcessConfig, ProcessStatus, Processor, Transport,
};
use std::fmt::Write;
use std::sync::{
    Arc, Mutex,
    atomic::{
        AtomicU32, AtomicU64,
        Ordering::{AcqRel, Acquire, Relaxed, Release},
    },
};

#[cfg(any(target_os = "macos", target_os = "windows", target_os = "linux"))]
mod editor;
mod ffi;

const STATE_PRODUCT: [u8; 8] = *b"ASHSTB33";
const STATE_VERSION: u32 = 2;
const PARAM_BYTES: usize = 16 + params::COUNT * 8;
/// Main-thread edits waiting for the next audio block.
const QUEUE: usize = 256;
/// Host MIDI and parameter events accepted per block; later ones are dropped.
pub const HOST_EVENTS: usize = 1024;
const EVENTS: usize = params::COUNT + QUEUE + 2 * HOST_EVENTS;

/// One short MIDI message at a frame offset within the block.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Dw303Midi {
    pub frame: u32,
    pub size: u32,
    pub bytes: [u8; 4],
}

/// One host parameter value (physical units) at a frame offset.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Dw303Param {
    pub frame: u32,
    pub index: u32,
    pub value: f32,
}

pub const TRANSPORT_PLAYING_VALID: u32 = 1;
pub const TRANSPORT_PLAYING: u32 = 2;
pub const TRANSPORT_TEMPO: u32 = 4;
pub const TRANSPORT_BEATS: u32 = 8;
pub const TRANSPORT_SAMPLE: u32 = 16;

/// Block-start host transport; `flags` marks which fields are known.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Dw303Transport {
    pub flags: u32,
    pub tempo: f64,
    pub beats: f64,
    pub sample: i64,
}

impl Dw303Transport {
    fn to_ravet(self) -> Transport {
        let has = |flag| self.flags & flag != 0;
        Transport {
            playing: has(TRANSPORT_PLAYING_VALID).then_some(has(TRANSPORT_PLAYING)),
            sample_position: has(TRANSPORT_SAMPLE).then_some(self.sample),
            beat_position: (has(TRANSPORT_BEATS) && self.beats.is_finite()).then_some(self.beats),
            tempo: (has(TRANSPORT_TEMPO) && self.tempo.is_finite() && self.tempo > 0.0)
                .then_some(self.tempo),
            discontinuity: false,
            details: None,
        }
    }
}

/// Values and edits shared by the audio thread, the format shell and the
/// editor. Every value word carries the restore generation that wrote it, so a
/// block that started before a restore cannot overwrite the restored values.
pub(crate) struct Shared {
    pub link: Arc<PanelLink>,
    values: [AtomicU64; params::COUNT],
    generation: AtomicU32,
    slots: [AtomicU64; QUEUE],
    head: AtomicU32,
    tail: AtomicU32,
    producer: Mutex<()>,
}

fn word(generation: u32, value: f32) -> u64 {
    (u64::from(generation) << 32) | u64::from(value.to_bits())
}

impl Shared {
    fn new() -> Self {
        Self {
            link: Arc::new(PanelLink::new()),
            values: params::DESCRIPTORS.map(|d| AtomicU64::new(word(0, d.default))),
            generation: AtomicU32::new(0),
            slots: std::array::from_fn(|_| AtomicU64::new(0)),
            head: AtomicU32::new(0),
            tail: AtomicU32::new(0),
            producer: Mutex::new(()),
        }
    }
    pub fn value(&self, index: usize) -> f32 {
        f32::from_bits(self.values[index].load(Acquire) as u32)
    }
    pub fn values(&self) -> [f32; params::COUNT] {
        std::array::from_fn(|i| self.value(i))
    }
    /// Publish a canonical edit for the next block. Full rejects the edit.
    pub fn set(&self, index: usize, value: f32) -> bool {
        let Some(value) = params::DESCRIPTORS
            .get(index)
            .and_then(|d| d.range.canonicalize(value))
        else {
            return false;
        };
        let _producer = self.producer.lock().unwrap_or_else(|e| e.into_inner());
        let h = self.head.load(Relaxed);
        if h.wrapping_sub(self.tail.load(Acquire)) >= QUEUE as u32 {
            return false;
        }
        let id = params::DESCRIPTORS[index].id.0;
        self.slots[h as usize % QUEUE].store((u64::from(id) << 32) | u64::from(value.to_bits()), Relaxed);
        self.head.store(h.wrapping_add(1), Release);
        self.values[index].store(word(self.generation.load(Acquire), value), Release);
        true
    }
    fn pop(&self) -> Option<Event> {
        let t = self.tail.load(Relaxed);
        if t == self.head.load(Acquire) {
            return None;
        }
        let word = self.slots[t as usize % QUEUE].load(Relaxed);
        self.tail.store(t.wrapping_add(1), Release);
        Some(Event {
            frame_offset: 0,
            data: EventData::ParameterValue {
                id: ParameterId((word >> 32) as u32),
                value: f32::from_bits(word as u32),
            },
        })
    }
    /// Store a complete value set under a new generation, then admit it.
    fn restore(&self, values: [f32; params::COUNT]) {
        let generation = self.generation.load(Acquire).wrapping_add(1);
        for (slot, value) in self.values.iter().zip(values) {
            slot.store(word(generation, value), Release);
        }
        self.generation.store(generation, Release);
    }
    /// Audio-thread reflection; a concurrent restore always wins.
    fn reflect(&self, index: usize, value: f32, generation: u32) {
        let old = self.values[index].load(Acquire);
        let new = word(generation, value);
        if (old >> 32) as u32 == generation && old != new {
            let _ = self.values[index].compare_exchange(old, new, AcqRel, Acquire);
        }
    }
}

/// One plug-in instance. The adapter calls prepare/process/reset from the
/// audio context only, and everything else from its control threads.
pub struct Dw303 {
    shared: Arc<Shared>,
    engine: Dw303Engine,
    events: Vec<Event>,
    config: Option<ProcessConfig>,
    generation: u32,
}

impl Default for Dw303 {
    fn default() -> Self {
        Self::new()
    }
}

impl Dw303 {
    pub fn new() -> Self {
        let shared = Arc::new(Shared::new());
        Self {
            engine: Dw303Engine::with_link(shared.link.clone()),
            shared,
            events: Vec::with_capacity(EVENTS),
            config: None,
            generation: u32::MAX,
        }
    }
    pub fn prepare(&mut self, rate: f64, maximum_frames: usize) -> bool {
        self.config = None;
        let Ok(config) =
            ProcessConfig::new(rate, maximum_frames, EVENTS, AudioLayout::STEREO_INSTRUMENT)
        else {
            return false;
        };
        if self.engine.prepare(config).is_err() {
            return false;
        }
        // The first block re-sends every stored value to the prepared engine.
        self.generation = self.shared.generation.load(Acquire).wrapping_sub(1);
        self.config = Some(config);
        true
    }
    pub fn reset(&mut self) {
        self.engine.reset();
    }
    pub fn deactivate(&mut self) {
        self.engine.deactivate();
        self.config = None;
    }
    /// Render one block; longer host blocks are split at the prepared size.
    /// `midi` and `values` must each be ordered by frame.
    pub fn process(
        &mut self,
        left: &mut [f32],
        right: &mut [f32],
        midi: &[Dw303Midi],
        values: &[Dw303Param],
        transport: Dw303Transport,
    ) -> bool {
        let frames = left.len().min(right.len());
        let Some(config) = self.config else {
            left.fill(0.0);
            right.fill(0.0);
            return false;
        };
        let _denormals =
            ravet_native_dsp::ScopedDenormalMode::enter(ravet_native_dsp::DenormalMode::FlushBoth)
                .ok();
        if frames == 0 {
            return true;
        }
        let mut transport = transport.to_ravet();
        let mut ok = true;
        let mut start = 0;
        while start < frames {
            let end = (start + config.maximum_frames()).min(frames);
            // Offsets past the block end are clamped to its last frame.
            let in_chunk = |frame: u32| (start..end).contains(&(frame as usize).min(frames - 1));
            ok &= self.chunk(
                &mut left[start..end],
                &mut right[start..end],
                start,
                midi.iter().filter(|m| in_chunk(m.frame)).take(HOST_EVENTS),
                values.iter().filter(|p| in_chunk(p.frame)).take(HOST_EVENTS),
                transport,
            );
            let count = end - start;
            if let Some(sample) = transport.sample_position.as_mut() {
                *sample += count as i64;
            }
            if let (Some(beats), Some(tempo)) = (transport.beat_position.as_mut(), transport.tempo)
            {
                *beats += count as f64 * tempo / 60.0 / config.sample_rate();
            }
            start = end;
        }
        ok
    }
    fn chunk<'a>(
        &mut self,
        left: &mut [f32],
        right: &mut [f32],
        start: usize,
        midi: impl Iterator<Item = &'a Dw303Midi>,
        values: impl Iterator<Item = &'a Dw303Param>,
        transport: Transport,
    ) -> bool {
        let shared = &*self.shared;
        let events = &mut self.events;
        events.clear();
        let generation = shared.generation.load(Acquire);
        if generation != self.generation {
            for (i, d) in params::DESCRIPTORS.iter().enumerate() {
                events.push(parameter(0, d.id, shared.value(i)));
            }
            self.generation = generation;
        }
        while let Some(event) = shared.pop() {
            events.push(event);
        }
        let queued = events.len();
        let last = left.len() - 1;
        let offset = |frame: u32| (frame as usize).min(start + last) - start;
        for m in midi {
            let size = (m.size as usize).min(m.bytes.len());
            if let Ok(message) = MidiMessage::from_bytes(&m.bytes[..size]) {
                events.push(Event {
                    frame_offset: offset(m.frame),
                    data: EventData::Midi(message),
                });
            }
        }
        for p in values {
            if let Some(d) = params::DESCRIPTORS.get(p.index as usize)
                && let Some(value) = d.range.canonicalize(p.value)
            {
                events.push(parameter(offset(p.frame), d.id, value));
            }
        }
        // Merge the two frame-ordered host lists without allocating; order
        // within a frame is preserved (values before notes, note-off before on).
        let host = &mut events[queued..];
        for i in 1..host.len() {
            let mut j = i;
            while j > 0 && host[j - 1].frame_offset > host[j].frame_offset {
                host.swap(j - 1, j);
                j -= 1;
            }
        }
        let frames = left.len();
        let mut outputs = [left, right];
        let Ok(audio) = AudioBuffer::separate(&[], &mut outputs, frames) else {
            return false;
        };
        let status = self
            .engine
            .process(&mut ProcessBlock::new(audio, &self.events, transport));
        let ok = status == ProcessStatus::Continue;
        if !ok {
            for output in outputs {
                output.fill(0.0);
            }
        }
        for (i, value) in self.engine.parameters().values().iter().enumerate() {
            shared.reflect(i, *value, self.generation);
        }
        ok
    }
    pub fn value(&self, index: usize) -> Option<f32> {
        (index < params::COUNT).then(|| self.shared.value(index))
    }
    pub fn set_value(&self, index: usize, value: f32) -> bool {
        self.shared.set(index, value)
    }
    pub fn save(&self) -> Option<Vec<u8>> {
        encode_state(self.shared.values(), &self.shared.link.memory())
    }
    pub fn load(&self, bytes: &[u8]) -> bool {
        let Some((values, memory)) = decode_state(bytes) else {
            return false;
        };
        self.shared.link.load_memory(memory);
        self.shared.restore(values);
        true
    }
    pub(crate) fn shared(&self) -> Arc<Shared> {
        self.shared.clone()
    }
}

fn parameter(frame_offset: usize, id: ParameterId, value: f32) -> Event {
    Event {
        frame_offset,
        data: EventData::ParameterValue { id, value },
    }
}

fn encode_state(values: [f32; params::COUNT], memory: &Memory) -> Option<Vec<u8>> {
    let state = params::schema().ok()?.state(values).ok()?;
    let mut bytes = vec![0; PARAM_BYTES + 4 + Memory::ENCODED_SIZE];
    state
        .encode(STATE_PRODUCT, STATE_VERSION, &mut bytes[..PARAM_BYTES])
        .ok()?;
    bytes[PARAM_BYTES..PARAM_BYTES + 4]
        .copy_from_slice(&(Memory::ENCODED_SIZE as u32).to_le_bytes());
    memory.encode(&mut bytes[PARAM_BYTES + 4..]).ok()?;
    Some(bytes)
}

fn decode_state(bytes: &[u8]) -> Option<([f32; params::COUNT], Memory)> {
    let schema = params::schema().ok()?;
    if bytes.len() == PARAM_BYTES {
        let state = ParameterState::decode(schema, STATE_PRODUCT, 1, bytes).ok()?;
        return Some((*state.values(), dw303_engine::factory_memory()));
    }
    if bytes.len() != PARAM_BYTES + 4 + Memory::ENCODED_SIZE {
        return None;
    }
    let state =
        ParameterState::decode(schema, STATE_PRODUCT, STATE_VERSION, &bytes[..PARAM_BYTES]).ok()?;
    let length = u32::from_le_bytes(bytes[PARAM_BYTES..PARAM_BYTES + 4].try_into().ok()?);
    if length as usize != Memory::ENCODED_SIZE {
        return None;
    }
    Some((*state.values(), Memory::decode(&bytes[PARAM_BYTES + 4..]).ok()?))
}

/// Host display text, matching the CLAP plug-in.
pub fn value_text(index: usize, value: f32) -> Option<String> {
    let d = params::DESCRIPTORS.get(index)?;
    if !value.is_finite() {
        return None;
    }
    let mut text = String::new();
    match d.id {
        params::TEMPO => write!(text, "{value:.1} BPM").ok()?,
        params::RUN => text.push_str(if value < 0.5 { "Stopped" } else { "Running" }),
        params::WAVEFORM => text.push_str(if value < 0.5 { "Saw" } else { "Square" }),
        params::SYNC_MODE => text.push_str(match value.round() as i32 {
            0 => "Internal",
            1 => "Host",
            _ => "MIDI clock",
        }),
        params::QUALITY => text.push_str(if value < 0.5 { "Economy" } else { "Full" }),
        // Knobs display 0–10 like the panel legend.
        _ => write!(text, "{:.1}", value * 10.0).ok()?,
    }
    Some(text)
}

pub fn text_value(index: usize, text: &str) -> Option<f32> {
    let d = params::DESCRIPTORS.get(index)?;
    let text = text.trim();
    let choice = match (d.id, text) {
        (params::RUN, "Stopped")
        | (params::WAVEFORM, "Saw")
        | (params::SYNC_MODE, "Internal")
        | (params::QUALITY, "Economy") => Some(0.0),
        (params::RUN, "Running")
        | (params::WAVEFORM, "Square")
        | (params::SYNC_MODE, "Host")
        | (params::QUALITY, "Full") => Some(1.0),
        (params::SYNC_MODE, "MIDI clock") => Some(2.0),
        _ => None,
    };
    if let Some(choice) = choice {
        return Some(choice);
    }
    let number: f32 = text.trim_end_matches("BPM").trim().parse().ok()?;
    let value = if d.unit.is_empty() && d.range.intervals().is_none() {
        number / 10.0
    } else {
        number
    };
    d.range.canonicalize(value)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn note(frame: u32, status: u8, key: u8, velocity: u8) -> Dw303Midi {
        Dw303Midi {
            frame,
            size: 3,
            bytes: [status, key, velocity, 0],
        }
    }

    #[test]
    fn notes_render_in_split_blocks_and_values_reflect() {
        let mut dsp = Dw303::new();
        assert!(dsp.prepare(48000.0, 64));
        let (mut left, mut right) = (vec![0.0; 200], vec![0.0; 200]);
        let midi = [note(0, 0x90, 48, 120), note(150, 0x80, 48, 0)];
        let cutoff = [Dw303Param {
            frame: 70,
            index: 1,
            value: 0.9,
        }];
        assert!(dsp.process(&mut left, &mut right, &midi, &cutoff, Dw303Transport::default()));
        assert!(left.iter().any(|s| s.abs() > 1e-6));
        assert_eq!(left, right);
        assert_eq!(dsp.value(1), Some(0.9));
    }

    #[test]
    fn queued_edits_and_restores_reach_the_engine() {
        let mut dsp = Dw303::new();
        assert!(dsp.prepare(44100.0, 128));
        let (mut left, mut right) = (vec![0.0; 128], vec![0.0; 128]);
        assert!(dsp.set_value(7, 150.0));
        assert!(!dsp.set_value(7, f32::NAN));
        assert!(!dsp.set_value(99, 0.0));
        dsp.process(&mut left, &mut right, &[], &[], Dw303Transport::default());
        assert_eq!(dsp.engine.parameters().values()[7], 150.0);

        let other = Dw303::new();
        other.set_value(1, 0.25);
        let mut memory = dw303_engine::factory_memory();
        memory.bank.patterns[3].steps[1].pitch = 7;
        other.shared.link.load_memory(memory.clone());
        let bytes = other.save().unwrap();
        assert!(dsp.load(&bytes));
        assert!(!dsp.load(&bytes[1..]));
        dsp.process(&mut left, &mut right, &[], &[], Dw303Transport::default());
        assert_eq!(dsp.engine.parameters().values()[1], 0.25);
        assert_eq!(dsp.engine.parameters().values()[7], 120.0);
        assert_eq!(dsp.shared.link.memory(), memory);
    }

    #[test]
    fn queue_is_bounded() {
        let dsp = Dw303::new();
        for n in 0..QUEUE {
            assert!(dsp.set_value(7, 40.0 + n as f32));
        }
        assert!(!dsp.set_value(7, 41.0));
    }

    #[test]
    fn text_round_trips() {
        for (i, d) in params::DESCRIPTORS.iter().enumerate() {
            for value in [d.range.minimum(), d.default, d.range.maximum()] {
                let text = value_text(i, value).unwrap();
                assert_eq!(text_value(i, &text), Some(value), "{} {text}", d.name);
            }
        }
    }
}
