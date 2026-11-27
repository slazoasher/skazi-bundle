//! The complete DW-303 signal path, following the TB-303 block diagram:
//!
//! ```text
//! PitchCv -> Vco -> WAVEFORM -> Vcf --(output + resonance tap)--> Vca -> OutputStage
//! TriggerPulse -> Meg -> Accent (VR7) -> AccentSweep (VR4B) -> FilterControl -^
//!              -> Veg (C42/Q31) --------------------------------------------^ (Vca)
//! ```
//!
//! Wiring order follows `docs/circuit/env.md` ("integration order").
//!
//! The circuit runs at an oversampled rate (1x/2x/4x/8x, chosen from the
//! host rate by [`oversampling_for`]) and is decimated with f64 elliptic
//! half-band stages.
use dw303_env::{
    Accent, AccentSweep, FilterControl, Meg, OutputStage, TriggerPulse, Vca, VcfSignals, Veg,
};
use dw303_osc::{PitchCv, Vco};
use dw303_vcf::{Quality, Vcf};
use ravet_signal::{ConfiguredHalfBand2x, HalfBandIirDesign};

/// Preferred minimum internal circuit rate (4x at 44.1 kHz).
pub const MIN_CIRCUIT_RATE: f64 = 176_400.0;
/// Highest internal rate the circuit stages accept.
pub const MAX_CIRCUIT_RATE: f64 = 384_000.0;
/// Lowest internal rate the circuit stages accept.
const STAGE_MIN_RATE: f64 = 44_100.0;
/// Up to 64x oversampling (hosts down to 1 kHz).
const MAX_DECIMATION_STAGES: usize = 6;

/// How the circuit rate relates to the host rate.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RateRatio {
    /// Circuit runs n times faster (n = 1..=64, power of two) and is decimated.
    Over(usize),
    /// Circuit runs n times slower (hosts above 384 kHz); its output is
    /// linearly interpolated, so content near the host Nyquist is imaged far
    /// above the audio band.
    Under(usize),
}

/// Circuit/host rate ratio: the smallest oversampling reaching
/// `MIN_CIRCUIT_RATE` within the stages' accepted range, else the nearest
/// accepted rate. Returns `None` only for non-finite or non-positive rates.
pub fn oversampling_for(host_rate: f64) -> Option<RateRatio> {
    if !(host_rate.is_finite() && host_rate > 0.0) {
        return None;
    }
    let valid = |rate: f64| (STAGE_MIN_RATE..=MAX_CIRCUIT_RATE).contains(&rate);
    let factors = [1, 2, 4, 8, 16, 32, 64];
    if let Some(&f) = factors
        .iter()
        .find(|&&f| valid(host_rate * f as f64) && host_rate * f as f64 >= MIN_CIRCUIT_RATE)
        .or_else(|| factors.iter().rev().find(|&&f| valid(host_rate * f as f64)))
    {
        return Some(RateRatio::Over(f));
    }
    [2, 4, 8, 16]
        .into_iter()
        .find(|&n| valid(host_rate / n as f64))
        .map(RateRatio::Under)
}

/// Highest VCO harmonic frequency synthesised (above audibility, below the
/// decimator's stopband at 44.1 kHz host rates).
pub const VCO_BAND_LIMIT_HZ: f64 = 20_000.0;

/// Output node voltage that maps to digital full scale (1.0).
/// VERIFY: session1 overall-RMS fit to TD-3 / UMC1820 at unity gain.
/// Round 1 used 11.595 V after a +7.10 dB baseline correction; round 2
/// used 10.334 V. Round 3 adds 0.25 dB after the physical envelope fit,
/// minimizing the full Harness-v2 objective with one session-wide gain.
/// Round 4 uses 10.192 V (about −0.13 dB) after the Harness-v3 timing audit;
/// see docs/circuit/env.md for unchanged-model v2/v3 scores and residuals.
/// This calibrates the recording chain, not a TB-303 component value.
/// VERIFY: absolute volts are unknown (no interface dBFS-per-volt calibration).
pub const FULL_SCALE_VOLTS: f64 = 10.192;

/// Knob positions as panel rotation fractions (0 = fully counter-clockwise).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Knobs {
    pub tuning: f64,
    pub cutoff: f64,
    pub resonance: f64,
    pub env_mod: f64,
    pub decay: f64,
    pub accent: f64,
    pub volume: f64,
    pub square: bool,
}

impl Knobs {
    fn all_finite(self) -> bool {
        [
            self.tuning,
            self.cutoff,
            self.resonance,
            self.env_mod,
            self.decay,
            self.accent,
            self.volume,
        ]
        .into_iter()
        .all(f64::is_finite)
    }
}

impl Default for Knobs {
    fn default() -> Self {
        Self {
            tuning: 0.5,
            cutoff: 0.5,
            resonance: 0.5,
            env_mod: 0.5,
            decay: 0.5,
            accent: 0.5,
            volume: 0.7,
            square: false,
        }
    }
}

/// The sequencer's control outputs for the current host sample.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct VoiceInput {
    /// Semitone index from the sequencer DAC (0 = lowest C).
    pub note: u8,
    pub gate: bool,
    /// True only on the host sample where a new (non-slid) note begins.
    pub trigger: bool,
    pub accent: bool,
    pub slide: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum VoiceError {
    InvalidSampleRate,
}

/// Knob smoothing time; the original pots have no zipper, so host steps are
/// slewed before they reach the circuit.
const KNOB_SMOOTHING_SECONDS: f64 = 0.005;

pub struct Voice {
    pitch: PitchCv,
    vco: Vco,
    vcf: Vcf,
    trigger: TriggerPulse,
    meg: Meg,
    veg: Veg,
    accent: Accent,
    sweep: AccentSweep,
    filter_control: FilterControl,
    vca: Vca,
    output: OutputStage,
    decimators: [ConfiguredHalfBand2x; MAX_DECIMATION_STAGES],
    ratio: RateRatio,
    /// Undersampled mode: host samples since the last circuit sample, the
    /// last two circuit outputs, and a trigger latched until the next tick.
    under_phase: usize,
    under_previous: f64,
    under_current: f64,
    under_trigger: bool,
    target: Knobs,
    knobs: Knobs,
    knob_coefficient: f64,
    knobs_settled: bool,
}

impl Voice {
    pub fn new() -> Self {
        // 4x: each stage passes the audio band with >100 dB stopband.
        let design =
            HalfBandIirDesign::from_attenuation(0.1, 110.0).expect("valid half-band design");
        Self {
            pitch: PitchCv::new(dw303_osc::Components::NOMINAL),
            vco: Vco::new(dw303_osc::Components::NOMINAL),
            vcf: Vcf::new(dw303_vcf::Components::NOMINAL),
            trigger: TriggerPulse::new(dw303_env::Components::NOMINAL),
            meg: Meg::new(dw303_env::Components::NOMINAL),
            veg: Veg::new(dw303_env::Components::NOMINAL),
            accent: Accent::new(dw303_env::Components::NOMINAL),
            sweep: AccentSweep::new(dw303_env::Components::NOMINAL),
            filter_control: FilterControl::new(calibrated_env_components()),
            vca: Vca::new(dw303_env::Components::NOMINAL),
            output: OutputStage::new(dw303_env::Components::NOMINAL),
            decimators: core::array::from_fn(|_| ConfiguredHalfBand2x::new(design, design)),
            ratio: RateRatio::Over(4),
            under_phase: 0,
            under_previous: 0.0,
            under_current: 0.0,
            under_trigger: false,
            target: Knobs::default(),
            knobs: Knobs::default(),
            knob_coefficient: 0.0,
            knobs_settled: true,
        }
    }

    pub fn prepare(&mut self, host_sample_rate: f64) -> Result<(), VoiceError> {
        if !host_sample_rate.is_finite() {
            return Err(VoiceError::InvalidSampleRate);
        }
        let ratio = oversampling_for(host_sample_rate).ok_or(VoiceError::InvalidSampleRate)?;
        let rate = match ratio {
            RateRatio::Over(n) => host_sample_rate * n as f64,
            RateRatio::Under(n) => host_sample_rate / n as f64,
        };
        let ok = self.pitch.prepare(rate).is_ok()
            && self.vco.prepare(rate).is_ok()
            && self.vcf.prepare(rate).is_ok()
            && self.trigger.prepare(rate).is_ok()
            && self.meg.prepare(rate).is_ok()
            && self.veg.prepare(rate).is_ok()
            && self.accent.prepare(rate).is_ok()
            && self.sweep.prepare(rate).is_ok()
            && self.filter_control.prepare(rate).is_ok()
            && self.vca.prepare(rate).is_ok()
            && self.output.prepare(rate).is_ok();
        if !ok {
            return Err(VoiceError::InvalidSampleRate);
        }
        self.ratio = ratio;
        // Harmonics above the host band are removed by the decimator anyway.
        self.vco
            .set_band_limit(Some((0.45 * host_sample_rate).min(VCO_BAND_LIMIT_HZ)));
        self.knob_coefficient = 1.0 - (-1.0 / (KNOB_SMOOTHING_SECONDS * rate)).exp();
        self.reset();
        Ok(())
    }

    pub fn reset(&mut self) {
        self.pitch.reset();
        self.vco.reset();
        self.vcf.reset();
        self.trigger.reset();
        self.meg.reset();
        self.veg.reset();
        self.accent.reset();
        self.sweep.reset();
        self.filter_control.reset();
        self.vca.reset();
        self.output.reset();
        for d in &mut self.decimators {
            d.reset();
        }
        self.under_phase = 0;
        self.under_previous = 0.0;
        self.under_current = 0.0;
        self.under_trigger = false;
        self.knobs = self.target;
        self.knobs_settled = self.target.all_finite();
    }

    pub fn set_knobs(&mut self, knobs: Knobs) {
        self.target = knobs;
        self.knobs_settled = false;
    }

    /// STB33 GLIDE (0..1, 0.5 = stock slide time) and PITCH GLIDE (0..1 = up to an octave swoop).
    pub fn set_glide(&mut self, glide: f64, pitch_glide: f64) {
        self.pitch.set_glide(10f64.powf(2.0 * glide - 1.0), pitch_glide);
    }
    pub fn set_quality(&mut self, quality: Quality) {
        self.vcf.set_quality(quality);
    }

    pub fn vcf_diagnostics(&self) -> dw303_vcf::VcfDiagnostics {
        self.vcf.diagnostics()
    }

    fn smooth_knobs(&mut self) {
        // The existing snap reaches the exact target. Until another setter,
        // every remaining smoothing update would assign those same bits.
        // Invalid targets retain the original sanitisation/smoothing path.
        if self.knobs_settled {
            return;
        }
        let k = self.knob_coefficient;
        // Snap when settled so downstream coefficient caches see identical values.
        let s = |current: &mut f64, target: f64| {
            let delta = target - *current;
            if delta.abs() < 1.0e-7 {
                *current = target;
            } else {
                *current += delta * k;
            }
        };
        s(&mut self.knobs.tuning, self.target.tuning);
        s(&mut self.knobs.cutoff, self.target.cutoff);
        s(&mut self.knobs.resonance, self.target.resonance);
        s(&mut self.knobs.env_mod, self.target.env_mod);
        s(&mut self.knobs.decay, self.target.decay);
        s(&mut self.knobs.accent, self.target.accent);
        s(&mut self.knobs.volume, self.target.volume);
        self.knobs.square = self.target.square;
        self.knobs_settled = self.knobs == self.target && self.target.all_finite();
    }

    /// One oversampled circuit tick, returning the OUTPUT jack voltage.
    fn tick_circuit(&mut self, input: VoiceInput, trigger: bool) -> f64 {
        self.smooth_knobs();
        let k = self.knobs;
        let cv = self.pitch.tick_glide(input.note, input.slide, trigger);
        let osc = self.vco.tick(cv, k.tuning);
        let waveform = if k.square { osc.square } else { osc.saw };
        let pulse = self.trigger.tick(trigger);
        let meg = self.meg.tick(pulse, input.accent, k.decay);
        let accent_wiper = self.accent.tick(input.accent, meg, k.accent);
        let sweep = self.sweep.tick(accent_wiper, k.resonance);
        let ladder_current = self
            .filter_control
            .ladder_current(k.cutoff, k.env_mod, meg, sweep);
        self.veg.set_gate(input.gate);
        let veg_current = self.veg.tick(pulse);
        let vcf = self.vcf.tick(waveform, ladder_current, k.resonance);
        let vca = self.vca.tick(
            VcfSignals {
                output: vcf.output,
                resonance_tap: vcf.resonance_tap,
            },
            veg_current,
            accent_wiper,
        );
        self.output.tick(vca, k.volume, 0.0)
    }

    /// Render one host-rate sample (digital full scale = `FULL_SCALE_VOLTS`).
    pub fn tick(&mut self, input: VoiceInput) -> f64 {
        match self.ratio {
            RateRatio::Over(n) => self.tick_over(input, n),
            RateRatio::Under(n) => self.tick_under(input, n),
        }
    }

    fn tick_over(&mut self, input: VoiceInput, factor: usize) -> f64 {
        // Common host rates need no 64-sample temporary or variable reduction
        // loop. Decimator states are independent of the circuit states.
        match factor {
            4 => {
                let a = self.tick_circuit(input, input.trigger);
                let b = self.tick_circuit(input, false);
                let lower = self.decimators[0].downsample([a, b]);
                let c = self.tick_circuit(input, false);
                let d = self.tick_circuit(input, false);
                let upper = self.decimators[0].downsample([c, d]);
                self.decimators[1].downsample([lower, upper]) / FULL_SCALE_VOLTS
            }
            2 => {
                let a = self.tick_circuit(input, input.trigger);
                let b = self.tick_circuit(input, false);
                self.decimators[0].downsample([a, b]) / FULL_SCALE_VOLTS
            }
            _ => self.tick_over_generic(input, factor),
        }
    }

    fn tick_over_generic(&mut self, input: VoiceInput, factor: usize) -> f64 {
        let mut x = [0.0; 1 << MAX_DECIMATION_STAGES];
        let mut count = factor;
        for (i, sample) in x[..count].iter_mut().enumerate() {
            *sample = self.tick_circuit(input, input.trigger && i == 0);
        }
        let mut stage = 0;
        while count > 1 {
            for i in 0..count / 2 {
                x[i] = self.decimators[stage].downsample([x[2 * i], x[2 * i + 1]]);
            }
            count /= 2;
            stage += 1;
        }
        x[0] / FULL_SCALE_VOLTS
    }

    fn tick_under(&mut self, input: VoiceInput, divisor: usize) -> f64 {
        self.under_trigger |= input.trigger;
        if self.under_phase == 0 {
            let trigger = core::mem::take(&mut self.under_trigger);
            self.under_previous = self.under_current;
            self.under_current = self.tick_circuit(input, trigger);
        }
        let t = self.under_phase as f64 / divisor as f64;
        self.under_phase = (self.under_phase + 1) % divisor;
        (self.under_previous + (self.under_current - self.under_previous) * t) / FULL_SCALE_VOLTS
    }

    /// Current circuit/host rate ratio.
    pub fn rate_ratio(&self) -> RateRatio {
        self.ratio
    }
}

/// Centre-cutoff small-signal ring pole used to normalise the fitted VR3 law.
/// VERIFY: 535 Hz gives a settled C1 harmonic-response peak at 523 Hz, compared
/// with session1 TD-3 page-7's 520 Hz (early-window peak approximately 552 Hz).
/// The service-note 500 Hz / 2 ms procedure remains the sanity check, rather
/// than an assertion that this measured clone or its damped pole is exactly 500 Hz.
/// See `docs/circuit/vcf-calibration.md` for the fitting evidence and limits.
pub const CALIBRATION_RING_HZ: f64 = 535.0;

/// VERIFY: rest MEG and accent-sweep voltages from the present envelope/device
/// reduction, not measured internal TD-3 voltages. Used with all knobs at minimum.
const CALIBRATION_MEG_REST: f64 = dw303_env::Components::NOMINAL.diode_drop
    + 2.0 * dw303_env::Components::NOMINAL.meg_discharge_drop
    - 2.0 * dw303_env::Components::NOMINAL.vbe;
/// VERIFY: computed Q9 rest/reference voltage; no internal-node capture calibration.
const CALIBRATION_SWEEP_REST: f64 = 3.3165;

/// VERIFY: output-only VR3 electrical track fit with R47 at +5% tolerance.
/// Supported ring-sweep corrections use half the measured residual to allow
/// harmonic-grid/placement uncertainty; the .4 current retains the joint
/// envelope fit. Centre normalisation and
/// the fixed electrical endpoints retain the established 535-Hz calibration.
const CALIBRATION_CUTOFF_TRACK: [f64; 11] = [
    0.000000000000,
    0.015992842107,
    0.048602438627,
    0.082664695567,
    0.106791530946,
    0.132999821583,
    0.150492774318,
    0.309012150489,
    0.662387518618,
    0.988512075163,
    1.000000000000,
];

/// VERIFY: R47 printed 10 kΩ → fitted 10.5 kΩ, assuming ±5% tolerance.
/// The low-cutoff ring is under-predicted; no hardware resistor measurement.
const CALIBRATION_CUTOFF_RETURN: f64 = 10_500.0;

/// Return fitted cutoff components with the expo ratio normalised at centre.
/// TM3 retains the ENV component setting; VR3 uses the top-range calibration
/// above. The bounded continuous pole search adjusts only Is(Q11)/Is(Q10),
/// on the control thread.
/// VERIFY: the effective ratio is derived from the fitted VCF and model rest
/// voltages (ENV default 3.074323); audio identifies its product with TM3's
/// reference current, not either device parameter independently.
pub fn calibrated_env_components() -> dw303_env::Components {
    let mut c = dw303_env::Components {
        vr3_taper: CALIBRATION_CUTOFF_TRACK,
        r47: CALIBRATION_CUTOFF_RETURN,
        ..dw303_env::Components::NOMINAL
    };
    let vcf = Vcf::new(dw303_vcf::Components::NOMINAL);
    let Some(target) = vcf.calibration_current(CALIBRATION_RING_HZ) else {
        return c;
    };
    let mut control = FilterControl::new(c);
    let actual = control.ladder_current(0.5, 0.0, CALIBRATION_MEG_REST, CALIBRATION_SWEEP_REST);
    if actual.is_finite() && actual > 0.0 {
        c.expo_is_ratio *= target / actual;
    }
    c
}

impl Default for Voice {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn direct_decimation_matches_generic_during_notes_and_knob_switches() {
        for (rate, factor) in [(48_000.0, 4), (96_000.0, 2)] {
            let mut direct = Voice::new();
            let mut generic = Voice::new();
            direct.prepare(rate).unwrap();
            generic.prepare(rate).unwrap();
            for n in 0..24_000 {
                if n % 4096 == 0 {
                    let knobs = Knobs {
                        cutoff: (n / 4096 % 5) as f64 / 4.0,
                        square: n / 4096 % 2 == 1,
                        ..Knobs::default()
                    };
                    direct.set_knobs(knobs);
                    generic.set_knobs(knobs);
                }
                let input = VoiceInput {
                    note: (n / 3000 % 25) as u8,
                    gate: n % 3000 < 2400,
                    trigger: n % 3000 == 0,
                    accent: n / 3000 % 2 == 0,
                    slide: n / 3000 % 3 == 0,
                };
                assert_eq!(
                    direct.tick(input).to_bits(),
                    generic.tick_over_generic(input, factor).to_bits()
                );
            }
            assert_eq!(direct.vcf_diagnostics(), generic.vcf_diagnostics());
        }
    }

    #[test]
    fn settled_smoothing_matches_unconditional_updates_and_invalid_targets() {
        let mut cached = Voice::new();
        let mut reference = Voice::new();
        cached.prepare(48_000.0).unwrap();
        reference.prepare(48_000.0).unwrap();
        let bits = |k: Knobs| {
            [
                k.tuning,
                k.cutoff,
                k.resonance,
                k.env_mod,
                k.decay,
                k.accent,
                k.volume,
            ]
            .map(f64::to_bits)
        };
        for cutoff in [0.0, 1.0, 0.25, -0.0, f64::INFINITY, f64::NAN, 0.5] {
            let knobs = Knobs {
                cutoff,
                square: cutoff == 1.0,
                ..Knobs::default()
            };
            cached.set_knobs(knobs);
            reference.set_knobs(knobs);
            for _ in 0..20_000 {
                cached.smooth_knobs();
                reference.knobs_settled = false;
                reference.smooth_knobs();
                assert_eq!(bits(cached.knobs), bits(reference.knobs));
                assert_eq!(cached.knobs.square, reference.knobs.square);
            }
            cached.reset();
            reference.reset();
            cached.smooth_knobs();
            reference.knobs_settled = false;
            reference.smooth_knobs();
            assert_eq!(bits(cached.knobs), bits(reference.knobs));
        }
    }

    #[test]
    fn oversampling_covers_common_and_extreme_host_rates() {
        use RateRatio::{Over, Under};
        for (rate, ratio) in [
            (1_000.0, Over(64)),
            (8_000.0, Over(32)),
            (22_050.0, Over(8)),
            (44_100.0, Over(4)),
            (48_000.0, Over(4)),
            (88_200.0, Over(2)),
            (96_000.0, Over(2)),
            (192_000.0, Over(1)),
            (384_000.0, Over(1)),
            (768_000.0, Under(2)),
        ] {
            assert_eq!(oversampling_for(rate), Some(ratio), "{rate}");
        }
        assert_eq!(oversampling_for(0.0), None);
        assert_eq!(oversampling_for(f64::NAN), None);
    }

    #[test]
    fn prepares_and_renders_finite_audio_across_host_rates() {
        for rate in [
            1_000.0, 8_000.0, 44_100.0, 96_000.0, 192_000.0, 384_000.0, 768_000.0,
        ] {
            let mut voice = Voice::new();
            voice.prepare(rate).expect("supported rate");
            let mut input = VoiceInput {
                note: 12,
                gate: true,
                trigger: true,
                accent: true,
                slide: false,
            };
            for _ in 0..(rate as usize / 10) {
                assert!(voice.tick(input).is_finite());
                input.trigger = false;
            }
        }
    }
}
