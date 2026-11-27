//! CLAP-compatible panel protocol, kept local to respect crate ownership.
use crate::json::Value;
use dw303_engine::link::{Button, Display, Led, Mode, PanelAction, Section};
/// Validate every field before sending a physical edge to the audio editor.
/// VERIFY: seven selector detents share groups as tracks 1/2, 3/4, 5/6, 7;
/// original pattern-mode switch wiring still needs comparison.
pub(crate) fn parse_panel(text: &str) -> Result<Option<PanelAction>, &'static str> {
    if text.len() > 4096 {
        return Err("Editor message too large");
    }
    let value: Value = crate::json::parse(text).map_err(|_| "Invalid editor message")?;
    if value["type"] != "panel" {
        return Ok(None);
    }
    let object = value.as_object().ok_or("Invalid panel message")?;
    let action = value["action"].as_str().ok_or("Invalid panel action")?;
    if matches!(action, "mode" | "pattern-group" | "section") {
        if object.len() != 3 || !object.contains_key("value") {
            return Err("Invalid selector fields");
        }
        let n = value["value"].as_u64().ok_or("Invalid selector")?;
        return Ok(Some(match action {
            "mode" => PanelAction::Mode(match n {
                0 => Mode::TrackWrite,
                1 => Mode::TrackPlay,
                2 => Mode::PatternPlay,
                3 => Mode::PatternWrite,
                _ => return Err("Invalid mode"),
            }),
            "pattern-group" if n < 7 => PanelAction::Track(n as u8),
            "section" if n < 2 => {
                PanelAction::Section(if n == 0 { Section::A } else { Section::B })
            }
            _ => return Err("Invalid selector"),
        }));
    }
    if action != "button"
        || object.len() != 4
        || !object.contains_key("key")
        || !object.contains_key("edge")
    {
        return Err("Invalid button fields");
    }
    let button = match value["key"].as_str().ok_or("Invalid key")? {
        "run-stop" => Button::RunStop,
        "function" => Button::Function,
        "pitch-mode" => Button::PitchMode,
        "time-mode" => Button::TimeMode,
        "tap" => Button::Tap,
        "clear" => Button::Clear,
        "key-c" => Button::Pitch(0),
        "key-c-sharp" => Button::Pitch(1),
        "key-d" => Button::Pitch(2),
        "key-d-sharp" => Button::Pitch(3),
        "key-e" => Button::Pitch(4),
        "key-f" => Button::Pitch(5),
        "key-f-sharp" => Button::Pitch(6),
        "key-g" => Button::Pitch(7),
        "key-g-sharp" => Button::Pitch(8),
        "key-a" => Button::Pitch(9),
        "key-a-sharp" => Button::Pitch(10),
        "key-b" => Button::Pitch(11),
        "key-c-up" => Button::Pitch(12),
        "key-down" => Button::Down,
        "key-up" => Button::Up,
        "key-accent" => Button::Accent,
        "key-slide" => Button::Slide,
        "key-back" => Button::Back,
        "key-step" => Button::Step,
        "key-triplet" => Button::Triplet,
        _ => return Err("Unknown panel key"),
    };
    Ok(Some(match value["edge"].as_str() {
        Some("press") => PanelAction::Press(button),
        Some("release") => PanelAction::Release(button),
        _ => return Err("Invalid button edge"),
    }))
}
fn lamp(l: Led) -> u8 {
    match l {
        Led::Off => 0,
        Led::On => 1,
        Led::Blink => 2,
    }
}
fn combine(a: Led, b: Led) -> Led {
    if a == Led::On || b == Led::On {
        Led::On
    } else if a == Led::Blink || b == Led::Blink {
        Led::Blink
    } else {
        Led::Off
    }
}
/// VERIFY: shared lamps follow artwork positions; firmware multiplexing needs hardware comparison.
pub(crate) fn panel_lamps(d: Display) -> std::collections::BTreeMap<String, Value> {
    let l = d.leds;
    let mut map = std::collections::BTreeMap::new();
    for (name, led) in [
        ("run", if d.running { Led::On } else { Led::Off }),
        ("pitch-c", l.modes[0]),
        ("function", l.modes[1]),
        ("pattern", l.modes[2]),
    ] {
        map.insert(name.into(), Value::Number(f64::from(lamp(led))));
    }
    for (name, n) in [
        ("c-sharp", 1),
        ("d-sharp", 3),
        ("f-sharp", 6),
        ("g-sharp", 8),
        ("a-sharp", 10),
    ] {
        map.insert(
            format!("pitch-{name}"),
            Value::Number(f64::from(lamp(l.pitch[n]))),
        );
    }
    for (i, n) in [0, 2, 4, 5, 7, 9, 11, 12].into_iter().enumerate() {
        map.insert(
            format!("step-{}", i + 1),
            Value::Number(f64::from(lamp(combine(l.pitch[n], l.selectors[i])))),
        );
    }
    for (i, led) in l.modifiers.into_iter().enumerate() {
        map.insert(
            format!("step-{}", i + 9),
            Value::Number(f64::from(lamp(if i >= 2 && d.edit_mode == 0 {
                l.sections[i - 2]
            } else {
                led
            }))),
        );
    }
    map
}
