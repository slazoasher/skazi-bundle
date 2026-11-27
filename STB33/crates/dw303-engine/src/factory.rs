//! Factory pattern memory: original acid bass lines written in the classic
//! TB-303 idioms (driving 16ths, octave jumps, accent/slide grooves, minor and
//! Phrygian lines, tie/rest rhythms, triplets) and, in group III, in broader
//! 1990s styles (braindance, filtered house, hypnotic acid, big beat). All are
//! original material, not transcriptions of recorded riffs, and none is named
//! after an artist or track.
//!
//! Notation, one token per time step:
//! - note: `C C# D D# E F F# G G# A A# B C'` (C' is the upper C of the
//!   octave), optionally prefixed `^` (octave up) or `v` (octave down) and
//!   suffixed `!` (accent) and/or `~` (slide into the next note)
//! - `_` tie (sustain the previous note), `.` rest
//! - a leading `T` makes a triplet-time pattern
use dw303_sequencer::{Memory, Octave, Pattern, PatternRef, Section, Step, Time, TrackEntry};

/// Factory patterns: (group 0..4, section, slot 0..8, name, notation).
/// Group I-A slot 1 holds the engine's demo pattern.
pub const PATTERNS: &[(u8, Section, u8, &str, &str)] = &[
    // Group I, section A: driving minor-key lines.
    (
        0,
        Section::A,
        1,
        "Octave Pump",
        "C ^C C C! ^C C C ^C~ C C ^C C! D# ^C C F~",
    ),
    (
        0,
        Section::A,
        2,
        "Minor Walk",
        "A ^A G A C~ D A! . A ^A A! G~ E D C~ A",
    ),
    (
        0,
        Section::A,
        3,
        "Root Hammer",
        "E! E E ^E E E! G E E! E ^E E D E! E ^E~",
    ),
    (
        0,
        Section::A,
        4,
        "Rolling Fifths",
        "C G ^C G C! G ^C~ ^D# C G ^C G C! G A# G",
    ),
    (
        0,
        Section::A,
        5,
        "Low Rider",
        "vA vA A! vA vA C vA vA! vA A vA G~ vA vA E! vA",
    ),
    (
        0,
        Section::A,
        6,
        "Climber",
        "C D D# F G! G# A# ^C! ^C A# G# G F! D# D C~",
    ),
    (
        0,
        Section::A,
        7,
        "Stab Line",
        "C! . C . ^C! . C . D#! . C . ^C! . A#~ G",
    ),
    // Group I, section B: accent and slide grooves.
    (
        0,
        Section::B,
        0,
        "Squelch One",
        "C C~ ^C~ C C! D#~ F C C ^C! C~ D# C ^C~ A# G!",
    ),
    (
        0,
        Section::B,
        1,
        "Glide Path",
        "A~ C~ E~ ^A! A~ G~ E~ C A~ C~ E! ^A~ G E~ D C",
    ),
    (
        0,
        Section::B,
        2,
        "Accent Chase",
        "D! D D! D ^D! D D! C D! D ^D! D F! D C! A",
    ),
    (
        0,
        Section::B,
        3,
        "Rubber Band",
        "F~ ^F F~ ^F! F F~ ^F! D# F~ ^F F! D#~ C D# F~ ^F!",
    ),
    (
        0,
        Section::B,
        4,
        "Wobble Root",
        "G G~ G! G ^G~ G G! F G G~ G! A# ^G~ G F! D",
    ),
    (
        0,
        Section::B,
        5,
        "Sliding Doors",
        "C~ D#~ G~ ^C! A#~ G~ D#~ C C~ F~ G#~ ^C! A#~ G~ F D#!",
    ),
    (
        0,
        Section::B,
        6,
        "Syncopated Squeeze",
        "E . E! ^E . E E! . E ^E! . E G! . E D!",
    ),
    (
        0,
        Section::B,
        7,
        "Acid Bounce",
        "A ^A! A ^A A! ^A A ^A~ ^C! A ^A A! ^A A G! ^A",
    ),
    // Group II, section A: dark and Phrygian lines.
    (
        1,
        Section::A,
        0,
        "Phrygian Dark",
        "E F E E! F~ E G E E! F E ^E! E D E F!",
    ),
    (
        1,
        Section::A,
        1,
        "Half Step",
        "C C# C C! C# C ^C C C# C! C ^C~ C# C C! A#",
    ),
    (
        1,
        Section::A,
        2,
        "Tritone Grind",
        "C F# C! C F#~ ^C C F# C! C F# ^C~ F# C! F# C",
    ),
    (
        1,
        Section::A,
        3,
        "Deep Pulse",
        "vD vD! vD D vD vD! F vD vD vD! D vD C vD! vD A",
    ),
    (
        1,
        Section::A,
        4,
        "Minor Cascade",
        "^C A# G# G! F D# D C! ^C A# G# G F! D# D C~",
    ),
    (
        1,
        Section::A,
        5,
        "Night Drive",
        "A A A! G A A C! A A A! E G A! A C ^A~",
    ),
    (
        1,
        Section::A,
        6,
        "Octave Spiral",
        "vC C ^C C vC! C ^C~ ^D# vC C ^C! C vC C ^C G!",
    ),
    (
        1,
        Section::A,
        7,
        "Cold Steel",
        "D# D# ^D#! D# F# D# D#! G# D# ^D# D#! C# D# F#~ G# D#!",
    ),
    // Group II, section B: ties, rests and triplets.
    (
        1,
        Section::B,
        0,
        "Tied Groove",
        "C _ C! . ^C _ C . D#! _ C . ^C~ _ C! .",
    ),
    (
        1,
        Section::B,
        1,
        "Long Slide",
        "A~ _ ^A _ G~ _ E . A~ _ ^C! _ A# _ G .",
    ),
    (
        1,
        Section::B,
        2,
        "Gate Play",
        "C . C . C! _ . C ^C . C . G! _ . C",
    ),
    (
        1,
        Section::B,
        3,
        "Broken Beat",
        "E! . . E ^E . E! . . E D . E! . G .",
    ),
    (
        1,
        Section::B,
        4,
        "Triplet Run",
        "T C ^C G C! ^C G C ^C~ D# C! ^C G",
    ),
    (
        1,
        Section::B,
        5,
        "Triplet Squelch",
        "T A~ ^A A! G~ A ^A~ C A! ^A E~ G A!",
    ),
    (
        1,
        Section::B,
        6,
        "Shuffle Dark",
        "T E E! F E ^E~ E E F! E D E ^E!",
    ),
    (
        1,
        Section::B,
        7,
        "Sparse Accent",
        "C! . . . C . ^C! . . C . . D#! . C .",
    ),
    // Group III, section A: restless braindance-style lines (chromatic runs,
    // stop-start rhythms, odd accents).
    (
        2,
        Section::A,
        0,
        "Twitch Logic",
        "C C# ^C! . D# C~ F#! C ^C# . C G#! C~ ^D C# F!",
    ),
    (
        2,
        Section::A,
        1,
        "Clockwork Glitch",
        "E ^E! F ^F . E G#~ A ^E! . E F E~ ^C E! .",
    ),
    (
        2,
        Section::A,
        2,
        "Detuned Toy",
        "G A# ^C# G! . G ^G A#~ ^C! G F# . G! ^A# G ^D#",
    ),
    (
        2,
        Section::A,
        3,
        "Run And Stop",
        "C D D# F# G! G# A# ^C! . . C . ^C! _ . A#",
    ),
    (
        2,
        Section::A,
        4,
        "Prepared Wire",
        "D# ^D# D#! E . D# A ^D#~ D#! . F# D# ^E! D# . A~",
    ),
    (
        2,
        Section::A,
        5,
        "Knotted Line",
        "T C ^C! C# G C~ ^F# C! G# ^C C# F! ^C",
    ),
    (
        2,
        Section::A,
        6,
        "Analogue Bubbles",
        "A ^A! ^A# A E~ F A! ^A ^A# A! C~ ^C A ^A! G# A",
    ),
    (
        2,
        Section::A,
        7,
        "Calculator Dub",
        "F . F! ^F . F C! . F ^F! . D# F . ^C! .",
    ),
    // Group III, section B: filtered-house octave grooves, hypnotic one-note
    // acid builds and big-beat stabs.
    (
        2,
        Section::B,
        0,
        "Filter Funk",
        "G . ^G G! . G ^G~ ^A# G . ^G! G F . ^G~ G!",
    ),
    (
        2,
        Section::B,
        1,
        "Disco Octaves",
        "A ^A A ^A! A ^A A ^A! G ^G G ^G! G ^G G ^G!",
    ),
    (
        2,
        Section::B,
        2,
        "Robot Strut",
        "E . E ^E! . E G A! . A ^A! G . E D E!",
    ),
    (
        2,
        Section::B,
        3,
        "Hypnotic Rise",
        "C C C! C C C~ C C C! C C ^C~ C C! C C",
    ),
    (
        2,
        Section::B,
        4,
        "Endless Pulse",
        "A! A A A! A A A A~ A! A A A! A A ^A~ A",
    ),
    (
        2,
        Section::B,
        5,
        "Breakbeat Stab",
        "E! . . E! . . ^E! . E! . E! . G! . . A!",
    ),
    (
        2,
        Section::B,
        6,
        "Rave Siren",
        "D ^D! D ^D! D# ^D#! D# ^D#! F ^F! F ^F! D# ^D#~ D C!",
    ),
    (
        2,
        Section::B,
        7,
        "Warehouse Hammer",
        "C! C C ^C! C! C D# C! C! C ^C C! F! C D#~ C!",
    ),
];

/// Factory tracks: chains of (group, section, slot) measures with no transpose.
/// A pattern slot: (group 0..4, section, slot 0..8).
pub type Slot = (u8, Section, u8);

pub const TRACKS: &[(&str, &[Slot])] = &[
    (
        "Acid Set One",
        &[
            (0, Section::A, 0),
            (0, Section::A, 0),
            (0, Section::A, 1),
            (0, Section::A, 1),
            (0, Section::B, 0),
            (0, Section::B, 0),
            (0, Section::B, 1),
            (0, Section::A, 7),
        ],
    ),
    (
        "Dark Set",
        &[
            (1, Section::A, 0),
            (1, Section::A, 0),
            (1, Section::A, 2),
            (1, Section::A, 2),
            (1, Section::A, 5),
            (1, Section::A, 5),
            (1, Section::B, 0),
            (1, Section::A, 7),
        ],
    ),
];

/// Parse one pattern in the notation above.
pub fn parse(notation: &str) -> Result<Pattern, String> {
    let mut pattern = Pattern::default();
    let mut tokens = notation.split_whitespace().peekable();
    if tokens.peek() == Some(&"T") {
        pattern.triplet = true;
        tokens.next();
    }
    let mut times = 0usize;
    let mut notes = 0usize;
    for token in tokens {
        if times >= 16 {
            return Err(format!("more than 16 steps in {notation:?}"));
        }
        pattern.time[times] = match token {
            "_" => Time::Tie,
            "." => Time::Rest,
            note => {
                if notes >= 16 {
                    return Err(format!("more than 16 notes in {notation:?}"));
                }
                pattern.steps[notes] = parse_note(note)?;
                notes += 1;
                Time::Note
            }
        };
        times += 1;
    }
    if times == 0 {
        return Err("empty pattern".into());
    }
    pattern.length = times as u8;
    Ok(pattern)
}

fn parse_note(token: &str) -> Result<Step, String> {
    let mut rest = token;
    let octave = if let Some(r) = rest.strip_prefix('^') {
        rest = r;
        Octave::Up
    } else if let Some(r) = rest.strip_prefix('v') {
        rest = r;
        Octave::Down
    } else {
        Octave::Mid
    };
    let mut accent = false;
    let mut slide = false;
    loop {
        if let Some(r) = rest.strip_suffix('!') {
            accent = true;
            rest = r;
        } else if let Some(r) = rest.strip_suffix('~') {
            slide = true;
            rest = r;
        } else {
            break;
        }
    }
    const NAMES: [&str; 13] = [
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C'",
    ];
    let pitch = NAMES
        .iter()
        .position(|name| *name == rest)
        .ok_or_else(|| format!("unknown note {token:?}"))? as u8;
    Ok(Step {
        pitch,
        octave,
        accent,
        slide,
    })
}

/// Fill `memory` with the factory patterns and tracks (empty slots unchanged).
pub fn install(memory: &mut Memory) {
    for &(group, section, slot, _, notation) in PATTERNS {
        let index = PatternRef {
            group,
            section,
            pattern: slot,
        }
        .index()
        .expect("valid factory slot");
        memory.bank.patterns[index] = parse(notation).expect("valid factory pattern");
    }
    for (track, (_, measures)) in memory.tracks.iter_mut().zip(TRACKS) {
        for (entry, &(group, section, pattern)) in track.entries.iter_mut().zip(*measures) {
            *entry = TrackEntry {
                pattern: PatternRef {
                    group,
                    section,
                    pattern,
                },
                transpose: 0,
            };
        }
        track.length = measures.len() as u8;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_factory_pattern_parses_with_up_to_16_steps() {
        for &(_, _, _, name, notation) in PATTERNS {
            let pattern = parse(notation).unwrap_or_else(|e| panic!("{name}: {e}"));
            assert!((1..=16).contains(&pattern.length), "{name}");
            if pattern.triplet {
                assert!(
                    pattern.length <= 12,
                    "{name}: triplet patterns use 12 steps"
                );
            } else {
                assert_eq!(pattern.length, 16, "{name}: straight patterns fill a bar");
            }
        }
    }

    #[test]
    fn factory_slots_are_unique_and_leave_the_demo_slot() {
        let mut seen = std::collections::HashSet::new();
        for &(group, section, slot, name, _) in PATTERNS {
            let index = PatternRef {
                group,
                section,
                pattern: slot,
            }
            .index()
            .unwrap();
            assert!(index != 0, "{name} would overwrite the demo pattern");
            assert!(seen.insert(index), "{name} duplicates a slot");
        }
    }

    #[test]
    fn notation_maps_pitch_octave_accent_slide_tie_and_rest() {
        let p = parse("^C'!~ vD# _ .").unwrap();
        assert_eq!(p.length, 4);
        assert_eq!(
            p.steps[0],
            Step {
                pitch: 12,
                octave: Octave::Up,
                accent: true,
                slide: true
            }
        );
        assert_eq!(p.steps[1].pitch, 3);
        assert_eq!(p.steps[1].octave, Octave::Down);
        assert_eq!(
            &p.time[..4],
            &[Time::Note, Time::Note, Time::Tie, Time::Rest]
        );
        assert!(parse("H").is_err());
    }

    #[test]
    fn installed_memory_round_trips_through_the_codec() {
        let mut memory = Memory::default();
        install(&mut memory);
        let mut bytes = vec![0; Memory::ENCODED_SIZE];
        memory.encode(&mut bytes).unwrap();
        assert_eq!(Memory::decode(&bytes).unwrap(), memory);
        assert_eq!(memory.tracks[0].length, 8);
    }
}
