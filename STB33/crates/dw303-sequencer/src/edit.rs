//! Fixed-storage front-panel editor. Button edges, including releases, are required.
use crate::{
    ClockSource, Memory, Octave, Pattern, PatternRef, Section, Sequencer, Step, StepEvent, Time,
    Track, TrackEntry, VoiceCommand,
};

/// Four detents of the original MODE switch.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Mode {
    /// Write an arrangement measure by measure.
    TrackWrite,
    /// Play the selected arrangement.
    TrackPlay,
    /// Play and queue pattern chains.
    #[default]
    PatternPlay,
    /// Enter independent pitch and time data.
    PatternWrite,
}
/// Submode selected by FUNCTION, PITCH MODE and TIME MODE.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum EditMode {
    /// Pattern selection and normal playback.
    #[default]
    Normal,
    /// Pitch entry or TAP recall.
    Pitch,
    /// Note/sustain/rest entry or TAP recall.
    Time,
}
/// Panel switches. Semantic aliases avoid coupling the editor to SVG coordinates.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Button {
    /// Chromatic keyboard, C=0 through C'=12.
    Pitch(u8),
    /// Pattern selectors, numbered 1..=8 (also the white pitch keys).
    Number(u8),
    /// Lower octave / time-mode note.
    Down,
    /// Upper octave / time-mode sustain.
    Up,
    /// Accent / time-mode rest.
    Accent,
    /// Outgoing portamento.
    Slide,
    /// Return one pitch/time position.
    /// VERIFY: TIME-mode BACK and stopped track browsing are deterministic
    /// conveniences; exact original BACK/D.S. multiplexing needs confirmation.
    Back,
    /// WRITE/NEXT and TAP are the same switch.
    Tap,
    /// Select pitch mode; hold for track key shift.
    PitchMode,
    /// Select time mode.
    TimeMode,
    /// Return to Normal; hold for secondary functions.
    Function,
    /// PATTERN CLEAR / D.C. / BAR RESET are the same switch.
    Clear,
    /// Start or stop.
    RunStop,
    /// FUNCTION + DEL (C key).
    Delete,
    /// FUNCTION + INS (D key).
    Insert,
    /// FUNCTION + STEP; press once per required rhythm position.
    Step,
    /// FUNCTION + triplet selector.
    Triplet,
    /// Explicit time-symbol alias for adapters.
    Time(Time),
}
const WHITE_PITCHES: [u8; 8] = [0, 2, 4, 5, 7, 9, 11, 12];
impl Button {
    fn index(self) -> Option<usize> {
        Some(match self {
            Self::Pitch(n) if n <= 12 => WHITE_PITCHES
                .iter()
                .position(|&p| p == n)
                .map_or(usize::from(n), |i| 13 + i),
            Self::Number(n) if (1..=8).contains(&n) => 13 + usize::from(n - 1),
            Self::Down => 21,
            Self::Up => 22,
            Self::Accent => 23,
            Self::Slide => 24,
            Self::Back => 25,
            Self::Tap => 26,
            Self::PitchMode => 27,
            Self::TimeMode => 28,
            Self::Function => 29,
            Self::Clear => 30,
            Self::RunStop => 31,
            Self::Delete => 32,
            Self::Insert => 33,
            Self::Step => 34,
            Self::Triplet => 35,
            Self::Time(Time::Note) => 36,
            Self::Time(Time::Tie) => 37,
            Self::Time(Time::Rest) => 38,
            _ => return None,
        })
    }
}
/// Control-thread input. Selectors and keyboard values are checked before use.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PanelAction {
    /// Turn MODE; stops transport and leaves edit mode.
    /// VERIFY: implicit stop is a deterministic interpretation of the manual's
    /// instruction to stop before changing MODE; original live switching is unspecified.
    Mode(Mode),
    /// Pattern-group detent I..IV, zero based.
    Group(u8),
    /// Track detent 1..7, zero based; also selects its accessible pattern group.
    Track(u8),
    /// Select A or B.
    Section(Section),
    /// Physical down edge (duplicate downs are ignored).
    Press(Button),
    /// Physical up edge.
    Release(Button),
}
/// Invalid selector or key input; memory remains unchanged.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EditError {
    /// Out-of-range panel input.
    InvalidAction,
}
/// Transport request produced on the control thread.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum TransportCommand {
    /// No transport change.
    #[default]
    None,
    /// Rewind and start.
    Start,
    /// Stop immediately.
    Stop,
    /// Rewind, preserving the editor's running state.
    Reset,
}
/// Result of one input; callers publish memory only when `changed` is true.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct EditOutput {
    /// Pattern/track data changed.
    pub changed: bool,
    /// Preview gate/attack or release; feed triggers for one sample only.
    pub preview: Option<VoiceCommand>,
    /// Optional clock operation, applied by [`Editor::apply_transport`].
    pub transport: TransportCommand,
}
/// A pattern chain in one group and section, inclusive ascending endpoints.
/// Original blocks contain patterns 1..4 and 5..8; cross-block chords select
/// only the most recently pressed pattern.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PatternChain {
    /// First pattern.
    pub first: PatternRef,
    /// Last pattern number, zero based, inclusive.
    pub last: u8,
}
/// Current panel cursors. Pitch/time positions are independent of playback position.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Selection {
    /// Selected pattern (queued selection while running).
    pub pattern: PatternRef,
    /// Selected track, zero based.
    pub track: u8,
    /// Pitch or time entry cursor, zero based; 16 indicates exhausted pitch entry.
    pub step: u8,
    /// Track-write measure cursor, zero based.
    pub measure: u8,
}
/// Explicit blinking state; render with the caller's common blink phase.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Led {
    /// Dark.
    #[default]
    Off,
    /// Continuously lit.
    On,
    /// Lit in the high half of the caller's phase.
    Blink,
}
impl Led {
    /// Resolve a logical LED to light intensity without an internal timer.
    pub fn lit(self, phase: bool) -> bool {
        self == Self::On || (self == Self::Blink && phase)
    }
}
/// Complete panel indication. `steps` is an additional logical 16-position view;
/// the original panel shows step counts with its selector LEDs, not sixteen lamps.
/// VERIFY: exact original LED multiplexing during FUNCTION and playback.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Leds {
    /// C..C' keyboard indicators.
    pub pitch: [Led; 13],
    /// Pattern selector indicators (shared white-key lamps on the real panel).
    pub selectors: [Led; 8],
    /// A and B.
    pub sections: [Led; 2],
    /// NORMAL, PITCH, TIME.
    pub modes: [Led; 3],
    /// RUN/BATTERY (battery monitoring belongs to the host).
    pub run: Led,
    /// BACK/sign lamp. No persistent D.S. marker is stored by version-one memory.
    pub back: Led,
    /// DOWN, UP, ACCENT, SLIDE.
    pub modifiers: [Led; 4],
    /// Straight and triplet step-mode indication.
    pub step_mode: [Led; 2],
    /// Additional cursor view, for editor adapters.
    pub steps: [Led; 16],
}
/// Original-style panel controller; transient edit/transport state is not saved
/// in the version-one memory codec. All arrays and loops have fixed bounds.
#[derive(Clone, Debug)]
pub struct Editor {
    mode: Mode,
    edit: EditMode,
    selection: Selection,
    held: u64,
    running: bool,
    pitch_cursor: u8,
    time_cursor: u8,
    recalled: Option<u8>,
    length_count: u8,
    transpose: i8,
    insert: bool,
    dc: bool,
    chain: PatternChain,
    playback_chain: Track,
    chain_offset: u8,
    clock_switch: bool,
    queued: Option<PatternChain>,
    playing: PatternRef,
    play_step: u8,
    origin: f64,
    last_position: Option<(usize, i64)>,
    restart: bool,
    preview: VoiceCommand,
    preview_owner: Option<Button>,
    displayed: Option<u8>,
    bar_number: u16,
    track_start: u8,
    /// PATT.GROUP position in pattern modes; applies at the next pattern-key press.
    group_knob: u8,
}
impl Default for Editor {
    fn default() -> Self {
        Self::new()
    }
}
impl Editor {
    /// Construct stopped Pattern Play, group I, section A, pattern 1, track 1.
    pub fn new() -> Self {
        let pattern = PatternRef::default();
        Self {
            mode: Mode::PatternPlay,
            edit: EditMode::Normal,
            selection: Selection {
                pattern,
                track: 0,
                step: 0,
                measure: 0,
            },
            held: 0,
            running: false,
            pitch_cursor: 0,
            time_cursor: 0,
            recalled: None,
            length_count: 0,
            transpose: 0,
            insert: false,
            dc: false,
            chain: PatternChain {
                first: pattern,
                last: 0,
            },
            playback_chain: Track::default(),
            chain_offset: 0,
            clock_switch: false,
            queued: None,
            playing: pattern,
            play_step: 0,
            origin: 0.0,
            last_position: None,
            restart: true,
            preview: VoiceCommand::default(),
            preview_owner: None,
            displayed: None,
            bar_number: 0,
            track_start: 0,
            group_knob: 0,
        }
    }
    /// MODE switch position.
    pub fn mode(&self) -> Mode {
        self.mode
    }
    /// Current pitch/time/normal submode.
    pub fn edit_mode(&self) -> EditMode {
        self.edit
    }
    /// Selected memories and edit cursors.
    pub fn selection(&self) -> Selection {
        self.selection
    }
    /// Front-panel transport latch.
    pub fn running(&self) -> bool {
        self.running
    }
    /// Currently active repeating chain.
    pub fn chain(&self) -> PatternChain {
        self.chain
    }
    /// Chain waiting for the end of the sounding pattern.
    pub fn queued_chain(&self) -> Option<PatternChain> {
        self.queued
    }
    /// Pattern sounding now (selection may already indicate the queued pattern).
    pub fn playing_pattern(&self) -> PatternRef {
        self.playing
    }
    fn down(&self, b: Button) -> bool {
        b.index().is_some_and(|n| self.held & (1u64 << n) != 0)
    }
    fn pattern_index(&self) -> usize {
        self.selection.pattern.index().unwrap_or(0)
    }
    fn select_chain(&mut self) {
        let mut first = self.selection.pattern.pattern;
        let mut last = first;
        for n in 1..=8 {
            if self.down(Button::Number(n)) && (n - 1) / 4 == self.selection.pattern.pattern / 4 {
                first = first.min(n - 1);
                last = last.max(n - 1);
            }
        }
        let chain = PatternChain {
            first: PatternRef {
                pattern: first,
                ..self.selection.pattern
            },
            last,
        };
        if self.running && self.mode == Mode::PatternPlay {
            self.queued = Some(chain);
        } else {
            self.chain = chain;
            self.playing = chain.first;
            self.restart = true;
        }
    }
    fn rebuild_chain(&mut self) {
        self.chain_offset = 0;
        self.playback_chain.length = self.chain.last - self.chain.first.pattern + 1;
        for i in 0..usize::from(self.playback_chain.length) {
            self.playback_chain.entries[i] = TrackEntry {
                pattern: PatternRef {
                    pattern: self.chain.first.pattern + i as u8,
                    ..self.chain.first
                },
                transpose: 0,
            };
        }
    }
    fn normal(&mut self) {
        self.edit = EditMode::Normal;
        self.recalled = None;
        self.displayed = None;
    }
    /// VERIFY: preview is gated until key/TAP release. The analog VEG supplies
    /// its decay; exact original firmware preview-gate pulse width is unmeasured.
    fn preview_note(&mut self, step: Step, owner: Button, out: &mut EditOutput) {
        let base = match step.octave {
            Octave::Down => 0,
            Octave::Mid => 12,
            Octave::Up => 24,
        };
        self.preview = VoiceCommand {
            note: step.pitch.min(12) + base,
            gate: true,
            trigger: true,
            accent: step.accent,
            slide: false,
        };
        self.preview_owner = Some(owner);
        out.preview = Some(self.preview);
    }
    /// Apply one panel edge to caller-owned memory. Invalid inputs are rejected
    /// before changing either editor state or memory; redundant downs do nothing.
    pub fn action(
        &mut self,
        memory: &mut Memory,
        action: PanelAction,
    ) -> Result<EditOutput, EditError> {
        let mut out = EditOutput::default();
        match action {
            PanelAction::Mode(mode) => {
                self.mode = mode;
                self.transpose = 0;
                self.running = false;
                self.normal();
                self.held = 0;
                self.insert = false;
                self.dc = false;
                self.queued = None;
                self.restart = true;
                self.preview_owner = None;
                self.preview = VoiceCommand::default();
                out.preview = Some(self.preview);
                out.transport = TransportCommand::Stop;
                // Track modes are tied to the knob's group; adopt any pending group.
                if matches!(mode, Mode::TrackWrite | Mode::TrackPlay)
                    && self.selection.pattern.group != self.group_knob
                {
                    self.selection.pattern.group = self.group_knob;
                    self.select_chain();
                }
            }
            PanelAction::Group(n) => {
                if n >= 4
                    || (matches!(self.mode, Mode::TrackWrite | Mode::TrackPlay)
                        && n != self.selection.track / 2)
                {
                    return Err(EditError::InvalidAction);
                }
                self.group_knob = n;
                if matches!(self.mode, Mode::TrackWrite | Mode::TrackPlay) {
                    self.selection.pattern.group = n;
                    self.normal();
                    self.select_chain();
                }
            }
            PanelAction::Track(n) => {
                if n >= 7 {
                    return Err(EditError::InvalidAction);
                }
                // Original track pairs share a pattern group: 1/2=I, 3/4=II, 5/6=III, 7=IV.
                self.selection.track = n;
                self.group_knob = n / 2;
                if matches!(self.mode, Mode::TrackWrite | Mode::TrackPlay) {
                    self.selection.pattern.group = n / 2;
                    // Selecting a track restarts track playback from measure 1.
                    self.selection.measure = 0;
                    self.transpose = 0;
                    self.select_chain();
                    self.restart = true;
                    out.transport = TransportCommand::Reset;
                }
                // In pattern modes the knob is PATT.GROUP: it only chooses the
                // group the pattern keys address. The selected/playing pattern
                // changes when a pattern key is pressed (`number`).
            }
            PanelAction::Section(section) => {
                self.selection.pattern.section = section;
                self.select_chain();
            }
            PanelAction::Release(b) => {
                let n = b.index().ok_or(EditError::InvalidAction)?;
                self.held &= !(1u64 << n);
                if b == Button::Function {
                    self.length_count = 0;
                }
                if b == Button::Tap {
                    self.recalled = None;
                }
                if self.preview_owner.and_then(Button::index) == b.index() {
                    self.preview.gate = false;
                    self.preview.trigger = false;
                    self.preview_owner = None;
                    out.preview = Some(self.preview);
                }
            }
            PanelAction::Press(b) => {
                let n = b.index().ok_or(EditError::InvalidAction)?;
                if self.held & (1u64 << n) != 0 {
                    return Ok(out);
                }
                self.held |= 1u64 << n;
                self.press(memory, b, &mut out);
            }
        }
        Ok(out)
    }
    /// PATTERN CLEAR empties timing data (sixteen straight Rests, so the pattern
    /// is silent) and keeps the pitch data, which PITCH MODE + TAP can still recall.
    fn press(&mut self, memory: &mut Memory, b: Button, out: &mut EditOutput) {
        let index = self.pattern_index();
        let writing = self.mode == Mode::PatternWrite && !self.running;
        // Table-driven stopped Pattern Write submode transitions.
        const MODES: [(Button, EditMode); 3] = [
            (Button::Function, EditMode::Normal),
            (Button::PitchMode, EditMode::Pitch),
            (Button::TimeMode, EditMode::Time),
        ];
        if writing {
            for (key, mode) in MODES {
                if b == key {
                    self.edit = mode;
                    break;
                }
            }
        }
        match b {
            Button::RunStop => {
                self.running = !self.running;
                if self.running {
                    self.track_start = self.selection.measure;
                }
                if self.mode != Mode::PatternWrite {
                    self.normal();
                }
                self.restart = true;
                self.last_position = None;
                self.preview.gate = false;
                self.preview.trigger = false;
                out.preview = Some(self.preview);
                out.transport = if self.running {
                    TransportCommand::Start
                } else {
                    TransportCommand::Stop
                };
            }
            Button::Function if self.mode != Mode::PatternWrite || !self.running => {
                self.normal();
                self.length_count = 0;
                self.bar_number = 0;
            }
            Button::PitchMode if writing => {
                self.edit = EditMode::Pitch;
                self.pitch_cursor = 0;
                self.selection.step = 0;
                self.recalled = None;
                self.displayed = None;
            }
            Button::TimeMode if writing => {
                self.edit = EditMode::Time;
                self.time_cursor = 0;
                self.selection.step = 0;
                self.recalled = None;
            }
            Button::Clear => {
                if writing
                    && self.edit == EditMode::Normal
                    && (1..=8).any(|n| self.down(Button::Number(n)))
                {
                    let pitches = memory.bank.patterns[index].steps;
                    memory.bank.patterns[index] = Pattern {
                        steps: pitches,
                        time: [Time::Rest; 16],
                        ..Pattern::default()
                    };
                    out.changed = true;
                    self.pitch_cursor = 0;
                    self.time_cursor = 0;
                    self.selection.step = 0;
                } else if self.mode == Mode::TrackWrite && self.running {
                    self.dc = true;
                } else if matches!(self.mode, Mode::TrackWrite | Mode::TrackPlay) {
                    self.selection.measure = 0;
                    self.track_start = 0;
                    self.restart = true;
                    out.transport = TransportCommand::Reset;
                }
            }
            Button::Triplet if writing && self.down(Button::Function) => {
                memory.bank.patterns[index].triplet = true;
                memory.bank.patterns[index].length = 12;
                out.changed = true;
            }
            Button::Step if writing && self.down(Button::Function) => {
                // Counting steps clears timing data. A sixteenth press in 3/4
                // (maximum fifteen steps) returns the pattern to 4/4.
                let p = &mut memory.bank.patterns[index];
                if self.length_count == 0 {
                    p.time = [Time::Rest; 16];
                }
                self.length_count = self.length_count.saturating_add(1).min(16);
                if self.length_count == 16 {
                    p.triplet = false;
                }
                p.length = self.length_count;
                out.changed = true;
            }
            Button::Number(n) => self.number(memory, n, b, out),
            Button::Pitch(n) => {
                if self.mode == Mode::TrackWrite
                    && self.down(Button::Function)
                    && (n == 0 || n == 2)
                {
                    self.edit_track(memory, n == 2, out);
                } else if let Some(i) = WHITE_PITCHES.iter().position(|&p| p == n) {
                    self.number(memory, i as u8 + 1, b, out);
                } else {
                    self.pitch(memory, n, b, out);
                }
            }
            Button::Down | Button::Up | Button::Accent | Button::Slide => {
                if matches!(self.mode, Mode::TrackWrite | Mode::TrackPlay)
                    && !self.running
                    && self.down(Button::Function)
                {
                    if b == Button::Down {
                        self.bar_digit(memory, 9);
                    } else if b == Button::Up {
                        self.bar_digit(memory, 0);
                    }
                } else if writing && self.edit == EditMode::Time {
                    let t = match b {
                        Button::Down => Some(Time::Note),
                        Button::Up => Some(Time::Tie),
                        Button::Accent => Some(Time::Rest),
                        _ => None,
                    };
                    if let Some(t) = t {
                        self.time(memory, Some(t), out);
                    }
                } else if writing && self.edit == EditMode::Pitch {
                    // Accent and slide are written only while TAP recalls a note.
                    if self.recalled.is_none() {
                        return;
                    }
                    let slot = self.recalled.unwrap_or(0).min(15) as usize;
                    let s = &mut memory.bank.patterns[index].steps[slot];
                    match b {
                        Button::Down => {
                            s.octave = if s.octave == Octave::Down {
                                Octave::Mid
                            } else {
                                Octave::Down
                            }
                        }
                        Button::Up => {
                            s.octave = if s.octave == Octave::Up {
                                Octave::Mid
                            } else {
                                Octave::Up
                            }
                        }
                        Button::Accent => s.accent = !s.accent,
                        Button::Slide => s.slide = !s.slide,
                        _ => {}
                    }
                    out.changed = true;
                }
            }
            Button::Time(t) if writing && self.edit == EditMode::Time => {
                self.time(memory, Some(t), out)
            }
            Button::Back if writing => {
                match self.edit {
                    EditMode::Pitch => {
                        self.pitch_cursor = self.pitch_cursor.saturating_sub(1);
                        self.selection.step = self.pitch_cursor;
                    }
                    EditMode::Time => {
                        self.time_cursor = self.time_cursor.saturating_sub(1);
                        self.selection.step = self.time_cursor;
                    }
                    EditMode::Normal => {}
                }
                self.recalled = None;
            }
            Button::Back if self.mode == Mode::TrackWrite && !self.running => {
                self.selection.measure = self.selection.measure.saturating_sub(1);
            }
            Button::Tap if writing && self.edit == EditMode::Pitch => {
                // Only the first `length` pitches can sound, so recall stops there.
                if self.pitch_cursor < memory.bank.patterns[index].length.clamp(1, 16) {
                    let slot = self.pitch_cursor;
                    self.recalled = Some(slot);
                    self.displayed = Some(slot);
                    self.preview_note(memory.bank.patterns[index].steps[usize::from(slot)], b, out);
                    self.pitch_cursor += 1;
                    self.selection.step = slot;
                }
            }
            Button::Tap if writing && self.edit == EditMode::Time => self.time(memory, None, out),
            Button::Tap if self.mode == Mode::TrackWrite && self.running => {
                self.commit_measure(memory, out)
            }
            Button::Delete if self.mode == Mode::TrackWrite && self.down(Button::Function) => {
                self.edit_track(memory, false, out)
            }
            Button::Insert if self.mode == Mode::TrackWrite && self.down(Button::Function) => {
                self.edit_track(memory, true, out)
            }
            _ => {}
        }
    }
    fn number(&mut self, memory: &mut Memory, n: u8, owner: Button, out: &mut EditOutput) {
        if matches!(self.mode, Mode::TrackWrite | Mode::TrackPlay)
            && !self.running
            && self.down(Button::Function)
        {
            self.bar_digit(memory, n);
        } else if (self.mode == Mode::PatternWrite && !self.running && self.edit == EditMode::Pitch)
            || (matches!(self.mode, Mode::TrackWrite | Mode::PatternPlay)
                && self.down(Button::PitchMode))
        {
            self.pitch(memory, WHITE_PITCHES[usize::from(n - 1)], owner, out);
        } else if self.edit == EditMode::Normal {
            self.selection.pattern.group = self.group_knob;
            self.selection.pattern.pattern = n - 1;
            self.select_chain();
            if self.mode == Mode::TrackWrite {
                self.transpose = 0;
            }
        }
    }
    /// VERIFY: FUNCTION-held decimal BAR entry uses DOWN=9, UP=0;
    /// original overlapping BAR/secondary-key legends need checking in a clearer scan.
    fn bar_digit(&mut self, memory: &Memory, digit: u8) {
        self.bar_number = (self.bar_number.saturating_mul(10) + u16::from(digit)).min(999);
        let length = memory.tracks[usize::from(self.selection.track)]
            .length
            .min(64);
        if (1..=u16::from(length)).contains(&self.bar_number) {
            self.selection.measure = (self.bar_number - 1) as u8;
            let entry = memory.tracks[usize::from(self.selection.track)].entries
                [usize::from(self.selection.measure)];
            self.selection.pattern = entry.pattern;
            self.transpose = entry.transpose;
            self.chain = PatternChain {
                first: entry.pattern,
                last: entry.pattern.pattern,
            };
            self.playing = entry.pattern;
            self.restart = true;
        }
    }
    /// VERIFY: keyboard C is unshifted, C' is +12; DOWN selects -12..0.
    /// Pitch entry stops at the pattern's step count, since no later pitch can
    /// sound; BACK or PITCH MODE allow overwriting.
    /// VERIFY: the original stores sixteen pitches regardless of step count;
    /// whether its cursor stops at the step count is unconfirmed.
    fn pitch(&mut self, memory: &mut Memory, pitch: u8, owner: Button, out: &mut EditOutput) {
        if matches!(self.mode, Mode::TrackWrite | Mode::PatternPlay) && self.down(Button::PitchMode)
        {
            self.transpose = pitch as i8 - if self.down(Button::Down) { 12 } else { 0 };
            if !self.running {
                self.preview_note(
                    Step {
                        pitch,
                        ..Step::default()
                    },
                    owner,
                    out,
                );
            }
        } else if self.mode == Mode::PatternWrite && !self.running && self.edit == EditMode::Pitch {
            let index = self.pattern_index();
            let slot = self.recalled.unwrap_or(self.pitch_cursor);
            if slot >= memory.bank.patterns[index].length.clamp(1, 16) {
                return;
            }
            let s = &mut memory.bank.patterns[index].steps[usize::from(slot)];
            // Pitch-key changes reset flags; WRITE/NEXT property edits keep them.
            if s.pitch != pitch {
                s.accent = false;
                s.slide = false;
            }
            s.pitch = pitch;
            if self.down(Button::Down) {
                s.octave = Octave::Down;
            } else if self.down(Button::Up) {
                s.octave = Octave::Up;
            } else if self.recalled.is_none() {
                s.octave = Octave::Mid;
            }
            let step = *s;
            self.displayed = Some(slot);
            out.changed = true;
            self.preview_note(step, owner, out);
            if self.recalled.is_none() {
                self.pitch_cursor += 1;
            }
            self.selection.step = self.pitch_cursor;
        }
    }
    fn time(&mut self, memory: &mut Memory, value: Option<Time>, out: &mut EditOutput) {
        let index = self.pattern_index();
        let p = &mut memory.bank.patterns[index];
        let slot = usize::from(self.time_cursor.min(15));
        if let Some(t) = value {
            p.time[slot] = t;
            out.changed = true;
        }
        self.recalled = Some(slot as u8);
        self.time_cursor += 1;
        self.selection.step = self.time_cursor;
        if self.time_cursor >= p.length.clamp(1, 16) {
            self.normal();
            self.selection.step = 0;
        }
    }
    fn edit_track(&mut self, memory: &mut Memory, insert: bool, out: &mut EditOutput) {
        if self.running {
            return;
        }
        let t = &mut memory.tracks[usize::from(self.selection.track)];
        let len = usize::from(t.length.min(64));
        let pos = usize::from(self.selection.measure).min(63);
        if insert {
            self.insert = len < 64 && pos <= len;
        } else if pos < len {
            t.entries.copy_within(pos + 1..len, pos);
            t.entries[len - 1] = TrackEntry::default();
            t.length = (len - 1) as u8;
            out.changed = true;
            self.selection.measure = self.selection.measure.min(t.length.saturating_sub(1));
        }
    }
    fn commit_measure(&mut self, memory: &mut Memory, out: &mut EditOutput) {
        let t = &mut memory.tracks[usize::from(self.selection.track)];
        let mut pos = usize::from(self.selection.measure).min(63);
        let mut wrote = false;
        for pattern in self.chain.first.pattern..=self.chain.last {
            let len = usize::from(t.length.min(64));
            if self.insert {
                if len == 64 || pos > len {
                    break;
                }
                t.entries.copy_within(pos..len, pos + 1);
                t.length = (len + 1) as u8;
            }
            t.entries[pos] = TrackEntry {
                pattern: PatternRef {
                    pattern,
                    ..self.chain.first
                },
                transpose: self.transpose,
            };
            t.length = t.length.max((pos + 1) as u8).min(64);
            wrote = true;
            pos += 1;
            if pos == 64 {
                break;
            }
        }
        self.insert = false;
        if self.dc && wrote {
            t.length = pos as u8;
            self.dc = false;
            self.selection.measure = 0;
        } else {
            self.selection.measure = pos.min(63) as u8;
        }
        self.transpose = 0;
        out.changed = wrote;
    }
    /// Apply the result's clock request once on the audio thread. Existing
    /// `Sequencer::tick`, `tick_track`, `clock` and event APIs remain unchanged.
    pub fn apply_transport(&self, sequencer: &mut Sequencer, output: EditOutput) {
        match output.transport {
            TransportCommand::Start => sequencer.clock.start(),
            TransportCommand::Stop => sequencer.clock.stop(),
            TransportCommand::Reset => {
                sequencer.clock.start();
                if !self.running {
                    sequencer.clock.stop();
                }
            }
            TransportCommand::None => {}
        }
    }
    /// Resolve all logical LEDs. Call [`Led::lit`] with a common blink phase.
    /// VERIFY: undocumented recall/last-step lamp details use stable deterministic indications.
    pub fn leds(&self, memory: &Memory) -> Leds {
        let mut leds = Leds::default();
        let r = if self.running {
            self.playing
        } else {
            self.selection.pattern
        };
        let p = &memory.bank.patterns[r.index().unwrap_or(0)];
        leds.sections[usize::from(r.section == Section::B)] = Led::On;
        leds.modes[match self.edit {
            EditMode::Normal => 0,
            EditMode::Pitch => 1,
            EditMode::Time => 2,
        }] = Led::On;
        leds.run = if self.running { Led::On } else { Led::Off };
        leds.back = if self.down(Button::Back) {
            Led::On
        } else {
            Led::Off
        };
        leds.step_mode[usize::from(p.triplet)] = Led::On;
        if self.mode == Mode::PatternPlay {
            for n in self.chain.first.pattern..=self.chain.last {
                leds.selectors[usize::from(n)] = Led::On;
            }
        }
        leds.selectors[usize::from(r.pattern.min(7))] = Led::Blink;
        if let Some(q) = self.queued {
            for n in q.first.pattern..=q.last {
                leds.selectors[usize::from(n)] = Led::Blink;
            }
        }
        let cursor = if self.running {
            self.play_step
        } else {
            self.selection.step.min(15)
        };
        leds.steps[usize::from(cursor.min(15))] = Led::On;
        // Playback does not follow the sounding note on the keyboard lamps; they
        // keep showing pattern selection while running.
        if !self.running && (self.edit == EditMode::Pitch || self.down(Button::PitchMode)) {
            let slot = usize::from(
                self.recalled
                    .or(self.displayed)
                    .unwrap_or(self.pitch_cursor)
                    .min(15),
            );
            let s = p.steps[slot];
            let pitch = if self.down(Button::PitchMode)
                && matches!(self.mode, Mode::TrackWrite | Mode::PatternPlay)
            {
                self.transpose.rem_euclid(12) as u8
            } else {
                s.pitch.min(12)
            };
            leds.pitch[usize::from(pitch)] = Led::On;
            for (led, on) in leds.modifiers.iter_mut().zip([
                s.octave == Octave::Down,
                s.octave == Octave::Up,
                s.accent,
                s.slide,
            ]) {
                if on {
                    *led = Led::On;
                }
            }
        } else if !self.running && self.edit == EditMode::Time {
            let t = p.time[usize::from(self.recalled.unwrap_or(self.time_cursor).min(15))];
            leds.modifiers[match t {
                Time::Note => 0,
                Time::Tie => 1,
                Time::Rest => 2,
            }] = Led::On;
        }
        // FUNCTION exposes the 3/4 indication on the shared UP/triplet lamp.
        if self.mode == Mode::PatternWrite && !self.running && self.down(Button::Function) {
            leds.modifiers[1] = if p.triplet { Led::On } else { Led::Off };
        }
        // The eight white-key lamps have two legends but one physical emitter.
        for (i, pitch) in WHITE_PITCHES.iter().enumerate() {
            let led = if !self.running
                && (self.edit != EditMode::Normal || self.down(Button::PitchMode))
            {
                leds.pitch[usize::from(*pitch)]
            } else {
                leds.selectors[i]
            };
            leds.pitch[usize::from(*pitch)] = led;
            leds.selectors[i] = led;
        }
        leds
    }
    /// Render a panel-selected sample, including queued chains, track playback,
    /// track-write audition and stopped pitch preview. Use this instead of `tick`
    /// when the panel owns transport; timestamp host/MIDI events as before.
    /// VERIFY: a host seek restarts the selected chain at the seek beat; it does
    /// not reconstruct the original hardware's unavailable transport history.
    pub fn tick(&mut self, sequencer: &mut Sequencer, memory: &Memory) -> VoiceCommand {
        if self.mode == Mode::TrackPlay {
            sequencer.set_transpose(0);
            let command = sequencer.tick_track_from(
                memory,
                usize::from(self.selection.track),
                usize::from(self.track_start),
            );
            if let Some((measure, step)) = sequencer.track_cursor() {
                self.selection.measure = measure as u8;
                self.playing =
                    memory.tracks[usize::from(self.selection.track)].entries[measure].pattern;
                self.play_step = step.min(15) as u8;
            }
            return command;
        }
        let triplet = memory.bank.patterns[self.playing.index().unwrap_or(0)].triplet;
        sequencer.clock.set_triplet(triplet);
        let mut event = sequencer.clock.tick();
        if self.clock_switch {
            event.resync = false;
            self.clock_switch = false;
        }
        if !event.running {
            let live = sequencer.tick_at(&memory.bank.patterns[self.pattern_index()], event);
            let preview = self.preview;
            self.preview.trigger = false;
            return if preview.gate || preview.trigger {
                preview
            } else {
                live
            };
        }
        // MIDI phase estimates gate release but cannot invent a received pulse.
        let phase = if sequencer.clock.source() == ClockSource::Midi {
            event.phase.min(1.0 - 1.0e-6)
        } else {
            event.phase
        };
        let beat = (event.index as f64 + phase) / if triplet { 3.0 } else { 4.0 };
        if self.restart || event.resync {
            self.origin = beat;
            self.playing = if matches!(self.mode, Mode::PatternPlay | Mode::TrackWrite) {
                self.chain.first
            } else {
                self.selection.pattern
            };
            self.last_position = None;
            self.rebuild_chain();
            self.restart = false;
        }
        sequencer.set_transpose(self.transpose);
        // At most one old-pattern boundary is processed. Large forward seeks
        // have resync set by the clock and restart above, so no catch-up loop.
        let old = &memory.bank.patterns[self.playing.index().unwrap_or(0)];
        let duration = f64::from(old.length.clamp(1, 16)) / if old.triplet { 3.0 } else { 4.0 };
        if beat - self.origin + 1.0e-9 >= duration {
            self.origin += duration;
            self.last_position = None;
            if matches!(self.mode, Mode::PatternPlay | Mode::TrackWrite) {
                if self.playing.pattern >= self.chain.last && self.queued.is_some() {
                    let q = self.queued.take().unwrap_or(self.chain);
                    // Keep the old series as history for the first new pattern's
                    // incoming slide/ties, then discard it at the next boundary.
                    if self.chain_offset != 0 {
                        self.rebuild_chain();
                    }
                    let offset = self.playback_chain.length;
                    let count = q.last - q.first.pattern + 1;
                    for i in 0..count {
                        self.playback_chain.entries[usize::from(offset + i)] = TrackEntry {
                            pattern: PatternRef {
                                pattern: q.first.pattern + i,
                                ..q.first
                            },
                            transpose: 0,
                        };
                    }
                    self.playback_chain.length = offset + count;
                    if count == 1 {
                        self.playback_chain.entries[usize::from(offset + 1)] =
                            self.playback_chain.entries[usize::from(offset)];
                        self.playback_chain.length += 1;
                    }
                    self.chain_offset = offset;
                    self.chain = q;
                    self.playing = q.first;
                } else {
                    if self.chain_offset != 0 {
                        self.rebuild_chain();
                    }
                    self.playing.pattern = if self.playing.pattern >= self.chain.last {
                        self.chain.first.pattern
                    } else {
                        self.playing.pattern + 1
                    };
                }
            }
        }
        let p = &memory.bank.patterns[self.playing.index().unwrap_or(0)];
        if p.triplet != triplet {
            self.clock_switch = true;
        }
        let position = ((beat - self.origin).max(0.0) * if p.triplet { 3.0 } else { 4.0 }).max(0.0);
        let step = (position + 1.0e-9) as i64;
        self.play_step = step.rem_euclid(i64::from(p.length.clamp(1, 16))) as u8;
        let key = (self.playing.index().unwrap_or(0), step);
        let edge = self.last_position != Some(key);
        self.last_position = Some(key);
        let local_event = StepEvent {
            index: step,
            phase: (position - step as f64).max(0.0),
            edge,
            ..event
        };
        if matches!(self.mode, Mode::PatternPlay | Mode::TrackWrite) {
            sequencer.tick_chain_at(
                memory,
                &self.playback_chain,
                usize::from(self.chain_offset + self.playing.pattern - self.chain.first.pattern),
                local_event,
            )
        } else {
            sequencer.tick_at(p, local_event)
        }
    }
}
