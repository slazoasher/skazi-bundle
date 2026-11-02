// Regression tests for asynchronous audio startup and stale scheduled UI work.
const {readFileSync}=require('node:fs');
const {join}=require('node:path');
const vm=require('node:vm');
const assert=require('node:assert/strict');
const source=readFileSync(join(__dirname,'../demo.js'),'utf8');
const lifecycle=source.slice(source.indexOf('function schedule()'),source.indexOf("$('#transport').onclick"));
function fixture(){
 let resolveAudio,rejectAudio;const pending=new Promise((resolve,reject)=>{resolveAudio=resolve;rejectAudio=reject;});
 const elements=new Map(),timers=[],intervals=new Set(),events=[];
 const element=()=>({textContent:'',set innerHTML(value){this.textContent=value.replace(/<[^>]*>/g,'');},focus(){},classList:{add(){},remove(){},toggle(){}},style:{setProperty(){}},setAttribute(){},removeAttribute(){}});
 const scope={SkaziStudio:require('../studio-engine.js'),studioEngine:{play(){},reset(){}},studioControls:{master:65},currentSession:'roll',noteEdits:{},pendingSession:null,pendingTempo:null,buildBusy:false,running:false,starting:false,paused:false,pausing:false,activeVoices:new Set(),runGeneration:0,step:0,nextTime:0,timer:null,beat:60/138,playStart:0,
  ctx:{currentTime:1,suspend:()=>Promise.resolve()},master:{gain:{cancelScheduledValues(){},setValueAtTime(){},setTargetAtTime(){}}},
  setupAudio:()=>pending,stopPreview(){},stopCompare(){},cancelBuild(){},
  $:selector=>{if(!elements.has(selector))elements.set(selector,element());return elements.get(selector);},$$:()=>[],
  document:{hidden:false,dispatchEvent:event=>events.push(event.type)},Event:class{constructor(type){this.type=type;}},
  layers:{kick:false,bass:false,acid:false,space:false,fx:false},patterns:{},cells:Array.from({length:16},element),
  tone(){},updateDAWStep:()=>events.push('step'),setTimeout:fn=>timers.push(fn),
  setInterval:fn=>{intervals.add(fn);return fn;},clearInterval:id=>intervals.delete(id)};
 vm.createContext(scope);vm.runInContext(lifecycle,scope);
 return {scope,resolveAudio,rejectAudio,timers,intervals,events};
}
(async()=>{
 const a=fixture();const first=a.scope.startLoop(),duplicate=a.scope.startLoop();a.resolveAudio();
 assert.equal(await first,true);assert.equal(await duplicate,false);assert.equal(a.intervals.size,1);
 assert.ok(a.events.includes('studio-audio-started'));a.scope.stopLoop();assert.equal(a.intervals.size,0);
 const b=fixture();const pending=b.scope.startLoop();b.scope.stopLoop();b.resolveAudio();
 assert.equal(await pending,false);assert.equal(b.scope.running,false);assert.equal(b.intervals.size,0);
 const c=fixture();const started=c.scope.startLoop();c.resolveAudio();await started;
 const stale=c.timers.slice();c.scope.stopLoop();await c.scope.startLoop();stale.forEach(fn=>fn());
 assert.equal(c.events.filter(e=>e==='step').length,0,'Previous session must not move the new session cursor');
 c.scope.stopLoop();assert.equal(c.scope.$('#daw-position').textContent,'01 : 01');
 const d=fixture();const failed=d.scope.startLoop();d.rejectAudio(new Error('Audio unavailable'));
 assert.equal(await failed,false);assert.equal(d.scope.starting,false);assert.equal(d.scope.running,false);
 assert.equal(d.scope.$('#transport').textContent,'Retry audio');
 const e=fixture();const playing=e.scope.startLoop();e.resolveAudio();await playing;
 const originalStart=e.scope.playStart;e.scope.ctx.currentTime=1.32;const savedStep=e.scope.step;
 assert.equal(await e.scope.pauseLoop(),true);assert.equal(e.scope.paused,true);assert.equal(e.intervals.size,0);
 assert.equal(e.scope.$('#transport').textContent,'Resume');assert.equal(e.scope.$('#reset-session').hidden,false);
 assert.equal(await e.scope.startLoop(),true);assert.equal(e.scope.playStart,originalStart,'Resume must preserve the audio clock origin');
 assert.ok(e.scope.step>=savedStep);assert.equal(e.scope.paused,false);assert.equal(e.intervals.size,1);
 e.scope.stopLoop();assert.equal(e.scope.$('#reset-session').hidden,true);assert.equal(e.scope.$('#transport').textContent,'Play');
 const f=fixture();const queued=[];f.scope.pendingSession='acid';f.scope.applySession=(name,time)=>queued.push({name,time});f.scope.buildBusy=true;f.scope.advanceBuild=time=>queued.push({build:time});const fstart=f.scope.startLoop();f.resolveAudio();await fstart;
 assert.equal(queued[0].name,'acid');assert.equal(queued[0].time,f.scope.playStart);assert.equal(queued[1].build,f.scope.playStart);assert.equal(f.scope.pendingSession,null);f.scope.stopLoop();
 console.log('PASS: duplicate start, stop during resume, stale callbacks, rewind, audio failure, pause/resume clock, conditional Stop');
})().catch(error=>{console.error(error);process.exitCode=1;});
