const {readFileSync}=require('node:fs');
const {join}=require('node:path');
const vm=require('node:vm');
const assert=require('node:assert/strict');
const code=readFileSync(join(__dirname,'../opening.js'),'utf8');
function fixture(reduced=false){
 const handlers={},timers=[],animations=[],stored={};let focus='';
 const animate=()=>{let finish;const finished=new Promise(resolve=>finish=resolve);const animation={finished,cancel(){},finish};animations.push(animation);return animation;};
 const main={inert:false,focus(){focus='main';}},background=[main,{inert:false},{inert:false},{inert:false}];
 const skip={focus(){focus='skip';},addEventListener(name,fn){handlers[name]=fn;}};
 const mark={style:{},getBoundingClientRect:()=>({left:300,top:200,width:400,height:208}),animate};
 const image={naturalWidth:1024,naturalHeight:532,getBoundingClientRect:()=>({left:50,top:100,width:400,height:208}),animate};
 const overlay={hidden:true,focus(){focus='overlay';},querySelector:s=>s==='.intro-skip'?skip:s==='.intro-mark'?mark:{animate},contains:()=>focus==='skip'||focus==='overlay',addEventListener(name,fn){handlers[name]=fn;},animate};
 let pending=true;
 const scope={window:{},document:{documentElement:{classList:{contains:()=>pending,remove:()=>pending=false}},querySelector:s=>s==='#site-intro'?overlay:s==='main'?main:image,querySelectorAll:()=>background,addEventListener(name,fn){handlers[name]=fn;},dispatchEvent(){}},
  innerHeight:900,matchMedia:()=>({matches:reduced,addEventListener(name,fn){handlers['media-'+name]=fn;}}),sessionStorage:{setItem:(key,value)=>stored[key]=value},
  setTimeout:(fn,delay)=>{timers.push({fn,delay});return timers.length;},clearTimeout(){},Event:class{}};
 vm.createContext(scope);vm.runInContext(code,scope);
 return {scope,main,background,overlay,handlers,timers,animations,stored,get pending(){return pending;},get focus(){return focus;}};
}
(async()=>{
 const skip=fixture();assert.ok(skip.background.every(el=>el.inert));assert.equal(skip.focus,'overlay');skip.handlers.click();await skip.scope.window.skaziOpeningReady;
 assert.equal(skip.overlay.hidden,true);assert.ok(skip.background.every(el=>!el.inert));assert.equal(skip.focus,'main');assert.equal(skip.pending,false);
 const keyboard=fixture();let prevented=false;keyboard.handlers.keydown({key:'Tab',preventDefault(){prevented=true;}});assert.equal(prevented,true);assert.equal(keyboard.focus,'skip');
 keyboard.handlers.keydown({key:'Escape',preventDefault(){}});await keyboard.scope.window.skaziOpeningReady;assert.equal(keyboard.pending,false);
 const natural=fixture();natural.timers.find(t=>t.delay===2100).fn();natural.animations.at(-1).finish();await natural.scope.window.skaziOpeningReady;assert.equal(natural.overlay.hidden,true);
 const fallback=fixture();fallback.timers.find(t=>t.delay===4000).fn();await fallback.scope.window.skaziOpeningReady;assert.ok(fallback.background.every(el=>!el.inert));
 const reduced=fixture(true);reduced.timers.find(t=>t.delay===2100).fn();await reduced.scope.window.skaziOpeningReady;assert.equal(reduced.animations.length,0);
 console.log('PASS: intro Skip, Escape, keyboard containment, natural completion, failsafe and reduced-motion exit');
})().catch(error=>{console.error(error);process.exitCode=1;});
