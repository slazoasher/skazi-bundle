'use strict';
const siteAssetBase=new URL('.',document.currentScript.src);
const $=s=>document.querySelector(s), $$=s=>[...document.querySelectorAll(s)];
const escapeText=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function pluginName(p){const parts=p.nameParts||[p.name,''];return '<span class="plugin-name-base">'+escapeText(parts[0])+'</span><em class="plugin-name-accent">'+escapeText(parts[1])+'</em>';}
// Re-align a cold deep link after the asynchronous catalogue and fonts settle.
const initialSectionHash=location.hash;let initialAnchorInterrupted=false;
for(const eventName of ['wheel','touchstart','pointerdown','keydown'])addEventListener(eventName,()=>{initialAnchorInterrupted=true;},{once:true,passive:true});
function alignInitialSection(){if(!initialSectionHash||initialSectionHash==='#'||initialAnchorInterrupted||location.hash!==initialSectionHash)return;let target;try{target=document.getElementById(decodeURIComponent(initialSectionHash.slice(1)));}catch{return;}if(target)target.scrollIntoView({behavior:'instant',block:'start'});}
function settleInitialSection(){Promise.resolve(document.fonts?.ready).then(()=>requestAnimationFrame(alignInitialSection));}
let plugins=[],filter='All',activeAudio=null,activeButton=null,showAll=false;
const dialog=$('#plugin-dialog');
const mediaIcon=playing=>'<svg class="media-icon" viewBox="0 0 24 24" aria-hidden="true">'+(playing?'<rect x="6" y="6" width="12" height="12" rx="1.7"/>':'<path d="m8 5 11 7-11 7z"/>')+'</svg>';
const expandIcon='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M9 4H4v5M15 4h5v5M9 20H4v-5M15 20h5v-5"/></svg>';
function setPreviewButton(button,playing){button.setAttribute('aria-pressed',String(playing));if(button.id==='detail-play')button.innerHTML=mediaIcon(playing)+'<span>'+(playing?'Stop':'Play')+' concept preview</span>';else{const icon=button.querySelector('.listen-bottom b');if(icon)icon.innerHTML=mediaIcon(playing);button.setAttribute('aria-label',(playing?'Stop':'Play')+' '+button.querySelector('.listen-bottom')?.textContent.trim());}}

function stopPreview(){if(activeAudio){activeAudio.pause();activeAudio.currentTime=0;}if(activeButton){setPreviewButton(activeButton,false);}activeAudio=null;activeButton=null;}
function playPreview(name,button){if(activeButton===button){stopPreview();return;}stopPreview();stopLoop();stopCompare();const audio=new Audio(new URL('../audio/'+name+'.wav',siteAssetBase).href);audio.volume=.65;activeAudio=audio;activeButton=button;audio.play().then(()=>{if(activeAudio!==audio){audio.pause();return;}setPreviewButton(button,true);$('#audio-status').textContent='Playing concept preview: '+name;}).catch(()=>{if(activeAudio!==audio)return;$('#audio-status').textContent='Audio could not play. Please try again.';stopPreview();});audio.onended=()=>{if(activeAudio===audio)stopPreview();};}
$$('[data-audio]').forEach(b=>{b.querySelector('.listen-bottom b').innerHTML=mediaIcon(false);setPreviewButton(b,false);b.onclick=()=>playPreview(b.dataset.audio,b);});
$$('.wave').forEach(w=>{w.innerHTML=Array.from({length:48},(_,i)=>`<i style="--h:${8+Math.abs(Math.sin(i*1.7)*25)}px;--d:${-i*.03}s"></i>`).join('');});
function render(){const groups={Bass:['Subich','SKAZI-Synth','Bully','TBON303','Skacid'],Acid:['Skacid','TBON303','SKAZI-Synth'],Space:['Skverb','Tahini','Skwide'],Movement:['Salat','Shaksuka','PitaPump','FilterFish','Skilter','MidiGod','Bamba']};const query=$('#search').value.toLowerCase();const result=plugins.filter(p=>(filter==='All'||p.category===filter||groups[filter]?.includes(p.folder))&&(p.name+' '+p.subtitle+' '+p.description).toLowerCase().includes(query));$('#result-count').textContent=result.length+' TOOLS / '+filter.toUpperCase();const visible=(showAll||filter!=='All'||query)?result:result.slice(0,6);$('#catalog-more').hidden=filter!=='All'||Boolean(query)||result.length<=6;$('#catalog-more').setAttribute('aria-expanded',String(showAll));$('#catalog-more').innerHTML=showAll?'Show fewer plugins <span>↑</span>':'Show all 19 plugins <span>↓</span>';$('#result-count').textContent=visible.length+' OF '+result.length+' TOOLS / '+filter.toUpperCase();$('#catalog').innerHTML=visible.map(p=>`<button class="tool" data-plugin="${p.folder}" aria-label="Explore ${escapeText(p.name)} plugin" aria-haspopup="dialog" aria-controls="plugin-dialog"><div class="tool-image"><img src="${p.image}" loading="lazy" alt="${escapeText(p.name)} interface"><span class="tool-open-icon" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M9 4H4v5M15 4h5v5M9 20H4v-5M15 20h5v-5"/></svg></span></div><div class="micro">${p.category}<span>${p.presets}</span></div><h3>${pluginName(p)}</h3><p>${escapeText(p.subtitle)}</p></button>`).join('')||'<p>No tools match. Try another search or category.</p>';$$('[data-plugin]').forEach(b=>b.onclick=()=>openPlugin(b.dataset.plugin));}
function openPlugin(folder){const p=plugins.find(p=>p.folder===folder);if(!p)return;stopPreview();stopLoop();stopCompare();$('#plugin-details').innerHTML=`<img class="detail-image" src="${p.image.replace('-480','-960')}" alt="${escapeText(p.name)} interface"><span class="micro">${p.category} / ${p.presets}</span><h2 id="plugin-title">${pluginName(p)}</h2><p>${escapeText(p.description)}</p><div class="tags">${p.tags.map(t=>`<span>${escapeText(t)}</span>`).join('')}</div><div class="dialog-actions">${p.preview?'<button class="button media-action" id="detail-play" aria-pressed="false"><svg class="media-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="m8 5 11 7-11 7z"/></svg><span>Play concept preview</span></button>':''}<a class="text-link" id="detail-download" href="#download">Included in the free bundle ↓</a></div><p class="note">Concept audio demonstrates the website experience; it is not recorded plugin output.</p>`;if(p.preview)$('#detail-play').onclick=()=>playPreview(p.preview,$('#detail-play'));$('#detail-download').onclick=()=>{stopPreview();dialog.close();};dialog.showModal();}
$('#close-dialog').onclick=()=>dialog.close();dialog.addEventListener('close',stopPreview);dialog.addEventListener('click',e=>{if(e.target===dialog){const r=dialog.getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)dialog.close();}});
$$('[data-filter]').forEach(b=>b.onclick=()=>{filter=b.dataset.filter;$$('[data-filter]').forEach(x=>x.setAttribute('aria-pressed',String(x===b)));render();});const search=$('#search'),searchHint=$('#search-hint');
const englishSearch=/[^A-Za-z0-9 .&'’+_\-/]/g;
function cleanSearch(){const before=search.value,caret=search.selectionStart??before.length,clean=before.replace(englishSearch,''),nextCaret=before.slice(0,caret).replace(englishSearch,'').length;if(clean!==before){search.value=clean;search.setSelectionRange(nextCaret,nextCaret);searchHint.hidden=false;}else searchHint.hidden=true;render();}
search.addEventListener('compositionend',cleanSearch);
search.addEventListener('input',event=>{if(!event.isComposing)cleanSearch();});
search.addEventListener('paste',e=>{const pasted=e.clipboardData?.getData('text')||'';const clean=pasted.replace(englishSearch,'');if(clean===pasted)return;e.preventDefault();search.setRangeText(clean,search.selectionStart,search.selectionEnd,'end');searchHint.hidden=false;render();});
fetch(new URL('plugins.json',siteAssetBase)).then(r=>{if(!r.ok)throw Error('catalog');return r.json();}).then(data=>{plugins=data.map(p=>({...p,image:new URL(p.image,siteAssetBase).href}));const featured=[['Subich','01 / FEEL THE LOW END','A focused foundation for your next bassline.'],['Skacid','02 / FOLLOW THE ACID','Accent, slide, and a sequence that keeps moving.'],['Tahini','03 / OPEN UP THE SPACE','Give a simple idea somewhere to go.']];$('#featured').innerHTML=featured.map(([folder,label,line])=>{const p=plugins.find(x=>x.folder===folder);return `<article class="feature"><button class="feature-image feature-open" data-plugin="${folder}" aria-label="Open ${escapeText(p.name)} plugin details" aria-haspopup="dialog" aria-controls="plugin-dialog"><img src="${p.image.replace('-480','-960')}" alt="${escapeText(p.name)} interface" loading="lazy"><span class="tool-open-icon" aria-hidden="true">${expandIcon}</span></button><div class="micro">${label}</div><h3>${pluginName(p)}</h3><p>${line}</p></article>`;}).join('');$$('#featured [data-plugin]').forEach(b=>b.onclick=()=>openPlugin(b.dataset.plugin));render();settleInitialSection();}).catch(()=>{$('#catalog').textContent='The collection could not load. Refresh to try again.';});
// A synchronized, original browser synthesizer sketch. No plugin output claims.
const channelGains={},channelAnalysers={},channelMeterBuffers={},meterSmoothing={};let playStart=0;let analyser=null;let ctx=null,master=null,timer=null,nextTime=0,step=0,running=false,starting=false,paused=false,pausing=false,runGeneration=0;const activeVoices=new Set();const layers={kick:true,bass:true,acid:false,space:false,fx:false};let beat=60/138;
const cells=Array.from({length:16},()=>{const x=document.createElement('i');$('.sequencer').append(x);return x;});
let studioEngine=null,currentSession='roll',pendingSession=null,pendingTempo=null,scheduledLayerTime=0;
const studioControls={drive:28,tone:54,space:32,master:65};
function setupAudio(){
 if(!ctx){ctx=new AudioContext();master=ctx.createGain();master.gain.value=studioControls.master/100;analyser=SkaziStudio.mastering(ctx,master,ctx.destination);
  for(const key of Object.keys(layers)){const gain=ctx.createGain();gain.gain.value=layers[key]?channelLevels[key]**2:0;const meter=ctx.createAnalyser();meter.fftSize=256;gain.connect(meter).connect(master);channelGains[key]=gain;channelAnalysers[key]=meter;channelMeterBuffers[key]=new Float32Array(256);meterSmoothing[key]=0;}
  studioEngine=SkaziStudio.create(ctx,channelGains,activeVoices,studioControls);$('#engine-rate').textContent=(ctx.sampleRate/1000).toFixed(1)+' kHz';
 }return ctx.resume();
}
function schedule(){
 while(nextTime<ctx.currentTime+.1){
  const s=step,t=nextTime,generation=runGeneration;
  if(s===0){if(pendingSession){const name=pendingSession;pendingSession=null;applySession(name,t);}if(pendingTempo!==null){applyTempo(pendingTempo,t);pendingTempo=null;}if(buildBusy)advanceBuild(t);}
  const bar=Math.floor(Math.max(0,(t-playStart)/(beat*4))+.00001)%4;
  for(const event of SkaziStudio.plan(currentSession,bar,s,patterns,layers,noteEdits))studioEngine.play(event,t,beat,s);
  const delay=Math.max(0,(t-ctx.currentTime)*1000);setTimeout(()=>{if(running&&generation===runGeneration){cells.forEach((c,i)=>c.classList.toggle('active',i===s));updateDAWStep(s);}},delay);
  step=(step+1)%16;nextTime+=beat/4;
 }
}
function setDAWPosition(bar,positionStep){const a=String(bar).padStart(2,'0'),b=String(positionStep).padStart(2,'0'),value=a+' : '+b;if($('#daw-position').textContent===value)return;$('#daw-position').textContent=value;$('#daw-bar').textContent=a;$('#daw-step').textContent=b;}
function setTransport(mode){
 const button=$('#transport'),label=mode==='pause'?'Pause':mode==='resume'?'Resume':mode==='retry'?'Retry audio':'Play';
 const icon=mode==='pause'?'<rect x="6" y="5" width="4" height="14" rx="1.2"/><rect x="14" y="5" width="4" height="14" rx="1.2"/>':'<path d="m8 5 11 7-11 7z"/>';
 button.innerHTML='<svg viewBox="0 0 24 24" aria-hidden="true">'+icon+'</svg><span class="control-label">'+label+'</span>';
 button.setAttribute('aria-label',label);button.setAttribute('aria-pressed',String(mode==='pause'));
 $('#reset-session').hidden=mode==='play'||mode==='retry';
}
async function startLoop(){
 if(running||starting||pausing)return false;
 const resume=paused;starting=true;const generation=++runGeneration;
 stopPreview();stopCompare();$('#transport').setAttribute('aria-busy','true');
 try{
  await setupAudio();if(generation!==runGeneration||document.hidden)return false;
  master.gain.cancelScheduledValues(ctx.currentTime);master.gain.setValueAtTime(studioControls.master/100,ctx.currentTime);
  running=true;paused=false;$('#engine-state').textContent='ENGINE RUNNING';
  if(!resume){playStart=ctx.currentTime+.05;step=0;nextTime=playStart;}
  document.dispatchEvent(new Event('studio-audio-started'));
  schedule();timer=setInterval(schedule,25);$('#session-state').textContent='PLAYING';$('#lab').classList.add('is-playing');$('#lab').classList.remove('is-paused');
  setTransport('pause');if(resume)$('#drop-status').textContent='Resumed. Keep shaping your sound.';return true;
 }catch{if(generation===runGeneration){setTransport('retry');$('#drop-status').textContent='Audio could not start. Press Retry audio to try again.';}return false;}
 finally{if(generation===runGeneration){starting=false;$('#transport').removeAttribute('aria-busy');}}
}
async function pauseLoop(){
 if(!running||pausing)return false;
 const generation=++runGeneration;running=false;paused=true;pausing=true;clearInterval(timer);timer=null;cancelBuild();
 $('#transport').disabled=true;$('#transport').setAttribute('aria-busy','true');
 try{
  await ctx.suspend();if(generation!==runGeneration)return false;
  $('#engine-state').textContent='ENGINE PAUSED';$('#session-state').textContent='PAUSED';$('#lab').classList.remove('is-playing');$('#lab').classList.add('is-paused');
  setTransport('resume');$('#drop-status').textContent='Paused. Resume continues here; Stop returns to the beginning.';
  document.dispatchEvent(new Event('studio-audio-paused'));return true;
 }catch{stopLoop();return false;}
 finally{if(generation===runGeneration){pausing=false;$('#transport').disabled=false;$('#transport').removeAttribute('aria-busy');}}
}
function stopLoop(){
 ++runGeneration;starting=false;paused=false;pausing=false;$('#transport').disabled=false;$('#transport').removeAttribute('aria-busy');
 running=false;clearInterval(timer);timer=null;$('#engine-state').textContent='ENGINE STANDBY';cancelBuild();
 for(const voice of activeVoices){try{voice.stop();}catch{}}activeVoices.clear();studioEngine?.reset();pendingSession=null;pendingTempo=null;$$('[data-session]').forEach(b=>b.classList.remove('is-queued'));
 $('#session-state').textContent='READY TO PLAY';setDAWPosition(1,1);
 $$('.step-button').forEach(b=>b.classList.remove('current-step'));$$('[data-meter]').forEach(m=>m.style.setProperty('--meter-level','0%'));
 $('#lab').classList.remove('is-playing','is-paused');$('.rack-playhead').style.setProperty('--playhead','0');
 if(master){master.gain.cancelScheduledValues(ctx.currentTime);master.gain.setValueAtTime(.0001,ctx.currentTime);}
 cells.forEach(c=>c.classList.remove('active'));if(document.activeElement===$('#reset-session'))$('#transport').focus();setTransport('play');document.dispatchEvent(new Event('studio-audio-stopped'));
}
$('#transport').onclick=()=>running?pauseLoop():starting?stopLoop():startLoop();$$('[data-layer]').forEach(b=>b.onclick=()=>{const key=b.dataset.layer;layers[key]=!layers[key];b.setAttribute('aria-pressed',String(layers[key]));b.title=(layers[key]?'Mute ':'Enable ')+key;updateLayers();});
// A/B effect demonstration using the same source and a browser delay.
let compareSource=null,compareContext=null,compareWet=null,comparing=false,wetEnabled=false,compareGeneration=0;
const compare=document.createElement('div');compare.className='compare';compare.innerHTML='<div><p class="eyebrow">HEAR THE DIFFERENCE</p><h3>A little space changes everything.</h3><p>Compare the same phrase with and without a browser delay.</p></div><div class="compare-buttons"><button id="compare-play" class="button outline media-action" aria-pressed="false">'+mediaIcon(false)+'<span>Play phrase</span></button><button id="compare-mode" class="button outline" aria-pressed="false">Dry / Original</button></div><p class="note">Interaction demo using browser processing. This is not a TAHINI or SKAZI REVERB recording.</p>';
$('#compare-mount').append(compare);
async function playCompare(){const generation=++compareGeneration;stopPreview();stopLoop();try{if(!compareContext){compareContext=new AudioContext();const audio=new Audio(new URL('../audio/arp.wav',siteAssetBase).href);audio.loop=true;audio.volume=.6;compareSource=audio;const source=compareContext.createMediaElementSource(audio);const dry=compareContext.createGain();dry.gain.value=.6;const delay=compareContext.createDelay(2);delay.delayTime.value=beat*.75;const feedback=compareContext.createGain();feedback.gain.value=.42;compareWet=compareContext.createGain();compareWet.gain.value=0;source.connect(dry).connect(compareContext.destination);source.connect(delay);delay.connect(feedback).connect(delay);delay.connect(compareWet).connect(compareContext.destination);}await compareContext.resume();if(generation!==compareGeneration)return;compareWet.gain.value=wetEnabled?.55:0;await compareSource.play();if(generation!==compareGeneration){compareSource.pause();return;}comparing=true;$('#compare-play').innerHTML=mediaIcon(true)+'<span>Stop phrase</span>';$('#compare-play').setAttribute('aria-pressed','true');}catch{if(generation===compareGeneration)$('#compare-play').innerHTML=mediaIcon(false)+'<span>Retry phrase</span>';}}
function stopCompare(){++compareGeneration;if(!comparing){compareSource?.pause();return;}compareSource.pause();compareSource.currentTime=0;compareWet.gain.setTargetAtTime(0,compareContext.currentTime,.02);comparing=false;$('#compare-play').innerHTML=mediaIcon(false)+'<span>Play phrase</span>';$('#compare-play').setAttribute('aria-pressed','false');}
$('#compare-play').onclick=()=>comparing?stopCompare():playCompare();$('#compare-mode').onclick=()=>{wetEnabled=!wetEnabled;$('#compare-mode').textContent=wetEnabled?'Wet / Delay on':'Dry / Original';$('#compare-mode').setAttribute('aria-pressed',String(wetEnabled));if(compareWet)compareWet.gain.setTargetAtTime(wetEnabled?.55:0,compareContext.currentTime,.03);};
const dawHelp={'Ableton Live':'Open Settings → Plug-ins, enable VST3, and rescan. Find SKAZI in the browser under Plug-ins.','FL Studio':'Open Options → Manage plugins and run Find installed plugins. Add a SKAZI instrument or effect from the plugin list.','Logic Pro':'Find instruments under AU Instruments and effects under Audio FX. Use Plug-in Manager to check their status.','Cubase':'Open Studio → VST Plug-in Manager. Rescan if needed, then search for SKAZI in your instruments or effects.','Reaper':'Open Preferences → Plug-ins → VST and rescan. Search for SKAZI in the FX browser.','Bitwig Studio':'Check Settings → Locations → Plug-in Locations, then search for SKAZI in the browser.','Studio One':'Open the browser’s Instruments or Effects tab and search for SKAZI. Restart or rescan if newly installed plugins are missing.'};function selectDaw(name){$('#daw-help').textContent=dawHelp[name];$('#selected-daw').textContent='in '+name;$$('[data-daw]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.daw===name)));}$$('[data-daw]').forEach(b=>b.onclick=()=>selectDaw(b.dataset.daw));selectDaw('Ableton Live');
document.addEventListener('visibilitychange',()=>{if(document.hidden){stopPreview();stopLoop();stopCompare();}});

// Audio-driven visual stage and one-bar-at-a-time arrangement.
let buildTimers=[],buildBusy=false;
function updateLayers(){if(ctx){for(const key of Object.keys(layers)){channelGains[key]?.gain.setTargetAtTime(layers[key]?channelLevels[key]**2:0,Math.max(ctx.currentTime,scheduledLayerTime),.015);}} $('#session-layers').textContent=Object.keys(layers).filter(k=>layers[k]).join(' + ').toUpperCase()||'ALL CHANNELS MUTED';$$('[data-track]').forEach(row=>row.classList.toggle('track-muted',!layers[row.dataset.track])); }
function setBuildLabel(label){const button=$('#build-drop');button.innerHTML='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="m12 3 2.6 6.4L21 12l-6.4 2.6L12 21l-2.6-6.4L3 12l6.4-2.6z"/></svg>';button.setAttribute('aria-label',label);button.title=label;if(label==='Building…')button.setAttribute('aria-busy','true');else button.removeAttribute('aria-busy');}
function cancelBuild(){buildTimers.forEach(clearTimeout);buildTimers=[];buildBusy=false;$('#build-drop').disabled=false;setBuildLabel('Build the drop');$('#drop-status').textContent='Add layers yourself, or let the energy build.';}
function setLayer(key,on){layers[key]=on;const b=document.querySelector('[data-layer="'+key+'"]');b.setAttribute('aria-pressed',String(on));b.title=(on?'Mute ':'Enable ')+key;updateLayers();}
let buildStage=-1;
function advanceBuild(time){
 scheduledLayerTime=time;buildStage++;const keys=['kick','bass','acid','space','fx'];keys.forEach((key,i)=>setLayer(key,i<=buildStage));
 $('#drop-status').textContent=['Kick first. Feel the pulse.','Bring in the rolling low end.','Let the acid through.','Open up the space.','Full groove. Make it yours.'][buildStage];
 scheduledLayerTime=0;if(buildStage===4){buildBusy=false;$('#build-drop').disabled=false;setBuildLabel('Build again');$('#session-state').textContent='FULL MIX';}
}
$('#build-drop').onclick=async()=>{if(buildBusy)return;cancelBuild();if(!running)await startLoop();if(!running)return;buildBusy=true;buildStage=-1;$('#build-drop').disabled=true;setBuildLabel('Building…');$('#drop-status').textContent='Build queued for the next bar.';};
const canvas=$('#sound-canvas'),paint=canvas.getContext('2d');let visualFrame=0,scopeVisible=false,lastVisualTime=0;
const scopeBins=new Uint8Array(1024),masterSamples=new Float32Array(2048);
const reducedMotion=matchMedia('(prefers-reduced-motion: reduce)');
const spectrumSmooth=new Float32Array(96),spectrumHold=new Float32Array(96);
function drawSpectrum(w,h,dpr){
 const left=34*dpr,right=w-10*dpr,top=12*dpr,bottom=h-23*dpr,plotW=right-left,plotH=bottom-top;
 paint.clearRect(0,0,w,h);if(plotW<=0||plotH<=0)return;
 paint.lineWidth=dpr;paint.strokeStyle='#ffffff12';paint.setLineDash([2*dpr,5*dpr]);
 for(let row=0;row<5;row++){const y=top+plotH*row/4;paint.beginPath();paint.moveTo(left,y);paint.lineTo(right,y);paint.stroke();}
 for(const hz of [30,250,2000,16000]){const x=left+Math.log(hz/30)/Math.log(16000/30)*plotW;paint.beginPath();paint.moveTo(x,top);paint.lineTo(x,bottom);paint.stroke();}paint.setLineDash([]);
 if(running&&analyser)analyser.getByteFrequencyData(scopeBins);else if(!paused){scopeBins.fill(0);spectrumSmooth.fill(0);spectrumHold.fill(0);}
 const points=[],held=[];
 for(let i=0;i<96;i++){
  const frequency=30*Math.pow(16000/30,i/95),index=ctx?Math.min(1022,Math.max(1,Math.round(frequency/(ctx.sampleRate/2048)))):1;
  const value=(scopeBins[index-1]+scopeBins[index]+scopeBins[index+1])/(3*255);
  if(!paused){spectrumSmooth[i]+=(value-spectrumSmooth[i])*(value > spectrumSmooth[i] ? .45 : .18);spectrumHold[i]=Math.max(spectrumSmooth[i],spectrumHold[i]-.008);}
  const x=left+i/95*plotW;points.push([x,bottom-spectrumSmooth[i]*plotH]);held.push([x,bottom-spectrumHold[i]*plotH]);
 }
 function curve(data){paint.moveTo(...data[0]);for(let i=1;i<data.length-1;i++){const next=data[i+1];paint.quadraticCurveTo(data[i][0],data[i][1],(data[i][0]+next[0])/2,(data[i][1]+next[1])/2);}paint.lineTo(...data[data.length-1]);}
 paint.beginPath();curve(points);paint.lineTo(right,bottom);paint.lineTo(left,bottom);paint.closePath();
 const gradient=paint.createLinearGradient(0,top,0,bottom);gradient.addColorStop(0,'#e5091490');gradient.addColorStop(.6,'#e5091438');gradient.addColorStop(1,'#e5091405');paint.fillStyle=gradient;paint.fill();
 if(running||paused){paint.beginPath();curve(held);paint.strokeStyle='#ffffff50';paint.lineWidth=dpr;paint.stroke();}
 paint.beginPath();curve(points);paint.strokeStyle=running||paused?'#e50914':'#ffffff20';paint.lineWidth=1.8*dpr;paint.lineJoin='round';paint.stroke();
}
function drawStage(timestamp=0){
 visualFrame=0;if(!scopeVisible||document.hidden)return;
 const interval=reducedMotion.matches||document.body.classList.contains('motion-paused')?140:32;
 if(timestamp-lastVisualTime<interval){visualFrame=requestAnimationFrame(drawStage);return;}lastVisualTime=timestamp;
 const rect=canvas.getBoundingClientRect(),dpr=Math.min(devicePixelRatio||1,2),w=Math.round(rect.width*dpr),h=Math.round(rect.height*dpr);
 if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;}drawSpectrum(w,h,dpr);
for(const key of Object.keys(layers)){let peak=0;if(running&&channelAnalysers[key]){const buf=channelMeterBuffers[key];channelAnalysers[key].getFloatTimeDomainData(buf);for(const sample of buf)peak=Math.max(peak,Math.abs(sample));}const target=peak>0?Math.max(0,(20*Math.log10(peak)+48)/48):0;meterSmoothing[key]=running?Math.max(target,(meterSmoothing[key]||0)*.83):0;document.querySelector('[data-meter="'+key+'"]').style.setProperty('--meter-level',Math.min(100,meterSmoothing[key]*100)+'%');}
if(running&&analyser){analyser.getFloatTimeDomainData(masterSamples);let peak=0;for(const sample of masterSamples)peak=Math.max(peak,Math.abs(sample));$('#master-peak').textContent='PEAK '+(peak>.0001?(20*Math.log10(peak)).toFixed(1):'−∞')+' dB';const elapsedBeats=Math.max(0,(ctx.currentTime-playStart)/beat);const phase=(elapsedBeats/16)%1;setDAWPosition(Math.floor(elapsedBeats/4)%4+1,Math.floor(elapsedBeats*4)%16+1);$('.rack-playhead').style.setProperty('--playhead',Math.max(0,phase));}else if(!paused){$('#master-peak').textContent='PEAK −∞ dB';}if(running&&!document.body.classList.contains('motion-paused'))visualFrame=requestAnimationFrame(drawStage);}
function requestStageDraw(){if(scopeVisible&&!visualFrame)visualFrame=requestAnimationFrame(drawStage);}
document.addEventListener('studio-audio-started',requestStageDraw);document.addEventListener('studio-audio-stopped',requestStageDraw);document.addEventListener('studio-audio-paused',requestStageDraw);document.addEventListener('visual-motion-change',requestStageDraw);addEventListener('resize',requestStageDraw);
const stageObserver=new IntersectionObserver(entries=>{scopeVisible=entries[0].isIntersecting;if(scopeVisible&&!visualFrame)visualFrame=requestAnimationFrame(drawStage);else if(!scopeVisible){cancelAnimationFrame(visualFrame);visualFrame=0;}});stageObserver.observe($('#lab'));
document.addEventListener('visibilitychange',()=>{if(!document.hidden&&scopeVisible&&!visualFrame)visualFrame=requestAnimationFrame(drawStage);});

$('#catalog-more').onclick=()=>{showAll=!showAll;render();if(!showAll)$('#collection').scrollIntoView({behavior:matchMedia('(prefers-reduced-motion: reduce)').matches?'instant':'smooth'});};
$('#mobile-menu').onclick=()=>{const open=$('#mobile-menu').getAttribute('aria-expanded')!=='true';$('#mobile-menu').setAttribute('aria-expanded',String(open));$('header').classList.toggle('menu-open',open);};
$$('#main-navigation a').forEach(a=>a.addEventListener('click',()=>{$('#mobile-menu').setAttribute('aria-expanded','false');$('header').classList.remove('menu-open');}));
document.addEventListener('keydown',e=>{if(e.key==='Escape'){if($('header').classList.contains('menu-open'))$('#mobile-menu').focus();$('#mobile-menu').setAttribute('aria-expanded','false');$('header').classList.remove('menu-open');}});

// A familiar channel rack with editable steps and real gain controls.
const noteEdits=Object.fromEntries(Object.keys(layers).map(key=>[key,Array(16).fill(null)]));let sessionPatternDefaults={};
const channelLevels={kick:.8,bass:.8,acid:.8,space:.8,fx:.8};
const patterns={kick:Array.from({length:16},(_,s)=>s%4===0),bass:Array.from({length:16},(_,s)=>s%4!==0),acid:Array.from({length:16},(_,s)=>s%2===0),space:Array.from({length:16},(_,s)=>s%4===0),fx:Array.from({length:16},(_,s)=>s%2===1)};
$$('[data-pattern]').forEach(row=>{const key=row.dataset.pattern;patterns[key].forEach((on,i)=>{const b=document.createElement('button');b.className='step-button';b.type='button';b.dataset.step=String(i);b.setAttribute('aria-label',key+' step '+(i+1));b.setAttribute('aria-pressed',String(on));b.onclick=()=>{patterns[key][i]=!patterns[key][i];b.setAttribute('aria-pressed',String(patterns[key][i]));};row.append(b);});});
$$('[data-level]').forEach(input=>input.oninput=()=>{const key=input.dataset.level,value=Number(input.value)/100;channelLevels[key]=value;if(channelGains[key])channelGains[key].gain.setTargetAtTime(layers[key]?value**2:0,ctx.currentTime,.015);document.querySelector('[data-level-output="'+key+'"]').textContent=value>0?(40*Math.log10(value)).toFixed(1)+' dB':'−∞ dB';});
function updateDAWStep(s){$$('[data-pattern]').forEach(row=>{const key=row.dataset.pattern;[...row.children].forEach((b,i)=>b.classList.toggle('current-step',i===s));});}
$('#reset-session').onclick=()=>{stopLoop();step=0;};
updateLayers();
function applySession(name,atTime){
 scheduledLayerTime=atTime||0;pendingTempo=null;cancelBuild();for(const key of Object.keys(noteEdits))noteEdits[key].fill(null);currentSession=name;const profile=SkaziStudio.profiles[name];
 const presets={roll:{active:['kick','bass','acid','fx'],steps:{kick:[0,4,8,12],bass:[1,2,3,5,6,7,9,10,11,13,14,15],acid:[0,3,6,8,10,11,14],space:[0,8],fx:[2,4,6,10,12,14]}},acid:{active:['kick','bass','acid','space','fx'],steps:{kick:[0,4,8,12],bass:[2,3,6,7,10,11,14,15],acid:[0,1,3,6,8,10,11,14],space:[0,8],fx:[2,4,6,10,12,14,15]}},minimal:{active:['kick','bass','space','fx'],steps:{kick:[0,4,8,12],bass:[3,6,10,14],acid:[2,6,10,14],space:[0,8],fx:[2,4,6,10,12,14]}}};
 const p=presets[name];$$('[data-session]').forEach(b=>{b.setAttribute('aria-pressed',String(b.dataset.session===name));b.classList.remove('is-queued');});
 for(const key of Object.keys(layers)){setLayer(key,p.active.includes(key));patterns[key]=Array.from({length:16},(_,i)=>p.steps[key].includes(i));const row=document.querySelector('[data-pattern="'+key+'"]');[...row.children].forEach((b,i)=>b.setAttribute('aria-pressed',String(patterns[key][i])));}
 sessionPatternDefaults=structuredClone(patterns);const when=atTime??ctx?.currentTime??0,elapsed=ctx?(when-playStart)/beat:0;beat=60/profile.tempo;if(ctx&&(running||paused))playStart=when-elapsed*beat;
 $('#studio-tempo').value=String(profile.tempo);$('#session-title').textContent=profile.title+' / '+profile.key;$('#session-key').textContent=profile.key;
 $('#drop-status').textContent=profile.title+' loaded. Make it yours.';studioEngine?.setBeat(beat,when);document.dispatchEvent(new Event('studio-session-changed'));scheduledLayerTime=0;
}
function applyTempo(value,atTime=ctx?.currentTime||0){const elapsed=ctx?(atTime-playStart)/beat:0;beat=60/value;if(ctx&&(running||paused))playStart=atTime-elapsed*beat;studioEngine?.setBeat(beat,atTime);$('#studio-tempo').value=String(value);}
function queueStudioTempo(value){if(running||paused){pendingTempo=value;$('#drop-status').textContent=value+' BPM queued for the next bar.';}else applyTempo(value);}
function selectSession(name){if(running){pendingSession=name;$$('[data-session]').forEach(b=>b.classList.toggle('is-queued',b.dataset.session===name));$('#drop-status').textContent=SkaziStudio.profiles[name].title+' queued for the next bar.';}else{pendingSession=null;applySession(name);}}
$$('[data-session]').forEach(b=>b.onclick=()=>selectSession(b.dataset.session));
$$('[data-shape]').forEach(input=>input.oninput=()=>{const key=input.dataset.shape,value=Number(input.value);studioControls[key]=value;input.nextElementSibling.value=value+'%';if(key==='master'&&master)master.gain.setTargetAtTime(value/100,ctx.currentTime,.025);else studioEngine?.set(key,value);});
$('#export-loop').onclick=async()=>{
 const button=$('#export-loop'),solo=$$('[data-solo][aria-pressed="true"]').map(b=>b.dataset.solo);button.disabled=true;button.setAttribute('aria-busy','true');$('#drop-status').textContent='Rendering your four-bar loop…';
 try{const snapshot={session:currentSession,tempo:Math.round(60/beat),noteEdits:structuredClone(noteEdits),layers:Object.fromEntries(Object.entries(layers).map(([key,on])=>[key,on&&(!solo.length||solo.includes(key))])),patterns:structuredClone(patterns),levels:{...channelLevels},controls:{...studioControls}};
 const buffer=await SkaziStudio.renderLoop(snapshot),url=URL.createObjectURL(new Blob([SkaziStudio.wav(buffer)],{type:'audio/wav'})),link=document.createElement('a');link.href=url;link.download='skazi-studio-'+snapshot.session+'-'+snapshot.tempo+'bpm.wav';link.click();setTimeout(()=>URL.revokeObjectURL(url),60000);$('#drop-status').textContent='Loop saved · stereo WAV · 44.1 kHz · effects tail included.';
 }catch{ $('#drop-status').textContent='Loop could not be saved in this browser. Try again in a current browser.';}finally{button.disabled=false;button.removeAttribute('aria-busy');}
};
applySession('roll');

// Keep header identity and download action synchronized, including deep-link jumps.
const headerBrand=$('header .brand'),headerDownload=$('#header-download'),heroDownload=$('.hero-copy .actions>.button');
let headerFrame=0,lastHeaderState;
function syncHeaderActions(){
 headerFrame=0;const show=heroDownload.getBoundingClientRect().bottom<=$('header').offsetHeight;
 if(show===lastHeaderState)return;lastHeaderState=show;
 for(const [element,hiddenClass] of [[headerBrand,'header-brand-hidden'],[headerDownload,'header-cta-hidden']]){
  element.classList.toggle(hiddenClass,!show);element.setAttribute('aria-hidden',String(!show));element.tabIndex=show?0:-1;
 }
}
function queueHeaderActions(){if(!headerFrame)headerFrame=requestAnimationFrame(syncHeaderActions);}
addEventListener('scroll',queueHeaderActions,{passive:true});addEventListener('pageshow',queueHeaderActions);
const headerLayoutObserver=new ResizeObserver(queueHeaderActions);headerLayoutObserver.observe($('header'));headerLayoutObserver.observe($('.hero-copy'));syncHeaderActions();


const winDownload=$('#windows-download'),winNote=$('#windows-note');if(winDownload&&winNote){winDownload.addEventListener('click',()=>{winNote.hidden=false;});winNote.querySelector('button').addEventListener('click',()=>{winNote.hidden=true;winDownload.focus();});}


(() => {
  const lab=document.querySelector('#lab'),hint=document.querySelector('#studio-rotate-hint');
  if(!lab||!hint)return;
  let visible=false;
  let dismissed=sessionStorage.getItem('skazi-rotate-tip-dismissed')==='1';
  const update=()=>{hint.hidden=dismissed||!visible||!matchMedia('(max-width: 650px) and (orientation: portrait)').matches;};
  hint.querySelector('button').addEventListener('click',()=>{sessionStorage.setItem('skazi-rotate-tip-dismissed','1');dismissed=true;update();});
  new IntersectionObserver(entries=>{visible=entries[0].isIntersecting;update();},{threshold:.15}).observe(lab);
  addEventListener('resize',update,{passive:true});
})();

