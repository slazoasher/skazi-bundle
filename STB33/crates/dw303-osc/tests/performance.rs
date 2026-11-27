//! Manual timing of the fitted circuit, independent of frozen waveforms.
use dw303_osc::{Components, Vco};

#[test]
#[ignore = "wall-clock benchmark; run in release mode with --ignored --nocapture"]
fn held_pitch_cost() {
    use std::{hint::black_box, time::Instant};
    let mut vco = Vco::new(Components::NOMINAL);
    vco.prepare(192_000.0).unwrap();
    vco.set_band_limit(Some(20_000.0));
    let start = Instant::now();
    for n in 0..384_000 {
        let out = black_box(vco.tick(black_box((n / 24_000 % 4) as f64), 0.5));
        assert!(out.saw.is_finite() && out.square.is_finite());
    }
    println!("Fitted VCO, 384000 samples: {:?}", start.elapsed());
}
