use dw303_sequencer::edit::*;
use dw303_sequencer::*;

fn press(e: &mut Editor, m: &mut Memory, b: Button) -> EditOutput {
    e.action(m, PanelAction::Press(b)).unwrap()
}
fn release(e: &mut Editor, m: &mut Memory, b: Button) -> EditOutput {
    e.action(m, PanelAction::Release(b)).unwrap()
}
fn tap(e: &mut Editor, m: &mut Memory, b: Button) -> EditOutput {
    let out = press(e, m, b);
    release(e, m, b);
    out
}
fn mode(e: &mut Editor, m: &mut Memory, mode: Mode) {
    e.action(m, PanelAction::Mode(mode)).unwrap();
}
fn clear(e: &mut Editor, m: &mut Memory, n: u8) {
    press(e, m, Button::Number(n));
    assert!(tap(e, m, Button::Clear).changed);
    release(e, m, Button::Number(n));
}
fn length(e: &mut Editor, m: &mut Memory, n: u8) {
    press(e, m, Button::Function);
    for _ in 0..n {
        tap(e, m, Button::Step);
    }
    release(e, m, Button::Function);
}
fn start(e: &mut Editor, m: &mut Memory, s: &mut Sequencer) {
    let out = tap(e, m, Button::RunStop);
    e.apply_transport(s, out);
}

#[test]
fn pitch_changes_reset_flags_but_recall_and_same_pitch_keep_them() {
    let mut e = Editor::new();
    let mut m = Memory::default();
    mode(&mut e, &mut m, Mode::PatternWrite);
    m.bank.patterns[0].steps[0] = Step {
        pitch: 5,
        accent: true,
        slide: true,
        ..Step::default()
    };
    tap(&mut e, &mut m, Button::PitchMode);
    tap(&mut e, &mut m, Button::Pitch(5));
    assert!(m.bank.patterns[0].steps[0].accent && m.bank.patterns[0].steps[0].slide);
    tap(&mut e, &mut m, Button::Back);
    tap(&mut e, &mut m, Button::Pitch(7));
    assert!(!m.bank.patterns[0].steps[0].accent && !m.bank.patterns[0].steps[0].slide);
    // Rewind and keep the pitch with WRITE/NEXT while changing its properties.
    tap(&mut e, &mut m, Button::PitchMode);
    press(&mut e, &mut m, Button::Tap);
    for b in [Button::Down, Button::Accent, Button::Slide] {
        tap(&mut e, &mut m, b);
    }
    release(&mut e, &mut m, Button::Tap);
    let kept = m.bank.patterns[0].steps[0];
    assert_eq!((kept.pitch, kept.octave), (7, Octave::Down));
    assert!(kept.accent && kept.slide);
    // BACK revisits that same pitch; held UP changes octave on entry.
    tap(&mut e, &mut m, Button::Back);
    press(&mut e, &mut m, Button::Up);
    tap(&mut e, &mut m, Button::Pitch(7));
    release(&mut e, &mut m, Button::Up);
    let same = m.bank.patterns[0].steps[0];
    assert_eq!(same.octave, Octave::Up);
    assert!(same.accent && same.slide);
    tap(&mut e, &mut m, Button::Function);
    assert_eq!(e.edit_mode(), EditMode::Normal);
    assert_eq!(m.bank.patterns[0].steps[0], same);
}

#[test]
fn pattern_write_submodes_lock_during_run_and_resume_after_stop() {
    for (button, submode) in [
        (Button::Function, EditMode::Normal),
        (Button::PitchMode, EditMode::Pitch),
        (Button::TimeMode, EditMode::Time),
    ] {
        let mut e = Editor::new();
        let mut m = Memory::default();
        let mut s = Sequencer::default();
        mode(&mut e, &mut m, Mode::PatternWrite);
        tap(&mut e, &mut m, button);
        let before = m.clone();
        start(&mut e, &mut m, &mut s);
        assert_eq!(e.edit_mode(), submode);
        assert_eq!(e.leds(&m).pitch[0], Led::Blink);
        assert_eq!(e.leds(&m).modifiers, [Led::Off; 4]);
        for b in [
            Button::Function,
            Button::PitchMode,
            Button::TimeMode,
            Button::Pitch(7),
            Button::Tap,
        ] {
            assert!(!tap(&mut e, &mut m, b).changed);
            assert_eq!(e.edit_mode(), submode);
        }
        assert_eq!(m, before);
        start(&mut e, &mut m, &mut s); // RUN/STOP stops this run.
        assert!(!e.running());
        assert_eq!(e.edit_mode(), submode);
        tap(&mut e, &mut m, Button::PitchMode);
        assert_eq!(e.edit_mode(), EditMode::Pitch);
        tap(&mut e, &mut m, Button::Function);
        assert_eq!(e.edit_mode(), EditMode::Normal);
    }
}

#[test]
fn function_displays_triplet_information_without_changing_timing() {
    let mut e = Editor::new();
    let mut m = Memory::default();
    mode(&mut e, &mut m, Mode::PatternWrite);
    let before = m.clone();
    press(&mut e, &mut m, Button::Function);
    assert_eq!(e.leds(&m).modifiers[1], Led::Off);
    assert_eq!(e.leds(&m).step_mode, [Led::On, Led::Off]);
    assert_eq!(m, before);
    tap(&mut e, &mut m, Button::Triplet);
    assert_eq!(e.leds(&m).modifiers[1], Led::On);
    assert_eq!(e.leds(&m).step_mode, [Led::Off, Led::On]);
    assert_eq!(m.bank.patterns[0].time, before.bank.patterns[0].time);
    release(&mut e, &mut m, Button::Function);
    assert_eq!(e.leds(&m).modifiers[1], Led::Off);
    length(&mut e, &mut m, 16);
    press(&mut e, &mut m, Button::Function);
    assert_eq!(e.leds(&m).modifiers[1], Led::Off);
    release(&mut e, &mut m, Button::Function);
}

#[test]
fn time_entry_rests_and_orphan_sustains_ignore_pitch_properties() {
    let mut e = Editor::new();
    let mut m = Memory::default();
    let mut s = Sequencer::default();
    mode(&mut e, &mut m, Mode::PatternWrite);
    length(&mut e, &mut m, 5);
    m.bank.patterns[0].steps[0].accent = true;
    m.bank.patterns[0].steps[0].slide = true;
    m.bank.patterns[0].steps[1].pitch = 7;
    let pitches = m.bank.patterns[0].steps;
    tap(&mut e, &mut m, Button::TimeMode);
    for b in [Button::Down, Button::Up, Button::Accent, Button::Up] {
        assert!(tap(&mut e, &mut m, b).changed);
        assert_eq!(e.edit_mode(), EditMode::Time);
    }
    tap(&mut e, &mut m, Button::Down);
    assert_eq!(e.edit_mode(), EditMode::Normal);
    let p = &m.bank.patterns[0];
    assert_eq!(p.steps, pitches);
    assert_eq!(
        &p.time[..5],
        &[Time::Note, Time::Tie, Time::Rest, Time::Tie, Time::Note]
    );
    for index in 0..5 {
        let c = s.tick_at(
            p,
            StepEvent {
                index,
                edge: true,
                running: true,
                ..StepEvent::default()
            },
        );
        if index == 2 || index == 3 {
            assert!(!c.gate && !c.trigger && !c.accent && !c.slide);
        } else {
            assert!(c.gate);
            assert_eq!(c.note, if index == 4 { 19 } else { 12 });
        }
    }
}

#[test]
fn manual_pitch_then_time_then_tap_held_accent_slide() {
    let mut e = Editor::new();
    let mut m = Memory::default();
    mode(&mut e, &mut m, Mode::PatternWrite);
    clear(&mut e, &mut m, 1);
    length(&mut e, &mut m, 4);
    tap(&mut e, &mut m, Button::PitchMode);
    let out = press(&mut e, &mut m, Button::Pitch(0));
    assert_eq!(
        out.preview.unwrap(),
        VoiceCommand {
            note: 12,
            gate: true,
            trigger: true,
            ..VoiceCommand::default()
        }
    );
    assert_eq!(e.leds(&m).pitch[0], Led::On);
    assert!(
        !release(&mut e, &mut m, Button::Pitch(0))
            .preview
            .unwrap()
            .gate
    );
    press(&mut e, &mut m, Button::Down);
    tap(&mut e, &mut m, Button::Pitch(9));
    release(&mut e, &mut m, Button::Down);
    tap(&mut e, &mut m, Button::Pitch(7));
    tap(&mut e, &mut m, Button::Back);
    press(&mut e, &mut m, Button::Up);
    tap(&mut e, &mut m, Button::Pitch(4));
    release(&mut e, &mut m, Button::Up);
    tap(&mut e, &mut m, Button::Function);
    tap(&mut e, &mut m, Button::TimeMode);
    for time in [Time::Note, Time::Tie, Time::Rest, Time::Note] {
        assert!(tap(&mut e, &mut m, Button::Time(time)).preview.is_none());
    }
    assert_eq!(e.edit_mode(), EditMode::Normal);
    tap(&mut e, &mut m, Button::PitchMode);
    press(&mut e, &mut m, Button::Tap);
    tap(&mut e, &mut m, Button::Accent);
    tap(&mut e, &mut m, Button::Slide);
    let leds = e.leds(&m);
    assert_eq!(leds.modifiers, [Led::Off, Led::Off, Led::On, Led::On]);
    release(&mut e, &mut m, Button::Tap);
    // Without TAP the modifier cannot change the next note.
    assert!(!tap(&mut e, &mut m, Button::Accent).changed);
    let p = m.bank.patterns[0];
    assert!(p.steps[0].accent && p.steps[0].slide);
    assert_eq!((p.steps[1].pitch, p.steps[1].octave), (9, Octave::Down));
    assert_eq!((p.steps[2].pitch, p.steps[2].octave), (4, Octave::Up));
    assert_eq!(
        &p.time[..4],
        &[Time::Note, Time::Tie, Time::Rest, Time::Note]
    );
    let mut bytes = [0; Memory::ENCODED_SIZE];
    m.encode(&mut bytes).unwrap();
    assert_eq!(Memory::decode(&bytes).unwrap(), m);
}

#[test]
fn clear_chord_length_triplets_and_time_last_step() {
    let mut e = Editor::new();
    let mut m = Memory::default();
    mode(&mut e, &mut m, Mode::PatternWrite);
    m.bank.patterns[0].steps[0].pitch = 7;
    assert!(!tap(&mut e, &mut m, Button::Clear).changed);
    m.bank.patterns[0].time[0] = Time::Rest;
    m.bank.patterns[0].length = 5;
    clear(&mut e, &mut m, 1);
    // CLEAR empties timing (all Rests) but keeps pitch data.
    assert_eq!(m.bank.patterns[0].steps[0].pitch, 7);
    assert_eq!(m.bank.patterns[0].time, [Time::Rest; 16]);
    assert_eq!(m.bank.patterns[0].length, 16);
    length(&mut e, &mut m, 3);
    assert_eq!(m.bank.patterns[0].length, 3);
    tap(&mut e, &mut m, Button::TimeMode);
    tap(&mut e, &mut m, Button::Down);
    tap(&mut e, &mut m, Button::Up);
    tap(&mut e, &mut m, Button::Back);
    tap(&mut e, &mut m, Button::Accent);
    assert_eq!(e.edit_mode(), EditMode::Time);
    tap(&mut e, &mut m, Button::Down);
    assert_eq!(e.edit_mode(), EditMode::Normal);
    assert_eq!(
        &m.bank.patterns[0].time[..3],
        &[Time::Note, Time::Rest, Time::Note]
    );
    press(&mut e, &mut m, Button::Function);
    tap(&mut e, &mut m, Button::Triplet);
    release(&mut e, &mut m, Button::Function);
    assert_eq!(
        (m.bank.patterns[0].triplet, m.bank.patterns[0].length),
        (true, 12)
    );
    // FUNCTION + 3/4 keeps timing data; counting STEP clears it.
    assert_eq!(m.bank.patterns[0].time[1], Time::Rest);
    length(&mut e, &mut m, 15);
    assert_eq!(
        (m.bank.patterns[0].triplet, m.bank.patterns[0].length),
        (true, 15)
    );
    assert_eq!(m.bank.patterns[0].time, [Time::Rest; 16]);
    // Sixteen STEP presses return 3/4 to 4/4.
    length(&mut e, &mut m, 16);
    assert_eq!(
        (m.bank.patterns[0].triplet, m.bank.patterns[0].length),
        (false, 16)
    );
    tap(&mut e, &mut m, Button::Triplet);
    press(&mut e, &mut m, Button::Function);
    tap(&mut e, &mut m, Button::Triplet);
    release(&mut e, &mut m, Button::Function);
    clear(&mut e, &mut m, 1);
    assert!(!m.bank.patterns[0].triplet);
    assert_eq!(m.bank.patterns[0].length, 16);
}

#[test]
fn pitch_capacity_duplicate_down_and_shared_white_key_aliases() {
    let mut e = Editor::new();
    let mut m = Memory::default();
    mode(&mut e, &mut m, Mode::PatternWrite);
    press(&mut e, &mut m, Button::Pitch(0));
    assert!(tap(&mut e, &mut m, Button::Clear).changed);
    release(&mut e, &mut m, Button::Pitch(0));
    tap(&mut e, &mut m, Button::PitchMode);
    press(&mut e, &mut m, Button::Pitch(2));
    assert_eq!(
        press(&mut e, &mut m, Button::Number(2)),
        EditOutput::default()
    );
    release(&mut e, &mut m, Button::Number(2));
    for _ in 1..16 {
        tap(&mut e, &mut m, Button::Pitch(5));
    }
    assert_eq!(e.selection().step, 16);
    assert!(!tap(&mut e, &mut m, Button::Pitch(9)).changed);
    assert_eq!(m.bank.patterns[0].steps[15].pitch, 5);
    tap(&mut e, &mut m, Button::Back);
    assert!(tap(&mut e, &mut m, Button::Pitch(9)).changed);
    assert_eq!(m.bank.patterns[0].steps[15].pitch, 9);
}

#[test]
fn chain_uses_four_pattern_blocks_and_defers_queue_until_series_end() {
    let mut e = Editor::new();
    let mut m = Memory::default();
    let mut s = Sequencer::default();
    for (i, p) in m.bank.patterns.iter_mut().enumerate() {
        p.length = 1;
        p.steps[0].pitch = (i % 12) as u8;
    }
    press(&mut e, &mut m, Button::Number(1));
    press(&mut e, &mut m, Button::Number(3));
    release(&mut e, &mut m, Button::Number(1));
    release(&mut e, &mut m, Button::Number(3));
    assert_eq!(e.chain().last, 2);
    start(&mut e, &mut m, &mut s);
    assert_eq!(e.tick(&mut s, &m).note, 12);
    tap(&mut e, &mut m, Button::Number(5));
    assert_eq!(e.queued_chain().unwrap().first.pattern, 4);
    for _ in 1..6000 {
        e.tick(&mut s, &m);
    }
    assert_eq!(e.tick(&mut s, &m).note, 13);
    assert!(e.queued_chain().is_some());
    for _ in 6001..12000 {
        e.tick(&mut s, &m);
    }
    assert_eq!(e.tick(&mut s, &m).note, 14);
    for _ in 12001..18000 {
        e.tick(&mut s, &m);
    }
    let c = e.tick(&mut s, &m);
    assert_eq!(c.note, 16);
    assert!(c.trigger);
    assert!(e.queued_chain().is_none());
    // Single-step repeated patterns still have a fresh edge each cycle.
    for _ in 18001..24000 {
        e.tick(&mut s, &m);
    }
    assert!(e.tick(&mut s, &m).trigger);
    start(&mut e, &mut m, &mut s); // stop
    press(&mut e, &mut m, Button::Number(4));
    press(&mut e, &mut m, Button::Number(5));
    assert_eq!(e.chain().first.pattern, 4);
    assert_eq!(e.chain().last, 4);
}

#[test]
fn chain_preserves_slide_between_patterns_and_mixed_step_modes() {
    let mut e = Editor::new();
    let mut m = Memory::default();
    let mut s = Sequencer::default();
    m.bank.patterns[0].length = 1;
    m.bank.patterns[0].steps[0].slide = true;
    m.bank.patterns[1].length = 1;
    m.bank.patterns[1].triplet = true;
    m.bank.patterns[1].steps[0].pitch = 7;
    press(&mut e, &mut m, Button::Number(1));
    press(&mut e, &mut m, Button::Number(2));
    release(&mut e, &mut m, Button::Number(1));
    release(&mut e, &mut m, Button::Number(2));
    start(&mut e, &mut m, &mut s);
    assert!(e.tick(&mut s, &m).trigger);
    for _ in 1..6000 {
        assert!(e.tick(&mut s, &m).gate);
    }
    let c = e.tick(&mut s, &m);
    assert_eq!(c.note, 19);
    assert!(c.slide && c.gate && !c.trigger);
    for _ in 6001..14000 {
        e.tick(&mut s, &m);
    }
    let c = e.tick(&mut s, &m);
    assert_eq!(c.note, 12);
    assert!(c.trigger);
}

#[test]
fn write_track_transpose_dc_insert_delete_and_codec() {
    let mut e = Editor::new();
    let mut m = Memory::default();
    let mut s = Sequencer::default();
    mode(&mut e, &mut m, Mode::TrackWrite);
    e.action(&mut m, PanelAction::Track(2)).unwrap(); // track 3 => pattern group II
    assert_eq!(e.selection().pattern.group, 1);
    tap(&mut e, &mut m, Button::Clear);
    start(&mut e, &mut m, &mut s);
    tap(&mut e, &mut m, Button::Number(2));
    press(&mut e, &mut m, Button::PitchMode);
    tap(&mut e, &mut m, Button::Pitch(7));
    release(&mut e, &mut m, Button::PitchMode);
    tap(&mut e, &mut m, Button::Tap);
    assert_eq!(m.tracks[2].entries[0].transpose, 7);
    tap(&mut e, &mut m, Button::Number(3));
    tap(&mut e, &mut m, Button::Clear);
    tap(&mut e, &mut m, Button::Tap);
    assert_eq!(m.tracks[2].length, 2);
    assert_eq!(e.selection().measure, 0);
    start(&mut e, &mut m, &mut s); // stop
    press(&mut e, &mut m, Button::Function);
    tap(&mut e, &mut m, Button::Number(2));
    release(&mut e, &mut m, Button::Function);
    assert_eq!(e.selection().measure, 1);
    press(&mut e, &mut m, Button::Function);
    tap(&mut e, &mut m, Button::Insert);
    release(&mut e, &mut m, Button::Function);
    assert_eq!(m.tracks[2].length, 2); // insertion is committed by WRITE/NEXT
    start(&mut e, &mut m, &mut s);
    tap(&mut e, &mut m, Button::Number(4));
    tap(&mut e, &mut m, Button::Tap);
    start(&mut e, &mut m, &mut s);
    assert_eq!(m.tracks[2].length, 3);
    assert_eq!(
        m.tracks[2].entries[..3]
            .iter()
            .map(|e| e.pattern.pattern)
            .collect::<Vec<_>>(),
        [1, 3, 2]
    );
    press(&mut e, &mut m, Button::Function);
    tap(&mut e, &mut m, Button::Number(2));
    tap(&mut e, &mut m, Button::Delete);
    release(&mut e, &mut m, Button::Function);
    assert_eq!(m.tracks[2].length, 2);
    assert_eq!(m.tracks[2].entries[1].pattern.pattern, 2);
    let mut bytes = [0; Memory::ENCODED_SIZE];
    m.encode(&mut bytes).unwrap();
    assert_eq!(Memory::decode(&bytes).unwrap(), m);
    mode(&mut e, &mut m, Mode::TrackPlay);
    tap(&mut e, &mut m, Button::Clear);
    m.bank.patterns[17].length = 1;
    m.bank.patterns[18].length = 1;
    start(&mut e, &mut m, &mut s);
    assert_eq!(e.tick(&mut s, &m).note, 19);
    for _ in 1..6000 {
        e.tick(&mut s, &m);
    }
    assert_eq!(e.tick(&mut s, &m).note, 12);
    assert_eq!(e.selection().measure, 1);
}

#[test]
fn leds_blink_phase_shared_lamps_modes_and_preview_consumption() {
    let mut e = Editor::new();
    let mut m = Memory::default();
    let mut s = Sequencer::default();
    let leds = e.leds(&m);
    assert_eq!(leds.modes, [Led::On, Led::Off, Led::Off]);
    assert!(leds.selectors[0].lit(true));
    assert!(!leds.selectors[0].lit(false));
    assert_eq!(leds.pitch[0], leds.selectors[0]);
    mode(&mut e, &mut m, Mode::PatternWrite);
    tap(&mut e, &mut m, Button::PitchMode);
    press(&mut e, &mut m, Button::Pitch(3));
    assert_eq!(e.leds(&m).modes, [Led::Off, Led::On, Led::Off]);
    assert_eq!(e.leds(&m).pitch[3], Led::On);
    assert!(e.tick(&mut s, &m).trigger);
    assert!(!e.tick(&mut s, &m).trigger);
    release(&mut e, &mut m, Button::Pitch(3));
    assert!(!e.tick(&mut s, &m).gate);
    let out = tap(&mut e, &mut m, Button::RunStop);
    e.apply_transport(&mut s, out);
    assert_eq!(e.leds(&m).run, Led::On);
    // Keyboard lamps do not follow the sounding note during playback.
    for _ in 0..48_000 {
        e.tick(&mut s, &m);
    }
    let leds = e.leds(&m);
    assert_eq!(leds.pitch[3], Led::Off);
    assert_eq!(leds.modifiers, [Led::Off; 4]);
    assert_eq!(leds.pitch[0], Led::Blink);
    mode(&mut e, &mut m, Mode::TrackPlay);
    assert_eq!(e.leds(&m).run, Led::Off);
}

#[test]
fn invalid_inputs_do_not_mutate_and_capacity_operations_are_bounded() {
    let mut e = Editor::new();
    let mut m = Memory::default();
    let before = m.clone();
    for action in [
        PanelAction::Group(4),
        PanelAction::Track(7),
        PanelAction::Press(Button::Pitch(13)),
        PanelAction::Press(Button::Number(0)),
        PanelAction::Release(Button::Number(9)),
    ] {
        assert_eq!(e.action(&mut m, action), Err(EditError::InvalidAction));
    }
    assert_eq!(m, before);
    assert_eq!(e.selection().pattern, PatternRef::default());
    mode(&mut e, &mut m, Mode::TrackWrite);
    m.tracks[0].length = 64;
    press(&mut e, &mut m, Button::Function);
    tap(&mut e, &mut m, Button::Insert);
    release(&mut e, &mut m, Button::Function);
    tap(&mut e, &mut m, Button::RunStop);
    for _ in 0..100 {
        tap(&mut e, &mut m, Button::Tap);
    }
    assert_eq!(m.tracks[0].length, 64);
    assert_eq!(e.selection().measure, 63);
    assert!(m.validate().is_ok());
}

#[test]
fn designated_track_start_and_track_chain_write() {
    let mut e = Editor::new();
    let mut m = Memory::default();
    let mut s = Sequencer::default();
    mode(&mut e, &mut m, Mode::TrackWrite);
    start(&mut e, &mut m, &mut s);
    press(&mut e, &mut m, Button::Number(1));
    press(&mut e, &mut m, Button::Number(3));
    release(&mut e, &mut m, Button::Number(1));
    release(&mut e, &mut m, Button::Number(3));
    tap(&mut e, &mut m, Button::Clear);
    tap(&mut e, &mut m, Button::Tap);
    start(&mut e, &mut m, &mut s);
    assert_eq!(m.tracks[0].length, 3);
    for (i, p) in m.bank.patterns.iter_mut().take(3).enumerate() {
        p.length = 1;
        p.steps[0].pitch = i as u8;
    }
    mode(&mut e, &mut m, Mode::TrackPlay);
    press(&mut e, &mut m, Button::Function);
    tap(&mut e, &mut m, Button::Number(2));
    release(&mut e, &mut m, Button::Function);
    start(&mut e, &mut m, &mut s);
    assert_eq!(e.tick(&mut s, &m).note, 13);
    assert_eq!(e.selection().measure, 1);
    assert_eq!(
        e.action(&mut m, PanelAction::Group(1)),
        Err(EditError::InvalidAction)
    );
}

#[test]
fn queued_switch_uses_old_pattern_slide_and_tie_history() {
    let mut e = Editor::new();
    let mut m = Memory::default();
    let mut s = Sequencer::default();
    m.bank.patterns[0].length = 1;
    m.bank.patterns[0].steps[0].pitch = 2;
    m.bank.patterns[0].steps[0].slide = true;
    m.bank.patterns[1].length = 2;
    m.bank.patterns[1].time[0] = Time::Tie;
    m.bank.patterns[1].steps[0].pitch = 7;
    start(&mut e, &mut m, &mut s);
    assert_eq!(e.tick(&mut s, &m).note, 14);
    tap(&mut e, &mut m, Button::Number(2));
    for _ in 1..6000 {
        e.tick(&mut s, &m);
    }
    let c = e.tick(&mut s, &m);
    assert_eq!(c.note, 14);
    assert!(c.gate && !c.trigger);
    for _ in 6001..12000 {
        e.tick(&mut s, &m);
    }
    let c = e.tick(&mut s, &m);
    assert_eq!(c.note, 19);
    assert!(c.slide && !c.trigger);
}

#[test]
fn panel_triplet_midi_requires_eight_received_pulses() {
    use ravet_foundation::{EventData, MidiMessage};
    let mut e = Editor::new();
    let mut m = Memory::default();
    let mut s = Sequencer::default();
    m.bank.patterns[0].triplet = true;
    m.bank.patterns[0].length = 2;
    m.bank.patterns[0].steps[1].pitch = 7;
    s.clock.set_source(ClockSource::Midi);
    start(&mut e, &mut m, &mut s);
    // Editor must establish triplet subdivision before the first MIDI event.
    e.tick(&mut s, &m);
    s.event(EventData::Midi(MidiMessage::from_bytes(&[0xf8]).unwrap()));
    assert_eq!(e.tick(&mut s, &m).note, 12);
    for pulse in 1..=8 {
        for _ in 0..1000 {
            let c = e.tick(&mut s, &m);
            if pulse <= 8 {
                assert_eq!(c.note, 12);
            }
        }
        s.event(EventData::Midi(MidiMessage::from_bytes(&[0xf8]).unwrap()));
        let c = e.tick(&mut s, &m);
        assert_eq!(c.note, if pulse < 8 { 12 } else { 19 });
        assert_eq!(c.trigger, pulse == 8);
    }
}

/// Turning PATT.GROUP in pattern modes never restarts or switches the sequence;
/// it only sets the group addressed by the next pattern-key press. In track
/// modes the same knob selects a track and restarts track playback.
#[test]
fn patt_group_knob_waits_for_pattern_key_and_restarts_only_in_track_modes() {
    let mut e = Editor::new();
    let mut m = Memory::default();
    let mut s = Sequencer::default();
    s.prepare(48_000.0).unwrap();
    mode(&mut e, &mut m, Mode::PatternPlay);
    tap(&mut e, &mut m, Button::Number(3));
    start(&mut e, &mut m, &mut s);
    assert!(e.running());
    for detent in [0u8, 1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1, 0, 2] {
        let out = e.action(&mut m, PanelAction::Track(detent)).unwrap();
        assert_eq!(out.transport, TransportCommand::None, "detent {detent}");
        assert_eq!(e.selection().pattern.group, 0, "detent {detent}");
        assert_eq!(e.queued_chain(), None, "detent {detent}");
    }
    assert_eq!(e.playing_pattern().pattern, 2);
    // The next pattern key adopts the knob's group (II), queued while running.
    tap(&mut e, &mut m, Button::Number(5));
    assert_eq!(e.selection().pattern.group, 1);
    assert_eq!(
        e.queued_chain().map(|c| (c.first.group, c.first.pattern)),
        Some((1, 4))
    );

    // Stopped Pattern Write: the edit target also waits for a pattern key.
    mode(&mut e, &mut m, Mode::PatternWrite);
    e.action(&mut m, PanelAction::Track(6)).unwrap();
    assert_eq!(e.selection().pattern.group, 1);
    tap(&mut e, &mut m, Button::Number(1));
    assert_eq!(
        (e.selection().pattern.group, e.selection().pattern.pattern),
        (3, 0)
    );

    mode(&mut e, &mut m, Mode::TrackPlay);
    let out = e.action(&mut m, PanelAction::Track(3)).unwrap();
    assert_eq!(out.transport, TransportCommand::Reset);
    assert_eq!(e.selection().track, 3);
    assert_eq!(e.selection().pattern.group, 1);
}

#[test]
fn pitch_entry_and_recall_stop_at_pattern_length() {
    let mut e = Editor::new();
    let mut m = Memory::default();
    mode(&mut e, &mut m, Mode::PatternWrite);
    clear(&mut e, &mut m, 1);
    length(&mut e, &mut m, 4);
    tap(&mut e, &mut m, Button::PitchMode);
    for _ in 0..4 {
        assert!(tap(&mut e, &mut m, Button::Pitch(5)).changed);
    }
    assert!(!tap(&mut e, &mut m, Button::Pitch(9)).changed);
    assert_eq!(m.bank.patterns[0].steps[4].pitch, Step::default().pitch);
    tap(&mut e, &mut m, Button::Back);
    assert!(tap(&mut e, &mut m, Button::Pitch(9)).changed);
    assert_eq!(m.bank.patterns[0].steps[3].pitch, 9);
    // WRITE/NEXT recall also stops after the fourth pitch.
    tap(&mut e, &mut m, Button::PitchMode);
    for _ in 0..4 {
        assert!(tap(&mut e, &mut m, Button::Tap).preview.is_some());
    }
    assert!(tap(&mut e, &mut m, Button::Tap).preview.is_none());
}
