//! Main-thread edits queued for the one CLAP process/flush consumer.
#[cfg(test)]
const CLAP_GAIN_ID: ClapId = ClapId::new(1);
#[cfg(test)]
const CLAP_BYPASS_ID: ClapId = ClapId::new(2);
use clack_plugin::events::{
    event_types::{ParamGestureBeginEvent, ParamGestureEndEvent, ParamValueEvent},
    io::OutputEvents,
};
use clack_plugin::prelude::{ClapId, Pckn};
use std::sync::atomic::{
    AtomicU32, AtomicU64,
    Ordering::{Acquire, Relaxed, Release},
};

const CAPACITY: usize = 32;

/// An editor edit was rejected before any state or host notification changed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EditError {
    /// The stable ID does not belong to the engine schema.
    UnknownParameter,
    /// A value is not finite or cannot be canonicalized.
    InvalidValue,
    /// The bounded queue has no slot available.
    Full,
    /// Another continuous gesture owns the producer.
    GestureActive,
    /// No matching continuous gesture is open.
    NoGesture,
}
impl std::fmt::Display for EditError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "editor edit rejected: {self:?}")
    }
}
impl std::error::Error for EditError {}

/// Product-owned SPSC queue. Only GainMain enqueues; CLAP serializes consumers.
/// Atomic slots avoid shared non-atomic storage; Release/Acquire indices own slots.
pub(crate) struct Edits {
    slots: [AtomicU64; CAPACITY],
    kinds: [AtomicU32; CAPACITY],
    generations: [AtomicU32; CAPACITY],
    producer: AtomicU64,
    active: AtomicU64,
    write: AtomicU32,
    read: AtomicU32,
    stage: AtomicU32,
}
impl Default for Edits {
    fn default() -> Self {
        Self {
            slots: std::array::from_fn(|_| AtomicU64::new(0)),
            kinds: std::array::from_fn(|_| AtomicU32::new(0)),
            generations: std::array::from_fn(|_| AtomicU32::new(0)),
            producer: AtomicU64::new(0),
            active: AtomicU64::new(0),
            write: AtomicU32::new(0),
            read: AtomicU32::new(0),
            stage: AtomicU32::new(0),
        }
    }
}
impl Edits {
    /// Single main-thread producer. Value/ID must already be validated.
    pub fn push(&self, id: ClapId, value: f32, generation: u32) -> Result<(), EditError> {
        if self.producer.load(Relaxed) != 0 {
            return Err(EditError::GestureActive);
        }
        self.enqueue(id, value, generation, 0, CAPACITY)
    }
    fn enqueue(
        &self,
        id: ClapId,
        value: f32,
        generation: u32,
        kind: u32,
        limit: usize,
    ) -> Result<(), EditError> {
        let write = self.write.load(Relaxed);
        if write.wrapping_sub(self.read.load(Acquire)) >= limit as u32 {
            return Err(EditError::Full);
        }
        let word = ((id.get() as u64) << 32) | value.to_bits() as u64;
        self.generations[write as usize % CAPACITY].store(generation, Relaxed);
        self.slots[write as usize % CAPACITY].store(word, Relaxed);
        self.kinds[write as usize % CAPACITY].store(kind, Relaxed);
        self.write.store(write.wrapping_add(1), Release);
        Ok(())
    }
    /// One continuous gesture at a time. Reserve a slot for its eventual end.
    pub(crate) fn begin(&self, id: ClapId, generation: u32) -> Result<(), EditError> {
        if self.producer.load(Relaxed) != 0 {
            return Err(EditError::GestureActive);
        }
        self.enqueue(id, 0.0, generation, 1, CAPACITY - 1)?;
        self.producer.store(
            ((generation as u64) << 6) | ((id.get() as u64) << 1) | 1,
            Relaxed,
        );
        Ok(())
    }
    pub(crate) fn update(&self, id: ClapId, value: f32) -> Result<(), EditError> {
        let token = self.producer.load(Relaxed);
        if token == 0 || (((token >> 1) & 31) as u32 != id.get()) {
            return Err(EditError::NoGesture);
        }
        self.enqueue(id, value, (token >> 6) as u32, 2, CAPACITY - 1)
    }
    pub(crate) fn end(&self) -> Result<(), EditError> {
        let token = self.producer.load(Relaxed);
        if token == 0 {
            return Err(EditError::NoGesture);
        }
        let id = ClapId::new(((token >> 1) & 31) as u32);
        self.enqueue(id, 0.0, (token >> 6) as u32, 3, CAPACITY)?;
        self.producer.store(0, Relaxed);
        Ok(())
    }
    pub(crate) fn pending(&self) -> bool {
        self.read.load(Relaxed) != self.write.load(Acquire)
    }
    /// At most 96 output attempts per callback. Retain rejected events for the
    /// next callback; never duplicate an accepted begin/value/end.
    /// Restore invalidates old values but still closes an already begun gesture.
    pub fn drain(
        &self,
        output: &mut OutputEvents<'_>,
        expected_generation: u32,
        generation: impl Fn() -> u32,
        mut apply: impl FnMut(ClapId, f32),
    ) -> bool {
        for _ in 0..CAPACITY * 3 {
            // A restore after the callback snapshot is applied next callback;
            // do not consume newer edits against the old DSP baseline.
            if generation() != expected_generation {
                return self.pending() || self.active.load(Relaxed) != 0;
            }
            let active = self.active.load(Relaxed);
            if active != 0 && (active >> 6) as u32 != expected_generation {
                let id = ClapId::new(((active >> 1) & 31) as u32);
                if output.try_push(ParamGestureEndEvent::new(0, id)).is_err() {
                    return true;
                }
                self.active.store(0, Relaxed);
                continue;
            }
            let read = self.read.load(Relaxed);
            if read == self.write.load(Acquire) {
                return false;
            }
            let word = self.slots[read as usize % CAPACITY].load(Relaxed);
            let id = ClapId::new((word >> 32) as u32);
            let value = f32::from_bits(word as u32);
            let stale =
                self.generations[read as usize % CAPACITY].load(Relaxed) != expected_generation;
            let kind = self.kinds[read as usize % CAPACITY].load(Relaxed);
            if kind != 0 {
                let token = ((expected_generation as u64) << 6) | ((id.get() as u64) << 1) | 1;
                let active = self.active.load(Relaxed);
                if stale || (kind != 1 && active != token) {
                    self.read.store(read.wrapping_add(1), Release);
                    continue;
                }
                let result = match kind {
                    1 => output.try_push(ParamGestureBeginEvent::new(0, id)),
                    2 => output.try_push(ParamValueEvent::new(
                        0,
                        id,
                        Pckn::match_all(),
                        value as f64,
                    )),
                    _ => output.try_push(ParamGestureEndEvent::new(0, id)),
                };
                if result.is_err() {
                    return true;
                }
                match kind {
                    1 => self.active.store(token, Relaxed),
                    2 => apply(id, value),
                    _ => self.active.store(0, Relaxed),
                }
                self.read.store(read.wrapping_add(1), Release);
                continue;
            }
            let stage = self.stage.load(Relaxed);
            if stale && stage == 0 {
                self.read.store(read.wrapping_add(1), Release);
                continue;
            }
            if stale && stage == 1 {
                self.stage.store(2, Relaxed);
                continue;
            }
            let result = match stage {
                0 => output.try_push(ParamGestureBeginEvent::new(0, id)),
                1 => output.try_push(ParamValueEvent::new(0, id, Pckn::match_all(), value as f64)),
                _ => output.try_push(ParamGestureEndEvent::new(0, id)),
            };
            if result.is_err() {
                return true;
            }
            if stage == 1 {
                apply(id, value);
            }
            if stage == 2 {
                self.stage.store(0, Relaxed);
                self.read.store(read.wrapping_add(1), Release);
            } else {
                self.stage.store(stage + 1, Relaxed);
            }
        }
        self.pending()
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use clack_plugin::events::{
        UnknownEvent,
        io::{OutputEventBuffer, TryPushError},
    };
    pub struct Sink {
        pub events: [(u8, ClapId, f32); 128],
        pub length: usize,
        pub limit: usize,
    }
    impl Sink {
        pub fn new(limit: usize) -> Self {
            Self {
                events: [(0, CLAP_GAIN_ID, 0.0); 128],
                length: 0,
                limit,
            }
        }
    }
    impl OutputEventBuffer for Sink {
        fn try_push(&mut self, event: &UnknownEvent) -> Result<(), TryPushError> {
            if self.length >= self.limit {
                return Err(TryPushError::new());
            }
            self.events[self.length] = if let Some(e) = event.as_event::<ParamGestureBeginEvent>() {
                (0, e.param_id().unwrap(), 0.0)
            } else if let Some(e) = event.as_event::<ParamValueEvent>() {
                (1, e.param_id().unwrap(), e.value() as f32)
            } else if let Some(e) = event.as_event::<ParamGestureEndEvent>() {
                (2, e.param_id().unwrap(), 0.0)
            } else {
                panic!("unexpected editor event")
            };
            self.length += 1;
            Ok(())
        }
    }
    #[test]
    fn all_parameter_gestures_preserve_full_generation_and_id() {
        for id in 1..=22 {
            let queue = Edits::default();
            let id = ClapId::new(id);
            queue.begin(id, u32::MAX).unwrap();
            queue.update(id, 0.5).unwrap();
            queue.end().unwrap();
            let mut sink = Sink::new(128);
            let mut value = None;
            assert!(!queue.drain(
                &mut OutputEvents::from_buffer(&mut sink),
                u32::MAX,
                || u32::MAX,
                |i, v| value = Some((i, v))
            ));
            assert_eq!(value, Some((id, 0.5)));
            assert_eq!(
                &sink.events[..3],
                &[(0, id, 0.0), (1, id, 0.5), (2, id, 0.0)]
            );
        }
    }
    #[test]
    fn live_gesture_spans_callbacks_and_reserves_end_under_overflow() {
        let queue = Edits::default();
        queue.begin(CLAP_GAIN_ID, 7).unwrap();
        assert_eq!(
            queue.push(CLAP_BYPASS_ID, 1.0, 7),
            Err(EditError::GestureActive)
        );
        let mut sink = Sink::new(128);
        assert!(!queue.drain(
            &mut OutputEvents::from_buffer(&mut sink),
            7,
            || 7,
            |_, _| panic!("begin only")
        ));
        for value in 0..CAPACITY - 1 {
            queue.update(CLAP_GAIN_ID, value as f32).unwrap();
        }
        assert_eq!(queue.update(CLAP_GAIN_ID, 99.0), Err(EditError::Full));
        queue.end().unwrap();
        assert_eq!(queue.update(CLAP_GAIN_ID, 1.0), Err(EditError::NoGesture));
        let mut values = Vec::new();
        while queue.pending() {
            sink.limit = sink.length + 1;
            queue.drain(
                &mut OutputEvents::from_buffer(&mut sink),
                7,
                || 7,
                |_, value| values.push(value),
            );
        }
        assert_eq!(values, (0..31).map(|v| v as f32).collect::<Vec<_>>());
        assert_eq!(sink.length, 33);
        assert_eq!(sink.events[0].0, 0);
        assert_eq!(sink.events[32].0, 2);
    }
    #[test]
    fn restore_closes_idle_live_gesture_and_discards_its_remaining_values() {
        let queue = Edits::default();
        queue.begin(CLAP_GAIN_ID, 1).unwrap();
        let mut sink = Sink::new(128);
        queue.drain(
            &mut OutputEvents::from_buffer(&mut sink),
            1,
            || 1,
            |_, _| panic!("no value"),
        );
        sink.limit = 1;
        assert!(queue.drain(
            &mut OutputEvents::from_buffer(&mut sink),
            2,
            || 2,
            |_, _| panic!("no value")
        ));
        queue.update(CLAP_GAIN_ID, 3.0).unwrap();
        queue.end().unwrap();
        queue.push(CLAP_BYPASS_ID, 1.0, 2).unwrap();
        sink.limit = 128;
        let mut values = Vec::new();
        assert!(!queue.drain(
            &mut OutputEvents::from_buffer(&mut sink),
            2,
            || 2,
            |id, value| values.push((id, value))
        ));
        assert_eq!(values, [(CLAP_BYPASS_ID, 1.0)]);
        assert_eq!(
            &sink.events[..sink.length],
            &[
                (0, CLAP_GAIN_ID, 0.0),
                (2, CLAP_GAIN_ID, 0.0),
                (0, CLAP_BYPASS_ID, 0.0),
                (1, CLAP_BYPASS_ID, 1.0),
                (2, CLAP_BYPASS_ID, 0.0)
            ]
        );
    }
    #[test]
    fn rejected_output_resumes_without_duplicate_events_or_early_value_application() {
        let queue = Edits::default();
        queue.push(CLAP_BYPASS_ID, 1.0, 7).unwrap();
        let mut applied = Vec::new();
        let mut all = Vec::new();
        for expected in 0..3 {
            let mut sink = Sink::new(1);
            let pending = queue.drain(
                &mut OutputEvents::from_buffer(&mut sink),
                7,
                || 7,
                |id, value| applied.push((id, value)),
            );
            assert_eq!(pending, expected != 2);
            assert_eq!(sink.length, 1);
            all.push(sink.events[0]);
            assert_eq!(applied.len(), usize::from(expected >= 1));
        }
        assert_eq!(
            all,
            [
                (0, CLAP_BYPASS_ID, 0.0),
                (1, CLAP_BYPASS_ID, 1.0),
                (2, CLAP_BYPASS_ID, 0.0)
            ]
        );
        assert_eq!(applied, [(CLAP_BYPASS_ID, 1.0)]);
    }
    #[test]
    fn restore_drops_unstarted_edits_and_closes_started_gestures() {
        for begun in [false, true] {
            let queue = Edits::default();
            queue.push(CLAP_GAIN_ID, 3.0, 1).unwrap();
            if begun {
                let mut sink = Sink::new(1);
                assert!(queue.drain(
                    &mut OutputEvents::from_buffer(&mut sink),
                    1,
                    || 1,
                    |_, _| panic!("value not yet accepted")
                ));
            }
            let mut sink = Sink::new(128);
            assert!(!queue.drain(
                &mut OutputEvents::from_buffer(&mut sink),
                2,
                || 2,
                |_, _| panic!("stale value applied")
            ));
            assert_eq!(sink.length, usize::from(begun));
            if begun {
                assert_eq!(sink.events[0], (2, CLAP_GAIN_ID, 0.0));
            }
        }
    }
    #[test]
    fn new_restore_generation_defers_edits_until_audio_synchronizes() {
        let queue = Edits::default();
        queue.push(CLAP_GAIN_ID, 3.0, 2).unwrap();
        let mut sink = Sink::new(128);
        assert!(queue.drain(
            &mut OutputEvents::from_buffer(&mut sink),
            1,
            || 2,
            |_, _| panic!("newer edit consumed on older DSP")
        ));
        assert_eq!(sink.length, 0);
        let mut value = None;
        assert!(!queue.drain(
            &mut OutputEvents::from_buffer(&mut sink),
            2,
            || 2,
            |_, v| value = Some(v)
        ));
        assert_eq!(value, Some(3.0));
    }
    #[test]
    fn full_queue_rejects_whole_edit_and_indices_wrap() {
        let queue = Edits::default();
        queue.read.store(u32::MAX - 5, Relaxed);
        queue.write.store(u32::MAX - 5, Relaxed);
        for i in 0..CAPACITY {
            queue.push(CLAP_GAIN_ID, i as f32, 0).unwrap();
        }
        assert_eq!(queue.push(CLAP_GAIN_ID, 99.0, 0), Err(EditError::Full));
        let mut next = 0;
        assert!(!queue.drain(
            &mut OutputEvents::void(),
            0,
            || 0,
            |_, value| {
                assert_eq!(value, next as f32);
                next += 1;
            }
        ));
        assert_eq!(next, CAPACITY);
        queue.push(CLAP_GAIN_ID, 1.0, 0).unwrap();
    }
    #[test]
    fn concurrent_single_producer_and_consumer_preserve_order() {
        let queue = Edits::default();
        std::thread::scope(|scope| {
            let producer = scope.spawn(|| {
                for i in 0..10_000 {
                    while queue.push(CLAP_GAIN_ID, i as f32, 1).is_err() {
                        std::thread::yield_now();
                    }
                }
            });
            let mut next = 0;
            while next < 10_000 {
                queue.drain(
                    &mut OutputEvents::void(),
                    1,
                    || 1,
                    |_, value| {
                        assert_eq!(value, next as f32);
                        next += 1;
                    },
                );
                std::thread::yield_now();
            }
            producer.join().unwrap();
        });
    }
}
