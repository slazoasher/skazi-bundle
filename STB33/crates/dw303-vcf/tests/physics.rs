use dw303_vcf::{Components, Quality, StageError, Vcf, VcfOut};
use std::f64::consts::TAU;

#[derive(Clone, Copy)]
struct Complex(f64, f64);
impl Complex {
    fn add(self, b: Self) -> Self {
        Self(self.0 + b.0, self.1 + b.1)
    }
    fn sub(self, b: Self) -> Self {
        Self(self.0 - b.0, self.1 - b.1)
    }
    fn mul(self, b: Self) -> Self {
        Self(self.0 * b.0 - self.1 * b.1, self.0 * b.1 + self.1 * b.0)
    }
    fn div(self, b: Self) -> Self {
        let d = b.0 * b.0 + b.1 * b.1;
        Self(
            (self.0 * b.0 + self.1 * b.1) / d,
            (self.1 * b.0 - self.0 * b.1) / d,
        )
    }
    fn scale(self, k: f64) -> Self {
        Self(self.0 * k, self.1 * k)
    }
    fn abs(self) -> f64 {
        self.0.hypot(self.1)
    }
}
fn hp(w: f64, t: f64) -> Complex {
    Complex(0.0, w * t).div(Complex(1.0, w * t))
}
// Independent AC analysis: eliminate coupling branches, then ladder determinant.
fn analytic(c: Components, current: f64, f: f64, p: f64) -> f64 {
    let w = TAU * f;
    let g = current * c.input_beta / (c.input_beta + 1.0) / (4.0 * c.thermal_voltage);
    let tail = (c.bias_supply / 3.0 - c.vbe) / c.amplifier_resistance[3];
    let alpha = c.output_beta / (c.output_beta + 1.0);
    let conductance = c
        .output_bias_resistance
        .map(|r| 1.0 / r + 0.5 * tail * alpha / (c.output_beta * c.thermal_voltage));
    let gain = tail * alpha * c.amplifier_resistance[4] / (4.0 * c.thermal_voltage)
        * c.output_gain_correction;
    let h25 = hp(
        w,
        c.output_capacitance[0] / conductance[0] * c.output_coupling_scale,
    );
    let h27 = hp(
        w,
        c.output_capacitance[1] / conductance[1] * c.output_coupling_scale,
    );
    let amp = h25.add(h27).scale(0.5 * gain);
    let load = h25
        .scale(0.25 * conductance[0])
        .add(h27.scale(0.25 * conductance[1]));
    let mut prev = Complex(1.0, 0.0);
    let mut det = Complex(g, w * c.ladder_capacitance[0]);
    for i in 1..4 {
        let mut diag = Complex(2.0 * g, w * c.ladder_capacitance[i]);
        if i == 3 {
            diag = diag.add(load);
        }
        let next = diag.mul(det).sub(prev.scale(g * g));
        prev = det;
        det = next;
    }
    let core = Complex(g.powi(4), 0.0).div(det);
    let source_left = Complex(1.0, 0.0)
        .sub(
            hp(w, (c.input_resistance[0] + c.input_resistance[1]) * c.c17)
                .scale(c.input_resistance[1] / (c.input_resistance[0] + c.input_resistance[1])),
        )
        .scale(c.input_resistance[1]);
    // Independent frequency-domain Q18 T-equivalent. R94 and C23's load
    // draw emitter current; its base current loads C15 and the VR4 wiper.
    let re = c.thermal_voltage
        * (c.follower_resistance[0]
            + (c.follower_resistance[1] + c.feedback_resistance[1]) / (c.follower_beta + 1.0))
        / (c.bias_supply - c.follower_vbe);
    let emit = c.follower_resistance[0];
    let bias = c.feedback_resistance[1];
    let beta = c.follower_beta + 1.0;
    let sr = c.feedback_resistance[0] + c.resonance_resistance * p * (1.0 - p);
    let series = Complex(sr, -1.0 / (w * c.coupling_capacitance[1]));
    let sum = c.feedback_resistance[2] + c.feedback_resistance[3];
    let right = c.feedback_resistance[3];
    let z23 = Complex(sum, -1.0 / (w * c.coupling_capacitance[2]));
    let a = Complex(1.0, 0.0)
        .add(series.scale(1.0 / bias))
        .scale(1.0 + re / emit)
        .add(series.scale(1.0 / (emit * beta)));
    let b = Complex(1.0, 0.0)
        .add(series.scale(1.0 / bias))
        .scale(re)
        .add(series.scale(1.0 / beta));
    let denominator = a.add(b.div(z23));
    let eo = Complex(p, 0.0).div(denominator);
    let ej = b.scale(right / c.input_beta).div(z23).div(denominator);
    let io = eo.div(z23);
    let ij = ej.sub(Complex(right / c.input_beta, 0.0)).div(z23);
    let e = (1.0 + re / emit) / bias + 1.0 / (emit * beta);
    let f = re / bias + 1.0 / beta;
    let no = eo.scale(e).add(io.scale(f));
    let nj = ej.scale(e).add(ij.scale(f));
    let sc14 = Complex(0.0, w * c.coupling_capacitance[0]);
    let output = amp.mul(core).scale(1.0 / g).sub(nj.scale(p).div(sc14)).div(
        Complex(1.0, 0.0).add(
            Complex(1.0 / c.resonance_resistance, 0.0)
                .add(no.scale(p))
                .div(sc14),
        ),
    );
    let source_right = Complex(right, 0.0).sub(Complex(right * right, 0.0).div(z23));
    let injection_denominator = Complex(1.0, 0.0)
        .add(source_left.add(source_right).scale(g / c.input_beta))
        .add(ej.scale(g * right).div(z23));
    let input = hp(w, (c.input_resistance[0] + c.input_resistance[1]) * c.c17)
        .scale(c.input_resistance[1] / (c.input_resistance[0] + c.input_resistance[1]));
    output
        .scale(g)
        .mul(input)
        .div(injection_denominator.add(output.mul(eo).scale(g * right).div(z23)))
        .abs()
}
fn response(rate: f64, current: f64, f: f64, p: f64, q: Quality) -> f64 {
    let mut v = Vcf::new(Components::NOMINAL);
    v.prepare(rate).unwrap();
    v.set_quality(q);
    // The loaded output/feedback network has slower coupled modes than its
    // individual RC corners; wait for those before measuring steady AC gain.
    let settle = (rate * 5.0) as usize;
    let count = (rate * (f * 0.2).ceil().max(2.0) / f).round() as usize;
    let mut sin = 0.0;
    let mut cos = 0.0;
    for n in 0..settle + count {
        let phase = TAU * f * n as f64 / rate;
        let y = v.tick(1e-4 * phase.sin(), current, p).output;
        if n >= settle {
            sin += y * phase.sin();
            cos += y * phase.cos();
        }
    }
    assert_eq!(v.diagnostics().non_convergence, 0);
    2.0 * sin.hypot(cos) / count as f64 / 1e-4
}
#[test]
fn fitted_passives_and_loaded_output_corner() {
    let c = Components::NOMINAL;
    for (fitted, printed) in c
        .ladder_capacitance
        .into_iter()
        .zip([18e-9, 33e-9, 33e-9, 33e-9])
    {
        assert!((fitted / printed - 1.0).abs() <= 0.100_001);
    }
    for fitted in c.output_capacitance {
        assert!((fitted / 0.1e-6 - 1.0).abs() < 0.20);
    }
    let fc = |r: f64, c: f64| 1.0 / (TAU * r * c);
    assert!((fc(222_200.0, c.c17) - 0.7163).abs() < 0.001);
    assert!((fc(12_200.0, c.coupling_capacitance[2]) - 13.0455).abs() < 0.001);
    let tail = (c.bias_supply / 3.0 - c.vbe) / c.amplifier_resistance[3];
    let collector = 0.5 * tail * c.output_beta / (c.output_beta + 1.0);
    let loaded_g =
        1.0 / c.output_bias_resistance[0] + collector / (c.output_beta * c.thermal_voltage);
    assert!((loaded_g / (TAU * c.output_capacitance[0]) - 77.11).abs() < 0.25);
    assert!((Vcf::new(c).output_gain() - 101.85).abs() < 0.25);
}
#[test]
fn response_matches_netlist_and_rates() {
    let c = Components::NOMINAL;
    // Include envelope/accent currents beyond the settled capture sweep.
    for current in [1e-6, 10e-6, 30e-6, 100e-6] {
        for p in [0.0, 0.5, 1.0] {
            for f in [10.0, 100.0, 500.0, 1500.0] {
                let expected = analytic(c, current, f, p);
                let measured = response(192_000.0, current, f, p, Quality::Full);
                let db = 20.0 * (measured / expected).log10();
                assert!(db.abs() < 0.15, "I={current} p={p} f={f} error={db}");
            }
        }
    }
    for f in [100.0, 500.0, 1500.0] {
        let full = response(192_000.0, 10e-6, f, 0.5, Quality::Full);
        for (rate, q) in [
            (48_000.0, Quality::Full),
            (96_000.0, Quality::Full),
            (192_000.0, Quality::Economy),
        ] {
            let y = response(rate, 10e-6, f, 0.5, q);
            assert!((20.0 * (y / full).log10()).abs() < 0.15);
        }
    }
}
#[test]
fn coupling_blocks_dc_and_wiper_is_loaded() {
    let mut v = Vcf::new(Components::NOMINAL);
    for _ in 0..1_536_000 {
        v.tick(8.0, 10e-6, 0.5);
    }
    let y = v.tick(8.0, 10e-6, 0.5);
    assert!(
        y.output.abs() < 1e-8 && y.resonance_tap.abs() < 1e-8,
        "{y:?}"
    );
    v.reset();
    let mut loaded = false;
    for n in 0..10000 {
        let y = v.tick(3.0 * (n as f64 * 0.02).sin(), 10e-6, 0.5);
        loaded |= (y.resonance_tap - 0.5 * y.output).abs() > 1e-6;
    }
    assert!(loaded);
    v.reset();
    for n in 0..10000 {
        let y = v.tick(3.0 * (n as f64 * 0.02).sin(), 10e-6, 1.0);
        assert_eq!(y.output, y.resonance_tap);
    }
    assert_eq!(v.diagnostics().non_convergence, 0);
}
#[test]
fn extreme_grid_and_long_run() {
    for q in [Quality::Full, Quality::Economy] {
        for rate in [44_100.0, 48_000.0, 96_000.0, 192_000.0, 384_000.0] {
            let mut v = Vcf::new(Components::NOMINAL);
            v.prepare(rate).unwrap();
            v.set_quality(q);
            for current in [0.0, 1e-9, 1e-7, 1e-6, 10e-6, 100e-6, 1e-3] {
                for p in [0.0, 0.5, 1.0] {
                    for amp in [0.0, 0.5, 3.0, 12.0] {
                        for n in 0..256 {
                            let y = v.tick(amp * (TAU * n as f64 / 37.0).sin(), current, p);
                            assert!(y.output.is_finite() && y.resonance_tap.is_finite());
                            assert!(y.output.abs() < 2.0 * Components::NOMINAL.bias_supply);
                        }
                    }
                }
            }
            assert_eq!(
                v.diagnostics().non_convergence,
                0,
                "{q:?} {rate} {:?}",
                v.diagnostics()
            );
            assert!(v.diagnostics().max_iterations <= 8);
        }
        let mut v = Vcf::new(Components::NOMINAL);
        v.set_quality(q);
        for n in 0..1_000_000 {
            let y = v.tick(
                6.0 * (TAU * n as f64 / 131.0).sin(),
                if n % 8192 < 4096 { 50e-6 } else { 0.1e-6 },
                1.0,
            );
            assert!(y.output.is_finite() && y.resonance_tap.is_finite());
        }
        assert_eq!(v.diagnostics().non_convergence, 0);
    }
}
#[test]
fn lifecycle_and_sanitization() {
    let mut v = Vcf::new(Components::NOMINAL);
    for rate in [0.0, f64::NAN, f64::INFINITY, 44099.0, 384001.0] {
        assert_eq!(v.prepare(rate), Err(StageError::InvalidSampleRate));
    }
    assert_eq!(
        v.tick(f64::NAN, f64::INFINITY, f64::NEG_INFINITY),
        VcfOut::default()
    );
    assert_eq!(v.diagnostics().sanitized_inputs, 3);
    v.reset();
    for _ in 0..1000 {
        assert_eq!(v.tick(0.0, 10e-6, 1.0), VcfOut::default());
    }
    assert_eq!(v.diagnostics().max_iterations, 1);
}
#[test]
#[ignore]
fn stage_timing() {
    use std::{hint::black_box, time::Instant};
    for q in [Quality::Full, Quality::Economy] {
        let mut v = Vcf::new(Components::NOMINAL);
        v.set_quality(q);
        let t = Instant::now();
        for n in 0..384_000 {
            black_box(v.tick(black_box(libm::sin(n as f64 * 0.02) * 3.0), 20e-6, 0.7));
        }
        eprintln!("{q:?} {:?}, {:?}", t.elapsed(), v.diagnostics());
    }
}

#[test]
fn page7_ring_calibration() {
    let current = Vcf::new(Components::NOMINAL)
        .calibration_current(535.0)
        .expect("calibration pole");
    eprintln!("page7 current {current:e} A");
    for rate in [48_000.0, 96_000.0, 192_000.0] {
        let mut v = Vcf::new(Components::NOMINAL);
        v.prepare(rate).unwrap();
        let mut older = 0.0;
        let mut old = 0.0;
        let mut peaks = Vec::new();
        for n in 0..(rate * 0.02) as usize {
            let y = v.tick(if n == 0 { 0.01 } else { 0.0 }, current, 1.0).output;
            if n > 2 && old > older && old > y {
                peaks.push((n - 1, old));
            }
            older = old;
            old = y;
        }
        eprintln!("rate {rate} peaks {peaks:?}");
        assert!(peaks.len() >= 2);
        let period = (peaks[1].0 - peaks[0].0) as f64 / rate;
        assert!((period * 535.0 - 1.0).abs() < 0.02, "period={period}");
        assert_eq!(v.diagnostics().non_convergence, 0);
    }
    assert!(
        Vcf::new(Components::NOMINAL)
            .calibration_current(f64::NAN)
            .is_none()
    );
}

#[test]
fn fitted_upper_cutoff_currents_ring_at_the_measured_plateau() {
    // Normalise with the same centre pole as the voice. These current ratios
    // belong to its retained VR3 track; they are not internal TD-3 measurements.
    let centre = Vcf::new(Components::NOMINAL)
        .calibration_current(535.0)
        .unwrap();
    // VERIFY: these are output-only inferred currents, not internal TD-3
    // measurements. Session1 C1 c18/c20 ring peaks are 2338/2371 Hz on each
    // capture's own harmonic grid. Check their physical ringing across rates,
    // allowing harmonic spacing and sample quantisation rather than exact bins.
    for quality in [Quality::Full, Quality::Economy] {
        for rate in [48_000.0, 96_000.0, 192_000.0] {
            for (ratio, target_hz) in [
                (36.616e-6 / 8.20377e-6, 2338.0),
                (37.166e-6 / 8.20377e-6, 2371.0),
            ] {
                let current = centre * ratio;
                let mut vcf = Vcf::new(Components::NOMINAL);
                vcf.prepare(rate).unwrap();
                vcf.set_quality(quality);
                let mut older = 0.0;
                let mut old = 0.0;
                let mut peaks = Vec::new();
                for n in 0..(rate * 0.004) as usize {
                    let output = vcf
                        .tick(if n == 0 { 0.01 } else { 0.0 }, current, 1.0)
                        .output;
                    if n > 2 && old > older && old > output {
                        peaks.push((n - 1, old));
                    }
                    older = old;
                    old = output;
                }
                assert!(peaks.len() >= 3);
                let period = (peaks[2].0 - peaks[0].0) as f64 / (2.0 * rate);
                assert!(
                    (period * target_hz - 1.0).abs() < 0.03,
                    "{quality:?} rate={rate} I={current:e} period={period}"
                );
                assert!(peaks[2].1 < peaks[0].1);
                assert_eq!(vcf.diagnostics().non_convergence, 0);
            }
        }
    }
}

#[test]
fn fitted_centre_has_a_sharp_peak_in_both_solver_modes() {
    let c = Components::NOMINAL;
    let current = Vcf::new(c).calibration_current(535.0).unwrap();
    for quality in [Quality::Full, Quality::Economy] {
        let peak = response(192_000.0, current, 535.0, 1.0, quality);
        let below = response(192_000.0, current, 400.0, 1.0, quality);
        let above = response(192_000.0, current, 700.0, 1.0, quality);
        assert!(20.0 * (peak / below).log10() > 5.0);
        assert!(20.0 * (peak / above).log10() > 5.0);
        assert!((20.0 * (peak / analytic(c, current, 535.0, 1.0)).log10()).abs() < 0.15);
    }
}

#[test]
fn resonance_decays_across_the_complete_control_current_range() {
    // Cover ENV MOD/accent excursions as well as the settled ring sweep.
    // A charge-balanced, small differential excitation avoids mistaking
    // nonlinear limiting for stable poles. The 1 nV floor allows residual
    // integration error in otherwise silent coupling states.
    for quality in [Quality::Full, Quality::Economy] {
        for rate in [48_000.0, 192_000.0, 384_000.0] {
            for current in [
                1e-6, 4e-6, 8e-6, 20e-6, 40e-6, 60e-6, 100e-6, 140e-6, 180e-6, 220e-6, 300e-6, 1e-3,
            ] {
                for resonance in [0.0, 0.5, 0.9, 1.0] {
                    let mut v = Vcf::new(Components::NOMINAL);
                    v.prepare(rate).unwrap();
                    v.set_quality(quality);
                    let mut early = 0.0_f64;
                    let mut late = 0.0_f64;
                    for n in 0..(rate * 2.0) as usize {
                        let y = v
                            .tick(
                                if n == 0 {
                                    0.1
                                } else if n == 1 {
                                    -0.1
                                } else {
                                    0.0
                                },
                                current,
                                resonance,
                            )
                            .output;
                        if n < (rate * 0.03) as usize {
                            early = early.max(y.abs());
                        }
                        if n > (rate * 1.8) as usize {
                            late = late.max(y.abs());
                        }
                    }
                    assert!(
                        late < early * 0.01 + 1e-9,
                        "{quality:?} rate={rate} I={current:e} res={resonance} early={early:e} late={late:e}"
                    );
                    assert_eq!(v.diagnostics().non_convergence, 0);
                }
            }
        }
    }
}

#[test]
fn large_signal_excitation_does_not_leave_sustained_resonance() {
    // A bounded nonlinear oscillation could pass a small-impulse pole test.
    // Drive the input pair into limiting for 10 ms, then require at least
    // 40 dB decay after four seconds and continued decay between equal tail
    // windows. The C14/C15/C23 network has slow, coupled infrasonic poles.
    // Cover the settled and ENV/accent current ranges.
    for quality in [Quality::Full, Quality::Economy] {
        for rate in [48_000.0, 192_000.0] {
            for current in [4e-6, 8e-6, 38e-6, 100e-6, 530e-6, 1e-3] {
                let mut v = Vcf::new(Components::NOMINAL);
                v.prepare(rate).unwrap();
                v.set_quality(quality);
                let mut early = 0.0_f64;
                let mut late = 0.0_f64;
                let mut preceding = 0.0_f64;
                for n in 0..(rate * 4.0) as usize {
                    let input = if n < (rate * 0.01) as usize {
                        12.0 * (TAU * n as f64 * 330.0 / rate).sin()
                    } else {
                        0.0
                    };
                    let y = v.tick(input, current, 1.0).output;
                    if n < (rate * 0.03) as usize {
                        early = early.max(y.abs());
                    }
                    if n >= (rate * 3.4) as usize && n < (rate * 3.6) as usize {
                        preceding = preceding.max(y.abs());
                    }
                    if n >= (rate * 3.8) as usize {
                        late = late.max(y.abs());
                    }
                }
                assert!(
                    late < early * 0.01 + 1e-9 && late < preceding * 0.95 + 1e-9,
                    "{quality:?} rate={rate} I={current:e} early={early:e} preceding={preceding:e} late={late:e}"
                );
                assert_eq!(v.diagnostics().non_convergence, 0);
            }
        }
    }
}
