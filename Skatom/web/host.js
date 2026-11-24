'use strict';
/* SKATOM — host bridge. Inside the JUCE plugin (window.__JUCE__ exists) this replaces the Web Audio
   engine: the DSP (incl. the matrix morph and RISE) runs in C++; this class syncs params + snapshots
   and reads telemetry. Matrix X/Y are ordinary params, so dragging the puck sends just two values. */
class HostEngine {
  constructor(state) {
    this.S = state; this.host = true; this.playing = false; this.bpm = 128; this.sampleRate = 48000; this.source = 'host';
    this.env = 0; this.rms = 0; this.gr = 0; this.bands = new Float32Array(40);
    this.riseProg = 0; this.riseDone = false;
    this.activity = { mode: 0, pos: 0, len: 1, rate: 1, gl: false };
    this.dirty = new Set(); this.ppq = 0; this.tAt = performance.now(); this.drops = -1;
    this.sentSnapVer = -1; this.lastSnapSend = 0; this.ready = false;
    this.be = window.__JUCE__.backend;
    state.onSet = id => this.dirty.add(id);
    this.be.addEventListener('init', o => this.init(o));
    this.be.addEventListener('params', o => Object.assign(this.S.base, o));
    this.be.addEventListener('telemetry', o => this.telemetry(o));
    this.be.addEventListener('userPresets', o => this.onUserPresets && this.onUserPresets(o.list));
    document.body.classList.add('host');
    this.be.emitEvent('ready', {});
  }
  init(o) {
    const bad = o.defs.filter(([id, min, max]) => !PM[id] || Math.abs(PM[id].min - min) > 1e-4 || Math.abs(PM[id].max - max) > 1e-4).map(d => d[0]);
    console.assert(!bad.length && o.defs.length === PARAMS.length, 'param tables differ: ' + bad.join(','));
    if (bad.length || o.defs.length !== PARAMS.length) this.status('PARAM MISMATCH · ' + bad.join(' '));
    Object.assign(this.S.base, o.params);
    if (o.snaps) {
      try { this.S.setSnaps(JSON.parse(o.snaps)); this.sentSnapVer = this.S.snapVer; }
      catch (err) { console.error('bad snapshot data', err); }
    }
    this.userPresets = o.userPresets || [];
    if (this.onUserPresets) this.onUserPresets(this.userPresets);
    this.dirty.clear(); this.ready = true;
    this.status('HOST SYNC · READY');
  }
  telemetry(o) {
    this.ppq = o.ppq; this.tAt = performance.now(); this.sampleRate = o.sr;
    this.env = o.env; this.rms = o.rms; this.gr = o.gr; this.bands.set(o.bands);
    this.riseProg = o.riseProg; this.riseDone = o.riseDone; this.activeBuild = o.activeBuild;
    this.activity = { mode: o.mode, pos: o.pos, len: o.len, rate: o.rate, gl: o.gl };
    if (this.drops >= 0 && o.drops > this.drops && this.onDrop) this.onDrop();
    this.drops = o.drops;
    if (o.bpm !== this.bpm || o.playing !== this.playing) {
      this.bpm = o.bpm; this.playing = o.playing;
      this.status(`HOST SYNC · ${this.bpm.toFixed(1)} BPM · ${this.playing ? 'PLAYING' : 'STOPPED'}`);
    }
  }
  status(msg) { document.querySelector('#status').textContent = msg; }
  beats() { return this.ppq + (performance.now() - this.tAt) / 1000 * this.bpm / 60; } // the DSP clock runs while stopped too
  flush() {
    if (!this.dirty.size) return;
    const o = {}; for (const id of this.dirty) o[id] = this.S.base[id];
    this.dirty.clear(); this.be.emitEvent('set', o);
  }
  // one host gesture per drag (Cubase gets one undo/automation pass, not one per frame)
  gesture(id, on) { if (!this.ready) return; this.flush(); this.be.emitEvent('gesture', { id, on }); }
  savePreset(name, json) { this.be.emitEvent('savePreset', { name, json }); }
  deletePreset(name) { this.be.emitEvent('deletePreset', { name }); }
  update() {
    if (!this.ready) return;
    this.flush();
    const now = performance.now();
    if (this.S.snapVer !== this.sentSnapVer && now - this.lastSnapSend > 150) {
      this.sentSnapVer = this.S.snapVer; this.lastSnapSend = now;
      this.be.emitEvent('snaps', { snaps: JSON.stringify(this.S.snaps.map(s => s.v)) });
    }
  }
  drop() { this.be.emitEvent('drop', {}); }
  async ensure() {}
  async start() {}
  stop() {}
  async startSource() {}
}
