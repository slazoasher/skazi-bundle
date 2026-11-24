'use strict';
/* SKATOM — state layer (SkatomState).
   Every param id below maps 1:1 to a JUCE AudioProcessorValueTreeState id in the DSP phase.
   Base values live in real units; snapshots, the Mega-Macro and modulation all work in normalized 0..1 space. */

const DIVS = ['1/1', '1/2', '1/4', '1/8', '1/16', '1/32', '1/64', '1/128', '1/4T', '1/8T', '1/16T', '1/32T'];
const LFO_RATES = ['4/1', '2/1', '1/1', '1/2', '1/4', '1/8', '1/16', '1/32'];
const LFO_BEATS = [16, 8, 4, 2, 1, 0.5, 0.25, 0.125];
const LFO_SHAPES = ['SIN', 'TRI', 'SAW', 'SQR', 'S&H'];
const TARGET_NAMES = { filtCut: 'CUTOFF', vowel: 'VOWEL', resonTension: 'TENSION', resonFreq: 'RESON FREQ', freqShift: 'FREQ SHIFT', hpf: 'HPF', wash: 'WASH', distDrive: 'DIST DRIVE', glitchAmt: 'GLITCH DENS', contrast: 'CONTRAST', roll: 'ROLL', jet: 'JET', spin: 'SPIN', pitch: 'PITCH' };
const MOD_TARGETS = Object.keys(TARGET_NAMES);
const VOWELS = ['A', 'E', 'I', 'O', 'U'];

const PARAMS = [];
const P = (id, name, min, max, def, o = {}) => PARAMS.push({ id, name, min, max, def, type: 'float', snap: true, ...o });
const B = (id, name, def = 0) => P(id, name, 0, 1, def, { type: 'bool', snap: false });
const EN = (id, name, options, def, o = {}) => P(id, name, 0, options.length - 1, def, { type: 'enum', options, ...o });

// global
P('dryWet', 'MIX', 0, 1, 1, { snap: false });
P('output', 'OUTPUT', -24, 12, 0, { unit: 'dB', snap: false });
B('bypass', 'BYPASS');
P('macro', 'MEGA MACRO', 0, 1, 0, { snap: false });
// 01 glitch buffer
EN('stutDiv', 'DIVISION', DIVS, 4);
B('stutter', 'STUTTER'); B('reverse', 'REVERSE'); B('tapeStop', 'TAPE STOP');
P('tapeTime', 'TAPE TIME', 0.05, 2, 0.6, { unit: 's', log: true });
P('glitchAmt', 'DENSITY', 0, 1, 0);
// 02 GRM spectral & reson
B('freeze', 'FREEZE');
P('freqShift', 'FREQ SHIFT', -500, 500, 0, { unit: 'Hz', bi: true });
P('contrast', 'CONTRAST', 0, 1, 0);
P('resonFreq', 'RESON FREQ', 40, 2000, 220, { unit: 'Hz', log: true });
P('resonTension', 'TENSION', 0, 1, 0);
// 03 dual filter & formant
EN('filtType', 'FILTER A', ['LP', 'BP', 'HP'], 0);
P('filtCut', 'CUTOFF', 20, 20000, 20000, { unit: 'Hz', log: true });
P('filtRes', 'RESO', 0.5, 18, 0.7, { unit: 'Q', log: true });
P('filtDrive', 'DRIVE', 0, 1, 0);
P('vowel', 'VOWEL', 0, 4, 0, { unit: 'vowel' });
P('formantMix', 'FORMANT', 0, 1, 0);
// 04 multi-distortion & bass
EN('distType', 'TYPE', ['TUBE', 'MECH', 'CRUSH', 'XCITE'], 0);
P('distDrive', 'DRIVE', 0, 1, 0);
P('distMix', 'MIX', 0, 1, 1);
P('magicBass', 'MAGIC BASS', 0, 1, 0);
// 05 build-up wash bus
P('hpf', 'HPF', 20, 8000, 20, { unit: 'Hz', log: true });
P('noise', 'NOISE', 0, 1, 0);
P('detune', 'DETUNE', 0, 1, 0);
P('wash', 'WASH', 0, 1, 0);
P('dropDuck', 'DROP DUCK', 0, 1, 0.7);
// 06 master dynamics
P('comp', 'PHAT COMP', 0, 1, 0.15);
P('ceiling', 'CEILING', -12, 0, -0.3, { unit: 'dB' });
B('limiter', 'LIMITER', 1);
// modulation sources
const TGT = MOD_TARGETS.map(id => TARGET_NAMES[id]);
for (const k of [1, 2]) {
  EN(`lfo${k}Rate`, 'RATE', LFO_RATES, k === 1 ? 4 : 2);
  EN(`lfo${k}Shape`, 'SHAPE', LFO_SHAPES, k === 1 ? 0 : 1);
  P(`lfo${k}Depth`, 'DEPTH', -1, 1, 0, { bi: true });
  EN(`lfo${k}Target`, 'TARGET', TGT, k - 1, { snap: false });
}
P('envSens', 'SENS', 0, 1, 0.5);
P('envAtk', 'ATTACK', 1, 200, 10, { unit: 'ms', log: true });
P('envRel', 'RELEASE', 10, 1000, 150, { unit: 'ms', log: true });
P('envDepth', 'DEPTH', -1, 1, 0, { bi: true });
EN('envTarget', 'TARGET', TGT, 2, { snap: false });
// matrix position (automatable — the DSP morphs the snapshots itself)
P('xyX', 'MATRIX X', 0, 1, 0.125, { snap: false });
P('xyY', 'MATRIX Y', 0, 1, 0.125, { snap: false });
// riser FX (05)
P('roll', 'ROLL', 0, 1, 0);
P('jet', 'JET', 0, 1, 0);
P('spin', 'SPIN', 0, 1, 0);
// RISE: the macro rides itself 0 → 100% over N bars, Endless Smile style
B('riseOn', 'RISE');
EN('riseBars', 'BARS', ['1', '2', '4', '8', '16', '32'], 3, { snap: false });
B('autoDrop', 'AUTO DROP', 1);
// BUILD: what the build is made of — all from the incoming sound (NOISE CLASSIC is the old white-noise riser)
const BUILD_MODES = ['WASH OUT', 'PITCH UP', 'ROLL RUSH', 'RESON DRONE', 'JET SWEEP', 'NOISE CLASSIC', 'RANDOM'];
EN('buildMode', 'BUILD', BUILD_MODES, 0, { snap: false });
P('pitch', 'PITCH', -24, 48, 0, { unit: 'st' });
const RISE_BARS = [1, 2, 4, 8, 16, 32];
// rack: each module can be switched off (its effects go neutral, matrix/macro pushes included)
for (let k = 1; k <= 6; k++) B(`mod${k}On`, `MODULE ${k}`, 1);
const FX_ORDERS = ['GLITCH › FILTER › DIST', 'GLITCH › DIST › FILTER', 'FILTER › DIST › GLITCH', 'DIST › FILTER › GLITCH'];
EN('fxOrder', 'FX ORDER', FX_ORDERS, 0, { snap: false });
// RISE PITCH: how far any build pitches your sound up (on the x^1.8 curve), up to +4 octaves
P('risePitch', 'RISE PITCH', 0, 48, 0, { unit: 'st', snap: false });
// TAIL: what the big reverb is when the build is fully open (mirrors TAILS in Params.h; the browser preview plays them all as HALL)
const TAIL_TYPES = ['HALL', 'PLATE', 'CATHEDRAL', 'SHIMMER', 'SKY SHIMMER', 'INFINITE', 'ECHO WASH', 'REVERSE SWELL', 'ROOM'];
EN('tailType', 'REVERB TYPE', TAIL_TYPES, 0, { snap: false });
// ACELERA: what else the build drives — tempo pump (accelerating 1/4 → 1/32), width to 200%, drive, and the ride curve (mirrors Params.h)
P('accPump', 'PUMP', 0, 1, 0, { snap: false });
B('accAccel', 'ACCEL', 1);
P('accWidth', 'WIDTH', 0, 1, 0, { snap: false });
P('accDrive', 'DRIVE', 0, 1, 0, { snap: false });
const ACC_CURVES = ['LINEAR', 'SMOOTH', 'LATE', 'EARLY'];
EN('accCurve', 'CURVE', ACC_CURVES, 0, { snap: false });
// ENDLESS: Shepard climb from the input (plugin only — the browser preview doesn't play it); HIGHER: the HPF opens all the way up
P('accEndless', 'ENDLESS', 0, 1, 0, { snap: false });
B('accHigh', 'HIGHER');
// SMILE slots: effects dragged onto the big knob (mirrors SMILE_PARAM in Params.h). def = the amount a fresh drag gets.
// Any slot in use = the knob drives only the dragged effects. PUMP / WIDTH / ENDLESS add to the ACELERA amounts.
const SMILE_FX = [
  { name: '--' },
  { name: 'FILTER', id: 'filtCut', def: -0.75, col: '#ff8a3d', tip: 'low-pass closes down' },
  { name: 'HPF', id: 'hpf', def: 0.85, col: '#ffd23d', tip: 'lows thin out' },
  { name: 'REVERB', id: 'wash', def: 0.95, col: '#5ad1ff', tip: 'reverb opens' },
  { name: 'PITCH', id: 'pitch', def: 0.67, col: '#ff4fd8', tip: 'up to +48 st, tempo stays' },
  { name: 'DRIVE', id: 'distDrive', def: 0.6, col: '#ff3d2e', tip: 'saturation' },
  { name: 'ROLL', id: 'roll', def: 0.9, col: '#b4ff3d', tip: 'gate speeds up' },
  { name: 'JET', id: 'jet', def: 0.8, col: '#3dffc8', tip: 'flanger sweep' },
  { name: 'SPIN', id: 'spin', def: 0.6, col: '#8f7bff', tip: 'auto-pan speeds up' },
  { name: 'DETUNE', id: 'detune', def: 0.6, col: '#7bd1ff', tip: 'chorus wash' },
  { name: 'RESON', id: 'resonTension', def: 0.8, col: '#ffb03d', tip: 'tuned resonance' },
  { name: 'SHIFT', id: 'freqShift', def: 0.1, col: '#ff7b9c', tip: 'frequency shift up' },
  { name: 'GLITCH', id: 'glitchAmt', def: 0.8, col: '#e0e0e0', tip: 'stutters + chops' },
  { name: 'NOISE', id: 'noise', def: 0.7, col: '#c0c0c0', tip: 'noise riser' },
  { name: 'PUMP', acc: 'accPump', def: 1, col: '#ff5a5a', tip: 'tempo pump, accelerates' },
  { name: 'WIDTH', acc: 'accWidth', def: 1, col: '#5affa0', tip: 'stereo opens to 200%' },
  { name: 'ENDLESS', acc: 'accEndless', def: 1, col: '#fff05a', tip: 'Shepard climb, never stops rising' },
];
const SMILE_SLOTS = 8;
for (let k = 1; k <= SMILE_SLOTS; k++) EN(`smile${k}T`, `SLOT ${k}`, SMILE_FX.map(f => f.name), 0, { snap: false });
for (let k = 1; k <= SMILE_SLOTS; k++) P(`smile${k}A`, `AMOUNT ${k}`, -1, 1, 0, { snap: false, bi: true });
const accShape = (k, x) => k === 1 ? smoothstep(0, 1, x) : k === 2 ? x * x : k === 3 ? Math.sqrt(x) : x;
// what a module's effects fall back to while it is OFF (mirrors MODULE_NEUTRAL in Params.h)
const MODULE_NEUTRAL = [
  { stutter: 0, reverse: 0, tapeStop: 0, glitchAmt: 0 },
  { freeze: 0, freqShift: 0, contrast: 0, resonTension: 0, pitch: 0 },
  { filtType: 0, filtCut: 20000, filtRes: 0.7, filtDrive: 0, formantMix: 0 },
  { distDrive: 0, magicBass: 0 },
  { hpf: 20, noise: 0, detune: 0, wash: 0, roll: 0, jet: 0, spin: 0 },
  { comp: 0, limiter: 0 },
];
const NEUTRAL_OF = {}; // paramId -> [module index, neutral value]
MODULE_NEUTRAL.forEach((o, k) => { for (const id in o) NEUTRAL_OF[id] = [k, o[id]]; });

const PM = {};
PARAMS.forEach(p => PM[p.id] = p);

const clamp = (v, a = 0, b = 1) => Math.min(b, Math.max(a, v));
const norm = (p, v) => p.log ? Math.log(v / p.min) / Math.log(p.max / p.min) : (v - p.min) / (p.max - p.min);
function denorm(p, n) {
  n = clamp(n);
  const v = p.log ? p.min * Math.pow(p.max / p.min, n) : p.min + n * (p.max - p.min);
  return p.type === 'float' ? v : Math.round(v);
}
const defaultsNorm = () => { const o = {}; PARAMS.forEach(p => { if (p.snap) o[p.id] = norm(p, p.def); }); return o; };

/* Mega-Macro: five non-linear build-up curves, x = macro 0..1 */
const smoothstep = (a, b, x) => { const t = clamp((x - a) / (b - a)); return t * t * (3 - 2 * t); };
const CURVES = [
  { key: 'hpf', name: 'HPF', f: x => x < 0.2 ? 0 : (Math.exp(4.5 * (x - 0.2) / 0.8) - 1) / (Math.exp(4.5) - 1) }, // late start, surges >70%
  { key: 'wash', name: 'WASH', f: x => Math.pow(x, 1.8) },
  { key: 'noise', name: 'NOISE', f: x => Math.pow(x, 2.7) },
  { key: 'reson', name: 'RESON', f: x => x <= 0.85 ? smoothstep(0.1, 0.85, x) : 1 - 0.18 * (x - 0.85) / 0.15 }, // peaks at 85%, tamed top
  { key: 'glitch', name: 'GLITCH', f: x => x < 0.55 ? 0 : Math.pow((x - 0.55) / 0.45, 2) },
];
/* BUILD modes — what each curve drives (mirrors buildRoutes() in Params.h). Every mode also gets
   wash += c1*.95, comp += c4*.6, and SWALLOW = c1*k: the dry bus sinks so the build ends in the reverb of your own sound.
   labels = what the five gauges show for that mode. */
const BUILD_MAPS = [
  { labels: ['HPF', 'WASH', 'DETUNE', 'RESON', 'SQUEEZE'], r: [[0, 'hpf', 0.85], [1, 'spin', 0.3], [2, 'detune', 0.45], [3, 'resonTension', 0.25]] },
  { labels: ['HPF', 'PITCH', 'DETUNE', 'JET', 'SQUEEZE'], r: [[0, 'hpf', 0.5], [1, 'pitch', 12 / 72], [2, 'detune', 0.3], [3, 'jet', 0.25]] },
  { labels: ['HPF', 'WASH', 'ROLL', 'SPIN', 'GLITCH'], r: [[0, 'hpf', 0.6], [2, 'roll', 0.95], [3, 'spin', 0.3], [4, 'glitchAmt', 0.8]] },
  { labels: ['HPF', 'DETUNE', 'CONTRAST', 'RESON', 'SQUEEZE'], r: [[0, 'hpf', 0.4], [1, 'detune', 0.6], [2, 'contrast', 0.3], [3, 'resonTension', 0.85]] },
  { labels: ['HPF', 'SPIN', 'LIFT', 'JET', 'SQUEEZE'], r: [[0, 'hpf', 0.6], [1, 'spin', 0.6], [2, 'freqShift', 0.06], [3, 'jet', 0.8]] },
  { labels: ['HPF', 'WASH', 'NOISE', 'RESON', 'GLITCH'], r: [[0, 'hpf', 0.85], [1, 'spin', 0.6], [2, 'noise', 0.8], [2, 'detune', 0.7], [2, 'freqShift', 0.08],
    [2, 'roll', 0.95], [3, 'resonTension', 0.75], [3, 'jet', 0.5], [4, 'glitchAmt', 1]] },
];
const BUILD_RANDOM = 6, BUILD_NOISE = 5;
const buildSwallow = mode => mode === BUILD_NOISE ? 0.4 : 0.75;

function mulberry32(a) { return () => { a |= 0; a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }

// A characterful random patch, sparse { id: norm } — only the stages it lights up.
function randomPatch(rng, intensity = 1) {
  const v = {}, r = (a, b) => a + rng() * (b - a) * intensity, pick = (id, a, b) => v[id] = r(a, b);
  const en = (id) => v[id] = norm(PM[id], Math.floor(rng() * PM[id].options.length));
  if (rng() < 0.6) { v.filtCut = 1 - r(0.08, 0.6); pick('filtRes', 0, 0.7); en('filtType'); }
  if (rng() < 0.35) { pick('formantMix', 0.3, 1); v.vowel = rng(); }
  if (rng() < 0.45) { pick('distDrive', 0.1, 0.75); en('distType'); }
  if (rng() < 0.4) { pick('resonTension', 0.2, 0.75); v.resonFreq = 0.1 + rng() * 0.6; }
  if (rng() < 0.25) v.freqShift = 0.5 + (rng() - 0.5) * 0.3 * intensity;
  if (rng() < 0.4) pick('wash', 0.1, 0.55);
  if (rng() < 0.3) pick('glitchAmt', 0.15, 0.6);
  if (rng() < 0.25) pick('contrast', 0.2, 0.8);
  if (rng() < 0.25) pick('roll', 0.2, 0.8);
  if (rng() < 0.25) pick('jet', 0.2, 0.8);
  if (rng() < 0.25) pick('spin', 0.2, 0.8);
  if (rng() < 0.2) v.pitch = norm(PM.pitch, [-12, -5, 7, 12][Math.floor(rng() * 4)]);
  if (rng() < 0.4) { v.lfo1Depth = 0.5 + r(0.1, 0.4); en('lfo1Rate'); en('lfo1Shape'); }
  return v;
}

const lfoShape = (s, ph, rnd) => {
  switch (s) {
    case 0: return Math.sin(ph * 2 * Math.PI);
    case 1: return 1 - 4 * Math.abs((ph + 0.25) % 1 - 0.5);
    case 2: return 2 * ph - 1;
    case 3: return ph < 0.5 ? 1 : -1;
    default: return rnd;
  }
};

class SkatomState {
  constructor() {
    this.base = {};
    PARAMS.forEach(p => this.base[p.id] = p.def);
    this.macroOff = {};  // normalized offsets from the Mega-Macro, rebuilt every frame
    this.modOff = {};    // normalized offsets from LFO 1/2 + envelope follower, rebuilt every frame
    this.matOff = {};    // normalized pull of the matrix snapshots, rebuilt every frame
    this.matEnum = {};   // enum values switched by the dominant snapshot
    this.curveVals = CURVES.map(() => 0);
    // snapshots are SPARSE: v = { paramId: normalized value } for the effects that snapshot pushes
    this.snaps = Array.from({ length: 16 }, (_, i) => ({ x: (i % 4 + 0.5) / 4, y: (Math.floor(i / 4) + 0.5) / 4, v: {} }));
    this.weights = new Array(16).fill(0);
    this.snapVer = 0; this.macroEff = 0; this.riseProg = 0; this.riseDone = false; this.building = false; this.activeBuild = 0; this.swallow = 0;
    this._sh = [0, 0, 0]; this._shN = [-1, -1, -1];
    this.initSnapshots(7);
    this.updateMatrix();
  }
  get(id) { return this.base[id]; }
  norm(id) { return norm(PM[id], this.base[id]); }
  set(id, v) { const p = PM[id]; v = clamp(v, p.min, p.max); this.put(id, p.type === 'float' ? v : Math.round(v)); }
  setNorm(id, n) { this.put(id, denorm(PM[id], n)); }
  put(id, v) { if (this.base[id] === v) return; this.base[id] = v; if (this.onSet) this.onSet(id); } // onSet = host bridge hook
  get xy() { return { x: this.base.xyX, y: this.base.xyY }; }
  setXY(x, y) { this.set('xyX', x); this.set('xyY', y); }
  offset(id) { return (this.matOff[id] || 0) + (this.macroOff[id] || 0) + (this.modOff[id] || 0); }
  // effective value = knob + matrix + macro + modulation (what the DSP actually receives)
  eff(id) {
    const p = PM[id], nz = NEUTRAL_OF[id];
    if (nz && !this.base[`mod${nz[0] + 1}On`]) return nz[1];
    if (p.type !== 'float') return this.matEnum[id] !== undefined ? this.matEnum[id] : this.base[id];
    const o = this.offset(id);
    return o ? denorm(p, norm(p, this.base[id]) + o) : this.base[id];
  }
  // knob + matrix only (no macro / LFO): what the matrix is "pushing" right now
  pushed(id) {
    const p = PM[id];
    if (p.type !== 'float') return this.eff(id);
    return denorm(p, norm(p, this.base[id]) + (this.matOff[id] || 0));
  }

  // hostBuild: the plugin reports which mode its DSP picked (RANDOM); the browser preview picks itself
  updateMacro(x = this.base.macro, hostBuild) {
    this.macroEff = x;
    const bm = this.base.buildMode;
    if (x > 0.02 && !this.building) { this.building = true; if (bm === BUILD_RANDOM) this.activeBuild = Math.floor(Math.random() * 5); }
    else if (x < 0.01) this.building = false;
    if (bm !== BUILD_RANDOM) this.activeBuild = bm;
    else if (hostBuild !== undefined) this.activeBuild = hostBuild;
    else if (this.activeBuild === BUILD_NOISE) this.activeBuild = Math.floor(Math.random() * 5);
    const add = (id, v) => this.macroOff[id] = (this.macroOff[id] || 0) + v;
    this.macroOff = {};
    const xs = accShape(this.base.accCurve, x); // ACELERA CURVE: how the ride is shaped
    CURVES.forEach((c, i) => this.curveVals[i] = c.f(xs));
    const cv = this.curveVals;
    const acc = { accPump: this.base.accPump, accWidth: this.base.accWidth, accEndless: this.base.accEndless };
    let anySlot = false;
    for (let k = 1; k <= SMILE_SLOTS; k++) {
      const f = SMILE_FX[this.base[`smile${k}T`]], a = this.base[`smile${k}A`];
      if (!f || !f.name || f.name === '--') continue;
      anySlot = true;
      if (f.acc) acc[f.acc] += a; else add(f.id, xs * a);
    }
    this.acc = acc; this.anySlot = anySlot;
    add('pitch', cv[1] * this.base.risePitch / 72);
    add('distDrive', cv[1] * this.base.accDrive);
    if (this.base.accHigh) add('hpf', cv[0] * 0.45);
    if (!anySlot) {
      add('wash', cv[1] * 0.95); add('comp', cv[4] * 0.6);
      for (const [ci, id, k] of BUILD_MAPS[this.activeBuild].r) add(id, cv[ci] * k);
    }
    this.swallow = this.base.mod5On && !anySlot ? cv[1] * buildSwallow(this.activeBuild) : 0; // the swallow is the 05 reverb
  }
  // browser preview only (the plugin runs this in C++): returns 'drop' when an auto-drop rise completes
  stepRise(dBeats) {
    if (!this.base.riseOn) { this.riseProg = 0; this.riseDone = false; return null; }
    if (this.riseDone) return null;
    this.riseProg += dBeats / (RISE_BARS[this.base.riseBars] * 4);
    if (this.riseProg < 1) return null;
    this.riseDone = true;
    if (this.base.autoDrop) { this.riseProg = 0; return 'drop'; }
    this.riseProg = 1; return null;
  }

  updateMod(beats, env) {
    this.modOff = {};
    const out = { env };
    const add = (tgt, v) => { const id = MOD_TARGETS[tgt]; this.modOff[id] = (this.modOff[id] || 0) + v; };
    for (const k of [1, 2]) {
      const cyc = beats / LFO_BEATS[this.eff(`lfo${k}Rate`)], n = Math.floor(cyc), ph = cyc - n;
      if (n !== this._shN[k]) { this._shN[k] = n; this._sh[k] = Math.random() * 2 - 1; }
      const v = lfoShape(this.eff(`lfo${k}Shape`), ph, this._sh[k]);
      out['lfo' + k] = v; out['lfo' + k + 'Ph'] = ph;
      add(this.base[`lfo${k}Target`], v * this.base[`lfo${k}Depth`] * 0.5);
    }
    add(this.base.envTarget, env * this.base.envDepth);
    return out;
  }

  /* ---- 16 snapshots, Inverse Distance Weighting: w_i = 1 / (d_i + eps)^p ---- */
  computeWeights(x, y, p = 3, eps = 1e-3) {
    let sum = 0;
    this.snaps.forEach((s, i) => { sum += this.weights[i] = 1 / Math.pow(Math.hypot(s.x - x, s.y - y) + eps, p); });
    for (let i = 0; i < 16; i++) this.weights[i] /= sum;
    return this.weights;
  }
  // same maths as SkatomProcessor::control: un-pushed snapshots contribute the knob's own ("home") value
  updateMatrix() {
    const w = this.computeWeights(this.base.xyX, this.base.xyY);
    let best = 0;
    w.forEach((wi, i) => { if (wi > w[best]) best = i; });
    this.matOff = {}; this.matEnum = {};
    for (const p of PARAMS) {
      if (!p.snap) continue;
      let any = false;
      for (const s of this.snaps) if (s.v[p.id] !== undefined) { any = true; break; }
      if (!any) continue;
      if (p.type === 'float') {
        const home = norm(p, this.base[p.id]);
        let sum = 0;
        for (let i = 0; i < 16; i++) { const v = this.snaps[i].v[p.id]; sum += w[i] * (v !== undefined ? v : home); }
        this.matOff[p.id] = sum - home;
      } else if (this.snaps[best].v[p.id] !== undefined) this.matEnum[p.id] = denorm(p, this.snaps[best].v[p.id]);
    }
  }
  // Shift+click: capture every knob that differs from INIT into snapshot i
  saveSnap(i) {
    const o = {};
    for (const p of PARAMS) if (p.snap && Math.abs(this.norm(p.id) - norm(p, p.def)) > 0.001) o[p.id] = this.norm(p.id);
    this.snaps[i].v = o; this.snapVer++;
  }
  // drag-to-matrix: snapshot i now pushes this effect at the knob's current value
  pushSnap(i, id) { this.snaps[i].v[id] = this.norm(id); this.snapVer++; }
  setPush(i, id, n) { this.snaps[i].v[id] = clamp(n); this.snapVer++; }   // LEARN / snapshot editor
  unpush(i, id) { delete this.snaps[i].v[id]; this.snapVer++; }
  setSnapV(i, v) { this.snaps[i].v = { ...v }; this.snapVer++; }          // paste
  clearSnap(i) { this.snaps[i].v = {}; this.snapVer++; }
  clearAll() { this.snaps.forEach(s => s.v = {}); this.snapVer++; }
  setSnaps(list) { this.snaps.forEach((s, i) => s.v = { ...(list[i] || {}) }); this.snapVer++; }
  // effects a snapshot pushes (or, without v, what knob + matrix push right now), strongest first
  pushes(v) {
    const out = [];
    for (const p of PARAMS) {
      if (!p.snap) continue;
      if (v) { if (v[p.id] !== undefined) out.push([p.id, Math.abs(v[p.id] - norm(p, p.def)) + 1]); continue; }
      if (p.type === 'enum' && p.id !== 'filtType' && p.id !== 'distType') continue;
      const d = Math.abs(norm(p, this.pushed(p.id)) - norm(p, p.def));
      if (d > 0.02) out.push([p.id, d]);
    }
    return out.sort((a, b) => b[1] - a[1]).map(x => x[0]);
  }
  // current knobs + matrix as a preset (same shape as PRESETS: real units)
  toPreset(name) {
    const p = {}, m = {};
    for (const q of PARAMS) if (q.id !== 'output' && q.id !== 'bypass') p[q.id] = this.base[q.id];
    this.snaps.forEach((s, i) => { const o = {}; for (const id in s.v) o[id] = denorm(PM[id], s.v[id]); if (Object.keys(o).length) m[i] = o; });
    return { cat: 'USER', name, p, m };
  }
  initSnapshots(seed) { const rng = mulberry32(seed); this.snaps.forEach((s, i) => s.v = i === 0 ? {} : randomPatch(rng)); this.snapVer++; }
  // RANDOM: a fresh sound on the knobs plus a fresh matrix around it
  randomize(seed) {
    const rng = mulberry32(seed);
    for (const p of PARAMS) if (p.snap) this.set(p.id, p.def);
    const home = randomPatch(rng, 0.6);
    for (const id in home) this.setNorm(id, home[id]);
    this.initSnapshots((rng() * 1e9) | 0);
  }
}

SkatomState.selfTest = () => {
  const s = new SkatomState(), a = console.assert;
  const w = s.computeWeights(s.snaps[5].x, s.snaps[5].y);
  a(Math.abs(w.reduce((x, y) => x + y) - 1) < 1e-9, 'IDW weights must sum to 1');
  a(w[5] > 0.99, 'IDW on a snapshot must return that snapshot');
  for (const p of PARAMS) if (p.type === 'float') a(Math.abs(denorm(p, norm(p, p.def)) - p.def) < 1e-6 * Math.max(1, Math.abs(p.def)), 'norm roundtrip ' + p.id);
  for (const c of CURVES) a(c.f(0) === 0 && c.f(1) > 0.8 && c.f(1) <= 1, 'macro curve range ' + c.key);
  s.clearAll(); s.snaps[5].v.filtCut = 0.25;
  s.setXY(s.snaps[5].x, s.snaps[5].y); s.updateMatrix();
  a(Math.abs(norm(PM.filtCut, s.eff('filtCut')) - 0.25) < 0.01, 'matrix pushes CUTOFF at S06');
  s.setXY(s.snaps[0].x, s.snaps[0].y); s.updateMatrix();
  a(s.eff('filtCut') > 19000, 'away from S06 the knob value comes back');
  s.set('riseOn', 1); s.set('riseBars', 0);
  a(s.stepRise(2) === null && Math.abs(s.riseProg - 0.5) < 1e-9 && s.stepRise(2) === 'drop', 'rise: 1 bar then auto drop');
};
