/* Native sampled kick/bass/acid, with browser synthesis for space/percussion and browser mix effects. */
(function(root){
 'use strict';
 const profiles={
  roll:{tempo:142,title:'First light',key:'A minor',bass:[0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0],acid:[12,12,19,15,12,22,19,15,12,24,19,15,22,19,15,12]},
  acid:{tempo:146,title:'Acid pressure',key:'A Phrygian',bass:[0,0,0,0,0,0,0,0,0,0,0,0,0,0,10,0],acid:[12,13,12,19,24,22,19,13,12,24,25,19,22,13,19,12]},
  minimal:{tempo:132,title:'After hours',key:'A minor',bass:[0,0,0,0,0,0,7,0,0,0,0,0,0,0,10,0],acid:[12,19,15,12,22,19,15,12,24,19,15,12,22,19,15,12]}
 };
 const midiHz=n=>440*Math.pow(2,(n-69)/12);
 function plan(session,bar,step,patterns,layers,edits){
  const p=profiles[session]||profiles.roll,events=[];
  for(const track of ['kick','bass','acid','space','fx']){
   if(!layers[track]||!patterns[track]?.[step])continue;
   const accent=step%4===0||step===11||step===14;
   const note=track==='kick'?33:track==='bass'?33+p.bass[step]:track==='acid'?45+p.acid[(step+(bar===3?2:0))%16]:[69,72,76,79][(step/2+bar|0)%4];
   events.push({track,note,accent,slide:track==='acid'&&step>0&&!!patterns.acid[step-1],previous:45+p.acid[(step-1+(bar===3?2:0)+16)%16],velocity:track==='bass'?[0,.92,.83,1][step%4]||.72:track==='fx'?(step%4===0?.85:.6):accent?1:.78,variant:bar===3&&step>=12});
  }
  return events.map(event=>{const edit=edits?.[event.track]?.[step];if(event.track==='acid')event.previous=edits?.acid?.[(step+15)%16]?.note??event.previous;if(!edit)return event;return {...event,note:edit.note??event.note,velocity:edit.velocity===undefined?event.velocity:Math.max(.01,Math.min(1,edit.velocity/100)),length:edit.length===undefined?1:Math.max(.25,Math.min(1.75,edit.length/100)),accent:edit.accent??event.accent,slide:edit.slide??event.slide};});
 }
 function mastering(ctx,input,destination){
  const highpass=ctx.createBiquadFilter();highpass.type='highpass';highpass.frequency.value=24;highpass.Q.value=.5;
  const glue=ctx.createDynamicsCompressor();glue.threshold.value=-12;glue.knee.value=10;glue.ratio.value=3;glue.attack.value=.008;glue.release.value=.12;
  const ceiling=ctx.createGain();ceiling.gain.value=.82;
  const guard=ctx.createWaveShaper(),curve=new Float32Array(4096);
  for(let i=0;i<curve.length;i++){const x=i*2/(curve.length-1)-1,a=Math.abs(x);curve[i]=Math.sign(x)*(a<=.8?a:.8+.18*Math.tanh((a-.8)/.18));}guard.curve=curve;guard.oversample='4x';
  const analyser=ctx.createAnalyser();analyser.fftSize=2048;analyser.smoothingTimeConstant=.78;analyser.minDecibels=-84;analyser.maxDecibels=-12;
  const dcBlock=ctx.createBiquadFilter();dcBlock.type='highpass';dcBlock.frequency.value=8;dcBlock.Q.value=.5;
  input.connect(highpass).connect(glue).connect(guard).connect(dcBlock).connect(ceiling).connect(analyser).connect(destination);
  return analyser;
 }
 function create(ctx,channels,activeVoices=new Set(),settings={}){
  const controls={drive:28,tone:54,space:32,...settings};
  const buses={},fxNodes=[],wetGains=[];
  let noiseSeed=246813579;
  const noise=ctx.createBuffer(1,ctx.sampleRate,ctx.sampleRate),noiseData=noise.getChannelData(0);
  for(let i=0;i<noiseData.length;i++){noiseSeed=(Math.imul(noiseSeed,1664525)+1013904223)|0;noiseData[i]=noiseSeed/2147483648;}
  function fxBus(track,beat){
   if(buses[track])return buses[track];
   const input=ctx.createGain(),hp=ctx.createBiquadFilter();hp.type='highpass';hp.frequency.value=track==='acid'?180:320;
   input.connect(hp).connect(channels[track]);
   const send=ctx.createGain();send.gain.value=controls.space/100*(track==='acid'?.24:.48);
   const delay=ctx.createDelay(2);delay.delayTime.value=beat*.75;
   const feedback=ctx.createGain();feedback.gain.value=.31;
   const lp=ctx.createBiquadFilter();lp.type='lowpass';lp.frequency.value=3300;
   const pan=ctx.createStereoPanner();pan.pan.value=track==='acid'?.34:-.38;
   hp.connect(send).connect(delay).connect(lp).connect(pan).connect(channels[track]);lp.connect(feedback).connect(delay);
   buses[track]={input,delay};wetGains.push({node:send,scale:track==='acid'?.24:.48});fxNodes.push(input,hp,send,delay,feedback,lp,pan);return buses[track];
  }
  function source(node,time,duration,nodes){
   activeVoices.add(node);node.onended=()=>{activeVoices.delete(node);node.disconnect();for(const n of nodes)n.disconnect();};node.start(time);node.stop(time+duration);
  }
  function envelope(g,time,duration,peak,attack=.002){g.gain.setValueAtTime(.00001,time);g.gain.exponentialRampToValueAtTime(Math.max(.00002,peak),time+attack);g.gain.exponentialRampToValueAtTime(.00001,time+duration);}
  function noiseVoice(time,duration,level,frequency,type,pan=0){
   const src=ctx.createBufferSource();src.buffer=noise;
   const filter=ctx.createBiquadFilter();filter.type=type;filter.frequency.value=frequency;filter.Q.value=.7;
   const amp=ctx.createGain();envelope(amp,time,duration,level,.001);
   const stereo=ctx.createStereoPanner();stereo.pan.value=pan;src.connect(filter).connect(amp).connect(stereo).connect(channels.fx);source(src,time,duration+.01,[filter,amp,stereo]);
  }
  function space(event,time,beat){
   const duration=beat*1.35*(event.length||1);
   for(const [offset,pan] of [[0,-.35],[7,.35]]){
    const osc=ctx.createOscillator();osc.type='triangle';osc.frequency.value=midiHz(event.note+offset);osc.detune.value=pan*8;
    const filter=ctx.createBiquadFilter();filter.type='lowpass';filter.frequency.value=2100;const amp=ctx.createGain();envelope(amp,time,duration,.048*event.velocity,.008);
    const stereo=ctx.createStereoPanner();stereo.pan.value=pan;osc.connect(filter).connect(amp).connect(stereo).connect(fxBus('space',beat).input);source(osc,time,duration+.02,[filter,amp,stereo]);
   }
  }
  function movement(event,time,beat,step){
   if(step===4||step===12){for(const offset of [0,.011,.023])noiseVoice(time+offset,.095*(event.length||1),.12*event.velocity,1600,'bandpass');}
   else{noiseVoice(time,beat*(step%4===2?.38:.12)*(event.length||1),.13*event.velocity,7300,'highpass',step%4===2?.18:-.18);if(event.variant&&step===14)noiseVoice(time+beat/8,.035,.045,9200,'highpass',-.25);}
  }
  function play(event,time,beat,step){if(['kick','bass','acid'].includes(event.track))root.SkaziNativeVoices.play(ctx,event.track==='acid'?fxBus('acid',beat).input:channels[event.track],activeVoices,event,time,beat,controls);else if(event.track==='space')space(event,time,beat);else movement(event,time,beat,step);}
  function set(name,value){controls[name]=Math.max(0,Math.min(100,Number(value)||0));if(name==='space')for(const wet of wetGains)wet.node.gain.setTargetAtTime(controls.space/100*wet.scale,ctx.currentTime,.025);}
  function reset(){for(const node of fxNodes)node.disconnect();fxNodes.length=0;wetGains.length=0;for(const key of Object.keys(buses))delete buses[key];}
  function setBeat(beat,time=ctx.currentTime){for(const bus of Object.values(buses))bus.delay.delayTime.setTargetAtTime(beat*.75,Math.max(ctx.currentTime,time),.025);}
  return {play,set,reset,setBeat,controls};
 }
 async function renderLoop(snapshot){
  const sampleRate=44100,beat=60/snapshot.tempo,bars=4,duration=bars*beat*4+1.5;
  const Offline=root.OfflineAudioContext||root.webkitOfflineAudioContext;if(!Offline)throw Error('Offline audio is unavailable in this browser.');
  const ctx=new Offline(2,Math.ceil(duration*sampleRate),sampleRate),master=ctx.createGain();master.gain.value=snapshot.controls.master/100;mastering(ctx,master,ctx.destination);
  const channels={};for(const key of Object.keys(snapshot.layers)){channels[key]=ctx.createGain();channels[key].gain.value=snapshot.levels[key]**2;channels[key].connect(master);}
  await loadVoices(ctx);const engine=create(ctx,channels,new Set(),snapshot.controls);
  for(let bar=0;bar<bars;bar++)for(let step=0;step<16;step++)for(const event of plan(snapshot.session,bar,step,snapshot.patterns,snapshot.layers,snapshot.noteEdits))engine.play(event,.03+(bar*16+step)*beat/4,beat,step);
  const buffer=await ctx.startRendering();engine.reset();return buffer;
 }
 function wav(buffer){
  const channels=buffer.numberOfChannels,length=buffer.length,bytes=new ArrayBuffer(44+length*channels*2),view=new DataView(bytes);
  const text=(at,s)=>{for(let i=0;i<s.length;i++)view.setUint8(at+i,s.charCodeAt(i));};text(0,'RIFF');view.setUint32(4,bytes.byteLength-8,true);text(8,'WAVE');text(12,'fmt ');view.setUint32(16,16,true);view.setUint16(20,1,true);view.setUint16(22,channels,true);view.setUint32(24,buffer.sampleRate,true);view.setUint32(28,buffer.sampleRate*channels*2,true);view.setUint16(32,channels*2,true);view.setUint16(34,16,true);text(36,'data');view.setUint32(40,length*channels*2,true);
  const data=Array.from({length:channels},(_,i)=>buffer.getChannelData(i));let offset=44;
  for(let i=0;i<length;i++)for(let ch=0;ch<channels;ch++){const sample=Math.max(-1,Math.min(1,data[ch][i]));view.setInt16(offset,Math.round(sample*(sample<0?32768:32767)),true);offset+=2;}return bytes;
 }
 async function loadVoices(ctx){if(!root.SkaziNativeVoices)throw Error('Native voice loader unavailable');return root.SkaziNativeVoices.load(ctx);}
 const api={profiles,midiHz,plan,mastering,create,renderLoop,wav,loadVoices};if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.SkaziStudio=api;
})(typeof window!=='undefined'?window:globalThis);
