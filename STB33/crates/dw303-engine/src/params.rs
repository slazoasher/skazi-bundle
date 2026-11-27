//! Stable DW-303 parameter IDs and schema. IDs are permanent: never renumber
//! or reuse them, because hosts and saved sessions refer to them.
use ravet_foundation::{
    ParameterDescriptor, ParameterError, ParameterId, ParameterRange, ParameterScale,
    ParameterSchema,
};

pub const TUNING: ParameterId = ParameterId(1);
pub const CUTOFF: ParameterId = ParameterId(2);
pub const RESONANCE: ParameterId = ParameterId(3);
pub const ENV_MOD: ParameterId = ParameterId(4);
pub const DECAY: ParameterId = ParameterId(5);
pub const ACCENT: ParameterId = ParameterId(6);
pub const VOLUME: ParameterId = ParameterId(7);
pub const TEMPO: ParameterId = ParameterId(8);
pub const WAVEFORM: ParameterId = ParameterId(9);
pub const SYNC_MODE: ParameterId = ParameterId(10);
pub const QUALITY: ParameterId = ParameterId(11);
pub const RUN: ParameterId = ParameterId(12);
// STB33 additions: ADSR amp envelope and the FX chain.
pub const ATTACK: ParameterId = ParameterId(13);
pub const AMP_DECAY: ParameterId = ParameterId(14);
pub const SUSTAIN: ParameterId = ParameterId(15);
pub const RELEASE: ParameterId = ParameterId(16);
pub const DRIVE: ParameterId = ParameterId(17);
pub const OTT: ParameterId = ParameterId(18);
pub const REVERB: ParameterId = ParameterId(19);
pub const WIDTH: ParameterId = ParameterId(20);
pub const GLIDE: ParameterId = ParameterId(21);
pub const PITCH_GLIDE: ParameterId = ParameterId(22);

pub const COUNT: usize = 22;

/// Knobs are panel rotation fractions (0 = fully counter-clockwise); the
/// circuit crates apply the pot tapers themselves.
const KNOB: ParameterRange = range(0.0, 1.0, None);

const fn range(min: f32, max: f32, intervals: Option<u16>) -> ParameterRange {
    match ParameterRange::new(min, max, ParameterScale::Linear, intervals) {
        Ok(r) => r,
        Err(_) => panic!("invalid parameter range"),
    }
}

const fn knob(id: ParameterId, name: &'static str, default: f32) -> ParameterDescriptor<'static> {
    ParameterDescriptor {
        id,
        name,
        unit: "",
        range: KNOB,
        default,
        automatable: true,
    }
}

pub static DESCRIPTORS: [ParameterDescriptor<'static>; COUNT] = [
    knob(TUNING, "Tuning", 0.5),
    knob(CUTOFF, "Cut Off Freq", 0.5),
    knob(RESONANCE, "Resonance", 0.5),
    knob(ENV_MOD, "Env Mod", 0.5),
    knob(DECAY, "Decay", 0.5),
    knob(ACCENT, "Accent", 0.5),
    knob(VOLUME, "Volume", 0.7),
    ParameterDescriptor {
        id: TEMPO,
        name: "Tempo",
        unit: "BPM",
        // VERIFY: the original TEMPO knob range; host/MIDI sync ignore it.
        range: range(40.0, 300.0, None),
        default: 120.0,
        automatable: true,
    },
    ParameterDescriptor {
        id: WAVEFORM,
        name: "Waveform",
        unit: "",
        range: range(0.0, 1.0, Some(1)), // 0 = saw, 1 = square
        default: 0.0,
        automatable: true,
    },
    ParameterDescriptor {
        id: SYNC_MODE,
        name: "Sync",
        unit: "",
        range: range(0.0, 2.0, Some(2)), // 0 = internal, 1 = host, 2 = MIDI clock
        default: 1.0,
        automatable: false,
    },
    ParameterDescriptor {
        id: QUALITY,
        name: "Quality",
        unit: "",
        range: range(0.0, 1.0, Some(1)), // 0 = economy, 1 = full circuit
        default: 1.0,
        automatable: false,
    },
    ParameterDescriptor {
        id: RUN,
        name: "Run",
        unit: "",
        // Starts/stops the internal clock; host and MIDI sync follow their transport.
        range: range(0.0, 1.0, Some(1)),
        default: 0.0,
        automatable: true,
    },
    knob(ATTACK, "Attack", 0.0),
    knob(AMP_DECAY, "Amp Decay", 0.5),
    knob(SUSTAIN, "Sustain", 1.0),
    knob(RELEASE, "Release", 0.0),
    knob(DRIVE, "Distortion", 0.0),
    knob(OTT, "OTT", 0.0),
    knob(REVERB, "Reverb", 0.0),
    knob(WIDTH, "Width", 0.0),
    knob(GLIDE, "Glide", 0.5),
    knob(PITCH_GLIDE, "Pitch Glide", 0.0),
];

pub fn schema() -> Result<ParameterSchema<'static, COUNT>, ParameterError> {
    ParameterSchema::new(&DESCRIPTORS)
}

#[cfg(test)]
mod tests {
    #[test]
    fn schema_is_valid() {
        super::schema().expect("valid DW-303 schema");
    }
}
