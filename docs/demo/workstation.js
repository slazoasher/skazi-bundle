'use strict';
(() => {
  const trackMeta={kick:{name:'Kick',plugin:'KICKASS',number:'01'},bass:{name:'Bass',plugin:'SUBICH',number:'02'},acid:{name:'Acid',plugin:'SKACID',number:'03'},space:{name:'Space',plugin:'TAHINI',number:'04'},fx:{name:'Movement',plugin:'SALAT',number:'05'}};
  const soloTracks=new Set();let selected='bass';
  function activatePanel(name){document.querySelectorAll('[data-studio-panel]').forEach(b=>{const active=b.dataset.studioPanel===name;b.setAttribute('aria-selected',String(active));b.tabIndex=active?0:-1;});document.querySelectorAll('.studio-panel').forEach(p=>p.hidden=p.id!=='panel-'+name);$('#edit-pattern').setAttribute('aria-pressed',String(name==='pattern'));document.querySelectorAll('[data-editor]').forEach(p=>{p.hidden=p.dataset.editor!==selected;p.inert=name!=='pattern'||p.dataset.editor!==selected;});}
  function selectTrack(key){selected=key;document.querySelectorAll('[data-select-track]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.selectTrack===key)));document.querySelectorAll('[data-track]').forEach(row=>row.classList.toggle('track-selected',row.dataset.track===key));document.querySelectorAll('[data-editor]').forEach(p=>p.hidden=p.dataset.editor!==key);document.querySelectorAll('[data-device]').forEach(p=>p.hidden=p.dataset.device!==key);document.querySelectorAll('.studio-channel').forEach(c=>c.classList.toggle('channel-selected',c.querySelector('[data-level]').dataset.level===key));$('#selected-track-label').textContent=trackMeta[key].number+' / '+trackMeta[key].plugin;const current=document.querySelector('[data-studio-panel][aria-selected="true"]');activatePanel(current.dataset.studioPanel);}
  function noteSVG(key,bar){const events=Array.from({length:16},(_,step)=>SkaziStudio.plan(currentSession,bar,step,patterns,{[key]:true}));return '<svg viewBox="0 0 160 40" preserveAspectRatio="none" aria-hidden="true">'+events.map((list,step)=>{const event=list[0];if(!event)return '';const y=key==='kick'?12:key==='fx'?(step%4===0?24:8):Math.max(4,Math.min(32,30-(event.note-(key==='bass'?33:57))*1.1));return '<rect x="'+(step*10+1)+'" y="'+y+'" width="'+(key==='space'?17:7)+'" height="3" rx=".7"/>';}).join('')+'</svg>';}
  function renderClips(){for(const key of Object.keys(trackMeta)){const target=document.querySelector('[data-clips="'+key+'"]');target.innerHTML=Array.from({length:4},(_,i)=>'<button class="timeline-clip" data-clip-track="'+key+'" aria-label="Edit '+trackMeta[key].name+' clip, bar '+(i+1)+'"><span>'+trackMeta[key].plugin+'<i>↗</i></span>'+noteSVG(key,i)+'</button>').join('');target.querySelectorAll('button').forEach(b=>b.onclick=()=>{selectTrack(key);activatePanel('pattern');});}}
  function applySolo(){for(const key of Object.keys(trackMeta)){const audible=layers[key]&&(!soloTracks.size||soloTracks.has(key));document.querySelector('[data-track="'+key+'"]').classList.toggle('solo-muted',!audible);if(ctx&&channelGains[key])channelGains[key].gain.setTargetAtTime(audible?channelLevels[key]**2:0,Math.max(ctx.currentTime,scheduledLayerTime),.015);}}
  const baseUpdate=updateLayers;updateLayers=function(){baseUpdate();applySolo();};
  document.querySelectorAll('[data-solo]').forEach(b=>b.onclick=()=>{const key=b.dataset.solo;if(soloTracks.has(key))soloTracks.delete(key);else{soloTracks.add(key);if(!layers[key])setLayer(key,true);}b.setAttribute('aria-pressed',String(soloTracks.has(key)));applySolo();});
  document.querySelectorAll('[data-select-track]').forEach(b=>b.onclick=()=>selectTrack(b.dataset.selectTrack));
  document.querySelectorAll('[data-studio-panel]').forEach(b=>{b.onclick=()=>activatePanel(b.dataset.studioPanel);b.onkeydown=e=>{if(!['ArrowLeft','ArrowRight','Home','End'].includes(e.key))return;e.preventDefault();const tabs=[...document.querySelectorAll('[data-studio-panel]')],i=tabs.indexOf(b),next=e.key==='Home'?0:e.key==='End'?2:(i+(e.key==='ArrowRight'?1:2))%3;activatePanel(tabs[next].dataset.studioPanel);tabs[next].focus();};});
  $('#edit-pattern').onclick=()=>activatePanel('pattern');
  document.querySelectorAll('[data-pattern]').forEach(row=>{row.querySelectorAll('button').forEach((b,i)=>{b.style.setProperty('--note-position',String([2,4,3,5,2,3,4,2][i%8]));b.addEventListener('click',renderClips);});});
  $('#build-drop').addEventListener('click',()=>{soloTracks.clear();document.querySelectorAll('[data-solo]').forEach(b=>b.setAttribute('aria-pressed','false'));applySolo();});
  document.addEventListener('studio-session-changed',()=>{soloTracks.clear();document.querySelectorAll('[data-solo]').forEach(x=>x.setAttribute('aria-pressed','false'));updateLayers();renderClips();});
  document.querySelectorAll('[data-level]').forEach(input=>input.addEventListener('input',applySolo));
  document.addEventListener('studio-audio-started',applySolo);
  const tempoInput=$('#studio-tempo');
  function commitTempo(){const value=Math.min(170,Math.max(80,Math.round(Number(tempoInput.value)||SkaziStudio.profiles[currentSession].tempo)));tempoInput.value=String(value);if(value!==Math.round(60/beat))queueStudioTempo(value);}
  tempoInput.addEventListener('change',commitTempo);tempoInput.addEventListener('blur',commitTempo);
  tempoInput.addEventListener('keydown',event=>{if(event.key==='Enter'){event.preventDefault();commitTempo();}});
  $('#studio-help-toggle').onclick=()=>{const show=$('#studio-help-toggle').getAttribute('aria-expanded')!=='true';$('#studio-help-toggle').setAttribute('aria-expanded',String(show));$('#studio-help').hidden=!show;};
  renderClips();selectTrack('bass');activatePanel('mixer');updateLayers();
})();

// Pointer-driven colour reveal: one read/write pass per frame, no idle loop.
(() => {
  const capable=matchMedia('(hover: hover) and (pointer: fine)');
  const reduced=matchMedia('(prefers-reduced-motion: reduce)');
  for(const portrait of document.querySelectorAll('[data-color-portrait]')){
    let frame=0,active=false,clientX=0,clientY=0,x=0,y=0,lastTime=0,first=true;
    function render(time){
      frame=0;if(!active)return;
      const rect=portrait.getBoundingClientRect(),targetX=clientX-rect.left,targetY=clientY-rect.top;
      const blend=first||reduced.matches?1:1-Math.exp(-Math.min(50,time-lastTime)/30);first=false;lastTime=time;
      x+=(targetX-x)*blend;y+=(targetY-y)*blend;
      portrait.style.setProperty('--reveal-x',x+'px');portrait.style.setProperty('--reveal-y',y+'px');
      if(Math.abs(targetX-x)+Math.abs(targetY-y)>.25)frame=requestAnimationFrame(render);
    }
    function move(event){
      if(!capable.matches||event.pointerType==='touch')return;
      clientX=event.clientX;clientY=event.clientY;
      if(!active){active=true;first=true;portrait.classList.add('is-color-revealing');}
      if(!frame)frame=requestAnimationFrame(render);
    }
    function leave(){active=false;first=true;cancelAnimationFrame(frame);frame=0;portrait.classList.remove('is-color-revealing');}
    portrait.addEventListener('pointerenter',move);portrait.addEventListener('pointermove',move,{passive:true});portrait.addEventListener('pointerleave',leave);
    capable.addEventListener('change',()=>{if(!capable.matches)leave();});
    addEventListener('scroll',()=>{if(active&&!frame)frame=requestAnimationFrame(render);},{passive:true});
  }
})();
