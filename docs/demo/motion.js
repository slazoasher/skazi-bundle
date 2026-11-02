'use strict';
(() => {
  const reduced=matchMedia('(prefers-reduced-motion: reduce)');
  const finePointer=matchMedia('(hover: hover) and (pointer: fine)');
  const ease='cubic-bezier(.22,1,.36,1)';
  const animated=new WeakSet();
  const selector='.section-heading,.feature,.tool,.story-copy,.video-card,.download>h2';
  let motionPaused=reduced.matches,openingDone=false;
  const animateEntrance=(el,delay=0)=>{
    if(motionPaused||reduced.matches)return;
    el.animate([{opacity:.96,transform:'translate3d(0,12px,0)'},{opacity:1,transform:'translate3d(0,0,0)'}],{duration:520,delay,easing:ease});
  };
  const reveals=new IntersectionObserver(entries=>{
    for(const entry of entries){
      if(!entry.isIntersecting||!openingDone)continue;
      reveals.unobserve(entry.target);
      if(animated.has(entry.target))continue;
      animated.add(entry.target);
      animateEntrance(entry.target,Number(entry.target.dataset.motionDelay||0));
    }
  },{threshold:.06});
  function observeContent(root=document){
    const elements=[...(root.matches?.(selector)?[root]:[]),...root.querySelectorAll(selector)];
    for(const el of elements){
      if(animated.has(el))continue;
      if(el.matches('.feature,.tool'))el.dataset.motionDelay=String(([...el.parentElement.children].indexOf(el)%3)*45);
      reveals.observe(el);
    }
  }
  const contentObserver=new MutationObserver(records=>{
    if(!openingDone)return;
    for(const record of records)for(const node of record.addedNodes)if(node.nodeType===1)observeContent(node);
  });
  for(const id of ['#catalog','#featured']){const mount=document.querySelector(id);if(mount)contentObserver.observe(mount,{childList:true});}
  const ticker=document.querySelector('.ticker');
  const track=document.createElement('div');track.className='ticker-track';
  const original=ticker.innerHTML;track.innerHTML=original+original;track.setAttribute('aria-hidden','true');
  ticker.removeAttribute('aria-hidden');ticker.replaceChildren(track);
  const motionToggle=document.createElement('button');motionToggle.className='motion-toggle';motionToggle.type='button';ticker.append(motionToggle);
  function setMotion(paused){
    motionPaused=paused;document.body.classList.toggle('motion-paused',paused);
    motionToggle.innerHTML='<svg viewBox="0 0 24 24" aria-hidden="true">'+(paused?'<path d="m8 5 11 7-11 7z"/>':'<rect x="6" y="5" width="4" height="14" rx="1"/><rect x="14" y="5" width="4" height="14" rx="1"/>')+'</svg>';
    motionToggle.setAttribute('aria-label',paused?'Resume visual motion':'Pause visual motion');motionToggle.setAttribute('aria-pressed',String(paused));
    if(paused)for(const animation of document.getAnimations()){
      if(animation.effect?.target?.closest?.('#site-intro'))continue;
      if(animation.effect?.getTiming().iterations!==Infinity)animation.finish();
    }
    document.dispatchEvent(new Event('visual-motion-change'));updateScroll();
  }
  motionToggle.addEventListener('click',()=>setMotion(!motionPaused));
  new IntersectionObserver(entries=>ticker.classList.toggle('in-view',entries[0].isIntersecting)).observe(ticker);
  const links=[...document.querySelectorAll('#main-navigation a')];
  const navSections=[...document.querySelectorAll('#sound,#collection,#story,#download')];
  function updateNavigation(){
    const height=document.querySelector('header').offsetHeight,probe=height+(innerHeight-height)*.35;
    let current='';for(const section of navSections)if(section.getBoundingClientRect().top<=probe)current='#'+section.id;
    for(const link of links){const active=link.hash===current;link.classList.toggle('is-current',active);if(active)link.setAttribute('aria-current','location');else link.removeAttribute('aria-current');}
  }
  const hero=document.querySelector('.hero');
  let scrollFrame=0,heroVisible=true;
  new IntersectionObserver(entries=>{heroVisible=entries[0].isIntersecting;document.body.classList.toggle('hero-in-view',heroVisible);}).observe(hero);
  function updateScroll(){
    scrollFrame=0;updateNavigation();
    const y=scrollY,total=document.documentElement.scrollHeight-innerHeight;
    document.documentElement.style.setProperty('--reading-progress',String(total>0?y/total:0));document.body.classList.toggle('page-scrolled',y>30);
    if(!motionPaused&&!reduced.matches&&finePointer.matches&&heroVisible){hero.style.setProperty('--hero-drift',Math.min(y*.04,24)+'px');hero.style.setProperty('--logo-drift',Math.min(y*.018,10)+'px');}
  }
  function requestScrollUpdate(){if(!scrollFrame)scrollFrame=requestAnimationFrame(updateScroll);}
  addEventListener('scroll',requestScrollUpdate,{passive:true});addEventListener('resize',requestScrollUpdate);
  new ResizeObserver(requestScrollUpdate).observe(document.querySelector('main'));
  Promise.resolve(window.skaziOpeningReady).then(()=>{
    openingDone=true;
    document.querySelectorAll('.hero-wordmark,.hero-copy>.eyebrow,.hero-copy>h1,.hero-copy>.intro,.hero-copy>.actions,.hero-facts').forEach((el,i)=>{
      if(el.getBoundingClientRect().bottom>0&&el.getBoundingClientRect().top<innerHeight)animateEntrance(el,i*45);
    });
    observeContent();requestScrollUpdate();
  });
  setMotion(motionPaused);
  reduced.addEventListener('change',()=>setMotion(reduced.matches));
})();
