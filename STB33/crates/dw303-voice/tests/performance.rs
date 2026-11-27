//! Batch timings at 48 kHz host / 192 kHz circuit, with actual stage inputs.
//! Test-only buffers keep clocks outside the hot path and retain stage order.
use dw303_env::{
    Accent, AccentSweep, FilterControl, Meg, OutputStage, TriggerPulse, Vca, VcfSignals, Veg,
};
use dw303_osc::{PitchCv, Vco};
use dw303_vcf::Vcf;
use dw303_voice::{Voice, VoiceInput};
use std::{hint::black_box, time::Instant};
#[test]
#[ignore = "manual stage profiling; run release with --ignored --nocapture"]
fn stage_costs() {
    for note in [0, 24] {
        const N: usize = 1_536_000;
        let mut v = Voice::new();
        v.prepare(48000.0).unwrap();
        let start = Instant::now();
        for i in 0..N / 4 {
            black_box(v.tick(VoiceInput {
                note,
                gate: true,
                trigger: i == 0,
                accent: false,
                slide: false,
            }));
        }
        println!(
            "note={note} voice {} ns/circuit, {}% core",
            start.elapsed().as_secs_f64() * 1e9 / N as f64,
            start.elapsed().as_secs_f64() / 8.0 * 100.0
        );
        let oc = dw303_osc::Components::NOMINAL;
        let ec = dw303_env::Components::NOMINAL;
        let mut p = PitchCv::new(oc);
        let mut o = Vco::new(oc);
        p.prepare(192000.0).unwrap();
        o.prepare(192000.0).unwrap();
        o.set_band_limit(Some(20000.0));
        let mut osc = Vec::with_capacity(N);
        let start = Instant::now();
        for _ in 0..N {
            osc.push(black_box(o.tick(p.tick(black_box(note), false), 0.5)));
        }
        println!(
            "note={note} osc {} ns",
            start.elapsed().as_secs_f64() * 1e9 / N as f64
        );
        let mut tr = TriggerPulse::new(ec);
        let mut m = Meg::new(ec);
        let mut a = Accent::new(ec);
        let mut s = AccentSweep::new(ec);
        let mut f = FilterControl::new(dw303_voice::calibrated_env_components());
        let mut veg = Veg::new(ec);
        macro_rules! prep {($($v:ident),*)=>{$($v.prepare(192000.0).unwrap();)*};}
        prep!(tr, m, a, s, f, veg);
        let mut control = Vec::with_capacity(N);
        let start = Instant::now();
        for i in 0..N {
            let pulse = tr.tick(i == 0);
            let meg = m.tick(pulse, false, 0.5);
            let ac = a.tick(false, meg, 0.5);
            let sw = s.tick(ac, 0.5);
            let current = f.ladder_current(0.5, 0.5, meg, sw);
            let ve = veg.tick(pulse);
            control.push(black_box((current, ve, ac)));
        }
        println!(
            "note={note} controls {} ns",
            start.elapsed().as_secs_f64() * 1e9 / N as f64
        );
        let mut filter = Vcf::new(dw303_vcf::Components::NOMINAL);
        prep!(filter);
        let mut filtered = Vec::with_capacity(N);
        let start = Instant::now();
        for i in 0..N {
            filtered.push(black_box(filter.tick(osc[i].saw, control[i].0, 0.5)));
        }
        println!(
            "note={note} vcf {} ns {:?}",
            start.elapsed().as_secs_f64() * 1e9 / N as f64,
            filter.diagnostics()
        );
        let mut vca = Vca::new(ec);
        let mut out = OutputStage::new(ec);
        prep!(vca, out);
        let start = Instant::now();
        for i in 0..N {
            let audio = vca.tick(
                VcfSignals {
                    output: filtered[i].output,
                    resonance_tap: filtered[i].resonance_tap,
                },
                control[i].1,
                control[i].2,
            );
            black_box(out.tick(audio, 0.7, 0.0));
        }
        println!(
            "note={note} output {} ns",
            start.elapsed().as_secs_f64() * 1e9 / N as f64
        );
    }
}
