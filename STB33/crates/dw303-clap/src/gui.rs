//! Embedded GUI (Cocoa, Win32 or X11 parent); FFI parent attachment stays
//! isolated here. Windows and X11 sizes are physical pixels scaled by the host's
//! `set_scale`; Cocoa sizes are logical points. Linux editors pump GTK from a
//! host timer, so a Linux host without timer support gets no embedded editor.
use super::*;
use clack_extensions::gui::*;
use ravet_surface::{AttachmentError, ParameterAttachment, ParameterControl};
use ravet_webview::{Callbacks, WebView};
use serde_json::{Value, json};
use std::ptr::NonNull;

const SIZE: GuiSize = GuiSize {
    width: 1227,
    height: 589,
};
const HTML: &str = include_str!("../../../editor/index.html");
/// The host's native window API on this platform.
const API: GuiApiType<'static> = match GuiApiType::default_for_current_platform() {
    Some(api) => api,
    None => GuiApiType::COCOA,
};
pub(super) struct GuiState<'a> {
    view: WebView<'a>,
    attached: bool,
    size: GuiSize,
    /// Host content scale for physical-pixel APIs; always 1 on Cocoa.
    scale: f64,
    #[cfg(target_os = "linux")]
    timer: Option<clack_extensions::timer::TimerId>,
}
struct Dw303Callbacks<'a> {
    shared: &'a Dw303Shared,
    host: HostMainThreadHandle<'a>,
    params: Option<HostParams>,
    error: Option<&'static str>,
    display: Display,
    attachments: [ParameterAttachment<'static>; params::COUNT],
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
    if index_of(ClapId::new(id)).is_none() {
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
fn panel_size(size: GuiSize) -> GuiSize {
    scaled_panel_size(size, 1.0)
}
/// Nearest panel-aspect size within the 818–2045 point range at `scale`.
fn scaled_panel_size(size: GuiSize, scale: f64) -> GuiSize {
    let rounded_height = |width: u32| (width as f64 * 935.0 / 1636.0).round() as u32;
    let (minimum, maximum) = (
        (818.0 * scale).round() as u32,
        (2045.0 * scale).round() as u32,
    );
    let width = if size.height == rounded_height(size.width) {
        size.width
    } else {
        size.width
            .min((size.height as f64 * 1636.0 / 935.0).round() as u32)
    }
    .clamp(minimum, maximum);
    GuiSize {
        width,
        height: rounded_height(width),
    }
}
struct GuiControl<'a> {
    shared: &'a Dw303Shared,
    host: HostMainThreadHandle<'a>,
    params: Option<HostParams>,
}
impl GuiControl<'_> {
    fn notify(&self, result: Result<(), EditError>) -> Result<(), EditError> {
        result?;
        if let Some(params) = self.params {
            params.request_flush(&self.host);
        } else {
            self.host.request_process();
        }
        Ok(())
    }
}
impl ParameterControl for GuiControl<'_> {
    type Error = EditError;
    fn commit(&self, id: ravet_foundation::ParameterId, physical: f32) -> Result<(), EditError> {
        self.notify(queue_edit(self.shared, id, physical))
    }
    fn begin(&self, id: ravet_foundation::ParameterId) -> Result<(), EditError> {
        self.notify(self.shared.edits.begin(
            ClapId::new(id.0),
            self.shared.generation.load(Ordering::Acquire),
        ))
    }
    fn update(&self, id: ravet_foundation::ParameterId, physical: f32) -> Result<(), EditError> {
        self.notify(self.shared.edits.update(ClapId::new(id.0), physical))
    }
    fn end(&self, _: ravet_foundation::ParameterId) -> Result<(), EditError> {
        self.notify(self.shared.edits.end())
    }
}
impl Callbacks for Dw303Callbacks<'_> {
    fn message(&mut self, json: &str) {
        let result = (|| {
            if self.shared.link.load_panic_message(json) {
                self.host.request_process();
                return Ok("panel");
            }
            if self.shared.link.load_pattern_message(json) {
                self.host.request_process();
                return Ok("panel");
            }
            if let Some(action) = parse_panel(json)? {
                self.shared
                    .link
                    .push(action)
                    .map_err(|_| "Panel queue full; repeat the edge")?;
                self.host.request_process();
                return Ok("panel");
            }
            let (kind, id, value) = parse_edit(json)?;
            let index = params::schema()
                .map_err(|_| "Invalid schema")?
                .index(id)
                .ok_or("Unknown parameter")?;
            let control = GuiControl {
                shared: self.shared,
                host: self.host,
                params: self.params,
            };
            let attachment = &mut self.attachments[index];
            let result = match kind {
                "edit" => attachment.commit(&control, value),
                "begin" => attachment.begin(&control),
                "value" => attachment.update(&control, value),
                _ => attachment.end(&control),
            };
            result.map_err(|error| match error {
                AttachmentError::InvalidValue => "Invalid parameter value",
                AttachmentError::GestureState => "Invalid gesture sequence",
                AttachmentError::Transport(EditError::GestureActive) => {
                    "Finish the current gesture first"
                }
                AttachmentError::Transport(EditError::NoGesture) => "No active gesture",
                AttachmentError::Transport(EditError::Full) => "Host edit queue full; try again",
                AttachmentError::Transport(EditError::UnknownParameter) => "Unknown parameter",
                AttachmentError::Transport(EditError::InvalidValue) => "Invalid parameter value",
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
        Some(json!({"params":values,"leds":panel_lamps(d),"selection":{"mode":d.mode,"group":d.group,"track":d.track,"section":if d.section_b {"B"}else{"A"},"pattern":d.pattern+1,"step":d.step+1,"editMode":d.edit_mode},"error":self.error,"pending":self.shared.edits.pending(),"hostBpm":self.shared.link.host_bpm()}).to_string())
    }
}
impl Drop for Dw303Callbacks<'_> {
    fn drop(&mut self) {
        let control = GuiControl {
            shared: self.shared,
            host: self.host,
            params: self.params,
        };
        for attachment in &mut self.attachments {
            if attachment.gesture_active() {
                let _ = attachment.end(&control);
            }
        }
    }
}
impl PluginGuiImpl for Dw303Main<'_> {
    fn is_api_supported(&self, configuration: GuiConfiguration) -> bool {
        configuration.api_type == API && !configuration.is_floating
    }
    fn get_preferred_api(&self) -> Option<GuiConfiguration<'_>> {
        Some(GuiConfiguration {
            api_type: API,
            is_floating: false,
        })
    }
    fn create(&self, configuration: GuiConfiguration) -> Result<(), PluginError> {
        if !self.is_api_supported(configuration) || self.gui.borrow().is_some() {
            return Err(PluginError::Message(
                "unsupported or already created editor",
            ));
        }
        let callbacks = Dw303Callbacks {
            shared: self.shared,
            host: self.host,
            params: self.host.get_extension::<HostParams>(),
            error: None,
            display: Display {
                mode: 2,
                ..Display::default()
            },
            attachments: core::array::from_fn(|i| {
                ParameterAttachment::new(
                    params::schema().expect("valid static schema"),
                    params::DESCRIPTORS[i].id,
                )
                .expect("known parameter")
            }),
        };
        let html = ravet_surface::embed_web_controls(HTML)
            .ok_or(PluginError::Message("invalid local control page"))?;
        let view = WebView::from_html(&html, SIZE.width as f64, SIZE.height as f64, callbacks)
            .map_err(|_| PluginError::Message("native editor creation failed or wrong thread"))?;
        view.set_background(23.0 / 255.0, 29.0 / 255.0, 36.0 / 255.0);
        view.set_visible(false);
        #[cfg(target_os = "linux")]
        let timer = Some(
            self.host
                .get_extension::<clack_extensions::timer::HostTimer>()
                .ok_or(PluginError::Message(
                    "host has no timer support for the editor",
                ))?
                .register_timer(&self.host, 10)
                .map_err(|_| PluginError::Message("host refused the editor timer"))?,
        );
        *self.gui.borrow_mut() = Some(GuiState {
            view,
            attached: false,
            size: SIZE,
            scale: 1.0,
            #[cfg(target_os = "linux")]
            timer,
        });
        Ok(())
    }
    fn destroy(&self) {
        let editor = self.gui.borrow_mut().take();
        #[cfg(target_os = "linux")]
        if let (Some(timer), Some(host_timer)) = (
            editor.as_ref().and_then(|state| state.timer),
            self.host
                .get_extension::<clack_extensions::timer::HostTimer>(),
        ) {
            let _ = host_timer.unregister_timer(&self.host, timer);
        }
        drop(editor);
    }
    fn set_scale(&self, scale: f64) -> Result<(), PluginError> {
        if !scale.is_finite() || scale <= 0.0 {
            return Err(PluginError::Message("invalid editor scale"));
        }
        if API.uses_logical_size() {
            return Ok(());
        }
        let mut state = self.gui.borrow_mut();
        let state = state
            .as_mut()
            .ok_or(PluginError::Message("editor not created"))?;
        let scale = scale.clamp(1.0, 4.0);
        let size = GuiSize {
            width: (state.size.width as f64 / state.scale * scale).round() as u32,
            height: (state.size.height as f64 / state.scale * scale).round() as u32,
        };
        state.scale = scale;
        state.size = scaled_panel_size(size, scale);
        if state.attached {
            state
                .view
                .set_size(state.size.width as f64, state.size.height as f64);
        }
        Ok(())
    }
    fn get_size(&self) -> Option<GuiSize> {
        self.gui.borrow().as_ref().map(|state| state.size)
    }
    fn can_resize(&self) -> bool {
        true
    }
    fn get_resize_hints(&self) -> Option<GuiResizeHints> {
        Some(GuiResizeHints {
            can_resize_horizontally: true,
            can_resize_vertically: true,
            strategy: AspectRatioStrategy::Preserve {
                width: 1636,
                height: 935,
            },
        })
    }
    fn adjust_size(&self, size: GuiSize) -> Option<GuiSize> {
        let scale = self.gui.borrow().as_ref().map_or(1.0, |state| state.scale);
        Some(scaled_panel_size(size, scale))
    }
    fn set_size(&self, size: GuiSize) -> Result<(), PluginError> {
        if self.adjust_size(size) != Some(size) {
            return Err(PluginError::Message(
                "editor size outside panel aspect/range",
            ));
        }
        let mut state = self.gui.borrow_mut();
        let state = state
            .as_mut()
            .ok_or(PluginError::Message("editor not created"))?;
        if !state.view.set_size(size.width as f64, size.height as f64) {
            return Err(PluginError::Message("native resize failed"));
        }
        state.size = size;
        Ok(())
    }
    fn set_parent(&self, window: Window) -> Result<(), PluginError> {
        #[cfg(target_os = "macos")]
        let parent = window.as_cocoa_nsview();
        #[cfg(target_os = "windows")]
        let parent = window.as_win32_hwnd();
        #[cfg(target_os = "linux")]
        let parent = window
            .as_x11_handle()
            .map(|id| id as usize as *mut std::ffi::c_void);
        let parent = parent.and_then(NonNull::new).ok_or(PluginError::Message(
            "editor requires a native parent window",
        ))?;
        let mut state = self.gui.borrow_mut();
        let state = state
            .as_mut()
            .ok_or(PluginError::Message("editor not created"))?;
        // SAFETY: the CLAP host supplies a live native parent (NSView, HWND or
        // X11 window) on its UI thread and retains it until detach/destroy, as
        // required by its GUI contract.
        unsafe {
            state.view.attach(parent);
        }
        state.attached = true;
        if !API.uses_logical_size() {
            state
                .view
                .set_size(state.size.width as f64, state.size.height as f64);
        }
        Ok(())
    }
    fn set_transient(&self, _: Window) -> Result<(), PluginError> {
        Err(PluginError::Message("floating editor unsupported"))
    }
    fn show(&self) -> Result<(), PluginError> {
        let state = self.gui.borrow();
        let state = state
            .as_ref()
            .filter(|s| s.attached)
            .ok_or(PluginError::Message("editor has no parent"))?;
        state.view.set_visible(true);
        Ok(())
    }
    fn hide(&self) -> Result<(), PluginError> {
        let state = self.gui.borrow();
        let state = state
            .as_ref()
            .ok_or(PluginError::Message("editor not created"))?;
        state.view.set_visible(false);
        Ok(())
    }
}
#[cfg(target_os = "linux")]
impl clack_extensions::timer::PluginTimerImpl for Dw303Main<'_> {
    fn on_timer(&self, _: clack_extensions::timer::TimerId) {
        if self.gui.borrow().is_some() {
            ravet_webview::pump();
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn resize_is_bounded_and_idempotent() {
        for size in [
            SIZE,
            GuiSize {
                width: 818,
                height: 393,
            },
            GuiSize {
                width: 2045,
                height: 981,
            },
            GuiSize {
                width: 0,
                height: 0,
            },
            GuiSize {
                width: u32::MAX,
                height: u32::MAX,
            },
        ] {
            let adjusted = panel_size(size);
            assert_eq!(panel_size(adjusted), adjusted);
            assert!((818..=2045).contains(&adjusted.width));
            let doubled = scaled_panel_size(size, 2.0);
            assert_eq!(scaled_panel_size(doubled, 2.0), doubled);
            assert!((1636..=4090).contains(&doubled.width));
            assert!((adjusted.height as f64 - adjusted.width as f64 * 935.0 / 1636.0).abs() <= 0.5);
        }
    }
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
    fn inline_page_fits_and_contains_all_controls() {
        let page = ravet_surface::embed_web_controls(HTML).unwrap();
        assert!(page.len() < 1_048_576);
        // All 37 original SVG filters (section shadows, button bevels) are kept.
        // 37 panel filters plus the knob/switch artwork's own shadows.
        assert_eq!(page.matches("<filter").count(), 55);
        for id in 1..=8 {
            assert!(page.contains(&format!("id=\"param-{id}\"")));
        }
    }
    #[test]
    fn editor_protocol_rejects_malformed_and_nonfinite_messages() {
        assert_eq!(
            parse_edit(r#"{"type":"edit","id":1,"value":0.5}"#),
            Ok(("edit", ParameterId(1), 0.5))
        );
        for text in [
            "null",
            "[]",
            "{",
            r#"{"type":"edit","id":-1,"value":1}"#,
            r#"{"type":"edit","id":1,"value":1e100}"#,
            r#"{"type":"edit","id":1,"value":1,"extra":1}"#,
            r#"{"type":"other","id":1,"value":1}"#,
        ] {
            assert!(parse_edit(text).is_err());
        }
    }
}
