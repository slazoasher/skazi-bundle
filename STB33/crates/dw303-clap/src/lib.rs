//! DW-303 CLAP instrument: stereo output, MIDI note input, DW-303 parameters
//! and parameter state, with an optional embedded macOS editor.
use clack_extensions::{audio_ports::*, note_ports::*, params::*, state::*, tail::*};
use clack_plugin::{events::event_types::ParamValueEvent, prelude::*, utils::Cookie};
use dw303_engine::{
    Dw303Engine,
    link::{Memory, PanelLink},
    params,
};
use ravet_clap::{MAXIMUM_EVENTS, MainBusBridge};
use ravet_foundation::{AudioLayout, ParameterId, ParameterState, ProcessConfig};
use std::sync::Arc;
use std::{
    ffi::CStr,
    fmt::Write,
    io::{Read, Write as IoWrite},
    sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering},
};

mod editor;
pub use editor::EditError;
#[cfg(all(
    feature = "native-editor",
    any(target_os = "macos", target_os = "windows", target_os = "linux")
))]
#[allow(unsafe_code)]
mod gui;

/// Stable CLAP identifier used by hosts and saved sessions.
pub const PLUGIN_ID: &str = "com.asherswissa.stb33";
const STATE_PRODUCT: [u8; 8] = *b"ASHSTB33";
const STATE_VERSION: u32 = 2;
const PARAM_BYTES: usize = 16 + params::COUNT * 8;

/// DW-303 CLAP factory and extension declaration.
pub struct Dw303Plugin;

impl Plugin for Dw303Plugin {
    type AudioProcessor<'a> = Dw303Audio<'a>;
    type Shared<'a> = Dw303Shared;
    type MainThread<'a> = Dw303Main<'a>;
    fn declare_extensions(builder: &mut PluginExtensions<Self>, _: Option<&Dw303Shared>) {
        builder
            .register::<PluginAudioPorts>()
            .register::<PluginNotePorts>()
            .register::<PluginParams>()
            .register::<PluginState>()
            .register::<PluginTail>();
        #[cfg(all(
            feature = "native-editor",
            any(target_os = "macos", target_os = "windows", target_os = "linux")
        ))]
        builder.register::<clack_extensions::gui::PluginGui>();
        // Linux editors run GTK work from the host's UI timer.
        #[cfg(all(feature = "native-editor", target_os = "linux"))]
        builder.register::<clack_extensions::timer::PluginTimer>();
    }
}

impl DefaultPluginFactory for Dw303Plugin {
    fn get_descriptor() -> PluginDescriptor {
        use clack_plugin::plugin::features::{INSTRUMENT, MONO, SYNTHESIZER};
        PluginDescriptor::new(PLUGIN_ID, "STB33")
            .with_vendor("SKAZI")
            .with_version(env!("CARGO_PKG_VERSION"))
            .with_description("Circuit-modelled bass line synthesizer")
            .with_features([INSTRUMENT, SYNTHESIZER, MONO])
    }
    fn new_shared(_: HostSharedHandle<'_>) -> Result<Dw303Shared, PluginError> {
        Ok(Dw303Shared {
            link: Arc::new(PanelLink::new()),
            edits: editor::Edits::default(),
            values: params::DESCRIPTORS.map(|d| AtomicU64::new(d.default.to_bits() as u64)),
            generation: AtomicU32::new(0),
            fault: AtomicU32::new(0),
            parameter_refresh: AtomicBool::new(false),
        })
    }
    fn new_main_thread<'a>(
        host: HostMainThreadHandle<'a>,
        shared: &'a Dw303Shared,
    ) -> Result<Dw303Main<'a>, PluginError> {
        Ok(Dw303Main {
            host,
            shared,
            #[cfg(all(
                feature = "native-editor",
                any(target_os = "macos", target_os = "windows", target_os = "linux")
            ))]
            gui: std::cell::RefCell::new(None),
        })
    }
}

/// Parameter values shared between threads. A state restore bumps
/// `generation`; the audio thread then re-sends every value to the engine.
pub struct Dw303Shared {
    link: Arc<PanelLink>,
    edits: editor::Edits,
    values: [AtomicU64; params::COUNT],
    generation: AtomicU32,
    fault: AtomicU32,
    parameter_refresh: AtomicBool,
}

impl PluginShared<'_> for Dw303Shared {}

impl Dw303Shared {
    fn value(&self, index: usize) -> f32 {
        f32::from_bits(self.values[index].load(Ordering::Acquire) as u32)
    }
    fn store(&self, index: usize, value: f32) -> bool {
        let generation = self.generation.load(Ordering::Acquire);
        let word = ((generation as u64) << 32) | value.to_bits() as u64;
        self.values[index].swap(word, Ordering::AcqRel) as u32 != value.to_bits()
    }
    /// Publish restore-tagged values before admitting the new audio generation.
    fn restore_values(&self, values: [f32; params::COUNT]) {
        let generation = self.generation.load(Ordering::Acquire).wrapping_add(1);
        for (slot, value) in self.values.iter().zip(values) {
            slot.store(
                ((generation as u64) << 32) | value.to_bits() as u64,
                Ordering::Release,
            );
        }
        self.generation.store(generation, Ordering::Release);
    }
    /// One attempt per field: a concurrent restore always wins.
    fn reflect_value(&self, index: usize, value: f32, generation: u32) -> bool {
        let old = self.values[index].load(Ordering::Acquire);
        if (old >> 32) as u32 != generation {
            return false;
        }
        let next = ((generation as u64) << 32) | value.to_bits() as u64;
        old != next
            && self.values[index]
                .compare_exchange(old, next, Ordering::AcqRel, Ordering::Acquire)
                .is_ok()
    }
    /// Last recorded audio callback fault (zero means success).
    pub fn fault_code(&self) -> u32 {
        self.fault.load(Ordering::Relaxed)
    }
}

fn index_of(id: ClapId) -> Option<usize> {
    params::DESCRIPTORS
        .iter()
        .position(|d| d.id == ParameterId(id.get()))
}

/// Main-thread parameter/state owner and optional embedded editor.
pub struct Dw303Main<'a> {
    host: HostMainThreadHandle<'a>,
    shared: &'a Dw303Shared,
    #[cfg(all(
        feature = "native-editor",
        any(target_os = "macos", target_os = "windows", target_os = "linux")
    ))]
    gui: std::cell::RefCell<Option<gui::GuiState<'a>>>,
}

fn queue_edit(shared: &Dw303Shared, id: ParameterId, value: f32) -> Result<(), EditError> {
    let i = index_of(ClapId::new(id.0)).ok_or(EditError::UnknownParameter)?;
    let value = params::DESCRIPTORS[i]
        .range
        .canonicalize(value)
        .ok_or(EditError::InvalidValue)?;
    shared.edits.push(
        ClapId::new(id.0),
        value,
        shared.generation.load(Ordering::Acquire),
    )
}
impl Dw303Main<'_> {
    /// Queue one physical panel edge or selector; retry unchanged on `Full`.
    pub fn panel_action(
        &self,
        action: dw303_engine::link::PanelAction,
    ) -> Result<(), dw303_engine::link::Full> {
        self.shared.link.push(action)?;
        self.host.request_process();
        Ok(())
    }
    fn request_edit_flush(&self) {
        if let Some(params) = self.host.get_extension::<HostParams>() {
            params.request_flush(&self.host);
        } else {
            self.host.request_process();
        }
    }
    /// Begin one continuous parameter gesture; only one can be open.
    pub fn begin_parameter_gesture(&self, id: ParameterId) -> Result<(), EditError> {
        index_of(ClapId::new(id.0)).ok_or(EditError::UnknownParameter)?;
        self.shared.edits.begin(
            ClapId::new(id.0),
            self.shared.generation.load(Ordering::Acquire),
        )?;
        self.request_edit_flush();
        Ok(())
    }
    /// Queue a finite, canonical physical value for the current gesture.
    pub fn update_parameter_gesture(&self, id: ParameterId, value: f32) -> Result<(), EditError> {
        let i = index_of(ClapId::new(id.0)).ok_or(EditError::UnknownParameter)?;
        let value = params::DESCRIPTORS[i]
            .range
            .canonicalize(value)
            .ok_or(EditError::InvalidValue)?;
        self.shared.edits.update(ClapId::new(id.0), value)?;
        self.request_edit_flush();
        Ok(())
    }
    /// End the current gesture using its reserved queue slot.
    pub fn end_parameter_gesture(&self) -> Result<(), EditError> {
        self.shared.edits.end()?;
        self.request_edit_flush();
        Ok(())
    }
    /// Submit a complete host-visible begin/value/end parameter edit.
    pub fn edit_parameter(&self, id: ParameterId, value: f32) -> Result<(), EditError> {
        queue_edit(self.shared, id, value)?;
        self.request_edit_flush();
        Ok(())
    }
}

impl<'a> PluginMainThread<'a, Dw303Shared> for Dw303Main<'a> {
    fn on_main_thread(&self) {
        if self.shared.parameter_refresh.swap(false, Ordering::AcqRel)
            && let Some(params) = self.host.get_extension::<HostParams>()
        {
            params.rescan(&self.host, ParamRescanFlags::VALUES);
        }
    }
}

impl PluginAudioPortsImpl for Dw303Main<'_> {
    fn count(&self, input: bool) -> u32 {
        u32::from(!input)
    }
    fn get(&self, index: u32, input: bool, writer: &mut AudioPortInfoWriter) {
        if !input && index == 0 {
            writer.set(&AudioPortInfo {
                id: ClapId::new(0),
                name: b"Output",
                channel_count: 2,
                flags: AudioPortFlags::IS_MAIN,
                port_type: Some(AudioPortType::STEREO),
                in_place_pair: None,
            });
        }
    }
}

impl PluginNotePortsImpl for Dw303Main<'_> {
    fn count(&self, input: bool) -> u32 {
        u32::from(input)
    }
    fn get(&self, index: u32, input: bool, writer: &mut NotePortInfoWriter) {
        if input && index == 0 {
            writer.set(&NotePortInfo {
                id: ClapId::new(0),
                name: b"MIDI input",
                supported_dialects: NoteDialects::MIDI,
                preferred_dialect: Some(NoteDialect::Midi),
            });
        }
    }
}

impl PluginMainThreadParams for Dw303Main<'_> {
    fn count(&self) -> u32 {
        params::COUNT as u32
    }
    fn get_info(&self, index: u32, writer: &mut ParamInfoWriter) {
        let Some(d) = params::DESCRIPTORS.get(index as usize) else {
            return;
        };
        let mut flags = ParamInfoFlags::empty();
        if d.automatable {
            flags |= ParamInfoFlags::IS_AUTOMATABLE;
        }
        if d.range.intervals().is_some() {
            flags |= ParamInfoFlags::IS_STEPPED;
        }
        writer.set(&ParamInfo {
            id: ClapId::new(d.id.0),
            flags,
            cookie: Cookie::empty(),
            name: d.name.as_bytes(),
            module: b"",
            min_value: f64::from(d.range.minimum()),
            max_value: f64::from(d.range.maximum()),
            default_value: f64::from(d.default),
        });
    }
    fn get_value(&self, id: ClapId) -> Option<f64> {
        index_of(id).map(|i| f64::from(self.shared.value(i)))
    }
    fn value_to_text(
        &self,
        id: ClapId,
        value: f64,
        writer: &mut ParamDisplayWriter,
    ) -> std::fmt::Result {
        if !value.is_finite() {
            return Err(std::fmt::Error);
        }
        match ParameterId(id.get()) {
            params::TEMPO => write!(writer, "{value:.1} BPM"),
            params::RUN => writer.write_str(if value < 0.5 { "Stopped" } else { "Running" }),
            params::WAVEFORM => writer.write_str(if value < 0.5 { "Saw" } else { "Square" }),
            params::SYNC_MODE => writer.write_str(match value.round() as i32 {
                0 => "Internal",
                1 => "Host",
                _ => "MIDI clock",
            }),
            params::QUALITY => writer.write_str(if value < 0.5 { "Economy" } else { "Full" }),
            _ if index_of(id).is_some() => write!(writer, "{:.1}", value * 10.0),
            _ => Err(std::fmt::Error),
        }
    }
    fn text_to_value(&self, id: ClapId, text: &CStr) -> Option<f64> {
        let d = params::DESCRIPTORS[index_of(id)?];
        let text = text.to_str().ok()?.trim();
        let choice = match (d.id, text) {
            (params::RUN, "Stopped")
            | (params::WAVEFORM, "Saw")
            | (params::SYNC_MODE, "Internal")
            | (params::QUALITY, "Economy") => Some(0.0),
            (params::RUN, "Running")
            | (params::WAVEFORM, "Square")
            | (params::SYNC_MODE, "Host")
            | (params::QUALITY, "Full") => Some(1.0),
            (params::SYNC_MODE, "MIDI clock") => Some(2.0),
            _ => None,
        };
        if let Some(choice) = choice {
            return Some(choice);
        }
        let number: f64 = text.trim_end_matches("BPM").trim().parse().ok()?;
        // Knobs display 0–10 like the panel legend.
        let value = if d.unit.is_empty() && d.range.intervals().is_none() {
            number / 10.0
        } else {
            number
        };
        d.range.canonicalize(value as f32).map(f64::from)
    }
    fn flush(&self, input: &InputEvents, output: &mut OutputEvents) {
        let mut changed = false;
        for event in input {
            if let Some(event) = event.as_event::<ParamValueEvent>()
                && let Some(id) = event.param_id()
                && let Some(index) = index_of(id)
                && let Some(value) = params::DESCRIPTORS[index]
                    .range
                    .canonicalize(event.value() as f32)
            {
                changed |= self.shared.store(index, value);
            }
        }
        if changed {
            self.shared
                .restore_values(core::array::from_fn(|i| self.shared.value(i)));
        }
        let generation = self.shared.generation.load(Ordering::Acquire);
        let pending = self.shared.edits.drain(
            output,
            generation,
            || self.shared.generation.load(Ordering::Acquire),
            |id, value| {
                if let Some(i) = index_of(id) {
                    self.shared.store(i, value);
                }
            },
        );
        if pending {
            self.request_edit_flush();
        }
    }
}

impl PluginStateImpl for Dw303Main<'_> {
    fn save(&self, output: &mut clack_plugin::stream::OutputStream) -> Result<(), PluginError> {
        let values = core::array::from_fn(|i| self.shared.value(i));
        let bytes = encode_state(values, &self.shared.link.memory())?;
        output.write_all(&bytes)?;
        Ok(())
    }
    fn load(&self, input: &mut clack_plugin::stream::InputStream) -> Result<(), PluginError> {
        let mut bytes = Vec::new();
        input.take(64 * 1024).read_to_end(&mut bytes)?;
        let (values, memory) = decode_state(&bytes)?;
        self.shared.link.load_memory(memory);
        self.shared.restore_values(values);
        self.host.request_process();
        if let Some(params) = self.host.get_extension::<HostParams>() {
            params.rescan(&self.host, ParamRescanFlags::VALUES);
        }
        Ok(())
    }
}

fn encode_state(values: [f32; params::COUNT], memory: &Memory) -> Result<Vec<u8>, PluginError> {
    let schema = params::schema().map_err(|_| PluginError::Message("schema"))?;
    let state = schema
        .state(values)
        .map_err(|_| PluginError::Message("parameter state"))?;
    let mut bytes = vec![0; PARAM_BYTES + 4 + Memory::ENCODED_SIZE];
    state
        .encode(STATE_PRODUCT, STATE_VERSION, &mut bytes[..PARAM_BYTES])
        .map_err(|_| PluginError::Message("state encode"))?;
    bytes[PARAM_BYTES..PARAM_BYTES + 4]
        .copy_from_slice(&(Memory::ENCODED_SIZE as u32).to_le_bytes());
    memory
        .encode(&mut bytes[PARAM_BYTES + 4..])
        .map_err(|_| PluginError::Message("memory encode"))?;
    Ok(bytes)
}
fn decode_state(bytes: &[u8]) -> Result<([f32; params::COUNT], Memory), PluginError> {
    let invalid = || PluginError::Message("invalid DW-303 state");
    let schema = params::schema().map_err(|_| invalid())?;
    if bytes.len() == PARAM_BYTES {
        let state =
            ParameterState::decode(schema, STATE_PRODUCT, 1, bytes).map_err(|_| invalid())?;
        return Ok((*state.values(), dw303_engine::factory_memory()));
    }
    if bytes.len() != PARAM_BYTES + 4 + Memory::ENCODED_SIZE {
        return Err(invalid());
    }
    let state = ParameterState::decode(schema, STATE_PRODUCT, STATE_VERSION, &bytes[..PARAM_BYTES])
        .map_err(|_| invalid())?;
    let length = u32::from_le_bytes(
        bytes[PARAM_BYTES..PARAM_BYTES + 4]
            .try_into()
            .map_err(|_| invalid())?,
    ) as usize;
    if length != Memory::ENCODED_SIZE {
        return Err(invalid());
    }
    let memory = Memory::decode(&bytes[PARAM_BYTES + 4..]).map_err(|_| invalid())?;
    Ok((*state.values(), memory))
}

/// Audio-thread engine bridge and bounded editor-event consumer.
pub struct Dw303Audio<'a> {
    host: HostAudioProcessorHandle<'a>,
    shared: &'a Dw303Shared,
    bridge: MainBusBridge<Dw303Engine>,
    generation: u32,
}

impl Dw303Audio<'_> {
    /// After a restore, send every stored value to the engine.
    fn synchronize(&mut self) {
        let generation = self.shared.generation.load(Ordering::Acquire);
        if generation == self.generation {
            return;
        }
        let events: [ParamValueEvent; params::COUNT] = core::array::from_fn(|i| {
            ParamValueEvent::new(
                0,
                ClapId::new(params::DESCRIPTORS[i].id.0),
                Pckn::match_all(),
                f64::from(self.shared.value(i)),
            )
        });
        let result = self.bridge.flush(&InputEvents::from_buffer(&events));
        debug_assert!(result.is_ok());
        self.bridge.reset();
        self.generation = generation;
    }

    /// Publish automation applied on the audio thread back to the host view.
    fn reflect(&self) {
        if self.shared.generation.load(Ordering::Acquire) != self.generation {
            return;
        }
        let values = self.bridge.processor().parameters().values();
        let mut changed = false;
        for (i, value) in values.iter().enumerate() {
            changed |= self.shared.reflect_value(i, *value, self.generation);
        }
        if changed && !self.shared.parameter_refresh.swap(true, Ordering::AcqRel) {
            self.host.request_callback();
        }
    }

    fn editor_events(&mut self, output: &mut OutputEvents<'_>) {
        let shared = self.shared;
        let bridge = &mut self.bridge;
        let pending = shared.edits.drain(
            output,
            self.generation,
            || shared.generation.load(Ordering::Acquire),
            |id, value| {
                let event = [ParamValueEvent::new(0, id, Pckn::match_all(), value as f64)];
                let result = bridge.flush(&InputEvents::from_buffer(&event));
                debug_assert!(result.is_ok());
            },
        );
        self.reflect();
        if pending {
            if let Some(params) = self.host.get_extension::<HostParams>() {
                params.request_flush(&self.host);
            } else {
                self.host.request_process();
            }
        }
    }

    fn record(&self, ok: bool) {
        self.shared.fault.store(u32::from(!ok), Ordering::Relaxed);
    }
}

impl<'a> PluginAudioProcessor<'a, Dw303Shared, Dw303Main<'a>> for Dw303Audio<'a> {
    fn activate(
        host: HostAudioProcessorHandle<'a>,
        _: &Dw303Main<'a>,
        shared: &'a Dw303Shared,
        config: PluginAudioConfiguration,
    ) -> Result<Self, PluginError> {
        let config = ProcessConfig::new(
            config.sample_rate,
            config.max_frames_count as usize,
            MAXIMUM_EVENTS,
            AudioLayout::STEREO_INSTRUMENT,
        )?;
        let bridge =
            MainBusBridge::prepare(Dw303Engine::with_link(shared.link.clone()), config, Some(0))
                .map_err(|_| PluginError::Message("invalid DW-303 activation"))?;
        let mut audio = Self {
            host,
            shared,
            bridge,
            generation: shared.generation.load(Ordering::Acquire).wrapping_sub(1),
        };
        audio.synchronize();
        Ok(audio)
    }
    fn process(
        &mut self,
        process: Process,
        mut audio: Audio,
        events: Events,
    ) -> Result<ProcessStatus, PluginError> {
        self.synchronize();
        self.editor_events(events.output);
        let result = self
            .bridge
            .process(&mut audio, events.input, process.transport);
        if result.is_ok() {
            self.reflect();
        }
        self.record(result.is_ok());
        Ok(ProcessStatus::Continue)
    }
    fn reset(&mut self) {
        self.synchronize();
        self.bridge.reset();
    }
    fn deactivate(mut self, _: &Dw303Main<'a>) {
        self.bridge.deactivate();
    }
}

impl PluginAudioProcessorParams for Dw303Audio<'_> {
    fn flush(&mut self, input: &InputEvents, output: &mut OutputEvents) {
        self.synchronize();
        self.editor_events(output);
        let result = self.bridge.flush(input);
        if result.is_ok() {
            self.reflect();
        }
        self.record(result.is_ok());
    }
}

impl PluginTailImpl for Dw303Audio<'_> {
    fn get(&self) -> TailLength {
        TailLength::Infinite
    }
}

#[allow(unsafe_code)]
mod export {
    clack_plugin::clack_export_entry!(clack_plugin::entry::SinglePluginEntry<super::Dw303Plugin>);
}

#[cfg(test)]
mod tests {
    use super::*;
    fn shared() -> Dw303Shared {
        Dw303Shared {
            link: Arc::new(PanelLink::new()),
            edits: editor::Edits::default(),
            values: params::DESCRIPTORS.map(|d| AtomicU64::new(d.default.to_bits() as u64)),
            generation: AtomicU32::new(0),
            fault: AtomicU32::new(0),
            parameter_refresh: AtomicBool::new(false),
        }
    }
    #[test]
    fn every_parameter_round_trips_through_host_events_and_state_codec() {
        let shared = shared();
        for (i, descriptor) in params::DESCRIPTORS.iter().enumerate() {
            queue_edit(&shared, descriptor.id, descriptor.range.maximum()).unwrap();
            let mut sink = editor::tests::Sink::new(128);
            assert!(!shared.edits.drain(
                &mut OutputEvents::from_buffer(&mut sink),
                0,
                || 0,
                |id, value| {
                    shared.store(index_of(id).unwrap(), value);
                }
            ));
            assert_eq!(sink.length, 3);
            assert_eq!(sink.events[0], (0, ClapId::new(descriptor.id.0), 0.0));
            assert_eq!(
                sink.events[1],
                (1, ClapId::new(descriptor.id.0), descriptor.range.maximum())
            );
            assert_eq!(sink.events[2], (2, ClapId::new(descriptor.id.0), 0.0));
            assert_eq!(shared.value(i), descriptor.range.maximum());
        }
        let state = params::schema()
            .unwrap()
            .state(core::array::from_fn(|i| shared.value(i)))
            .unwrap();
        let mut bytes = [0; 16 + params::COUNT * 8];
        state
            .encode(STATE_PRODUCT, STATE_VERSION, &mut bytes)
            .unwrap();
        let restored = ParameterState::decode(
            params::schema().unwrap(),
            STATE_PRODUCT,
            STATE_VERSION,
            &bytes,
        )
        .unwrap();
        assert_eq!(restored.values(), state.values());
    }
    #[test]
    fn state_v2_roundtrip_v1_factory_and_corruption() {
        let values = params::DESCRIPTORS.map(|d| d.default);
        let mut memory = Memory::default();
        memory.bank.patterns[7].steps[2].pitch = 9;
        let bytes = encode_state(values, &memory).unwrap();
        assert_eq!(decode_state(&bytes).unwrap(), (values, memory));
        let mut v1 = [0; PARAM_BYTES];
        params::schema()
            .unwrap()
            .state(values)
            .unwrap()
            .encode(STATE_PRODUCT, 1, &mut v1)
            .unwrap();
        assert_eq!(
            decode_state(&v1).unwrap(),
            (values, dw303_engine::factory_memory())
        );
        for length in [0, PARAM_BYTES - 1, PARAM_BYTES + 3, bytes.len() - 1] {
            assert!(decode_state(&bytes[..length]).is_err());
        }
        let mut bad = bytes.clone();
        bad[PARAM_BYTES] = 0;
        assert!(decode_state(&bad).is_err());
        let mut bad = bytes;
        bad[PARAM_BYTES + 4] = 0;
        assert!(decode_state(&bad).is_err());
    }
    #[test]
    fn invalid_edits_leave_queue_and_parameters_unchanged() {
        let shared = shared();
        assert_eq!(
            queue_edit(&shared, ParameterId(99), 0.5),
            Err(EditError::UnknownParameter)
        );
        for value in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY] {
            assert_eq!(
                queue_edit(&shared, params::CUTOFF, value),
                Err(EditError::InvalidValue)
            );
        }
        assert!(!shared.edits.pending());
        assert_eq!(shared.value(1), 0.5);
    }
    #[test]
    fn restore_tag_prevents_stale_reflection_before_and_after_publication() {
        let shared = shared();
        // A restore has written this field but has not published its generation.
        shared.values[0].store((1_u64 << 32) | 0.25_f32.to_bits() as u64, Ordering::Release);
        assert!(!shared.reflect_value(0, 0.9, 0));
        shared.restore_values([0.75; params::COUNT]);
        assert!(!shared.reflect_value(0, 0.9, 0));
        assert_eq!(shared.value(0), 0.75);
    }
}
