use dw303_sequencer::*;
use ravet_foundation::{EventData, MidiMessage, Transport};

fn midi(byte: u8) -> MidiMessage {
    MidiMessage::from_bytes(&[byte]).unwrap()
}
fn event(index: i64, phase: f64) -> StepEvent {
    StepEvent {
        running: true,
        edge: phase == 0.0,
        resync: false,
        index,
        phase,
    }
}

fn edges(block: usize, bpm: f64, host: bool, triplet: bool) -> Vec<usize> {
    let mut clock = StepClock::new();
    clock.prepare(48_000.0).unwrap();
    clock.set_tempo(bpm);
    clock.set_triplet(triplet);
    if host {
        clock.set_source(ClockSource::Host);
    } else {
        clock.start();
    }
    let mut result = Vec::new();
    let frames = 300_000;
    for start in (0..frames).step_by(block) {
        if host {
            clock.begin_host_block(Transport {
                playing: Some(true),
                beat_position: Some(start as f64 * bpm / (60.0 * 48_000.0)),
                tempo: Some(bpm),
                ..Transport::default()
            });
        }
        for frame in start..(start + block).min(frames) {
            let e = clock.tick();
            if e.edge {
                result.push(frame);
            }
        }
    }
    result
}
#[test]
fn sample_edges_do_not_depend_on_block_partition() {
    for bpm in [73.0, 120.0, 137.5, 241.0] {
        for triplet in [false, true] {
            let reference = edges(1, bpm, false, triplet);
            for block in [1, 7, 64, 512] {
                assert_eq!(edges(block, bpm, false, triplet), reference);
                assert_eq!(edges(block, bpm, true, triplet), reference);
            }
            let divisions = if triplet { 3.0 } else { 4.0 };
            for (i, &frame) in reference.iter().enumerate() {
                assert_eq!(
                    frame,
                    (i as f64 * 60.0 * 48_000.0 / (bpm * divisions) - 1e-7)
                        .max(0.0)
                        .ceil() as usize
                );
            }
        }
    }
}
#[test]
fn host_mid_step_start_loop_seek_stop_and_tempo_change() {
    let mut s = Sequencer::default();
    s.clock.set_source(ClockSource::Host);
    let p = Pattern::default();
    s.clock.begin_host_block(Transport {
        playing: Some(true),
        beat_position: Some(0.1),
        tempo: Some(120.0),
        ..Transport::default()
    });
    let c = s.tick(&p);
    assert!(c.gate);
    assert!(!c.trigger);
    for _ in 1..3600 {
        assert!(!s.tick(&p).trigger);
    }
    assert!(s.tick(&p).trigger); // next quarter-beat edge at sample 3600
    s.clock.begin_host_block(Transport {
        beat_position: Some(2.0),
        discontinuity: true,
        ..Transport::default()
    });
    assert!(s.tick(&p).trigger);
    s.clock.begin_host_block(Transport {
        beat_position: Some(-0.1),
        discontinuity: true,
        tempo: Some(90.0),
        ..Transport::default()
    });
    let e = s.clock.tick();
    assert!(e.resync);
    assert_eq!(e.index, -1);
    assert!((e.phase - 0.6).abs() < 1e-10);
    s.clock.begin_host_block(Transport {
        playing: Some(false),
        ..Transport::default()
    });
    assert!(!s.tick(&p).gate);
}
#[test]
fn continuous_host_tempo_change_keeps_phase_without_retrigger() {
    let mut c = StepClock::new();
    c.set_source(ClockSource::Host);
    c.begin_host_block(Transport {
        playing: Some(true),
        beat_position: Some(0.0),
        tempo: Some(120.0),
        ..Transport::default()
    });
    for _ in 0..3000 {
        c.tick();
    }
    c.begin_host_block(Transport {
        beat_position: Some(0.125),
        tempo: Some(240.0),
        ..Transport::default()
    });
    let e = c.tick();
    assert!(!e.edge);
    assert!(!e.resync);
    assert_eq!(e.phase, 0.5);
    for _ in 1..1500 {
        assert!(!c.tick().edge);
    }
    assert!(c.tick().edge);
}
#[test]
fn midi_clock_edges_start_stop_continue_and_triplets() {
    for (triplet, pulses) in [(false, 6), (true, 8)] {
        let mut c = StepClock::new();
        c.set_source(ClockSource::Midi);
        c.set_triplet(triplet);
        c.midi(midi(0xfa));
        assert!(!c.tick().running);
        let mut edges = Vec::new();
        for frame in 0..20_000 {
            if frame % 1000 == 0 {
                c.midi(midi(0xf8));
            }
            let e = c.tick();
            if e.edge {
                edges.push(frame);
            }
        }
        assert_eq!(
            edges,
            (0..20_000).step_by(pulses * 1000).collect::<Vec<_>>()
        );
        c.midi(midi(0xfc));
        assert!(!c.tick().running);
        c.midi(midi(0xfb));
        assert!(c.tick().running);
        c.midi(midi(0xfa));
        c.midi(midi(0xf8));
        let e = c.tick();
        assert_eq!(e.index, 0);
        assert!(e.edge);
    }
}
#[test]
fn midi_gate_release_is_on_third_clock_for_straight_notes() {
    let mut s = Sequencer::default();
    s.clock.set_source(ClockSource::Midi);
    s.clock.midi(midi(0xfa));
    let p = Pattern::default();
    for frame in 0..6000 {
        if frame % 1000 == 0 {
            s.clock.midi(midi(0xf8));
        }
        assert_eq!(s.tick(&p).gate, frame < 3000);
    }
}
#[test]
fn nominal_gate_length_scales_with_sample_rate() {
    for rate in [48_000.0, 96_000.0, 192_000.0] {
        let mut s = Sequencer::default();
        s.prepare(rate).unwrap();
        s.clock.start();
        let length = (rate / 8.0) as usize;
        for sample in 0..length {
            let c = s.tick(&Pattern::default());
            assert_eq!(c.gate, sample < length / 2);
            assert_eq!(c.trigger, sample == 0);
        }
    }
}
#[test]
fn slide_tie_rest_and_independent_pitch_cursor() {
    let mut p = Pattern {
        length: 5,
        ..Pattern::default()
    };
    p.time[..5].copy_from_slice(&[Time::Note, Time::Tie, Time::Note, Time::Rest, Time::Note]);
    p.steps[0].pitch = 1;
    p.steps[0].accent = true;
    p.steps[0].slide = true;
    p.steps[1].pitch = 7;
    p.steps[2].pitch = 12;
    p.steps[2].octave = Octave::Up;
    let mut s = Sequencer::default();
    let c = s.tick_at(&p, event(0, 0.0));
    assert!(c.trigger); // previous wrapped note isn't slid
    assert!(s.tick_at(&p, event(0, 0.9)).gate);
    let c = s.tick_at(&p, event(1, 0.0));
    assert_eq!(c.note, 13);
    assert!(c.accent);
    assert!(!c.trigger);
    assert!(c.gate);
    let c = s.tick_at(&p, event(2, 0.0));
    assert_eq!(c.note, 19);
    assert!(c.slide);
    assert!(!c.trigger);
    assert!(!c.accent);
    let c = s.tick_at(&p, event(3, 0.0));
    assert!(!c.gate);
    assert!(!c.trigger);
    let c = s.tick_at(&p, event(4, 0.0));
    assert_eq!(c.note, 36);
    assert!(c.trigger);
    assert!(!c.slide);
    s.set_transpose(-24);
    assert_eq!(s.tick_at(&p, event(4, 0.0)).note, 12);
}
#[test]
fn ties_extend_gate_and_final_tie_releases() {
    let mut p = Pattern {
        length: 3,
        ..Pattern::default()
    };
    p.time[1] = Time::Tie;
    p.time[2] = Time::Rest;
    let mut s = Sequencer::default();
    assert!(s.tick_at(&p, event(0, 0.99)).gate);
    assert!(s.tick_at(&p, event(1, 0.49)).gate);
    assert!(!s.tick_at(&p, event(1, 0.5)).gate);
    p.time = [Time::Tie; 16];
    assert!(!s.tick_at(&p, event(1, 0.0)).gate);
}
#[test]
fn mono_last_note_priority_accent_legato_and_channels() {
    let mut m = MonoNoteInput::default();
    m.note_on(0, 40, 100);
    let c = m.tick();
    assert!(c.trigger && c.gate && c.accent);
    assert!(!m.tick().trigger);
    m.note_on(1, 44, 99);
    let c = m.tick();
    assert_eq!(c.note, 44);
    assert!(c.slide);
    assert!(!c.trigger && !c.accent);
    m.note_off(0, 44);
    assert_eq!(m.tick().note, 44);
    m.note_off(1, 44);
    let c = m.tick();
    assert_eq!(c.note, 40);
    assert!(c.slide && c.accent);
    assert!(!c.trigger);
    m.note_off(0, 40);
    assert!(!m.tick().gate);
    m.midi(MidiMessage::from_bytes(&[0x90, 60, 127]).unwrap());
    assert!(m.tick().trigger);
    m.midi(MidiMessage::from_bytes(&[0x90, 60, 0]).unwrap());
    assert!(!m.tick().gate);
}
#[test]
fn stopped_sequencer_plays_foundation_note_events() {
    let mut s = Sequencer::default();
    s.event(EventData::NoteOn {
        channel: 0,
        key: 50,
        velocity: 0.8,
    });
    let c = s.tick(&Pattern::default());
    assert_eq!(c.note, 50);
    assert!(c.trigger && c.accent);
    s.event(EventData::NoteOff {
        channel: 0,
        key: 50,
        velocity: 0.0,
    });
    assert!(!s.tick(&Pattern::default()).gate);
}
#[test]
fn mixed_triplet_track_edges_and_transpose() {
    let mut m = Memory::default();
    m.tracks[0].length = 2;
    m.tracks[0].entries[1].pattern.pattern = 1;
    m.tracks[0].entries[1].transpose = 3;
    m.bank.patterns[0].length = 2;
    m.bank.patterns[1].length = 3;
    m.bank.patterns[1].triplet = true;
    let mut s = Sequencer::default();
    s.clock.start();
    let mut attacks = Vec::new();
    for frame in 0..36_001 {
        let c = s.tick_track(&m, 0);
        if c.trigger {
            attacks.push((frame, c.note));
        }
    }
    assert_eq!(
        attacks,
        [
            (0, 12),
            (6000, 12),
            (12000, 15),
            (20000, 15),
            (28000, 15),
            (36000, 12)
        ]
    );
}
#[test]
fn codec_round_trips_full_memory_and_individual_records() {
    let mut m = Memory::default();
    for (i, p) in m.bank.patterns.iter_mut().enumerate() {
        p.length = (i % 16 + 1) as u8;
        p.triplet = i % 2 == 0;
        for j in 0..16 {
            p.steps[j] = Step {
                pitch: (j % 13) as u8,
                octave: [Octave::Down, Octave::Mid, Octave::Up][j % 3],
                accent: j % 2 == 0,
                slide: j % 3 == 0,
            };
            p.time[j] = [Time::Note, Time::Tie, Time::Rest][j % 3];
        }
    }
    for (i, t) in m.tracks.iter_mut().enumerate() {
        t.length = 64;
        for (j, e) in t.entries.iter_mut().enumerate() {
            e.pattern = PatternRef {
                group: (j / 16) as u8,
                section: if j % 16 < 8 { Section::A } else { Section::B },
                pattern: (j % 8) as u8,
            };
            e.transpose = i as i8 - 3;
        }
    }
    let mut bytes = [0; Memory::ENCODED_SIZE];
    assert_eq!(m.encode(&mut bytes).unwrap(), bytes.len());
    assert_eq!(Memory::decode(&bytes).unwrap(), m);
    let mut b = [0; Bank::ENCODED_SIZE];
    m.bank.encode(&mut b).unwrap();
    assert_eq!(Bank::decode(&b).unwrap(), m.bank);
    let mut t = [0; Track::ENCODED_SIZE];
    m.tracks[0].encode(&mut t).unwrap();
    assert_eq!(Track::decode(&t).unwrap(), m.tracks[0]);
    let mut p = [0; Pattern::ENCODED_SIZE];
    m.bank.patterns[0].encode(&mut p).unwrap();
    assert_eq!(Pattern::decode(&p).unwrap(), m.bank.patterns[0]);
}
#[test]
fn codec_rejects_corrupt_truncated_trailing_and_invalid_entries() {
    let mut good = [0; Pattern::ENCODED_SIZE];
    Pattern::default().encode(&mut good).unwrap();
    for n in 0..good.len() {
        assert!(Pattern::decode(&good[..n]).is_err());
    }
    for (offset, value) in [
        (0, b'X'),
        (4, 2),
        (5, 0),
        (5, 17),
        (6, 2),
        (7, 15),
        (7, 48),
        (8, 3),
    ] {
        let mut b = good;
        b[offset] = value;
        assert!(Pattern::decode(&b).is_err());
    }
    let mut trailing = good.to_vec();
    trailing.push(0);
    assert_eq!(Pattern::decode(&trailing), Err(DataError::TrailingData));
    let mut t = [0; Track::ENCODED_SIZE];
    Track::default().encode(&mut t).unwrap();
    t[6] = 64;
    assert!(Track::decode(&t).is_err());
    t[6] = 0;
    t[7] = 25;
    assert!(Track::decode(&t).is_err());
    let mut bad = Pattern::default();
    bad.steps[15].pitch = 13;
    assert!(bad.validate().is_err());
    assert!(Pattern::default().encode(&mut [0; 5]).is_err());
}
#[test]
fn invalid_controls_are_bounded_and_counted() {
    let mut c = StepClock::new();
    assert_eq!(c.prepare(f64::NAN), Err(StageError::InvalidSampleRate));
    c.set_tempo(f64::INFINITY);
    c.set_tempo(-1.0);
    assert_eq!(c.invalid_inputs(), 2);
    c.set_source(ClockSource::Host);
    c.begin_host_block(Transport {
        playing: Some(true),
        beat_position: Some(f64::NAN),
        tempo: Some(f64::NAN),
        ..Transport::default()
    });
    assert_eq!(c.invalid_inputs(), 4);
    assert!(c.tick().phase.is_finite());
    let mut s = Sequencer::default();
    let p = Pattern {
        length: 255,
        steps: [Step {
            pitch: 255,
            octave: Octave::Up,
            accent: true,
            slide: false,
        }; 16],
        ..Pattern::default()
    };
    s.set_transpose(127);
    assert_eq!(s.tick_at(&p, event(i64::MIN, 0.0)).note, 127);
}

#[test]
fn track_boundary_slide_tie_and_single_step_repeat() {
    let mut memory = Memory::default();
    memory.tracks[0].length = 2;
    memory.tracks[0].entries[1].pattern.pattern = 1;
    memory.bank.patterns[0].length = 1;
    memory.bank.patterns[0].steps[0].slide = true;
    memory.bank.patterns[1].length = 2;
    memory.bank.patterns[1].time[0] = Time::Tie;
    memory.bank.patterns[1].steps[0].pitch = 7;
    let mut s = Sequencer::default();
    s.clock.start();
    for frame in 0..12001 {
        let c = s.tick_track(&memory, 0);
        if frame == 6000 {
            assert!(c.gate);
            assert_eq!(c.note, 12);
            assert!(!c.trigger);
        }
        if frame == 12000 {
            assert!(c.slide);
            assert_eq!(c.note, 19);
            assert!(!c.trigger);
        }
    }
    memory.tracks[0].length = 1;
    memory.bank.patterns[0].steps[0].slide = false;
    s.reset();
    s.clock.start();
    for frame in 0..12001 {
        let c = s.tick_track(&memory, 0);
        assert_eq!(c.trigger, frame % 6000 == 0);
    }
}
#[test]
fn all_keys_can_be_held_and_repeated_keys_do_not_overflow() {
    let mut notes = MonoNoteInput::default();
    for channel in 0..16 {
        for key in 0..128 {
            notes.note_on(channel, key, 100);
        }
    }
    notes.note_on(0, 0, 127);
    assert_eq!(notes.tick().note, 0);
    notes.note_off(0, 0);
    assert_eq!(notes.tick().note, 127);
    for channel in 0..16 {
        for key in 0..128 {
            notes.note_off(channel, key);
        }
    }
    assert!(!notes.tick().gate);
}
