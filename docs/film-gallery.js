/* Velocity-driven film reel, implemented for this buildless site.
   No Motion+ source or runtime is required. */
(() => {
  'use strict';

  window.initPluginFilmGallery = function initPluginFilmGallery(root, onMove = () => {}, onSettle = () => {}) {
    if (!root || root.dataset.ready === 'true') return;
    const viewport = root.querySelector('.film-viewport');
    const track = root.querySelector('#plugin-reel');
    const allCards = [...track.querySelectorAll('.card')];
    if (!allCards.length) return;
    const gridSizes = allCards.map(card => card.querySelector('img').sizes);
    // The reel runs on the cards the filter leaves visible; these are rebound by refresh().
    let cards = allCards, pictures = [], names = [], tilts = [], total = 0;
    const meta = root.querySelector('.film-meta');
    const count = root.querySelector('.film-count');
    const title = root.querySelector('.film-title');
    const subtitle = root.querySelector('.film-subtitle');
    const description = root.querySelector('.film-description');
    const announcement = root.querySelector('.film-announcement');
    const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
    const kicker = root.querySelector('.film-kicker');
    const presets = root.querySelector('.film-presets');
    const changelogField = root.querySelector('[data-film-changelog]');
    const changelogSection = root.querySelector('[data-film-changelog-section]');
    const descriptionField = root.querySelector('[data-film-description]');
    const chipsField = root.querySelector('[data-film-chips]');
    function bind() {
      cards = allCards.filter(card => !card.hidden);
      if (!cards.length) cards = allCards;
      pictures = cards.map(card => card.querySelector('.pic'));
      names = cards.map(card => card.querySelector('h3').textContent.trim());
      tilts = cards.map(() => 0);
      total = cards.length;
      allCards.forEach(card => { card.style.visibility = cards.includes(card) ? '' : 'hidden'; });
    }
    bind();
    const modulo = (value, size) => ((value % size) + size) % size;
    const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
    let width = 0, step = 0, cycle = 0, position = 0, target = 0, velocity = 0;
    let selected = 0, frame = 0, lastFrame = 0, pointer = null;
    let wheelActive = false, wheelTimer = 0, lastWheel = 0, suppressClickUntil = 0;
    let moving = false;
    let view = 'grid';

    root.dataset.ready = 'true';
    root.dataset.view = view;
    root.closest('#plugins')?.classList.add('film-enhanced');
    root.setAttribute('role', 'region');
    root.setAttribute('aria-roledescription', 'carousel');
    viewport.tabIndex = 0;
    viewport.setAttribute('aria-label', 'Plugin gallery. Drag or use the left and right arrow keys.');
    meta.hidden = false;
    cards.forEach((card, index) => {
      card.setAttribute('role', 'group');
      card.setAttribute('aria-roledescription', 'slide');
      card.setAttribute('aria-label', `${index + 1} of ${total}: ${names[index]}`);
      card.querySelector('img').draggable = false;
    });

    function setMoving(value) {
      if (moving === value) return;
      moving = value;
      root.dataset.moving = String(value);
      if (value) onMove();
    }

    function select(index, announce = true) {
      const previous = selected;
      selected = modulo(index, total);
      // Keep keyboard focus out of a slide that is about to become hidden.
      if (previous !== selected && cards[previous].contains(document.activeElement)) {
        viewport.focus({ preventScroll: true });
      }
      cards.forEach((card, i) => {
        const active = i === selected;
        card.classList.toggle('film-active', active);
        card.setAttribute('aria-roledescription',view==='reel'?'slide':'plugin');
        if(view==='reel')card.setAttribute('aria-hidden', String(!active));else card.removeAttribute('aria-hidden');
        card.querySelectorAll('img.zoom, button').forEach(control => { control.tabIndex = view==='grid'||active ? 0 : -1; });
        const distance = Math.min(modulo(i - selected, total), modulo(selected - i, total));
        if (distance <= 1) card.querySelector('img').loading = 'eager';
      });
      count.replaceChildren(document.createTextNode(String(selected + 1).padStart(2, '0') + ' '));
      const denominator = document.createElement('span');
      denominator.textContent = `/ ${total}`;
      count.append(denominator);
      const card = cards[selected];
      title.innerHTML = card.querySelector('h3').innerHTML;
      subtitle.textContent = card.querySelector('.sub').textContent;
      const badge = card.querySelector('.badge');
      if (kicker) kicker.textContent = badge ? badge.firstChild.textContent.trim() : '';
      if (presets) presets.textContent = badge?.querySelector('i')?.textContent || '';
      const changelog = card.querySelector('.card-changelog')?.textContent.trim() || '';
      if (changelogField) { changelogField.textContent = changelog; changelogSection.hidden = !changelog; }
      if (descriptionField) descriptionField.textContent = card.querySelector('.body > p')?.textContent || '';
      if (chipsField) chipsField.replaceChildren(...[...card.querySelectorAll('.chips .chip:not(.more)')].map(chip => { const copy = chip.cloneNode(true); copy.hidden = false; return copy; }));
      if (announce) announcement.textContent = `${selected + 1} of ${total}: ${names[selected]}`;
    }

    function render() {
      if (!step || view==='grid') return;
      cards.forEach((card, i) => {
        const offset = modulo(i * step + position + cycle / 2, cycle) - cycle / 2;
        card.style.transform = `translate3d(${offset.toFixed(3)}px,0,0)`;
        card.style.visibility = Math.abs(offset) < viewport.clientWidth / 2 + width ? 'visible' : 'hidden';
        const tilt = reducedMotion.matches ? 0 : tilts[i];
        pictures[i].style.transform = `perspective(1100px) rotateY(${tilt.toFixed(3)}deg) rotateZ(${(-tilt * .09).toFixed(3)}deg)`;
      });
    }

    function settle() {
      const shouldPreview = moving;
      cancelAnimationFrame(frame);
      frame = 0;
      position = target = -selected * step;
      velocity = 0;
      tilts.fill(0);
      render();
      setMoving(false);
      if(shouldPreview && view==='reel')onSettle(cards[selected]);
    }

    function tick(now) {
      frame = 0;
      const elapsed = Math.min((now - lastFrame) / 1000 || 1 / 60, .04);
      lastFrame = now;
      const held = pointer?.dragging || wheelActive;
      if (!held) {
        // Small integration steps keep the spring stable on slower devices.
        const steps = Math.ceil(elapsed / (1 / 120));
        const dt = elapsed / steps;
        for (let i = 0; i < steps; i++) {
          velocity += ((target - position) * 180 - velocity * 25) * dt;
          position += velocity * dt;
        }
      } else if (now - (pointer?.lastTime || lastWheel) > 40) {
        velocity *= Math.exp(-elapsed * 18);
      }
      const lean = reducedMotion.matches ? 0 : clamp(velocity * .014, -35, 35);
      tilts.forEach((angle, i) => {
        const distance = Math.min(modulo(i - selected, total), modulo(selected - i, total));
        const lag = .055 + Math.min(distance, 3) * .025;
        tilts[i] += (lean - angle) * (1 - Math.exp(-elapsed / lag));
      });
      render();
      const atRest = !held && Math.abs(target - position) < .15 && Math.abs(velocity) < 2 && tilts.every(angle => Math.abs(angle) < .04);
      if (atRest) settle();
      else frame = requestAnimationFrame(tick);
    }

    function animate() {
      if (frame) return;
      lastFrame = performance.now();
      frame = requestAnimationFrame(tick);
    }

    function goTo(logicalIndex, keepVelocity = false) {
      if (!step) return;
      clearTimeout(wheelTimer);
      wheelActive = false;
      target = -logicalIndex * step;
      select(logicalIndex);
      if (!keepVelocity) velocity = 0;
      setMoving(true);
      if (reducedMotion.matches) settle();
      else animate();
    }

    function snap(projectMomentum = true) {
      const projected = position + (projectMomentum && !reducedMotion.matches ? velocity * .17 : 0);
      goTo(Math.round(-projected / step), projectMomentum);
    }

    function releaseCapture(id) {
      if (viewport.hasPointerCapture(id)) viewport.releasePointerCapture(id);
    }

    viewport.addEventListener('pointerdown', event => {
      if(view!=='reel')return;
      if (!event.isPrimary || event.button !== 0 || pointer || event.target.closest('button, a')) return;
      clearTimeout(wheelTimer);
      wheelActive = false;
      cancelAnimationFrame(frame);
      frame = 0;
      velocity = 0;
      pointer = { id: event.pointerId, startX: event.clientX, startY: event.clientY,
        origin: position, lastX: event.clientX, lastTime: performance.now(), dragging: false };
    });

    viewport.addEventListener('pointermove', event => {
      if (!pointer || pointer.id !== event.pointerId) return;
      const dx = event.clientX - pointer.startX;
      const dy = event.clientY - pointer.startY;
      if (!pointer.dragging) {
        if (Math.abs(dy) > 8 && Math.abs(dy) > Math.abs(dx)) {
          pointer = null;
          snap(false);
          return;
        }
        if (Math.abs(dx) < 6) return;
        pointer.dragging = true;
        viewport.setPointerCapture(event.pointerId);
        root.dataset.dragging = 'true';
        setMoving(true);
      }
      if (event.cancelable) event.preventDefault();
      const now = performance.now();
      const dt = Math.max(now - pointer.lastTime, 8) / 1000;
      const measured = clamp((event.clientX - pointer.lastX) / dt, -3500, 3500);
      velocity = velocity * .35 + measured * .65;
      position = pointer.origin + dx;
      pointer.lastX = event.clientX;
      pointer.lastTime = now;
      render();
      animate();
    });

    function finishPointer(event, cancelled = false) {
      if (!pointer || pointer.id !== event.pointerId) return;
      const finished = pointer;
      pointer = null;
      root.dataset.dragging = 'false';
      releaseCapture(finished.id);
      if (finished.dragging) {
        suppressClickUntil = performance.now() + 350;
        if (cancelled || performance.now() - finished.lastTime > 90) velocity = 0;
        snap(!cancelled);
      } else if (Math.abs(position - target) > .15 || moving) {
        animate();
      }
    }
    // Window listeners also handle a press released outside the viewport before capture.
    window.addEventListener('pointerup', event => finishPointer(event));
    window.addEventListener('pointercancel', event => finishPointer(event, true));
    viewport.addEventListener('lostpointercapture', event => finishPointer(event, true));
    viewport.addEventListener('dragstart', event => event.preventDefault());
    viewport.addEventListener('click', event => {
      if(view!=='reel')return;
      if (event.detail > 0 && performance.now() < suppressClickUntil) {
        event.preventDefault();
        event.stopImmediatePropagation();
        return;
      }
      const index = cards.indexOf(event.target.closest('.card'));
      if (index < 0 || index === selected) return;
      event.preventDefault();
      event.stopImmediatePropagation();
      const logical = Math.round(-position / step);
      const delta = modulo(index - modulo(logical, total) + Math.floor(total / 2), total) - Math.floor(total / 2);
      goTo(logical + delta);
    }, true);

    viewport.addEventListener('wheel', event => {
      if(view!=='reel')return;
      if (event.ctrlKey || pointer) return;
      const horizontal = event.shiftKey && !event.deltaX ? event.deltaY : event.deltaX;
      if (!horizontal || (!event.shiftKey && Math.abs(event.deltaY) > Math.abs(horizontal))) return;
      event.preventDefault();
      const multiplier = event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? viewport.clientWidth : 1;
      const delta = horizontal * multiplier;
      const now = performance.now();
      const dt = Math.max(8, Math.min(50, now - lastWheel)) / 1000;
      velocity = clamp(-delta / dt, -3000, 3000);
      lastWheel = now;
      position -= delta;
      wheelActive = true;
      setMoving(true);
      render();
      animate();
      clearTimeout(wheelTimer);
      wheelTimer = setTimeout(() => { wheelActive = false; snap(false); }, 110);
    }, { passive: false });

    root.querySelector('.film-prev').addEventListener('click', () => goTo(Math.round(-target / step) - 1));
    root.querySelector('.film-next').addEventListener('click', () => goTo(Math.round(-target / step) + 1));
    viewport.addEventListener('keydown', event => {
      if(view!=='reel')return;
      if (event.altKey || event.ctrlKey || event.metaKey) return;
      const logical = Math.round(-target / step);
      const destinations = { ArrowLeft: logical - 1, ArrowRight: logical + 1, Home: 0, End: total - 1 };
      if (!(event.key in destinations)) return;
      event.preventDefault();
      goTo(destinations[event.key]);
    });

    function measure() {
      pointer = null;
      root.dataset.dragging = 'false';
      wheelActive = false;
      clearTimeout(wheelTimer);
      if(view==='grid'){cancelAnimationFrame(frame);frame=0;setMoving(false);return;}
      width = cards[0].offsetWidth;
      step = width + parseFloat(getComputedStyle(track).columnGap || 0);
      cycle = total * step;
      settle();
    }
    new ResizeObserver(measure).observe(viewport);
    reducedMotion.addEventListener('change', () => { if (reducedMotion.matches) measure(); });
    document.addEventListener('visibilitychange', () => { if (document.hidden) measure(); });
    select(0, false);
    root.refresh = function () {
      bind();
      allCards.forEach(card => { card.style.removeProperty('transform'); card.querySelector('.pic').style.removeProperty('transform'); });
      selected = 0; position = target = 0;
      select(0, false); measure();
    };
    function changeView(next) {
      onMove();
      view=next;root.dataset.view=view;
      root.setAttribute('aria-roledescription',view==='reel'?'carousel':'collection');
      viewport.tabIndex=view==='reel'?0:-1;
      viewport.setAttribute('aria-label',view==='reel'?'Plugin gallery. Drag or use the left and right arrow keys.':'Plugin collection');
      meta.hidden=view==='grid';
      document.querySelectorAll('[data-gallery-view]').forEach(button=>button.setAttribute('aria-pressed',String(button.dataset.galleryView===view)));
      allCards.forEach((card,i)=>{card.querySelector('img').sizes=view==='grid'?gridSizes[i]:'(max-width:900px) 90vw, 740px';card.style.removeProperty('transform');card.style.removeProperty('visibility');card.querySelector('.pic').style.removeProperty('transform');});
      bind();select(Math.min(selected,total-1),false);measure();
    }
    document.querySelectorAll('[data-gallery-view]').forEach(button=>button.addEventListener('click',()=>changeView(button.dataset.galleryView)));
    changeView('grid');
  };
})();
