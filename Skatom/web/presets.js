'use strict';
/* SKATOM factory presets. p = knob values in real units (anything missing = INIT),
   m = matrix snapshots { index 0..15: { paramId: real value } } — what each snapshot pushes.
   RISE presets arm nothing by themselves: press RISE (or ride the MEGA-MACRO) and they build.
   BUILD (buildMode): 0 WASH OUT, 1 PITCH UP, 2 ROLL RUSH, 3 RESON DRONE, 4 JET SWEEP, 5 NOISE CLASSIC, 6 RANDOM —
   every build is made from the incoming sound and ends swallowed by its own reverb. */
const RISE_DEFAULTS = { dropDuck: 0.85, comp: 0.25, autoDrop: 1 };
const PRESETS = [
  { cat: 'INIT', name: 'INIT', p: {}, m: {} },

  // ---- uplifters / risers: tuned for the MEGA-MACRO + RISE ----
  { cat: 'RISE', name: 'Wash Out 8 Bar Uplifter', p: { ...RISE_DEFAULTS, buildMode: 0, riseBars: 3, wash: 0.12 },
    m: { 3: { wash: 0.55, spin: 0.35 }, 12: { filtCut: 900, filtRes: 4 }, 15: { roll: 0.5, wash: 0.7 } } },
  { cat: 'RISE', name: '16 Bar Pitch Lift', p: { ...RISE_DEFAULTS, buildMode: 1, riseBars: 4, detune: 0.2, wash: 0.18 },
    m: { 5: { detune: 0.6, wash: 0.5 }, 10: { jet: 0.4 }, 15: { pitch: 7, spin: 0.5 } } },
  { cat: 'RISE', name: 'Snare Roll Rush', p: { ...RISE_DEFAULTS, buildMode: 2, riseBars: 3, roll: 0.3, spin: 0.15 },
    m: { 3: { roll: 0.85 }, 12: { roll: 0.6, filtType: 2, filtCut: 600 }, 15: { roll: 1, wash: 0.6 } } },
  { cat: 'RISE', name: 'Jet Takeoff', p: { ...RISE_DEFAULTS, buildMode: 4, riseBars: 3, jet: 0.35, freqShift: 25, wash: 0.2 },
    m: { 3: { jet: 0.9, freqShift: 120 }, 15: { jet: 1, pitch: 12 } } },
  { cat: 'RISE', name: 'Cathedral Wash', p: { ...RISE_DEFAULTS, buildMode: 0, riseBars: 4, wash: 0.45, detune: 0.3, dropDuck: 1 },
    m: { 5: { wash: 0.9, filtType: 2, filtCut: 300 }, 15: { wash: 1, detune: 0.8, spin: 0.4 } } },
  { cat: 'RISE', name: '4 Bar Quick Pitch Lift', p: { ...RISE_DEFAULTS, buildMode: 1, riseBars: 2, roll: 0.2 },
    m: { 15: { roll: 0.9, pitch: 12 } } },
  { cat: 'RISE', name: 'Psy Tension Reson Drone', p: { ...RISE_DEFAULTS, buildMode: 3, riseBars: 3, resonTension: 0.3, resonFreq: 110, lfo1Target: 2, lfo1Depth: 0.4, lfo1Rate: 5 },
    m: { 6: { resonTension: 0.8, resonFreq: 220 }, 9: { resonFreq: 55, resonTension: 0.7 }, 15: { resonTension: 0.9, roll: 0.6 } } },
  { cat: 'RISE', name: 'Hold At The Top (No Drop)', p: { ...RISE_DEFAULTS, buildMode: 0, autoDrop: 0, riseBars: 3, wash: 0.3 },
    m: { 15: { wash: 0.8, spin: 0.6 } } },
  { cat: 'RISE', name: 'Glitch Storm Build', p: { ...RISE_DEFAULTS, buildMode: 2, riseBars: 3, glitchAmt: 0.2, stutDiv: 5 },
    m: { 3: { glitchAmt: 0.6 }, 15: { glitchAmt: 0.9, roll: 0.7 } } },
  // ---- ACELERA: pump that accelerates, endless Shepard climb, width, drive ----
  { cat: 'ACELERA', name: 'Endless Smile Pump 8 Bar', p: { ...RISE_DEFAULTS, buildMode: 0, riseBars: 3, accPump: 0.8, accWidth: 0.6, accDrive: 0.25, accCurve: 1, wash: 0.1 }, m: {} },
  { cat: 'ACELERA', name: 'Endless Uplifter To The Sky', p: { ...RISE_DEFAULTS, buildMode: 1, riseBars: 3, accEndless: 0.8, accHigh: 1, accWidth: 0.7, risePitch: 12, tailType: 4, wash: 0.15 }, m: {} },
  { cat: 'ACELERA', name: 'Infinite Climb 16 Bar', p: { ...RISE_DEFAULTS, buildMode: 0, riseBars: 4, accEndless: 1, accHigh: 1, accPump: 0.5, accWidth: 0.8, tailType: 3, wash: 0.2 }, m: {} },
  { cat: 'ACELERA', name: 'Hard Pump Rush 4 Bar', p: { ...RISE_DEFAULTS, buildMode: 2, riseBars: 2, accPump: 1, accDrive: 0.5, accCurve: 2, accHigh: 1 }, m: {} },
  { cat: 'RISE', name: 'Every Build Different (Random)', p: { ...RISE_DEFAULTS, buildMode: 6, riseBars: 3, spin: 0.2, wash: 0.2 },
    m: { 15: { spin: 1, wash: 0.7, detune: 0.5 } } },
  { cat: 'RISE', name: '+48 Sky Rocket', p: { ...RISE_DEFAULTS, buildMode: 1, riseBars: 3, risePitch: 36, tailType: 4, wash: 0.15 },
    m: { 15: { spin: 0.6, jet: 0.5 } } },
  { cat: 'RISE', name: 'Shimmer Heaven 16 Bar', p: { ...RISE_DEFAULTS, buildMode: 0, riseBars: 4, risePitch: 12, tailType: 3, wash: 0.2, detune: 0.2 },
    m: { 15: { wash: 0.8, detune: 0.6 } } },
  { cat: 'RISE', name: 'Reverse Swell Into Drop', p: { ...RISE_DEFAULTS, buildMode: 0, riseBars: 2, risePitch: 7, tailType: 7, wash: 0.25 },
    m: { 15: { wash: 0.9 } } },
  { cat: 'RISE', name: 'Infinite Cathedral Hold', p: { ...RISE_DEFAULTS, buildMode: 3, autoDrop: 0, riseBars: 4, risePitch: 5, tailType: 5, wash: 0.3 },
    m: { 15: { wash: 1, resonTension: 0.6 } } },
  { cat: 'RISE', name: 'Echo Wash Pitch Climb', p: { ...RISE_DEFAULTS, buildMode: 1, riseBars: 3, risePitch: 24, tailType: 6, wash: 0.2 },
    m: { 15: { spin: 0.4 } } },
  { cat: 'RISE', name: 'Cathedral Plate 32 Bar Epic', p: { ...RISE_DEFAULTS, buildMode: 0, riseBars: 5, risePitch: 19, tailType: 2, wash: 0.25, detune: 0.3 },
    m: { 15: { wash: 1, detune: 0.7, spin: 0.3 } } },
  { cat: 'RISE', name: 'Old School Noise Riser', p: { ...RISE_DEFAULTS, buildMode: 5, riseBars: 3 }, m: {} },

  // ---- filters ----
  { cat: 'FILTER', name: 'Acid LP Sweep', p: { filtCut: 800, filtRes: 8, filtDrive: 0.35, lfo1Target: 0, lfo1Depth: 0.6, lfo1Rate: 3, lfo1Shape: 2 },
    m: { 3: { filtCut: 3000, filtRes: 12 }, 12: { filtCut: 250, distDrive: 0.4 } } },
  { cat: 'FILTER', name: 'Vowel Talker', p: { formantMix: 1, lfo2Target: 1, lfo2Depth: 0.9, lfo2Rate: 2, lfo2Shape: 1 },
    m: { 3: { vowel: 4 }, 12: { vowel: 1, distDrive: 0.3 } } },
  { cat: 'FILTER', name: 'Telephone Band', p: { filtType: 1, filtCut: 1500, filtRes: 3, distType: 2, distDrive: 0.3 },
    m: { 15: { filtCut: 3500, distDrive: 0.6 } } },
  { cat: 'FILTER', name: 'HP Thin Out', p: { filtType: 2, filtCut: 400, filtRes: 1.5 },
    m: { 3: { filtCut: 1500 }, 15: { filtCut: 4000, wash: 0.4 } } },

  // ---- glitch ----
  { cat: 'GLITCH', name: 'Stutter Gate 1/16', p: { glitchAmt: 0.45, stutDiv: 4 },
    m: { 3: { glitchAmt: 0.8 }, 15: { glitchAmt: 1, freqShift: -60 } } },
  { cat: 'GLITCH', name: 'Tape Machine', p: { tapeTime: 0.9, distDrive: 0.15, magicBass: 0.3 },
    m: { 5: { tapeTime: 0.3 }, 15: { tapeTime: 1.8 } } },
  { cat: 'GLITCH', name: 'Granular Freeze Pad', p: { wash: 0.5, detune: 0.4, contrast: 0.2 },
    m: { 15: { wash: 0.9, spin: 0.4 } } },
  { cat: 'GLITCH', name: 'Shifted Chaos', p: { glitchAmt: 0.3, freqShift: -80, lfo1Target: 4, lfo1Depth: 0.5, lfo1Shape: 4, lfo1Rate: 5 },
    m: { 15: { freqShift: 250, glitchAmt: 0.7 } } },

  // ---- space / dirt / movement ----
  { cat: 'SPACE', name: 'Wide Detune Wash', p: { detune: 0.6, wash: 0.35, spin: 0.15 }, m: { 15: { wash: 0.8, detune: 0.9 } } },
  { cat: 'SPACE', name: 'Shimmer Shift', p: { freqShift: 12, wash: 0.5, resonTension: 0.2, resonFreq: 440 }, m: { 15: { freqShift: 40, wash: 0.9 } } },
  { cat: 'DIRT', name: 'Tube Warmth', p: { distDrive: 0.3, magicBass: 0.4, comp: 0.35 }, m: { 15: { distDrive: 0.7 } } },
  { cat: 'DIRT', name: 'Mech Fold Bass', p: { distType: 1, distDrive: 0.5, magicBass: 0.6, filtCut: 6000 }, m: { 15: { distDrive: 0.9, magicBass: 1 } } },
  { cat: 'DIRT', name: 'Bitcrush 8-Bit', p: { distType: 2, distDrive: 0.55 }, m: { 15: { distDrive: 0.85, filtType: 1, filtCut: 2000 } } },
  { cat: 'MOVE', name: 'Jet Flanger Groove', p: { jet: 0.5, lfo1Target: 11, lfo1Depth: 0.4, lfo1Rate: 2 }, m: { 15: { jet: 1, spin: 0.5 } } },
  { cat: 'MOVE', name: 'Auto-Pan Spin', p: { spin: 0.4 }, m: { 15: { spin: 1 } } },
  { cat: 'MOVE', name: 'Envelope Wah', p: { formantMix: 0.7, envTarget: 1, envDepth: 0.8, envSens: 0.7 }, m: { 15: { formantMix: 1, distDrive: 0.4 } } },
];

// apply a preset to a SkatomState: knobs (output and bypass stay as they are), matrix, puck back to S01
function applyPreset(S, pr) {
  for (const p of PARAMS) if (p.id !== 'output' && p.id !== 'bypass') S.set(p.id, p.id in pr.p ? pr.p[p.id] : p.def);
  S.setSnaps(Array.from({ length: 16 }, (_, i) => {
    const o = {};
    for (const [id, v] of Object.entries(pr.m[i] || {})) o[id] = norm(PM[id], clamp(v, PM[id].min, PM[id].max));
    return o;
  }));
}
