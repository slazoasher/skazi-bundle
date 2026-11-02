'use strict';
(() => {
  const studio=document.querySelector('#lab'),workspace=document.querySelector('#studio-advanced');
  const toggle=document.querySelector('#studio-advanced-toggle');
  const storageKey='skazi-studio-workspace-v1';
  let activePanel='pattern';
  function openWorkspace(open,{persist=true}={}){
    if(!open&&workspace.contains(document.activeElement))toggle.focus({preventScroll:true});
    workspace.hidden=!open;
    toggle.setAttribute('aria-expanded',String(open));
    document.querySelector('#advanced-action-label').textContent=open?'Close workspace':'Open workspace';
    if(persist)try{sessionStorage.setItem(storageKey,JSON.stringify({open,panel:activePanel}));}catch{}
  }
  toggle.addEventListener('click',()=>openWorkspace(workspace.hidden));
  document.querySelector('#studio-advanced-close').addEventListener('click',()=>openWorkspace(false));
  document.addEventListener('studio-panel-activated',event=>{
    activePanel=event.detail.name;
    if(event.detail.open)openWorkspace(true);
  });
  try{
    const saved=JSON.parse(sessionStorage.getItem(storageKey));
    if(saved&&['pattern','mixer','sound'].includes(saved.panel)){
      document.querySelector('[data-studio-panel="'+saved.panel+'"]').click();
      openWorkspace(Boolean(saved.open));
    }
  }catch{}

  // Both level controls address the same gain. The engine retains its original
  // input handler and solo routing; the compact control simply delegates to it.
  for(const compact of document.querySelectorAll('[data-track-level]')){
    const key=compact.dataset.trackLevel,full=document.querySelector('[data-level="'+key+'"]');
    function sync(){
      compact.value=full.value;
      const level=Number(full.value)/100;
      const db=level>0?(40*Math.log10(level)).toFixed(1)+' dB':'−∞ dB';
      document.querySelector('[data-track-level-output="'+key+'"]').value=db;
      compact.setAttribute('aria-valuetext',db);
      full.setAttribute('aria-valuetext',db);
    }
    compact.addEventListener('input',()=>{full.value=compact.value;full.dispatchEvent(new Event('input',{bubbles:true}));});
    full.addEventListener('input',sync);sync();
  }
  const snapshot=()=>JSON.stringify({patterns,noteEdits,channelLevels,layers,studioControls,tempo:Math.round(60/beat),session:currentSession});
  let initial=snapshot();
  function edited(){document.querySelector('#studio-edited').hidden=snapshot()===initial;}
  studio.addEventListener('input',()=>queueMicrotask(edited));
  studio.addEventListener('click',()=>queueMicrotask(edited));
  document.addEventListener('studio-pattern-changed',edited);
  document.addEventListener('studio-session-changed',()=>{initial=snapshot();edited();});
})();
