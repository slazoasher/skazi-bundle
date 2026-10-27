(() => {
  'use strict';
  window.initSiteExperience = function () {
    const audio=document.getElementById('site-audio');
    const artist=document.getElementById('artist-shot');
    const status=document.getElementById('sound-status');
    const unlock=document.querySelector('.audio-unlock');
    const gallery=document.querySelector('.film-gallery');
    const cards=[...gallery.querySelectorAll('.card')];
    const tracks={skazi:{name:'SKAZI · JUNAM',src:'audio/skazi-junam.mp3'},asher:{name:'ASHER SWISSA · IMANI',src:'audio/asher-imani.mp3'}};
    let target=null,token=0,blocked=false;
    audio.volume=.75;
    function clearVisuals(){artist.classList.remove('is-playing');cards.forEach(card=>card.classList.remove('is-previewing'));}
    function stop(){++token;audio.pause();target=null;clearVisuals();}
    async function play(next){
      if(document.hidden)return;
      if(target?.key===next.key&&!audio.paused)return;
      stop();target=next;const request=++token;
      const url=new URL(next.src,document.baseURI).href;
      if(audio.src!==url)audio.src=url;
      else {try{audio.currentTime=0;}catch{}}
      try{
        await audio.play();
        if(request!==token)return;
        blocked=false;unlock.hidden=true;
        if(next.card)next.card.classList.add('is-previewing');else artist.classList.add('is-playing');
        status.textContent=`Playing ${next.name}`;
      }catch(error){
        if(request!==token)return;
        clearVisuals();
        if(error.name==='NotAllowedError'){blocked=true;unlock.hidden=false;status.textContent='Enable sound once, then previews play automatically.';}
        else if(error.name!=='AbortError')status.textContent='This audio preview is unavailable.';
      }
    }
    function retry(){if(blocked&&target){const next=target;play(next);}else if(navigator.userActivation?.hasBeenActive){blocked=false;unlock.hidden=true;}}
    // Hover requests playback directly. A genuine page interaction retries a
    // browser-blocked request; the UI never claims to bypass autoplay policy.
    document.addEventListener('pointerup',retry);
    document.addEventListener('keydown',retry);
    unlock.addEventListener('click',()=>{blocked=false;unlock.hidden=true;if(target)play(target);});
    artist.querySelectorAll('[data-persona]').forEach(zone=>{
      const persona=zone.dataset.persona;
      const enter=()=>{artist.dataset.active=persona;play({key:persona,...tracks[persona]});};
      const leave=()=>{artist.dataset.active='';if(target?.key===persona)stop();};
      zone.addEventListener('pointerenter',enter);
      zone.addEventListener('pointerleave',leave);
      zone.addEventListener('focus',enter);
      zone.addEventListener('blur',leave);
      zone.addEventListener('click',enter);
    });
    cards.forEach(card=>{
      const reduced=matchMedia('(prefers-reduced-motion: reduce)');
      card.addEventListener('pointermove',event=>{
        if(gallery.dataset.view!=='grid'||reduced.matches||event.pointerType==='touch')return;
        const bounds=card.getBoundingClientRect();
        const x=(event.clientX-bounds.left)/bounds.width, y=(event.clientY-bounds.top)/bounds.height;
        card.style.setProperty('--tilt-x',`${(0.5-y)*5}deg`);
        card.style.setProperty('--tilt-y',`${(x-0.5)*6}deg`);
        card.style.setProperty('--glow-x',`${x*100}%`);
        card.style.setProperty('--glow-y',`${y*100}%`);
      });
      card.addEventListener('pointerleave',()=>{card.style.setProperty('--tilt-x','0deg');card.style.setProperty('--tilt-y','0deg');});
    });
    audio.addEventListener('ended',clearVisuals);
    document.addEventListener('visibilitychange',()=>{if(document.hidden)stop();});

    const overlay=document.getElementById('site-intro');
    if(document.documentElement.classList.contains('intro-pending')){
      const main=document.getElementById('main');
      const navigation=document.querySelector('nav');
      const mark=overlay.querySelector('.intro-mark');
      const reduced=matchMedia('(prefers-reduced-motion: reduce)').matches;
      let finished=false,started=false;
      overlay.hidden=false;main.inert=true;if(navigation)navigation.inert=true;
      try{localStorage.setItem('skazi-intro-seen-v1','1');}catch{try{sessionStorage.setItem('skazi-intro-seen-v1','1');}catch{}}
      const cleanup=()=>{if(finished)return;finished=true;document.documentElement.classList.remove('intro-pending');overlay.hidden=true;main.inert=false;if(navigation)navigation.inert=false;};
      const finish=()=>{
        if(started||finished)return;started=true;
        const destination=document.querySelector('.hero-logo .logo-art').getBoundingClientRect();
        const origin=mark.getBoundingClientRect();
        const visible=destination.top>=0&&destination.bottom<=innerHeight;
        const duration=reduced?180:750;
        if(!reduced&&visible){
          mark.style.transformOrigin='0 0';
          mark.animate([{transform:'translate(-50%,-50%)'},{transform:`translate(calc(-50% + ${destination.left-origin.left}px),calc(-50% + ${destination.top-origin.top}px)) scale(${destination.width/origin.width})`}],{duration,easing:'cubic-bezier(.22,.8,.2,1)',fill:'forwards'});
        }
        overlay.animate([{opacity:1},{opacity:0}],{duration,easing:'ease-in',fill:'forwards'}).finished.then(cleanup,cleanup);
      };
      overlay.querySelector('.intro-skip').addEventListener('click',cleanup);
      setTimeout(finish,reduced?150:1500);
      setTimeout(cleanup,4300);
    }
    return {stop};
  };
})();
