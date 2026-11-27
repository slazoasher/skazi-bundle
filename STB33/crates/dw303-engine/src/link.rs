//! Bounded front-panel exchange. Producers serialize on the main thread; only
//! one audio consumer/publisher may use a link. Audio mutex access never waits.
pub use dw303_sequencer::edit::{Button, Editor, Led, Leds, Mode, PanelAction};
pub use dw303_sequencer::{Memory, Section};
use std::sync::{
    Mutex,
    atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering},
};

/// Maximum queued physical edges.
pub const CAPACITY: usize = 256;
/// Submission could not be accepted; caller must retry the same edge.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Full;
/// Coherent display state; blinking is resolved by the UI clock.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Display {
    /// Logical lamps.
    pub leds: Leds,
    /// MODE detent, zero based.
    pub mode: u8,
    /// NORMAL/PITCH/TIME submode, zero based.
    pub edit_mode: u8,
    /// Selected group, zero based.
    pub group: u8,
    /// Section B when true.
    pub section_b: bool,
    /// Pattern number, zero based.
    pub pattern: u8,
    /// Track number, zero based.
    pub track: u8,
    /// Editor cursor, zero based.
    pub step: u8,
    /// Transport state.
    pub running: bool,
}
/// Shared panel, display and complete memory exchange.
pub struct PanelLink {
    ring: [AtomicU32; CAPACITY],
    head: AtomicU32,
    tail: AtomicU32,
    producer: AtomicBool,
    sequence: AtomicU32,
    leds: [AtomicU64; 2],
    status: AtomicU64,
    memory: Mutex<Memory>,
    pending: Mutex<Option<Memory>>,
    generation: AtomicU32,
    /// Host tempo bits (f64), 0 when the host reports none.
    host_bpm: AtomicU64,
    /// PANIC pressed: the next audio block silences everything.
    panic: AtomicBool,
}
impl Default for PanelLink {
    fn default() -> Self {
        Self::new()
    }
}
impl PanelLink {
    /// Construct with factory group I/A/1 content.
    pub fn new() -> Self {
        Self {
            ring: core::array::from_fn(|_| AtomicU32::new(0)),
            head: AtomicU32::new(0),
            tail: AtomicU32::new(0),
            producer: AtomicBool::new(false),
            sequence: AtomicU32::new(0),
            leds: encode_leds(Editor::new().leds(&crate::factory_memory())).map(AtomicU64::new),
            status: AtomicU64::new(2),
            memory: Mutex::new(crate::factory_memory()),
            pending: Mutex::new(None),
            generation: AtomicU32::new(0),
            host_bpm: AtomicU64::new(0),
            panic: AtomicBool::new(false),
        }
    }
    /// PANIC message `{"type":"panic"}` from the editor: raise the flag for the audio thread.
    pub fn load_panic_message(&self, text: &str) -> bool {
        if text.len() > 64 || !text.contains(r#""type":"panic""#) {
            return false;
        }
        self.panic.store(true, Ordering::Release);
        true
    }
    /// Audio thread: true once per PANIC press.
    pub fn take_panic(&self) -> bool {
        self.panic.swap(false, Ordering::AcqRel)
    }
    /// Submit one action without waiting. Concurrent producers receive `Full`.
    pub fn push(&self, action: PanelAction) -> Result<(), Full> {
        if self
            .producer
            .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            return Err(Full);
        }
        let head = self.head.load(Ordering::Relaxed);
        let result = if head.wrapping_sub(self.tail.load(Ordering::Acquire)) >= CAPACITY as u32 {
            Err(Full)
        } else {
            self.ring[head as usize % CAPACITY].store(encode_action(action), Ordering::Relaxed);
            self.head.store(head.wrapping_add(1), Ordering::Release);
            Ok(())
        };
        self.producer.store(false, Ordering::Release);
        result
    }
    /// Audio consumer: remove at most one action.
    pub fn pop(&self) -> Option<PanelAction> {
        let tail = self.tail.load(Ordering::Relaxed);
        if tail == self.head.load(Ordering::Acquire) {
            return None;
        }
        let word = self.ring[tail as usize % CAPACITY].load(Ordering::Relaxed);
        self.tail.store(tail.wrapping_add(1), Ordering::Release);
        decode_action(word)
    }
    /// Audio publisher: one nonblocking attempt. Retry next block on false.
    pub fn publish_memory(&self, memory: &Memory) -> bool {
        if let Ok(mut slot) = self.memory.try_lock() {
            slot.clone_from(memory);
            true
        } else {
            false
        }
    }
    /// Main-thread save snapshot; includes a pending restore if not yet adopted.
    pub fn memory(&self) -> Memory {
        let pending = self.pending.lock().unwrap_or_else(|e| e.into_inner());
        if let Some(memory) = pending.as_ref() {
            return memory.clone();
        }
        self.memory
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .clone()
    }
    /// Main-thread restore after complete codec validation.
    pub fn load_memory(&self, memory: Memory) {
        *self.pending.lock().unwrap_or_else(|e| e.into_inner()) = Some(memory);
        self.generation.fetch_add(1, Ordering::Release);
    }
    /// Audio consumer: retry on contention, acknowledge only successful adoption.
    pub fn adopt_memory(&self, seen: &mut u32) -> Option<Memory> {
        if self.generation.load(Ordering::Acquire) == *seen {
            return None;
        }
        let mut slot = self.pending.try_lock().ok()?;
        let mut published = self.memory.try_lock().ok()?;
        if let Some(memory) = slot.as_ref() {
            published.clone_from(memory);
        }
        *seen = self.generation.load(Ordering::Acquire);
        slot.take()
    }
    /// Audio publisher: publish LEDs and status as one sequence.
    /// Host tempo from the audio thread, for the panel's BPM window.
    pub fn set_host_bpm(&self, bpm: Option<f64>) {
        self.host_bpm.store(bpm.filter(|b| b.is_finite()).unwrap_or(0.0).to_bits(), Ordering::Relaxed);
    }
    pub fn host_bpm(&self) -> Option<f64> {
        Some(f64::from_bits(self.host_bpm.load(Ordering::Relaxed))).filter(|b| *b > 0.0)
    }
    /// STB33 preset/random message `{"type":"pattern","code":"..."}`: loads the
    /// pattern into group I, section A, pattern 1 (the selection after a load).
    /// Code: 1..=16 tokens split by ',', each `<n|t|r><pitch a..m><d|m|u><accent 0|1><slide 0|1>`.
    pub fn load_pattern_message(&self, text: &str) -> bool {
        // Key order is up to the bridge's serializer, so look the fields up.
        if text.len() > 512 || !text.contains(r#""type":"pattern""#) {
            return false;
        }
        let Some(code) = text
            .split_once(r#""code":""#)
            .and_then(|(_, rest)| rest.split_once('"'))
            .map(|(code, _)| code)
        else {
            return false;
        };
        let Some(pattern) = decode_pattern(code) else {
            return false;
        };
        let mut memory = self.memory();
        memory.bank.patterns[0] = pattern;
        self.load_memory(memory);
        true
    }
    pub fn publish_display(&self, editor: &Editor, memory: &Memory, running: bool) {
        let s = editor.selection();
        let lamps = editor.leds(memory);
        let step = if running {
            lamps.steps.iter().position(|l| *l == Led::On).unwrap_or(0) as u8
        } else {
            s.step
        };
        let status = (match editor.edit_mode() {
            dw303_sequencer::edit::EditMode::Normal => 0u64,
            dw303_sequencer::edit::EditMode::Pitch => 1,
            dw303_sequencer::edit::EditMode::Time => 2,
        }) << 56
            | mode_index(editor.mode()) as u64
            | (s.pattern.group as u64) << 8
            | u64::from(s.pattern.section == Section::B) << 16
            | (s.pattern.pattern as u64) << 24
            | (s.track as u64) << 32
            | (step as u64) << 40
            | u64::from(running) << 48;
        self.sequence.fetch_add(1, Ordering::SeqCst);
        for (slot, word) in self.leds.iter().zip(encode_leds(lamps)) {
            slot.store(word, Ordering::SeqCst);
        }
        self.status.store(status, Ordering::SeqCst);
        self.sequence.fetch_add(1, Ordering::SeqCst);
    }
    /// Main-thread reader: bounded attempts; retain last display if None.
    pub fn display(&self) -> Option<Display> {
        for _ in 0..3 {
            let before = self.sequence.load(Ordering::SeqCst);
            if before & 1 != 0 {
                continue;
            }
            let words = self.leds.each_ref().map(|s| s.load(Ordering::SeqCst));
            let s = self.status.load(Ordering::SeqCst);
            if before == self.sequence.load(Ordering::SeqCst) {
                return Some(Display {
                    leds: decode_leds(words),
                    mode: s as u8,
                    edit_mode: (s >> 56) as u8,
                    group: (s >> 8) as u8,
                    section_b: (s >> 16) & 1 != 0,
                    pattern: (s >> 24) as u8,
                    track: (s >> 32) as u8,
                    step: (s >> 40) as u8,
                    running: (s >> 48) & 1 != 0,
                });
            }
        }
        None
    }
}
/// MODE legend detent order.
pub fn mode_index(mode: Mode) -> u8 {
    match mode {
        Mode::TrackWrite => 0,
        Mode::TrackPlay => 1,
        Mode::PatternPlay => 2,
        Mode::PatternWrite => 3,
    }
}
fn button_word(b: Button) -> u32 {
    match b {
        Button::Pitch(n) => n as u32,
        Button::Number(n) => 256 + n as u32,
        Button::Down => 512,
        Button::Up => 513,
        Button::Accent => 514,
        Button::Slide => 515,
        Button::Back => 516,
        Button::Tap => 517,
        Button::PitchMode => 518,
        Button::TimeMode => 519,
        Button::Function => 520,
        Button::Clear => 521,
        Button::RunStop => 522,
        Button::Delete => 523,
        Button::Insert => 524,
        Button::Step => 525,
        Button::Triplet => 526,
        Button::Time(dw303_sequencer::Time::Note) => 527,
        Button::Time(dw303_sequencer::Time::Tie) => 528,
        Button::Time(dw303_sequencer::Time::Rest) => 529,
    }
}
fn decode_button(n: u32) -> Option<Button> {
    Some(match n {
        0..=255 => Button::Pitch(n as u8),
        256..=511 => Button::Number((n - 256) as u8),
        512 => Button::Down,
        513 => Button::Up,
        514 => Button::Accent,
        515 => Button::Slide,
        516 => Button::Back,
        517 => Button::Tap,
        518 => Button::PitchMode,
        519 => Button::TimeMode,
        520 => Button::Function,
        521 => Button::Clear,
        522 => Button::RunStop,
        523 => Button::Delete,
        524 => Button::Insert,
        525 => Button::Step,
        526 => Button::Triplet,
        527 => Button::Time(dw303_sequencer::Time::Note),
        528 => Button::Time(dw303_sequencer::Time::Tie),
        529 => Button::Time(dw303_sequencer::Time::Rest),
        _ => return None,
    })
}
fn encode_action(a: PanelAction) -> u32 {
    match a {
        PanelAction::Press(b) => button_word(b),
        PanelAction::Release(b) => 1 << 16 | button_word(b),
        PanelAction::Mode(m) => 2 << 16 | mode_index(m) as u32,
        PanelAction::Group(n) => 3 << 16 | n as u32,
        PanelAction::Track(n) => 4 << 16 | n as u32,
        PanelAction::Section(s) => 5 << 16 | u32::from(s == Section::B),
    }
}
fn decode_action(w: u32) -> Option<PanelAction> {
    let n = w & 65535;
    Some(match w >> 16 {
        0 => PanelAction::Press(decode_button(n)?),
        1 => PanelAction::Release(decode_button(n)?),
        2 => PanelAction::Mode(match n {
            0 => Mode::TrackWrite,
            1 => Mode::TrackPlay,
            2 => Mode::PatternPlay,
            3 => Mode::PatternWrite,
            _ => return None,
        }),
        3 => PanelAction::Group(n as u8),
        4 => PanelAction::Track(n as u8),
        5 => PanelAction::Section(if n == 0 { Section::A } else { Section::B }),
        _ => return None,
    })
}
/// Pack all 50 logical lamps in two words.
pub fn encode_leds(l: Leds) -> [u64; 2] {
    let mut words = [0; 2];
    for (i, l) in l
        .pitch
        .into_iter()
        .chain(l.selectors)
        .chain(l.sections)
        .chain(l.modes)
        .chain([l.run, l.back])
        .chain(l.modifiers)
        .chain(l.step_mode)
        .chain(l.steps)
        .enumerate()
    {
        words[i / 32] |= (match l {
            Led::Off => 0,
            Led::On => 1,
            Led::Blink => 2,
        }) << (2 * (i % 32));
    }
    words
}
/// Unpack every logical lamp; reserved states decode dark.
pub fn decode_leds(w: [u64; 2]) -> Leds {
    let mut l = Leds::default();
    for (i, l) in l
        .pitch
        .iter_mut()
        .chain(&mut l.selectors)
        .chain(&mut l.sections)
        .chain(&mut l.modes)
        .chain([&mut l.run, &mut l.back])
        .chain(&mut l.modifiers)
        .chain(&mut l.step_mode)
        .chain(&mut l.steps)
        .enumerate()
    {
        *l = match (w[i / 32] >> (2 * (i % 32))) & 3 {
            1 => Led::On,
            2 => Led::Blink,
            _ => Led::Off,
        };
    }
    l
}
/// Decode a preset pattern code (see [`PanelLink::load_pattern_message`]).
pub fn decode_pattern(code: &str) -> Option<dw303_sequencer::Pattern> {
    use dw303_sequencer::{Octave, Pattern, Step, Time};
    let mut pattern = Pattern::default();
    let mut notes = 0;
    let mut length = 0;
    for (i, token) in code.split(',').enumerate() {
        let b = token.as_bytes();
        if i >= 16 || b.len() != 5 {
            return None;
        }
        pattern.time[i] = match b[0] {
            b'n' => Time::Note,
            b't' => Time::Tie,
            b'r' => Time::Rest,
            _ => return None,
        };
        if b[0] == b'n' {
            let pitch = b[1].checked_sub(b'a').filter(|p| *p <= 12)?;
            let octave = match b[2] {
                b'd' => Octave::Down,
                b'm' => Octave::Mid,
                b'u' => Octave::Up,
                _ => return None,
            };
            pattern.steps[notes] = Step { pitch, octave, accent: b[3] == b'1', slide: b[4] == b'1' };
            notes += 1;
        }
        length = i + 1;
    }
    pattern.length = u8::try_from(length).ok().filter(|l| *l >= 1)?;
    Some(pattern)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn pattern_message_loads_slot_one() {
        let link = PanelLink::new();
        assert!(!link.load_pattern_message(r#"{"type":"pattern","code":"x"}"#));
        assert!(!link.load_pattern_message(r#"{"type":"pattern","code":"nzm10"}"#));
        assert!(link.load_pattern_message(r#"{"type":"pattern","code":"nam10,ram00,nhu01,tam00"}"#));
        assert!(!link.take_panic());
        assert!(!link.load_panic_message(r#"{"type":"pattern"}"#));
        assert!(link.load_panic_message(r#"{"type":"panic"}"#));
        assert!(link.take_panic());
        assert!(!link.take_panic());
        let p = link.memory().bank.patterns[0];
        assert_eq!((p.length, p.steps[1].pitch, p.steps[0].accent, p.steps[1].slide), (4, 7, true, true));
    }
    #[test]
    fn ring_full_order_edges() {
        let l = PanelLink::new();
        for n in 0..CAPACITY {
            l.push(if n % 2 == 0 {
                PanelAction::Press(Button::Pitch((n % 13) as u8))
            } else {
                PanelAction::Release(Button::Pitch((n % 13) as u8))
            })
            .unwrap();
        }
        assert_eq!(l.push(PanelAction::Group(0)), Err(Full));
        for n in 0..CAPACITY {
            assert_eq!(
                l.pop(),
                Some(if n % 2 == 0 {
                    PanelAction::Press(Button::Pitch((n % 13) as u8))
                } else {
                    PanelAction::Release(Button::Pitch((n % 13) as u8))
                })
            );
        }
        assert_eq!(l.pop(), None);
    }
    #[test]
    fn concurrent_ring_preserves_actions() {
        let link = std::sync::Arc::new(PanelLink::new());
        let producer = link.clone();
        let thread = std::thread::spawn(move || {
            for n in 0..4096 {
                let a = if n % 2 == 0 {
                    PanelAction::Press(Button::Pitch((n % 13) as u8))
                } else {
                    PanelAction::Release(Button::Pitch((n % 13) as u8))
                };
                while producer.push(a).is_err() {
                    std::thread::yield_now();
                }
            }
        });
        for n in 0..4096 {
            let expected = if n % 2 == 0 {
                PanelAction::Press(Button::Pitch((n % 13) as u8))
            } else {
                PanelAction::Release(Button::Pitch((n % 13) as u8))
            };
            let actual = loop {
                if let Some(a) = link.pop() {
                    break a;
                }
                std::thread::yield_now();
            };
            assert_eq!(actual, expected);
        }
        thread.join().unwrap();
    }
    #[test]
    fn lamps_roundtrip() {
        let mut l = Leds::default();
        for (i, v) in l.pitch.iter_mut().enumerate() {
            *v = match i % 3 {
                0 => Led::Off,
                1 => Led::On,
                _ => Led::Blink,
            };
        }
        l.steps = [Led::Blink; 16];
        l.selectors = [Led::On; 8];
        l.sections = [Led::Blink; 2];
        l.modes = [Led::On; 3];
        l.run = Led::On;
        l.back = Led::Blink;
        l.modifiers = [Led::Blink; 4];
        l.step_mode = [Led::On; 2];
        assert_eq!(decode_leds(encode_leds(l)), l);
    }
    #[test]
    fn memory_contention_retries() {
        let l = PanelLink::new();
        let m = Memory::default();
        let guard = l.memory.lock().unwrap();
        assert!(!l.publish_memory(&m));
        drop(guard);
        assert!(l.publish_memory(&m));
        l.load_memory(m.clone());
        let guard = l.pending.lock().unwrap();
        let mut seen = 0;
        assert!(l.adopt_memory(&mut seen).is_none());
        assert_eq!(seen, 0);
        drop(guard);
        assert_eq!(l.adopt_memory(&mut seen), Some(m));
        assert!(l.adopt_memory(&mut seen).is_none());
    }
}
