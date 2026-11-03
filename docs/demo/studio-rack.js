(() => {
  const panel = document.querySelector('#panel-sound');
  if (!panel) return;

  const names = { kick: 'KICKASS', bass: 'SUBICH', acid: 'SKACID', space: 'Space', fx: 'Movement' };
  const level = ['level', 'Level', 0, 100];
  const pan = ['pan', 'Pan', -100, 100, 0];
  const tune = ['tune', 'Tune', -12, 12, 0];
  const gate = ['gate', 'Gate', 25, 150, 100];
  const drive = ['drive', 'Drive', 0, 100, 28];
  const send = ['send', 'Echo send', 0, 100, 32];
  const feedback = ['feedback', 'Feedback', 0, 70, 31];
  const parameters = {
    kick: [level, pan, tune, gate],
    bass: [level, pan, tune, gate, drive],
    acid: [level, pan, tune, gate, drive, ['tone', 'Cutoff', 0, 100, 54], send, feedback],
    space: [level, pan, tune, gate, send, feedback],
    fx: [level, pan, ['gate', 'Decay', 25, 150, 100]]
  };

  const rack = document.createElement('section');
  rack.className = 'track-rack';
  rack.setAttribute('aria-labelledby', 'track-rack-title');
  rack.innerHTML = '<div class="track-rack-header"><h3 id="track-rack-title"></h3><button type="button" class="track-rack-reset">Reset channel</button></div><div class="track-rack-controls"></div>';
  const defaults = document.createElement('details');
  defaults.className = 'rack-defaults';
  const summary = document.createElement('summary');
  summary.textContent = 'Sound defaults';
  defaults.append(summary, panel.querySelector('.studio-shaping'));
  const reference = document.createElement('details');
  reference.className = 'rack-defaults';
  const referenceSummary = document.createElement('summary');
  referenceSummary.textContent = 'Plugin reference';
  reference.append(referenceSummary, ...panel.querySelectorAll('[data-device]'));
  panel.prepend(rack);
  panel.append(defaults, reference);

  const controls = rack.querySelector('.track-rack-controls');
  let track = 'bass';

  function valueFor(key, fallback) {
    if (key === 'level') return Math.round(channelLevels[track] * 100);
    if (key === 'send') fallback = studioControls.space;
    if (key === 'drive' || key === 'tone') fallback = studioControls[key];
    return trackSettings[track]?.[key] ?? fallback;
  }

  function format(key, value) {
    if (key === 'level') return value ? (40 * Math.log10(value / 100)).toFixed(1) + ' dB' : '−∞ dB';
    if (key === 'pan') return value === 0 ? 'Center' : Math.abs(value) + (value < 0 ? ' L' : ' R');
    if (key === 'tune') return (value > 0 ? '+' : '') + value + ' st';
    if (key === 'tone') return Math.round(300 + Math.pow(value / 100, 1.7) * 10000) + ' Hz';
    return value + '%';
  }

  function changeLevel(value) {
    const slider = document.querySelector('[data-level="' + track + '"]');
    slider.value = value;
    slider.dispatchEvent(new Event('input', { bubbles: true }));
  }

  function apply() {
    studioEngine?.setTrack(track, trackSettings[track]);
    const panner = channelPanners[track];
    if (panner) panner.pan.setTargetAtTime((trackSettings[track]?.pan ?? 0) / 100, ctx.currentTime, .025);
  }

  function updateLabel(input, value) {
    const label = format(input.dataset.rack, value);
    input.setAttribute('aria-valuetext', label);
    input.parentElement.querySelector('output').textContent = label;
  }

  function render() {
    track = document.querySelector('[data-select-track][aria-pressed="true"]')?.dataset.selectTrack ?? 'bass';
    rack.querySelector('h3').textContent = names[track];
    controls.replaceChildren();
    for (const [key, label, min, max, initial] of parameters[track]) {
      const field = document.createElement('label');
      field.innerHTML = '<span></span><output></output><input type="range" step="1">';
      field.firstElementChild.textContent = label;
      const input = field.querySelector('input');
      input.setAttribute('aria-label', label);
      input.dataset.rack = key;
      input.min = min;
      input.max = max;
      input.value = valueFor(key, initial);
      updateLabel(input, Number(input.value));
      input.addEventListener('input', () => {
        const value = Number(input.value);
        if (key === 'level') changeLevel(value);
        else {
          (trackSettings[track] ??= {})[key] = value;
          apply();
        }
        updateLabel(input, value);
      });
      controls.append(field);
    }
  }

  function refresh() {
    for (const [key, , , , initial] of parameters[track]) {
      const input = controls.querySelector('[data-rack="' + key + '"]');
      const value = valueFor(key, initial);
      input.value = value;
      updateLabel(input, value);
    }
  }

  rack.querySelector('.track-rack-reset').addEventListener('click', () => {
    delete trackSettings[track];
    changeLevel(80);
    apply();
    refresh();
  });
  document.addEventListener('studio-track-selected', render);
  document.querySelectorAll('[data-level], .studio-shaping [data-shape]').forEach(input => input.addEventListener('input', refresh));
  render();

  const message = document.querySelector('#drop-status');
  const routineMessages = new Set([
    'Press Play to start. Select a clip to edit its track.',
    'Add layers yourself, or let the energy build.',
    'Native KICKASS · SUBICH · SKACID voices ready.',
    'Paused. Resume continues here; Stop returns to the beginning.',
    'Resumed. Keep shaping your sound.'
  ]);
  function hideRoutineMessage() {
    const text = message.textContent;
    message.classList.toggle('sr', routineMessages.has(text) || text.endsWith(' loaded. Make it yours.'));
  }
  new MutationObserver(hideRoutineMessage).observe(message, { childList: true, characterData: true, subtree: true });
  hideRoutineMessage();
})();
