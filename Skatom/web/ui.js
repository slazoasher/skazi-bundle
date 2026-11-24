'use strict';
/* SKATOM — kinetic UI layer. One rAF loop drives every view; views only read/write SkatomState. */

const S = new SkatomState();
const E = window.__JUCE__ ? new HostEngine(S) : new SkatomEngine(S); // plugin: DSP in C++, browser: Web Audio
SkatomState.selfTest();

const $ = s => document.querySelector(s);
const TAU = Math.PI * 2;
const COL = { amber: '#ffb02e', copper: '#d9773a', crimson: '#ff2d3c', mod: '#4fd1ff', ink: '#c9ccd1', dim: '#6b7078' };
const CURVE_COL = ['#ffb02e', '#d9773a', '#ff2d3c', '#4fd1ff', '#c9d1dc'];
// local fonts only — the plugin must look identical offline
const FONT = "'Cascadia Mono', Consolas, 'SF Mono', Menlo, monospace";
const DISPLAY = "Bahnschrift, 'DIN Alternate', 'Arial Narrow', sans-serif";
const views = [];
let MODV = {}, BEATS = 0;

function mk(tag, cls, parent, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text != null) e.textContent = text;
  if (parent) parent.appendChild(e);
  return e;
}
function hiDPI(cv, w, h) {
  const r = window.devicePixelRatio || 1;
  cv.width = Math.round(w * r); cv.height = Math.round(h * r); cv.style.width = w + 'px'; cv.style.height = h + 'px';
  const g = cv.getContext('2d'); g.setTransform(r, 0, 0, r, 0, 0);
  return g;
}
function layer(w, h, paint) { // pre-rendered static art (drawn once, blitted every frame)
  const cv = document.createElement('canvas'), g = hiDPI(cv, w, h);
  paint(g);
  return cv;
}
function arc(g, x, y, r, a0, a1, col, w) { g.beginPath(); g.arc(x, y, r, a0, a1); g.strokeStyle = col; g.lineWidth = w; g.stroke(); }
function disc(g, x, y, r, fill) { g.beginPath(); g.arc(x, y, r, 0, TAU); g.fillStyle = fill; g.fill(); }
function fmt(id, v) {
  const p = PM[id];
  if (p.options) return p.options[v];
  switch (p.unit) {
    case 'Hz': { const a = Math.abs(v), s = v < -0.5 ? '-' : p.bi && v > 0.5 ? '+' : ''; return s + (a >= 1000 ? (a / 1000).toFixed(a >= 10000 ? 1 : 2) + 'k' : a.toFixed(0)); }
    case 'dB': return (v > 0 ? '+' : '') + v.toFixed(1) + 'dB';
    case 's': return v.toFixed(2) + 's';
    case 'ms': return v.toFixed(0) + 'ms';
    case 'Q': return 'Q ' + v.toFixed(1);
    case 'st': return (v > 0.05 ? '+' : '') + v.toFixed(1) + 'st';
    case 'vowel': { const i = Math.floor(v), f = v - i; return f < 0.15 || i >= 4 ? VOWELS[Math.min(4, Math.round(v))] : f > 0.85 ? VOWELS[i + 1] : VOWELS[i] + '›' + VOWELS[i + 1]; }
  }
  return p.bi ? (v > 0 ? '+' : '') + (v * 100).toFixed(0) : (v * 100).toFixed(0) + '%';
}
const status = msg => $('#status').textContent = msg;
const pad2 = i => String(i + 1).padStart(2, '0');

/* ---------- rotary knob: damped-spring needle, value arc, live modulation / matrix ring ---------- */
class Knob {
  constructor(parent, id, { size = 52, label, hero = false } = {}) {
    this.id = id; this.p = PM[id]; this.size = size; this.hero = hero;
    this.el = mk('div', hero ? 'knob hero' : 'knob', parent);
    this.cv = mk('canvas', null, this.el); this.g = hiDPI(this.cv, size, size);
    this.cv.tabIndex = 0; this.cv.setAttribute('role', 'slider'); this.cv.setAttribute('aria-label', label || this.p.name);
    const lab = mk('div', 'klabel', this.el, label || this.p.name);
    if (this.p.snap) { // matrix-assignable: drag the name, or right-drag the knob, onto the matrix
      lab.classList.add('grab'); lab.title = 'Drag (or right-drag the knob) onto the matrix to push this effect at a snapshot';
      lab.addEventListener('pointerdown', e => Drag.start([id], e));
      this.cv.addEventListener('contextmenu', e => e.preventDefault());
      BUILDING.push(id);
    }
    this.val = mk('div', 'kval', this.el);
    this.pos = S.norm(id); this.vel = 0; this.key = '';
    let y0, n0;
    this.cv.addEventListener('pointerdown', e => {
      if (e.button === 2 && this.p.snap) return Drag.start([id], e);
      if (e.button !== 0) return;
      this.cv.setPointerCapture(e.pointerId); y0 = e.clientY; n0 = knobGet(id); this.drag = true;
      if (!learning(id)) hostGesture(id, true);
    });
    this.cv.addEventListener('pointermove', e => { if (this.drag) knobSet(id, n0 + (y0 - e.clientY) * (e.shiftKey ? 0.0015 : 0.0075)); });
    const end = () => { if (!this.drag) return; this.drag = false; hostGesture(id, false); };
    this.cv.addEventListener('pointerup', end); this.cv.addEventListener('pointercancel', end);
    this.cv.addEventListener('dblclick', () => learning(id) ? S.unpush(pad.sel, id) : S.set(id, this.p.def));
    this.cv.addEventListener('wheel', e => { e.preventDefault(); knobSet(id, knobGet(id) - Math.sign(e.deltaY) * 0.02); }, { passive: false });
    this.cv.addEventListener('keydown', e => { const d = { ArrowUp: 0.02, ArrowRight: 0.02, ArrowDown: -0.02, ArrowLeft: -0.02 }[e.key]; if (d) { e.preventDefault(); knobSet(id, knobGet(id) + d); } });
    views.push(this);
  }
  draw() {
    const id = this.id, target = S.norm(id), eff = clamp(target + S.offset(id));
    if (this.drag) { this.pos = target; this.vel = 0; } // under your hand: no spring lag
    else { this.vel += (target - this.pos) * 0.35; this.vel *= 0.55; this.pos += this.vel; }
    // the text shows what the knob + matrix actually push (cyan when the matrix pulls it)
    const pushed = S.pushed(id), pulled = Math.abs(norm(this.p, pushed) - target) > 0.01, txt = fmt(id, pushed);
    if (txt !== this.txt) { this.val.textContent = this.txt = txt; this.cv.setAttribute('aria-valuetext', txt); }
    if (pulled !== this.pulled) this.val.classList.toggle('pulled', this.pulled = pulled);
    const key = Math.round(this.pos * 4000) + '|' + Math.round(eff * 4000);
    if (key === this.key) return; // nothing moved: skip the redraw
    this.key = key;

    const g = this.g, s = this.size, c = s / 2, R = s / 2 - 3, A0 = Math.PI * 0.75, ang = n => A0 + Math.PI * 1.5 * n, a = ang(this.pos);
    g.clearRect(0, 0, s, s);
    g.lineWidth = 1; g.strokeStyle = '#4a4f56'; g.beginPath();
    for (let i = 0; i <= 10; i++) { const t = ang(i / 10); g.moveTo(c + Math.cos(t) * (R - 2.5), c + Math.sin(t) * (R - 2.5)); g.lineTo(c + Math.cos(t) * R, c + Math.sin(t) * R); }
    g.stroke();
    const wv = this.hero ? 5 : 3;
    arc(g, c, c, R - 6, ang(0), ang(1), '#0c0d0f', wv);
    const from = this.p.bi ? 0.5 : 0;
    if (Math.abs(this.pos - from) > 0.002) arc(g, c, c, R - 6, ang(Math.min(from, this.pos)), ang(Math.max(from, this.pos)), this.hero ? COL.copper : COL.amber, wv);
    if (Math.abs(eff - target) > 0.003) { // live pull: matrix / macro / LFO / ENV
      arc(g, c, c, R - 1.5, ang(Math.min(target, eff)), ang(Math.max(target, eff)), COL.mod, 2);
      disc(g, c + Math.cos(ang(eff)) * (R - 1.5), c + Math.sin(ang(eff)) * (R - 1.5), 2, '#fff');
    }
    const br = R - (this.hero ? 12 : 10), grd = g.createRadialGradient(c - br * 0.35, c - br * 0.45, 1, c, c, br);
    grd.addColorStop(0, '#7a8089'); grd.addColorStop(0.55, '#2d3035'); grd.addColorStop(1, '#131416');
    disc(g, c, c, br, grd);
    g.strokeStyle = 'rgba(0,0,0,.7)'; g.lineWidth = 1; g.beginPath();
    for (let i = 0; i < 20; i++) { const t = a + i * TAU / 20; g.moveTo(c + Math.cos(t) * (br - 2), c + Math.sin(t) * (br - 2)); g.lineTo(c + Math.cos(t) * br, c + Math.sin(t) * br); }
    g.stroke();
    g.strokeStyle = this.hero ? COL.copper : COL.amber; g.lineWidth = this.hero ? 3 : 2; g.lineCap = 'round';
    g.beginPath(); g.moveTo(c + Math.cos(a) * 3, c + Math.sin(a) * 3); g.lineTo(c + Math.cos(a) * (br - 3), c + Math.sin(a) * (br - 3)); g.stroke(); g.lineCap = 'butt';
  }
}

/* ---------- drag a knob name onto the matrix = push that effect at the nearest snapshot ---------- */
const TAGS = { filtCut: 'CUTOFF', filtRes: 'RESO', filtDrive: 'F.DRIVE', filtType: 'F.TYPE', formantMix: 'FORMANT', distDrive: 'DIST', distMix: 'D.MIX',
  distType: 'D.TYPE', magicBass: 'BASS', resonTension: 'TENSION', resonFreq: 'R.FREQ', freqShift: 'SHIFT', glitchAmt: 'GLITCH', tapeTime: 'TAPE',
  lfo1Depth: 'LFO1', lfo2Depth: 'LFO2', envDepth: 'ENV', envSens: 'ENV SENS', envAtk: 'ENV ATK', envRel: 'ENV REL', dropDuck: 'DUCK', comp: 'COMP', ceiling: 'CEIL',
  lfo1Rate: 'LFO1 RATE', lfo2Rate: 'LFO2 RATE', lfo1Shape: 'LFO1 WAVE', lfo2Shape: 'LFO2 WAVE', stutDiv: 'DIV' };
const TAG = id => TAGS[id] || PM[id].name;
const Drag = {
  // ids = one knob, or every effect of a module (drag its title); label = what the ghost says
  start(ids, e, label) {
    e.preventDefault();
    const ghost = mk('div', 'ghost', document.body);
    const name = () => label || `${TAG(ids[0])} ${fmt(ids[0], S.get(ids[0]))}`;
    const move = ev => {
      ghost.textContent = name();
      ghost.style.transform = `translate(${ev.clientX + 14}px, ${ev.clientY + 10}px)`;
      pad.dropTarget = pad.nearest(ev); pad.dropLabel = label || TAG(ids[0]);
    };
    const up = ev => {
      removeEventListener('pointermove', move); removeEventListener('pointerup', up); ghost.remove();
      const i = pad.nearest(ev); pad.dropTarget = -1;
      if (i < 0) return;
      ids.forEach(id => S.pushSnap(i, id)); pad.flash[i] = 1; pad.sel = i; pad.goto(i);
      status(`${name()} → S${pad2(i)}`);
    };
    addEventListener('pointermove', move); addEventListener('pointerup', up); move(e);
  },
};
let BUILDING = []; // matrix-assignable ids created while a rack module is being built
/* LEARN: arm a snapshot, then every knob you turn is written into that snapshot (its home value stays) */
const LEARN = { on: false };
const learning = id => LEARN.on && PM[id].snap;
const knobGet = id => learning(id) && S.snaps[pad.sel].v[id] !== undefined ? S.snaps[pad.sel].v[id] : S.norm(id);
const knobSet = (id, n) => learning(id) ? S.setPush(pad.sel, id, n) : S.setNorm(id, n);
const pickEnum = (id, i) => learning(id) ? S.setPush(pad.sel, id, norm(PM[id], i)) : S.set(id, i);
const hostGesture = (id, on) => E.gesture && E.gesture(id, on);

/* ---------- mechanical toggle, segmented switch, select ---------- */
class Toggle {
  constructor(parent, id, label, cls = '') {
    this.id = id;
    this.el = mk('button', 'toggle ' + cls, parent);
    this.el.innerHTML = `<span class="tlabel">${label}</span><span class="slot"><span class="lever"></span></span>`;
    this.el.onclick = () => S.set(id, 1 - S.get(id));
    views.push(this);
  }
  draw() { const v = !!S.get(this.id); if (v !== this.v) { this.v = v; this.el.classList.toggle('on', v); this.el.setAttribute('aria-pressed', v); } }
}
class Seg {
  constructor(parent, id) {
    this.id = id; this.el = mk('div', 'seg', parent);
    this.btns = PM[id].options.map((o, i) => { const b = mk('button', null, this.el, o); b.onclick = () => pickEnum(id, i); return b; });
    if (PM[id].snap) BUILDING.push(id);
    views.push(this);
  }
  draw() { const v = S.eff(this.id); if (v !== this.v) { this.v = v; this.btns.forEach((b, i) => b.classList.toggle('on', i === v)); } }
}
class Sel {
  constructor(parent, id, label) {
    this.id = id;
    const w = mk('label', 'selw', parent); mk('span', null, w, label || PM[id].name);
    this.el = mk('select', null, w);
    PM[id].options.forEach((o, i) => { mk('option', null, this.el, o).value = i; });
    this.el.onchange = () => pickEnum(id, +this.el.value);
    if (PM[id].snap) BUILDING.push(id);
    views.push(this);
  }
  draw() { const v = learning(this.id) ? S.eff(this.id) : S.get(this.id); if (+this.el.value !== v && document.activeElement !== this.el) this.el.value = v; }
}

/* ---------- reactor core: XY matrix + 16 snapshots ---------- */
class XYPad {
  constructor(parent) {
    const s = this.s = 500, M = this.M = 55, W = this.W = 390;
    this.cv = mk('canvas', null, parent); this.g = hiDPI(this.cv, s, s);
    this.cv.style.touchAction = 'none';
    this.glideTo = null; this.speed = 0; this.vx = S.xy.x; this.vy = S.xy.y;
    this.r1 = 0; this.r2 = 0; this.gy = [0, 0, 0]; this.sp = [0, 1, 2]; this.flash = new Float32Array(16);
    this.dropTarget = -1; this.tagVer = -1; this.sel = 0;
    this.plate = layer(s, s, g => {
      const pg = g.createLinearGradient(0, M, 0, M + W); pg.addColorStop(0, '#14161a'); pg.addColorStop(1, '#0a0b0d');
      g.fillStyle = pg; g.fillRect(M, M, W, W);
      g.strokeStyle = '#000'; g.lineWidth = 2; g.strokeRect(M, M, W, W);
      g.strokeStyle = '#3c4148'; g.lineWidth = 1; g.strokeRect(M - 2.5, M - 2.5, W + 5, W + 5);
      for (let i = 1; i < 16; i++) {
        const a = M + i * W / 16; g.strokeStyle = i % 4 ? 'rgba(255,255,255,.035)' : 'rgba(255,255,255,.085)';
        g.beginPath(); g.moveTo(a, M); g.lineTo(a, M + W); g.moveTo(M, a); g.lineTo(M + W, a); g.stroke();
      }
      for (const [bx, by] of [[M + 8, M + 8], [M + W - 8, M + 8], [M + 8, M + W - 8], [M + W - 8, M + W - 8]]) { disc(g, bx, by, 3.5, '#5d636b'); disc(g, bx, by, 1.5, '#1b1d20'); }
    });
    this.cv.addEventListener('pointerdown', e => {
      const [u, v] = this.toPad(e), hit = S.snaps.findIndex(n => Math.hypot(n.x - u, n.y - v) < 0.04);
      if (hit >= 0 && e.shiftKey) { S.saveSnap(hit); this.flash[hit] = 1; status(`S${pad2(hit)} SAVED`); return; }
      if (hit >= 0 && e.altKey) { S.clearSnap(hit); this.flash[hit] = 1; status(`S${pad2(hit)} CLEARED`); return; }
      if (hit >= 0) { this.sel = hit; return this.goto(hit); }
      this.cv.setPointerCapture(e.pointerId); this.drag = true; this.glideTo = null;
      hostGesture('xyX', true); hostGesture('xyY', true);
      S.setXY(clamp(u), clamp(v)); // the puck is where your finger is — no lag
    });
    this.cv.addEventListener('pointermove', e => { if (!this.drag) return; const [u, v] = this.toPad(e); S.setXY(clamp(u), clamp(v)); });
    const end = () => { if (!this.drag) return; this.drag = false; hostGesture('xyX', false); hostGesture('xyY', false); };
    this.cv.addEventListener('pointerup', end); this.cv.addEventListener('pointercancel', end);
    views.push(this);
  }
  goto(i) { this.glideTo = [S.snaps[i].x, S.snaps[i].y]; this.flash[i] = Math.max(this.flash[i], 0.6); }
  toPad(e) { const r = this.cv.getBoundingClientRect(); return [((e.clientX - r.left) / r.width * this.s - this.M) / this.W, ((e.clientY - r.top) / r.height * this.s - this.M) / this.W]; }
  nearest(e) { // snapshot under a drop, or -1 when the pointer is off the matrix
    const r = this.cv.getBoundingClientRect();
    if (e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom) return -1;
    if (this.editorOpen) return this.sel; // the snapshot editor is open: drops land on the page it shows
    const [u, v] = this.toPad(e);
    let best = 0, bd = Infinity;
    S.snaps.forEach((n, i) => { const d = Math.hypot(n.x - u, n.y - v); if (d < bd) { bd = d; best = i; } });
    return best;
  }
  draw(t, dt) {
    const { g, s, M, W } = this, X = u => M + u * W, Y = v => M + v * W, macro = S.macroEff, c = s / 2;
    if (this.glideTo) { // snapshot click: quick ~150 ms morph glide, written to the automatable XY params
      const k = 1 - Math.pow(0.78, dt * 60), [gx, gy] = this.glideTo, { x, y } = S.xy;
      if (Math.hypot(gx - x, gy - y) < 0.002) { S.setXY(gx, gy); this.glideTo = null; }
      else S.setXY(x + (gx - x) * k, y + (gy - y) * k);
    }
    const kv = 1 - Math.pow(0.45, dt * 60), ox = this.vx, oy = this.vy; // visual smoothing only (host automation arrives at 30 Hz)
    this.vx += (S.xy.x - this.vx) * kv; this.vy += (S.xy.y - this.vy) * kv;
    this.speed += (Math.hypot(this.vx - ox, this.vy - oy) / Math.max(dt, 1e-3) - this.speed) * 0.15;
    const w = S.weights, px = X(this.vx), py = Y(this.vy), spin = 0.25 + macro * 3 + this.speed * 4;
    this.r1 += dt * spin; this.r2 -= dt * spin * 0.6;
    const hot = `255,${(176 - 130 * macro) | 0},46`;
    g.clearRect(0, 0, s, s);

    // turret rings behind the plate
    g.save(); g.translate(c, c);
    arc(g, 0, 0, 232, 0, TAU, '#1a1c20', 13);
    arc(g, 0, 0, 238.5, 0, TAU, '#3a3f46', 1); arc(g, 0, 0, 225.5, 0, TAU, '#000', 1);
    for (const major of [false, true]) {
      g.beginPath();
      for (let i = 0; i < 72; i++) {
        if ((i % 6 === 0) !== major) continue;
        const a = this.r1 + i * TAU / 72, l = major ? 10 : 4;
        g.moveTo(Math.cos(a) * (232 - l / 2), Math.sin(a) * (232 - l / 2)); g.lineTo(Math.cos(a) * (232 + l / 2), Math.sin(a) * (232 + l / 2));
      }
      g.strokeStyle = major ? COL.copper : '#4a5058'; g.lineWidth = major ? 2 : 1; g.stroke();
    }
    for (let i = 0; i < 8; i++) { // counter-rotating latches glow as the macro loads the reactor
      g.save(); g.rotate(this.r2 + i * TAU / 8);
      g.fillStyle = '#2b2f35'; g.fillRect(202, -8, 16, 16);
      g.fillStyle = `rgba(${hot},${0.2 + 0.75 * macro})`; g.fillRect(205, -5, 10, 10);
      g.restore();
    }
    g.restore();
    g.drawImage(this.plate, 0, 0, s, s);

    // reticle + edge markers
    g.strokeStyle = `rgba(${hot},.22)`; g.lineWidth = 1;
    g.beginPath(); g.moveTo(px, M); g.lineTo(px, M + W); g.moveTo(M, py); g.lineTo(M + W, py); g.stroke();
    g.fillStyle = COL.amber;
    g.beginPath(); g.moveTo(px - 5, M - 9); g.lineTo(px + 5, M - 9); g.lineTo(px, M - 3); g.fill();
    g.beginPath(); g.moveTo(M - 9, py - 5); g.lineTo(M - 9, py + 5); g.lineTo(M - 3, py); g.fill();

    // tension lines to the snapshots that carry weight
    g.save(); g.setLineDash([6, 5]); g.lineDashOffset = -t * 40;
    S.snaps.forEach((n, i) => {
      if (w[i] < 0.02) return;
      g.strokeStyle = `rgba(${hot},${Math.min(1, 0.15 + w[i] * 1.6)})`; g.lineWidth = 0.6 + w[i] * 4;
      g.beginPath(); g.moveTo(px, py); g.lineTo(X(n.x), Y(n.y)); g.stroke();
    });
    g.restore();

    // snapshot nodes: size + pulse = weight; cyan tags = what each one pushes
    if (this.tagVer !== S.snapVer) { this.tagVer = S.snapVer; this.tags = S.snaps.map(n => S.pushes(n.v)); }
    g.textAlign = 'center';
    S.snaps.forEach((n, i) => {
      const x = X(n.x), y = Y(n.y), wi = w[i], tg = this.tags[i], r = 5 + 13 * wi * (1 + 0.18 * Math.sin(t * 7 + i));
      disc(g, x, y, r, `rgba(${hot},${0.1 + wi * 0.8})`);
      arc(g, x, y, r + 3, 0, TAU, wi > 0.3 ? COL.amber : tg.length ? '#3f7f95' : '#4a5058', 1.2);
      if (this.flash[i] > 0) { arc(g, x, y, r + 6 + (1 - this.flash[i]) * 26, 0, TAU, `rgba(79,209,255,${this.flash[i]})`, 2); this.flash[i] = Math.max(0, this.flash[i] - dt * 1.5); }
      g.font = `10px ${FONT}`; g.fillStyle = wi > 0.3 ? COL.amber : '#7d848d';
      g.fillText('S' + pad2(i), x, y + r + 15);
      if (tg.length) {
        g.font = `8px ${FONT}`; g.fillStyle = wi > 0.3 ? COL.mod : 'rgba(79,209,255,.6)';
        g.fillText(tg.slice(0, 2).map(TAG).join('·') + (tg.length > 2 ? ` +${tg.length - 2}` : ''), x, y + r + 25);
      }
    });
    if (this.dropTarget >= 0) { // drag-to-matrix target
      const n = S.snaps[this.dropTarget], x = X(n.x), y = Y(n.y);
      arc(g, x, y, 24 + 4 * Math.sin(t * 12), 0, TAU, COL.mod, 2.5);
      g.fillStyle = COL.mod; g.font = `11px ${FONT}`;
      g.fillText(`PUSH ${this.dropLabel} → S${pad2(this.dropTarget)}`, x, y - 30);
    }
    { // selected snapshot (LEARN target / editor page)
      const n = S.snaps[this.sel], x = X(n.x), y = Y(n.y);
      g.save(); g.setLineDash([3, 3]); g.strokeStyle = LEARN.on ? COL.crimson : 'rgba(79,209,255,.55)'; g.lineWidth = LEARN.on ? 2 : 1;
      g.strokeRect(x - 21, y - 21, 42, 42); g.restore();
    }

    // gyroscope rings around the puck
    for (let j = 0; j < 3; j++) {
      this.gy[j] += dt * spin * (1.3 + j * 0.7) * (j % 2 ? -1 : 1);
      this.sp[j] += dt * (0.8 + j * 0.5 + this.speed * 6 + macro * 4);
      g.save(); g.translate(px, py); g.rotate(this.gy[j]); g.scale(1, Math.max(0.08, Math.abs(Math.cos(this.sp[j]))));
      g.beginPath(); g.arc(0, 0, 18 + j * 9, 0, TAU); g.restore();
      g.strokeStyle = [COL.amber, COL.copper, '#9aa3ad'][j]; g.lineWidth = j ? 1.2 : 1.8; g.stroke();
    }
    disc(g, px, py, 11, 'rgba(255,140,40,.35)'); disc(g, px, py, 6, COL.amber); disc(g, px, py, 2.5, '#fff');

    // readouts
    let best = 0; w.forEach((v, i) => { if (v > w[best]) best = i; });
    g.font = `11px ${FONT}`; g.fillStyle = COL.amber;
    g.textAlign = 'left'; g.fillText(`X ${S.xy.x.toFixed(3)}  Y ${(1 - S.xy.y).toFixed(3)}`, M, M - 16);
    g.textAlign = 'right'; g.fillText(`S${pad2(best)} ${(w[best] * 100).toFixed(0)}%`, M + W, M - 16);
    const live = S.pushes().slice(0, 3).map(id => `${TAG(id)} ${fmt(id, S.pushed(id))}`);
    g.textAlign = 'left'; g.font = `10px ${FONT}`;
    g.fillText('PUSHING ▸ ' + (live.length ? live.join(' · ') : 'CLEAN / INIT'), M, M + W + 22);
    g.textAlign = 'center'; g.fillStyle = COL.dim; g.font = `9px ${FONT}`;
    if (LEARN.on) { g.fillStyle = COL.crimson; g.fillText(`LEARN ▸ TURN KNOBS TO WRITE THEM INTO S${pad2(this.sel)} · CLICK ANOTHER S## TO SWITCH`, c, M + W + 40); }
    else g.fillText('RIGHT-DRAG A KNOB / DRAG A NAME OR MODULE TITLE ONTO THE MATRIX · CLICK S## = GLIDE · SHIFT = CAPTURE · ALT = CLEAR', c, M + W + 40);
  }
}

/* ---------- Mega-Macro: detonation dial (+ RISE) ---------- */
class MacroDial {
  constructor(parent) {
    const s = this.s = 336;
    this.cv = mk('canvas', null, parent); this.g = hiDPI(this.cv, s, s);
    this.cv.style.touchAction = 'none'; this.cv.style.cursor = 'ns-resize';
    this.cv.tabIndex = 0; this.cv.setAttribute('role', 'slider'); this.cv.setAttribute('aria-label', 'Mega macro');
    this.pos = 0; this.vel = 0; this.lockA = 0; this.lockB = 0; this.flash = 0; this.hi = 0;
    let y0, m0;
    this.cv.addEventListener('pointerdown', e => { this.cv.setPointerCapture(e.pointerId); y0 = e.clientY; m0 = S.get('macro'); this.drag = true; hostGesture('macro', true); });
    this.cv.addEventListener('pointermove', e => { if (this.drag) S.set('macro', m0 + (y0 - e.clientY) * (e.shiftKey ? 0.001 : 0.005)); });
    const end = () => { if (!this.drag) return; this.drag = false; hostGesture('macro', false); };
    this.cv.addEventListener('pointerup', end); this.cv.addEventListener('pointercancel', end);
    this.cv.addEventListener('wheel', e => { e.preventDefault(); S.set('macro', S.get('macro') - Math.sign(e.deltaY) * 0.02); }, { passive: false });
    this.cv.addEventListener('keydown', e => { const d = { ArrowUp: 0.02, ArrowDown: -0.02 }[e.key]; if (d) { e.preventDefault(); S.set('macro', S.get('macro') + d); } });
    views.push(this);
  }
  fire(local = true) {
    if (this.flash > 0.5) return;
    this.flash = 1; this.hi = 0;
    if (local) E.drop();
    $('#main').animate([{ filter: 'brightness(2.2) saturate(1.5)' }, { filter: 'none' }], { duration: 360, easing: 'ease-out' });
  }
  draw(t, dt) {
    const g = this.g, s = this.s, m = S.macroEff;
    if (!E.host) { // the plugin detects drops in the DSP; the browser preview does it here
      this.hi = Math.max(m, this.hi - dt * 0.5);
      if (m < 0.08 && this.hi > 0.7) this.fire();
    }
    this.vel += (m - this.pos) * 0.3; this.vel *= 0.62; this.pos += this.vel;
    const crit = clamp((m - 0.85) / 0.15), A0 = Math.PI * 0.75, SW = Math.PI * 1.5;
    this.lockA += dt * (0.1 + m * m * 7); this.lockB -= dt * (0.15 + m * m * 5);
    g.clearRect(0, 0, s, s);
    g.save();
    g.scale(s / 360, s / 360); // drawn on a 360 grid
    const c = 180;
    if (crit > 0) g.translate((Math.random() - 0.5) * crit * 3, (Math.random() - 0.5) * crit * 3); // critical micro-vibration

    const rl = 177 - m * 3, seg = TAU / 12 * (0.45 + 0.5 * m);
    for (let i = 0; i < 12; i++) { const a = this.lockA + i * TAU / 12; arc(g, c, c, rl, a, a + seg, i % 3 ? '#3b4048' : COL.copper, 5); }

    g.font = `9px ${FONT}`;
    CURVES.forEach((cv, i) => {
      const r = 164 - i * 11, v = S.curveVals[i];
      arc(g, c, c, r, A0, A0 + SW, '#0d0e10', 7);
      if (v > 0.001) { arc(g, c, c, r, A0, A0 + SW * v, CURVE_COL[i], 7); if (crit > 0) arc(g, c, c, r, A0, A0 + SW * v, `rgba(255,255,255,${crit * 0.25})`, 2); }
    });
    // labels in the open bottom of the gauge, kept clear of the dial body
    CURVES.forEach((cv, i) => {
      const y = 276 + i * 10, v = S.curveVals[i];
      g.fillStyle = CURVE_COL[i]; g.textAlign = 'right'; g.fillText(BUILD_MAPS[S.activeBuild].labels[i], c - 34, y);
      g.textAlign = 'left'; g.fillText((v * 100).toFixed(0) + '%', c + 34, y);
    });

    const rj = 106 - m * 7, jaw = TAU / 8 * (0.35 + 0.55 * m);
    for (let i = 0; i < 8; i++) { const a = this.lockB + i * TAU / 8; arc(g, c, c, rj, a, a + jaw, '#2c3036', 10); arc(g, c, c, rj + 5, a, a + jaw, `rgba(255,${(176 - 130 * m) | 0},46,${0.15 + 0.8 * m})`, 1.5); }

    // RISE progress ring
    if (S.get('riseOn')) {
      const rp = E.host ? E.riseProg : S.riseProg;
      arc(g, c, c, 96, -Math.PI / 2, -Math.PI / 2 + TAU * rp, 'rgba(255,255,255,.85)', 2.5);
      g.fillStyle = '#fff'; g.textAlign = 'center'; g.font = `10px ${FONT}`;
      const done = E.host ? E.riseDone : S.riseDone;
      g.fillText(done ? 'RISE DONE · RE-ARM' : `RISE ${PM.riseBars.options[S.get('riseBars')]} BAR ${(rp * 100).toFixed(0)}%`, c, 236);
    }

    g.beginPath();
    for (let i = 0; i <= 20; i++) { const a = A0 + SW * i / 20, l = i % 5 ? 3 : 6; g.moveTo(c + Math.cos(a) * 88, c + Math.sin(a) * 88); g.lineTo(c + Math.cos(a) * (88 + l), c + Math.sin(a) * (88 + l)); }
    g.strokeStyle = '#626870'; g.lineWidth = 1; g.stroke();
    const a = A0 + SW * clamp(this.pos, -0.02, 1.02);
    const bg = g.createRadialGradient(c - 25, c - 30, 5, c, c, 84);
    bg.addColorStop(0, '#80868f'); bg.addColorStop(0.5, '#33373d'); bg.addColorStop(1, '#111214');
    disc(g, c, c, 84, bg);
    g.strokeStyle = 'rgba(0,0,0,.65)'; g.lineWidth = 1.5; g.beginPath();
    for (let i = 0; i < 60; i++) { const b = a + i * TAU / 60; g.moveTo(c + Math.cos(b) * 78, c + Math.sin(b) * 78); g.lineTo(c + Math.cos(b) * 84, c + Math.sin(b) * 84); }
    g.stroke();
    if (crit > 0) arc(g, c, c, 85, 0, TAU, `rgba(255,45,60,${crit * (0.5 + 0.5 * Math.sin(t * 40))})`, 3);
    g.strokeStyle = crit > 0 ? COL.crimson : COL.amber; g.lineWidth = 5; g.lineCap = 'round';
    g.beginPath(); g.moveTo(c + Math.cos(a) * 36, c + Math.sin(a) * 36); g.lineTo(c + Math.cos(a) * 72, c + Math.sin(a) * 72); g.stroke(); g.lineCap = 'butt';
    disc(g, c, c, 30, '#0b0c0e'); arc(g, c, c, 30, 0, TAU, '#3a3e44', 1);
    g.fillStyle = crit > 0 ? COL.crimson : COL.amber; g.textAlign = 'center';
    g.font = `700 24px ${DISPLAY}`; g.fillText(Math.round(m * 100), c, c + 8);
    g.font = `9px ${FONT}`; g.fillStyle = COL.dim; g.fillText('%', c, c + 21);
    if (crit > 0 && Math.sin(t * 18) > 0) { g.fillStyle = COL.crimson; g.font = `10px ${FONT}`; g.fillText('CRITICAL', c, c - 44); }
    else if (m > 0.02) { g.fillStyle = COL.mod; g.font = `9px ${FONT}`; g.fillText(BUILD_MODES[S.activeBuild], c, c - 44); }
    if (this.flash > 0) { // drop-release flash + shockwave
      g.fillStyle = `rgba(255,240,220,${this.flash * 0.8})`; g.fillRect(-10, -10, 380, 380);
      arc(g, c, c, 84 + (1 - this.flash) * 110, 0, TAU, `rgba(255,255,255,${this.flash})`, 4);
      this.flash = Math.max(0, this.flash - dt * 2.5);
    }
    g.restore();
    this.cv.setAttribute('aria-valuenow', Math.round(m * 100));
  }
}

class CurvePlot {
  constructor(parent) { this.cv = mk('canvas', 'lane', parent); this.W = 380; this.H = 64; this.g = hiDPI(this.cv, this.W, this.H); views.push(this); }
  draw() {
    const { g, W, H } = this, m = S.macroEff, X = x => 6 + x * (W - 12), Y = y => H - 6 - y * (H - 20);
    g.fillStyle = '#07080a'; g.fillRect(0, 0, W, H);
    g.fillStyle = 'rgba(255,45,60,.08)'; g.fillRect(X(0.85), 14, X(1) - X(0.85), H - 20);
    CURVES.forEach((cv, i) => {
      g.beginPath(); for (let k = 0; k <= 40; k++) { const x = k / 40; g[k ? 'lineTo' : 'moveTo'](X(x), Y(cv.f(x))); }
      g.strokeStyle = CURVE_COL[i]; g.globalAlpha = 0.8; g.lineWidth = 1.3; g.stroke(); g.globalAlpha = 1;
      disc(g, X(m), Y(S.curveVals[i]), 3, CURVE_COL[i]);
    });
    g.strokeStyle = 'rgba(255,255,255,.5)'; g.beginPath(); g.moveTo(X(m), 14); g.lineTo(X(m), H - 6); g.stroke();
    g.font = `9px ${FONT}`; g.textAlign = 'left';
    BUILD_MAPS[S.activeBuild].labels.forEach((l, i) => { g.fillStyle = CURVE_COL[i]; g.fillText(l, 8 + i * 58, 10); });
    g.fillStyle = COL.dim; g.textAlign = 'right'; g.fillText('BUILD · ' + BUILD_MODES[S.activeBuild], W - 6, 10);
  }
}

/* ---------- telemetry ---------- */
class Spectrum {
  constructor(parent) {
    this.cv = mk('canvas', 'lane', parent); this.W = 272; this.H = 150; this.g = hiDPI(this.cv, this.W, this.H); this.pk = new Float32Array(40);
    const { W, H } = this;
    this.grad = this.g.createLinearGradient(0, H - 14, 0, 2);
    this.grad.addColorStop(0, COL.amber); this.grad.addColorStop(0.6, COL.amber); this.grad.addColorStop(0.62, COL.copper); this.grad.addColorStop(0.8, COL.copper); this.grad.addColorStop(0.82, COL.crimson);
    this.grid = layer(W, H, g => { g.fillStyle = '#07080a'; for (let y = H - 15; y > 0; y -= 4) g.fillRect(0, y, W, 1); }); // LED segment gaps
    views.push(this);
  }
  draw(t, dt) {
    const { g, W, H } = this, N = 40, bw = W / N, live = E.host || E.playing;
    g.fillStyle = '#07080a'; g.fillRect(0, 0, W, H);
    g.fillStyle = this.grad;
    for (let i = 0; i < N; i++) {
      const v = live ? E.bands[i] : 0.03 + 0.02 * Math.sin(t * 2 + i * 0.4), h = v * (H - 16);
      this.pk[i] = Math.max(v, this.pk[i] - dt * 0.5);
      g.fillRect(i * bw + 1, H - 14 - h, bw - 2, h);
    }
    g.drawImage(this.grid, 0, 0, W, H);
    g.fillStyle = '#fff';
    for (let i = 0; i < N; i++) g.fillRect(i * bw + 1, H - 14 - this.pk[i] * (H - 16), bw - 2, 1);
    g.font = `9px ${FONT}`; g.fillStyle = COL.dim; g.textAlign = 'left';
    [[100, '100'], [1000, '1k'], [10000, '10k']].forEach(([f, l]) => g.fillText(l, Math.log(f / 30) / Math.log(16000 / 30) * W, H - 3));
    g.textAlign = 'right'; g.fillText('SPECTRUM OUT', W - 4, 10);
  }
}
class BufferView {
  constructor(parent) { this.cv = mk('canvas', 'lane', parent); this.W = 272; this.H = 66; this.g = hiDPI(this.cv, this.W, this.H); this.mode = 0; this.entry = 0; views.push(this); }
  draw(t) {
    const { g, W, H } = this, a = E.activity, cells = 32, cw = W / cells, head = Math.floor((((BEATS / 8) % 1) + 1) % 1 * cells);
    if (a.mode !== this.mode) { this.mode = a.mode; this.entry = head; }
    const step = 60 / E.bpm / 4 * E.sampleRate, lc = Math.max(1, Math.min(cells, Math.round(a.len / step)));
    g.fillStyle = '#07080a'; g.fillRect(0, 0, W, H);
    const looping = a.mode && a.mode !== 3;
    for (let i = 0; i < cells; i++) {
      const d = (head - i + cells) % cells, back = (this.entry - i + cells) % cells;
      const inLoop = looping && (lc === 1 ? back === 1 : back > 0 && back <= lc);
      g.fillStyle = inLoop ? (a.mode === 4 ? `rgba(79,209,255,${0.5 + 0.3 * Math.sin(t * 9)})` : COL.crimson) : `rgba(255,176,46,${E.host || E.playing ? 0.05 + 0.4 * (1 - d / cells) * (a.mode === 3 ? a.rate : 1) : 0.05})`;
      g.fillRect(i * cw + 1, 16, cw - 2, 30);
    }
    if (a.mode === 1 || a.mode === 2 || a.mode === 4) {
      const x0 = ((this.entry - lc + cells) % cells) * cw, x = x0 + (a.mode === 2 ? 1 - a.pos : a.pos) * lc * cw;
      g.fillStyle = '#fff'; g.fillRect(x % W, 12, 2, 38);
    }
    g.font = `10px ${FONT}`; g.textAlign = 'left'; g.fillStyle = a.mode ? COL.crimson : COL.amber;
    const name = ['LIVE', 'STUTTER', 'REVERSE', 'TAPE STOP', 'FREEZE'][a.mode] + (a.gl ? ' · RANDOM' : '') + (a.mode === 3 ? ` ${(a.rate * 100).toFixed(0)}%` : '');
    g.fillText(name, 4, 11);
    g.textAlign = 'right'; g.fillStyle = COL.dim; g.fillText('GLITCH BUFFER', W - 4, 11);
    g.fillText('2 BARS · 1/16 CELLS', W - 4, H - 4);
  }
}
class VU {
  constructor(parent) {
    this.cv = mk('canvas', 'lane', parent); this.W = 272; this.H = 112; this.g = hiDPI(this.cv, this.W, this.H); this.n = 0; this.v = 0;
    const { W, H } = this, px = W / 2, py = H + 40, R = 130, ang = n => -Math.PI / 2 - 0.75 + 1.5 * n, nz = d => (d + 20) / 23;
    this.geo = { px, py, R, ang };
    this.face = layer(W, H, g => {
      const bg = g.createRadialGradient(W / 2, H, 10, W / 2, H, 160); bg.addColorStop(0, '#3a2408'); bg.addColorStop(1, '#0a0806');
      g.fillStyle = bg; g.fillRect(0, 0, W, H);
      arc(g, px, py, R, ang(0), ang(nz(0)), '#d8c7a4', 1.5); arc(g, px, py, R, ang(nz(0)), ang(1), COL.crimson, 3);
      g.font = `9px ${FONT}`; g.textAlign = 'center';
      [-20, -10, -7, -5, -3, 0, 3].forEach(d => {
        const a = ang(nz(d)); g.strokeStyle = d >= 0 ? COL.crimson : '#d8c7a4'; g.lineWidth = 1;
        g.beginPath(); g.moveTo(px + Math.cos(a) * R, py + Math.sin(a) * R); g.lineTo(px + Math.cos(a) * (R + 7), py + Math.sin(a) * (R + 7)); g.stroke();
        g.fillStyle = d >= 0 ? COL.crimson : '#d8c7a4'; g.fillText((d > 0 ? '+' : '') + d, px + Math.cos(a) * (R + 16), py + Math.sin(a) * (R + 16) + 3);
      });
      g.fillStyle = COL.amber; g.font = `11px ${FONT}`; g.fillText('VU · OUTPUT', W / 2, H - 8);
    });
    views.push(this);
  }
  draw() {
    const { g, W, H } = this, { px, py, R, ang } = this.geo, db = 20 * Math.log10((E.rms || 0) + 1e-6) + 18; // −18 dBFS RMS = 0 VU
    this.v += ((clamp(db, -20, 3) + 20) / 23 - this.n) * 0.12; this.v *= 0.78; this.n += this.v;
    g.drawImage(this.face, 0, 0, W, H);
    const a = ang(clamp(this.n, -0.03, 1.03));
    g.strokeStyle = '#f2ece0'; g.lineWidth = 1.5; g.beginPath(); g.moveTo(px + Math.cos(a) * 60, py + Math.sin(a) * 60); g.lineTo(px + Math.cos(a) * (R + 4), py + Math.sin(a) * (R + 4)); g.stroke();
  }
}
class Bars {
  constructor(parent) { this.cv = mk('canvas', 'lane', parent); this.W = 272; this.H = 94; this.g = hiDPI(this.cv, this.W, this.H); views.push(this); }
  draw() {
    const { g, W } = this, a = E.activity, lvl = Math.min(1, (E.rms || 0) * 4);
    const rows = [
      ['RESON', clamp(S.eff('resonTension') * (0.3 + 0.7 * lvl))],
      ['GLITCH', clamp(S.eff('glitchAmt') * 0.6 + (a.mode && a.mode < 4 ? 0.4 + 0.3 * Math.random() : 0))],
      ['COMP GR', clamp(-(E.gr || 0) / 18)],
      ['ENV', clamp(E.env)],
    ];
    g.fillStyle = '#07080a'; g.fillRect(0, 0, W, this.H);
    g.font = `9px ${FONT}`; g.textAlign = 'left';
    rows.forEach(([name, v], r) => {
      const y = 6 + r * 22; g.fillStyle = COL.dim; g.fillText(name, 4, y + 11);
      for (let i = 0; i < 30; i++) { const on = i / 30 < v; g.fillStyle = on ? (i > 24 ? COL.crimson : i > 18 ? COL.copper : COL.amber) : '#16181b'; g.fillRect(62 + i * 7, y + 2, 6, 12); }
    });
  }
}

/* ---------- modulation lanes ---------- */
class LfoView {
  constructor(parent, k) { this.k = k; this.cv = mk('canvas', 'lane', parent); this.W = 132; this.H = 54; this.g = hiDPI(this.cv, this.W, this.H); this.sh = Array.from({ length: 8 }, () => Math.random() * 2 - 1); views.push(this); }
  draw() {
    const { g, W, H, k } = this, sh = S.eff(`lfo${k}Shape`), amp = (H / 2 - 6) * (0.25 + 0.75 * Math.abs(S.get(`lfo${k}Depth`)));
    g.fillStyle = '#07080a'; g.fillRect(0, 0, W, H);
    g.strokeStyle = '#1a1c20'; g.lineWidth = 1; g.beginPath(); g.moveTo(0, H / 2); g.lineTo(W, H / 2); g.stroke();
    g.beginPath();
    for (let x = 0; x <= W; x += 2) { const u = x / W, v = sh === 4 ? this.sh[Math.min(7, (u * 8) | 0)] : lfoShape(sh, u, 0); g[x ? 'lineTo' : 'moveTo'](x, H / 2 - v * amp); }
    g.strokeStyle = 'rgba(79,209,255,.55)'; g.lineWidth = 1.3; g.stroke();
    const ph = MODV['lfo' + k + 'Ph'] || 0, v = MODV['lfo' + k] || 0;
    g.strokeStyle = 'rgba(79,209,255,.25)'; g.lineWidth = 1; g.beginPath(); g.moveTo(ph * W, 0); g.lineTo(ph * W, H); g.stroke();
    disc(g, ph * W, H / 2 - v * amp, 3.5, '#fff');
    g.font = `9px ${FONT}`; g.fillStyle = COL.mod; g.textAlign = 'left'; g.fillText('→ ' + TGT[S.get(`lfo${k}Target`)], 4, 10);
  }
}
class EnvView {
  constructor(parent) { this.cv = mk('canvas', 'lane', parent); this.W = 132; this.H = 54; this.g = hiDPI(this.cv, this.W, this.H); this.hist = new Float32Array(66); views.push(this); }
  draw() {
    const { g, W, H, hist } = this;
    hist.copyWithin(0, 1); hist[hist.length - 1] = E.env;
    g.fillStyle = '#07080a'; g.fillRect(0, 0, W, H);
    g.beginPath(); g.moveTo(0, H);
    hist.forEach((v, i) => g.lineTo(i * W / (hist.length - 1), H - 2 - v * (H - 14)));
    g.lineTo(W, H); g.fillStyle = 'rgba(79,209,255,.25)'; g.fill(); g.strokeStyle = COL.mod; g.lineWidth = 1.2; g.stroke();
    g.font = `9px ${FONT}`; g.fillStyle = COL.mod; g.textAlign = 'left'; g.fillText('→ ' + TGT[S.get('envTarget')], 4, 10);
  }
}

/* ---------- snapshot editor: what each S## pushes, edit / remove, copy / paste ---------- */
class SnapEditor {
  constructor(parent) {
    this.el = mk('div', 'sned', parent); this.el.hidden = true;
    const top = mk('div', 'sned-top', this.el);
    this.btns = Array.from({ length: 16 }, (_, i) => { const b = mk('button', null, top, 'S' + pad2(i)); b.onclick = () => { pad.sel = i; pad.goto(i); }; return b; });
    const act = mk('div', 'sned-act', this.el);
    this.title = mk('span', 'sned-title', act);
    mk('button', 'mini', act, 'COPY').onclick = () => { this.clip = { ...S.snaps[pad.sel].v }; status(`COPIED S${pad2(pad.sel)} · PICK ANOTHER S## AND PASTE`); };
    mk('button', 'mini', act, 'PASTE').onclick = () => { if (!this.clip) return status('COPY A SNAPSHOT FIRST'); S.setSnapV(pad.sel, this.clip); pad.flash[pad.sel] = 1; status(`PASTED → S${pad2(pad.sel)}`); };
    mk('button', 'mini', act, 'CAPTURE').onclick = () => { S.saveSnap(pad.sel); pad.flash[pad.sel] = 1; status(`S${pad2(pad.sel)} = ALL CURRENT KNOBS`); };
    mk('button', 'mini', act, 'CLEAR').onclick = () => { S.clearSnap(pad.sel); status(`S${pad2(pad.sel)} CLEARED`); };
    mk('button', 'mini', act, 'CLOSE').onclick = () => this.toggle();
    this.list = mk('div', 'sned-list', this.el);
    this.key = '';
    views.push(this);
  }
  toggle() { this.el.hidden = !this.el.hidden; pad.editorOpen = !this.el.hidden; this.key = ''; }
  draw() {
    if (this.el.hidden) return;
    const key = pad.sel + '|' + S.snapVer;
    if (key === this.key) return;
    this.key = key;
    this.btns.forEach((b, i) => { b.classList.toggle('on', i === pad.sel); b.classList.toggle('has', Object.keys(S.snaps[i].v).length > 0); });
    const v = S.snaps[pad.sel].v, ids = PARAMS.map(p => p.id).filter(id => v[id] !== undefined);
    this.title.textContent = `S${pad2(pad.sel)} · ${ids.length} FX`;
    this.list.textContent = '';
    if (!ids.length) { mk('div', 'sned-empty', this.list, 'EMPTY · RIGHT-DRAG KNOBS HERE FROM THE RACK, OR LEARN / CAPTURE'); return; }
    for (const id of ids) {
      const row = mk('div', 'sned-row', this.list);
      mk('span', 'sned-name', row, TAG(id));
      const r = mk('input', null, row); r.type = 'range'; r.min = 0; r.max = 1; r.step = 0.001; r.value = v[id];
      r.setAttribute('aria-label', `${TAG(id)} in S${pad2(pad.sel)}`);
      const val = mk('span', 'sned-val', row, fmt(id, denorm(PM[id], v[id])));
      r.oninput = () => { S.setPush(pad.sel, id, +r.value); this.key = pad.sel + '|' + S.snapVer; val.textContent = fmt(id, denorm(PM[id], +r.value)); };
      const x = mk('button', 'mini', row, '✕'); x.title = 'Remove from this snapshot'; x.onclick = () => S.unpush(pad.sel, id);
    }
  }
}

/* ---------- build the panels ---------- */
// header: presets + RANDOM, hero MIX, bypass
const presetSel = $('#preset');
/* MY PRESETS: plugin = .skatom files in Documents/SKATOM/Presets (via the host), browser = localStorage */
const MINE = {
  list: [],
  all: () => PRESETS.concat(MINE.list),
  set(raw) {
    MINE.list = [];
    for (const r of raw) { try { MINE.list.push({ ...JSON.parse(r.json), cat: 'USER', name: r.name }); } catch (err) { console.warn('bad preset', r.name); } }
    MINE.list.sort((a, b) => a.name.localeCompare(b.name));
    fillPresets();
  },
  local() { try { return JSON.parse(localStorage.getItem('skatom.presets') || '[]'); } catch (err) { return []; } },
  store(raw) { try { localStorage.setItem('skatom.presets', JSON.stringify(raw)); } catch (err) { /* private mode: session only */ } MINE.set(raw); },
  save(name, pr) {
    const json = JSON.stringify(pr);
    if (E.host) E.savePreset(name, json); else MINE.store(MINE.local().filter(r => r.name !== name).concat({ name, json }));
  },
  del(name) { if (E.host) E.deletePreset(name); else MINE.store(MINE.local().filter(r => r.name !== name)); },
};
function fillPresets() {
  const keep = presetSel.selectedOptions[0] ? presetSel.selectedOptions[0].textContent : '';
  presetSel.textContent = '';
  let group = null, cat = '';
  MINE.all().forEach((pr, i) => {
    if (pr.cat !== cat) { cat = pr.cat; group = mk('optgroup', null, presetSel); group.label = cat === 'RISE' ? 'RISE · UPLIFTERS' : cat === 'USER' ? 'MY PRESETS' : cat; }
    const o = mk('option', null, group, pr.name); o.value = i; if (pr.name === keep) o.selected = true;
  });
}
function loadPreset(i) {
  const all = MINE.all();
  i = (i + all.length) % all.length;
  presetSel.value = i;
  applyPreset(S, all[i]);
  S.setXY(S.snaps[0].x, S.snaps[0].y); pad.glideTo = null; pad.sel = 0;
  status(`PRESET · ${all[i].cat === 'USER' ? 'MY PRESETS' : all[i].cat} · ${all[i].name}`);
}
E.onUserPresets = list => MINE.set(list);
if (!E.host) MINE.set(MINE.local());
{ // SAVE: type a name in place (no pop-up dialogs inside the plugin)
  const row = $('.prow'), save = mk('button', 'bigbtn sm', row, 'SAVE'), del = mk('button', 'bigbtn sm', row, 'DEL');
  const nameIn = mk('input', 'pname'); nameIn.placeholder = 'NAME + ENTER'; nameIn.maxLength = 48; nameIn.hidden = true;
  presetSel.after(nameIn);
  const close = () => { nameIn.hidden = true; presetSel.hidden = false; save.textContent = 'SAVE'; };
  const commit = () => {
    const name = nameIn.value.trim().replace(/[\\/:*?"<>|]/g, '');
    if (!name) return close();
    MINE.save(name, S.toPreset(name)); close();
    status(`SAVED · MY PRESETS · ${name}`);
  };
  save.onclick = () => {
    if (!nameIn.hidden) return commit();
    const cur = MINE.all()[+presetSel.value];
    nameIn.value = cur && cur.cat === 'USER' ? cur.name : ''; nameIn.hidden = false; presetSel.hidden = true; save.textContent = 'OK'; nameIn.focus();
  };
  nameIn.onkeydown = e => { e.stopPropagation(); if (e.key === 'Enter') commit(); if (e.key === 'Escape') close(); };
  del.title = 'Delete the selected preset from MY PRESETS';
  del.onclick = () => {
    const cur = MINE.all()[+presetSel.value];
    if (!cur || cur.cat !== 'USER') return status('DEL · ONLY YOUR OWN PRESETS CAN BE DELETED');
    if (del.dataset.arm !== cur.name) { del.dataset.arm = cur.name; del.textContent = 'SURE?'; return setTimeout(() => { del.dataset.arm = ''; del.textContent = 'DEL'; }, 2500); }
    del.dataset.arm = ''; del.textContent = 'DEL';
    MINE.del(cur.name); status(`DELETED · ${cur.name}`);
  };
}
fillPresets();
presetSel.onchange = () => loadPreset(+presetSel.value);
$('#prev').onclick = () => loadPreset(+presetSel.value - 1);
$('#next').onclick = () => loadPreset(+presetSel.value + 1);
$('#rnd').onclick = () => {
  S.randomize((Math.random() * 1e9) | 0);
  status('RANDOM · NEW KNOBS + NEW MATRIX · DRAG THE PUCK');
};
const globals = $('#globals');
{
  const hero = mk('div', 'herobox', globals);
  new Knob(hero, 'dryWet', { size: 64, label: 'MIX', hero: true });
  mk('div', 'herocap', hero, 'DRY ◂ ▸ WET');
}
new Toggle(globals, 'bypass', 'BYPASS', 'crit');

const tel = $('#telemetry');
new Spectrum(tel); new BufferView(tel); new VU(tel); new Bars(tel);

const pad = new XYPad($('#core'));
{
  const h = $('#core h3');
  const learnBtn = mk('button', 'mini learn', h, 'LEARN'), editBtn = mk('button', 'mini', h, 'EDIT S##');
  mk('button', 'mini', h, 'CLEAR ALL').onclick = () => { S.clearAll(); status('MATRIX CLEARED · DRAG KNOBS ONTO SNAPSHOTS'); };
  learnBtn.title = 'Arm the selected snapshot: every knob you turn is written into it';
  learnBtn.onclick = () => {
    LEARN.on = !LEARN.on; learnBtn.classList.toggle('on', LEARN.on);
    if (LEARN.on) { pad.goto(pad.sel); status(`LEARN · S${pad2(pad.sel)} · TURN KNOBS · DOUBLE-CLICK A KNOB = REMOVE IT FROM S${pad2(pad.sel)}`); }
    else status('LEARN OFF · KNOBS SET THEIR HOME VALUE AGAIN');
  };
  const ed = new SnapEditor($('#core'));
  editBtn.onclick = () => ed.toggle();
}

const mac = $('#macro');
const dial = new MacroDial(mac);
new CurvePlot(mac);
{
  const row = mk('div', 'riserow', mac);
  new Toggle(row, 'riseOn', 'RISE');
  new Sel(row, 'buildMode', 'BUILD');
  new Sel(row, 'riseBars', 'BARS');
  new Toggle(row, 'autoDrop', 'AUTO DROP');
  const row2 = mk('div', 'riserow r2', mac);
  new Knob(row2, 'risePitch', { size: 40, label: 'RISE PITCH' });
  new Sel(row2, 'tailType', 'REVERB');
  // ACELERA: tempo pump that accelerates, width opening, drive, ride curve — all ride the macro / RISE
  const row3 = mk('div', 'riserow r3', mac);
  mk('span', 'acc-tag', row3, 'ACELERA');
  ['accPump', 'accWidth', 'accDrive', 'accEndless'].forEach(id => new Knob(row3, id, { size: 28 }));
  new Toggle(row3, 'accAccel', 'ACCEL');
  new Toggle(row3, 'accHigh', 'HIGHER');
  new Sel(row3, 'accCurve', 'CURVE');
}
const dropBtn = mk('button', null, mac); dropBtn.id = 'drop'; mk('span', null, dropBtn, 'DROP / RELEASE');
dropBtn.onclick = () => { dial.fire(); S.set('macro', 0); S.set('riseOn', 0); };
E.onDrop = () => dial.fire(false);   // drop fired inside the plugin (RISE end, automation)

/* EFFECTS strip: every effect in the face. Drag a tile onto the big knob (or click it) → it rides the knob.
   "ON THE KNOB" lists the slots: amount slider (negative = the other way) + remove. Any slot in use = the knob drives only these. */
class FxBar {
  constructor(parent) {
    mk('h3', null, parent, 'EFFECTS · DRAG ONTO THE BIG KNOB · OR CLICK');
    this.tiles = mk('div', 'fx-tiles', parent);
    const onRow = mk('div', 'fx-on', parent);
    mk('span', 'fx-ontag', onRow, 'ON THE KNOB');
    this.chips = mk('div', 'fx-chips', onRow);
    mk('button', 'mini', onRow, 'CLEAR KNOB').onclick = () => { for (let k = 1; k <= SMILE_SLOTS; k++) { S.set(`smile${k}T`, 0); S.set(`smile${k}A`, 0); } };
    this.tileEls = SMILE_FX.map((f, t) => {
      if (!t) return null;
      const b = mk('button', 'fx-tile', this.tiles);
      b.style.setProperty('--c', f.col);
      mk('b', null, b, f.name); mk('small', null, b, f.tip);
      b.title = `${f.name}: ${f.tip}. Drag onto the big knob, or click to add / remove.`;
      b.onpointerdown = e => this.drag(e, t);
      return b;
    });
    this.sig = '';
    views.push(this);
  }
  slotOf(t) { for (let k = 1; k <= SMILE_SLOTS; k++) if (S.get(`smile${k}T`) === t) return k; return 0; }
  toggle(t) {
    const k = this.slotOf(t);
    if (k) { S.set(`smile${k}T`, 0); S.set(`smile${k}A`, 0); return; }
    this.add(t);
  }
  add(t) {
    if (this.slotOf(t)) return;
    for (let k = 1; k <= SMILE_SLOTS; k++) if (!S.get(`smile${k}T`)) {
      S.set(`smile${k}T`, t); S.set(`smile${k}A`, SMILE_FX[t].def);
      status(`${SMILE_FX[t].name} IS ON THE KNOB · TURN THE BIG KNOB`);
      return;
    }
    status('THE KNOB IS FULL (8) · REMOVE ONE FIRST');
  }
  drag(e, t) {
    e.preventDefault();
    const x0 = e.clientX, y0 = e.clientY, dcv = dial.cv;
    let ghost = null;
    const over = ev => { const r = dcv.getBoundingClientRect(); return ev.clientX > r.left && ev.clientX < r.right && ev.clientY > r.top && ev.clientY < r.bottom; };
    const move = ev => {
      if (!ghost && Math.hypot(ev.clientX - x0, ev.clientY - y0) > 6) { ghost = mk('div', 'fx-ghost', document.body, SMILE_FX[t].name); ghost.style.setProperty('--c', SMILE_FX[t].col); }
      if (!ghost) return;
      ghost.style.left = ev.clientX + 'px'; ghost.style.top = ev.clientY + 'px';
      dcv.classList.toggle('drophot', over(ev));
    };
    const up = ev => {
      removeEventListener('pointermove', move); removeEventListener('pointerup', up);
      dcv.classList.remove('drophot');
      if (!ghost) { this.toggle(t); return; } // a click
      ghost.remove();
      if (over(ev)) this.add(t);
    };
    addEventListener('pointermove', move); addEventListener('pointerup', up);
  }
  draw() {
    let sig = '';
    for (let k = 1; k <= SMILE_SLOTS; k++) sig += S.get(`smile${k}T`) + ',';
    this.tileEls.forEach((b, t) => b && b.classList.toggle('on', !!this.slotOf(t)));
    if (sig === this.sig) { // only refresh the numbers
      this.chips.querySelectorAll('input').forEach(i => { if (document.activeElement !== i) { i.value = S.get(i.dataset.id) * 100; i.nextSibling.textContent = `${Math.round(i.value)}%`; } });
      return;
    }
    this.sig = sig; this.chips.textContent = '';
    for (let k = 1; k <= SMILE_SLOTS; k++) {
      const t = S.get(`smile${k}T`); if (!t) continue;
      const f = SMILE_FX[t], id = `smile${k}A`, chip = mk('div', 'fx-chip', this.chips);
      chip.style.setProperty('--c', f.col);
      mk('b', null, chip, f.name);
      const r = mk('input', null, chip); r.type = 'range'; r.min = -100; r.max = 100; r.value = S.get(id) * 100; r.dataset.id = id;
      r.setAttribute('aria-label', `${f.name} amount`);
      const v = mk('span', null, chip, `${Math.round(r.value)}%`);
      r.onpointerdown = () => hostGesture(id, true); r.onpointerup = () => hostGesture(id, false);
      r.oninput = () => { S.set(id, r.value / 100); v.textContent = `${r.value}%`; };
      const x = mk('button', 'fx-x', chip, '✕'); x.setAttribute('aria-label', `Remove ${f.name}`);
      x.onclick = () => { S.set(`smile${k}T`, 0); S.set(id, 0); };
    }
    if (!this.chips.children.length) mk('span', 'fx-empty', this.chips, 'NOTHING YET · THE KNOB RUNS THE BUILD MODE · DRAG EFFECTS HERE TO MAKE YOUR OWN');
  }
}
new FxBar($('#fxbar'));

const RACK = [
  ['01', 'GLITCH BUFFER', b => { new Sel(b, 'stutDiv'); new Knob(b, 'tapeTime'); new Knob(b, 'glitchAmt'); new Toggle(b, 'stutter', 'STUTTER'); new Toggle(b, 'reverse', 'REVERSE'); new Toggle(b, 'tapeStop', 'TAPE STOP'); },
    () => E.activity.mode > 0 && E.activity.mode < 4],
  ['02', 'PITCH · SPECTRAL', b => { ['pitch', 'freqShift', 'contrast', 'resonFreq', 'resonTension'].forEach(id => new Knob(b, id)); new Toggle(b, 'freeze', 'FREEZE'); },
    () => S.get('freeze') || S.eff('resonTension') > 0.02 || Math.abs(S.eff('freqShift')) > 1 || S.eff('contrast') > 0.02 || Math.abs(S.eff('pitch')) > 0.05],
  ['03', 'FILTER & FORMANT', b => { new Seg(b, 'filtType'); ['filtCut', 'filtRes', 'filtDrive', 'vowel', 'formantMix'].forEach(id => new Knob(b, id)); },
    () => S.eff('filtCut') < 19000 || S.eff('filtType') || S.eff('formantMix') > 0.02 || S.eff('filtDrive') > 0.02],
  ['04', 'DIST & BASS', b => { new Seg(b, 'distType'); ['distDrive', 'distMix', 'magicBass'].forEach(id => new Knob(b, id)); },
    () => S.eff('distDrive') > 0.02 || S.eff('magicBass') > 0.02],
  ['05', 'BUILD-UP FX BUS', b => ['hpf', 'noise', 'detune', 'wash', 'roll', 'jet', 'spin', 'dropDuck'].forEach(id => new Knob(b, id)),
    () => S.eff('hpf') > 25 || ['wash', 'noise', 'detune', 'roll', 'jet', 'spin'].some(id => S.eff(id) > 0.02)],
  ['06', 'MASTER', b => { new Knob(b, 'comp'); new Knob(b, 'ceiling'); new Knob(b, 'output'); new Toggle(b, 'limiter', 'LIMITER'); },
    () => (E.gr || 0) < -0.5],
];
RACK.forEach(([num, name, build, active], k) => {
  const pl = mk('div', 'plate', $('#rack')), h = mk('h3', null, pl), onId = `mod${k + 1}On`;
  const pwr = mk('button', 'pwr', h, '⏻'); pwr.title = `Module ${num} on / off`; pwr.setAttribute('aria-label', `${name} on / off`);
  pwr.onclick = e => { e.stopPropagation(); S.set(onId, S.get(onId) ? 0 : 1); };
  pwr.onpointerdown = e => e.stopPropagation();
  mk('b', null, h, num); const title = mk('span', 'grab', h, name);
  const led = mk('span', 'led', h);
  BUILDING = [];
  build(mk('div', 'mod-body', pl));
  const ids = BUILDING; BUILDING = [];
  title.title = `Drag onto the matrix: push all of ${num} (${ids.map(TAG).join(', ')}) at a snapshot`;
  title.addEventListener('pointerdown', e => Drag.start(ids, e, `${num} ${name} ×${ids.length}`));
  views.push({ draw() {
    const on = !!active() && !!S.get(onId), off = !S.get(onId);
    if (on !== led._on) led.classList.toggle('on', led._on = on);
    if (off !== pl._off) { pl.classList.toggle('off', pl._off = off); pwr.classList.toggle('on', !off); }
  } });
});
{ // FX ORDER: click cycles (shortcut: the browser preview always runs the first order, the plugin DSP runs all four)
  const b = mk('button', 'mini order', $('#core h3')), short = o => o.split(' › ').map(w => w[0]).join('›');
  b.title = 'FX ORDER — where the glitch sits, and the filter before or after the distortion. Click to change.';
  b.onclick = () => { S.set('fxOrder', (S.get('fxOrder') + 1) % FX_ORDERS.length); status('FX ORDER · ' + FX_ORDERS[S.get('fxOrder')]); };
  views.push({ draw() { const v = S.get('fxOrder'); if (v !== b._v) { b._v = v; b.textContent = 'ORDER ' + short(FX_ORDERS[v]); } } });
}

[1, 2].forEach(k => {
  const pl = mk('div', 'plate', $('#modbar')); mk('h3', null, pl, `LFO ${k} · BPM SYNC`);
  const b = mk('div', 'mod-body', pl);
  new LfoView(b, k); new Knob(b, `lfo${k}Depth`); new Sel(b, `lfo${k}Rate`); new Sel(b, `lfo${k}Target`); new Seg(b, `lfo${k}Shape`);
});
{
  const pl = mk('div', 'plate', $('#modbar')); mk('h3', null, pl, 'ENVELOPE FOLLOWER · INPUT');
  const b = mk('div', 'mod-body', pl);
  new EnvView(b); ['envSens', 'envAtk', 'envRel', 'envDepth'].forEach(id => new Knob(b, id)); new Sel(b, 'envTarget');
}

/* ---------- transport (browser preview only — the plugin follows the host) ---------- */
const playBtn = $('#play'), srcBtns = [...document.querySelectorAll('#srcSeg button')];
const setPlayUI = () => { playBtn.classList.toggle('on', E.playing); playBtn.innerHTML = E.playing ? '&#9632; STOP' : '&#9654; PLAY'; };
const setSrcUI = () => srcBtns.forEach(b => b.classList.toggle('on', b.dataset.src === E.source));
const srcStatus = () => E.source === 'mic' ? 'LIVE · MIC INPUT — USE HEADPHONES' : E.source === 'file' ? `LIVE · FILE ${E.fileName || ''}` : `LIVE · TEST LOOP ${E.bpm} BPM`;

playBtn.onclick = async () => {
  try {
    if (E.playing) { E.stop(); status('STOPPED'); }
    else { status('IGNITING ENGINE…'); await E.start(); status(srcStatus()); }
  } catch (err) { E.playing = false; status('ERROR · ' + err.message); }
  setPlayUI();
};
srcBtns.forEach(b => b.onclick = async () => {
  if (b.dataset.src === 'file' && !E.fileBuf) return $('#file').click();
  E.source = b.dataset.src; setSrcUI();
  if (E.playing) { try { await E.startSource(); status(srcStatus()); } catch (err) { status('ERROR · ' + err.message); } }
});
async function loadFile(f) {
  if (!f || E.host) return;
  try {
    status('DECODING ' + f.name + '…');
    await E.ensure();
    E.fileBuf = await E.ctx.decodeAudioData(await f.arrayBuffer());
    E.fileName = f.name; E.source = 'file'; setSrcUI();
    if (E.playing) await E.startSource();
    status(E.playing ? srcStatus() : `LOADED ${f.name} · PRESS PLAY`);
  } catch (err) { status('ERROR · cannot decode ' + f.name); }
}
$('#file').onchange = e => loadFile(e.target.files[0]);
window.addEventListener('dragover', e => e.preventDefault());
window.addEventListener('drop', e => { e.preventDefault(); loadFile(e.dataTransfer.files[0]); });
$('#bpm').onchange = async e => {
  E.bpm = clamp(+e.target.value || 128, 60, 200); e.target.value = E.bpm; E.loopBuf = null;
  if (E.playing && E.source === 'loop') { await E.startSource(); status(srcStatus()); }
};

/* ---------- main loop ---------- */
const syncLed = $('#syncLed');
let last = performance.now();
function frame(now) {
  const dt = Math.min(0.05, (now - last) / 1000), t = now / 1000; last = now;
  const nb = E.beats(t), dBeats = clamp(nb - BEATS, 0, 0.5); BEATS = nb;
  if (!E.host && E.playing && S.stepRise(dBeats) === 'drop') dial.fire();
  S.updateMacro(Math.max(S.get('macro'), S.get('riseOn') ? (E.host ? E.riseProg : S.riseProg) : 0), E.host ? E.activeBuild : undefined);
  S.updateMatrix();
  MODV = S.updateMod(BEATS, E.env);
  E.update(dt);
  for (const v of views) v.draw(t, dt);
  syncLed.classList.toggle('on', (E.playing || E.host) && ((BEATS % 1) + 1) % 1 < 0.15);
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);

// fixed-design plugin window, scaled to fill whatever size the host gives it, centered
function fit() {
  const ch = $('#chassis');
  ch.style.transform = `translate(-50%, -50%) scale(${Math.min(innerWidth / ch.offsetWidth, innerHeight / ch.offsetHeight)})`;
}
window.addEventListener('resize', fit); fit();
loadPreset(0);
