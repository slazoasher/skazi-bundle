//! STB33 post-voice chain: ADSR amp envelope -> distortion -> OTT -> width -> reverb.
//! Every stage is an exact pass-through at its default knob value.

/// Knob values (0..1 panel fractions).
#[derive(Clone, Copy, Debug, Default)]
pub struct FxKnobs {
    pub attack: f64,
    pub decay: f64,
    pub sustain: f64,
    pub release: f64,
    pub drive: f64,
    pub ott: f64,
    pub reverb: f64,
    pub width: f64,
}

fn one_pole(seconds: f64, rate: f64) -> f64 {
    1.0 - (-1.0 / (seconds * rate).max(1.0)).exp()
}
/// 0..1 knob to seconds on an exponential taper.
fn time(knob: f64, min: f64, max: f64) -> f64 {
    min * (max / min).powf(knob.clamp(0.0, 1.0))
}

#[derive(Clone, Copy, PartialEq, Debug)]
enum Stage {
    Idle,
    Attack,
    Decay,
    Release,
}

struct Adsr {
    stage: Stage,
    level: f64,
    gate: bool,
}

impl Adsr {
    fn tick(&mut self, k: &FxKnobs, rate: f64, gate: bool, trigger: bool) -> f64 {
        if gate && (trigger || !self.gate) {
            self.stage = Stage::Attack;
        } else if !gate && self.gate {
            self.stage = Stage::Release;
        }
        self.gate = gate;
        match self.stage {
            Stage::Attack => {
                if k.attack < 0.01 {
                    self.level = 1.0;
                } else {
                    // Aim past 1 so the curve reaches it in the set time.
                    self.level += (1.3 - self.level) * one_pole(time(k.attack, 0.001, 3.0), rate);
                }
                if self.level >= 1.0 {
                    self.level = 1.0;
                    self.stage = Stage::Decay;
                }
            }
            Stage::Decay => {
                self.level += (k.sustain - self.level) * one_pole(time(k.decay, 0.005, 4.0), rate);
            }
            Stage::Release => {
                // RELEASE at zero leaves the fade to the circuit's own VCA.
                if k.release >= 0.01 {
                    self.level -= self.level * one_pole(time(k.release, 0.005, 5.0), rate);
                    if self.level < 1e-4 {
                        self.level = 0.0;
                        self.stage = Stage::Idle;
                    }
                }
            }
            Stage::Idle => {}
        }
        self.level
    }
    /// The voice gate is held open while a set RELEASE is still fading.
    fn holding(&self, k: &FxKnobs) -> bool {
        self.stage == Stage::Release && k.release >= 0.01
    }
}

#[derive(Default)]
struct Lp(f64);
impl Lp {
    fn tick(&mut self, x: f64, c: f64) -> f64 {
        self.0 += (x - self.0) * c;
        self.0
    }
}

/// One OTT band: downward above and upward below a -18 dB threshold.
#[derive(Default)]
struct Band {
    env: f64,
}
impl Band {
    fn tick(&mut self, x: f64, amount: f64, att: f64, rel: f64) -> f64 {
        let a = x.abs();
        self.env += (a - self.env) * if a > self.env { att } else { rel };
        let db = 20.0 * (self.env + 1e-9).log10();
        const THRESHOLD: f64 = -18.0;
        let gain_db = if db > THRESHOLD {
            (THRESHOLD - db) * 0.75 // 4:1 down
        } else if db > -70.0 {
            ((THRESHOLD - db) * 0.67).min(24.0) // 3:1 up, capped, noise floor left alone
        } else {
            0.0
        };
        x * 10f64.powf(gain_db * amount / 20.0)
    }
}

struct Comb {
    buf: Vec<f64>,
    pos: usize,
    store: f64,
}
impl Comb {
    fn new(len: usize) -> Self {
        Self { buf: vec![0.0; len.max(1)], pos: 0, store: 0.0 }
    }
    fn clear(&mut self) {
        self.buf.fill(0.0);
        self.pos = 0;
        self.store = 0.0;
    }
    fn tick(&mut self, x: f64, feedback: f64, damp: f64) -> f64 {
        let out = self.buf[self.pos];
        self.store = out * (1.0 - damp) + self.store * damp;
        self.buf[self.pos] = x + self.store * feedback;
        self.pos = (self.pos + 1) % self.buf.len();
        out
    }
}
struct Allpass {
    buf: Vec<f64>,
    pos: usize,
}
impl Allpass {
    fn new(len: usize) -> Self {
        Self { buf: vec![0.0; len.max(1)], pos: 0 }
    }
    fn clear(&mut self) {
        self.buf.fill(0.0);
        self.pos = 0;
    }
    fn tick(&mut self, x: f64) -> f64 {
        let b = self.buf[self.pos];
        self.buf[self.pos] = x + b * 0.5;
        self.pos = (self.pos + 1) % self.buf.len();
        b - x
    }
}

/// Freeverb-style stereo hall.
struct Reverb {
    combs: [Vec<Comb>; 2],
    allpasses: [Vec<Allpass>; 2],
    hp: Lp,
}
impl Reverb {
    fn new(rate: f64) -> Self {
        let s = rate / 44100.0;
        let len = |n: usize, spread: usize| ((n + spread) as f64 * s) as usize;
        let side = |spread| {
            (
                [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617]
                    .map(|n| Comb::new(len(n, spread)))
                    .into(),
                [556, 441, 341, 225].map(|n| Allpass::new(len(n, spread))).into(),
            )
        };
        let (cl, al) = side(0);
        let (cr, ar) = side(23);
        Self { combs: [cl, cr], allpasses: [al, ar], hp: Lp::default() }
    }
    fn clear(&mut self) {
        self.combs.iter_mut().flatten().for_each(Comb::clear);
        self.allpasses.iter_mut().flatten().for_each(Allpass::clear);
        self.hp = Lp::default();
    }
    fn tick(&mut self, x: f64, hp: f64) -> (f64, f64) {
        // Keep the bass out of the tail.
        let input = (x - self.hp.tick(x, hp)) * 0.015;
        let mut out = [0.0; 2];
        for (c, o) in out.iter_mut().enumerate() {
            let mut sum = 0.0;
            for comb in &mut self.combs[c] {
                sum += comb.tick(input, 0.86, 0.3);
            }
            for ap in &mut self.allpasses[c] {
                sum = ap.tick(sum);
            }
            *o = sum;
        }
        (out[0], out[1])
    }
}

pub struct Fx {
    rate: f64,
    adsr: Adsr,
    split_low: Lp,
    split_mid: Lp,
    bands: [Band; 3],
    haas: Vec<f64>,
    haas_pos: usize,
    side_hp: Lp,
    reverb: Reverb,
}

impl Fx {
    pub fn new(rate: f64) -> Self {
        Self {
            rate,
            adsr: Adsr { stage: Stage::Idle, level: 0.0, gate: false },
            split_low: Lp::default(),
            split_mid: Lp::default(),
            bands: Default::default(),
            haas: vec![0.0; ((0.013 * rate) as usize).max(1)],
            haas_pos: 0,
            side_hp: Lp::default(),
            reverb: Reverb::new(rate),
        }
    }

    /// Silence every tail (envelope, OTT, Haas, reverb) without allocating: safe on the audio thread.
    pub fn clear(&mut self) {
        self.adsr = Adsr { stage: Stage::Idle, level: 0.0, gate: false };
        self.split_low = Lp::default();
        self.split_mid = Lp::default();
        self.bands = Default::default();
        self.haas.fill(0.0);
        self.haas_pos = 0;
        self.side_hp = Lp::default();
        self.reverb.clear();
    }

    pub fn holding(&self, k: &FxKnobs) -> bool {
        self.adsr.holding(k)
    }

    pub fn tick(&mut self, k: &FxKnobs, x: f64, gate: bool, trigger: bool) -> (f64, f64) {
        let rate = self.rate;
        let mut x = x * self.adsr.tick(k, rate, gate, trigger);

        if k.drive > 0.0 {
            let g = 1.0 + 40.0 * k.drive * k.drive;
            // Normalised so a 0.5 peak keeps its level; slight asymmetry adds even harmonics.
            let shaped = ((x + 0.1 * k.drive) * g).tanh() - (0.1 * k.drive * g).tanh();
            x = shaped / (0.5 * g).tanh() * 0.5;
        }

        if k.ott > 0.0 {
            let low = self.split_low.tick(x, one_pole(1.0 / (2.0 * std::f64::consts::PI * 120.0), rate));
            let rest = x - low;
            let mid = self.split_mid.tick(rest, one_pole(1.0 / (2.0 * std::f64::consts::PI * 2500.0), rate));
            let high = rest - mid;
            let (att, rel) = (one_pole(0.003, rate), one_pole(0.08, rate));
            x = self.bands[0].tick(low, k.ott, att, rel)
                + self.bands[1].tick(mid, k.ott, att, rel)
                + self.bands[2].tick(high, k.ott, att, rel);
        }

        let delayed = self.haas[self.haas_pos];
        self.haas[self.haas_pos] = x;
        self.haas_pos = (self.haas_pos + 1) % self.haas.len();
        // Side from a 13 ms copy, high-passed at 200 Hz so the bass stays mono.
        let side = (delayed - self.side_hp.tick(delayed, one_pole(1.0 / (2.0 * std::f64::consts::PI * 200.0), rate)))
            * 0.8
            * k.width;
        let (mut l, mut r) = (x + side, x - side);

        if k.reverb > 0.0 {
            let hp = one_pole(1.0 / (2.0 * std::f64::consts::PI * 150.0), rate);
            let (wl, wr) = self.reverb.tick(0.5 * (l + r), hp);
            l += wl * k.reverb;
            r += wr * k.reverb;
        }
        (l, r)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn defaults_pass_through_and_effects_stay_finite() {
        let neutral = FxKnobs { decay: 0.5, sustain: 1.0, ..Default::default() };
        let mut fx = Fx::new(48000.0);
        for n in 0..2000 {
            let x = (n as f64 * 0.05).sin() * 0.5;
            let (l, r) = fx.tick(&neutral, x, true, n == 0);
            assert!((l - x).abs() < 1e-12 && (r - x).abs() < 1e-12);
        }
        let all = FxKnobs { attack: 0.3, decay: 0.4, sustain: 0.5, release: 0.5, drive: 1.0, ott: 1.0, reverb: 1.0, width: 1.0 };
        let mut fx = Fx::new(44100.0);
        for n in 0..44100 {
            let (l, r) = fx.tick(&all, (n as f64 * 0.03).sin(), n < 20000, n == 0);
            assert!(l.is_finite() && r.is_finite() && l.abs() < 20.0);
        }
        assert!(fx.holding(&all) || fx.adsr.stage == Stage::Idle);
    }
}
