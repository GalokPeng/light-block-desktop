const block=document.getElementById('desktop-assets');
export const deploymentAssets=block?JSON.parse(block.textContent):null;
block?.remove();
export const en = new URLSearchParams(location.search).get('lang') === 'en';
document.documentElement.lang = en ? 'en' : 'zh-CN';
export const t = (zh, english) => en ? english : zh;
export const $ = id => document.getElementById(id);
export const escape = value => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const shapes = {
  settings:'<path d="m10 3 4 0 1 3 3-1 2 3-2 3 2 3-2 3-3-1-1 3h-4l-1-3-3 1-2-3 2-3-2-3 2-3 3 1z"/><circle cx="12" cy="11" r="3"/>',
  about:'<circle cx="12" cy="12" r="9"/><path d="M12 11v6m0-10v1"/>',moon:'<path d="M20 14A8.5 8.5 0 0 1 10 3a9 9 0 1 0 10 11Z"/>',
  sun:'<circle cx="12" cy="12" r="4"/><path d="M12 2v2m0 16v2M2 12h2m16 0h2M4 4l2 2m12 12 2 2M4 20l2-2M18 6l2-2"/>',tasks:'<path d="M3 20h18M6 17v-6m6 6V4m6 13V8"/>',
  wall:'<rect x="3" y="4" width="18" height="16" rx="2"/><circle cx="8" cy="9" r="1.5"/><path d="m4 17 5-5 3 3 3-4 5 6"/>',
  cursor:'<path d="m5 2 15 11-7 1-3 7z"/>', capture:'<path d="M8 3H3v5m13-5h5v5M3 16v5h5m13-5v5h-5"/><rect x="7" y="7" width="10" height="10" rx="1"/>',
  note:'<path d="M4 4h16v11l-5 5H4z"/><path d="M15 20v-5h5M8 9h8m-8 4h5"/>',
  tools:'<rect x="3" y="3" width="7" height="7" rx="2"/><rect x="14" y="3" width="7" height="7" rx="2"/><rect x="3" y="14" width="7" height="7" rx="2"/><rect x="14" y="14" width="7" height="7" rx="2"/>',
  folder:'<path fill="#e4b34f" stroke="#a47524" d="M2 6h8l2 2h10v12H2z"/><path fill="#ffda82" stroke="#f6cf78" d="M2 9h20l-2 10H3z"/>',
  file:'<path fill="#e5eef1" stroke="#526a72" d="M5 2h10l5 5v15H5z"/><path stroke="#7796a1" d="M14 2v6h6M8 12h8m-8 4h6"/>',
  win:'<path stroke="none" fill="#62bafd" d="M2 3h9v9H2zm11 0h9v9h-9zM2 14h9v9H2zm11 0h9v9h-9z"/>',
  search:'<circle cx="10" cy="10" r="6.3"/><path d="m15 15 6 6"/>', volume:'<path d="m3 9 4 0 5-4v14l-5-4H3zM16 8q4 4 0 8m3-11q7 7 0 14"/>',
  signal:'<path d="M3 9q9-9 18 0M6 13q6-6 12 0m-9 4q3-3 6 0"/><circle cx="12" cy="20" r="1"/>',
  minus:'<path d="M5 12h14"/>', close:'<path d="m6 6 12 12M18 6 6 18"/>',add:'<path d="M12 4v16M4 12h16"/>',
  play:'<path d="m7 4 13 8-13 8z"/>', grid:'<rect x="3" y="3" width="18" height="18" rx="3"/><path d="M3 11h18m-10 0v10"/>',
  power:'<path d="M12 2v10m-5-7a8 8 0 1 0 10 0"/>',check:'<path d="m4 12 5 5L21 5"/>',pin:'<path d="m7 3 11 3-2 2-1 5 2 3-12-3 4-2 1-5zM10 14l-3 8"/>'
};
export function icon(name){return '<svg aria-hidden="true" class="icon" viewBox="0 0 24 24">'+shapes[name]+'</svg>';}
export function ib(action,label,name){return '<button class="ib" type="button" data-act="'+action+'" aria-label="'+escape(label)+'" title="'+escape(label)+'">'+icon(name)+'</button>';}
export function button(action,label,primary=false){return '<button class="'+(primary?'action':'pick')+'" data-act="'+action+'">'+escape(label)+'</button>';}
export function choice(action,label,on){return '<button class="choice" data-act="'+action+'" aria-pressed="'+on+'">'+escape(label)+'</button>';}
export function drag(box,handle,desk){
  let origin;
  handle.addEventListener('pointerdown',e=>{
    if(e.button!==0 || e.target.closest('button,a,input,textarea'))return;
    origin={x:e.clientX,y:e.clientY,left:box.offsetLeft,top:box.offsetTop};
    handle.setPointerCapture(e.pointerId);
  });
  handle.addEventListener('pointermove',e=>{
    if(!origin)return;
    box.style.left=Math.max(0,Math.min(desk.clientWidth-box.offsetWidth,origin.left+e.clientX-origin.x))+'px';
    box.style.top=Math.max(0,Math.min(desk.clientHeight-handle.offsetHeight-35,origin.top+e.clientY-origin.y))+'px';
  });
  handle.addEventListener('lostpointercapture',()=>{origin=null;});
}
export function toast(message){$('toast').textContent=message;clearTimeout(toast.timer);toast.timer=setTimeout(()=>{$('toast').textContent='';},1900);}
