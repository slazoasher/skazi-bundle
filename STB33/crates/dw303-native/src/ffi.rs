//! `extern "C"` surface declared in `include/dw303.h`. Null and out-of-range
//! arguments are rejected here; the adapters own thread discipline.
#![allow(unsafe_code)]
use super::*;
use std::ffi::{CStr, c_char, c_void};

#[repr(C)]
pub struct Dw303ParamInfo {
    id: u32,
    name: *const c_char,
    unit: *const c_char,
    minimum: f32,
    maximum: f32,
    initial: f32,
    steps: u32,
    automatable: bool,
}

/// NUL-terminated copies of the static descriptor strings.
static NAMES: [&CStr; params::COUNT] = [
    c"Tuning",
    c"Cut Off Freq",
    c"Resonance",
    c"Env Mod",
    c"Decay",
    c"Accent",
    c"Volume",
    c"Tempo",
    c"Waveform",
    c"Sync",
    c"Quality",
    c"Run",
    c"Attack",
    c"Amp Decay",
    c"Sustain",
    c"Release",
    c"Distortion",
    c"OTT",
    c"Reverb",
    c"Width",
    c"Glide",
    c"Pitch Glide",
];

#[unsafe(no_mangle)]
pub extern "C" fn dw303_param_count() -> u32 {
    params::COUNT as u32
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn dw303_param_info(index: u32, info: *mut Dw303ParamInfo) -> bool {
    let (Some(d), false) = (params::DESCRIPTORS.get(index as usize), info.is_null()) else {
        return false;
    };
    let unit = if d.unit == "BPM" { c"BPM" } else { c"" };
    // SAFETY: checked non-null; the caller supplies a writable record.
    unsafe {
        info.write(Dw303ParamInfo {
            id: d.id.0,
            name: NAMES[index as usize].as_ptr(),
            unit: unit.as_ptr(),
            minimum: d.range.minimum(),
            maximum: d.range.maximum(),
            initial: d.default,
            steps: d.range.intervals().map_or(0, u32::from),
            automatable: d.automatable,
        });
    }
    true
}

#[unsafe(no_mangle)]
pub extern "C" fn dw303_param_normalize(index: u32, value: f32) -> f32 {
    params::DESCRIPTORS
        .get(index as usize)
        .and_then(|d| d.range.normalize(d.range.canonicalize(value)?))
        .unwrap_or(0.0)
}

#[unsafe(no_mangle)]
pub extern "C" fn dw303_param_denormalize(index: u32, normalized: f32) -> f32 {
    params::DESCRIPTORS
        .get(index as usize)
        .and_then(|d| {
            d.range
                .canonicalize(d.range.denormalize(normalized.clamp(0.0, 1.0))?)
        })
        .unwrap_or(0.0)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn dw303_param_text(
    index: u32,
    value: f32,
    text: *mut c_char,
    capacity: usize,
) -> bool {
    let Some(string) = value_text(index as usize, value) else {
        return false;
    };
    if text.is_null() || string.len() >= capacity {
        return false;
    }
    // SAFETY: capacity bytes are writable and exceed the string plus NUL.
    unsafe {
        std::ptr::copy_nonoverlapping(string.as_ptr(), text.cast(), string.len());
        text.add(string.len()).write(0);
    }
    true
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn dw303_param_parse(
    index: u32,
    text: *const c_char,
    value: *mut f32,
) -> bool {
    if text.is_null() || value.is_null() {
        return false;
    }
    // SAFETY: caller supplies a NUL-terminated string.
    let Ok(text) = unsafe { CStr::from_ptr(text) }.to_str() else {
        return false;
    };
    match text_value(index as usize, text) {
        Some(parsed) => {
            // SAFETY: checked non-null.
            unsafe { value.write(parsed) };
            true
        }
        None => false,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn dw303_create() -> *mut Dw303 {
    Box::into_raw(Box::new(Dw303::new()))
}

/// # Safety
/// `dsp` came from `dw303_create`, has no open editor, and is not in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dw303_destroy(dsp: *mut Dw303) {
    if !dsp.is_null() {
        // SAFETY: ownership returns from the adapter.
        drop(unsafe { Box::from_raw(dsp) });
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn dw303_prepare(dsp: *mut Dw303, rate: f64, maximum_frames: u32) -> bool {
    // SAFETY: audio-context exclusive access.
    unsafe { dsp.as_mut() }.is_some_and(|d| d.prepare(rate, maximum_frames as usize))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn dw303_reset(dsp: *mut Dw303) {
    // SAFETY: audio-context exclusive access.
    if let Some(d) = unsafe { dsp.as_mut() } {
        d.reset();
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn dw303_deactivate(dsp: *mut Dw303) {
    // SAFETY: audio-context exclusive access.
    if let Some(d) = unsafe { dsp.as_mut() } {
        d.deactivate();
    }
}

unsafe fn slice<'a, T>(data: *const T, count: usize) -> &'a [T] {
    if data.is_null() || count == 0 {
        &[]
    } else {
        // SAFETY: caller supplies `count` readable records.
        unsafe { std::slice::from_raw_parts(data, count) }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn dw303_process(
    dsp: *mut Dw303,
    left: *mut f32,
    right: *mut f32,
    frames: u32,
    midi: *const Dw303Midi,
    midi_count: usize,
    values: *const Dw303Param,
    value_count: usize,
    transport: *const Dw303Transport,
) -> bool {
    if left.is_null() || right.is_null() || left == right {
        return false;
    }
    // SAFETY: two distinct host buffers of `frames` samples; audio-context
    // exclusive access to the instance.
    let (left, right) = unsafe {
        (
            std::slice::from_raw_parts_mut(left, frames as usize),
            std::slice::from_raw_parts_mut(right, frames as usize),
        )
    };
    let Some(d) = (unsafe { dsp.as_mut() }) else {
        left.fill(0.0);
        right.fill(0.0);
        return false;
    };
    let transport = unsafe { transport.as_ref() }.copied().unwrap_or_default();
    unsafe {
        d.process(
            left,
            right,
            slice(midi, midi_count),
            slice(values, value_count),
            transport,
        )
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn dw303_value(dsp: *const Dw303, index: u32) -> f32 {
    // SAFETY: shared values are atomics.
    unsafe { dsp.as_ref() }
        .and_then(|d| d.value(index as usize))
        .unwrap_or(0.0)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn dw303_set_value(dsp: *mut Dw303, index: u32, value: f32) -> bool {
    // SAFETY: the edit queue is shared and internally synchronized.
    unsafe { dsp.as_ref() }.is_some_and(|d| d.set_value(index as usize, value))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn dw303_state_save(dsp: *const Dw303, size: *mut usize) -> *mut u8 {
    // SAFETY: state reads shared atomics and the link's memory snapshot.
    let Some(bytes) = (unsafe { dsp.as_ref() }).and_then(Dw303::save) else {
        return std::ptr::null_mut();
    };
    if size.is_null() {
        return std::ptr::null_mut();
    }
    let mut bytes = bytes.into_boxed_slice();
    // SAFETY: checked non-null.
    unsafe { size.write(bytes.len()) };
    let pointer = bytes.as_mut_ptr();
    std::mem::forget(bytes);
    pointer
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn dw303_state_load(dsp: *mut Dw303, bytes: *const u8, size: usize) -> bool {
    // SAFETY: restores publish through the shared atomics and link.
    unsafe { dsp.as_ref() }.is_some_and(|d| d.load(unsafe { slice(bytes, size) }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn dw303_bytes_free(bytes: *mut u8, size: usize) {
    if !bytes.is_null() {
        // SAFETY: allocated by dw303_state_save as a boxed slice of `size`.
        drop(unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(bytes, size)) });
    }
}

#[cfg(any(target_os = "macos", target_os = "windows", target_os = "linux"))]
mod editor_ffi {
    use super::*;
    use crate::editor::{Editor, EditorHost};
    use std::ptr::NonNull;

    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn dw303_editor_default_size(width: *mut u32, height: *mut u32) {
        let (w, h) = crate::editor::DEFAULT_SIZE;
        // SAFETY: optional out-parameters.
        unsafe {
            if let Some(width) = width.as_mut() {
                *width = w;
            }
            if let Some(height) = height.as_mut() {
                *height = h;
            }
        }
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn dw303_editor_constrain(width: *mut u32, height: *mut u32) {
        // SAFETY: in/out parameters supplied by the adapter.
        let (Some(width), Some(height)) = (unsafe { width.as_mut() }, unsafe { height.as_mut() })
        else {
            return;
        };
        (*width, *height) = crate::editor::constrain(*width, *height);
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn dw303_editor_constrain_scaled(
        scale: f64,
        width: *mut u32,
        height: *mut u32,
    ) {
        // SAFETY: in/out parameters supplied by the adapter.
        let (Some(width), Some(height)) = (unsafe { width.as_mut() }, unsafe { height.as_mut() })
        else {
            return;
        };
        (*width, *height) = crate::editor::constrain_scaled(*width, *height, scale);
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn dw303_editor_set_scale(
        editor: *mut Editor<'static>,
        scale: f64,
    ) -> bool {
        // SAFETY: UI thread.
        unsafe { editor.as_mut() }.is_some_and(|e| e.set_scale(scale))
    }

    #[unsafe(no_mangle)]
    pub extern "C" fn dw303_editor_idle() {
        ravet_webview::pump();
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn dw303_editor_create(
        dsp: *mut Dw303,
        host: EditorHost,
    ) -> *mut Editor<'static> {
        // SAFETY: the editor keeps its own reference to the shared state.
        let Some(d) = (unsafe { dsp.as_ref() }) else {
            return std::ptr::null_mut();
        };
        Editor::new(d.shared(), host).map_or(std::ptr::null_mut(), |e| Box::into_raw(Box::new(e)))
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn dw303_editor_attach(
        editor: *mut Editor<'static>,
        parent: *mut c_void,
    ) -> bool {
        // SAFETY: main thread; the host keeps `parent` alive until destroy.
        match (unsafe { editor.as_mut() }, NonNull::new(parent)) {
            (Some(editor), Some(parent)) => unsafe { editor.attach(parent) },
            _ => false,
        }
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn dw303_editor_set_size(
        editor: *mut Editor<'static>,
        width: u32,
        height: u32,
    ) -> bool {
        // SAFETY: main thread.
        unsafe { editor.as_mut() }.is_some_and(|e| e.set_size(width, height))
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn dw303_editor_destroy(editor: *mut Editor<'static>) {
        if !editor.is_null() {
            // SAFETY: main thread; ownership returns from the adapter.
            drop(unsafe { Box::from_raw(editor) });
        }
    }
}
