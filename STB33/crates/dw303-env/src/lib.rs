#![no_std]
//! Schematic-based TB-303 trigger, envelopes, accent, OTA and line output.
//! All signals are SI node voltages or currents. See `docs/circuit/env.md`
//! for reductions, calibration, integration order and unresolved device models.

/// Unsupported circuit clock.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StageError {
    /// Expected a finite sample rate in 44.1–384 kHz.
    InvalidSampleRate,
}

macro_rules! components {
    ($( $(#[$doc:meta])* $name:ident = $value:expr; )*) => {
        /// Schematic components in ohms, farads and volts, plus device estimates.
        #[derive(Clone, Copy, Debug)]
        pub struct Components {
            $( $(#[$doc])* pub $name: f64, )*
            /// VERIFY: VR3 wiper fractions at 0.1 panel increments, fitted to
            /// session1 ring/grid spectra; see vcf-calibration.md.
            pub vr3_taper: [f64; 11],
            /// VERIFY: VR5 electrical fractions at tenth-turn intervals, fitted
            /// to session1 envelope spectra; printed A law is unspecified.
            pub vr5_taper: [f64; 11],
            /// VERIFY: VR6 electrical fractions at tenth-turn intervals, fitted
            /// to session1 effective decay; output-only identification.
            pub vr6_taper: [f64; 11],
        }
        impl Components {
            /// Reconciled service-note values; device assumptions remain flagged.
            pub const NOMINAL: Self = Self {
                $( $name: $value, )*
                vr3_taper: [0.0, 0.016988877, 0.048210059, 0.078453452,
                            0.100557131, 0.124966812, 0.1415,
                            0.292831010, 0.621164626, 0.978428507, 1.0],
                vr5_taper: [0.0, 0.00689807, 0.01760141, 0.03420803,
                            0.05967361, 0.1, 0.16180566, 0.36, 0.65, 0.92, 1.0],
                vr6_taper: [0.0, 0.038644737, 0.07275, 0.10875, 0.133381579,
                            0.161802632, 0.195907895, 0.398644737, 0.616539474,
                            0.815486842, 1.0],
            };
            fn guarded(mut self) -> Self {
                let zero_trim = self.tm3 == 0.0;
                $( self.$name = if self.$name.is_finite() && self.$name > 0.0 {
                    self.$name.clamp(1e-15, 1e9)
                } else { Self::NOMINAL.$name }; )*
                self.tm3 = self.tm3.min(470e3);
                if zero_trim { self.tm3 = 0.0; }
                if self.vr3_taper[0] != 0.0 || self.vr3_taper[10] != 1.0
                    || self.vr3_taper.windows(2).any(|p| !p[0].is_finite()
                        || !p[1].is_finite() || p[0] > p[1]) {
                    self.vr3_taper = Self::NOMINAL.vr3_taper;
                }
                for (track, nominal) in [
                    (&mut self.vr5_taper, Self::NOMINAL.vr5_taper),
                    (&mut self.vr6_taper, Self::NOMINAL.vr6_taper),
                ] {
                    if track[0] != 0.0 || track[10] != 1.0
                        || track.windows(2).any(|p| !p[0].is_finite()
                            || !p[1].is_finite() || p[0] > p[1]) {
                        *track = nominal;
                    }
                }
                self
            }
        }
    };
}
components! {
    /// IC16 pin 1, labelled +12 V rail.
    supply = 12.0;
    /// IC16 pin 7, labelled +5.333 V rail.
    reference = 5.333;
    /// R148, Q38 base pulse drive.
    r148 = 10e3;
    /// R149, Q38 base return to +12 V.
    r149 = 10e3;
    /// C54, differentiator from N_GATE to R148.
    c54 = 47e-9;
    /// R152, D37 to C62 charge resistance.
    r152 = 100.0;
    /// C62, MEG storage to ground.
    /// VERIFY: 1 µF nominal → 0.95 µF fitted electrolytic tolerance.
    c62 = 0.95e-6;
    /// VERIFY: effective D27/D28 forward drop at the low MEG discharge current.
    /// D26 retains the shared drop because R117 biases it at about 0.2 mA.
    /// Generic 0.6 V → 0.45 V; the full current-dependent diode law is reduced.
    meg_discharge_drop = 0.45;
    /// R138, fixed MEG discharge resistance.
    /// VERIFY: 68 kΩ nominal → 71.4 kΩ fitted (+5%) to preserve accent decay.
    r138 = 71.4e3;
    /// VR6, 1 MΩ A DECAY rheostat, wiper strapped to terminal 3.
    /// VERIFY: 1 MΩ nominal → 0.8 MΩ fitted track (−20% tolerance limit).
    vr6 = 0.8e6;
    /// VR7, 50 kΩ B ACCENT divider.
    vr7 = 50e3;
    /// R46, D24 to resonance gang B terminal 1.
    r46 = 47e3;
    /// VR4B, 50 kΩ B RESONANCE second gang.
    vr4b = 50e3;
    /// C13, resonance gang B terminal 3 to ground.
    /// VERIFY: 1 µF nominal → 0.8 µF fitted sweep capacitor tolerance.
    c13 = 0.8e-6;
    /// R72, gang B wiper to EXP_B.
    r72 = 100e3;
    /// R71, EXP_B to VQ9.
    r71 = 2.2e3;
    /// R64 and R65, equal Q9 bias divider arms.
    q9_divider = 10e3;
    /// VR3, 50 kΩ A CUTOFF track.
    vr3 = 50e3;
    /// R47, VR3 terminal 1 to VQ9.
    r47 = 10e3;
    /// R73, cutoff wiper to Q10 emitter through TM3.
    r73 = 100e3;
    /// TM3, 470 kΩ FREQ rheostat setting (not its full track value).
    /// VERIFY: fitted 470 kΩ endpoint with expo ratio; these two parameters are
    /// not separately identifiable from audio. See vcf-calibration.md.
    tm3 = 470e3;
    /// VR5, 50 kΩ A ENV MOD divider.
    vr5 = 50e3;
    /// R61, VR5 terminal 1 to VQ9.
    /// VERIFY: 10 kΩ printed → 9.5 kΩ session1/2 interaction fit, within
    /// assumed −5% resistor tolerance. Audio does not identify R61 separately
    /// from pot track, Q9 reference and the voice's expo-ratio calibration.
    r61 = 9.5e3;
    /// R63, VR5 wiper to EXP_B.
    r63 = 220e3;
    /// R69, Q12 emitters to Q11 collector; limits Q11 compliance.
    r69 = 2.2e3;
    /// R142, Q36 base return to +12 V.
    /// VERIFY: 10 kΩ nominal → 10.5 kΩ release fit (+5% tolerance).
    r142 = 10.5e3;
    /// R143, Q37 collector to Q36 base.
    /// VERIFY: 10 kΩ nominal → 9.5 kΩ attack fit (−5% tolerance).
    r143 = 9.5e3;
    /// R145, Q36 collector load to ground.
    r145 = 10e3;
    /// C72, Q36 base-emitter storage (+ at the +12 V rail).
    /// VERIFY: 10 µF printed → 11 µF joint session1/2 gate-shape fit (+10%).
    /// R142/R143 and Q36's junction law are not separately identified by audio.
    c72 = 11e-6;
    /// VERIFY: Q36 forward-active collector capacity at `q36_vbe`.
    /// Gate-load estimate 1.18 mA → 1.90 mA fitted saturation headroom; the actual
    /// collector current is limited by R145 and Q31's emitter load. This is
    /// an effective junction parameter, not an internal-node measurement.
    q36_reference_current = 1.90e-3;
    /// VERIFY: Q36 PNP forward-junction reference, not a measured Vbe.
    /// 0.65 V estimate → 0.40 V preceding fit → 0.50 V gate-shape fit.
    /// This also clamps the C72 drive in the reduced saturation model; the
    /// junction reference and saturation charge are not separately identified.
    q36_vbe = 0.50;
    /// R123, C42 recharge from +12 V.
    r123 = 1.5e6;
    /// C42, Q31 base storage to ground, positive at the base.
    /// VERIFY: 1 µF nominal → 1.05 µF session1 held-note tolerance fit.
    c42 = 1.05e-6;
    /// R130, Q32 emitter degeneration.
    r130 = 100.0;
    /// R132, C42 to Q32 collector.
    r132 = 100.0;
    /// R134, D36 to Q32 base.
    r134 = 22e3;
    /// C41, Q32 base pulse stretcher to ground.
    c41 = 100e-9;
    /// R131, Q31 emitter supply.
    r131 = 220e3;
    /// R133, in series with R131 after D35.
    r133 = 2.2e3;
    /// R119, accent wiper to C36.
    /// VERIFY: 47 kΩ nominal → 49.35 kΩ accent-current tolerance fit.
    r119 = 49.35e3;
    /// R120, C36 to the VCA duplicate D27.
    /// VERIFY: 22 kΩ nominal → 23.1 kΩ accent-current tolerance fit.
    r120 = 23.1e3;
    /// C36, VCA accent-current smoothing.
    c36 = 33e-9;
    /// R121, VCF output to BA662A pin 3 via C21.
    r121 = 220e3;
    /// R122, resonance wiper to BA662A pin 3 via VCA C22.
    /// VERIFY: 100 kΩ nominal → 95 kΩ fitted (−5% assumed tolerance).
    /// Session1/2 high-RES level evidence does not identify R122 separately
    /// from C22, the tap source impedance or the unmeasured BA662A transfer.
    r122 = 95e3;
    /// R124, BA662A pin 3 bias resistance; R125 biases pin 2 equally.
    r124 = 2.2e3;
    /// C21, VCF output coupling capacitor.
    c21 = 10e-9;
    /// C22 (VCA), resonance wiper coupling capacitor, distinct from VCF C22.
    c22_vca = 10e-9;
    /// R127, in series with R129 at the OTA output.
    r127 = 47e3;
    /// R129, series OTA load arm, not an additional parallel resistor.
    r129 = 22e3;
    /// R128, parallel with R127 + R129.
    r128 = 220e3;
    /// C38, BA662A buffer output to VR8 terminal 3.
    /// VERIFY: 1 µF nominal → 0.8 µF fitted output coupling tolerance.
    c38 = 0.8e-6;
    /// VR8, 50 kΩ A VOLUME track.
    vr8 = 50e3;
    /// R153, mixer feedback from Q33 output to Q34 emitter.
    r153 = 100e3;
    /// R155, VOLUME wiper to C59 and mixer SUM.
    r155 = 47e3;
    /// R156, MIX IN to C58 and mixer SUM.
    r156 = 100e3;
    /// C59, VOLUME path coupling to SUM.
    /// VERIFY: 1 µF nominal → 0.8 µF fitted mixer coupling tolerance.
    c59 = 0.8e-6;
    /// C58, MIX IN coupling to SUM.
    c58 = 1e-6;
    /// C56, mixer output coupling to R161.
    /// VERIFY: 1 µF nominal → 0.8 µF fitted line coupling tolerance.
    c56 = 0.8e-6;
    /// R161, output series resistance.
    r161 = 1e3;
    /// R165, output jack shunt to ground (unloaded jack).
    r165 = 100e3;
    /// VERIFY: effective Si forward drop for D24/D26–D28/D35–D37; not printed.
    diode_drop = 0.6;
    /// VERIFY: Q9/Q10/Q11/Q31/Q32/Q40/Q41 junction drop, generic Si model.
    vbe = 0.65;
    /// VERIFY: Q36/Q38/Q32 saturation voltage, not printed.
    saturation = 0.2;
    /// VERIFY: Q32/Q36 current gain; type/β not assigned on the schematic.
    beta = 200.0;
    /// VERIFY: Q10/Q11 and IC15 junction thermal voltage at about 300 K.
    thermal_voltage = 0.0259;
    /// VERIFY: Is(Q11)/Is(Q10) effective ratio fitted to session1 ring/grid.
    expo_is_ratio = 3.074_323_408_395_499;
    /// VERIFY: BA662A pin 1 bias voltage, absent from service-note transfer data.
    ota_control_voltage = 0.65;
    /// VERIFY: Q31 PNP current gain for base current charging C42; generic
    /// 2SA733-class estimate, not a printed or independently measured value.
    q31_beta = 200.0;
    /// VERIFY: Q31 collector current at the reference Vbe; inferred device law,
    /// not a printed part parameter or internal-node measurement.
    q31_reference_current = 47e-6;
    /// VERIFY: BA662A current-mirror factor in Iout = factor*Iabc*tanh(Vdiff/2VT).
    ota_mirror_ratio = 1.0;
}

// A bounded interpolation of the fitted electrical pot track, not an envelope.
fn track_fraction(track: [f64; 11], rotation: f64) -> f64 {
    let position = rotation * 10.0;
    let index = (position as usize).min(9);
    track[index] + (track[index + 1] - track[index]) * (position - index as f64)
}

/// Per-stage sanitisation telemetry, cleared by reset.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Diagnostics {
    /// Non-finite floating-point arguments replaced by zero (saturating count).
    pub non_finite_inputs: u64,
}

#[derive(Clone, Copy, Default)]
struct Coefficient {
    key: Option<(u64, u64)>,
    k: f64,
    average: f64,
}
struct Fixed {
    accent_threshold: f64,
    accent_source_weight: f64,
    accent_threshold_weight: f64,
    accent_tau_on: f64,
    accent_tau_off: f64,
    accent_inverse_r: f64,
    ota_scale: f64,
    q31_scale: f64,
    q31_log_reference: f64,
    inverse_two_vt: f64,
    line_tau: f64,
    line_gain: f64,
    mix_gain: f64,
}

// Positive root of y + ln(y) = x. Large forward currents permit an
// asymptotic seed followed by one Newton step, retaining the junction law.
fn q31_forward_root(x: f64) -> f64 {
    let log_x = libm::log(x);
    let inverse = 1.0 / x;
    let y = x - log_x + log_x * inverse + 0.5 * log_x * (log_x - 2.0) * inverse * inverse;
    y - (y + libm::log(y) - x) * y / (y + 1.0)
}

impl Fixed {
    fn new(c: Components) -> Self {
        let r = parallel(c.r119, c.r120);
        Self {
            accent_threshold: c.diode_drop + c.ota_control_voltage,
            accent_source_weight: r / c.r119,
            accent_threshold_weight: r / c.r120,
            accent_tau_on: r * c.c36,
            accent_tau_off: c.r119 * c.c36,
            accent_inverse_r: 1.0 / c.r120,
            ota_scale: parallel(c.r128, c.r129 + c.r127) * c.ota_mirror_ratio,
            q31_scale: c.thermal_voltage / (c.r131 + c.r133),
            q31_log_reference: libm::log(
                c.q31_reference_current * (c.r131 + c.r133) / c.thermal_voltage,
            ),
            inverse_two_vt: 1.0 / (2.0 * c.thermal_voltage),
            line_tau: (c.r161 + c.r165) * c.c56,
            line_gain: c.r165 / (c.r161 + c.r165),
            mix_gain: -c.r153 / c.r156,
        }
    }
}
struct Circuit {
    c: Components,
    dt: f64,
    bad: u64,
    coefficients: [Coefficient; 8],
    tapers: [Option<(u64, f64)>; 2],
    input_g: [f64; 2],
    input_denominator: f64,
    input_step: [f64; 2],
    output_g: [f64; 2],
    output_inverse: [f64; 3],
    fixed: Fixed,
}
impl Circuit {
    fn new(c: Components) -> Self {
        let c = c.guarded();
        let mut s = Self {
            c,
            dt: 1.0 / 48_000.0,
            bad: 0,
            coefficients: [Coefficient::default(); 8],
            tapers: [None; 2],
            input_g: [0.0; 2],
            input_denominator: 1.0,
            input_step: [0.0; 2],
            output_g: [0.0; 2],
            output_inverse: [0.0; 3],
            fixed: Fixed::new(c),
        };
        s.cache_inputs();
        s
    }
    fn cache_inputs(&mut self) {
        self.input_g = [
            1.0 / (self.c.r121 + self.dt / (2.0 * self.c.c21)),
            1.0 / (self.c.r122 + self.dt / (2.0 * self.c.c22_vca)),
        ];
        self.input_denominator = 1.0 / (1.0 / self.c.r124 + self.input_g[0] + self.input_g[1]);
        self.input_step = [self.dt / self.c.c21, self.dt / self.c.c22_vca];
        // Midpoint companions for the coupled C38/VR8/C59/R155 network.
        // C59's branch loads the C38 node; treating both high passes as
        // independent misses that current during attacks and releases.
        self.output_g = [2.0 * self.c.c38 / self.dt, 2.0 * self.c.c59 / self.dt];
        let b = 1.0 / self.c.r155;
        let a = self.output_g[0] + 1.0 / self.c.vr8 + b;
        let d = self.output_g[1] + b;
        let inverse_det = 1.0 / ((self.output_g[0] + 1.0 / self.c.vr8) * d + b * self.output_g[1]);
        self.output_inverse = [d * inverse_det, b * inverse_det, a * inverse_det];
    }
    fn prepare(&mut self, rate: f64) -> Result<(), StageError> {
        if !rate.is_finite() || !(44_100.0..=384_000.0).contains(&rate) {
            return Err(StageError::InvalidSampleRate);
        }
        self.dt = 1.0 / rate;
        self.cache_inputs();
        Ok(())
    }
    fn input(&mut self, x: f64) -> f64 {
        if x.is_finite() {
            x
        } else {
            self.bad = self.bad.saturating_add(1);
            0.0
        }
    }
    fn knob(&mut self, x: f64) -> f64 {
        self.input(x).clamp(0.0, 1.0)
    }
    /// VERIFY: A-pot marking is printed, but its exact track law is unavailable;
    /// use the estimated 10%-at-half-rotation taper until hardware calibration.
    fn audio(&mut self, slot: usize, x: f64) -> f64 {
        if let Some((key, y)) = self.tapers[slot]
            && key == x.to_bits()
        {
            return y;
        }
        let y = libm::expm1(4.394_449_154_672_439 * x) / 80.0;
        self.tapers[slot] = Some((x.to_bits(), y));
        y
    }
    fn k(&mut self, slot: usize, time: f64, tau: f64) -> f64 {
        let tau = tau.clamp(1e-12, 1e12);
        let key = (time.to_bits(), tau.to_bits());
        if self.coefficients[slot].key != Some(key) {
            let k = -libm::expm1(-time / tau);
            self.coefficients[slot] = Coefficient {
                key: Some(key),
                k,
                average: if time > 0.0 { k * tau / time } else { 1.0 },
            };
        }
        self.coefficients[slot].k
    }
    fn rc(&mut self, slot: usize, old: f64, target: f64, time: f64, tau: f64) -> f64 {
        flush(old + (target - old) * self.k(slot, time, tau))
    }
    fn high_pass(&mut self, slot: usize, cap: &mut f64, input: f64, tau: f64) -> f64 {
        let delta = input - *cap;
        let k = self.k(slot, self.dt, tau);
        *cap = flush(*cap + delta * k);
        flush(delta * self.coefficients[slot].average)
    }
    fn reference(&self) -> f64 {
        self.c.reference * 0.5 + self.c.vbe
    }
}
fn flush(x: f64) -> f64 {
    if x.abs() < 1e-24 { 0.0 } else { x }
}
fn parallel(a: f64, b: f64) -> f64 {
    a * b / (a + b)
}

macro_rules! lifecycle {
    ($($field:ident = $init:expr),* $(,)?) => {
        /// Construct a circuit at 48 kHz with its capacitors at quiescent DC.
        pub fn new(c: Components) -> Self {
            let circuit = Circuit::new(c);
            Self { $($field: $init(&circuit),)* circuit }
        }
        /// Change the circuit clock, preserving capacitor charge and diagnostics.
        pub fn prepare(&mut self, sample_rate: f64) -> Result<(), StageError> {
            self.circuit.prepare(sample_rate)
        }
        /// Restore quiescent DC charge and clear diagnostics, retaining the clock.
        pub fn reset(&mut self) {
            $(self.$field = $init(&self.circuit);)* self.circuit.bad = 0;
        }
        /// Read the sanitisation counter.
        pub fn diagnostics(&self) -> Diagnostics {
            Diagnostics { non_finite_inputs: self.circuit.bad }
        }
    };
}

/// Q38 edge one-shot, storing the differentiated C54 drive voltage.
pub struct TriggerPulse {
    circuit: Circuit,
    drive: f64,
    previous: bool,
}
impl TriggerPulse {
    lifecycle!(drive = |_: &Circuit| 0.0, previous = |_: &Circuit| false);
    /// A rising trigger starts the Q38 pulse; a held trigger does not retrigger.
    /// Returns TRIG volts (about 11.8 V for about 1.4 ms), not a Boolean gate.
    pub fn tick(&mut self, trigger: bool) -> f64 {
        // A discharged C54 is an exact fixed point until the next edge.
        if self.drive == 0.0 && !(trigger && !self.previous) {
            self.previous = trigger;
            return 0.0;
        }
        let c = self.circuit.c;
        if trigger && !self.previous {
            self.drive = c.supply - c.vbe;
        }
        self.previous = trigger;
        let on = self.drive / c.r148 > c.vbe / c.r149;
        self.drive = self
            .circuit
            .rc(0, self.drive, 0.0, self.circuit.dt, c.r148 * c.c54);
        if on { c.supply - c.saturation } else { 0.0 }
    }
}

/// C62, the D37 charge path, IC12 decay short and Q41/Q40 buffer.
pub struct Meg {
    circuit: Circuit,
    capacitor: f64,
}
impl Meg {
    lifecycle!(capacitor = |s: &Circuit| s.c.diode_drop + 2.0 * s.c.meg_discharge_drop);
    /// Buffered ENV_OUT voltage; DECAY is the VR6 rotation and accent shorts VR6.
    /// D26–D28 are a fixed-drop clamp in this device reduction.
    pub fn tick(&mut self, pulse: f64, accent: bool, decay: f64) -> f64 {
        let pulse = self
            .circuit
            .input(pulse)
            .max(0.0)
            .min(self.circuit.c.supply);
        let decay = self.circuit.knob(decay);
        let track = track_fraction(self.circuit.c.vr6_taper, decay);
        let c = self.circuit.c;
        let r = c.r138 + if accent { 0.0 } else { track * c.vr6 };
        let floor = c.diode_drop + 2.0 * c.meg_discharge_drop;
        let source = pulse - c.diode_drop;
        let (target, tau, slot) = if source > self.capacitor {
            let rth = parallel(r, c.r152);
            ((source / c.r152 + floor / r) * rth, rth * c.c62, 0)
        } else {
            (floor, r * c.c62, 1)
        };
        self.capacitor = self
            .circuit
            .rc(slot, self.capacitor, target, self.circuit.dt, tau);
        (self.capacitor - 2.0 * c.vbe).max(0.0)
    }
    /// C62 voltage before the two emitter-follower junction drops.
    pub fn capacitor_voltage(&self) -> f64 {
        self.capacitor
    }
}

/// IC12 1/2 accent gate and loaded linear VR7 divider.
pub struct Accent {
    circuit: Circuit,
}
impl Accent {
    lifecycle!();
    /// Return the loaded VR7 wiper voltage, zero when IC12 is open.
    /// The printed track supplies `VR7*k*(1-k)` Thevenin resistance into
    /// R119/R120 and D27's conducting threshold.
    /// VERIFY: uses the fast C36 branch's steady load; omits its transient
    /// back-current and the simultaneous D24/sweep branch load. The contracted
    /// voltage-only stage boundary cannot exchange those branch currents.
    pub fn tick(&mut self, accent: bool, meg: f64, accent_knob: f64) -> f64 {
        // IC12's open accent gate returns zero. Retain input sanitisation
        // counters even though neither voltage affects an inactive output.
        if !accent {
            self.circuit.input(meg);
            self.circuit.input(accent_knob);
            return 0.0;
        }
        let meg = self.circuit.input(meg).max(0.0).min(self.circuit.c.supply);
        let k = self.circuit.knob(accent_knob);
        let resistance = self.circuit.c.vr7 * k * (1.0 - k);
        let source = meg * k;
        let threshold = self.circuit.c.diode_drop + self.circuit.c.ota_control_voltage;
        let load = self.circuit.c.r119 + self.circuit.c.r120;
        if source > threshold {
            (source * load + threshold * resistance) / (load + resistance)
        } else {
            source
        }
    }
}

/// D24/R46/VR4B/C13 with the R72 load into the expo reference.
pub struct AccentSweep {
    circuit: Circuit,
    capacitor: f64,
    network: Option<(u64, SweepNetwork)>,
}
#[derive(Clone, Copy)]
struct SweepNetwork {
    rb: f64,
    off_fraction: f64,
    off_tau: f64,
    on_tau: f64,
    target_source: f64,
    target_reference: f64,
    wiper_source: f64,
    wiper_reference: f64,
    wiper_capacitor: f64,
}
impl AccentSweep {
    #[cold]
    #[inline(never)]
    fn update_network(&mut self, k: f64) -> SweepNetwork {
        let c = self.circuit.c;
        let ra = c.r46 + k * c.vr4b;
        let rb = (1.0 - k) * c.vr4b;
        let load = c.r72 + c.r71;
        let rth = parallel(ra, load);
        let wiper_denominator = if rb > 0.0 {
            1.0 / ra + 1.0 / load + 1.0 / rb
        } else {
            1.0
        };
        let n = SweepNetwork {
            rb,
            off_fraction: load / (rb + load),
            off_tau: (rb + load) * c.c13,
            on_tau: (rb + rth) * c.c13,
            target_source: rth / ra,
            target_reference: rth / load,
            wiper_source: 1.0 / ra / wiper_denominator,
            wiper_reference: 1.0 / load / wiper_denominator,
            wiper_capacitor: if rb > 0.0 {
                1.0 / rb / wiper_denominator
            } else {
                1.0
            },
        };
        self.network = Some((k.to_bits(), n));
        n
    }
    lifecycle!(
        capacitor = |s: &Circuit| s.reference(),
        network = |_: &Circuit| None
    );
    /// Gang B moves from terminal 1 (direct) toward terminal 3 (C13) as RES rises.
    /// VERIFY: gang direction correlation is an open item in the transcription.
    /// The R72 load is reduced against stiff VQ9 through R71.
    pub fn tick(&mut self, accent_wiper: f64, resonance: f64) -> f64 {
        let source = self
            .circuit
            .input(accent_wiper)
            .max(0.0)
            .min(self.circuit.c.supply)
            - self.circuit.c.diode_drop;
        let k = self.circuit.knob(resonance);
        let reference = self.circuit.reference();
        let n = if let Some((key, n)) = self.network
            && key == k.to_bits()
        {
            n
        } else {
            self.update_network(k)
        };
        let off_w = reference + (self.capacitor - reference) * n.off_fraction;
        let on_target = source * n.target_source + reference * n.target_reference;
        let mut conducting = source > off_w;
        let mut remaining = self.circuit.dt;
        if !conducting && source > reference {
            let cross = reference + (source - reference) / n.off_fraction;
            let next = self
                .circuit
                .rc(0, self.capacitor, reference, remaining, n.off_tau);
            if next < cross && self.capacitor > cross {
                let time = (n.off_tau
                    * libm::log((self.capacitor - reference) / (cross - reference)))
                .clamp(0.0, remaining);
                self.capacitor = cross;
                remaining -= time;
                conducting = true;
            }
        }
        self.capacitor = if conducting {
            self.circuit
                .rc(1, self.capacitor, on_target, remaining, n.on_tau)
        } else {
            self.circuit
                .rc(0, self.capacitor, reference, remaining, n.off_tau)
        };
        if n.rb == 0.0 {
            self.capacitor
        } else if conducting {
            source * n.wiper_source
                + reference * n.wiper_reference
                + self.capacitor * n.wiper_capacitor
        } else {
            reference + (self.capacitor - reference) * n.off_fraction
        }
    }
    /// C13 positive-plate voltage, including its VQ9-related rest offset.
    pub fn capacitor_voltage(&self) -> f64 {
        self.capacitor
    }
}

/// Loaded cutoff/ENV MOD dividers and complementary Q10/Q11 expo converter.
/// VR3 uses the fitted resistive track in `Components::vr3_taper`; all other
/// stages retain their own pot laws. See `docs/circuit/vcf-calibration.md`.
pub struct FilterControl {
    circuit: Circuit,
    network: Option<(u64, u64, FilterNetwork)>,
    cached_current: Option<([u64; 4], f64)>,
}
#[derive(Clone, Copy)]
struct FilterNetwork {
    cutoff_current: f64,
    env_exponent: f64,
    sweep_exponent: f64,
    compliance: f64,
}
impl FilterControl {
    #[cold]
    #[inline(never)]
    fn update_network(&mut self, cutoff: f64, env: f64) -> FilterNetwork {
        let c = self.circuit.c;
        let reference = self.circuit.reference();
        // Linear interpolation of a monotone measured resistive track law.
        // Eleven fixed knots; no allocation, search or envelope state.
        let position = cutoff * 10.0;
        let index = (position as usize).min(9);
        let kc = c.vr3_taper[index]
            + (c.vr3_taper[index + 1] - c.vr3_taper[index]) * (position - index as f64);
        let ke = track_fraction(c.vr5_taper, env);
        let low_c = c.r47 + kc * c.vr3;
        let high_c = (1.0 - kc) * c.vr3;
        let vw = reference + (c.supply - reference) * low_c / (low_c + high_c);
        let rw = parallel(low_c, high_c);
        let low_e = c.r61 + ke * c.vr5;
        let high_e = (1.0 - ke) * c.vr5;
        let fraction = low_e / (low_e + high_e);
        let re = c.r63 + parallel(low_e, high_e);
        let denominator = 1.0 / c.r71 + 1.0 / re + 1.0 / c.r72;
        let n = FilterNetwork {
            cutoff_current: c.expo_is_ratio
                * ((vw - reference - c.vbe) / (c.r73 + c.tm3 + rw)).max(0.0),
            env_exponent: fraction / re / denominator / c.thermal_voltage,
            sweep_exponent: 1.0 / c.r72 / denominator / c.thermal_voltage,
            compliance: ((c.reference - c.vbe - reference - c.saturation) / c.r69).max(0.0),
        };
        self.network = Some((cutoff.to_bits(), env.to_bits(), n));
        n
    }
    lifecycle!(
        network = |_: &Circuit| None,
        cached_current = |_: &Circuit| None
    );
    /// Return Q11 collector (total ladder tail) current in amperes.
    /// References are VQ9, not ground; R61 gives a residual ENV MOD feed at CCW.
    /// Q11 compliance is limited by R69 and the estimated Q12/Q11 junction drops.
    pub fn ladder_current(&mut self, cutoff: f64, env_mod: f64, meg: f64, sweep_wiper: f64) -> f64 {
        let cutoff = self.circuit.knob(cutoff);
        let env = self.circuit.knob(env_mod);
        let meg = self.circuit.input(meg).max(0.0).min(self.circuit.c.supply);
        let sweep = self
            .circuit
            .input(sweep_wiper)
            .max(0.0)
            .min(self.circuit.c.supply);
        // Q10/Q11 have no dynamic state in this reduction. Once their four
        // sanitized controls are bitwise identical, reuse the exact current.
        // Sanitisation above still runs and retains every diagnostic count.
        let key = [
            cutoff.to_bits(),
            env.to_bits(),
            meg.to_bits(),
            sweep.to_bits(),
        ];
        if let Some((previous, current)) = self.cached_current
            && previous == key
        {
            return current;
        }
        let reference = self.circuit.reference();
        let n = if let Some((kc, ke, n)) = self.network
            && kc == cutoff.to_bits()
            && ke == env.to_bits()
        {
            n
        } else {
            self.update_network(cutoff, env)
        };
        let exponent = n.env_exponent * (meg - reference) + n.sweep_exponent * (sweep - reference);
        let current = (n.cutoff_current
            * libm::exp2(exponent.clamp(-80.0, 40.0) * core::f64::consts::LOG2_E))
        .min(n.compliance);
        self.cached_current = Some((key, current));
        current
    }
    /// Q9 emitter reference, from the equal R64/R65 divider plus a PNP Vbe.
    pub fn reference_voltage(&self) -> f64 {
        self.circuit.reference()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ota_pair_preserves_the_junction_law_and_branch_boundaries() {
        let mut previous = -1.0;
        for i in 0..=100_000 {
            let z = f64::from(i) / 100_000.0 - 0.5;
            let pair = ota_pair(z);
            assert!((pair - libm::tanh(z)).abs() <= 5e-16);
            assert!(pair >= previous);
            assert_eq!(ota_pair(-z), -pair);
            previous = pair;
        }
        for boundary in [-0.5, -1e-4, 1e-4, 0.5] {
            for offset in [-f64::EPSILON, 0.0, f64::EPSILON] {
                let z = boundary + offset;
                assert!((ota_pair(z) - libm::tanh(z)).abs() <= 5e-16);
            }
        }
        for z in [-100.0, -2.0, 2.0, 100.0] {
            assert_eq!(ota_pair(z), libm::tanh(z));
        }
    }

    #[test]
    fn q31_forward_root_satisfies_the_current_equation() {
        for i in 0..=100_000 {
            let x = 32.0 + f64::from(i) * 0.01;
            let y = q31_forward_root(x);
            assert!(y > 0.0);
            assert!((y + libm::log(y) - x).abs() <= 1e-10);
        }
        // Independently solve at the branch boundary with the original
        // four-step reduction, rather than compare two asymptotic seeds.
        let x = 32.0;
        let mut y = x - libm::log(x);
        for _ in 0..4 {
            y -= (y + libm::log(y) - x) * y / (y + 1.0);
        }
        assert!((q31_forward_root(x) - y).abs() < 1e-10);
    }

    #[test]
    fn cached_rc_composes_and_rate_changes_preserve_charge() {
        let mut s = Circuit::new(Components::NOMINAL);
        let a = s.rc(0, 4.0, 1.0, 0.1, 0.2);
        let b = s.rc(1, 4.0, 1.0, 0.05, 0.2);
        let b = s.rc(1, b, 1.0, 0.05, 0.2);
        assert!((a - b).abs() < 1e-14);
        let mut m = Meg::new(Components::NOMINAL);
        m.tick(11.8, false, 1.0);
        let voltage = m.capacitor_voltage();
        assert!(m.prepare(f64::NAN).is_err());
        m.prepare(192_000.0).unwrap();
        assert_eq!(m.capacitor_voltage(), voltage);
        m.reset();
        assert_eq!(m.circuit.dt, 1.0 / 192_000.0);
        assert_eq!(flush(1e-25), 0.0);
    }

    #[test]
    fn q36_discharge_is_bounded_by_forward_junction_currents() {
        let c = Components::NOMINAL;
        for rate in [48_000.0, 96_000.0, 192_000.0] {
            let mut v = Veg::new(c);
            v.prepare(rate).unwrap();
            for _ in 0..(rate * 0.005) as usize {
                v.tick(11.8);
            }
            for _ in 0..(rate * 0.055) as usize {
                v.tick(0.0);
            }
            let initial_cap = v.capacitor_voltage();
            let initial_current = v.tick(0.0);
            let load =
                (c.supply - c.saturation) / c.r145 + initial_current * (1.0 + 1.0 / c.q31_beta);
            let maximum_base_drop = c.r142 * c.q36_reference_current / c.beta;
            let minimum_base_drop = c.r142 * load / c.beta;
            let threshold =
                c.q36_vbe + c.thermal_voltage * libm::log(load / c.q36_reference_current);
            let minimum_saturation_time = c.r142
                * c.c72
                * libm::log((c.q36_vbe + maximum_base_drop) / (threshold + maximum_base_drop));
            v.set_gate(false);
            let samples = (rate * minimum_saturation_time * 0.5).floor() as usize;
            assert!(samples > 0);
            for _ in 0..samples {
                v.tick(0.0);
            }
            // Independent limiting RC solutions bound the decreasing forward
            // junction current. A load/β-only saturation law falls on the
            // upper bound, omitting the extra forward-junction base current.
            let decay = (-(samples as f64) / rate / (c.r142 * c.c72)).exp();
            let lower = (c.q36_vbe + maximum_base_drop) * decay - maximum_base_drop;
            let upper = (c.q36_vbe + minimum_base_drop) * decay - minimum_base_drop;
            assert!(v.gate_drive >= lower - 5e-5);
            assert!(v.gate_drive < upper - 1e-5);
            assert!(v.collector_current > initial_current * 0.985);
            assert!(v.capacitor_voltage() > initial_cap);
            assert!(v.gate_drive > threshold);
            for _ in 0..(rate * 0.027).round() as usize {
                v.tick(0.0);
            }
            assert_eq!(v.collector_current, 0.0);
            assert!(v.gate_drive < threshold);
        }
    }
}

/// C72/Q36 gate supply, C41/Q32 reset, and C42/Q31 control current source.
pub struct Veg {
    circuit: Circuit,
    capacitor: f64,
    base: f64,
    gate: bool,
    gate_drive: f64,
    collector_current: f64,
}
impl Veg {
    lifecycle!(
        capacitor = |s: &Circuit| s.c.supply,
        base = |_: &Circuit| 0.0,
        gate = |_: &Circuit| true,
        gate_drive = |s: &Circuit| s.c.q36_vbe,
        collector_current = |_: &Circuit| 0.0
    );
    /// Set Q37's digital gate drive before `tick`; C72/Q36 retain release charge.
    /// Default/reset is high so the contracted pulse-only interface exposes decay.
    pub fn set_gate(&mut self, gate: bool) {
        self.gate = gate;
    }
    /// Discharge C42 on a positive TRIG pulse; return Q31 collector amperes.
    /// C42 otherwise recharges through R123, irrespective of the gate state.
    /// VERIFY: Q36 uses a fixed-beta saturation reduction and the preceding
    /// sample's Q31 emitter load; base-collector charge/recombination is omitted.
    pub fn tick(&mut self, pulse: f64) -> f64 {
        let pulse = self
            .circuit
            .input(pulse)
            .max(0.0)
            .min(self.circuit.c.supply);
        let c = self.circuit.c;
        let source = (pulse - c.diode_drop).max(0.0);
        let base_r = c.r130 * (c.beta + 1.0);
        if source > self.base {
            let r = parallel(c.r134, base_r);
            let target = (source / c.r134 + c.vbe / base_r) * r;
            self.base = self
                .circuit
                .rc(0, self.base, target, self.circuit.dt, r * c.c41);
        } else if self.base > c.vbe {
            self.base = self
                .circuit
                .rc(1, self.base, c.vbe, self.circuit.dt, base_r * c.c41);
        }
        let available = c.beta * ((self.base - c.vbe) / base_r).max(0.0);
        let rdis = c.r130 + c.r132;
        let target = c.saturation.max(self.capacitor - available * rdis);
        if available > 1e-12 && self.capacitor > c.saturation {
            let r = parallel(c.r123, rdis);
            let v = (target / rdis + c.supply / c.r123) * r;
            self.capacitor = self
                .circuit
                .rc(2, self.capacitor, v, self.circuit.dt, r * c.c42);
        } else {
            self.capacitor =
                self.circuit
                    .rc(3, self.capacitor, c.supply, self.circuit.dt, c.r123 * c.c42);
        }
        // A PNP delivers base current into C42 in addition to R123's current.
        // Use the previous circuit sample as a bounded explicit current source;
        // this preserves charge without adding another nonlinear solve.
        self.capacitor = (self.capacitor
            + self.collector_current * self.circuit.dt / (c.q31_beta * c.c42))
            .min(c.supply);
        // C72 stores the voltage across Q36's base-emitter junction. Q37
        // sinks through R143 while on; R142 and Q36 base current remove
        // charge after it turns off. Saturation clamps forward base drive.
        // VERIFY: constant beta and a Vbe saturation clamp reduce Q36's
        // junction/storage dynamics; C72 and both resistors are printed.
        let capacity = if self.gate_drive == c.q36_vbe {
            c.q36_reference_current
        } else {
            c.q36_reference_current * libm::exp((self.gate_drive - c.q36_vbe) / c.thermal_voltage)
        };
        // Q36's active capacity can exceed the load while saturated. GATE_HI
        // then remains at the rail until C72 loses that forward-drive margin.
        // Q31 also draws its emitter current from this node. Use its previous
        // sample as a bounded explicit load, as for the C42 base-current term.
        let emitter_load = self.collector_current * (1.0 + 1.0 / c.q31_beta);
        let gate_voltage =
            (c.r145 * (capacity - emitter_load)).clamp(0.0, (c.supply - c.saturation).max(0.0));
        // VERIFY: forward-junction base current is capacity/beta. Additional
        // base-collector recombination/forced-beta storage in saturation needs
        // device validation; capacity is not actual collector load current.
        let base_current = capacity / c.beta;
        let discharge = self.gate_drive / c.r142 + base_current;
        let charge = if self.gate {
            (c.supply - c.saturation - self.gate_drive) / c.r143
        } else {
            0.0
        };
        self.gate_drive = flush(
            (self.gate_drive + self.circuit.dt * (charge - discharge) / c.c72)
                .clamp(0.0, c.q36_vbe),
        );
        self.collector_current = if gate_voltage > c.diode_drop {
            // Emitter degeneration plus the PNP junction equation. A fixed Vbe
            // truncates the physical low-current tail at an arbitrary voltage.
            // VERIFY: ideality one and Vbe at 47 µA require device validation.
            let scale = self.circuit.fixed.q31_scale;
            let x = (gate_voltage - c.diode_drop - c.vbe - self.capacitor) / c.thermal_voltage
                + self.circuit.fixed.q31_log_reference;
            // Solve y + ln(y) = x (positive Lambert-W branch). At most four
            // Newton steps; asymptotic seeds avoid overflow and negative y.
            let y = if (-80.0..-16.0).contains(&x) {
                // W(z) = z - z^2 + 3z^3/2 + O(z^4). Here z <= exp(-16)
                // bounds the remainder by 4.3e-28, below normal f64 roundoff.
                // Retain the same exp(-80) floor and Q31 capacitor feedback.
                let z = libm::exp(x.max(-80.0));
                z * (1.0 + z * (-1.0 + 1.5 * z))
            } else if x >= 32.0 {
                q31_forward_root(x)
            } else {
                let mut y = if x > 1.0 {
                    x - libm::log(x)
                } else {
                    libm::exp(x.max(-80.0))
                };
                let iterations = if x.abs() > 16.0 { 2 } else { 4 };
                for _ in 0..iterations {
                    y -= (y + libm::log(y) - x) * y / (y + 1.0);
                    y = y.max(1e-36);
                }
                y
            };
            flush(scale * y)
        } else {
            0.0
        };
        self.collector_current
    }
    /// C42 voltage at the Q31 base, rising toward +12 V during decay.
    pub fn capacitor_voltage(&self) -> f64 {
        self.capacitor
    }
}

/// AC node voltages supplied by the VCF; no dependency on its crate is needed.
#[derive(Clone, Copy, Debug, Default)]
pub struct VcfSignals {
    /// C14 output, resonance gang A terminal 3.
    pub output: f64,
    /// Resonance gang A wiper, also feeding C22 (VCA).
    pub resonance_tap: f64,
}

// Seventh Lambert continued-fraction convergent of tanh. For |z| <= 1/2,
// the seventh/eighth convergents bracket the exact transfer within
// |z|^15 / (135135 * 2027025) < 1.12e-16, before f64 rounding.
// Keep the cheap settled-tail cubic and the library's large-signal law.
fn ota_pair(z: f64) -> f64 {
    if z.abs() < 1e-4 {
        z - z * z * z / 3.0
    } else if z.abs() <= 0.5 {
        let square = z * z;
        z * (135_135.0 + square * (17_325.0 + square * (378.0 + square)))
            / (135_135.0 + square * (62_370.0 + square * (3_150.0 + 28.0 * square)))
    } else {
        libm::tanh(z)
    }
}

/// BA662A OTA, summed coupled inputs, accent-current network and C38 output.
pub struct Vca {
    circuit: Circuit,
    input_caps: [f64; 2],
    accent_cap: f64,
    // C38 voltage relative to the buffer's quiescent reference. Using this
    // origin keeps the silent network an exact floating-point fixed point.
    output_cap: f64,
    output_load_cap: f64,
    current: f64,
}
impl Vca {
    lifecycle!(
        input_caps = |_: &Circuit| [0.0; 2],
        accent_cap = |_: &Circuit| 0.0,
        output_cap = |_: &Circuit| 0.0,
        output_load_cap = |_: &Circuit| 0.0,
        current = |_: &Circuit| 0.0
    );
    /// Return the C38 negative-plate node in AC volts, including C59 loading.
    /// VERIFY: downstream loading uses full VOLUME, the capture setting.
    /// The voltage-only contract supplies no VR8 position to this stage;
    /// volume-dependent reciprocal loading still needs a shared-node interface.
    /// OTA law: `Iout = Iabc * mirror_ratio * tanh(Vdiff/(2*VT))`;
    /// small-signal gm is therefore `Iabc * mirror_ratio/(2*VT)`.
    pub fn tick(&mut self, vcf: VcfSignals, veg_current: f64, accent_wiper: f64) -> f64 {
        let u = [
            self.circuit.input(vcf.output).clamp(-24.0, 24.0),
            self.circuit.input(vcf.resonance_tap).clamp(-24.0, 24.0),
        ];
        let veg = self.circuit.input(veg_current).clamp(0.0, 1e-3);
        let accent = self
            .circuit
            .input(accent_wiper)
            .max(0.0)
            .min(self.circuit.c.supply);
        let c = self.circuit.c;
        let fixed = &self.circuit.fixed;
        let threshold = fixed.accent_threshold;
        let target_on =
            accent * fixed.accent_source_weight + threshold * fixed.accent_threshold_weight;
        let tau_on = fixed.accent_tau_on;
        let tau_off = fixed.accent_tau_off;
        let mut remaining = self.circuit.dt;
        let mut on = self.accent_cap > threshold;
        let target = if on { target_on } else { accent };
        let tau = if on { tau_on } else { tau_off };
        let next = self.circuit.rc(
            if on { 0 } else { 1 },
            self.accent_cap,
            target,
            remaining,
            tau,
        );
        if (self.accent_cap - threshold) * (next - threshold) < 0.0 {
            let time = (tau * libm::log((self.accent_cap - target) / (threshold - target)))
                .clamp(0.0, remaining);
            remaining -= time;
            self.accent_cap = threshold;
            on = !on;
            self.accent_cap = self.circuit.rc(
                if on { 0 } else { 1 },
                self.accent_cap,
                if on { target_on } else { accent },
                remaining,
                if on { tau_on } else { tau_off },
            );
        } else {
            self.accent_cap = next;
        }
        self.current =
            veg + ((self.accent_cap - threshold) * self.circuit.fixed.accent_inverse_r).max(0.0);
        // Trapezoidal capacitor companions solve the shared R124 input node,
        // including loading between the R121 and R122 paths.
        let g = self.circuit.input_g;
        let vdiff = (g[0] * (u[0] - self.input_caps[0]) + g[1] * (u[1] - self.input_caps[1]))
            * self.circuit.input_denominator;
        let i0 = g[0] * (u[0] - self.input_caps[0] - vdiff);
        let i1 = g[1] * (u[1] - self.input_caps[1] - vdiff);
        self.input_caps[0] = flush(self.input_caps[0] + self.circuit.input_step[0] * i0);
        self.input_caps[1] = flush(self.input_caps[1] + self.circuit.input_step[1] * i1);
        let z = vdiff * self.circuit.fixed.inverse_two_vt;
        let pair = ota_pair(z);
        let swing = self.circuit.fixed.ota_scale * self.current * pair;
        let buffer = (c.reference + swing)
            .max(c.saturation)
            .min(c.supply - c.saturation);
        let buffer = buffer - c.reference;
        let g = self.circuit.output_g;
        let inverse = self.circuit.output_inverse;
        let b = 1.0 / c.r155;
        let rhs38 = g[0] * self.output_cap + (1.0 / c.vr8 + b) * buffer;
        let rhs59 = g[1] * self.output_load_cap + b * buffer;
        let mid38 = inverse[0] * rhs38 - inverse[1] * rhs59;
        let mid59 = inverse[2] * rhs59 - inverse[1] * rhs38;
        self.output_cap = flush(2.0 * mid38 - self.output_cap);
        self.output_load_cap = flush(2.0 * mid59 - self.output_load_cap);
        flush(buffer - mid38)
    }
    /// Last total BA662A Iabc, Q31 current plus the conducting accent path.
    pub fn control_current(&self) -> f64 {
        self.current
    }
}

/// VR8 divider, Q34/Q33 inverting mixer, coupling capacitors and J6 line output.
pub struct OutputStage {
    circuit: Circuit,
    volume_cap: f64,
    mix_cap: f64,
    line_cap: f64,
    network: Option<(u64, VolumeNetwork)>,
}
#[derive(Clone, Copy)]
struct VolumeNetwork {
    fraction: f64,
    gain: f64,
    tau: f64,
}
impl OutputStage {
    #[cold]
    #[inline(never)]
    fn update_network(&mut self, volume: f64) -> VolumeNetwork {
        let k = self.circuit.audio(0, volume);
        let c = self.circuit.c;
        let r = c.r155 + c.vr8 * k * (1.0 - k);
        let n = VolumeNetwork {
            fraction: k,
            gain: -c.r153 / r,
            tau: r * c.c59,
        };
        self.network = Some((volume.to_bits(), n));
        n
    }
    lifecycle!(
        volume_cap = |_: &Circuit| 0.0,
        mix_cap = |_: &Circuit| 0.0,
        line_cap = |_: &Circuit| 0.0,
        network = |_: &Circuit| None
    );
    /// Return J6 volts; MIX IN joins after VOLUME with the independent −1 gain.
    /// Includes VOLUME wiper loading by R155, C59/C58 and C56/R161/R165.
    pub fn tick(&mut self, vca: f64, volume: f64, mix_in: f64) -> f64 {
        let vca = self.circuit.input(vca).clamp(-24.0, 24.0);
        let volume = self.circuit.knob(volume);
        let mix = self.circuit.input(mix_in).clamp(-24.0, 24.0);
        let c = self.circuit.c;
        let n = if let Some((key, n)) = self.network
            && key == volume.to_bits()
        {
            n
        } else {
            self.update_network(volume)
        };
        let a = self
            .circuit
            .high_pass(0, &mut self.volume_cap, vca * n.fraction, n.tau);
        let b = self
            .circuit
            .high_pass(1, &mut self.mix_cap, mix, c.r156 * c.c58);
        let sum = (n.gain * a + self.circuit.fixed.mix_gain * b)
            .max(-c.reference)
            .min(c.supply - c.reference);
        self.circuit
            .high_pass(2, &mut self.line_cap, sum, self.circuit.fixed.line_tau)
            * self.circuit.fixed.line_gain
    }
}
