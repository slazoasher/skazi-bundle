(() => {
  const advanced = document.querySelector('#studio-advanced-toggle');
  if (!advanced) return;

  const instruments = {
    bass: { name: 'SUBICH', base: 33 },
    acid: { name: 'SKACID', base: 57 },
    space: { name: 'Space', base: 57 },
    kick: { name: 'KICKASS', pads: [['Soft kick', 0, .38], ['Kick', 0, .7], ['Hard kick', 0, .95]] },
    fx: { name: 'Movement', pads: [['Closed hat', 1, .7], ['Open hat', 2, .8], ['Clap', 4, .85]] }
  };
  const pitchNames = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];
  const shortcuts = ['A', 'W', 'S', 'E', 'D', 'F', 'T', 'G', 'Y', 'H', 'U', 'J', 'K', 'O', 'L', 'P'];
  const notes = new Map();
  let track = 'bass';
  let octave = 0;
  let audio, engine, output, previewPan, meter, waveform, frame = 0;
  let visible = true;
  let loading = false;

  const surface = document.createElement('section');
  surface.className = 'studio-keys';
  surface.setAttribute('aria-labelledby', 'studio-keys-toggle');
  surface.innerHTML = `
    <button class="keys-toggle" id="studio-keys-toggle" type="button" aria-expanded="false" aria-controls="studio-keys-panel">
      <svg viewBox="0 0 24 24" aria-hidden="true"><rect x="3" y="5" width="18" height="14" rx="2"/><path d="M9 5v14m6-14v14M7 5v7h2m6 0h2V5"/></svg>
      <span>Keys <small></small></span>
      <span class="keys-toggle-device">SUBICH</span>
      <svg class="keys-chevron" viewBox="0 0 24 24" aria-hidden="true"><path d="m8 10 4 4 4-4"/></svg>
    </button>
    <div class="keys-panel" id="studio-keys-panel" hidden>
      <div class="keys-console">
        <div class="keys-instrument"><strong>SUBICH</strong></div>
        <canvas class="keys-waveform" width="264" height="56" aria-hidden="true"></canvas>
        <div class="keys-octaves" aria-label="Keyboard octave" role="group">
          <button type="button" data-octave="-1" aria-label="Lower octave">−</button>
          <output aria-label="Keyboard range"></output>
          <button type="button" data-octave="1" aria-label="Raise octave">+</button>
        </div>
      </div>
      <div class="keys-bed" tabindex="0" role="group" aria-label="Playable keyboard" aria-describedby="studio-keys-hint"></div>
      <div class="keys-footer"><span id="studio-keys-hint"></span><span class="keys-status" role="status"></span></div>
    </div>`;
  advanced.before(surface);
  const toggle = surface.querySelector('.keys-toggle');
  const panel = surface.querySelector('.keys-panel');
  const bed = surface.querySelector('.keys-bed');
  const status = surface.querySelector('.keys-status');
  const canvas = surface.querySelector('.keys-waveform');
  const compact = matchMedia('(max-width: 650px)');
  const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
  const voices = new Set();

  function pitchName(note) {
    return pitchNames[note % 12] + (Math.floor(note / 12) - 1);
  }

  function firstNote() {
    const base = instruments[track].base;
    return base - base % 12 + octave * 12;
  }

  function paintWave() {
    frame = 0;
    const paint = canvas.getContext('2d');
    const { width, height } = canvas;
    paint.clearRect(0, 0, width, height);
    paint.beginPath();
    const active = voices.size > 0 && notes.size > 0 && visible && !panel.hidden;
    if (active) {
      meter.getFloatTimeDomainData(waveform);
      for (let i = 0; i < waveform.length; i++) {
        const x = i * width / (waveform.length - 1);
        const y = height / 2 - Math.max(-1, Math.min(1, waveform[i] * 2)) * height * .44;
        if (i === 0) paint.moveTo(x, y);
        else paint.lineTo(x, y);
      }
    } else {
      paint.moveTo(0, height / 2);
      paint.lineTo(width, height / 2);
    }
    paint.strokeStyle = active ? '#ff454e' : '#666';
    paint.lineWidth = 2;
    paint.stroke();
    if (active && !reducedMotion.matches && !document.body.classList.contains('motion-paused')) {
      frame = requestAnimationFrame(paintWave);
    }
  }

  function renderKeys() {
    const instrument = instruments[track];
    surface.querySelector('.keys-toggle-device').textContent = instrument.name;
    surface.querySelector('.keys-instrument strong').textContent = instrument.name;
    surface.querySelector('.keys-octaves').hidden = !!instrument.pads;
    bed.replaceChildren();
    bed.classList.toggle('is-pads', !!instrument.pads);
    if (instrument.pads) {
      instrument.pads.forEach(([name], i) => addKey(bed, i, name, 'keys-pad'));
    } else {
      const start = firstNote();
      const octaves = compact.matches ? 1 : 2;
      for (let group = 0; group < octaves; group++) {
        const row = document.createElement('div');
        row.className = 'keys-octave';
        let white = 0;
        for (let i = 0; i < 12; i++) {
          const offset = group * 12 + i;
          const sharp = pitchNames[i].includes('♯');
          const key = addKey(row, offset, pitchName(start + offset), sharp ? 'keys-black' : 'keys-white');
          if (sharp) key.style.setProperty('--key-position', white);
          else white++;
        }
        bed.append(row);
      }
      surface.querySelector('output').textContent = pitchName(start) + '–' + pitchName(start + octaves * 12 - 1);
      surface.querySelector('[data-octave="-1"]').disabled = start <= 24;
      surface.querySelector('[data-octave="1"]').disabled = start + 24 > 96 - 12;
    }
    const hint = surface.querySelector('#studio-keys-hint');
    hint.textContent = instrument.pads
      ? 'Focus the pads to play with your computer keyboard.'
      : 'Focus the piano to play with your computer keyboard.';
    status.textContent = 'Ready to audition';
  }

  function addKey(parent, index, name, className) {
    const key = document.createElement('button');
    key.type = 'button';
    key.tabIndex = -1;
    key.className = className;
    key.dataset.key = index;
    key.setAttribute('aria-label', name);
    const shortcut = (instruments[track].pads ? ['A', 'S', 'D'] : shortcuts)[index];
    if (shortcut) {
      const badge = document.createElement('kbd');
      badge.className = 'keys-keycap';
      badge.textContent = shortcut;
      badge.setAttribute('aria-hidden', 'true');
      key.setAttribute('aria-keyshortcuts', shortcut);
      key.append(badge);
    }
    key.append(document.createTextNode(name));
    parent.append(key);
    return key;
  }

  async function start(id, key, tap = false) {
    if (!key || notes.has(id) || notes.size >= 8 || panel.hidden) return;
    const index = Number(key.dataset.key);
    const instrument = instruments[track];
    const pad = instrument.pads?.[index];
    const event = { track, note: pad ? 33 : firstNote() + index, velocity: pad?.[2] ?? .82, length: 1.75 };
    const state = { key, sources: [], gain: null };
    notes.set(id, state);
    key.classList.add('is-down');
    stopPreview();
    stopCompare();
    try {
      if (!audio) {
        // The transport suspends its own context on Pause. Audition can still play.
        audio = new AudioContext();
        output = audio.createGain();
        previewPan = audio.createStereoPanner();
        output.connect(previewPan);
        meter = SkaziStudio.mastering(audio, previewPan, audio.destination);
        meter.fftSize = 1024;
        waveform = new Float32Array(meter.fftSize);
        engine = SkaziStudio.create(audio, {}, voices);
      }
      const needsBank = ['kick', 'bass', 'acid'].includes(event.track);
      if (needsBank && !globalThis.SkaziNativeVoices.ready()) {
        loading = true;
        status.textContent = 'Loading original voices…';
      }
      await Promise.all([audio.resume(), needsBank && SkaziStudio.loadVoices(audio)]);
      if (loading && globalThis.SkaziNativeVoices.ready()) {
        loading = false;
        status.textContent = 'Ready to audition';
      }
      if (notes.get(id) !== state) return;
      output.gain.setTargetAtTime(studioControls.master / 100 * channelLevels[event.track] ** 2 * .65, audio.currentTime, .01);
      previewPan.pan.setTargetAtTime((trackSettings[event.track]?.pan ?? 0) / 100, audio.currentTime, .01);
      for (const name of ['drive', 'tone']) engine.set(name, studioControls[name]);
      engine.setTrack(event.track, trackSettings[event.track]);
      state.gain = audio.createGain();
      state.gain.connect(output);
      const previous = new Set(voices);
      try {
        engine.play(event, audio.currentTime + .004, beat, pad?.[1] ?? 0, state.gain);
      } finally {
        state.sources = [...voices].filter(source => !previous.has(source));
      }
      status.textContent = 'Playing ' + key.getAttribute('aria-label');
      if (!frame) frame = requestAnimationFrame(paintWave);
      if (tap) state.releaseTimer = setTimeout(() => release(id), 320);
    } catch {
      const current = notes.get(id) === state;
      if (current) release(id);
      if (current || loading) status.textContent = 'Audio unavailable. Try a key again.';
      loading = false;
    }
  }

  function release(id) {
    const state = notes.get(id);
    if (!state) return;
    notes.delete(id);
    clearTimeout(state.releaseTimer);
    if (![...notes.values()].some(note => note.key === state.key)) state.key.classList.remove('is-down');
    if (state.gain) {
      const now = audio.currentTime;
      state.gain.gain.setTargetAtTime(0, now, .004);
      for (const source of state.sources) {
        try { source.stop(now + .024); } catch { /* Voice already ended. */ }
      }
      setTimeout(() => state.gain.disconnect(), 40);
    }
    if (!notes.size) {
      if (!loading) status.textContent = 'Ready to audition';
      cancelAnimationFrame(frame);
      paintWave();
    }
  }

  function releaseAll() {
    for (const id of notes.keys()) release(id);
  }

  function selectTrack() {
    releaseAll();
    track = document.querySelector('[data-select-track][aria-pressed="true"]')?.dataset.selectTrack ?? 'bass';
    octave = 0;
    renderKeys();
  }

  toggle.addEventListener('click', () => {
    const open = panel.hidden;
    panel.hidden = !open;
    toggle.setAttribute('aria-expanded', String(open));
    if (open) {
      bed.focus({ preventScroll: true });
      panel.scrollIntoView({ block: 'nearest', behavior: 'instant' });
    } else releaseAll();
  });
  surface.querySelectorAll('[data-octave]').forEach(button => button.addEventListener('click', () => {
    releaseAll();
    octave += Number(button.dataset.octave);
    renderKeys();
    if (button.disabled) bed.focus({ preventScroll: true });
  }));

  bed.addEventListener('pointerdown', event => {
    const key = event.target.closest('[data-key]');
    if (!key || event.button !== 0 || instruments[track].pads) return;
    key.setPointerCapture(event.pointerId);
    void start('pointer-' + event.pointerId, key);
  });
  for (const type of ['pointerup', 'pointercancel', 'lostpointercapture']) {
    bed.addEventListener(type, event => release('pointer-' + event.pointerId));
  }
  bed.addEventListener('click', event => {
    const key = event.target.closest('[data-key]');
    if (key && (event.detail === 0 || instruments[track].pads)) {
      const id = 'activate-' + key.dataset.key;
      release(id);
      void start(id, key, true);
    }
  });
  bed.addEventListener('keydown', event => {
    if (event.ctrlKey || event.metaKey || event.altKey || event.isComposing) return;
    if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') {
      event.preventDefault();
      const keys = [...bed.querySelectorAll('[data-key]')];
      const current = keys.indexOf(document.activeElement);
      const next = current < 0 ? 0 : current + (event.key === 'ArrowLeft' ? -1 : 1);
      keys[Math.max(0, Math.min(next, keys.length - 1))]?.focus({ preventScroll: true });
      return;
    }
    const mapping = instruments[track].pads ? ['A', 'S', 'D'] : shortcuts;
    const index = mapping.indexOf(event.code.replace(/^Key/, ''));
    if (index < 0) return;
    const key = bed.querySelector('[data-key="' + index + '"]');
    if (!key) return;
    event.preventDefault();
    if (!event.repeat) void start(event.code, key);
  });
  bed.addEventListener('keyup', event => release(event.code));
  bed.addEventListener('focusout', event => {
    if (!bed.contains(event.relatedTarget)) releaseAll();
  });
  surface.addEventListener('keydown', event => {
    if (event.key !== 'Escape' || panel.hidden) return;
    event.preventDefault();
    toggle.click();
    toggle.focus({ preventScroll: true });
  });
  document.addEventListener('studio-track-selected', selectTrack);
  document.addEventListener('studio-session-changed', releaseAll);
  document.addEventListener('studio-audio-stopped', releaseAll);
  document.addEventListener('studio-audio-paused', releaseAll);
  document.addEventListener('visual-motion-change', () => {
    cancelAnimationFrame(frame);
    paintWave();
  });
  window.addEventListener('blur', releaseAll);
  compact.addEventListener('change', () => {
    const hadFocus = bed.contains(document.activeElement);
    releaseAll();
    renderKeys();
    if (hadFocus) bed.focus({ preventScroll: true });
  });
  new IntersectionObserver(([entry]) => {
    visible = entry.isIntersecting;
    if (!visible) releaseAll();
  }).observe(surface);

  selectTrack();
  paintWave();
})();
