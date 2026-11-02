'use strict';
(() => {
 const panel=document.querySelector('#panel-pattern');
 const inspector=document.createElement('div');inspector.className='note-inspector';
 inspector.innerHTML='<div class="note-inspector-heading"><div><b id="note-selection">Bass · Step 01</b><p>Click to add or select · Shift-click or Delete to remove</p></div><div class="pattern-tools"><button type="button" id="note-undo" disabled>Undo</button><button type="button" id="pattern-clear">Clear</button><button type="button" id="pattern-restore">Reset</button></div></div><div class="note-controls"><button type="button" id="note-enabled" aria-pressed="false">Note off</button><label id="pitch-control">Pitch <output id="note-pitch-name">A1</output><input type="range" min="24" max="96" step="1" id="note-pitch" aria-label="Selected note pitch"></label><label>Velocity <output id="note-velocity-value">90%</output><input type="range" min="1" max="100" id="note-velocity" aria-label="Selected note velocity"></label><label>Length <output id="note-length-value">100%</output><input type="range" min="25" max="175" id="note-length" aria-label="Selected note length"></label><div class="acid-articulation"><button type="button" id="note-accent" aria-pressed="false">Accent</button><button type="button" id="note-slide" aria-pressed="false">Slide</button></div></div><p class="pattern-hint">16 steps per bar · edits repeat across the four-bar loop</p><span class="sr" id="pattern-status" aria-live="polite"></span>';
 panel.append(inspector);
 let track=document.querySelector('[data-select-track][aria-pressed="true"]').dataset.selectTrack,selectedStep=1,editStart=null;
 const history=Object.fromEntries(Object.keys(layers).map(key=>[key,[]]));
 const name=n=>['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'][n%12]+(Math.floor(n/12)-1);
 function state(){return {pattern:[...patterns[track]],notes:structuredClone(noteEdits[track])};}
 function remember(snapshot=state()){history[track].push(snapshot);if(history[track].length>32)history[track].shift();}
 function selectedEvent(){return SkaziStudio.plan(currentSession,0,selectedStep,{...patterns,[track]:Array(16).fill(true)},{[track]:true},noteEdits)[0];}
 function sync(){
  const event=selectedEvent(),on=patterns[track][selectedStep],melodic=['bass','acid','space'].includes(track);
  const title=document.querySelector('[data-select-track="'+track+'"] b').textContent;
  $('#note-selection').textContent=title+' · Step '+String(selectedStep+1).padStart(2,'0');
  $('#note-enabled').textContent=on?'Note on':'Note off';$('#note-enabled').setAttribute('aria-pressed',String(on));
  $('#pitch-control').hidden=!melodic;$('#note-pitch').disabled=!melodic;$('#note-pitch').value=event.note;$('#note-pitch').setAttribute('aria-valuetext',name(event.note));$('#note-pitch-name').value=name(event.note);
  $('#note-velocity').value=Math.round(event.velocity*100);$('#note-velocity-value').value=Math.round(event.velocity*100)+'%';
  $('#note-length').value=Math.round((event.length||1)*100);$('#note-length-value').value=Math.round((event.length||1)*100)+'%';
  $('.acid-articulation').hidden=track!=='acid';for(const field of ['accent','slide'])$('#note-'+field).setAttribute('aria-pressed',String(event[field]));
  $('#note-undo').disabled=!history[track].length;
  for(const row of $$('[data-pattern]'))for(const [i,b] of [...row.children].entries()){
   const key=row.dataset.pattern,active=patterns[key][i],note=SkaziStudio.plan(currentSession,0,i,{...patterns,[key]:Array(16).fill(true)},{[key]:true},noteEdits)[0];b.style.setProperty('--note-position',String(1+Math.min(36,Math.max(0,note.note-(key==='bass'?24:48)))/36*5));b.style.setProperty('--note-opacity',String(.35+note.velocity*.65));b.title=name(note.note)+' · Velocity '+Math.round(note.velocity*100)+'% · Length '+Math.round((note.length||1)*100)+'%';b.setAttribute('aria-pressed',String(active));b.classList.toggle('edit-selected',key===track&&i===selectedStep);
   b.setAttribute('aria-label',key+' step '+(i+1)+(active?', note on':', note off')+(key===track&&i===selectedStep?', selected':''));
  }
 }
 function changed(message){sync();document.dispatchEvent(new Event('studio-pattern-changed'));if(message)$('#pattern-status').textContent=message;}
 function toggleNote(){remember();patterns[track][selectedStep]=!patterns[track][selectedStep];changed('Step '+(selectedStep+1)+(patterns[track][selectedStep]?' added.':' removed.'));}
 for(const row of $$('[data-pattern]'))for(const [i,b] of [...row.children].entries()){
  b.onclick=event=>{track=row.dataset.pattern;selectedStep=i;if(event.shiftKey)toggleNote();else{if(!patterns[track][i]){remember();patterns[track][i]=true;}changed();}};
  b.addEventListener('keydown',event=>{if(['ArrowLeft','ArrowRight','Home','End'].includes(event.key)){event.preventDefault();selectedStep=event.key==='Home'?0:event.key==='End'?15:(i+(event.key==='ArrowRight'?1:15))%16;sync();row.children[selectedStep].focus();}else if(event.key==='Delete'||event.key==='Backspace'){event.preventDefault();selectedStep=i;if(patterns[track][i])toggleNote();}});
 }
 $('#note-enabled').onclick=toggleNote;
 for(const [id,property] of [['note-pitch','note'],['note-velocity','velocity'],['note-length','length']]){
  const input=$('#'+id);input.addEventListener('focus',()=>{editStart=state();});input.addEventListener('pointerdown',()=>{editStart=state();});
  input.oninput=()=>{if(!editStart)editStart=state();noteEdits[track][selectedStep]={...noteEdits[track][selectedStep],[property]:Number(input.value)};patterns[track][selectedStep]=true;changed();};
  input.onchange=()=>{if(editStart){remember(editStart);editStart=null;sync();}};
 }
 for(const property of ['accent','slide'])$('#note-'+property).onclick=()=>{remember();noteEdits[track][selectedStep]={...noteEdits[track][selectedStep],[property]:!selectedEvent()[property]};patterns[track][selectedStep]=true;changed();};
 $('#pattern-clear').onclick=()=>{remember();patterns[track].fill(false);changed('Pattern cleared. Undo is available.');};
 $('#pattern-restore').onclick=()=>{remember();patterns[track]=[...sessionPatternDefaults[track]];noteEdits[track].fill(null);changed('Track restored to its session pattern.');};
 $('#note-undo').onclick=()=>{const previous=history[track].pop();if(!previous)return;patterns[track]=previous.pattern;noteEdits[track]=previous.notes;changed('Last edit undone.');};
 document.addEventListener('studio-track-selected',()=>{track=document.querySelector('[data-select-track][aria-pressed="true"]').dataset.selectTrack;editStart=null;sync();});
 document.addEventListener('studio-session-changed',()=>{for(const key of Object.keys(history))history[key]=[];editStart=null;sync();});
 sync();
})();
