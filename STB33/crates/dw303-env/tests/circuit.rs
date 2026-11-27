//! Physical checks against the reconciled netlist rather than provisional traces.
use dw303_env::*;
use std::f64::consts::TAU;

fn near(actual: f64, expected: f64, tolerance: f64) {
    assert!(
        (actual - expected).abs() <= tolerance,
        "actual {actual:e}, expected {expected:e}, tolerance {tolerance:e}"
    );
}
fn rates() -> [f64; 3] {
    [48_000.0, 96_000.0, 192_000.0]
}

#[test]
fn q38_pulse_is_edge_triggered_and_derived_from_c54() {
    let c = Components::NOMINAL;
    let duration = c.r148 * c.c54 * ((c.supply - c.vbe) * c.r149 / (c.vbe * c.r148)).ln();
    near(duration, 0.0014, 0.0001);
    for rate in rates() {
        let mut p = TriggerPulse::new(c);
        p.prepare(rate).unwrap();
        let mut samples = 0;
        for _ in 0..(rate * 0.01) as usize {
            if p.tick(true) > 0.0 {
                samples += 1;
            }
        }
        near(f64::from(samples) / rate, duration, 1.0 / rate);
        assert_eq!(p.tick(true), 0.0);
        p.tick(false);
        near(p.tick(true), 11.8, 1e-12);
        p.reset();
        assert_eq!(p.tick(false), 0.0);
    }
}

#[test]
fn real_q38_pulses_recharge_partly_charged_meg_and_reset_veg() {
    let c = Components::NOMINAL;
    for rate in rates() {
        let mut pulse = TriggerPulse::new(c);
        let mut meg = Meg::new(c);
        let mut veg = Veg::new(c);
        pulse.prepare(rate).unwrap();
        meg.prepare(rate).unwrap();
        veg.prepare(rate).unwrap();
        let mut first_peak = 0.0_f64;
        for attack in 0..4 {
            if attack > 0 {
                // Both storage nodes retain charge in a 125 ms note interval.
                assert!((0.4..2.0).contains(&veg.capacitor_voltage()));
                assert!((3.0..10.0).contains(&meg.capacitor_voltage()));
            }
            let mut peak = 0.0_f64;
            veg.set_gate(true);
            for sample in 0..(rate * 0.01).round() as usize {
                let p = pulse.tick(sample == 0);
                meg.tick(p, false, 0.5);
                peak = peak.max(veg.tick(p));
            }
            if attack == 0 {
                first_peak = peak;
            } else {
                near(peak, first_peak, first_peak * 0.01);
            }
            near(meg.capacitor_voltage(), 10.8, 0.15);
            assert!(veg.capacitor_voltage() < 0.3);
            for sample in 0..(rate * 0.115).round() as usize {
                veg.set_gate(sample < (rate * 0.0525).round() as usize);
                meg.tick(pulse.tick(false), false, 0.5);
                veg.tick(0.0);
            }
        }
    }
}

#[test]
fn meg_charge_buffer_clamp_and_decay_follow_r152_r138_vr6_c62() {
    let c = Components::NOMINAL;
    let floor = c.diode_drop + 2.0 * c.meg_discharge_drop;
    for rate in rates() {
        for (decay, accent, r) in [
            (0.0, false, c.r138),
            (1.0, false, c.r138 + c.vr6),
            (1.0, true, c.r138),
            (0.5, false, c.r138 + c.vr6_taper[5] * c.vr6),
        ] {
            let mut m = Meg::new(c);
            m.prepare(rate).unwrap();
            near(m.tick(0.0, false, decay), floor - 2.0 * c.vbe, 1e-12);
            for _ in 0..(rate * 0.002) as usize {
                m.tick(11.8, accent, decay);
            }
            let peak = m.capacitor_voltage();
            near(peak, 11.2, 0.02);
            let n = (r * c.c62 * rate).round() as usize;
            for _ in 0..n {
                m.tick(0.0, accent, decay);
            }
            near(
                m.capacitor_voltage(),
                floor + (peak - floor) * (-(n as f64) / rate / (r * c.c62)).exp(),
                1e-9,
            );
        }
        let mut m = Meg::new(c);
        m.prepare(rate).unwrap();
        let value = m.tick(11.8, false, 0.0);
        let r = 1.0 / (1.0 / c.r152 + 1.0 / c.r138);
        let target = r * ((11.8 - c.diode_drop) / c.r152 + floor / c.r138);
        near(
            value + 2.0 * c.vbe,
            target + (floor - target) * (-1.0 / rate / (r * c.c62)).exp(),
            1e-12,
        );
    }
    // Fitted electrical time constants, within capacitor/pot tolerances.
    near((c.r138 + c.vr6) * c.c62, 0.82783, 0.001);
    near((c.r138 + c.vr6_taper[5] * c.vr6) * c.c62, 0.1908, 0.001);
    near(c.r138 * c.c62, 0.06783, 0.001);
}

#[test]
fn low_current_clamp_preserves_d37_peak_and_c62_time_constant() {
    let fitted = Components::NOMINAL;
    let mut shared_drop = fitted;
    shared_drop.meg_discharge_drop = shared_drop.diode_drop;
    for rate in rates() {
        let mut current_clamp = Meg::new(fitted);
        let mut old_clamp = Meg::new(shared_drop);
        current_clamp.prepare(rate).unwrap();
        old_clamp.prepare(rate).unwrap();
        for _ in 0..(rate * 0.002).round() as usize {
            current_clamp.tick(11.8, false, 0.0);
            old_clamp.tick(11.8, false, 0.0);
        }
        // D37/R152 still set the attack. Changing low-current discharge
        // junctions must not reduce the charged peak by a diode-sized step.
        near(current_clamp.capacitor_voltage(), 11.2, 0.02);
        near(
            current_clamp.capacitor_voltage(),
            old_clamp.capacitor_voltage(),
            0.001,
        );
        let rest = fitted.diode_drop + 2.0 * fitted.meg_discharge_drop - 2.0 * fitted.vbe;
        let start = current_clamp.capacitor_voltage() - 2.0 * fitted.vbe;
        for _ in 0..(rate * fitted.r138 * fitted.c62).round() as usize {
            current_clamp.tick(0.0, false, 0.0);
            old_clamp.tick(0.0, false, 0.0);
        }
        let at_tau = current_clamp.tick(0.0, false, 0.0);
        near((at_tau - rest) / (start - rest), (-1.0_f64).exp(), 0.001);
        for _ in 0..(rate * 10.0 * fitted.r138 * fitted.c62).round() as usize {
            current_clamp.tick(0.0, false, 0.0);
            old_clamp.tick(0.0, false, 0.0);
        }
        let tail = current_clamp.tick(0.0, false, 0.0);
        assert!((0.18..0.37).contains(&tail), "fitted MEG tail: {tail}");
        assert!(old_clamp.tick(0.0, false, 0.0) > tail + 0.14);
    }
}

#[test]
fn accent_wiper_obeys_loaded_linear_pot_and_switches_off() {
    let c = Components::NOMINAL;
    let mut a = Accent::new(c);
    let threshold = c.diode_drop + c.ota_control_voltage;
    // Independent three-arm nodal divider, with the conducting accent
    // branch terminating at D27 + the OTA pin-1 voltage rather than ground.
    for k in [0.2, 0.5, 0.7, 0.9] {
        let upper = c.vr7 * (1.0 - k);
        let lower = c.vr7 * k;
        let load = c.r119 + c.r120;
        let expected = (9.9 / upper + threshold / load) / (1.0 / upper + 1.0 / lower + 1.0 / load);
        let actual = a.tick(true, 9.9, k);
        near(actual, expected, 1e-12);
        assert!(actual < 9.9 * k);
    }
    near(a.tick(true, 9.9, 0.1), 0.99, 1e-12);
    near(a.tick(true, 9.9, 0.0), 0.0, 1e-12);
    near(a.tick(true, 9.9, 1.0), 9.9, 1e-12);
    assert_eq!(a.tick(false, 9.9, 1.0), 0.0);
}

#[test]
fn accent_sweep_uses_loaded_track_and_retains_charge() {
    let c = Components::NOMINAL;
    let reference = c.reference / 2.0 + c.vbe;
    let charge_r = c.r46 + c.vr4b;
    let load = c.r72 + c.r71;
    let rth = 1.0 / (1.0 / charge_r + 1.0 / load);
    let target = rth * ((10.0 - c.diode_drop) / charge_r + reference / load);
    for rate in rates() {
        let mut sweep = AccentSweep::new(c);
        sweep.prepare(rate).unwrap();
        near(sweep.tick(0.0, 1.0), reference, 1e-12);
        let n = (rth * c.c13 * rate).round() as usize;
        for _ in 0..n {
            sweep.tick(10.0, 1.0);
        }
        near(
            sweep.capacitor_voltage(),
            target + (reference - target) * (-(n as f64) / rate / (rth * c.c13)).exp(),
            1e-10,
        );
        let old = sweep.capacitor_voltage();
        let n = (load * c.c13 * rate).round() as usize;
        for _ in 0..n {
            sweep.tick(0.0, 1.0);
        }
        near(
            sweep.capacitor_voltage(),
            reference + (old - reference) * (-(n as f64) / rate / (load * c.c13)).exp(),
            1e-10,
        );
        let mut direct = AccentSweep::new(c);
        direct.prepare(rate).unwrap();
        let mut smooth = AccentSweep::new(c);
        smooth.prepare(rate).unwrap();
        assert!(direct.tick(10.0, 0.0) > smooth.tick(10.0, 1.0) + 1.0);
        let mut peaks = [0.0; 8];
        for peak in &mut peaks {
            for _ in 0..(rate * 0.02) as usize {
                *peak = smooth.tick(10.0, 1.0);
            }
            for _ in 0..(rate * 0.02) as usize {
                smooth.tick(0.0, 1.0);
            }
        }
        for pair in peaks.windows(2) {
            assert!(pair[1] > pair[0]);
        }
    }
}

#[test]
fn converter_has_vq9_reference_loaded_pots_and_netlist_sensitivities() {
    let c = Components::NOMINAL;
    let mut f = FilterControl::new(c);
    let r = f.reference_voltage();
    near(r, 3.3165, 1e-12);
    let neutral = f.ladder_current(0.5, 0.0, r, r);
    let low = c.r47 + c.vr3_taper[5] * c.vr3;
    let high = (1.0 - c.vr3_taper[5]) * c.vr3;
    let vw = r + (c.supply - r) * low / (low + high);
    let rw = low * high / (low + high);
    let expected = c.expo_is_ratio * (vw - r - c.vbe) / (c.r73 + c.tm3 + rw);
    near(neutral, expected, 1e-15);
    let base = f.ladder_current(0.5, 1.0, r, r);
    let env_octave = f.ladder_current(0.5, 1.0, r + 1.79, r);
    near(env_octave / base, 2.0, 0.05);
    let sweep_octave = f.ladder_current(0.5, 1.0, r, r + 0.81);
    near(sweep_octave / base, 2.0, 0.07);
    assert!(f.ladder_current(0.5, 1.0, 0.5, r) < f.ladder_current(0.5, 0.0, 0.5, r));
    assert!(f.ladder_current(1.0, 0.0, r, r) > f.ladder_current(0.0, 0.0, r, r));
    let compliance = (c.reference - c.vbe - r - c.saturation) / c.r69;
    near(f.ladder_current(1.0, 1.0, 12.0, 12.0), compliance, 1e-15);
    let mut zero_trim = c;
    zero_trim.tm3 = 0.0;
    assert!(FilterControl::new(zero_trim).ladder_current(0.5, 0.0, r, r) > neutral);
}

// Minimal complex arithmetic keeps the analytical AC reference independent of
// the library's time-domain solvers and adds no dependencies.
#[derive(Clone, Copy)]
struct Z(f64, f64);
impl Z {
    fn add(self, b: Self) -> Self {
        Self(self.0 + b.0, self.1 + b.1)
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
    fn norm(self) -> f64 {
        self.0.hypot(self.1)
    }
}
fn hp(w: f64, tau: f64) -> Z {
    Z(0.0, w * tau).div(Z(1.0, w * tau))
}

#[test]
fn fitted_cutoff_track_reproduces_identified_tail_currents() {
    let c = Components::NOMINAL;
    let mut f = FilterControl::new(c);
    let reference = f.reference_voltage();
    // Raw control currents for the identified cutoff track with fitted R61.
    // The voice separately renormalizes the expo ratio at its ring reference.
    // Rounded targets retain tolerances instead of pinning floating arithmetic.
    let targets = [
        3.60, 4.15, 5.17, 6.14, 6.86, 7.64, 8.17, 13.04, 23.73, 36.05, 36.83,
    ];
    for rate in rates() {
        f.prepare(rate).unwrap();
        for (i, target) in targets.into_iter().enumerate() {
            let current = f.ladder_current(i as f64 / 10.0, 0.0, 0.5, reference);
            near(current, target * 1e-6, 0.02e-6);
        }
    }
    let mut previous = 0.0;
    for i in 0..=100 {
        let current = f.ladder_current(i as f64 / 100.0, 0.0, 0.5, reference);
        assert!(current >= previous);
        previous = current;
    }
    let mut invalid = c;
    invalid.vr3_taper[3] = f64::NAN;
    near(
        FilterControl::new(invalid).ladder_current(0.5, 0.0, 0.5, reference),
        f.ladder_current(0.5, 0.0, 0.5, reference),
        1e-15,
    );
}

#[test]
fn r61_keeps_a_small_filter_envelope_at_env_mod_minimum() {
    let c = Components::NOMINAL;
    let rest = c.diode_drop + 2.0 * c.meg_discharge_drop - 2.0 * c.vbe;
    for rate in rates() {
        let mut f = FilterControl::new(c);
        f.prepare(rate).unwrap();
        let reference = f.reference_voltage();
        let low = f.ladder_current(0.4, 0.0, rest, reference);
        let peak = f.ladder_current(0.4, 0.0, 9.9, reference);
        // Output-fit R61 retains roughly 0.81 octaves of CCW excursion.
        // Its static current calibration cancels in this ratio.
        near(peak / low, 1.749, 0.01);
        let mut printed = c;
        printed.r61 = 10e3;
        let mut original = FilterControl::new(printed);
        let original_low = original.ladder_current(0.4, 0.0, rest, reference);
        let original_peak = original.ladder_current(0.4, 0.0, 9.9, reference);
        assert!(peak / low < original_peak / original_low);
    }
}

#[test]
fn veg_c42_recharge_and_q31_junction_tail_are_rate_independent() {
    let c = Components::NOMINAL;
    for rate in rates() {
        let mut v = Veg::new(c);
        v.prepare(rate).unwrap();
        assert_eq!(v.tick(0.0), 0.0);
        for _ in 0..(rate * 0.005) as usize {
            v.tick(11.8);
        }
        assert!(v.capacitor_voltage() < 0.3);
        let peak_current = v.tick(0.0);
        near(peak_current, 47e-6, 2e-6);
        for _ in 0..(rate * 0.05) as usize {
            v.tick(0.0);
        }
        let old = v.capacitor_voltage();
        let n = (rate * c.r123 * c.c42).round() as usize;
        for _ in 0..n {
            v.tick(0.0);
        }
        // Independent linear-junction reduction: Q31's PNP base current
        // adds (Vgate - VD35 - Vbe - V_C42)/(beta * R_E) to R123's current.
        // The logarithmic junction correction stays below 2 mV here.
        let conductance = 1.0 / c.r123 + 1.0 / (c.q31_beta * (c.r131 + c.r133));
        let headroom = c.supply - c.saturation - c.diode_drop - c.vbe;
        let target =
            (c.supply / c.r123 + headroom / (c.q31_beta * (c.r131 + c.r133))) / conductance;
        let expected = target + (old - target) * (-(n as f64) / rate * conductance / c.c42).exp();
        near(v.capacitor_voltage(), expected, 0.002);
        let unloaded = c.supply + (old - c.supply) * (-(n as f64) / rate / (c.r123 * c.c42)).exp();
        assert!(v.capacitor_voltage() > unloaded + 0.08);
        v.set_gate(false);
        assert!(v.tick(0.0) > 0.0);
        for _ in 0..(rate * 0.03) as usize {
            v.tick(0.0);
        }
        assert_eq!(v.tick(0.0), 0.0);
        v.set_gate(true);
        for _ in 0..(rate * 0.01) as usize {
            v.tick(0.0);
        }
        assert!(v.tick(0.0) > 0.0);
        for _ in 0..(rate * 2.0) as usize {
            v.tick(0.0);
        }
        assert!(v.tick(0.0) < 10e-9);
        v.reset();
        near(v.capacitor_voltage(), 12.0, 1e-12);
    }
}

#[test]
fn q31_current_obeys_junction_equation_and_has_a_smooth_low_current_tail() {
    let c = Components::NOMINAL;
    let mut tails = [0.0; 3];
    for (index, rate) in rates().into_iter().enumerate() {
        let mut v = Veg::new(c);
        v.prepare(rate).unwrap();
        for _ in 0..(rate * 0.0014).round() as usize {
            v.tick(c.supply - c.saturation);
        }
        let mut previous = 1e-3;
        for sample in 0..(rate * 3.6).round() as usize {
            let current = v.tick(0.0);
            if sample > (rate * 0.05) as usize {
                assert!(current <= previous + 1e-15);
                let emitter_headroom =
                    c.supply - c.saturation - c.diode_drop - v.capacitor_voltage();
                let junction = c.vbe + c.thermal_voltage * (current / c.q31_reference_current).ln();
                near(
                    emitter_headroom,
                    (c.r131 + c.r133) * current + junction,
                    1e-8,
                );
            }
            previous = current;
            if sample == (rate * 3.2).round() as usize {
                tails[index] = current;
                // The former fixed-Vbe source cut off abruptly in this region.
                assert!((50e-9..2e-6).contains(&current));
            }
        }
        v.set_gate(false);
        for _ in 0..(rate * 0.03) as usize {
            v.tick(0.0);
        }
        assert_eq!(v.tick(0.0), 0.0);
    }
    for tail in tails {
        near(tail, tails[2], tails[2] * 0.01);
    }
}

#[test]
fn gate_source_without_supply_headroom_stays_closed() {
    let c = Components {
        supply: 0.1,
        saturation: 0.2,
        ..Components::NOMINAL
    };
    let mut v = Veg::new(c);
    for gate in [false, true] {
        v.set_gate(gate);
        for _ in 0..32 {
            assert_eq!(v.tick(0.0), 0.0);
        }
    }
}

#[test]
fn q36_gate_storage_has_a_short_monotone_release_at_all_rates() {
    let mut crossings = [0.0; 3];
    for (index, rate) in rates().into_iter().enumerate() {
        let mut v = Veg::new(Components::NOMINAL);
        v.prepare(rate).unwrap();
        for _ in 0..(rate * 0.005).round() as usize {
            v.tick(11.8);
        }
        let peak = v.tick(0.0);
        v.set_gate(false);
        let mut previous = peak;
        let mut crossed = false;
        for sample in 0..(rate * 0.03).round() as usize {
            let current = v.tick(0.0);
            assert!(current <= previous + 1e-12);
            if !crossed && current < peak * 0.2 {
                crossings[index] = sample as f64 / rate;
                crossed = true;
            }
            previous = current;
        }
        assert!(crossed);
        assert_eq!(previous, 0.0);
        // The load-aware fit retains about 10 ms electrical 20%-current
        // release. Audible crossings also include output phase and coupling.
        assert!(
            (0.009..0.011).contains(&crossings[index]),
            "fitted release at {rate} Hz: {} s",
            crossings[index]
        );
        // Reopening an uncharged C72 takes a finite interval, without a trigger.
        v.set_gate(true);
        assert_eq!(v.tick(0.0), 0.0);
        for _ in 0..(rate * 0.01).round() as usize {
            v.tick(0.0);
        }
        assert!(v.tick(0.0) > peak * 0.9);
    }
    for crossing in crossings {
        near(crossing, crossings[2], 2.0 / 48_000.0);
    }
}

fn vca_amplitude(rate: f64, hz: f64, input: VcfSignals, control: f64) -> f64 {
    let mut v = Vca::new(Components::NOMINAL);
    v.prepare(rate).unwrap();
    let mut sin = 0.0;
    let mut cos = 0.0;
    let mut n = 0;
    for i in 0..rate as usize {
        let phase = TAU * hz * i as f64 / rate;
        let wave = phase.sin();
        let y = v.tick(
            VcfSignals {
                output: input.output * wave,
                resonance_tap: input.resonance_tap * wave,
            },
            control,
            0.0,
        );
        if i >= (rate * 0.5) as usize {
            sin += y * wave;
            cos += y * phase.cos();
            n += 1;
        }
    }
    2.0 * sin.hypot(cos) / f64::from(n)
}

#[test]
fn ota_gm_input_corners_and_resonance_tap_follow_netlist() {
    let c = Components::NOMINAL;
    let load = c.r128 * (c.r127 + c.r129) / (c.r128 + c.r127 + c.r129);
    near(load, 52_525.95155709342, 1e-8);
    for rate in rates() {
        for hz in [40.0, 72.0, 159.0, 1000.0] {
            let w = TAU * hz;
            let y1 = Z(1.0, 0.0).div(Z(c.r121, -1.0 / (w * c.c21)));
            let y2 = Z(1.0, 0.0).div(Z(c.r122, -1.0 / (w * c.c22_vca)));
            let denominator = Z(1.0 / c.r124, 0.0).add(y1).add(y2);
            let gm = 10e-6 / (2.0 * c.thermal_voltage);
            // C59/R155 is a real load on the C38/VR8 junction at full volume.
            // The two capacitors are coupled rather than cascaded stiff stages.
            let y38 = Z(0.0, w * c.c38);
            let y59 = Z(1.0, 0.0).div(Z(c.r155, -1.0 / (w * c.c59)));
            let out = y38.div(y38.add(Z(1.0 / c.vr8, 0.0)).add(y59));
            for (input, path) in [
                (
                    VcfSignals {
                        output: 1e-3,
                        resonance_tap: 0.0,
                    },
                    y1,
                ),
                (
                    VcfSignals {
                        output: 0.0,
                        resonance_tap: 1e-3,
                    },
                    y2,
                ),
            ] {
                let expected = path.div(denominator).mul(out).norm() * gm * load * 1e-3;
                let amplitude = vca_amplitude(rate, hz, input, 10e-6);
                near(amplitude, expected, expected * 0.004);
            }
        }
        let input = VcfSignals {
            output: 1e-3,
            resonance_tap: 0.0,
        };
        let a = vca_amplitude(rate, 1000.0, input, 10e-6);
        let b = vca_amplitude(rate, 1000.0, input, 20e-6);
        near(b / a, 2.0, 1e-8);
        assert_eq!(vca_amplitude(rate, 1000.0, input, 0.0), 0.0);
    }
    let low = vca_amplitude(
        192000.0,
        1000.0,
        VcfSignals {
            output: 1e-3,
            resonance_tap: 0.0,
        },
        1e-6,
    );
    let high = vca_amplitude(
        192000.0,
        1000.0,
        VcfSignals {
            output: 10.0,
            resonance_tap: 0.0,
        },
        1e-6,
    );
    assert!(high < low * 10_000.0 * 0.9); // input-differential tanh compression
}

#[test]
fn c38_c59_reciprocal_loading_matches_the_combined_output_network() {
    let c = Components::NOMINAL;
    for rate in rates() {
        for hz in [8.0, 32.7, 65.4, 1000.0] {
            let mut vca = Vca::new(c);
            let mut output = OutputStage::new(c);
            vca.prepare(rate).unwrap();
            output.prepare(rate).unwrap();
            let w = TAU * hz;
            let y121 = Z(1.0, 0.0).div(Z(c.r121, -1.0 / (w * c.c21)));
            let y122 = Z(1.0, 0.0).div(Z(c.r122, -1.0 / (w * c.c22_vca)));
            let input = y121.div(Z(1.0 / c.r124, 0.0).add(y121).add(y122));
            let y38 = Z(0.0, w * c.c38);
            let y59 = Z(1.0, 0.0).div(Z(c.r155, -1.0 / (w * c.c59)));
            let node = y38.div(y38.add(Z(1.0 / c.vr8, 0.0)).add(y59));
            let mixer = hp(w, c.r155 * c.c59);
            let line = hp(w, (c.r161 + c.r165) * c.c56);
            let gain =
                c.ota_mirror_ratio * 10e-6 / (2.0 * c.thermal_voltage) * c.r128 * (c.r127 + c.r129)
                    / (c.r128 + c.r127 + c.r129)
                    * c.r153
                    / c.r155
                    * c.r165
                    / (c.r161 + c.r165);
            let expected = input.mul(node).mul(mixer).mul(line).norm() * gain * 1e-3;
            let mut sin = 0.0;
            let mut cos = 0.0;
            let mut count = 0;
            for sample in 0..(rate * 2.0).round() as usize {
                let phase = w * sample as f64 / rate;
                let voltage = vca.tick(
                    VcfSignals {
                        output: 1e-3 * phase.sin(),
                        resonance_tap: 0.0,
                    },
                    10e-6,
                    0.0,
                );
                let value = output.tick(voltage, 1.0, 0.0);
                if sample >= rate as usize {
                    sin += value * phase.sin();
                    cos += value * phase.cos();
                    count += 1;
                }
            }
            near(
                2.0 * sin.hypot(cos) / f64::from(count),
                expected,
                expected * 0.004,
            );
        }
    }
}

#[test]
fn vca_accent_current_obeys_loaded_c36_network_and_blocks_below_d27() {
    let c = Components::NOMINAL;
    for rate in rates() {
        let mut v = Vca::new(c);
        v.prepare(rate).unwrap();
        let n = (rate * 0.001).round() as usize;
        for _ in 0..n {
            v.tick(VcfSignals::default(), 0.0, 9.9);
        }
        let threshold = c.diode_drop + c.ota_control_voltage;
        let crossing = -c.r119 * c.c36 * (1.0 - threshold / 9.9).ln();
        let tau = (c.r119 * c.r120 / (c.r119 + c.r120)) * c.c36;
        let steady = (9.9 - threshold) / (c.r119 + c.r120);
        near(
            v.control_current(),
            steady * (1.0 - (-(n as f64 / rate - crossing) / tau).exp()),
            1e-12,
        );
        v.reset();
        for _ in 0..(rate * 0.03) as usize {
            v.tick(VcfSignals::default(), 10e-6, 9.9);
        }
        let expected = 10e-6 + (9.9 - c.diode_drop - c.ota_control_voltage) / (c.r119 + c.r120);
        near(v.control_current(), expected, 1e-12);
        for _ in 0..(rate * 0.03) as usize {
            v.tick(VcfSignals::default(), 0.0, 0.5);
        }
        assert_eq!(v.control_current(), 0.0);
    }
}

fn output_amplitude(rate: f64, volume: f64, vca: f64, mix: f64) -> f64 {
    let mut o = OutputStage::new(Components::NOMINAL);
    o.prepare(rate).unwrap();
    let mut sin = 0.0;
    let mut cos = 0.0;
    for i in 0..rate as usize {
        let p = TAU * 1000.0 * i as f64 / rate;
        let y = o.tick(vca * p.sin(), volume, mix * p.sin());
        if i >= rate as usize / 2 {
            sin += y * p.sin();
            cos += y * p.cos();
        }
    }
    4.0 * sin.hypot(cos) / rate
}

#[test]
fn output_has_inverting_mixer_gains_volume_loading_and_independent_mix_input() {
    let c = Components::NOMINAL;
    for rate in rates() {
        near(
            output_amplitude(rate, 1.0, 0.1, 0.0),
            0.1 * c.r153 / c.r155 * c.r165 / (c.r165 + c.r161),
            3e-5,
        );
        near(
            output_amplitude(rate, 0.0, 0.0, 0.1),
            0.1 * c.r153 / c.r156 * c.r165 / (c.r165 + c.r161),
            2e-5,
        );
        assert_eq!(output_amplitude(rate, 0.0, 0.1, 0.0), 0.0);
        let expected =
            0.1 * 0.1 * c.r153 / (c.r155 + c.vr8 * 0.1 * 0.9) * c.r165 / (c.r165 + c.r161);
        near(output_amplitude(rate, 0.5, 0.1, 0.0), expected, 1e-5);
        let mut o = OutputStage::new(c);
        o.prepare(rate).unwrap();
        assert!(o.tick(0.1, 1.0, 0.0) < 0.0);
        let mut y = 0.0;
        for _ in 0..(rate * 6.0) as usize {
            y = o.tick(0.1, 1.0, 0.2);
        }
        assert!(y.abs() < 1e-12);
    }
    near(1.0 / (TAU * (c.r161 + c.r165) * c.c56), 1.9697, 0.0001);
    near(1.0 / (TAU * c.r155 * c.c59), 4.2328, 0.0001);
    near(1.0 / (TAU * c.r156 * c.c58), 1.5915, 0.0001);
}

#[test]
fn lifecycle_sanitisation_and_extreme_settings_stay_finite() {
    for rate in [44_100.0, 48_000.0, 96_000.0, 192_000.0, 384_000.0] {
        let c = Components::NOMINAL;
        let mut p = TriggerPulse::new(c);
        let mut m = Meg::new(c);
        let mut a = Accent::new(c);
        let mut s = AccentSweep::new(c);
        let mut f = FilterControl::new(c);
        let mut v = Veg::new(c);
        let mut ota = Vca::new(c);
        let mut o = OutputStage::new(c);
        macro_rules! lifecycle {($($stage:ident),*)=>{$(
            assert!($stage.prepare(f64::NAN).is_err());$stage.prepare(rate).unwrap();
        )*};}
        lifecycle!(p, m, a, s, f, v, ota, o);
        for i in 0..200_000 {
            let knob = if i % 97 == 0 {
                f64::NAN
            } else {
                f64::from(i % 2)
            };
            let pulse = p.tick(i % 997 < 500);
            let meg = m.tick(pulse, i % 3 == 0, knob);
            let accent = a.tick(i % 3 == 0, meg, knob);
            let sweep = s.tick(accent, knob);
            let current = f.ladder_current(knob, knob, meg, sweep);
            v.set_gate(i % 997 < 500);
            let veg = v.tick(pulse);
            let audio = if i % 127 == 0 { f64::INFINITY } else { 1e300 };
            let y = ota.tick(
                VcfSignals {
                    output: audio,
                    resonance_tap: -audio,
                },
                veg,
                accent,
            );
            let output = o.tick(y, knob, -audio);
            for x in [pulse, meg, accent, sweep, current, veg, y, output] {
                assert!(x.is_finite() && !x.is_subnormal());
            }
        }
        assert!(m.diagnostics().non_finite_inputs > 0);
        assert!(ota.diagnostics().non_finite_inputs > 0);
        m.reset();
        assert_eq!(m.diagnostics().non_finite_inputs, 0);
    }
    let mut c = Components::NOMINAL;
    c.c62 = f64::NAN;
    c.r123 = -1.0;
    c.thermal_voltage = 0.0;
    assert!(Meg::new(c).tick(12.0, true, 1.0).is_finite());
    assert!(Veg::new(c).tick(12.0).is_finite());
    assert!(
        Vca::new(c)
            .tick(VcfSignals::default(), 1e-6, 1.0)
            .is_finite()
    );
}
