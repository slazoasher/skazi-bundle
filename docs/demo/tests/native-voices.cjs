const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const source=fs.readFileSync(path.join(__dirname,'../native-voices.js'),'utf8');
const manifest=JSON.parse(fs.readFileSync(path.join(__dirname,'../../audio/native/manifest.json'),'utf8'));
function fixture(failOnce=false){
 let calls=0,failed=false;const scope={module:{exports:{}},URL,Float32Array,document:{currentScript:{src:'https://example.test/project/demo/native-voices.js'}},fetch:async url=>{
  calls++;if(failOnce&&!failed){failed=true;throw Error('Offline');}
  if(String(url).includes('manifest.json'))return {ok:true,json:async()=>structuredClone(manifest)};
  const entry=Object.values(manifest.voices).find(v=>String(url).endsWith(v.file));assert.ok(entry,'Asset URL must preserve the project prefix');assert.ok(String(url).startsWith('https://example.test/project/audio/native/'));return {ok:true,arrayBuffer:async()=>new ArrayBuffer(entry.bytes)};
 }};
 const ctx={decodeAudioData:async bytes=>{const entry=Object.values(manifest.voices).find(v=>v.bytes===bytes.byteLength);return {numberOfChannels:2,duration:entry.frames/48000};}};
 vm.createContext(scope);vm.runInContext(source,scope);return {api:scope.module.exports,ctx,calls:()=>calls};
}
(async()=>{
 const f=fixture();assert.equal(f.api.ready(),false);const [a,b]=await Promise.all([f.api.load(f.ctx),f.api.load(f.ctx)]);assert.equal(a,b);assert.equal(f.calls(),4,'Parallel requests must share one manifest and three bank downloads');assert.equal(f.api.ready(),true);
 await f.api.load(f.ctx);assert.equal(f.calls(),4,'Loaded banks must be cached');
 const retry=fixture(true);await assert.rejects(retry.api.load(retry.ctx),/Offline/);assert.equal(retry.api.ready(),false);await retry.api.load(retry.ctx);assert.equal(retry.api.ready(),true);
 for(const track of ['bass','acid'])for(let note=24;note<=96;note++)for(const velocity of [.01,.38,.69,.94,1]){
  const selected=f.api.choose(manifest.voices[track].samples,{track,note,velocity,accent:true});assert.ok(selected.length>=1&&selected.length<=2);assert.ok(selected.every(v=>Math.abs(v.sample.midi-note)<=1&&Number.isFinite(v.weight)&&v.weight>0));
  if(track==='acid')assert.ok(selected.every(v=>v.sample.accent));
 }
 const normal=f.api.choose(manifest.voices.acid.samples,{track:'acid',note:57,velocity:1,accent:false});assert.equal(normal[0].sample.accent,false);
 const blended=f.api.choose(manifest.voices.bass.samples,{track:'bass',note:33,velocity:100/127});assert.equal(blended.length,2);assert.ok(Math.abs(blended.reduce((n,s)=>n+s.weight,0)-1)<1e-9);
 const sources=[],param=()=>({setValueAtTime(){},linearRampToValueAtTime(){},exponentialRampToValueAtTime(){}}),node=()=>({connect(target){return target;},disconnect(){}});
 f.ctx.createBufferSource=()=>{const s={...node(),playbackRate:param(),start(...args){this.started=args;},stop(time){this.stopped=time;}};sources.push(s);return s;};
 f.ctx.createGain=()=>({...node(),gain:param()});f.ctx.createBiquadFilter=()=>({...node(),frequency:{},Q:{}});
 const active=new Set();f.api.play(f.ctx,node(),active,{track:'acid',note:24,previous:96,accent:true,slide:true,velocity:1,length:1.75},1,.5,{drive:0,tone:54});
 assert.equal(sources.length,1);assert.ok(sources[0].started[2]<=.601,'A source is restricted to its own sample region');assert.ok(sources[0].stopped>1.3,'A downward slide must not truncate the gate using its initial pitch rate');sources[0].onended();assert.equal(active.size,0);
 console.log('PASS: project-relative native assets, concurrent/cached loading, retry after failure, full MIDI range, velocity interpolation and native accent selection');
})().catch(error=>{console.error(error);process.exitCode=1;});
