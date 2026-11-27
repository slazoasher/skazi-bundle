//! Fixed atomic parameter queue and prepared audio/MIDI adapter.
use dw303_engine::{Dw303Engine, link::PanelLink, params};
use ravet_foundation::{
    AudioBuffer, AudioLayout, Event, EventData, MidiMessage, ParameterId, PrepareError,
    ProcessBlock, ProcessConfig, ProcessError, ProcessStatus, Processor, Transport,
};
use std::sync::{
    Arc,
    atomic::{
        AtomicU32, AtomicU64,
        Ordering::{Acquire, Relaxed, Release},
    },
};
pub(crate) const QUEUE: usize = 256;
pub(crate) const FRAMES: usize = 512;
pub(crate) struct Shared {
    pub link: Arc<PanelLink>,
    slots: [AtomicU64; QUEUE],
    head: AtomicU32,
    tail: AtomicU32,
    values: [AtomicU32; params::COUNT],
}
impl Shared {
    pub fn new(values: [f32; params::COUNT], link: Arc<PanelLink>) -> Arc<Self> {
        Arc::new(Self {
            link,
            slots: std::array::from_fn(|_| AtomicU64::new(0)),
            head: AtomicU32::new(0),
            tail: AtomicU32::new(0),
            values: values.map(|v| AtomicU32::new(v.to_bits())),
        })
    }
    pub fn values(&self) -> [f32; params::COUNT] {
        self.values
            .each_ref()
            .map(|v| f32::from_bits(v.load(Acquire)))
    }
    /// Main thread only; canonicalize before publishing. Full rejects the whole edit.
    pub fn push(&self, id: ParameterId, value: f32) -> crate::Result<()> {
        let index = params::DESCRIPTORS
            .iter()
            .position(|d| d.id == id)
            .ok_or("Unknown parameter")?;
        let value = params::DESCRIPTORS[index]
            .range
            .canonicalize(value)
            .ok_or("Invalid value")?;
        let h = self.head.load(Relaxed);
        if h.wrapping_sub(self.tail.load(Acquire)) >= QUEUE as u32 {
            return Err("Parameter queue full; retry the edit".into());
        }
        self.slots[h as usize % QUEUE].store(
            (u64::from(id.0) << 32) | u64::from(value.to_bits()),
            Relaxed,
        );
        self.head.store(h.wrapping_add(1), Release);
        Ok(())
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
    pub fn pending(&self) -> bool {
        self.head.load(Acquire) != self.tail.load(Acquire)
    }
}
pub(crate) struct Audio {
    engine: Dw303Engine,
    shared: Arc<Shared>,
    midi: Vec<ravet_midi_device::MidiReceiver>,
    drops: Vec<u64>,
    cursor: usize,
    mask: u64,
    scratch: [Vec<f32>; 2],
    config: Option<ProcessConfig>,
}
impl Audio {
    pub fn new(shared: Arc<Shared>, midi: Vec<ravet_midi_device::MidiReceiver>, mask: u64) -> Self {
        let drops = vec![0; midi.len()];
        Self {
            engine: Dw303Engine::with_link(shared.link.clone()),
            shared,
            midi,
            drops,
            cursor: 0,
            mask,
            scratch: [Vec::new(), Vec::new()],
            config: None,
        }
    }
    fn inner(&mut self, frames: usize, events: &[Event]) -> ProcessStatus {
        let [l, r] = &mut self.scratch;
        let mut views = [&mut l[..frames], &mut r[..frames]];
        let Ok(audio) = AudioBuffer::separate(&[], &mut views, frames) else {
            return ProcessStatus::Fault(ProcessError::NotPrepared);
        };
        self.engine
            .process(&mut ProcessBlock::new(audio, events, Transport::default()))
    }
}
impl Processor for Audio {
    fn prepare(&mut self, c: ProcessConfig) -> std::result::Result<(), PrepareError> {
        self.config = None;
        if c.maximum_frames() > FRAMES || c.layout().input_channels() != 0 || self.midi.len() > 16 {
            return Err(PrepareError::UnsupportedLayout);
        }
        let channels = c.layout().output_channels();
        if !(1..=64).contains(&channels) {
            return Err(PrepareError::UnsupportedLayout);
        }
        let valid = u64::MAX >> (64 - channels);
        if self.mask == 0 || self.mask & !valid != 0 {
            return Err(PrepareError::UnsupportedLayout);
        }
        self.engine.prepare(ProcessConfig::new(
            c.sample_rate(),
            c.maximum_frames(),
            QUEUE + 64,
            AudioLayout::STEREO_INSTRUMENT,
        )?)?;
        self.scratch = [vec![0.; c.maximum_frames()], vec![0.; c.maximum_frames()]];
        let values = self.shared.values();
        let events = std::array::from_fn::<_, { params::COUNT }, _>(|i| Event {
            frame_offset: 0,
            data: EventData::ParameterValue {
                id: params::DESCRIPTORS[i].id,
                value: values[i],
            },
        });
        self.inner(0, &events);
        self.config = Some(c);
        Ok(())
    }
    fn process(&mut self, b: &mut ProcessBlock<'_, '_, '_>) -> ProcessStatus {
        let _denormals =
            ravet_native_dsp::ScopedDenormalMode::enter(ravet_native_dsp::DenormalMode::FlushBoth)
                .ok();
        if let Err(e) = self
            .config
            .ok_or(ProcessError::NotPrepared)
            .and_then(|c| c.validate_block(b))
        {
            b.clear_outputs();
            return ProcessStatus::Fault(e);
        }
        let empty = Event {
            frame_offset: 0,
            data: EventData::ParameterValue {
                id: params::RUN,
                value: 0.,
            },
        };
        let mut events = [empty; QUEUE + 64];
        let mut n = 0;
        for _ in 0..QUEUE {
            let Some(e) = self.shared.pop() else { break };
            events[n] = e;
            n += 1;
        }
        for i in 0..self.midi.len() {
            let drops = self.midi[i].statistics().dropped_full;
            if drops != self.drops[i] {
                self.drops[i] = drops;
                self.engine.reset();
                for _ in 0..256 {
                    if self.midi[i].pop().is_none() {
                        break;
                    }
                }
            }
        }
        let mut empty_sources = 0;
        for _ in 0..64 {
            if self.midi.is_empty() {
                break;
            }
            let i = self.cursor % self.midi.len();
            self.cursor = (i + 1) % self.midi.len();
            if let Some(packet) = self.midi[i].pop() {
                empty_sources = 0;
                if let Ok(m) = MidiMessage::from_bytes(packet.as_bytes()) {
                    events[n] = Event {
                        frame_offset: 0,
                        data: EventData::Midi(m),
                    };
                    n += 1
                }
            } else {
                empty_sources += 1;
                if empty_sources >= self.midi.len() {
                    break;
                }
            }
        }
        let frames = b.audio.frames();
        let status = self.inner(frames, &events[..n]);
        b.clear_outputs();
        if status == ProcessStatus::Continue {
            for ch in 0..b.audio.output_channels() {
                if self.mask & (1u64 << ch) != 0
                    && let Some(out) = b.audio.output_mut(ch)
                {
                    out.copy_from_slice(&self.scratch[ch % 2][..frames]);
                }
            }
        }
        for (i, v) in self.engine.parameters().values().iter().enumerate() {
            self.shared.values[i].store(v.to_bits(), Release)
        }
        status
    }
    fn reset(&mut self) {
        self.engine.reset()
    }
    fn deactivate(&mut self) {
        self.engine.deactivate();
        self.config = None;
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn concurrent_queue_wraps_without_losing_values() {
        let shared = Shared::new(
            *params::schema().unwrap().defaults().values(),
            Arc::new(PanelLink::new()),
        );
        shared.head.store(u32::MAX - 10, Relaxed);
        shared.tail.store(u32::MAX - 10, Relaxed);
        std::thread::scope(|scope| {
            scope.spawn(|| {
                for i in 0..10000 {
                    while shared.push(params::TEMPO, 40. + (i % 260) as f32).is_err() {
                        std::thread::yield_now();
                    }
                }
            });
            for i in 0..10000 {
                let event = loop {
                    if let Some(e) = shared.pop() {
                        break e;
                    }
                    std::thread::yield_now();
                };
                assert_eq!(
                    event.data,
                    EventData::ParameterValue {
                        id: params::TEMPO,
                        value: 40. + (i % 260) as f32
                    }
                );
            }
        });
        assert!(!shared.pending());
    }
    #[test]
    fn synthetic_midi_notes_clock_and_drop_recovery() {
        let mut values = *params::schema().unwrap().defaults().values();
        values[9] = 0.;
        let shared = Shared::new(values, Arc::new(PanelLink::new()));
        let (mut source, receiver) = ravet_midi_device::PreparedMidiInput::new(2).unwrap();
        let mut renderer = ravet_device::PreparedOutput::new(
            Audio::new(shared.clone(), vec![receiver], 3),
            48000,
            2,
            FRAMES,
        )
        .unwrap();
        let mut out = [0.; 1024];
        source.capture(0, &[0x90, 48, 120]);
        renderer.render(&mut out);
        assert!(out.iter().any(|s| s.abs() > 1e-6));
        assert!(!shared.link.display().unwrap().running);
        source.capture(1, &[0x80, 48, 0]);
        shared.push(params::SYNC_MODE, 2.).unwrap();
        source.capture(2, &[0xfa]);
        renderer.render(&mut out);
        assert!(shared.link.display().unwrap().running);
        for time in 0..24 {
            source.capture(time, &[0xf8]);
            renderer.render(&mut out);
        }
        source.capture(3, &[0xfc]);
        renderer.render(&mut out);
        assert!(!shared.link.display().unwrap().running);
        source.capture(4, &[0x90, 48, 120]);
        renderer.render(&mut out);
        source.capture(5, &[0xb0, 1, 0]);
        source.capture(6, &[0xb0, 1, 0]);
        assert_eq!(
            source.capture(7, &[0x80, 48, 0]),
            ravet_midi_device::CaptureStatus::Full
        );
        renderer.render(&mut out);
        assert!(out.iter().all(|s| *s == 0.));
        assert_eq!(renderer.statistics().processor_faults, 0);
    }
    #[test]
    fn queue_validation_capacity_order_and_panel() {
        let shared = Shared::new(
            *params::schema().unwrap().defaults().values(),
            Arc::new(PanelLink::new()),
        );
        assert!(shared.push(ParameterId(99), 0.).is_err());
        assert!(shared.push(params::CUTOFF, f32::NAN).is_err());
        for n in 0..QUEUE {
            shared.push(params::TEMPO, 40. + n as f32).unwrap()
        }
        assert!(shared.push(params::RUN, 1.).is_err());
        for n in 0..QUEUE {
            assert_eq!(
                shared.pop().unwrap().data,
                EventData::ParameterValue {
                    id: params::TEMPO,
                    value: 40. + n as f32
                }
            );
        }
        assert!(shared.pop().is_none());
        shared.push(params::SYNC_MODE, 0.).unwrap();
        shared
            .link
            .push(dw303_engine::link::PanelAction::Press(
                dw303_engine::link::Button::RunStop,
            ))
            .unwrap();
        let mut renderer = ravet_device::PreparedOutput::new(
            Audio::new(shared.clone(), Vec::new(), 3),
            48000,
            2,
            FRAMES,
        )
        .unwrap();
        let mut out = [0.; 1024];
        renderer.render(&mut out);
        assert_eq!(shared.values()[9], 0.);
        assert!(shared.link.display().unwrap().running);
        assert!(
            out.as_chunks::<2>()
                .0
                .iter()
                .all(|v| v[0] == v[1] && v[0].is_finite())
        );
        assert!(out.iter().any(|v| v.abs() > 1e-6));
    }
}
