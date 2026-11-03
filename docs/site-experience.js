(() => {
  'use strict';
  window.initSiteExperience = function () {
    const gallery=document.querySelector('.film-gallery');
    const cards=[...gallery.querySelectorAll('.card')];
    document.querySelector('.hero-wave').addEventListener('click',()=>{
      document.getElementById('plugins').focus({preventScroll:true});
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

    const overlay=document.getElementById('site-intro');
    if(document.documentElement.classList.contains('intro-pending')){
      const main=document.getElementById('main');
      const navigation=document.querySelector('nav');
      const reduced=matchMedia('(prefers-reduced-motion: reduce)').matches;
      let finished=false,started=false;
      overlay.hidden=false;main.inert=true;if(navigation)navigation.inert=true;
      const soft=document.documentElement.classList.contains('intro-soft');
      const mark=overlay.querySelector('.intro-mark');
      const heroArt=document.querySelector('.hero-logo .logo-art');
      const matchLogo=()=>{mark.style.width=heroArt.getBoundingClientRect().width+'px';};
      matchLogo();
      window.addEventListener('resize',matchLogo);
      const logoImage=heroArt.querySelector('img');
      logoImage.decode?.().then(matchLogo).catch(()=>{});
      const cleanup=()=>{if(finished)return;finished=true;window.removeEventListener('resize',matchLogo);document.documentElement.classList.remove('intro-pending','intro-soft');overlay.hidden=true;main.inert=false;if(navigation)navigation.inert=false;};
      const finish=()=>{
        if(started||finished)return;started=true;
        const duration=reduced?180:soft?650:900;
        const heroLogo=document.querySelector('.hero-logo');
        const target=heroArt.getBoundingClientRect();
        const origin=mark.getBoundingClientRect();
        const visible=target.top>=0&&target.bottom<=innerHeight;
        if(!reduced&&visible){
          mark.animate([
            {transform:'translate(-50%,-50%)',opacity:1},
            {transform:`translate(calc(-50% + ${target.left-origin.left}px),calc(-50% + ${target.top-origin.top}px))`,opacity:1}
          ],{duration,easing:'cubic-bezier(.22,1,.36,1)',fill:'forwards'});
        }
        heroLogo.animate([{opacity:0,offset:0},{opacity:0,offset:.5},{opacity:1,offset:1}],{duration,easing:'ease-in-out',fill:'forwards'});
        overlay.animate([{opacity:1},{opacity:0}],{duration,easing:'ease-in-out',fill:'forwards'}).finished.then(cleanup,cleanup);
      };
      overlay.querySelector('.intro-skip').addEventListener('click',cleanup);
      setTimeout(finish,reduced?150:soft?650:2100);
      setTimeout(cleanup,4300);
    }
    return {stop(){}};
  };

  /* Downloads follow the visitor's OS, show live release details and stay reachable while scrolling. */
  window.initDownloads = function () {
    const platform=navigator.userAgentData?.platform||navigator.platform||navigator.userAgent||'';
    const os=/Mac|iPhone|iPad|iPod/i.test(platform)?'mac':'win';
    const other=os==='mac'?'win':'mac';
    const links={win:'https://github.com/slazoasher/skazi-bundle/releases/latest/download/SKAZI.Bundle.Setup.exe',mac:'https://github.com/slazoasher/skazi-bundle/releases/latest/download/SKAZI.Bundle.pkg'};
    const labels={win:'Windows',mac:'Mac'};
    document.querySelectorAll('[data-dl-primary]').forEach(link=>{
      link.href=links[os];
      link.querySelector('[data-dl-os-label]').textContent=`for ${labels[os]}`;
      link.setAttribute('aria-label',`Download SKAZI Bundle for ${labels[os]}`);
    });
    document.querySelectorAll('[data-dl-secondary]').forEach(link=>{link.href=links[other];link.textContent=`${labels[other]} version`;});
    const ctas=document.getElementById('download');
    // One Download button everywhere; the chooser recommends the visitor's OS.
    const chooser=document.getElementById('os-chooser');
    const options=[...chooser.querySelectorAll('.os-option')];
    options.forEach(option=>{
      const match=option.dataset.dl===os;
      option.classList.toggle('is-recommended',match);
      option.setAttribute('aria-label',`Download for ${labels[option.dataset.dl]}${match?' (detected)':''}`);
    });
    chooser.querySelector('.os-options').prepend(chooser.querySelector(`.os-option[data-dl="${os}"]`));
    let chooserTrigger=null;
    const setInert=value=>document.querySelectorAll('#main,.back-top,.site-nav,.dl-bar').forEach(el=>{el.inert=value;});
    const openChooser=trigger=>{
      chooserTrigger=trigger;chooser.hidden=false;setInert(true);document.body.style.overflow='hidden';
      chooser.querySelector('.os-option').focus();
    };
    const closeChooser=(restore=true)=>{
      if(chooser.hidden)return;
      chooser.hidden=true;setInert(false);document.body.style.overflow='';
      if(restore)chooserTrigger?.focus();
    };
    document.querySelectorAll('[data-open-download]').forEach(button=>button.addEventListener('click',()=>openChooser(button)));
    chooser.querySelector('.os-chooser-close').addEventListener('click',()=>closeChooser());
    chooser.addEventListener('click',event=>{if(event.target===chooser)closeChooser();});
    chooser.addEventListener('keydown',event=>{
      if(event.key==='Escape'){event.preventDefault();closeChooser();return;}
      if(event.key!=='Tab')return;
      const focusable=[...chooser.querySelectorAll('a,button')];
      const first=focusable[0], last=focusable[focusable.length-1];
      if(event.shiftKey&&document.activeElement===first){event.preventDefault();last.focus();}
      else if(!event.shiftKey&&document.activeElement===last){event.preventDefault();first.focus();}
    });

    // After a download starts, explain the unsigned-installer prompt for that OS.
    const toast=document.getElementById('dl-toast');
    const toastText={
      win:'Windows may show “Windows protected your PC” because the installer isn’t code-signed yet. Click More info → Run anyway.',
      mac:'macOS may block the first open. Right-click the .pkg, choose Open, then confirm.'
    };
    let toastTimer=0;
    const hideToast=()=>{toast.hidden=true;clearTimeout(toastTimer);};
    const showToast=key=>{
      toast.querySelector('[data-toast-text]').textContent=toastText[key];
      toast.hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(hideToast,20000);
    };
    toast.querySelector('.dl-toast-close').addEventListener('click',hideToast);
    document.addEventListener('keydown',event=>{if(event.key==='Escape'&&!toast.hidden)hideToast();});
    options.forEach(option=>option.addEventListener('click',()=>{closeChooser(false);showToast(option.dataset.dl);}));

    // Social proof: installer downloads summed over every release, rounded down to the hundred.
    const downloads=document.querySelector('[data-downloads]');
    if(downloads){
      const show=total=>{downloads.dataset.count=String(total);if(window.animateFigure&&!document.documentElement.classList.contains('intro-pending'))window.animateFigure(downloads);};
      let cachedDownloads=null;
      try{cachedDownloads=Number(sessionStorage.getItem('skazi-downloads'))||null;}catch(error){}
      if(cachedDownloads)show(cachedDownloads);
      else fetch('https://api.github.com/repos/slazoasher/skazi-bundle/releases?per_page=100',{headers:{Accept:'application/vnd.github+json'}})
        .then(response=>response.ok?response.json():Promise.reject(new Error(response.status)))
        .then(releases=>{
          const total=releases.reduce((sum,release)=>sum+(release.assets||[]).reduce((s,asset)=>s+(asset.download_count||0),0),0);
          if(total>0){try{sessionStorage.setItem('skazi-downloads',String(total));}catch(error){}show(total);}
        })
        .catch(()=>{});
    }

    // The sticky download control appears once the hero buttons have scrolled away.
    const placeCta=()=>{document.body.classList.toggle('cta-away',ctas.getBoundingClientRect().bottom<0);};
    placeCta();
    addEventListener('scroll',placeCta,{passive:true});
    addEventListener('pageshow',placeCta);
    return os;
  };

  /* A red glow follows a fine pointer and widens over anything clickable. */
  window.initCursorGlow = function () {
    if(!matchMedia('(pointer:fine)').matches||matchMedia('(prefers-reduced-motion: reduce)').matches)return;
    const glow=document.createElement('div');
    glow.className='cursor-glow';glow.setAttribute('aria-hidden','true');
    document.body.append(glow);
    let x=-100,y=-100,frame=0;
    const paint=()=>{frame=0;glow.style.transform=`translate(${x}px,${y}px)`;};
    document.addEventListener('pointermove',event=>{
      x=event.clientX;y=event.clientY;
      glow.classList.toggle('is-active',!!event.target.closest('a,button,[role="button"],summary,.card,input,label'));
      glow.classList.add('is-visible');
      if(!frame)frame=requestAnimationFrame(paint);
    },{passive:true});
    document.addEventListener('pointerleave',()=>glow.classList.remove('is-visible'));
    document.addEventListener('pointerdown',()=>glow.classList.add('is-pressed'));
    document.addEventListener('pointerup',()=>glow.classList.remove('is-pressed'));
  };

  /* Card chips: show only the ones that fit on one row, then a "+N" counter for the rest. */
  window.initCardChips = function () {
    const rows=[...document.querySelectorAll('#plugins .film-gallery .chips')];
    if(!rows.length)return;
    const gallery=document.querySelector('.film-gallery');
    const fit=()=>{
      if(gallery.dataset.view!=='grid')return;
      rows.forEach(row=>{
        row.querySelector('.chip.more')?.remove();
        const chips=[...row.querySelectorAll('.chip')];
        chips.forEach(chip=>{chip.hidden=false;});
        const limit=row.clientWidth;
        if(!limit)return;
        let more=document.createElement('span');more.className='chip more';row.append(more);
        let hidden=0;
        for(let i=chips.length-1;i>0;i--){
          more.textContent=`+${hidden}`;
          const last=chips.filter(c=>!c.hidden).pop();
          const fits=last.offsetLeft+last.offsetWidth+(hidden?more.offsetWidth+6:0)<=limit;
          if(fits)break;
          chips[i].hidden=true;hidden++;
        }
        if(hidden){more.textContent=`+${hidden}`;more.title=chips.filter(c=>c.hidden).map(c=>c.textContent).join(' · ');}
        else more.remove();
      });
    };
    fit();
    let timer=0;
    addEventListener('resize',()=>{clearTimeout(timer);timer=setTimeout(fit,120);},{passive:true});
    document.querySelectorAll('[data-gallery-view]').forEach(button=>button.addEventListener('click',()=>setTimeout(fit,50)));
    document.fonts?.ready.then(fit);
  };

  /* Hero figures count up once the page is in view; static numbers for reduced motion or no JavaScript. */
  window.initHeroCounters = function () {
    const figures=[...document.querySelectorAll('[data-count]')];
    if(!figures.length)return;
    const reduced=matchMedia('(prefers-reduced-motion: reduce)').matches;
    const format=value=>Math.round(value).toLocaleString('en-US');
    // Animate one figure from its current value to data-count; reused when live numbers arrive.
    window.animateFigure=(figure,index=0)=>{
      const target=Number(figure.dataset.count);
      const from=Number(String(figure.textContent).replace(/[^\d]/g,''))||0;
      if(reduced||document.visibilityState!=='visible'){figure.textContent=format(target);return;}
      const duration=1300+index*200, start=performance.now();
      const step=now=>{
        const progress=Math.min(1,(now-start)/duration);
        const eased=1-Math.pow(1-progress,3);
        figure.textContent=format(from+(target-from)*eased);
        if(progress<1)requestAnimationFrame(step);
      };
      requestAnimationFrame(step);
      setTimeout(()=>{figure.textContent=format(target);},duration+120);
    };
    const run=()=>figures.forEach((figure,index)=>{figure.textContent='0';window.animateFigure(figure,index);});
    const html=document.documentElement;
    if(document.visibilityState!=='visible'){
      figures.forEach(figure=>{figure.textContent=format(Number(figure.dataset.count));});
      return;
    }
    if(html.classList.contains('intro-pending')){
      const watcher=new MutationObserver(()=>{if(!html.classList.contains('intro-pending')){watcher.disconnect();run();}});
      watcher.observe(html,{attributes:true,attributeFilter:['class']});
    } else run();
  };
})();
