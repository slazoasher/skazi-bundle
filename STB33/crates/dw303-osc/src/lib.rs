#![no_std]
//! TB-303 pitch voltage, exponential VCO, capacitor reset and PNP square shaper.
//!
//! Voltages are SI node voltages. See `docs/circuit/osc.md` for calibration
//! assumptions, unverified values and the bandwidth/CPU tradeoff.

use core::f64::consts::{LN_2, PI, TAU};

const HARMONICS: usize = 256;
const BOLTZMANN_OVER_Q: f64 = 8.617_333_262_145e-5;

/// Stage preparation error.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StageError {
    /// Rate is non-finite or outside the supported 44.1–384 kHz interval.
    InvalidSampleRate,
}

/// Circuit component values and fitted adjustment targets, in SI units.
/// Invalid values fall back individually to nominal.
#[derive(Clone, Copy, Debug)]
pub struct Components {
    /// R74–R90/R96 ladder Thevenin resistance (200k arms in parallel).
    pub slide_resistance: f64,
    /// C35, printed 0.22 uF.
    /// VERIFY: 0.22 → 0.232 uF fitted (+5.45%); assumed capacitor tolerance.
    pub slide_capacitance: f64,
    /// TM4 centre adjustment, expressed as lowest-C frequency at 1 V.
    /// VERIFY: 32.703195663 Hz service target → 32.577173 Hz session fit.
    pub reference_hz: f64,
    /// Chosen operating temperature of the Q26 2SC1583 pair, kelvin.
    pub temperature_kelvin: f64,
    /// VR2 total tuning span in octaves, converted to an effective pin-3 rail.
    /// VERIFY: O3 pin-3 assumed reference rail; 1400 → 1978.472 cents fitted.
    pub tuning_octaves: f64,
    /// VR2 B-taper panel pot total resistance, printed 50k(B).
    /// VERIFY: 50k → 44.945k fitted within typical 20% pot tolerance.
    pub tuning_resistance: f64,
    /// VR2 smooth resistive-track bow coefficient (ideal linear B taper: zero).
    /// Electrical fraction = x + x*(1-x)*(2*x-1)*(bow + skew*(2*x-1)).
    /// VERIFY: 0 → 0.102497 fitted to session-2 intermediate tuning positions;
    /// track density and operator angle placement cannot be isolated by audio.
    pub tuning_track_bow: f64,
    /// VR2 resistive-track skew coefficient (ideal linear B taper: zero).
    /// Shares the electrical-fraction law documented on `tuning_track_bow`.
    /// VERIFY: 0 → -0.038277 fitted; smooth B-taper approximation, not a
    /// manufacturer track specification. No panel-angle offset is applied.
    pub tuning_track_skew: f64,
    /// R118, tuning summing resistor, 100k.
    pub tuning_input_resistance: f64,
    /// R106, CV summing resistor, 27k.
    pub cv_input_resistance: f64,
    /// R100 posistor, 560 ohms at the chosen operating temperature.
    pub posistor_resistance: f64,
    /// VERIFY: R100 PTC coefficient; ideal 1/Tnom compensation per kelvin.
    pub posistor_temperature_coefficient: f64,
    /// TM5 4.7k rheostat range; nominal position is solved for 1 V/oct.
    pub width_resistance: f64,
    /// C33, .01 uF.
    pub timing_capacitance: f64,
    /// C33 lower reset threshold, set by Q24/Q25 junction difference.
    /// VERIFY: ideal source annotation 5.5 V → fitted capacitor threshold 5.2 V.
    pub saw_low: f64,
    /// C33 upper reset voltage after Q27/D25 conduction.
    /// VERIFY: ideal source annotation 12 V → fitted capacitor endpoint 11.5 V.
    pub saw_high: f64,
    /// R105 source-follower load, printed 10k.
    pub follower_load_resistance: f64,
    /// VERIFY: Q28 2SK30(O) zero-gate-bias drain current; fitted 0.6 mA.
    pub follower_idss_a: f64,
    /// VERIFY: magnitude of Q28 negative pinch-off voltage; fitted 1 V.
    pub follower_pinch_off_v: f64,
    /// Q27 emitter / R45 top, 12 V rail.
    pub square_supply: f64,
    /// R36 return, 5.333 V rail (exact 64/12 for DAC calibration).
    pub square_return: f64,
    /// R35, DC saw-to-Q8-base path, printed 100k.
    /// VERIFY: 100k → 102.310k fitted (assumed 5% resistor tolerance);
    /// round-10 output fit includes the current VCF coupling parameters.
    pub square_dc_resistance: f64,
    /// R34, series path from C10 to Q8 base, 10k.
    pub square_input_resistance: f64,
    /// C10, printed .01 uF.
    /// VERIFY: 10 nF → 9.2824 nF fitted (assumed 20% capacitor tolerance).
    pub square_input_capacitance: f64,
    /// R36, collector return resistance, 10k.
    pub square_collector_resistance: f64,
    /// R45, emitter supply resistance, printed 22k.
    /// VERIFY: 22k → 22.7705k fitted (assumed 5% resistor tolerance).
    pub square_emitter_resistance: f64,
    /// C11, emitter storage capacitor, printed 1 uF.
    /// VERIFY: 1 uF → 0.8373 uF fitted (assumed 20% capacitor tolerance).
    pub square_emitter_capacitance: f64,
    /// Q8 forward Veb at 100 uA collector current and `temperature_kelvin`.
    /// VERIFY: Q8 unprinted part/model (O6); fitted 0.5 V reference drop,
    /// replacing the previous constant 0.5425 V junction approximation.
    pub transistor_vbe: f64,
    /// VERIFY: Q8 forward beta, estimated 200 → fitted 428.559; no device curve supplied.
    pub transistor_beta: f64,
    /// VERIFY: Q8 saturation emitter-to-collector voltage, estimated 0.2 V → fitted 0.045733 V.
    pub transistor_vce_sat: f64,
}

impl Components {
    /// Schematic components with session-1/2 fitted tolerances/device estimates.
    /// See the VERIFY register for every departure from printed nominals.
    pub const NOMINAL: Self = Self {
        slide_resistance: 100_000.0,
        slide_capacitance: 0.232e-6,
        reference_hz: 32.577_173_303_309_65,
        temperature_kelvin: 300.0,
        tuning_octaves: 1_978.472 / 1_200.0,
        tuning_resistance: 44_945.454,
        tuning_track_bow: 0.102_496_922_354_261_72,
        tuning_track_skew: -0.038_276_829_432_479_28,
        tuning_input_resistance: 100_000.0,
        cv_input_resistance: 27_000.0,
        posistor_resistance: 560.0,
        posistor_temperature_coefficient: 1.0 / 300.0,
        width_resistance: 4_700.0,
        timing_capacitance: 10e-9,
        saw_low: 5.2,
        saw_high: 11.5,
        follower_load_resistance: 10_000.0,
        follower_idss_a: 0.6e-3,
        follower_pinch_off_v: 1.0,
        square_supply: 12.0,
        square_return: 64.0 / 12.0,
        square_dc_resistance: 102_309.6,
        square_input_resistance: 10_000.0,
        square_input_capacitance: 9.282_418e-9,
        square_collector_resistance: 10_000.0,
        square_emitter_resistance: 22_770.5,
        square_emitter_capacitance: 0.837_299_7e-6,
        transistor_vbe: 0.5,
        transistor_beta: 428.5591,
        transistor_vce_sat: 0.045_732_68,
    };

    fn valid(mut self) -> Self {
        let n = Self::NOMINAL;
        macro_rules! bound {
            ($field:ident, $lo:expr, $hi:expr) => {
                if !self.$field.is_finite() || !($lo..=$hi).contains(&self.$field) {
                    self.$field = n.$field;
                }
            };
        }
        bound!(slide_resistance, 1.0, 1e9);
        bound!(slide_capacitance, 1e-12, 1.0);
        bound!(reference_hz, 1.0, 10_000.0);
        bound!(temperature_kelvin, 200.0, 400.0);
        bound!(tuning_octaves, 0.0, 4.0);
        bound!(tuning_resistance, 1.0, 1e9);
        // These bounds keep the track resistance density strictly positive.
        bound!(tuning_track_bow, -0.25, 0.25);
        bound!(tuning_track_skew, -0.15, 0.15);
        bound!(tuning_input_resistance, 1.0, 1e9);
        bound!(cv_input_resistance, 1.0, 1e9);
        bound!(posistor_resistance, 1.0, 1e9);
        bound!(posistor_temperature_coefficient, 0.0, 0.005);
        bound!(width_resistance, 1.0, 1e9);
        bound!(timing_capacitance, 1e-12, 1e-3);
        bound!(saw_low, 0.0, 10.0);
        bound!(saw_high, 0.1, 20.0);
        if self.saw_high - self.saw_low < 1e-3 {
            self.saw_low = n.saw_low;
            self.saw_high = n.saw_high;
        }
        bound!(follower_load_resistance, 1.0, 1e9);
        bound!(follower_idss_a, 1e-6, 0.1);
        bound!(follower_pinch_off_v, 0.01, 10.0);
        bound!(square_supply, 1.0, 20.0);
        bound!(square_return, 0.0, 20.0);
        if self.square_return >= self.square_supply {
            self.square_return = n.square_return;
            self.square_supply = n.square_supply;
        }
        bound!(square_dc_resistance, 1.0, 1e9);
        bound!(square_input_resistance, 1.0, 1e9);
        bound!(square_input_capacitance, 1e-12, 1.0);
        bound!(square_collector_resistance, 1.0, 1e9);
        bound!(square_emitter_resistance, 1.0, 1e9);
        bound!(square_emitter_capacitance, 1e-12, 1.0);
        bound!(transistor_vbe, 0.1, 1.0);
        bound!(transistor_beta, 1.0, 1000.0);
        bound!(transistor_vce_sat, 0.01, 0.5);
        self
    }
}

/// Diagnostic counters since reset; saturating, deterministic and allocation-free.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct OscDiagnostics {
    /// Number of non-finite signal/knob arguments replaced by zero.
    pub non_finite_inputs: u64,
    /// Number of VCO samples whose requested frequency exceeds 0.45 × rate.
    pub frequency_limited_samples: u64,
}

fn sanitise(x: f64, count: &mut u64) -> f64 {
    if x.is_finite() {
        x
    } else {
        *count = count.saturating_add(1);
        0.0
    }
}

fn rate_valid(rate: f64) -> Result<(), StageError> {
    if rate.is_finite() && (44_100.0..=384_000.0).contains(&rate) {
        Ok(())
    } else {
        Err(StageError::InvalidSampleRate)
    }
}

fn clean_state(x: f64) -> f64 {
    if x.abs() < 1e-24 { 0.0 } else { x }
}

/// Ideal calibrated sequencer DAC mapping, volts: one semitone is 1/12 V.
/// R78 terminates at the reference rail: DAC code = min(note + 11, 63).
/// VERIFY: IC10 supply is unprinted; ideal logic-high = 64/12 V, per transcription.
pub fn cv_for_note(note: u8) -> f64 {
    f64::from(note.min(52) + 12) / 12.0
}

/// Sequencer DAC and switched slide capacitor voltage.
/// VERIFY: O2 latch/slide clock ambiguity is delegated to the sequencer; `note`
/// is an already-latched DAC value, independent of slide in this binding API.
/// VERIFY: ideal IC12 switches (O7 supply unprinted) and IC11b buffer omit
/// finite Ron, settling, D30/D31 junction drops and logic transistor transients.
pub struct PitchCv {
    c: Components,
    voltage: f64,
    retention: f64,
    rate: f64,
    /// STB33 GLIDE: multiplies the slide RC time (1.0 = stock circuit).
    time_scale: f64,
    /// STB33 PITCH GLIDE: volts each fresh note starts above its pitch.
    swoop_volts: f64,
    swoop: f64,
}

impl PitchCv {
    /// Construct at 48 kHz with a discharged slide capacitor.
    pub fn new(c: Components) -> Self {
        let c = c.valid();
        Self {
            c,
            voltage: 0.0,
            retention: libm::exp(-1.0 / (48_000.0 * c.slide_resistance * c.slide_capacitance)),
            rate: 48_000.0,
            time_scale: 1.0,
            swoop_volts: 0.0,
            swoop: 0.0,
        }
    }
    /// Set rate without altering the capacitor voltage; invalid rates preserve state.
    pub fn prepare(&mut self, sample_rate: f64) -> Result<(), StageError> {
        rate_valid(sample_rate)?;
        self.rate = sample_rate;
        self.retention = libm::exp(
            -1.0 / (sample_rate * self.c.slide_resistance * self.c.slide_capacitance * self.time_scale),
        );
        Ok(())
    }
    /// Discharge the slide storage capacitor.
    pub fn reset(&mut self) {
        self.voltage = 0.0;
        self.swoop = 0.0;
    }
    /// STB33 glide controls: RC time multiplier and per-note swoop in volts.
    pub fn set_glide(&mut self, time_scale: f64, swoop_volts: f64) {
        let time_scale = if time_scale.is_finite() { time_scale.clamp(0.05, 20.0) } else { 1.0 };
        if time_scale != self.time_scale {
            self.time_scale = time_scale;
            let _ = self.prepare(self.rate);
        }
        self.swoop_volts = if swoop_volts.is_finite() { swoop_volts } else { 0.0 };
    }
    /// [`Self::tick`] plus the PITCH GLIDE swoop, restarted on each fresh (non-slide) trigger.
    pub fn tick_glide(&mut self, note: u8, slide: bool, trigger: bool) -> f64 {
        let cv = self.tick(note, slide);
        if trigger && !slide {
            self.swoop = self.swoop_volts;
        }
        self.swoop *= self.retention;
        cv + self.swoop
    }
    /// DAC voltage for a sequencer semitone index (lowest C = 1 V).
    pub fn cv_for_note(note: u8) -> f64 {
        cv_for_note(note)
    }
    /// Return expo-converter CV. Bypassing slide clamps the capacitor to the DAC.
    /// Slide uses the exact RC solution for a held DAC value over one sample.
    pub fn tick(&mut self, note: u8, slide: bool) -> f64 {
        let target = cv_for_note(note);
        self.voltage = if slide {
            clean_state(target + (self.voltage - target) * self.retention)
        } else {
            target
        };
        self.voltage
    }
}

/// Q8 PNP shaper with the printed R35 || (C10 + R34) base network,
/// R45/C11 emitter network and R36 return to 5.333 V.
/// Stores C10 voltage and C11 emitter voltage; collector is DC-coupled.
/// Forward-active Q8 uses an exponential base junction with constant beta;
/// saturation clamps Vec while preserving base/emitter/collector current balance.
/// VERIFY: the saturation reduction retains the forward base-junction law;
/// reverse-active transport, beta rolloff, Early effect and parasitics are omitted.
/// The reference Veb is an output-audio fit, not a measured device characteristic.
pub struct SquareShaper {
    c: Components,
    input_cap: f64,
    emitter: f64,
    dt_over_input_cap: f64,
    base_dc_g: f64,
    base_ac_g: f64,
    emitter_g: f64,
    emitter_cap_g: f64,
    collector_g: f64,
    junction_vt: f64,
    inverse_base_g: f64,
    inverse_emitter_g: f64,
    inverse_saturated_g: f64,
    inverse_collector_g: f64,
    inverse_junction_vt: f64,
    inverse_active_scale: f64,
    saturation_limit_gain: f64,
    active_drive_offset: f64,
    active_current_scale: f64,
    saturated_drive_offset: f64,
    saturated_current_scale: f64,
    diagnostics: OscDiagnostics,
}

// ln(x), positive normal x. Reduce to [1/sqrt(2), sqrt(2)] and
// use the atanh series through z^13. |z| <= 0.171573 bounds the
// omitted tail below 4.6e-13; no libc calls are needed in the Halley loop.
fn junction_log(x: f64) -> f64 {
    let bits = x.to_bits();
    let mut exponent = ((bits >> 52) & 0x7ff) as i32 - 1023;
    let mut mantissa = f64::from_bits((bits & ((1_u64 << 52) - 1)) | (1023_u64 << 52));
    if mantissa > core::f64::consts::SQRT_2 {
        mantissa *= 0.5;
        exponent += 1;
    }
    let z = (mantissa - 1.0) / (mantissa + 1.0);
    let z2 = z * z;
    let series = 1.0
        + z2 * (1.0 / 3.0
            + z2 * (1.0 / 5.0
                + z2 * (1.0 / 7.0 + z2 * (1.0 / 9.0 + z2 * (1.0 / 11.0 + z2 / 13.0)))));
    exponent as f64 * LN_2 + 2.0 * z * series
}

// Positive W(exp(t)), equivalently u + ln(u) = t. This is the
// exact inverse of the reduced exponential base-junction/load-line law.
// Four Halley steps are bounded. At t <= -30, the omitted voltage
// drop across the effective load is <= Vt*exp(-30) < 3.3e-15 V
// even at 400 K; flushing it avoids unnecessary underflow-scale work.
fn junction_w(t: f64) -> f64 {
    if t <= -30.0 {
        return 0.0;
    }
    let mut u = if t > 1.0 {
        t - junction_log(t)
    } else {
        libm::exp(t)
    };
    for _ in 0..4 {
        let f = u + junction_log(u) - t;
        let next = u - f * u * (u + 1.0) / ((u + 1.0) * (u + 1.0) + 0.5 * f);
        // Once the update rounds to the same root, every remaining fixed
        // iteration would evaluate the same expression and state.
        if next == u {
            break;
        }
        u = next;
    }
    u
}

impl SquareShaper {
    /// Construct at 48 kHz, C10 discharged and C11 at its unloaded 12 V rail.
    pub fn new(c: Components) -> Self {
        let c = c.valid();
        let mut s = Self {
            c,
            input_cap: 0.0,
            emitter: c.square_supply,
            dt_over_input_cap: 0.0,
            base_dc_g: 1.0 / c.square_dc_resistance,
            base_ac_g: 0.0,
            emitter_g: 1.0 / c.square_emitter_resistance,
            emitter_cap_g: 0.0,
            collector_g: 1.0 / c.square_collector_resistance,
            junction_vt: BOLTZMANN_OVER_Q * c.temperature_kelvin,
            inverse_base_g: 0.0,
            inverse_emitter_g: 0.0,
            inverse_saturated_g: 0.0,
            inverse_collector_g: 0.0,
            inverse_junction_vt: 0.0,
            inverse_active_scale: 0.0,
            saturation_limit_gain: 0.0,
            active_drive_offset: 0.0,
            active_current_scale: 0.0,
            saturated_drive_offset: 0.0,
            saturated_current_scale: 0.0,
            diagnostics: OscDiagnostics::default(),
        };
        s.set_rate(48_000.0);
        s
    }
    fn set_rate(&mut self, rate: f64) {
        self.dt_over_input_cap = 1.0 / (rate * self.c.square_input_capacitance);
        self.base_ac_g = 1.0 / (self.c.square_input_resistance + self.dt_over_input_cap);
        self.emitter_cap_g = rate * self.c.square_emitter_capacitance;
        let gb = self.base_dc_g + self.base_ac_g;
        let ge = self.emitter_g + self.emitter_cap_g;
        // Reference collector current only parameterises the junction curve;
        // Is = 100 uA * exp(-Veb_ref/Vt), not a separate fitted gain.
        let reference = 100e-6 / self.c.transistor_beta;
        let active_r = 1.0 / gb + (self.c.transistor_beta + 1.0) / ge;
        let saturated_r = 1.0 / gb + 1.0 / (ge + self.collector_g);
        self.active_current_scale = self.junction_vt / active_r;
        self.saturated_current_scale = self.junction_vt / saturated_r;
        self.inverse_base_g = 1.0 / gb;
        self.inverse_emitter_g = 1.0 / ge;
        self.inverse_saturated_g = 1.0 / (ge + self.collector_g);
        self.inverse_collector_g = 1.0 / self.collector_g;
        self.inverse_junction_vt = 1.0 / self.junction_vt;
        self.inverse_active_scale = 1.0 / self.active_current_scale;
        self.saturation_limit_gain =
            1.0 / (self.c.transistor_beta / self.collector_g + (self.c.transistor_beta + 1.0) / ge);
        self.active_drive_offset = libm::log(reference / self.active_current_scale);
        self.saturated_drive_offset = libm::log(reference / self.saturated_current_scale);
    }
    /// Prepare backward-Euler capacitor companions, preserving node voltages.
    pub fn prepare(&mut self, sample_rate: f64) -> Result<(), StageError> {
        rate_valid(sample_rate)?;
        self.set_rate(sample_rate);
        Ok(())
    }
    /// Restore rail equilibrium and clear diagnostics, preserving the rate.
    pub fn reset(&mut self) {
        self.input_cap = 0.0;
        self.emitter = self.c.square_supply;
        self.diagnostics = OscDiagnostics::default();
    }
    /// Diagnostics since reset.
    pub fn diagnostics(&self) -> OscDiagnostics {
        self.diagnostics
    }
    /// Process Q28 source voltage into Q8 collector voltage (no output capacitor).
    /// Backward-Euler capacitor companions reduce each conduction branch to
    /// `R*ib + Vt*ln(ib/ib_ref) = Vdrive - Veb_ref`, inverted by four
    /// bounded Halley steps. Saturation uses `ic=(Ve-Vec_sat-Vreturn)/R36`.
    /// No allocations or sample-rate-dependent iteration bounds.
    pub fn tick(&mut self, saw: f64) -> f64 {
        let saw = sanitise(saw, &mut self.diagnostics.non_finite_inputs).clamp(-100.0, 100.0);
        let gb = self.base_dc_g + self.base_ac_g;
        let drive = gb * saw - self.base_ac_g * self.input_cap;
        let supply = self.emitter_g * self.c.square_supply + self.emitter_cap_g * self.emitter;
        let off_emitter = supply * self.inverse_emitter_g;
        let off_base = drive * self.inverse_base_g;
        // Eliminate Ve and Vb using the capacitor companion KCL equations.
        // Active: Ve=Voff-(beta+1)*ib/ge, Vb=Voff_base+ib/gb.
        let beta1 = self.c.transistor_beta + 1.0;
        let delta = (off_emitter - off_base - self.c.transistor_vbe) * self.inverse_junction_vt;
        let active_drive = delta + self.active_drive_offset;
        // The active solution saturates once its base current exceeds this
        // load-line boundary. Compare in the monotone u + ln(u) domain before
        // solving: saturated samples then need only the saturated root.
        // Both C10 and C11 still update on every tick, for either waveform.
        let limit = (off_emitter - self.c.transistor_vce_sat - self.c.square_return)
            * self.saturation_limit_gain;
        let limit_u = limit * self.inverse_active_scale;
        let saturated = limit <= 0.0 || active_drive > limit_u + junction_log(limit_u);
        let (ve, vc, base) = if saturated {
            // Saturated: ie=ib+ic, with the collector load entering the
            // emitter companion. This keeps C11 charge physically balanced.
            let sat_supply =
                supply + self.collector_g * (self.c.square_return + self.c.transistor_vce_sat);
            let delta = (sat_supply * self.inverse_saturated_g - off_base - self.c.transistor_vbe)
                * self.inverse_junction_vt;
            let ib = self.saturated_current_scale * junction_w(delta + self.saturated_drive_offset);
            let ve = (sat_supply - ib) * self.inverse_saturated_g;
            (
                ve,
                (ve - self.c.transistor_vce_sat).max(self.c.square_return),
                (drive + ib) * self.inverse_base_g,
            )
        } else {
            let ib = self.active_current_scale * junction_w(active_drive);
            (
                (supply - beta1 * ib) * self.inverse_emitter_g,
                self.c.square_return + self.c.transistor_beta * ib * self.inverse_collector_g,
                (drive + ib) * self.inverse_base_g,
            )
        };
        let (emitter, collector) = (ve, vc);
        self.input_cap = clean_state(
            self.input_cap
                + self.dt_over_input_cap * self.base_ac_g * (saw - base - self.input_cap),
        );
        self.emitter = emitter.clamp(0.0, self.c.square_supply);
        collector.clamp(self.c.square_return, self.c.square_supply)
    }
}

/// Node voltages at the WAVEFORM switch, in volts.
#[derive(Clone, Copy, Debug, Default)]
pub struct VcoOut {
    /// Band-limited saw/source-follower voltage, including DC bias.
    pub saw: f64,
    /// DC-coupled Q8 PNP collector voltage, nominally 5.333–8 V.
    pub square: f64,
}

/// Q26 matched-pair current sink and C33 falling-ramp oscillator.
/// Q25 senses the low threshold established by diode-connected Q24/R101;
/// Q27/D25 regeneratively reset C33 toward the 12 V rail.
/// VERIFY: reset treated as instantaneous (printed waveform); device recovery omitted.
/// VERIFY: Q25 NPN choice follows O5 topology; Q24/Q25 Vbe and D25 drop
/// are represented by the fitted low endpoint, not measured device models.
/// VERIFY: Q26 ideal constant-current sink omits Early effect and pair mismatch.
/// VERIFY: IC11a/R104/C34 treated as ideal reference regulation; TM4/R102
/// current is calibrated from centre-pitch captures, bypassing R102 unit (O1).
/// VERIFY: Q28/R105 uses a Shockley saturation/triode follower without gate
/// current or capacitances; IDSS/pinch-off fitted from output harmonic balance.
/// Analytic cycle projection bounds the reset spectrum to 256 harmonics.
pub struct Vco {
    c: Components,
    phase: f64,
    phase_sine: f64,
    phase_cosine: f64,
    step_sine: f64,
    step_cosine: f64,
    phase_ticks: u8,
    step_ready: bool,
    sample_rate: f64,
    dc: f64,
    reference_frequency: f64,
    band_limit: Option<f64>,
    cosine: [f64; HARMONICS],
    sine: [f64; HARMONICS],
    cap_gain: [f64; HARMONICS],
    weighted_cosine: [f64; HARMONICS],
    weighted_sine: [f64; HARMONICS],
    active_harmonics: usize,
    pass_harmonics: usize,
    cached_frequency: f64,
    cached_cv: f64,
    cached_tuning: f64,
    cached_requested: f64,
    square: SquareShaper,
    diagnostics: OscDiagnostics,
}

// Count harmonics at a band edge, correcting the quotient estimate with the
// exact operation order used by the original loop. Bounded adjustments handle
// roundoff at an integer harmonic without dropping/adding a boundary term.
fn harmonics_in_band(frequency: f64, rate: f64, edge: f64, inclusive: bool) -> usize {
    let mut count = ((edge * rate / frequency) as usize).min(HARMONICS);
    let admits = |h: usize| {
        let ratio = h as f64 * frequency / rate;
        if inclusive {
            ratio <= edge
        } else {
            ratio < edge
        }
    };
    for _ in 0..2 {
        if count > 0 && !admits(count) {
            count -= 1;
        } else if count < HARMONICS && admits(count + 1) {
            count += 1;
        }
    }
    count
}

// C33 loses this charge between the capacitor endpoints; Q26b is a sink.
fn cycle_charge(c: Components) -> f64 {
    c.timing_capacitance * (c.saw_high - c.saw_low)
}

// Positive root of the source-follower load-line equation. The gate is the
// C33 voltage; Vs is Q28 source. Only construction calls this static function.
fn follower_voltage(c: Components, gate: f64) -> f64 {
    let a = c.follower_pinch_off_v / libm::sqrt(c.follower_load_resistance * c.follower_idss_a);
    let overdrive = gate + c.follower_pinch_off_v;
    if overdrive > c.square_supply {
        // Linear/triode region when Vds < Vgs - Vp. Include the finite
        // drain rail instead of letting the saturation law approach it.
        let b = 2.0 * overdrive + a * a;
        let k = c.square_supply * (2.0 * overdrive - c.square_supply);
        return 2.0 * k / (b + libm::sqrt((b * b - 4.0 * k).max(0.0)));
    }
    let root = 2.0 * overdrive / (libm::sqrt(a * a + 4.0 * overdrive) + a);
    root * root
}

impl Vco {
    /// Construct a calibrated oscillator at 48 kHz, with precomputed reset spectrum.
    pub fn new(c: Components) -> Self {
        let c = c.valid();
        let cap_gain = core::array::from_fn(|i| {
            if i < 224 {
                1.0
            } else {
                0.5 + 0.5 * libm::cos(PI * (i + 1 - 224) as f64 / 32.0)
            }
        });
        let mut s = Self {
            c,
            phase: 0.0,
            phase_sine: 0.0,
            phase_cosine: 1.0,
            step_sine: 0.0,
            step_cosine: 1.0,
            phase_ticks: 0,
            step_ready: false,
            sample_rate: 48_000.0,
            dc: 0.0,
            reference_frequency: c.reference_hz * cycle_charge(Components::NOMINAL)
                / cycle_charge(c),
            band_limit: None,
            cosine: [0.0; HARMONICS],
            sine: [0.0; HARMONICS],
            cap_gain,
            weighted_cosine: [0.0; HARMONICS],
            weighted_sine: [0.0; HARMONICS],
            active_harmonics: 0,
            pass_harmonics: 0,
            cached_frequency: f64::NAN,
            cached_cv: f64::NAN,
            cached_tuning: f64::NAN,
            cached_requested: 0.0,
            square: SquareShaper::new(c),
            diagnostics: OscDiagnostics::default(),
        };
        // Q26b discharges C33 linearly. Q28's loaded source follows a
        // Shockley JFET law: Vs/R105 = IDSS*(1 + (Vg-Vs)/|Vp|)^2.
        // Project the small nonlinear residual around the exact linear ramp.
        // Its zero endpoint jump avoids quadrature error at the reset step.
        let high = follower_voltage(c, c.saw_high);
        let low = follower_voltage(c, c.saw_low);
        s.dc = 0.5 * (high + low);
        for h in 1..=HARMONICS {
            s.sine[h - 1] = (high - low) / (PI * h as f64);
        }
        const POINTS: usize = 1024;
        for i in 0..POINTS {
            let phase = (i as f64 + 0.5) / POINTS as f64;
            let gate = c.saw_high - (c.saw_high - c.saw_low) * phase;
            let linear = high - (high - low) * phase;
            let residual = (follower_voltage(c, gate) - linear) / POINTS as f64;
            s.dc += residual;
            let (si, co) = libm::sincos(TAU * phase);
            let (mut hs, mut hc) = (si, co);
            for h in 0..HARMONICS {
                s.cosine[h] += 2.0 * residual * hc;
                s.sine[h] += 2.0 * residual * hs;
                let next = hc * co - hs * si;
                hs = hs * co + hc * si;
                hc = next;
            }
        }
        s
    }
    /// Set the stage rate; preserve phase and capacitor states on success or error.
    pub fn prepare(&mut self, sample_rate: f64) -> Result<(), StageError> {
        rate_valid(sample_rate)?;
        self.square.prepare(sample_rate)?;
        self.sample_rate = sample_rate;
        self.cached_frequency = f64::NAN;
        Ok(())
    }
    /// Reset phase, capacitor states and counters.
    pub fn reset(&mut self) {
        self.phase = 0.0;
        self.phase_ticks = 0;
        self.square.reset();
        self.diagnostics = OscDiagnostics::default();
    }
    /// Counters since reset, including the square-stage input diagnostics.
    pub fn diagnostics(&self) -> OscDiagnostics {
        let mut d = self.diagnostics;
        d.non_finite_inputs = d
            .non_finite_inputs
            .saturating_add(self.square.diagnostics().non_finite_inputs);
        d
    }
    /// Static exponential law, before the sampling-frequency safety limit.
    /// The reference current is calibrated using NOMINAL capacitor/endpoints;
    /// changing timing components therefore changes frequency physically.
    ///
    /// Matched pair: Iout/Iref = exp(delta_Vbe/Vt). R106/TM5, R118 and
    /// R100 set the divider; the posistor compensates junction temperature.
    /// CV is limited to ±16 V for bounded numerical behaviour; non-finite CV and
    /// knob values map to zero. This read-only query does not update diagnostics.
    pub fn frequency_hz(&self, cv: f64, tuning: f64) -> f64 {
        let cv = if cv.is_finite() {
            cv.clamp(-16.0, 16.0)
        } else {
            0.0
        };
        let tuning = if tuning.is_finite() {
            tuning.clamp(0.0, 1.0)
        } else {
            0.0
        };
        let n = Components::NOMINAL;
        let nominal_vt = BOLTZMANN_OVER_Q * n.temperature_kelvin;
        let vt = BOLTZMANN_OVER_Q * self.c.temperature_kelvin;
        // Solve nominal TM5 WIDTH position for g_cv/g_total = Vt*ln(2).
        // Preserve the physical trimmer position under component changes.
        let target = nominal_vt * LN_2;
        let nominal_rcv =
            (1.0 / target - 1.0) / (1.0 / n.posistor_resistance + 1.0 / n.tuning_input_resistance);
        let width_rotation =
            ((nominal_rcv - n.cv_input_resistance) / n.width_resistance).clamp(0.0, 1.0);
        let rcv = self.c.cv_input_resistance + width_rotation * self.c.width_resistance;
        let ptc = self.c.posistor_resistance
            * (1.0
                + self.c.posistor_temperature_coefficient
                    * (self.c.temperature_kelvin - n.temperature_kelvin));
        let cv_gain = 1.0 / (1.0 + rcv / ptc + rcv / self.c.tuning_input_resistance);
        // VR2 is loaded toward the R100 reference by R118, rather
        // than toward its own midpoint. R118 loading produces the measured
        // asymmetric endpoints; TM4 absorbs the centre offset.
        // VERIFY: O3 pin-3 effective rail is inferred from the endpoint span;
        // 5.402 V is close to, but does not establish, the 5.333 V reference.
        let load = self.c.tuning_input_resistance;
        let tune_gain = cv_gain * rcv / load;
        let nominal_tune_gain = target * nominal_rcv / n.tuning_input_resistance;
        let rail = target * self.c.tuning_octaves / nominal_tune_gain;
        // Integrating a smooth positive track resistance density gives this
        // electrical fraction. Both unloaded voltage and Thevenin resistance
        // use the same two track segments; centre and endpoints stay fixed.
        let u = 2.0 * tuning - 1.0;
        let fraction = tuning
            + tuning
                * (1.0 - tuning)
                * u
                * (self.c.tuning_track_bow + self.c.tuning_track_skew * u);
        let source_r = self.c.tuning_resistance * fraction * (1.0 - fraction);
        let centre_r = self.c.tuning_resistance * 0.25;
        let wiper_voltage = (rail * fraction * load + (64.0 / 12.0) * source_r) / (load + source_r);
        let centre_voltage = (rail * 0.5 * load + (64.0 / 12.0) * centre_r) / (load + centre_r);
        let delta_vbe = cv_gain * (cv - 1.0) + tune_gain * (wiper_voltage - centre_voltage);
        self.reference_frequency * libm::exp((delta_vbe / vt).clamp(-80.0, 80.0))
    }
    /// Q26b collector sink current, amperes, from TM4 calibration and C33 charge.
    /// This uses C33 capacitor endpoints, before Q28 follower attenuation.
    pub fn charging_current_a(&self, cv: f64, tuning: f64) -> f64 {
        self.frequency_hz(cv, tuning) * cycle_charge(self.c)
    }

    /// Limit synthesised harmonics to `hz` (passband edge; fade over a further
    /// 20%). When the voice runs oversampled and decimates, harmonics above the
    /// host band are discarded anyway, so this saves work without audible
    /// change. `None` restores the full oversampled band (0.40/0.48 x rate).
    pub fn set_band_limit(&mut self, hz: Option<f64>) {
        let band_limit = hz.filter(|hz| hz.is_finite() && *hz > 0.0);
        if self.band_limit.map(f64::to_bits) != band_limit.map(f64::to_bits) {
            self.cached_frequency = f64::NAN;
            self.band_limit = band_limit;
        }
    }
    fn band_ratios(&self) -> (f64, f64) {
        match self.band_limit {
            Some(hz) => {
                let pass = (hz / self.sample_rate).min(0.40);
                (pass, (pass * 1.2).min(0.48))
            }
            None => (0.40, 0.48),
        }
    }
    /// Advance the capacitor cycle and return saw and PNP square node voltages.
    /// Frequencies above 0.45 × sample rate are limited and counted.
    pub fn tick(&mut self, cv: f64, tuning: f64) -> VcoOut {
        let cv = sanitise(cv, &mut self.diagnostics.non_finite_inputs);
        let tuning = sanitise(tuning, &mut self.diagnostics.non_finite_inputs);
        if cv.to_bits() != self.cached_cv.to_bits()
            || tuning.to_bits() != self.cached_tuning.to_bits()
        {
            self.cached_requested = self.frequency_hz(cv, tuning);
            self.cached_cv = cv;
            self.cached_tuning = tuning;
        }
        let requested = self.cached_requested;
        let frequency = requested.min(0.45 * self.sample_rate);
        if requested > frequency {
            self.diagnostics.frequency_limited_samples =
                self.diagnostics.frequency_limited_samples.saturating_add(1);
        }
        let same_frequency = frequency.to_bits() == self.cached_frequency.to_bits();
        if !same_frequency {
            self.phase_ticks = 0;
            self.step_ready = false;
        }
        // Held frequency: rotate between exact phase evaluations, resynchronising
        // every 64 circuit samples to bound roundoff. Changing frequency uses
        // sincos of the actual phase every sample; the step is built lazily.
        let (si, co) = if self.phase_ticks == 0 {
            libm::sincos(TAU * self.phase)
        } else {
            if !self.step_ready {
                (self.step_sine, self.step_cosine) =
                    libm::sincos(TAU * (frequency / self.sample_rate));
                self.step_ready = true;
            }
            (
                self.phase_sine * self.step_cosine + self.phase_cosine * self.step_sine,
                self.phase_cosine * self.step_cosine - self.phase_sine * self.step_sine,
            )
        };
        self.phase_sine = si;
        self.phase_cosine = co;
        self.phase_ticks = (self.phase_ticks + 1) & 63;
        let mut hc = co;
        let mut hs = si;
        let mut saw = self.dc;
        // Rebuild only for a bitwise frequency/rate/bandwidth change. Slides
        // still use their exact instantaneous frequency; no quantisation.
        if !same_frequency {
            let (pass, stop) = self.band_ratios();
            let pass_count = harmonics_in_band(frequency, self.sample_rate, pass, true);
            let active_count = harmonics_in_band(frequency, self.sample_rate, stop, false);
            // Restore only harmonics that have moved from the fade into the
            // flat passband; unchanged flat gains need no per-sample work.
            if pass_count > self.pass_harmonics {
                for i in self.pass_harmonics..pass_count {
                    self.weighted_cosine[i] = self.cap_gain[i] * self.cosine[i];
                    self.weighted_sine[i] = self.cap_gain[i] * self.sine[i];
                }
            }
            for i in pass_count..active_count {
                let ratio = (i + 1) as f64 * frequency / self.sample_rate;
                let rate_gain = 0.5 + 0.5 * libm::cos(PI * (ratio - pass) / (stop - pass));
                let gain = rate_gain * self.cap_gain[i];
                self.weighted_cosine[i] = gain * self.cosine[i];
                self.weighted_sine[i] = gain * self.sine[i];
            }
            self.pass_harmonics = pass_count;
            self.active_harmonics = active_count;
            self.cached_frequency = frequency;
        }
        let count = self.active_harmonics;
        let (cosine, cosine_tail) = self.weighted_cosine[..count].as_chunks::<4>();
        let (sine, sine_tail) = self.weighted_sine[..count].as_chunks::<4>();
        // Four independent harmonic chains allow the compiler to use packed
        // arithmetic. Seed h=1..4 with the original recurrence, then rotate
        // each chain by four harmonics. Independent sums remove the serial
        // accumulator dependency; only floating-point rounding order changes.
        let mut cosines = [0.0; 4];
        let mut sines = [0.0; 4];
        for lane in 0..4 {
            cosines[lane] = hc;
            sines[lane] = hs;
            let next = hc * co - hs * si;
            hs = hs * co + hc * si;
            hc = next;
        }
        let step_cosine = cosines[3];
        let step_sine = sines[3];
        let mut sums = [0.0; 4];
        for (cosine, sine) in cosine.iter().zip(sine) {
            sums = core::array::from_fn(|lane| {
                sums[lane] + (cosine[lane] * cosines[lane] + sine[lane] * sines[lane])
            });
            let next_cosines =
                core::array::from_fn(|lane| cosines[lane] * step_cosine - sines[lane] * step_sine);
            sines =
                core::array::from_fn(|lane| sines[lane] * step_cosine + cosines[lane] * step_sine);
            cosines = next_cosines;
        }
        for (lane, (cosine, sine)) in cosine_tail.iter().zip(sine_tail).enumerate() {
            sums[lane] += cosine * cosines[lane] + sine * sines[lane];
        }
        for sum in sums {
            saw += sum;
        }
        self.phase += frequency / self.sample_rate;
        if self.phase >= 1.0 {
            self.phase -= 1.0;
        }
        VcoOut {
            saw,
            square: self.square.tick(saw),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parallel_harmonics_match_serial_recurrence_during_pitch_and_band_changes() {
        for rate in [44_100.0, 48_000.0, 96_000.0, 192_000.0, 384_000.0] {
            let mut vco = Vco::new(Components::NOMINAL);
            vco.prepare(rate).unwrap();
            for n in 0..12_000 {
                if n % 997 == 0 {
                    vco.set_band_limit(if n % 2 == 0 { Some(20_000.0) } else { None });
                }
                let cv = if n % 256 < 128 {
                    -1.0 + 18.0 * (n / 256) as f64 / 47.0
                } else {
                    -1.0 + 18.0 * n as f64 / 12_000.0
                };
                let tuning = (n / 512 % 5) as f64 / 4.0;
                let frequency = vco.frequency_hz(cv, tuning).min(0.45 * rate);
                let (si, co) = libm::sincos(TAU * vco.phase);
                let mut hs = si;
                let mut hc = co;
                let mut expected = vco.dc;
                let (pass, stop) = vco.band_ratios();
                // Independent original serial recurrence and per-harmonic
                // band decision, including empty bands and boundary tails.
                for i in 0..HARMONICS {
                    let ratio = (i + 1) as f64 * frequency / rate;
                    if ratio >= stop {
                        break;
                    }
                    let gain = if ratio <= pass {
                        1.0
                    } else {
                        0.5 + 0.5 * libm::cos(PI * (ratio - pass) / (stop - pass))
                    } * vco.cap_gain[i];
                    expected += gain * (vco.cosine[i] * hc + vco.sine[i] * hs);
                    let next = hc * co - hs * si;
                    hs = hs * co + hc * si;
                    hc = next;
                }
                let actual = vco.tick(cv, tuning);
                assert!(
                    (actual.saw - expected).abs() < 2e-11,
                    "rate={rate} n={n} difference={}",
                    actual.saw - expected
                );
            }
        }
    }

    #[test]
    fn exponential_junction_inverse_converges_across_bounded_drive() {
        for i in 0..=20_000 {
            let t = -29.999 + i as f64 * (20_000.0 + 29.999) / 20_000.0;
            let u = junction_w(t);
            assert!(u.is_normal() && u > 0.0);
            // Independent libm log checks the actual load-line equation,
            // including the polynomial-log error and all four Halley steps.
            let residual = u + libm::log(u) - t;
            assert!(residual.abs() < 5e-10, "t={t}, residual={residual}");
        }
        for i in 0..=10_000 {
            let x = libm::exp(-30.0 + i as f64 * 50.0 / 10_000.0);
            assert!((junction_log(x) - libm::log(x)).abs() < 5e-13);
        }
        assert_eq!(junction_w(-30.0), 0.0);
        assert!(junction_w(-29.999) < 1e-13);
    }

    #[test]
    fn nonlinear_q8_satisfies_junction_law_and_capacitor_kcl() {
        for rate in [44_100.0, 48_000.0, 96_000.0, 192_000.0, 384_000.0] {
            let mut q = SquareShaper::new(Components::NOMINAL);
            q.prepare(rate).unwrap();
            q.emitter = 8.5;
            for saw in [5.3, 6.5, 7.9, 8.4, 9.2, 11.5, 12.0] {
                let old_cap = q.input_cap;
                let old_emitter = q.emitter;
                let collector = q.tick(saw);
                let base =
                    saw - old_cap - (q.input_cap - old_cap) / (q.dt_over_input_cap * q.base_ac_g);
                let ib = q.base_dc_g * (base - saw) + q.base_ac_g * (base - saw + old_cap);
                let ic = (collector - q.c.square_return) * q.collector_g;
                let ie = q.emitter_g * (q.c.square_supply - q.emitter)
                    + q.emitter_cap_g * (old_emitter - q.emitter);
                assert!((ie - ib - ic).abs() < 1e-12);
                if ib > 1e-12 {
                    let junction_ib = 100e-6 / q.c.transistor_beta
                        * libm::exp((q.emitter - base - q.c.transistor_vbe) / q.junction_vt);
                    assert!((ib / junction_ib - 1.0).abs() < 1e-8);
                }
            }
        }
    }

    #[test]
    fn follower_load_line_and_drain_compliance() {
        let c = Components::NOMINAL;
        for gate in [5.2, 8.35, 11.0, 11.5, 12.0] {
            let source = follower_voltage(c, gate);
            let overdrive = gate - source + c.follower_pinch_off_v;
            let vds = c.square_supply - source;
            let current = c.follower_idss_a / (c.follower_pinch_off_v * c.follower_pinch_off_v)
                * if vds >= overdrive {
                    overdrive * overdrive
                } else {
                    2.0 * overdrive * vds - vds * vds
                };
            assert!((current * c.follower_load_resistance - source).abs() < 1e-11);
            assert!(source > 0.0 && source < c.square_supply);
            let lighter_load = Components {
                follower_load_resistance: 20_000.0,
                ..c
            };
            assert!(follower_voltage(lighter_load, gate) > source);
        }
        let boundary = c.square_supply - c.follower_pinch_off_v;
        assert!(
            (follower_voltage(c, boundary + 1e-7) - follower_voltage(c, boundary - 1e-7)).abs()
                < 2e-7
        );
    }

    #[test]
    fn harmonic_counts_preserve_exact_band_edge_decisions() {
        for rate in [48_000.0, 96_000.0, 192_000.0] {
            for edge in [0.40, 0.48, 20_000.0 / rate, 24_000.0 / rate] {
                for h in 1..=HARMONICS {
                    let boundary = edge * rate / h as f64;
                    for frequency in [
                        f64::from_bits(boundary.to_bits() - 1),
                        boundary,
                        f64::from_bits(boundary.to_bits() + 1),
                    ] {
                        for inclusive in [false, true] {
                            let expected = (1..=HARMONICS)
                                .take_while(|h| {
                                    let ratio = *h as f64 * frequency / rate;
                                    if inclusive {
                                        ratio <= edge
                                    } else {
                                        ratio < edge
                                    }
                                })
                                .count();
                            assert_eq!(
                                harmonics_in_band(frequency, rate, edge, inclusive),
                                expected
                            );
                        }
                    }
                }
            }
        }
    }

    #[test]
    fn timing_capacitor_changes_frequency_not_converter_current() {
        let a = Vco::new(Components::NOMINAL);
        let b = Vco::new(Components {
            timing_capacitance: 20e-9,
            ..Components::NOMINAL
        });
        assert!((a.frequency_hz(2.75, 0.5) / b.frequency_hz(2.75, 0.5) - 2.0).abs() < 1e-12);
        assert!((a.charging_current_a(2.75, 0.5) - b.charging_current_a(2.75, 0.5)).abs() < 1e-18);
    }

    #[test]
    fn unloaded_c11_recharges_through_r45() {
        for rate in [48_000.0, 96_000.0, 192_000.0] {
            let mut s = SquareShaper::new(Components::NOMINAL);
            s.prepare(rate).unwrap();
            s.emitter = 6.0;
            let tau = s.c.square_emitter_resistance * s.c.square_emitter_capacitance;
            let n = (rate * tau).round() as usize;
            for _ in 0..n {
                s.tick(12.0);
            }
            assert!(
                (s.emitter - (12.0 - 6.0 * libm::exp(-(n as f64) / (rate * tau)))).abs() < 0.0014
            );
        }
    }

    #[test]
    fn c10_off_state_time_constant_includes_r35_return() {
        for rate in [48_000.0, 96_000.0, 192_000.0] {
            let mut s = SquareShaper::new(Components::NOMINAL);
            s.prepare(rate).unwrap();
            s.input_cap = 1.0;
            let tau = (s.c.square_input_resistance + s.c.square_dc_resistance)
                * s.c.square_input_capacitance;
            let n = (rate * tau).round() as usize;
            for _ in 0..n {
                s.tick(20.0);
            }
            let expected = libm::exp(-(n as f64) / (rate * tau));
            assert!((s.input_cap - expected).abs() < 0.0045);
        }
    }
}

#[cfg(test)]
mod stb33_glide {
    use super::*;
    #[test]
    fn pitch_glide_swoops_down_and_settles() {
        let mut cv = PitchCv::new(Components::NOMINAL);
        cv.prepare(48_000.0).unwrap();
        cv.set_glide(1.0, 1.0);
        let target = PitchCv::cv_for_note(12);
        let start = cv.tick_glide(12, false, true);
        assert!(start > target + 0.9, "swoop starts about an octave up");
        let mut last = start;
        for _ in 0..48_000 {
            last = cv.tick_glide(12, false, false);
        }
        assert!((last - target).abs() < 1e-6, "swoop lands on the note");
        cv.set_glide(1.0, 0.0);
        assert_eq!(cv.tick_glide(5, false, true), PitchCv::cv_for_note(5), "off = stock pitch");
    }
}
