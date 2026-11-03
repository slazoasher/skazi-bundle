/* Native sampled kick/bass/acid, with browser synthesis for space/percussion and browser mix effects. */
(function(root){
 'use strict';
 const profiles={
  roll:{tempo:146,bars:16,title:'Forest signal',key:'A minor',bass:[0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0],acid:[12,12,19,15,12,22,19,15,12,24,19,15,22,19,15,12]},
  acid:{tempo:146,title:'Acid pressure',key:'A Phrygian',bass:[0,0,0,0,0,0,0,0,0,0,0,0,0,0,10,0],acid:[12,13,12,19,24,22,19,13,12,24,25,19,22,13,19,12]},
  minimal:{tempo:132,title:'After hours',key:'A minor',bass:[0,0,0,0,0,0,7,0,0,0,0,0,0,0,10,0],acid:[12,19,15,12,22,19,15,12,24,19,15,12,22,19,15,12]}
 };
 // Four-bar call, turnaround, drop and answer. Values are MIDI pitches.
 const forestLead=[
  {0:57,3:60,6:64,8:57,10:55,11:57,14:60},
  {0:57,3:60,6:64,8:67,10:65,11:64},
  {0:69,3:67,6:64,8:60,10:62,11:64,14:67},
  {0:69,3:67,6:64,8:60,10:62,11:60,14:59,15:57}
 ];
 const forestBass=[{}, {10:36,11:40}, {7:45,11:40}, {10:36,11:40,14:40,15:31}];
 const forestSpace=[[69,76],[72,76],[69,79],[65,76]];
 const barsFor=session=>profiles[session]?.bars??4;
 const midiHz=n=>440*Math.pow(2,(n-69)/12);
 function plan(session,bar,step,patterns,layers,edits){
  const p=profiles[session]||profiles.roll,events=[];
  for(const track of ['kick','bass','acid','space','fx']){
   if(!layers[track]||!patterns[track]?.[step])continue;
   let accent=step%4===0||step===11||step===14;
   let note=track==='kick'?33:track==='bass'?33+p.bass[step]:track==='acid'?45+p.acid[(step+(bar===3?2:0))%16]:[69,72,76,79][(step/2+bar|0)%4];
   let slide=track==='acid'&&step>0&&!!patterns.acid[step-1];
   let previous=45+p.acid[(step-1+(bar===3?2:0)+16)%16];
   let velocity=track==='bass'?[.72,.92,.83,1][step%4]:track==='fx'?(step%4===0?.85:.6):accent?1:.78;
   let length, toneOffset, riser;
   if(session==='roll'){
    const phraseBar=bar%4;
    const breakdown=bar===8||bar===9;
    const peak=bar>=12;
    if(breakdown&&['kick','bass'].includes(track)&&!edits?.[track]?.[step])continue;
    if(breakdown&&track==='acid'&&step!==0&&step!==8&&!edits?.acid?.[step])continue;
    if(phraseBar===1&&step>=12&&['kick','bass','acid'].includes(track)&&!edits?.[track]?.[step])continue;
    if(track==='kick')length=.53;
    if(track==='bass'){
     note=forestBass[phraseBar][step]??note;
     length=[1,.84,.91,.78][step%4];
    }
    if(track==='space'){
     note=forestSpace[phraseBar][step<8?0:1];
     velocity=breakdown?.83:phraseBar===2?.72:.5;
    }
    if(track==='fx'){
     velocity=step%4===2?(phraseBar===2?.9:.7):step%4===0?.84:.48;
     riser=bar===9&&step===10;
     if(breakdown&&!riser&&step!==14&&!edits?.fx?.[step])continue;
    }
    if(track==='acid'){
     const phrase=forestLead[phraseBar];
     note=phrase[step]??note;
     slide=Boolean(step>0&&patterns.acid[step-1]&&phrase[step-1]!==undefined);
     previous=phrase[step-1]??previous;
     const lift=(bar>=4&&bar<8&&step>=8)||(peak&&step<8)?12:0;
     note+=lift;
     if(slide&&step!==8)previous+=lift;
     if(breakdown)slide=false;
     accent=step===0||step===6||(phraseBar>=2&&step===14);
     velocity=accent?1:phraseBar>=2?.85:.68;
     length=breakdown?1.65:accent?1.02:.71;
     toneOffset=breakdown?-20:peak?12:bar>=10?8:bar>=4?0:-8;
     if(bar===10&&step===0){note=69;accent=true;velocity=1;length=1.23;}
    }
   }
   events.push({track,note,accent,slide,previous,velocity,length,toneOffset,riser,variant:bar%4===3&&step>=12});
  }
  return events.map(event=>{const edit=edits?.[event.track]?.[step];if(event.track==='acid')event.previous=edits?.acid?.[(step+15)%16]?.note??event.previous;if(!edit)return event;return {...event,note:edit.note??event.note,velocity:edit.velocity===undefined?event.velocity:Math.max(.01,Math.min(1,edit.velocity/100)),length:edit.length===undefined?event.length:Math.max(.25,Math.min(1.75,edit.length/100)),accent:edit.accent??event.accent,slide:edit.slide??event.slide};});
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
 function create(ctx,channels,activeVoices=new Set(),settings={},trackSettings={}){
  const controls={drive:28,tone:54,space:32,...settings};
  const buses={},fxNodes=[];
  const trackControls=structuredClone(trackSettings);
  const wetScale=track=>track==='acid'?.24:.48;
  const wetLevel=track=>(trackControls[track]?.send??controls.space)/100*wetScale(track);
  let noiseSeed=246813579;
  const noise=ctx.createBuffer(1,ctx.sampleRate,ctx.sampleRate),noiseData=noise.getChannelData(0);
  for(let i=0;i<noiseData.length;i++){noiseSeed=(Math.imul(noiseSeed,1664525)+1013904223)|0;noiseData[i]=noiseSeed/2147483648;}
  function fxBus(track,beat){
   if(buses[track])return buses[track];
   const input=ctx.createGain(),hp=ctx.createBiquadFilter();hp.type='highpass';hp.frequency.value=track==='acid'?180:320;
   input.connect(hp).connect(channels[track]);
   const send=ctx.createGain();send.gain.value=wetLevel(track);
   const delay=ctx.createDelay(2);delay.delayTime.value=beat*.75;
   const feedback=ctx.createGain();feedback.gain.value=(trackControls[track]?.feedback??31)/100;
   const lp=ctx.createBiquadFilter();lp.type='lowpass';lp.frequency.value=3300;
   const pan=ctx.createStereoPanner();pan.pan.value=track==='acid'?.34:-.38;
   hp.connect(send).connect(delay).connect(lp).connect(pan).connect(channels[track]);lp.connect(feedback).connect(delay);
   buses[track]={input,delay,send,feedback};fxNodes.push(input,hp,send,delay,feedback,lp,pan);return buses[track];
  }
  function source(node,time,duration,nodes){
   activeVoices.add(node);node.onended=()=>{activeVoices.delete(node);node.disconnect();for(const n of nodes)n.disconnect();};node.start(time);node.stop(time+duration);
  }
  function envelope(g,time,duration,peak,attack=.002){g.gain.setValueAtTime(.00001,time);g.gain.exponentialRampToValueAtTime(Math.max(.00002,peak),time+attack);g.gain.exponentialRampToValueAtTime(.00001,time+duration);}
  function noiseVoice(time,duration,level,frequency,type,pan=0,destination=channels.fx){
   const src=ctx.createBufferSource();src.buffer=noise;
   const filter=ctx.createBiquadFilter();filter.type=type;filter.frequency.value=frequency;filter.Q.value=.7;
   const amp=ctx.createGain();envelope(amp,time,duration,level,.001);
   const stereo=ctx.createStereoPanner();stereo.pan.value=pan;src.connect(filter).connect(amp).connect(stereo).connect(destination);source(src,time,duration+.01,[filter,amp,stereo]);
  }
  function space(event,time,beat,destination=fxBus('space',beat).input){
   const duration=beat*1.35*(event.length||1);
   for(const [offset,pan] of [[0,-.35],[7,.35]]){
    const osc=ctx.createOscillator();osc.type='triangle';osc.frequency.value=midiHz(event.note+offset);osc.detune.value=pan*8;
    const filter=ctx.createBiquadFilter();filter.type='lowpass';filter.frequency.value=2100;const amp=ctx.createGain();envelope(amp,time,duration,.048*event.velocity,.008);
    const stereo=ctx.createStereoPanner();stereo.pan.value=pan;osc.connect(filter).connect(amp).connect(stereo).connect(destination);source(osc,time,duration+.02,[filter,amp,stereo]);
   }
  }
  function movement(event,time,beat,step,destination=channels.fx){
   if(event.riser){
    const duration=beat*1.49;
    const src=ctx.createBufferSource();src.buffer=noise;src.loop=true;
    const filter=ctx.createBiquadFilter();filter.type='bandpass';filter.Q.value=.8;
    filter.frequency.setValueAtTime(340,time);
    filter.frequency.exponentialRampToValueAtTime(5300,time+duration);
    const gain=ctx.createGain();
    gain.gain.setValueAtTime(0,time);
    gain.gain.linearRampToValueAtTime(.23,time+duration-.022);
    gain.gain.linearRampToValueAtTime(0,time+duration);
    src.connect(filter).connect(gain).connect(destination);
    source(src,time,duration,[filter,gain]);
    return;
   }
   if(step===4||step===12){for(const offset of [0,.011,.023])noiseVoice(time+offset,.095*(event.length||1),.12*event.velocity,1600,'bandpass',0,destination);}
   else{noiseVoice(time,beat*(step%4===2?.38:.12)*(event.length||1),.13*event.velocity,7300,'highpass',step%4===2?.18:-.18,destination);if(event.variant&&step===14)noiseVoice(time+beat/8,.035,.045,9200,'highpass',-.25);}
  }
  function play(event,time,beat,step,output){
   const channel=trackControls[event.track]||{};
   if(channel.tune||channel.gate!==undefined){
    const tune=channel.tune||0;
    event={...event,note:Math.min(96,Math.max(24,event.note+tune)),
     previous:event.previous===undefined?undefined:Math.min(96,Math.max(24,event.previous+tune)),
     length:(event.length||1)*(channel.gate??100)/100};
   }
   if(['kick','bass','acid'].includes(event.track)){
    const destination=output||(event.track==='acid'?fxBus('acid',beat).input:channels[event.track]);
    const voiceControls={...controls,...channel};
    if(event.toneOffset)voiceControls.tone=Math.max(0,Math.min(100,voiceControls.tone+event.toneOffset));
    root.SkaziNativeVoices.play(ctx,destination,activeVoices,event,time,beat,voiceControls);
   }else if(event.track==='space')space(event,time,beat,output);
   else movement(event,time,beat,step,output);
  }
  function updateSend(track){
   const bus=buses[track];
   if(!bus)return;
   bus.send.gain.setTargetAtTime(wetLevel(track),ctx.currentTime,.025);
   bus.feedback.gain.setTargetAtTime((trackControls[track]?.feedback??31)/100,ctx.currentTime,.025);
  }
  function set(name,value){
   controls[name]=Math.max(0,Math.min(100,Number(value)||0));
   if(name==='space')for(const track of Object.keys(buses))updateSend(track);
  }
  function setTrack(track,settings){
   trackControls[track]={...settings};
   updateSend(track);
  }
  function reset(){for(const node of fxNodes)node.disconnect();fxNodes.length=0;for(const key of Object.keys(buses))delete buses[key];}
  function setBeat(beat,time=ctx.currentTime){for(const bus of Object.values(buses))bus.delay.delayTime.setTargetAtTime(beat*.75,Math.max(ctx.currentTime,time),.025);}
  return {play,set,setTrack,reset,setBeat,controls};
 }
 async function renderLoop(snapshot){
  const sampleRate=44100,beat=60/snapshot.tempo,bars=barsFor(snapshot.session);
  let tail=1.5;
  for(const track of ['acid','space']){
   const controls=snapshot.trackSettings?.[track];
   const feedback=(controls?.feedback??31)/100;
   const send=controls?.send??snapshot.controls.space;
   if(!snapshot.layers[track]||!send||feedback<=0)continue;
   const repeats=1+Math.ceil(Math.log(.001)/Math.log(feedback));
   tail=Math.max(tail,beat*(1.35*1.75+.75*repeats)+.15);
  }
  const duration=.03+bars*beat*4+tail;
  const Offline=root.OfflineAudioContext||root.webkitOfflineAudioContext;if(!Offline)throw Error('Offline audio is unavailable in this browser.');
  const ctx=new Offline(2,Math.ceil(duration*sampleRate),sampleRate),master=ctx.createGain();master.gain.value=snapshot.controls.master/100;const fade=ctx.createGain();fade.connect(ctx.destination);
  fade.gain.setValueAtTime(1,duration-.08);fade.gain.linearRampToValueAtTime(0,duration-1/sampleRate);
  mastering(ctx,master,fade);
  const channels={};for(const key of Object.keys(snapshot.layers)){channels[key]=ctx.createGain();channels[key].gain.value=snapshot.levels[key]**2;const pan=ctx.createStereoPanner();pan.pan.value=(snapshot.trackSettings?.[key]?.pan??0)/100;channels[key].connect(pan).connect(master);}
  await loadVoices(ctx);const engine=create(ctx,channels,new Set(),snapshot.controls,snapshot.trackSettings);
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
 const api={profiles,barsFor,midiHz,plan,mastering,create,renderLoop,wav,loadVoices};if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.SkaziStudio=api;
})(typeof window!=='undefined'?window:globalThis);
