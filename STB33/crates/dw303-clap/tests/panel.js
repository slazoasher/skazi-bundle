// Dependency-free probe of DW-303's custom wiring; RAVET widgets are stubbed.
// Run: node crates/dw303-clap/tests/panel.js
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync('editor/index.html','utf8'),nodes=new Map(),messages=[];
class Node {
  constructor(id){this.id=id;this.listeners={};this.dataset={};this.attrs={};this.style={setProperty:(k,v)=>this.attrs[k]=v};}
  addEventListener(k,f){(this.listeners[k]??=[]).push(f);}
  setAttribute(k,v){this.attrs[k]=v;} getAttribute(k){return this.attrs[k];} querySelector(){return null;}
  focus(){} setPointerCapture(){}
  emit(type,extra={}){for(const f of this.listeners[type]||[])f({button:0,pointerId:1,detail:0,preventDefault(){},...extra});}
}
for(const match of html.matchAll(/id="([^"]+)"/g))nodes.set(match[1],new Node(match[1]));
const hits=[...html.matchAll(/<button id="([^"]+)" class="hit"/g)].map(m=>nodes.get(m[1]));
const lamps=[...nodes.values()].filter(n=>n.id.startsWith('led-'));
class Knob {constructor(node,number,spec,post){this.node=node;this.current=spec.value;this.spec=spec;this.post=post;}paint(v){this.current=v;}update(v){this.paint(v);}commit(v){this.paint(v);this.post({type:'edit',value:v});}}
class Simple {constructor(){}update(){}}
const windowListeners={};
const context=vm.createContext({document:{getElementById:id=>nodes.get(id),querySelectorAll:q=>q==='.hit'?hits:lamps},innerWidth:1227,innerHeight:589,addEventListener(k,f){(windowListeners[k]??=[]).push(f);},setInterval(){},setTimeout:f=>f(),performance:{now:()=>0},RavetControls:{Knob,Toggle:Simple,Choice:Simple},webkit:{messageHandlers:{ravet:{postMessage:m=>messages.push(m)}}}});
context.window=context;
vm.runInContext(html.slice(html.lastIndexOf('<script>')+8,html.lastIndexOf('</script>')),context);
function evaluate(code){return vm.runInContext(code,context);}
function edge(id){nodes.get(id).emit('pointerdown');nodes.get(id).emit('pointerup');}
function actions(){return messages.splice(0).map(m=>[m.action,m.key,m.edge,m.value]);}
edge('run-stop');assert.deepStrictEqual(actions(),[['button','run-stop','press',undefined],['button','run-stop','release',undefined]]);
nodes.get('key-c').emit('keydown',{key:'Enter'});nodes.get('key-c').emit('keyup',{key:'Enter'});assert.deepStrictEqual(actions().map(a=>a.slice(0,3)),[['button','key-c','press'],['button','key-c','release']]);
edge('function');assert.equal(actions()[0][1],'clear');edge('clear');assert.equal(actions()[0][1],'time-mode');edge('time-mode');assert.equal(actions()[0][1],'key-back');
evaluate('editMode=0');edge('key-slide');assert.deepStrictEqual(actions()[0],['section',undefined,undefined,0]);edge('key-back');assert.deepStrictEqual(actions()[0],['section',undefined,undefined,1]);
nodes.get('key-down').emit('pointerdown');edge('key-up');edge('key-accent');nodes.get('key-down').emit('pointerup');assert.deepStrictEqual(actions().map(a=>a[1]),['function','key-step','key-step','key-triplet','key-triplet','function']);
// Shift-click latches: pattern key held across a CLEAR click, released by clicking it again.
nodes.get('key-c').emit('pointerdown',{shiftKey:true});nodes.get('key-c').emit('pointerup');nodes.get('key-c').emit('blur');edge('function');edge('key-c');assert.deepStrictEqual(actions().map(a=>a.slice(1,3)),[['key-c','press'],['clear','press'],['clear','release'],['key-c','release']]);
evaluate('selection={pattern:3};editMode=0');nodes.get('function').emit('pointerdown',{shiftKey:true});nodes.get('function').emit('pointerup');assert.deepStrictEqual(actions().map(a=>a.slice(1,3)),[['key-e','press'],['clear','press'],['clear','release'],['key-e','release']]);evaluate('selection=null');
nodes.get('key-down').emit('pointerdown',{shiftKey:true});edge('key-up');edge('key-up');edge('key-down');assert.deepStrictEqual(actions().map(a=>a.slice(1,3)),[['function','press'],['key-step','press'],['key-step','release'],['key-step','press'],['key-step','release'],['function','release']]);
nodes.get('key-c').emit('pointerdown');nodes.get('key-c').emit('pointercancel');nodes.get('key-c').emit('lostpointercapture');assert.equal(actions().length,2);
for(const [name,count] of [['mode',4],['pattern-group',7]]){for(let value=0;value<count;value++){evaluate(`selectors.get('${name}').commit(${value})`);assert.deepStrictEqual(actions()[0],[name,undefined,undefined,value]);}}
evaluate('window.ravetUpdate({params:{12:1},leds:{run:1,"step-1":2},selection:{mode:3,editMode:1,group:0,track:0,section:"A",pattern:1,step:4}})');assert.equal(nodes.get('led-run').dataset.on,'1');assert.equal(nodes.get('led-step-1').dataset.on,'1');assert(nodes.get('status').textContent.includes('Pattern Write'));evaluate('window.ravetUpdate({leds:{}})');assert(lamps.every(l=>l.dataset.on==='0'));
// Real host snapshots use one-based pattern numbers; every white key must form the CLEAR chord.
for(const [i,key] of ['key-c','key-d','key-e','key-f','key-g','key-a','key-b','key-c-up'].entries()){
  evaluate(`window.ravetUpdate({selection:{mode:3,editMode:0,group:3,track:6,section:"A",pattern:${i+1},step:1}})`);
  assert(nodes.get('status').textContent.includes(`Pattern Write · IVA pattern ${i+1} · step 1`));
  const clear=nodes.get('function');clear.emit('pointerdown',{shiftKey:true});clear.emit('pointerup',{shiftKey:true});clear.emit('lostpointercapture');clear.emit('click',{detail:1,shiftKey:true});
  assert.deepStrictEqual(messages.splice(0).map(m=>JSON.parse(JSON.stringify(m))),[['press',key],['press','clear'],['release','clear'],['release',key]].map(([edge,key])=>({action:'button',key,edge,type:'panel'})));
  assert.equal(evaluate('held.size'),0);assert.equal(evaluate('latched.size'),0);
}
// Pattern Write submode buttons lock during RUN; local routing must agree with the editor.
for(const editMode of [0,1,2]){
  evaluate(`window.ravetUpdate({params:{12:1},selection:{mode:3,editMode:${editMode},group:3,track:6,section:"A",pattern:1,step:1}})`);
  for(const id of ['pitch-mode','clear','key-down','run-stop']){edge(id);assert.equal(evaluate('editMode'),editMode);}
  actions();
}
evaluate('window.ravetUpdate({params:{12:0},selection:{mode:3,editMode:2,group:3,track:6,section:"A",pattern:1,step:1}})');
edge('key-down');assert.equal(evaluate('editMode'),0);actions();
// Computer-keyboard shortcuts: physical codes, real holds for chords, repeats and Cmd ignored.
function key(type,code,extra={}){for(const f of windowListeners[type]||[])f({code,repeat:false,shiftKey:false,metaKey:false,ctrlKey:false,altKey:false,target:null,preventDefault(){},stopPropagation(){},...extra});}
evaluate('editMode=0;selection={pattern:2}');actions();
key('keydown','KeyA');key('keydown','KeyA',{repeat:true});key('keyup','KeyA');key('keydown','KeyK');key('keyup','KeyK');key('keydown','KeyW');key('keyup','KeyW');
assert.deepStrictEqual(actions().map(a=>a.slice(1,3)),[['key-c','press'],['key-c','release'],['key-c-up','press'],['key-c-up','release'],['key-c-sharp','press'],['key-c-sharp','release']]);
key('keydown','Digit2');key('keydown','KeyZ');key('keyup','KeyZ');key('keyup','Digit2');
assert.deepStrictEqual(actions().map(a=>a.slice(1,3)),[['function','press'],['key-step','press'],['key-step','release'],['function','release']]);
key('keydown','Digit1',{shiftKey:true});key('keyup','Digit1');
assert.deepStrictEqual(actions().map(a=>a.slice(1,3)),[['key-d','press'],['clear','press'],['clear','release'],['key-d','release']]);
key('keydown','KeyA',{metaKey:true});key('keydown','Space',{target:{classList:{contains:c=>c==='hit'}}});assert.equal(actions().length,0);
key('keydown','Space');key('keyup','Space');key('keydown','Period');key('keyup','Period');key('keydown','Comma');key('keyup','Comma');key('keydown','Enter');key('keydown','Backspace');
assert.deepStrictEqual(actions().filter(a=>a[0]==='button').map(a=>a[1]),['run-stop','run-stop','tap','tap','key-back','key-back']);
evaluate('selection=null');
console.log('Panel wiring checks passed: edges, artwork bindings, sections, secondary keys, selectors, LEDs/status.');
