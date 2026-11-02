/* Lossless native VST3 recordings; pitch, gate and mix remain browser processing. */
(function(root){
 'use strict';
 const manifestURL=typeof document!=='undefined'?new URL('../audio/native/manifest.json?v=42',document.currentScript.src):null;
 let bank=null,pending=null;
 const curves=new Map();
 function choose(samples,event){
  const distance=Math.min(...samples.map(s=>Math.abs(s.midi-event.note)));
  let candidates=samples.filter(s=>Math.abs(s.midi-event.note)===distance);
  const midi=candidates[0].midi;candidates=candidates.filter(s=>s.midi===midi);
  if(event.track==='acid')return [{sample:candidates.find(s=>s.accent===Boolean(event.accent))||candidates[0],weight:Math.max(.01,event.velocity)}];
  candidates.sort((a,b)=>a.velocity-b.velocity);
  const velocity=Math.max(1,Math.min(127,event.velocity*127));
  const lower=candidates.filter(s=>s.velocity<=velocity).at(-1),upper=candidates.find(s=>s.velocity>=velocity);
  if(!lower||!upper||lower===upper){const sample=lower||upper;return [{sample,weight:velocity/sample.velocity}];}
  const blend=(velocity-lower.velocity)/(upper.velocity-lower.velocity);
  return [{sample:lower,weight:1-blend},{sample:upper,weight:blend}];
 }
 async function load(ctx){
  if(bank)return bank;if(pending)return pending;
  pending=(async()=>{
   const response=await fetch(manifestURL);if(!response.ok)throw Error('Native voice manifest unavailable');
   const manifest=await response.json();if(manifest.version!==1||manifest.sampleRate!==48000)throw Error('Unsupported native voice bank');
   const voices={};await Promise.all(['kick','bass','acid'].map(async track=>{
    const entry=manifest.voices[track];if(!entry?.samples?.length)throw Error('Missing native '+track+' voice');
    if(entry.samples.some(s=>!Number.isInteger(s.midi)||s.midi<0||s.midi>127||!Number.isInteger(s.velocity)||s.velocity<1||s.velocity>127||!Number.isInteger(s.offsetFrames)||s.offsetFrames<0||!Number.isInteger(s.frames)||s.frames<=0||s.offsetFrames+s.frames>entry.frames))throw Error('Invalid native '+track+' sample index');
    const response=await fetch(new URL(entry.file,manifestURL));if(!response.ok)throw Error('Native '+track+' recording unavailable');
    const bytes=await response.arrayBuffer();
    if(bytes.byteLength!==entry.bytes)throw Error('Incomplete native '+track+' recording');
    const buffer=await ctx.decodeAudioData(bytes);
    if(buffer.numberOfChannels!==2||Math.abs(buffer.duration-entry.frames/manifest.sampleRate)>.01)throw Error('Invalid native voice duration');
    voices[track]={...entry,buffer};
   }));bank={manifest,voices};return bank;
  })();
  try{return await pending;}finally{pending=null;}
 }
 function curve(drive){
  const key=Math.round(drive);if(curves.has(key))return curves.get(key);
  const data=new Float32Array(2048),amount=1+key/100;
  for(let i=0;i<data.length;i++){const x=2*i/(data.length-1)-1;data[i]=Math.tanh(x*amount)/Math.tanh(amount);}curves.set(key,data);return data;
 }
 function play(ctx,destination,activeVoices,event,time,beat,controls){
  if(!bank)throw Error('Native recordings must load before playback');
  const voice=bank.voices[event.track],rate=bank.manifest.sampleRate;
  const length=event.length||1,gate=(event.track==='kick'?.20:beat*(event.track==='bass'?.215:event.slide?.42:.32))*length;
  const release=event.track==='bass'?.014:event.track==='acid'?.022:.012;
  const level={kick:2.25,bass:2.0,acid:1.1}[event.track];
  for(const {sample,weight} of choose(voice.samples,event)){
   if(weight<.0001)continue;
   const source=ctx.createBufferSource(),amp=ctx.createGain(),nodes=[amp];source.buffer=voice.buffer;
   const target=Math.pow(2,(event.note-sample.midi)/12),start=event.track==='acid'&&event.slide?Math.pow(2,(event.previous-sample.midi)/12):target;
   source.playbackRate.setValueAtTime(start,time);if(start!==target)source.playbackRate.exponentialRampToValueAtTime(target,time+.026);
   let tail=source;
   if(event.track==='acid'){
    const tone=ctx.createBiquadFilter();tone.type='lowpass';tone.frequency.value=300+Math.pow(controls.tone/100,1.7)*10000;tone.Q.value=.55;tail.connect(tone);tail=tone;nodes.push(tone);
   }
   if(event.track!=='kick'&&controls.drive>0){const shape=ctx.createWaveShaper();shape.curve=curve(controls.drive);shape.oversample='2x';tail.connect(shape);tail=shape;nodes.push(shape);}
   tail.connect(amp).connect(destination);
   const duration=gate+release;
   const peak=level*weight,hold=Math.max(.002,duration-release);
   amp.gain.setValueAtTime(0,time);amp.gain.linearRampToValueAtTime(peak,time+.001);amp.gain.setValueAtTime(peak,time+hold);amp.gain.linearRampToValueAtTime(0,time+duration);
   activeVoices.add(source);source.onended=()=>{activeVoices.delete(source);source.disconnect();for(const node of nodes)node.disconnect();};
   source.start(time,sample.offsetFrames/rate,sample.frames/rate);source.stop(time+duration);
  }
 }
 const api={load,choose,play,ready:()=>Boolean(bank)};
 if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.SkaziNativeVoices=api;
})(typeof window!=='undefined'?window:globalThis);
