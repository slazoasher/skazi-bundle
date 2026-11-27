//! Control-thread data model and strict, canonical binary persistence.

/// Pitch octave relative to the lowest C supported by the voice.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Octave {
    /// Lowest octave, semitone offset zero.
    Down,
    /// Middle octave, semitone offset twelve.
    #[default]
    Mid,
    /// Upper octave, semitone offset twenty-four.
    Up,
}
/// Pitch-mode entry. Time-mode entries are stored separately in [`Pattern::time`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Step {
    /// C through the C above it (0..=12); validation rejects other values.
    pub pitch: u8,
    /// Octave switch setting.
    pub octave: Octave,
    /// Accent switch setting.
    pub accent: bool,
    /// Connect this note to the next note without a fresh attack.
    pub slide: bool,
}
impl Default for Step {
    fn default() -> Self {
        Self {
            pitch: 0,
            octave: Octave::Mid,
            accent: false,
            slide: false,
        }
    }
}
/// Independent time-mode entry.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Time {
    /// Consume a pitch entry and sound a note.
    #[default]
    Note,
    /// Sustain the preceding note without consuming a pitch entry.
    Tie,
    /// Silence for this interval without consuming a pitch entry.
    Rest,
}
/// Sixteen pitch entries and sixteen independent rhythm entries.
/// Only `Note` entries advance the pitch cursor; `length` counts time entries.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Pattern {
    /// Pitch-mode storage, independent of rhythm position.
    pub steps: [Step; 16],
    /// Time-mode storage.
    pub time: [Time; 16],
    /// Number of active time entries, 1..=16.
    pub length: u8,
    /// Three subdivisions per quarter (normally twelve entries per 4/4 bar).
    pub triplet: bool,
}
impl Default for Pattern {
    fn default() -> Self {
        Self {
            steps: [Step::default(); 16],
            time: [Time::Note; 16],
            length: 16,
            triplet: false,
        }
    }
}
/// A section inside one of four pattern groups.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Section {
    /// First eight patterns.
    #[default]
    A,
    /// Second eight patterns.
    B,
}
/// One of the 64 pattern memories.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct PatternRef {
    /// Zero-based group, 0..4.
    pub group: u8,
    /// A or B.
    pub section: Section,
    /// Zero-based pattern, 0..8.
    pub pattern: u8,
}
impl PatternRef {
    /// Return the flat memory index, rejecting invalid selectors.
    pub fn index(self) -> Result<usize, DataError> {
        if self.group >= 4 || self.pattern >= 8 {
            return Err(DataError::InvalidValue);
        }
        Ok(usize::from(self.group) * 16
            + if self.section == Section::B { 8 } else { 0 }
            + usize::from(self.pattern))
    }
    fn byte(self) -> Result<u8, DataError> {
        Ok(self.index()? as u8)
    }
    fn from_byte(b: u8) -> Result<Self, DataError> {
        if b >= 64 {
            return Err(DataError::InvalidValue);
        }
        Ok(Self {
            group: b / 16,
            section: if b & 8 == 0 { Section::A } else { Section::B },
            pattern: b & 7,
        })
    }
}
/// Four groups × two sections × eight patterns, flattened by [`PatternRef::index`].
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Bank {
    /// Fixed pattern memories.
    pub patterns: [Pattern; 64],
}
impl Default for Bank {
    fn default() -> Self {
        Self {
            patterns: [Pattern::default(); 64],
        }
    }
}
/// A track measure, including the original track-mode key shift.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct TrackEntry {
    /// Pattern to play for this measure.
    pub pattern: PatternRef,
    /// Signed semitone shift; supported storage range -12..=12.
    /// VERIFY: exact original key-shift limits/encoding; signed shifts are an API extension.
    pub transpose: i8,
}
/// Baseline original track capacity (extended shared-memory tracks are not modelled).
pub const TRACK_CAPACITY: usize = 64;
/// One pattern chain. An empty track is valid and produces silence.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Track {
    /// Fixed measure storage.
    pub entries: [TrackEntry; TRACK_CAPACITY],
    /// Active measure count, 0..=64.
    pub length: u8,
}
impl Default for Track {
    fn default() -> Self {
        Self {
            entries: [TrackEntry::default(); TRACK_CAPACITY],
            length: 0,
        }
    }
}
/// Complete instrument sequencer memory (seven tracks and one pattern bank).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Memory {
    /// Pattern memories.
    pub bank: Bank,
    /// Seven track chains.
    pub tracks: [Track; 7],
}
/// Binary codec/validation failure; decode never accepts partial or trailing data.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DataError {
    /// Output storage too short or truncated input.
    BufferTooSmall,
    /// Incorrect four-byte type magic.
    BadMagic,
    /// Unsupported schema version.
    UnsupportedVersion,
    /// Invalid length, pitch, reference, transpose or reserved bits.
    InvalidValue,
    /// Noncanonical bytes after the expected record.
    TrailingData,
}
struct Writer<'a> {
    bytes: &'a mut [u8],
    pos: usize,
}
impl Writer<'_> {
    fn put(&mut self, b: u8) -> Result<(), DataError> {
        *self
            .bytes
            .get_mut(self.pos)
            .ok_or(DataError::BufferTooSmall)? = b;
        self.pos += 1;
        Ok(())
    }
}
struct Reader<'a> {
    bytes: &'a [u8],
    pos: usize,
}
impl Reader<'_> {
    fn get(&mut self) -> Result<u8, DataError> {
        let b = *self.bytes.get(self.pos).ok_or(DataError::BufferTooSmall)?;
        self.pos += 1;
        Ok(b)
    }
}
trait Record: Sized {
    const MAGIC: [u8; 4];
    const SIZE: usize;
    fn write(&self, w: &mut Writer<'_>) -> Result<(), DataError>;
    fn read(r: &mut Reader<'_>) -> Result<Self, DataError>;
}
macro_rules! codec {
    ($t:ty) => {
        impl $t {
            /// Exact version-one record size, including magic and version.
            pub const ENCODED_SIZE: usize = <Self as Record>::SIZE + 5;
            /// Encode into caller-owned storage; returns bytes written.
            /// On error the output prefix may have been overwritten.
            pub fn encode(&self, output: &mut [u8]) -> Result<usize, DataError> {
                let mut w = Writer {
                    bytes: output,
                    pos: 0,
                };
                for b in <Self as Record>::MAGIC {
                    w.put(b)?;
                }
                w.put(1)?;
                self.write(&mut w)?;
                Ok(w.pos)
            }
            /// Decode and validate every stored entry, including unused entries.
            pub fn decode(input: &[u8]) -> Result<Self, DataError> {
                let mut r = Reader {
                    bytes: input,
                    pos: 0,
                };
                for b in <Self as Record>::MAGIC {
                    if r.get()? != b {
                        return Err(DataError::BadMagic);
                    }
                }
                if r.get()? != 1 {
                    return Err(DataError::UnsupportedVersion);
                }
                let result = Self::read(&mut r)?;
                if r.pos != input.len() {
                    return Err(DataError::TrailingData);
                }
                Ok(result)
            }
            /// Validate all fields without allocating or serialising.
            pub fn validate(&self) -> Result<(), DataError> {
                let mut storage = [0u8; Self::ENCODED_SIZE];
                self.encode(&mut storage).map(|_| ())
            }
        }
    };
}
impl Record for Pattern {
    const MAGIC: [u8; 4] = *b"D3PT";
    const SIZE: usize = 34;
    fn write(&self, w: &mut Writer<'_>) -> Result<(), DataError> {
        if !(1..=16).contains(&self.length) {
            return Err(DataError::InvalidValue);
        }
        w.put(self.length)?;
        w.put(u8::from(self.triplet))?;
        for (s, t) in self.steps.iter().zip(self.time) {
            if s.pitch > 12 {
                return Err(DataError::InvalidValue);
            }
            let octave = match s.octave {
                Octave::Down => 0,
                Octave::Mid => 1,
                Octave::Up => 2,
            };
            w.put(s.pitch | octave << 4 | u8::from(s.accent) << 6 | u8::from(s.slide) << 7)?;
            w.put(match t {
                Time::Note => 0,
                Time::Tie => 1,
                Time::Rest => 2,
            })?;
        }
        Ok(())
    }
    fn read(r: &mut Reader<'_>) -> Result<Self, DataError> {
        let length = r.get()?;
        let triplet = r.get()?;
        if !(1..=16).contains(&length) || triplet > 1 {
            return Err(DataError::InvalidValue);
        }
        let mut p = Self {
            length,
            triplet: triplet != 0,
            ..Self::default()
        };
        for (s, t) in p.steps.iter_mut().zip(p.time.iter_mut()) {
            let b = r.get()?;
            if b & 15 > 12 {
                return Err(DataError::InvalidValue);
            }
            *s = Step {
                pitch: b & 15,
                octave: match (b >> 4) & 3 {
                    0 => Octave::Down,
                    1 => Octave::Mid,
                    2 => Octave::Up,
                    _ => return Err(DataError::InvalidValue),
                },
                accent: b & 64 != 0,
                slide: b & 128 != 0,
            };
            *t = match r.get()? {
                0 => Time::Note,
                1 => Time::Tie,
                2 => Time::Rest,
                _ => return Err(DataError::InvalidValue),
            };
        }
        Ok(p)
    }
}
impl Record for Bank {
    const MAGIC: [u8; 4] = *b"D3BK";
    const SIZE: usize = 64 * Pattern::SIZE;
    fn write(&self, w: &mut Writer<'_>) -> Result<(), DataError> {
        for p in &self.patterns {
            p.write(w)?;
        }
        Ok(())
    }
    fn read(r: &mut Reader<'_>) -> Result<Self, DataError> {
        let mut b = Self::default();
        for p in &mut b.patterns {
            *p = Pattern::read(r)?;
        }
        Ok(b)
    }
}
impl Record for Track {
    const MAGIC: [u8; 4] = *b"D3TR";
    const SIZE: usize = 129;
    fn write(&self, w: &mut Writer<'_>) -> Result<(), DataError> {
        if usize::from(self.length) > TRACK_CAPACITY {
            return Err(DataError::InvalidValue);
        }
        w.put(self.length)?;
        for e in self.entries {
            if !(-12..=12).contains(&e.transpose) {
                return Err(DataError::InvalidValue);
            }
            w.put(e.pattern.byte()?)?;
            w.put((i16::from(e.transpose) + 12) as u8)?;
        }
        Ok(())
    }
    fn read(r: &mut Reader<'_>) -> Result<Self, DataError> {
        let length = r.get()?;
        if usize::from(length) > TRACK_CAPACITY {
            return Err(DataError::InvalidValue);
        }
        let mut t = Self {
            length,
            ..Self::default()
        };
        for e in &mut t.entries {
            e.pattern = PatternRef::from_byte(r.get()?)?;
            let b = r.get()?;
            if b > 24 {
                return Err(DataError::InvalidValue);
            }
            e.transpose = b as i8 - 12;
        }
        Ok(t)
    }
}
impl Record for Memory {
    const MAGIC: [u8; 4] = *b"D303";
    const SIZE: usize = Bank::SIZE + 7 * Track::SIZE;
    fn write(&self, w: &mut Writer<'_>) -> Result<(), DataError> {
        self.bank.write(w)?;
        for t in &self.tracks {
            t.write(w)?;
        }
        Ok(())
    }
    fn read(r: &mut Reader<'_>) -> Result<Self, DataError> {
        let mut m = Self {
            bank: Bank::read(r)?,
            ..Self::default()
        };
        for t in &mut m.tracks {
            *t = Track::read(r)?;
        }
        Ok(m)
    }
}
codec!(Pattern);
codec!(Bank);
codec!(Track);
codec!(Memory);
