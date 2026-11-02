'use strict';
// This promise lets the page entrance begin after the opening has cleared.
window.skaziOpeningReady = new Promise(resolve => {
  const html = document.documentElement;
  const overlay = document.querySelector('#site-intro');
  if (!html.classList.contains('intro-pending') || !overlay) { resolve(); return; }
  const reduced = matchMedia('(prefers-reduced-motion: reduce)');
  const skip = overlay.querySelector('.intro-skip');
  const mark = overlay.querySelector('.intro-mark');
  const heroImage = document.querySelector('.hero-wordmark img');
  const background = [...document.querySelectorAll('body>header,body>main,body>footer,body>.skip')];
  const previousInert = background.map(el => el.inert);
  let complete = false, exiting = false;
  const animations = [];
  const timers = [];
  const cleanup = () => {
    if (complete) return;
    complete = true;
    timers.forEach(clearTimeout);
    clearTimeout(window.skaziOpeningFailsafe);
    html.classList.remove('intro-pending');
    overlay.hidden = true;
    background.forEach((el,i) => { el.inert = previousInert[i]; });
    animations.forEach(animation => animation.cancel());
    try { sessionStorage.setItem('skazi-demo-intro-30','1'); } catch {}
    if (overlay.contains(document.activeElement)) document.querySelector('main').focus({preventScroll:true});
    resolve();
    document.dispatchEvent(new Event('site-intro-complete'));
  };
  const finish = () => {
    if (complete || exiting) return;
    exiting = true;
    if (reduced.matches) { cleanup(); return; }
    const box = heroImage.getBoundingClientRect();
    const ratio = heroImage.naturalWidth / heroImage.naturalHeight;
    const width = Math.min(box.width, box.height * ratio);
    const height = width / ratio;
    const target = {left:box.left,top:box.top+(box.height-height)/2,width,height};
    const origin = mark.getBoundingClientRect();
    const visible = Number.isFinite(width) && width>0 && target.top>=0 && target.top+height<=innerHeight;
    if (visible) {
      Object.assign(mark.style,{left:origin.left+'px',top:origin.top+'px',width:origin.width+'px',transform:'none',transformOrigin:'0 0'});
      animations.push(mark.animate([
        {transform:'translate3d(0,0,0) scale(1)'},
        {transform:`translate3d(${target.left-origin.left}px,${target.top-origin.top}px,0) scale(${width/origin.width})`}
      ],{duration:900,easing:'cubic-bezier(.22,1,.36,1)',fill:'forwards'}));
      animations.push(heroImage.animate([{opacity:0},{opacity:1}],{duration:500,delay:400,easing:'ease-out',fill:'forwards'}));
    }
    animations.push(overlay.querySelector('.intro-line').animate([{opacity:1},{opacity:0}],{duration:180,fill:'forwards'}));
    const fade = overlay.animate([{opacity:1},{opacity:0}],{duration:900,easing:'cubic-bezier(.22,1,.36,1)',fill:'forwards'});
    animations.push(fade);
    fade.finished.then(cleanup,cleanup);
  };
  overlay.hidden = false;
  background.forEach(el => { el.inert = true; });
  overlay.focus({preventScroll:true});
  skip.addEventListener('click',cleanup);
  overlay.addEventListener('keydown',event => {
    if (event.key==='Escape') { event.preventDefault(); cleanup(); }
    if (event.key==='Tab') { event.preventDefault(); skip.focus(); }
  });
  document.addEventListener('visibilitychange',() => { if (document.hidden) cleanup(); });
  reduced.addEventListener('change',() => { if (reduced.matches) cleanup(); });
  timers.push(setTimeout(finish,2100),setTimeout(cleanup,4000));
});
