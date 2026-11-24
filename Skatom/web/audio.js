'use strict';
/* SKATOM — Web Audio preview engine.
   Stages 01–02 (glitch buffer, freq shift, contrast, reson combs) run sample-accurate in an AudioWorklet;
   stages 03–06 use native nodes. Each block maps to one JUCE processor in the DSP phase. */

const FX_WORKLET = `
const H1 = [0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737].map(a => a * a);
const H2 = [0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278].map(a => a * a);
const DV = [1, 2, 4, 8, 16, 32, 64, 128, 4, 8, 16, 32];
class SkatomFX extends AudioWorkletProcessor {
  constructor() {
    super();
    this.N = (sampleRate * 4) | 0;
    this.buf = [new Float32Array(this.N), new Float32Array(this.N)];
    this.w = 0;
    this.p = { bpm: 128, div: 4, stutter: 0, reverse: 0, tape: 0, tapeTime: 0.6, glitch: 0, freeze: 0, shift: 0, contrast: 0, resonFreq: 220, tension: 0, roll: 0, jet: 0, spin: 0, pitch: 0 };
    this.PW = sampleRate * 0.06; this.PN = (this.PW | 0) + 8; this.ps = [new Float32Array(this.PN), new Float32Array(this.PN)]; this.pw = 0; this.psPh = 0;
    this.beat = 0; this.rollG = 1; this.spinPh = 0; this.JB = (sampleRate * 0.02) | 0; this.jb = [new Float32Array(this.JB), new Float32Array(this.JB)]; this.jw = 0;
    this.mode = 0; this.t = 0; this.start = 0; this.len = 1; this.rp = 0; this.rate = 1; this.wet = 0; this.fin = 1; this.pos = 0;
    this.clock = 0; this.gl = null; this.tick = 0;
    this.hs = [0, 1].map(() => [0, 1].map(() => [0, 1, 2, 3].map(() => new Float64Array(4)))); // per section: x1 x2 y1 y2
    this.hd = [0, 0]; this.ph = 0; this.ef = 0; this.es = 0;
    this.CM = Math.ceil(sampleRate / 35) + 4;
    this.cb = [0, 1].map(() => [new Float32Array(this.CM), new Float32Array(this.CM)]);
    this.cw = 0; this.cd = [sampleRate / 220, sampleRate / 330]; this.clp = [new Float64Array(2), new Float64Array(2)];
    this.port.onmessage = e => { if (e.data.resetClock) { this.clock = 0; this.gl = null; this.beat = 0; } else Object.assign(this.p, e.data); };
  }
  divLen(d) { return Math.max(64, Math.round(4 / DV[d] * (d >= 8 ? 2 / 3 : 1) * 60 / this.p.bpm * sampleRate)); }
  enter(m) {
    const N = this.N, g = this.gl; this.mode = m; this.t = 0; this.wet = 0;
    if (m === 1) { this.len = this.divLen(g && g.kind === 1 ? g.div : this.p.div); this.start = (this.w - this.len + N) % N; }
    else if (m === 2) { this.len = this.divLen(g && g.kind === 2 ? 4 : 2); this.start = this.w; }
    else if (m === 3) { this.rp = this.w - 1; this.rate = 1; }
    else if (m === 4) { this.len = (sampleRate * 0.12) | 0; this.start = (this.w - this.len + N) % N; }
    else this.fin = 0;
  }
  onStep() { // every 16th: random glitch events, density from DENSITY + macro
    if (this.gl) { if (--this.gl.left <= 0) this.gl = null; return; }
    if (Math.random() < this.p.glitch * 0.5) this.gl = { kind: Math.random() < 0.25 ? 2 : 1, div: [4, 5, 6, 7, 9, 10][(Math.random() * 6) | 0], left: Math.random() < 0.4 ? 2 : 1 };
  }
  hilb(c, x, cs, sn) {
    let a = x, b = x; const s = this.hs[c];
    for (let k = 0; k < 4; k++) { const q = s[0][k], y = H1[k] * (a + q[3]) - q[1]; q[1] = q[0]; q[0] = a; q[3] = q[2]; q[2] = y; a = y; }
    for (let k = 0; k < 4; k++) { const q = s[1][k], y = H2[k] * (b + q[3]) - q[1]; q[1] = q[0]; q[0] = b; q[3] = q[2]; q[2] = y; b = y; }
    const I = this.hd[c]; this.hd[c] = a;
    return I * cs - b * sn;
  }
  process(inputs, outputs) {
    const inp = inputs[0], out = outputs[0], N = this.N, p = this.p, b0 = this.buf[0], b1 = this.buf[1];
    const iL = inp[0], iR = inp[1] || inp[0], oL = out[0], oR = out[1] || out[0], n = oL.length;
    const spStep = 60 / p.bpm / 4 * sampleRate, F = 96;
    const fb = p.tension * 0.96, dT0 = sampleRate / p.resonFreq, dT1 = dT0 / 1.5, dph = p.shift / sampleRate, sm = Math.min(1, Math.abs(p.shift) / 4);
    for (let i = 0; i < n; i++) {
      const xl = iL ? iL[i] : 0, xr = iR ? iR[i] : 0;
      b0[this.w] = xl; b1[this.w] = xr; this.w = (this.w + 1) % N;
      if (++this.clock >= spStep) { this.clock -= spStep; this.onStep(); }
      const g = this.gl;
      const want = p.freeze ? 4 : p.tape ? 3 : (p.stutter || (g && g.kind === 1)) ? 1 : (p.reverse || (g && g.kind === 2)) ? 2 : 0;
      if (want !== this.mode) this.enter(want);
      let yl = xl, yr = xr;
      const m = this.mode;
      // 01 — glitch buffer: 1 stutter, 2 reverse, 3 tape stop, 4 freeze (two Hann grains, 50% overlap)
      if (m) {
        let a = 0, b = 0;
        if (m === 1 || m === 2) {
          if (m === 1 && !g) this.len = this.divLen(p.div); // live division changes re-slice the held loop
          const L = this.len, ph = this.t % L, idx = m === 1 ? this.start + ph : this.start - 1 - ph;
          const j = ((idx % N) + N) % N, f = Math.min(1, ph / F, (L - ph) / F);
          a = b0[j] * f; b = b1[j] * f;
          if (m === 2 && ph === L - 1) this.start = this.w;
          this.t++; this.pos = ph / L;
        } else if (m === 3) {
          this.rate = Math.max(0, this.rate - 1 / (p.tapeTime * sampleRate)); this.rp += this.rate; if (this.rp >= N) this.rp -= N;
          const k = Math.floor(this.rp), fr = this.rp - k, j0 = ((k % N) + N) % N, j1 = (j0 + 1) % N, f = Math.min(1, this.rate * 3);
          a = (b0[j0] + (b0[j1] - b0[j0]) * fr) * f; b = (b1[j0] + (b1[j1] - b1[j0]) * fr) * f;
          this.pos = 1 - this.rate;
        } else {
          const L = this.len;
          for (let k = 0; k < 2; k++) { const ph = (this.t + k * (L >> 1)) % L, h = 0.5 - 0.5 * Math.cos(6.283185307 * ph / L), j = (this.start + ph) % N; a += b0[j] * h; b += b1[j] * h; }
          this.t++; this.pos = (this.t % L) / L;
        }
        if (this.wet < 1) this.wet = Math.min(1, this.wet + 1 / 128);
        yl = xl + (a - xl) * this.wet; yr = xr + (b - xr) * this.wet;
      } else if (this.fin < 1) { this.fin = Math.min(1, this.fin + 1 / 128); yl *= this.fin; yr *= this.fin; }
      // 01b — pitch shifter: two read heads over a 60 ms window, triangular crossfade
      this.ps[0][this.pw] = yl; this.ps[1][this.pw] = yr;
      if (Math.abs(p.pitch) > 0.01) {
        const ratio = Math.pow(2, p.pitch / 12);
        this.psPh += (1 - ratio) / this.PW; this.psPh -= Math.floor(this.psPh);
        let a2 = 0, b2 = 0;
        for (let k = 0; k < 2; k++) {
          let ph2 = this.psPh + k * 0.5; ph2 -= Math.floor(ph2);
          let rp2 = this.pw - (ph2 * this.PW + 2); if (rp2 < 0) rp2 += this.PN;
          const q0 = rp2 | 0, q1 = (q0 + 1) % this.PN, fq = rp2 - q0, g = 1 - Math.abs(2 * ph2 - 1);
          a2 += (this.ps[0][q0] + (this.ps[0][q1] - this.ps[0][q0]) * fq) * g;
          b2 += (this.ps[1][q0] + (this.ps[1][q1] - this.ps[1][q0]) * fq) * g;
        }
        const mp = Math.min(1, Math.abs(p.pitch) * 2); yl += (a2 - yl) * mp; yr += (b2 - yr) * mp;
      }
      this.pw = (this.pw + 1) % this.PN;
      // 02a — frequency shifter (Hilbert allpass pair)
      this.ph += dph; this.ph -= Math.floor(this.ph);
      const cs = Math.cos(6.283185307 * this.ph), sn = Math.sin(6.283185307 * this.ph);
      const sl = this.hilb(0, yl, cs, sn), sr = this.hilb(1, yr, cs, sn);
      yl += (sl - yl) * sm; yr += (sr - yr) * sm;
      // 02b — contrast: fast/slow envelope ratio as an upward expander
      // shortcut: dynamic contrast, not true spectral contrast — the JUCE phase does it per FFT bin
      const ax = Math.max(Math.abs(yl), Math.abs(yr));
      this.ef += (ax - this.ef) * (ax > this.ef ? 0.05 : 0.0015); this.es += (ax - this.es) * 0.00015;
      if (p.contrast > 0.001) { const gc = Math.min(4, Math.pow((this.ef + 1e-5) / (this.es + 1e-5), p.contrast * 1.5)); yl *= gc; yr *= gc; }
      // 02c — reson bank: two damped feedback combs (root + fifth)
      this.cd[0] += (dT0 - this.cd[0]) * 0.002; this.cd[1] += (dT1 - this.cd[1]) * 0.002;
      const CM = this.CM; let accL = 0, accR = 0;
      for (let k = 0; k < 2; k++) {
        let r = this.cw - this.cd[k]; if (r < 0) r += CM;
        const r0 = r | 0, fr = r - r0, r1 = (r0 + 1) % CM;
        for (let c = 0; c < 2; c++) {
          const bb = this.cb[c][k], v = bb[r0] + (bb[r1] - bb[r0]) * fr;
          this.clp[c][k] += 0.5 * (v - this.clp[c][k]);
          const yk = (c ? yr : yl) + fb * this.clp[c][k];
          bb[this.cw] = yk; if (c) accR += yk; else accL += yk;
        }
      }
      this.cw = (this.cw + 1) % CM;
      if (p.tension > 0.001) { const T = p.tension, wg = 0.5 * (1 - fb * 0.85) * T; yl = yl * (1 - 0.6 * T) + accL * wg; yr = yr * (1 - 0.6 * T) + accR * wg; }
      // 05 riser FX (the plugin runs these after the filters; close enough for the preview)
      this.beat += p.bpm / 60 / sampleRate;
      if (p.jet > 0.001) {
        for (let c = 0; c < 2; c++) {
          const jd = (0.0006 + 0.0044 * (0.5 + 0.5 * Math.sin(6.283185307 * (this.beat / 4 + c * 0.25)))) * sampleRate;
          let jp = this.jw - jd; if (jp < 0) jp += this.JB;
          const j0 = jp | 0, j1 = (j0 + 1) % this.JB, jf = jp - j0, b = this.jb[c], dl = b[j0] + (b[j1] - b[j0]) * jf, x = c ? yr : yl;
          b[this.jw] = x + 0.75 * p.jet * dl;
          const y = (x + p.jet * dl) / (1 + 0.5 * p.jet); if (c) yr = y; else yl = y;
        }
      } else { this.jb[0][this.jw] = yl; this.jb[1][this.jw] = yr; }
      this.jw = (this.jw + 1) % this.JB;
      const rd = Math.min(1, p.roll * 2.5), open = rd < 0.001 || (this.beat * Math.pow(2, Math.floor(p.roll * 3.999))) % 1 < 0.5;
      this.rollG += ((open ? 1 : 1 - rd) - this.rollG) * 0.015;
      yl *= this.rollG; yr *= this.rollG;
      if (p.spin > 0.001) {
        this.spinPh = (this.spinPh + 0.25 * Math.pow(2, p.spin * 5) * p.bpm / 60 / sampleRate) % 1;
        const a = (p.spin * Math.sin(6.283185307 * this.spinPh) + 1) * Math.PI / 4;
        yl *= Math.cos(a) * Math.SQRT2; yr *= Math.sin(a) * Math.SQRT2;
      }
      oL[i] = yl; if (oR !== oL) oR[i] = yr;
    }
    if ((this.tick += n) >= 2048) { this.tick = 0; this.port.postMessage({ mode: this.mode, pos: this.pos, len: this.len, rate: this.rate, gl: !!this.gl }); }
    return true;
  }
}
registerProcessor('skatom-fx', SkatomFX);
`;

const FORMANTS = [[800, 1150, 2900], [350, 2000, 2800], [270, 2140, 2950], [450, 800, 2830], [325, 700, 2700]]; // A E I O U
const dB = v => Math.pow(10, v / 20);
const rmsOf = a => { let s = 0; for (let i = 0; i < a.length; i++) s += a[i] * a[i]; return Math.sqrt(s / a.length); };

function makeCurve(type, d) {
  const N = 2048, c = new Float32Array(N), k = 1 + d * 30;
  for (let i = 0; i < N; i++) {
    const x = i / (N - 1) * 2 - 1;
    let y;
    if (type === 'tube') y = (x >= 0 ? Math.tanh(k * x) : Math.tanh(k * 0.7 * x) / 0.9) / Math.tanh(k); // asymmetric → even harmonics
    else if (type === 'mech') y = Math.sin(x * (1 + d * 7) * Math.PI / 2);                                // wavefolder
    else if (type === 'crush') { const st = 2 / Math.pow(2, 12 - d * 10.5); y = Math.round(x / st) * st; } // 12 → 1.5 bits
    else y = 0.55 * x + 0.45 * Math.tanh(k * 2 * x);                                                     // xcite: parallel harmonics
    c[i] = clamp(y, -1, 1);
  }
  return c;
}

class SkatomEngine {
  constructor(state) {
    this.S = state; this.ctx = null; this.playing = false; this.source = 'loop'; this.bpm = 128;
    this.env = 0; this.rms = 0; this.gr = 0; this.t0 = 0;
    this.activity = { mode: 0, pos: 0, len: 1, rate: 1, gl: false };
    this.sent = {}; this.curves = {}; this.bands = new Float32Array(40);
  }

  async ensure() {
    if (this.ctx) return;
    const ctx = new AudioContext({ latencyHint: 'interactive' });
    await ctx.audioWorklet.addModule(URL.createObjectURL(new Blob([FX_WORKLET], { type: 'text/javascript' })));
    this.build(ctx);
    this.ctx = ctx;
  }

  build(ctx) {
    const G = (v = 1) => { const g = ctx.createGain(); g.gain.value = v; return g; };
    const BQ = (type, f, q = 0.707, gain = 0) => { const b = ctx.createBiquadFilter(); b.type = type; b.frequency.value = f; b.Q.value = q; b.gain.value = gain; return b; };
    const n = this.n = {};
    n.in = G(); n.inAn = ctx.createAnalyser(); n.inAn.fftSize = 1024; n.in.connect(n.inAn);
    n.dry = G(0); n.in.connect(n.dry);
    // 01–02 worklet
    n.fx = new AudioWorkletNode(ctx, 'skatom-fx', { outputChannelCount: [2] });
    n.fx.port.onmessage = e => this.activity = e.data;
    n.in.connect(n.fx);
    // 03 dual filter + formant
    n.fDrive = ctx.createWaveShaper(); n.fDrive.oversample = '2x';
    n.filt = BQ('lowpass', 20000);
    n.fx.connect(n.fDrive).connect(n.filt);
    n.formDry = G(1); n.formWet = G(0);
    n.formants = [1, 0.5, 0.25].map(lvl => { const b = BQ('bandpass', 800, 8); n.filt.connect(b).connect(G(lvl)).connect(n.formWet); return b; });
    n.filt.connect(n.formDry);
    // 04 multi-distortion + magic bass
    n.distIn = G(); n.formDry.connect(n.distIn); n.formWet.connect(n.distIn);
    n.shaper = ctx.createWaveShaper(); n.shaper.oversample = '4x';
    n.distWet = G(1); n.distDry = G(0);
    n.distIn.connect(n.shaper).connect(n.distWet); n.distIn.connect(n.distDry);
    n.bassShelf = BQ('lowshelf', 90); n.bassMud = BQ('peaking', 280, 1);
    n.distWet.connect(n.bassShelf); n.distDry.connect(n.bassShelf); n.bassShelf.connect(n.bassMud);
    // 05 build-up wash bus
    n.hpf1 = BQ('highpass', 20); n.hpf2 = BQ('highpass', 20, 0.9);
    n.bassMud.connect(n.hpf1).connect(n.hpf2);
    n.bus = G(); n.hpf2.connect(n.bus);
    const nb = ctx.createBuffer(2, ctx.sampleRate * 2, ctx.sampleRate);
    for (let c = 0; c < 2; c++) { const d = nb.getChannelData(c); for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1; }
    n.noiseSrc = ctx.createBufferSource(); n.noiseSrc.buffer = nb; n.noiseSrc.loop = true; n.noiseSrc.start();
    n.noiseBP = BQ('bandpass', 600, 1.2); n.noiseG = G(0);
    n.noiseSrc.connect(n.noiseBP).connect(n.noiseG).connect(n.bus);
    n.chG = G(0); n.chMod = [];
    [-1, 1].forEach((pan, i) => { // detune: two LFO-swept delays, hard L/R
      const d = ctx.createDelay(0.1); d.delayTime.value = 0.012;
      const o = ctx.createOscillator(); o.frequency.value = [0.63, 0.91][i];
      const od = G(0); o.connect(od).connect(d.delayTime); o.start(); n.chMod.push(od);
      const p = ctx.createStereoPanner(); p.pan.value = pan;
      n.hpf2.connect(d).connect(p).connect(n.chG);
    });
    n.chG.connect(n.bus);
    n.send = G(0); n.bus.connect(n.send);
    // the reverb keeps its body: add back what the dry HPF took away, through a gentler HPF (mirrors hpfSend in C++)
    n.hpfSend = BQ('highpass', 20); n.hpfSend2 = BQ('highpass', 20, 0.9); n.bassMud.connect(n.hpfSend).connect(n.hpfSend2).connect(n.send);
    n.hpfNeg = G(-1); n.hpf2.connect(n.hpfNeg).connect(n.send);
    // small bright room + huge dark hall; WASH crossfades small → huge ("the reverb opens")
    n.verb = ctx.createConvolver(); n.verb.buffer = this.makeIR(ctx, 1.4, 0.55);
    n.verbL = ctx.createConvolver(); n.verbL.buffer = this.makeIR(ctx, 6.5, 0.22);
    n.sendS = G(1); n.sendL = G(0);
    n.dly = ctx.createDelay(2); n.dlyFb = G(0.45); n.dlyLP = BQ('lowpass', 3500);
    n.send.connect(n.sendS).connect(n.verb); n.send.connect(n.sendL).connect(n.verbL); n.dly.connect(n.dlyLP).connect(n.dlyFb).connect(n.dly);
    n.duck = G(1); n.verb.connect(n.duck); n.verbL.connect(n.duck); n.dlyLP.connect(n.duck);
    n.mix = G(); n.busOut = G(); n.bus.connect(n.busOut).connect(n.mix); n.duck.connect(n.mix); // busOut = 1 - swallow
    // 06 master dynamics
    n.comp = ctx.createDynamicsCompressor(); n.comp.knee.value = 6; n.comp.attack.value = 0.005; n.comp.release.value = 0.12;
    n.compMake = G();
    n.lim = ctx.createDynamicsCompressor(); n.lim.knee.value = 0; n.lim.attack.value = 0.001; n.lim.release.value = 0.08;
    n.mix.connect(n.comp).connect(n.compMake).connect(n.lim);
    n.wet = G(); n.lim.connect(n.wet);
    n.out = G(); n.wet.connect(n.out); n.dry.connect(n.out);
    n.outAn = ctx.createAnalyser(); n.outAn.fftSize = 2048; n.outAn.smoothingTimeConstant = 0.7;
    n.out.connect(n.outAn); n.out.connect(ctx.destination);
    this.tdIn = new Float32Array(n.inAn.fftSize); this.tdOut = new Float32Array(n.outAn.fftSize);
    this.fq = new Uint8Array(n.outAn.frequencyBinCount);
  }

  makeIR(ctx, sec, bright = 0.35) {
    const n = (sec * ctx.sampleRate) | 0, b = ctx.createBuffer(2, n, ctx.sampleRate);
    for (let c = 0; c < 2; c++) { const d = b.getChannelData(c); let lp = 0; for (let i = 0; i < n; i++) { lp += bright * ((Math.random() * 2 - 1) - lp); d[i] = lp * Math.pow(1 - i / n, 2.2); } }
    return b;
  }

  // 2-bar rolling psy test loop: kick, rolling F bass on 16ths, hats, clap
  makeLoop() {
    const sr = this.ctx.sampleRate, beat = 60 / this.bpm, len = Math.round(beat * 8 * sr);
    const b = this.ctx.createBuffer(2, len, sr), L = b.getChannelData(0), R = b.getChannelData(1);
    const add = (t0, dur, fn, pan = 0) => { const s0 = Math.round(t0 * sr), n = Math.min(Math.round(dur * sr), len - s0); for (let i = 0; i < n; i++) { const v = fn(i / sr); L[s0 + i] += v * (1 - pan); R[s0 + i] += v * (1 + pan); } };
    for (let q = 0; q < 8; q++) {
      const t = q * beat;
      let ph = 0;
      add(t, 0.35, x => { ph += 2 * Math.PI * (45 + 260 * Math.exp(-x * 28)) / sr; return Math.sin(ph) * Math.exp(-x * 7) * 0.9; });
      for (let s = 1; s < 4; s++) {
        let p = 0, lp = 0; const f = q % 4 === 3 && s === 3 ? 51.91 : 43.65;
        add(t + s * beat / 4, beat / 4 * 0.9, x => { p = (p + f / sr) % 1; lp += (0.04 + 0.5 * Math.exp(-x * 30)) * (2 * p - 1 - lp); return lp * Math.exp(-x * 9) * 0.55; });
      }
      for (let s = 0; s < 4; s++) {
        let prev = 0; const open = s === 2;
        add(t + s * beat / 4, open ? 0.12 : 0.04, x => { const w = Math.random() * 2 - 1, h = w - prev; prev = w; return h * Math.exp(-x * (open ? 35 : 90)) * (open ? 0.22 : 0.1); }, open ? 0.3 : -0.3);
      }
      if (q % 2 === 1) add(t, 0.2, x => (Math.random() * 2 - 1) * Math.exp(-x * 25) * (x < 0.03 && Math.floor(x * 300) % 3 ? 0.4 : 1) * 0.25);
    }
    for (let i = 0; i < len; i++) { L[i] = Math.tanh(L[i]); R[i] = Math.tanh(R[i]); }
    return b;
  }

  get sampleRate() { return this.ctx ? this.ctx.sampleRate : 48000; }
  beats(t) { return this.playing ? (this.ctx.currentTime - this.t0) * this.bpm / 60 : t * this.bpm / 60; }

  async start() { await this.ensure(); await this.ctx.resume(); await this.startSource(); this.playing = true; }
  stop() { this.stopSource(); this.playing = false; }
  async startSource() {
    this.stopSource();
    const ctx = this.ctx;
    if (this.source === 'mic') {
      this.mic = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
      this.src = ctx.createMediaStreamSource(this.mic);
    } else {
      const s = this.src = ctx.createBufferSource();
      s.buffer = this.source === 'file' && this.fileBuf ? this.fileBuf : (this.loopBuf ||= this.makeLoop());
      s.loop = true; s.start();
    }
    this.src.connect(this.n.in);
    this.t0 = ctx.currentTime;
    this.n.fx.port.postMessage({ resetClock: 1 });
  }
  stopSource() {
    if (this.src) { try { this.src.stop && this.src.stop(); } catch (e) { /* already stopped */ } this.src.disconnect(); this.src = null; }
    if (this.mic) { this.mic.getTracks().forEach(t => t.stop()); this.mic = null; }
  }

  // DROP: duck the wash tail so the drop hits clean
  drop() {
    if (!this.ctx) return;
    const g = this.n.duck.gain, t = this.ctx.currentTime;
    g.cancelScheduledValues(t); g.setValueAtTime(g.value, t);
    g.linearRampToValueAtTime(1 - this.S.get('dropDuck'), t + 0.01);
    g.setTargetAtTime(1, t + 0.25, 0.35);
  }

  shape(node, type, d) {
    const q = Math.round(d * 40), key = type + q;
    if (node._key === key) return;
    node._key = key;
    node.curve = q === 0 ? null : (this.curves[key] ||= makeCurve(type, q / 40));
  }

  // called once per animation frame: meters, envelope follower, push effective params
  update(dt) {
    if (!this.ctx) return;
    const S = this.S, n = this.n, now = this.ctx.currentTime, e = id => S.eff(id);
    const tgt = (key, param, v, tc = 0.02) => { if (this.sent[key] === v) return; this.sent[key] = v; param.setTargetAtTime(v, now, tc); };

    n.inAn.getFloatTimeDomainData(this.tdIn);
    const envT = clamp(rmsOf(this.tdIn) * (1 + S.get('envSens') * 15));
    this.env += (envT - this.env) * (1 - Math.exp(-dt / ((envT > this.env ? S.get('envAtk') : S.get('envRel')) / 1000)));
    n.outAn.getFloatTimeDomainData(this.tdOut); this.rms = rmsOf(this.tdOut);
    n.outAn.getByteFrequencyData(this.fq);
    const hz = this.ctx.sampleRate / 2 / this.fq.length;
    for (let i = 0; i < 40; i++) { // 40 log bands, 30 Hz – 16 kHz (the plugin sends the same)
      const f0 = 30 * Math.pow(16000 / 30, i / 40), f1 = 30 * Math.pow(16000 / 30, (i + 1) / 40);
      let v = 0; for (let b = Math.floor(f0 / hz); b <= Math.ceil(f1 / hz) && b < this.fq.length; b++) v = Math.max(v, this.fq[b] / 255);
      this.bands[i] = v;
    }
    this.gr = n.comp.reduction;

    const bp = S.get('bypass'), dw = S.get('dryWet');
    tgt('dry', n.dry.gain, bp ? 1 : 1 - dw); tgt('wet', n.wet.gain, bp ? 0 : dw);
    tgt('out', n.out.gain, dB(S.get('output')));
    // 01–02 → worklet (only changed fields)
    const w = { bpm: this.bpm, div: S.get('stutDiv'), stutter: S.get('stutter'), reverse: S.get('reverse'), tape: S.get('tapeStop'), tapeTime: e('tapeTime'), glitch: e('glitchAmt'), freeze: S.get('freeze'), shift: e('freqShift'), contrast: e('contrast'), resonFreq: e('resonFreq'), tension: e('resonTension'), roll: e('roll'), jet: e('jet'), spin: e('spin'), pitch: e('pitch') };
    const diff = {}; let any = false;
    for (const k in w) if (this.sent['w_' + k] !== w[k]) { this.sent['w_' + k] = diff[k] = w[k]; any = true; }
    if (any) n.fx.port.postMessage(diff);
    // 03
    const ft = ['lowpass', 'bandpass', 'highpass'][S.get('filtType')];
    if (n.filt.type !== ft) n.filt.type = ft;
    tgt('fc', n.filt.frequency, e('filtCut')); tgt('fq', n.filt.Q, e('filtRes'));
    this.shape(n.fDrive, 'tube', e('filtDrive'));
    const vw = e('vowel'), i0 = Math.min(3, Math.floor(vw)), fr = vw - i0;
    n.formants.forEach((b, j) => tgt('fm' + j, b.frequency, FORMANTS[i0][j] + (FORMANTS[i0 + 1][j] - FORMANTS[i0][j]) * fr));
    const fm = e('formantMix'); tgt('fmd', n.formDry.gain, 1 - fm); tgt('fmw', n.formWet.gain, fm * 4);
    // 04
    const dd = e('distDrive'), dm = e('distMix');
    this.shape(n.shaper, ['tube', 'mech', 'crush', 'xcite'][S.get('distType')], dd);
    tgt('dw', n.distWet.gain, dm / (1 + dd * 1.2)); tgt('dd', n.distDry.gain, 1 - dm);
    tgt('bs', n.bassShelf.gain, e('magicBass') * 10); tgt('bm', n.bassMud.gain, -e('magicBass') * 5);
    // 05
    const hp = e('hpf'); tgt('h1', n.hpf1.frequency, hp); tgt('h2', n.hpf2.frequency, hp); const fs = Math.min(hp, 300 + 0.05 * hp); tgt('hs', n.hpfSend.frequency, fs); tgt('hs2', n.hpfSend2.frequency, fs);
    const nz = e('noise'); tgt('ng', n.noiseG.gain, Math.pow(nz, 1.5) * 0.3); tgt('nf', n.noiseBP.frequency, 400 * Math.pow(2, nz * 4.5));
    const de = e('detune'); tgt('cg', n.chG.gain, de * 0.8); n.chMod.forEach((g, j) => tgt('cm' + j, g.gain, 0.0004 + de * 0.004));
    tgt('sw', n.busOut.gain, 1 - S.swallow); tgt('ws', n.send.gain, e('wash') * 1.3); const size = clamp(e('wash') * 1.4); tgt('vs', n.sendS.gain, 1 - size); tgt('vl', n.sendL.gain, size);
    tgt('dl', n.dly.delayTime, 0.75 * 60 / this.bpm, 0.1);
    // 06
    const cp = e('comp'); tgt('ct', n.comp.threshold, -cp * 36); tgt('cr', n.comp.ratio, 1 + cp * 9); tgt('cmk', n.compMake.gain, dB(cp * 8));
    const lim = S.get('limiter'); tgt('lt', n.lim.threshold, lim ? S.get('ceiling') : 0); tgt('lr', n.lim.ratio, lim ? 20 : 1);
  }
}
