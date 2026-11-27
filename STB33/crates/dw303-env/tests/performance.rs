//! Manual isolated release benchmark; no frozen provisional implementation.
use dw303_env::*;
use std::hint::black_box;

#[test]
#[ignore = "manual stage profiling"]
fn stage_costs() {
    let c = black_box(Components::NOMINAL);
    let mut p = TriggerPulse::new(c);
    let mut m = Meg::new(c);
    let mut a = Accent::new(c);
    let mut s = AccentSweep::new(c);
    let mut f = FilterControl::new(c);
    let mut v = Veg::new(c);
    let mut ota = Vca::new(c);
    let mut o = OutputStage::new(c);
    macro_rules! timing {
        ($stage:ident, $call:expr) => {{
            $stage.prepare(192000.0).unwrap();
            let start = std::time::Instant::now();
            for _ in 0..1_536_000 {
                black_box($call);
            }
            println!(
                "{}: {:.6} s",
                stringify!($stage),
                start.elapsed().as_secs_f64()
            );
        }};
    }
    timing!(p, black_box(&mut p).tick(black_box(false)));
    timing!(
        m,
        black_box(&mut m).tick(black_box(0.0), black_box(false), black_box(0.5))
    );
    timing!(
        a,
        black_box(&mut a).tick(black_box(true), black_box(5.0), black_box(0.7))
    );
    timing!(s, black_box(&mut s).tick(black_box(5.0), black_box(0.7)));
    timing!(
        f,
        black_box(&mut f).ladder_current(
            black_box(0.6),
            black_box(0.7),
            black_box(5.0),
            black_box(3.0)
        )
    );
    timing!(v, black_box(&mut v).tick(black_box(0.0)));
    timing!(
        ota,
        black_box(&mut ota).tick(
            black_box(VcfSignals {
                output: 0.3,
                resonance_tap: 0.21
            }),
            black_box(1e-5),
            black_box(5.0)
        )
    );
    timing!(
        o,
        black_box(&mut o).tick(black_box(0.3), black_box(0.8), black_box(0.0))
    );
}

#[test]
#[ignore = "manual release timing benchmark"]
fn benchmark_revision_two() {
    let mut best = f64::INFINITY;
    for _ in 0..5 {
        let c = black_box(Components::NOMINAL);
        let mut p = TriggerPulse::new(c);
        let mut m = Meg::new(c);
        let mut a = Accent::new(c);
        let mut s = AccentSweep::new(c);
        let mut f = FilterControl::new(c);
        let mut v = Veg::new(c);
        let mut ota = Vca::new(c);
        let mut o = OutputStage::new(c);
        macro_rules! prepare {($($stage:ident),*)=>{$($stage.prepare(192000.0).unwrap();)*};}
        prepare!(p, m, a, s, f, v, ota, o);
        let start = std::time::Instant::now();
        for n in 0..1_536_000 {
            let gate = n % 24_000 < 16_000;
            let accent = gate && n % 72_000 < 48_000;
            let knob = black_box((n / 24_000 % 11) as f64 / 10.0);
            let pulse = black_box(&mut p).tick(n % 24_000 == 0);
            let meg = black_box(&mut m).tick(pulse, accent, knob);
            v.set_gate(gate);
            let veg = black_box(&mut v).tick(pulse);
            let acc = black_box(&mut a).tick(accent, meg, knob);
            let sweep = black_box(&mut s).tick(acc, 0.7);
            black_box(black_box(&mut f).ladder_current(0.6, knob, meg, sweep));
            let audio = black_box(&mut ota).tick(
                VcfSignals {
                    output: black_box(0.3),
                    resonance_tap: black_box(0.21),
                },
                veg,
                acc,
            );
            black_box(black_box(&mut o).tick(audio, 0.8, black_box(0.0)));
        }
        let elapsed = start.elapsed().as_secs_f64();
        best = best.min(elapsed);
        println!("Revision 2 eight stages / 8 s at 192 kHz: {elapsed:.6} s");
    }
    println!(
        "best {best:.6} s; {:.2}% core; ratio to measured provisional 0.066706 s: {:.3}",
        best / 8.0 * 100.0,
        best / 0.066706
    );
}
