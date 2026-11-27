//! Editor for the native adapters: the CLAP panel page in RAVET's web view
//! (Cocoa, Win32/WebView2 or GTK/WebKitGTK). Panel edges go to the shared link; parameter gestures go to the
//! adapter, which forwards them to its host, and to the audio edit queue.
#![allow(unsafe_code)]
use crate::Shared;
use dw303_engine::params;
use ravet_surface::{AttachmentError, ParameterAttachment, ParameterControl};
use ravet_webview::{Callbacks, WebView};
use serde_json::{Value, json};
use std::ffi::c_void;
use std::ptr::NonNull;
use std::sync::Arc;

pub const DEFAULT_SIZE: (u32, u32) = (1227, 701);
const HTML: &str = include_str!("../../../editor/index.html");

/// Adapter callbacks; see `Dw303EditorHost` in `include/dw303.h`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct EditorHost {
    context: *mut c_void,
    begin: Option<extern "C" fn(*mut c_void, u32)>,
    perform: Option<extern "C" fn(*mut c_void, u32, f32)>,
    end: Option<extern "C" fn(*mut c_void, u32)>,
}

pub struct Editor<'a> {
    view: WebView<'a>,
    /// Host content scale for physical-pixel parents (Windows, Linux); 1 on macOS.
    scale: f64,
}

impl Editor<'_> {
    pub fn new(shared: Arc<Shared>, host: EditorHost) -> Option<Self> {
        let callbacks = EditorCallbacks {
            shared,
            host,
            error: None,
            display: Display {
                mode: 2,
                ..Display::default()
            },
            attachments: core::array::from_fn(|i| {
                ParameterAttachment::new(params::schema().ok()?, params::DESCRIPTORS[i].id)
            }),
        };
        if callbacks.attachments.iter().any(Option::is_none) {
            return None;
        }
        let html = ravet_surface::embed_web_controls(HTML)?;
        let (width, height) = DEFAULT_SIZE;
        let view = WebView::from_html(&html, width as f64, height as f64, callbacks).ok()?;
        view.set_background(23.0 / 255.0, 29.0 / 255.0, 36.0 / 255.0);
        Some(Self { view, scale: 1.0 })
    }
    /// # Safety
    /// `parent` is a live NSView, HWND or X11 window ID on the UI thread,
    /// retained until drop.
    pub unsafe fn attach(&mut self, parent: NonNull<c_void>) -> bool {
        unsafe { self.view.attach(parent) };
        self.view.set_visible(true);
        true
    }
    pub fn set_size(&mut self, width: u32, height: u32) -> bool {
        constrain_scaled(width, height, self.scale) == (width, height)
            && self.view.set_size(width as f64, height as f64)
    }
    /// Accept the host's content scale; sizes then range over the scaled panel.
    pub fn set_scale(&mut self, scale: f64) -> bool {
        if !scale.is_finite() || scale <= 0.0 {
            return false;
        }
        self.scale = scale.clamp(1.0, 4.0);
        true
    }
}

/// Keep the panel artwork's 1636:935 aspect within 818–2045 points wide.
pub fn constrain(width: u32, height: u32) -> (u32, u32) {
    constrain_scaled(width, height, 1.0)
}
/// [`constrain`] for a content scale clamped to 1–4 (physical-pixel hosts).
pub fn constrain_scaled(width: u32, height: u32, scale: f64) -> (u32, u32) {
    let scale = if scale.is_finite() {
        scale.clamp(1.0, 4.0)
    } else {
        1.0
    };
    let (minimum, maximum) = (
        (818.0 * scale).round() as u32,
        (2045.0 * scale).round() as u32,
    );
    let rounded_height = |width: u32| (width as f64 * 935.0 / 1636.0).round() as u32;
    let width = if height == rounded_height(width) {
        width
    } else {
        width.min((height as f64 * 1636.0 / 935.0).round() as u32)
    }
    .clamp(minimum, maximum);
    (width, rounded_height(width))
}

#[derive(Clone, Copy, Debug)]
struct Rejected;

struct Control<'a> {
    shared: &'a Shared,
    host: EditorHost,
}

impl Control<'_> {
    fn index(id: ravet_foundation::ParameterId) -> Result<u32, Rejected> {
        params::DESCRIPTORS
            .iter()
            .position(|d| d.id == id)
            .map(|i| i as u32)
            .ok_or(Rejected)
    }
}

impl ParameterControl for Control<'_> {
    type Error = Rejected;
    fn commit(&self, id: ravet_foundation::ParameterId, physical: f32) -> Result<(), Rejected> {
        self.begin(id)?;
        let result = self.update(id, physical);
        self.end(id)?;
        result
    }
    fn begin(&self, id: ravet_foundation::ParameterId) -> Result<(), Rejected> {
        let index = Self::index(id)?;
        if let Some(begin) = self.host.begin {
            begin(self.host.context, index);
        }
        Ok(())
    }
    fn update(&self, id: ravet_foundation::ParameterId, physical: f32) -> Result<(), Rejected> {
        let index = Self::index(id)?;
        if !self.shared.set(index as usize, physical) {
            return Err(Rejected);
        }
        if let Some(perform) = self.host.perform {
            perform(self.host.context, index, self.shared.value(index as usize));
        }
        Ok(())
    }
    fn end(&self, id: ravet_foundation::ParameterId) -> Result<(), Rejected> {
        let index = Self::index(id)?;
        if let Some(end) = self.host.end {
            end(self.host.context, index);
        }
        Ok(())
    }
}

struct EditorCallbacks {
    shared: Arc<Shared>,
    host: EditorHost,
    error: Option<&'static str>,
    display: Display,
    attachments: [Option<ParameterAttachment<'static>>; params::COUNT],
}

impl Callbacks for EditorCallbacks {
    fn message(&mut self, json: &str) {
        let result = (|| {
            if self.shared.link.load_panic_message(json) {
                return Ok("panel");
            }
            if self.shared.link.load_pattern_message(json) {
                return Ok("panel");
            }
            if let Some(action) = parse_panel(json)? {
                self.shared
                    .link
                    .push(action)
                    .map_err(|_| "Panel queue full; repeat the edge")?;
                return Ok("panel");
            }
            let (kind, id, value) = parse_edit(json)?;
            let index = params::DESCRIPTORS
                .iter()
                .position(|d| d.id == id)
                .ok_or("Unknown parameter")?;
            let control = Control {
                shared: &self.shared,
                host: self.host,
            };
            let attachment = self.attachments[index]
                .as_mut()
                .ok_or("Unknown parameter")?;
            let result = match kind {
                "edit" => attachment.commit(&control, value),
                "begin" => attachment.begin(&control),
                "value" => attachment.update(&control, value),
                _ => attachment.end(&control),
            };
            result.map_err(|error| match error {
                AttachmentError::InvalidValue => "Invalid parameter value",
                AttachmentError::GestureState => "Invalid gesture sequence",
                AttachmentError::Transport(Rejected) => "Edit queue full; try again",
            })?;
            Ok(kind)
        })();
        self.error = match result {
            Ok("end") => self.error,
            Ok(_) => None,
            Err(error) => Some(error),
        };
    }
    fn snapshot(&mut self) -> Option<String> {
        let mut values = serde_json::Map::new();
        for (i, attachment) in self.attachments.iter_mut().enumerate() {
            let attachment = attachment.as_mut()?;
            attachment.refresh(self.shared.value(i));
            values.insert(
                params::DESCRIPTORS[i].id.0.to_string(),
                json!(attachment.physical()),
            );
        }
        if let Some(display) = self.shared.link.display() {
            self.display = display;
        }
        let d = self.display;
        Some(json!({"params":values,"leds":panel_lamps(d),"selection":{"mode":d.mode,"group":d.group,"track":d.track,"section":if d.section_b {"B"}else{"A"},"pattern":d.pattern+1,"step":d.step+1,"editMode":d.edit_mode},"error":self.error,"pending":false,"hostBpm":self.shared.link.host_bpm()}).to_string())
    }
}

impl Drop for EditorCallbacks {
    fn drop(&mut self) {
        let control = Control {
            shared: &self.shared,
            host: self.host,
        };
        for attachment in self.attachments.iter_mut().flatten() {
            if attachment.gesture_active() {
                let _ = attachment.end(&control);
            }
        }
    }
}

fn parse_edit(
    json: &str,
) -> Result<(&'static str, ravet_foundation::ParameterId, f32), &'static str> {
    if json.len() > 4096 {
        return Err("Editor message too large");
    }
    let value: Value = serde_json::from_str(json).map_err(|_| "Invalid editor message")?;
    let object = value.as_object().ok_or("Invalid editor message")?;
    if object.len() != 3 {
        return Err("Unknown editor message");
    }
    let kind = match value["type"].as_str() {
        Some("edit") => "edit",
        Some("begin") => "begin",
        Some("value") => "value",
        Some("end") => "end",
        _ => return Err("Unknown editor message"),
    };
    let id = u32::try_from(value["id"].as_u64().ok_or("Invalid parameter ID")?)
        .map_err(|_| "Invalid parameter ID")?;
    if params::DESCRIPTORS.iter().all(|d| d.id.0 != id) {
        return Err("Unknown parameter");
    }
    let value = value["value"].as_f64().ok_or("Invalid parameter value")? as f32;
    if !value.is_finite() {
        return Err("Invalid parameter value");
    }
    Ok((kind, ravet_foundation::ParameterId(id), value))
}
use dw303_engine::link::{Button, Display, Led, Mode, PanelAction, Section};
/// Validate every field before sending a physical edge to the audio editor.
/// VERIFY: seven selector detents share groups as tracks 1/2, 3/4, 5/6, 7;
/// original pattern-mode switch wiring still needs comparison.
fn parse_panel(text: &str) -> Result<Option<PanelAction>, &'static str> {
    if text.len() > 4096 {
        return Err("Editor message too large");
    }
    let value: Value = serde_json::from_str(text).map_err(|_| "Invalid editor message")?;
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
fn panel_lamps(d: Display) -> serde_json::Map<String, Value> {
    let l = d.leds;
    let mut map = serde_json::Map::new();
    for (name, led) in [
        ("run", if d.running { Led::On } else { Led::Off }),
        ("pitch-c", l.modes[0]),
        ("function", l.modes[1]),
        ("pattern", l.modes[2]),
    ] {
        map.insert(name.into(), json!(lamp(led)));
    }
    for (name, n) in [
        ("c-sharp", 1),
        ("d-sharp", 3),
        ("f-sharp", 6),
        ("g-sharp", 8),
        ("a-sharp", 10),
    ] {
        map.insert(format!("pitch-{name}"), json!(lamp(l.pitch[n])));
    }
    for (i, n) in [0, 2, 4, 5, 7, 9, 11, 12].into_iter().enumerate() {
        map.insert(
            format!("step-{}", i + 1),
            json!(lamp(combine(l.pitch[n], l.selectors[i]))),
        );
    }
    for (i, led) in l.modifiers.into_iter().enumerate() {
        map.insert(
            format!("step-{}", i + 9),
            json!(lamp(if i >= 2 && d.edit_mode == 0 {
                l.sections[i - 2]
            } else {
                led
            })),
        );
    }
    map
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn protocol_bounds_and_panel_validation() {
        for message in [
            " ".repeat(4097),
            format!(r#"{{"type":"edit","id":1,"value":{}}}"#, "9".repeat(4096)),
        ] {
            assert!(parse_edit(&message).is_err());
            assert!(parse_panel(&message).is_err());
        }
        for text in [
            r#"{"type":"edit","id":99,"value":0}"#,
            r#"{"type":"edit","id":1,"value":"NaN"}"#,
            r#"{"type":"edit","id":1,"value":1e309}"#,
        ] {
            assert!(parse_edit(text).is_err());
        }
        for text in [
            r#"{"type":"panel","action":"unknown"}"#,
            r#"{"type":"panel","action":"mode","value":17}"#,
            r#"{"type":"panel","action":"pitch","key":[]}"#,
            r#"{"type":"panel","action":"pitch","value":1e100}"#,
            r#"{"type":"panel","action":"mode","extra":0}"#,
        ] {
            assert!(parse_panel(text).is_err());
        }
        assert_eq!(
            parse_panel(r#"{"type":"panel","action":"button","key":"key-c","edge":"press"}"#),
            Ok(Some(PanelAction::Press(Button::Pitch(0))))
        );
        assert_eq!(
            parse_panel(r#"{"type":"panel","action":"mode","value":3}"#),
            Ok(Some(PanelAction::Mode(Mode::PatternWrite)))
        );
    }
    #[test]
    fn every_artwork_lamp_is_published() {
        let map = panel_lamps(Display::default());
        let ids: Vec<_> = HTML
            .split("id=\"led-")
            .skip(1)
            .map(|s| s.split('"').next().unwrap())
            .collect();
        assert_eq!(map.len(), ids.len());
        for id in ids {
            assert!(map.contains_key(id));
        }
        for (key, button) in [
            ("run-stop", Button::RunStop),
            ("key-c", Button::Pitch(0)),
            ("key-slide", Button::Slide),
        ] {
            for (edge, action) in [
                ("press", PanelAction::Press(button)),
                ("release", PanelAction::Release(button)),
            ] {
                assert_eq!(
                    parse_panel(&format!(
                        r#"{{"type":"panel","action":"button","key":"{key}","edge":"{edge}"}}"#
                    )),
                    Ok(Some(action))
                );
            }
        }
    }
    #[test]
    fn resize_is_bounded_and_idempotent() {
        for (w, h) in [
            DEFAULT_SIZE,
            (818, 393),
            (2045, 981),
            (0, 0),
            (u32::MAX, u32::MAX),
        ] {
            let adjusted = constrain(w, h);
            assert_eq!(constrain(adjusted.0, adjusted.1), adjusted);
            let doubled = constrain_scaled(w, h, 2.0);
            assert_eq!(constrain_scaled(doubled.0, doubled.1, 2.0), doubled);
            assert!((1636..=4090).contains(&doubled.0));
            assert!((818..=2045).contains(&adjusted.0));
        }
    }
}
