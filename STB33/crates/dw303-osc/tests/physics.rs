use dw303_osc::{Components, PitchCv, SquareShaper, StageError, Vco, cv_for_note};
use std::f64::consts::TAU;

fn cents(ratio: f64) -> f64 {
    1200.0 * ratio.log2()
}

#[test]
fn dac_and_slide_are_interval_and_rate_independent() {
    for note in 0..=255 {
        assert_eq!(cv_for_note(note), f64::from(note.min(52) + 12) / 12.0);
    }
    let c = Components::NOMINAL;
    let tau = c.slide_resistance * c.slide_capacitance;
    // C35 session fit stays within an assumed ±10% capacitor tolerance.
    assert!((tau - 0.0232).abs() < 0.0001);
    assert!((c.slide_capacitance / 0.22e-6 - 1.0).abs() < 0.10);
    for rate in [44_100.0, 48_000.0, 96_000.0, 192_000.0, 384_000.0] {
        for (from, to) in [(0, 12), (0, 60), (60, 0), (24, 25)] {
            let mut cv = PitchCv::new(c);
            cv.prepare(rate).unwrap();
            cv.tick(from, false);
            let n = (tau * rate).round() as usize;
            let mut value = 0.0;
            for _ in 0..n {
                value = cv.tick(to, true);
            }
            let remaining = (value - cv_for_note(to)) / (cv_for_note(from) - cv_for_note(to));
            let expected = (-(n as f64) / (rate * tau)).exp();
            assert!((remaining - expected).abs() < 1e-11);
            assert_eq!(cv.tick(to, false), cv_for_note(to));
            cv.reset();
            assert!(cv.tick(0, true) > 0.0);
        }
    }
}

#[test]
fn schematic_component_changes_affect_expo_divider() {
    let nominal = Vco::new(Components::NOMINAL);
    let less_cv_gain = Vco::new(Components {
        cv_input_resistance: 30_000.0,
        ..Components::NOMINAL
    });
    assert!(less_cv_gain.frequency_hz(2.0, 0.5) / less_cv_gain.frequency_hz(1.0, 0.5) < 2.0);
    let less_tune_gain = Vco::new(Components {
        tuning_input_resistance: 200_000.0,
        ..Components::NOMINAL
    });
    assert!(
        cents(less_tune_gain.frequency_hz(1.0, 1.0) / less_tune_gain.frequency_hz(1.0, 0.5))
            < 550.0
    );
    assert!(
        (cents(nominal.frequency_hz(1.0, 1.0) / nominal.frequency_hz(1.0, 0.5)) - 891.852).abs()
            < 0.01
    );
    assert_eq!(cv_for_note(0), 1.0);
    assert_eq!(cv_for_note(21), 2.75);
    assert_eq!(cv_for_note(36), 4.0);
    assert_eq!(cv_for_note(255), 64.0 / 12.0);
}

#[test]
fn fitted_b_taper_vr2_is_monotonic_with_asymmetric_endpoints() {
    let vco = Vco::new(Components::NOMINAL);
    let centre = vco.frequency_hz(1.0, 0.5);
    assert!((cents(vco.frequency_hz(1.0, 0.0) / centre) + 1086.62).abs() < 1.0);
    assert!((cents(vco.frequency_hz(1.0, 1.0) / centre) - 891.852).abs() < 1.0);
    let mut previous = vco.frequency_hz(1.0, 0.0);
    for i in 1..=100 {
        let frequency = vco.frequency_hz(1.0, i as f64 / 100.0);
        assert!(frequency > previous);
        previous = frequency;
    }
    for cv in [1.0, 2.0, 3.0, 4.0] {
        assert!(
            (cents(vco.frequency_hz(cv, 0.0) / vco.frequency_hz(cv, 0.5)) + 1086.62).abs() < 1.0
        );
    }
}

#[test]
fn intermediate_tuning_matches_both_sessions_without_retrimming_pitch() {
    let c = Components::NOMINAL;
    let mut fitted = Vco::new(c);
    let mut linear = Vco::new(Components {
        tuning_track_bow: 0.0,
        tuning_track_skew: 0.0,
        ..c
    });
    for rate in [44_100.0, 48_000.0, 96_000.0, 192_000.0, 384_000.0] {
        fitted.prepare(rate).unwrap();
        linear.prepare(rate).unwrap();
        for cv in [1.0, 3.0, 5.0] {
            let centre = fitted.frequency_hz(cv, 0.5);
            // Session-2 same-key offsets: median of MIDI 24/48/72. Allow
            // estimator/key/session variation, rather than pinning coefficients.
            for (rotation, measured_cents) in [(0.25, -502.8), (0.75, 447.3)] {
                assert!(
                    (cents(fitted.frequency_hz(cv, rotation) / centre) - measured_cents).abs()
                        < 1.5
                );
            }
            for rotation in [0.0, 0.5, 1.0] {
                assert_eq!(
                    fitted.frequency_hz(cv, rotation),
                    linear.frequency_hz(cv, rotation)
                );
            }
        }
        fitted.reset();
        linear.reset();
        for _ in 0..1024 {
            let a = fitted.tick(3.0, 0.5);
            let b = linear.tick(3.0, 0.5);
            assert_eq!(a.saw, b.saw);
            assert_eq!(a.square, b.square);
        }
    }
}

#[test]
fn b_taper_track_has_positive_resistance_density_and_physical_loading() {
    let c = Components::NOMINAL;
    // Differentiate the integrated track law independently. The validation
    // bounds must retain positive segment resistance at every panel angle.
    for bow in [-0.25, c.tuning_track_bow, 0.25] {
        for skew in [-0.15, c.tuning_track_skew, 0.15] {
            let vco = Vco::new(Components {
                tuning_track_bow: bow,
                tuning_track_skew: skew,
                ..c
            });
            let mut previous = vco.frequency_hz(1.0, 0.0);
            for i in 1..=1000 {
                let x = i as f64 / 1000.0;
                let u = 2.0 * x - 1.0;
                let density = 1.0 + 0.5 * bow * (1.0 - 3.0 * u * u) + skew * (u - 2.0 * u * u * u);
                assert!(density >= 0.6);
                let frequency = vco.frequency_hz(1.0, x);
                assert!(frequency > previous);
                previous = frequency;
            }
        }
    }
    // Removing R118 loading must reduce the asymmetric endpoint displacement.
    let unloaded = Vco::new(Components {
        tuning_input_resistance: 1e9,
        ..c
    });
    let centre = unloaded.frequency_hz(1.0, 0.5);
    let lo = cents(unloaded.frequency_hz(1.0, 0.0) / centre);
    let hi = cents(unloaded.frequency_hz(1.0, 1.0) / centre);
    assert!((lo + hi).abs() < 1e-4);
}

fn measured_frequency(vco: &mut Vco, rate: f64, cv: f64, tuning: f64) -> f64 {
    let expected = vco.frequency_hz(cv, tuning);
    let samples = (rate * 80.0 / expected).ceil() as usize;
    let mut previous = vco.tick(cv, tuning).saw;
    let mut first = None;
    let mut last = 0.0;
    let mut periods = 0;
    // Crossing away from the reset/kernel ringing.
    let threshold = 8.2;
    for n in 1..samples {
        let x = vco.tick(cv, tuning).saw;
        if previous < threshold && x >= threshold {
            let t = n as f64 - 1.0 + (threshold - previous) / (x - previous);
            if first.is_some() {
                periods += 1;
            } else {
                first = Some(t);
            }
            last = t;
        }
        previous = x;
    }
    periods as f64 * rate / (last - first.unwrap())
}

#[test]
fn measured_pitch_octaves_and_tuning_follow_expo_law() {
    for rate in [44_100.0, 48_000.0, 96_000.0, 192_000.0, 384_000.0] {
        let mut vco = Vco::new(Components::NOMINAL);
        vco.prepare(rate).unwrap();
        for note in (0..=48).step_by(12) {
            vco.reset();
            let cv = cv_for_note(note);
            let f = measured_frequency(&mut vco, rate, cv, 0.5);
            assert!(
                cents(f / vco.frequency_hz(cv, 0.5)).abs() < 1.0,
                "rate={rate}, note={note}, f={f}"
            );
            assert!(
                cents(vco.frequency_hz(cv + 1.0, 0.5) / vco.frequency_hz(cv, 0.5) / 2.0).abs()
                    < 1e-9
            );
        }
        for tuning in [0.0, 0.25, 0.5, 0.75, 1.0] {
            vco.reset();
            let f = measured_frequency(&mut vco, rate, 2.0, tuning);
            assert!(cents(f / vco.frequency_hz(2.0, tuning)).abs() < 1.0);
        }
        assert!(
            (cents(vco.frequency_hz(0.0, 1.0) / vco.frequency_hz(0.0, 0.0)) - 1978.472).abs()
                < 1e-9
        );
    }
    let nominal = Vco::new(Components::NOMINAL);
    assert!((nominal.frequency_hz(1.0, 0.5) - 32.577).abs() < 0.004);
    assert!((nominal.frequency_hz(2.75, 0.5) - 109.576).abs() < 0.001);
    assert!((nominal.charging_current_a(2.75, 0.5) - 6.903e-6).abs() < 1e-9);
    for temp in [280.0, 300.0, 320.0] {
        let v = Vco::new(Components {
            temperature_kelvin: temp,
            ..Components::NOMINAL
        });
        assert!((v.frequency_hz(4.0, 0.5) / v.frequency_hz(2.0, 0.5) / 4.0 - 1.0).abs() < 0.005);
    }
}

#[test]
fn fitted_source_waveforms_and_dc_levels() {
    for rate in [48_000.0, 96_000.0, 192_000.0] {
        let mut vco = Vco::new(Components::NOMINAL);
        vco.prepare(rate).unwrap();
        for _ in 0..(rate / 2.0) as usize {
            vco.tick(2.75, 0.5);
        }
        let mut lo = [f64::INFINITY; 2];
        let mut hi = [f64::NEG_INFINITY; 2];
        let mut falling = 0;
        let mut previous = 0.0;
        let count = (rate / vco.frequency_hz(2.75, 0.5)).round() as usize;
        let mut saw_sum = 0.0;
        let mut duty = 0;
        let mut plateau_sum = 0.0;
        for n in 0..count {
            let out = vco.tick(2.75, 0.5);
            for (i, x) in [out.saw, out.square].into_iter().enumerate() {
                lo[i] = lo[i].min(x);
                hi[i] = hi[i].max(x);
            }
            saw_sum += out.saw;
            duty += usize::from(out.square > 6.5);
            if out.square > 6.5 {
                plateau_sum += out.square;
            }
            falling += usize::from(n > 0 && out.saw < previous);
            previous = out.saw;
        }
        println!(
            "rate {rate} saw {}..{}, square {}..{}, duty {}",
            lo[0],
            hi[0],
            lo[1],
            hi[1],
            duty as f64 / count as f64
        );
        // Kernel overshoot is allowed near the instantaneous reset only.
        assert!(lo[0] < 5.4 && lo[0] > 4.6 && hi[0] > 11.0 && hi[0] < 11.8);
        assert!((saw_sum / count as f64 - 8.19).abs() < 0.04);
        assert!(falling as f64 / count as f64 > 0.8);
        assert!((lo[1] - 64.0 / 12.0).abs() < 1e-12);
        println!("plateau average {}", plateau_sum / duty as f64);
        assert!((hi[1] - 8.0).abs() < 1.0);
        assert!((plateau_sum / duty as f64 - 8.0).abs() < 0.5);
        assert!((0.2..0.7).contains(&(duty as f64 / count as f64)));
        // Hold a high saw: Q8 cuts off, collector stays on the reference rail.
        let mut shaper = SquareShaper::new(Components::NOMINAL);
        shaper.prepare(rate).unwrap();
        for _ in 0..(rate / 4.0) as usize {
            shaper.tick(12.0);
        }
        // The exponential junction retains a tiny forward subthreshold current.
        assert!((shaper.tick(12.0) - 64.0 / 12.0).abs() < 1e-7);
    }
}

#[test]
fn square_even_harmonic_null_varies_with_octave() {
    let rate = 192_000.0;
    let c = Components::NOMINAL;
    let mut h2_db = [0.0; 5];
    for (octave, level) in h2_db.iter_mut().enumerate() {
        // A coherent cycle keeps the collector DC out of the harmonic bins.
        let count = (rate / (c.reference_hz * 2_f64.powi(octave as i32))).round() as usize;
        let cv = 1.0 + (rate / count as f64 / c.reference_hz).log2();
        let mut vco = Vco::new(c);
        vco.prepare(rate).unwrap();
        vco.set_band_limit(Some(20_000.0));
        for _ in 0..((0.4 * rate / count as f64).ceil() as usize * count) {
            vco.tick(cv, 0.5);
        }
        let mut re = [0.0; 2];
        let mut im = [0.0; 2];
        for n in 0..count {
            let collector = vco.tick(cv, 0.5).square;
            for h in 0..2 {
                let angle = TAU * (h + 1) as f64 * n as f64 / count as f64;
                re[h] += collector * angle.cos();
                im[h] += collector * angle.sin();
            }
        }
        let power = |h: usize| re[h] * re[h] + im[h] * im[h];
        *level = 10.0 * (power(1) / power(0)).log10();
    }
    // Session square captures have their deepest low-order even-harmonic
    // cancellation near C3. Downstream filtering changes the exact dB level.
    assert!(h2_db[2] < -24.0, "collector H2/H1 {h2_db:?}");
    assert!(h2_db[2] < h2_db[1] - 8.0 && h2_db[2] < h2_db[3] - 3.0);
}

#[test]
fn stability_sanitisation_and_prepare_errors() {
    for rate in [44_100.0, 48_000.0, 96_000.0, 192_000.0, 384_000.0] {
        let mut vco = Vco::new(Components::NOMINAL);
        vco.prepare(rate).unwrap();
        for n in 0..100_000 {
            let cv = [-16.0, 0.0, 3.0, 16.0][(n / 25_000) % 4];
            let out = vco.tick(cv, if n % 2 == 0 { 0.0 } else { 1.0 });
            assert!(out.saw.is_finite() && out.square.is_finite());
            assert!(out.saw.abs() < 20.0 && out.square.abs() < 20.0);
        }
        for x in [f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
            let out = vco.tick(x, x);
            assert!(out.saw.is_finite() && out.square.is_finite());
        }
        assert_eq!(vco.diagnostics().non_finite_inputs, 6);
        assert!(vco.diagnostics().frequency_limited_samples > 0);
        for bad in [0.0, -1.0, 1000.0, f64::NAN, f64::INFINITY] {
            assert_eq!(vco.prepare(bad), Err(StageError::InvalidSampleRate));
        }
        vco.reset();
        assert_eq!(vco.diagnostics().non_finite_inputs, 0);
        let mut pitch = PitchCv::new(Components::NOMINAL);
        assert_eq!(pitch.prepare(0.0), Err(StageError::InvalidSampleRate));
        let mut square = SquareShaper::new(Components::NOMINAL);
        square.tick(f64::NAN);
        assert_eq!(square.diagnostics().non_finite_inputs, 1);
        assert_eq!(square.prepare(f64::NAN), Err(StageError::InvalidSampleRate));
        square.reset();
        assert_eq!(square.diagnostics().non_finite_inputs, 0);
    }
    let mut bad = Components::NOMINAL;
    bad.square_supply = f64::NAN;
    bad.saw_high = -1.0;
    bad.slide_capacitance = f64::INFINITY;
    bad.tuning_track_bow = f64::NAN;
    bad.tuning_track_skew = f64::INFINITY;
    assert!(Vco::new(bad).tick(f64::MAX, f64::MAX).saw.is_finite());
    assert!(PitchCv::new(bad).tick(255, true).is_finite());
}

// In-place radix-2 FFT, only in tests. Ideal decimation is selection of bins
// below host Nyquist; rectangular integration is coherent (integer periods).
fn fft(xs: &mut [(f64, f64)]) {
    let n = xs.len();
    let mut j = 0;
    for i in 1..n {
        let mut bit = n >> 1;
        while j & bit != 0 {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if i < j {
            xs.swap(i, j);
        }
    }
    let mut length = 2;
    while length <= n {
        let angle = -TAU / length as f64;
        let step = (angle.cos(), angle.sin());
        for start in (0..n).step_by(length) {
            let mut w = (1.0, 0.0);
            for offset in 0..length / 2 {
                let a = xs[start + offset];
                let b = xs[start + offset + length / 2];
                let t = (w.0 * b.0 - w.1 * b.1, w.0 * b.1 + w.1 * b.0);
                xs[start + offset] = (a.0 + t.0, a.1 + t.1);
                xs[start + offset + length / 2] = (a.0 - t.0, a.1 - t.1);
                w = (w.0 * step.0 - w.1 * step.1, w.0 * step.1 + w.1 * step.0);
            }
        }
        length *= 2;
    }
}

#[test]
fn saw_alias_energy_after_ideal_fourfold_decimation_below_minus_80_db() {
    let rate = 192_000.0;
    let n = 65_536;
    let bin = 341;
    let frequency = rate * bin as f64 / n as f64; // 999.0234375 Hz, coherent ~1 kHz.
    let c = Components::NOMINAL;
    let cv = 1.0 + (frequency / c.reference_hz).log2();
    let mut vco = Vco::new(c);
    vco.prepare(rate).unwrap();
    let mut spectrum: Vec<_> = (0..n).map(|_| (vco.tick(cv, 0.5).saw, 0.0)).collect();
    fft(&mut spectrum);
    let power = |x: (f64, f64)| x.0 * x.0 + x.1 * x.1;
    let fundamental = power(spectrum[bin]);
    // Avoid a vacuous pass on silence or a sine-only substitute: ramp fundamental
    // is approximately span/pi peak, and the second harmonic must be substantial.
    assert!(fundamental > (n as f64 * 0.6).powi(2));
    assert!(power(spectrum[2 * bin]) / fundamental > 0.15);
    let alias: f64 = spectrum
        .iter()
        .enumerate()
        .take(n / 8)
        .skip(1)
        .filter(|(k, _)| k % bin != 0)
        .map(|(_, &x)| power(x))
        .sum();
    let db = 10.0 * (alias / fundamental).log10();
    println!("saw alias energy after ideal 192k→48k decimation: {db:.2} dB");
    assert!(db < -80.0, "alias energy {db} dB");
}

#[test]
fn long_run_retains_finite_state_and_reset_is_deterministic() {
    let mut a = Vco::new(Components::NOMINAL);
    a.prepare(48_000.0).unwrap();
    // 41.7 seconds, well beyond every coupling time constant.
    for _ in 0..2_000_000 {
        let out = a.tick(3.0, 0.5);
        assert!(out.saw.is_finite() && out.square.is_finite());
    }
    a.reset();
    let mut b = Vco::new(Components::NOMINAL);
    for _ in 0..1024 {
        let x = a.tick(2.0, 0.5);
        let y = b.tick(2.0, 0.5);
        assert_eq!(x.saw, y.saw);
        assert_eq!(x.square, y.square);
    }
}

/// Whole cycles distinguish source amplitude drift from sub-cycle RMS ripple.
#[test]
fn low_note_source_energy_has_no_long_decay_or_frequency_droop() {
    for rate in [48_000.0, 96_000.0, 192_000.0] {
        let mut energies = [0.0; 3];
        for (note, energy) in energies.iter_mut().enumerate() {
            let c = Components::NOMINAL;
            let frequency = c.reference_hz * 2_f64.powf(note as f64 / 12.0);
            let count = (rate / frequency).round() as usize;
            let cv = 1.0 + (rate / count as f64 / c.reference_hz).log2();
            let mut vco = Vco::new(c);
            vco.prepare(rate).unwrap();
            vco.set_band_limit(Some(20_000.0));
            let mut first = 0.0;
            for cycle in 0..32 {
                let mut sum = 0.0;
                let mut squares = 0.0;
                for _ in 0..count {
                    let saw = vco.tick(cv, 0.5).saw;
                    sum += saw;
                    squares += saw * saw;
                }
                let ac_rms = (squares / count as f64 - (sum / count as f64).powi(2)).sqrt();
                if cycle == 0 {
                    first = ac_rms;
                }
                assert!((ac_rms / first - 1.0).abs() < 1e-8);
                *energy = ac_rms;
            }
            assert!(
                (*energy - 1.69).abs() < 0.02,
                "rate={rate}, note={note}, rms={energy}"
            );
        }
        for energy in energies {
            assert!((energy / energies[0] - 1.0).abs() < 0.001);
        }
    }
}
