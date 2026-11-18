import { $, t, escape as esc, icon, ib, button, choice, drag, toast } from './ui.mjs';
import { startCapture, startRecording, longCapture, cancelCapture, stopRecording } from './capture.mjs';

import { renderCatalog, leaveCatalog } from './catalog.mjs';

const product=t('轻块桌面','Light Block Desktop');
document.title=product+' · '+t('在线体验','Desktop playground');
const wallpapers=[
  ['mountain-bay',t('山海 · 晨光海湾','Mountain Bay'), 'image'],
  ['milky-way',t('银河 · 高山湖泊','Milky Way'), 'image'],
  ['night-drive',t('夜驰 · 城郊高速','Night Drive'), 'image'],
  ['rain-coast',t('海岸 · 雨夜弯道','Rain Coast'), 'image'],
  ['startrail',t('流光 · 星轨','Star Trail'), 'gif'],
  ['motion',t('律动 · 光的呼吸','Breathing Light'), 'video']
];
const sections={wall:t('壁纸','Wallpapers'),cursor:t('美化','Appearance'),capture:t('截屏','Capture'),note:t('便签','Notes'),tools:t('工具','Tools')};
const typeLabel={image:t('静态','Still'),gif:'GIF',video:t('视频','Video')};
const state={tab:'wall',filter:'all',wall:0,playing:true,appearance:'grid',styled:true,barColor:'#223b39',barAlpha:88,barRadius:12,collected:false,place:'bottom',plate:33,shelf:55,files:[],notes:[],noteColor:'#f9e9a6',cursor:'system',color:'#9aebcb',effect:'off',size:100,tool:'launch',clips:[],workspace:null};
const reduced=matchMedia('(prefers-reduced-motion: reduce)');
let shown=true, appZ=3, noteSeq=0, fileSeq=0;
const thumb=i=>wallpapers[i][3]?.poster || (wallpapers[i][2]==='image'?'/media/wallpapers/'+wallpapers[i][0]+'.webp':'./media/'+wallpapers[i][0]+'.webp');
function row(label,control){return '<label class="row"><span>'+esc(label)+'</span>'+control+'</label>';}
function range(key,label,min,max,value){return row(label,'<input type="range" data-key="'+key+'" aria-label="'+esc(label)+'" min="'+min+'" max="'+max+'" value="'+value+'">');}
function select(key,label,values){return row(label,'<select data-key="'+key+'">'+Object.entries(values).map(([v,name])=>'<option value="'+v+'" '+(state[key]===v?'selected':'')+'>'+esc(name)+'</option>').join('')+'</select>');}
function check(key,label){return row(label,'<input type="checkbox" data-key="'+key+'" '+(state[key]?'checked':'')+'>');}
function color(key,label){return row(label,'<input type="color" data-key="'+key+'" value="'+state[key]+'">');}
function barButtons(list){return '<div class="strip">'+list.join('')+'</div>';}
function searchField(id,label){return '<input class="field" type="search" id="'+id+'" aria-label="'+esc(label)+'" placeholder="'+esc(label)+'">';}
function nameFile(name,folder=false){return {id:++fileSeq,name,folder};}
state.files=[nameFile(t('我的文档','Documents'),true),nameFile(t('照片','Pictures'),true),nameFile(t('灵感.txt','Ideas.txt'))];
$('product').textContent=product;
$('win-actions').innerHTML=ib('hide',t('最小化','Minimize'),'minus')+ib('hide',t('关闭窗口','Close window'),'close');
$('rail').setAttribute('aria-label',t('演示功能','Demo features'));
function renderNav(){
  $('rail').innerHTML=Object.entries(sections).map(([k,v])=>'<button data-act="show:'+k+'" aria-pressed="'+(k===state.tab)+'">'+icon(k)+'<span>'+v+'</span></button>').join('');
}
function show(tab){
  if(!sections[tab])return;
  state.tab=tab;$('app').hidden=false;$('app').style.zIndex=++appZ;
  renderNav();render();$('content').scrollTop=0;
}
function updateScene(){
  const [id,,type,external]=wallpapers[state.wall], motion=state.playing&&shown&&!document.hidden;
  const video=$('movie'), image=$('wall');
  const src=type==='gif'&&motion?(external?.url||'./media/startrail.gif'):thumb(state.wall);
  if(image.getAttribute('src')!==src)image.src=src;
  if(type==='video'){
    const url=external?.url||'./media/'+id+'.mp4';
    if(video.getAttribute('src')!==url){video.src=url;video.poster=thumb(state.wall);}
    video.hidden=false;
    if(motion)video.play().catch(()=>{if(video.paused){video.hidden=true;}});else video.pause();
  }else{video.pause();video.hidden=true;if(video.hasAttribute('src')){video.removeAttribute('src');video.load();}}
}
function renderWalls(){
  const [,name,type]=wallpapers[state.wall];
  const online=choice('filter:online',t('在线精选','Online'),state.filter==='online');
  if(state.filter==='online')return barButtons([button('filter:all',t('‹ 本地壁纸','‹ Local library')),online]);
  return '<div class="spot"><img src="'+thumb(state.wall)+'" alt=""><div><small>'+t('当前壁纸','Current wallpaper')+'</small><strong>'+esc(name)+'</strong></div></div>'+
    barButtons(['all','image','gif','video'].map(k=>choice('filter:'+k,k==='all'?t('全部','All'):typeLabel[k],state.filter===k)).concat(online))+
    '<div class="gallery">'+wallpapers.map((w,i)=>state.filter!=='all'&&w[2]!==state.filter?'':'<button class="thumb" data-act="wall:'+i+'" aria-pressed="'+(state.wall===i)+'"><img src="'+thumb(i)+'" alt=""><span>'+esc(w[1])+'</span></button>').join('')+'</div>'+
    (type!=='image'?barButtons([button('pause',state.playing?t('暂停动态壁纸','Pause motion'):t('播放动态壁纸','Play motion'))]):'');
}
function renderAppearance(){
  const names={grid:t('收纳','Organizer'),bar:t('任务栏','Taskbar'),cursor:t('指针','Pointer')};
  const tabs=barButtons(Object.entries(names).map(([k,v])=>choice('appearance:'+k,v,state.appearance===k)));
  if(state.appearance==='bar')return tabs+check('styled',t('三段居中任务栏','Center taskbar in three groups'))+
    color('barColor',t('任务栏颜色','Taskbar color'))+range('barAlpha',t('不透明度','Opacity'),20,100,state.barAlpha)+range('barRadius',t('圆角','Corner radius'),0,20,state.barRadius);
  if(state.appearance==='cursor')return tabs+select('cursor',t('指针样式','Pointer style'),{system:t('系统','System'),stream:t('流线','Stream'),prism:t('棱镜','Prism'),pixel:t('像素','Pixel')})+
    color('color',t('指针与粒子颜色','Pointer and particle color'))+range('size',t('指针大小','Pointer size'),75,200,state.size)+select('effect',t('移动特效','Pointer effect'),{off:t('关闭','Off'),dot:t('点点','Dots'),fall:t('飘落','Falling'),glow:t('光芒','Glow')});
  return tabs+check('collected',t('收纳桌面图标','Collect desktop icons'))+select('place',t('收纳位置','Shelf position'),{top:t('居上','Top'),center:t('居中','Center'),bottom:t('居下','Bottom')})+
    range('shelf',t('收纳背景不透明度','Shelf opacity'),0,100,state.shelf)+range('plate',t('图标底板不透明度','Icon plate opacity'),0,100,state.plate)+
    barButtons([button('newfile',t('新建示例文件','New sample file')),button('hide',t('查看桌面','Show desktop'))]);
}
function renderCapture(){
  return '<div class="group"><h2>'+t('截取桌面区域','Capture a region')+'</h2><p>'+t('拖动框选后，可以涂鸦、画框并贴到桌面。','Drag a selection, annotate it, then pin it to the desktop.')+'</p>'+
    barButtons([button('capture',t('开始截屏','Take screenshot'),true),button('long',t('截取示例长图','Capture sample scroll'))])+'</div>'+
    '<div class="group"><h2>'+t('录制虚拟桌面','Record the virtual desktop')+'</h2><p>'+t('开始后可以打开应用、移动便签。最多录制 12 秒，结束后直接回放。','Open the app or move notes while recording. Play back up to 12 seconds when you stop.')+'</p>'+
    barButtons([button('record',t('开始录屏','Start recording'),true)])+'</div>';
}
function renderNotes(){return barButtons([button('newnote',t('新建桌面便签','New desktop note'),true)])+color('noteColor',t('新便签颜色','New note color'))+
  '<div class="list">'+state.notes.map(n=>'<button data-act="edit:'+n.id+'">'+(n.done?'✓ ':'')+esc(n.text.split('\n')[0]||t('空白便签','Untitled note'))+'</button>').join('')+'</div>';}
const toolNames={launch:t('快速启动','Launcher'),clips:t('剪贴板','Clipboard'),files:t('文件工作台','Files'),focus:t('专注','Focus')};
let focusEnd=0;const fmt=s=>Math.floor(s/60)+':'+String(s%60).padStart(2,'0');
function renderTools(){
  const tabs=barButtons(Object.entries(toolNames).map(([k,v])=>choice('tool:'+k,v,state.tool===k)));
  if(state.tool==='launch')return tabs+searchField('lookup',t('搜索演示文件或功能','Find demo files or features'))+'<div class="list" id="matches"></div>';
  if(state.tool==='clips')return tabs+'<p>'+t('保存演示文字，点击条目可把它贴成便签。','Save a snippet, then select it to make a desktop note.')+'</p><input class="field" id="clip-text" maxlength="500" aria-label="'+t('演示文字','Demo text')+'">'+barButtons([button('clip-add',t('存入演示剪贴板','Save snippet'))])+'<div class="list">'+state.clips.map((c,i)=>'<button data-act="clip:'+i+'">'+esc(c)+'</button>').join('')+'</div>';
  if(state.tool==='files')return tabs+'<div class="list">'+state.files.map(f=>'<div class="row"><span>'+esc(f.name)+'</span>'+button('rename:'+f.id,t('重命名','Rename'))+'</div>').join('')+'</div><div id="rename-pane"></div>'+barButtons([button('newfile',t('新增文件','Add file'))]);
  return tabs+'<div class="group"><h2 id="timer-text">'+fmt(focusEnd?Math.max(0,Math.ceil((focusEnd-Date.now())/1000)):60)+'</h2>'+barButtons([button('timer',focusEnd?t('结束专注','End focus'):t('专注一分钟','Focus for a minute'),true)])+'</div>'+barButtons([button('save-work',t('保存当前桌面布局','Save workspace')),button('restore-work',t('恢复布局','Restore workspace'))]);
}
function matches(q=''){
  if(!$('matches'))return;
  const items=[...Object.entries(sections).map(([k,v])=>[v,'show:'+k]),...state.files.map(f=>[f.name,'file:'+f.id])];
  $('matches').innerHTML=items.filter(([name])=>name.toLowerCase().includes(q.trim().toLowerCase())).map(([name,act])=>button(act,name)).join('')||'<p>'+t('没有匹配项','No matches')+'</p>';
}
function render(){
  const panels={wall:renderWalls,cursor:renderAppearance,capture:renderCapture,note:renderNotes,tools:renderTools};
  $('content').innerHTML='<h1>'+sections[state.tab]+'</h1>'+panels[state.tab]();
  if(state.tab==='wall'&&state.filter==='online')renderCatalog($('content'),(entry,blob)=>{
    const prior=wallpapers[6]?.[3].url, url=URL.createObjectURL(blob);
    wallpapers[6]=[entry.id,t(entry.title.zh,entry.title.en),entry.kind,{url,poster:entry.kind==='video'||entry.kind==='gif'?entry.thumbnail_url:url}];
    state.filter='all';state.wall=6;state.playing=!reduced.matches;updateScene();render();
    if(prior)URL.revokeObjectURL(prior);
  });
  else leaveCatalog();
  matches();
}
function files(){
  const shelf=$('files');
  shelf.classList.toggle('collected',state.collected);shelf.dataset.place=state.place;
  $('desk').style.setProperty('--plate',state.plate/100);$('desk').style.setProperty('--shelf',state.shelf/100);
  shelf.innerHTML=state.files.map(f=>'<button class="file-tile" data-act="file:'+f.id+'"><i>'+icon(f.folder?'folder':'file')+'</i><span>'+esc(f.name)+'</span></button>').join('');
}
function taskbar(){
  const bar=$('bar');bar.classList.toggle('styled',state.styled);
  bar.style.setProperty('--bar-color',state.barColor+Math.round(state.barAlpha*2.55).toString(16).padStart(2,'0'));bar.style.setProperty('--bar-radius',state.barRadius+'px');
  bar.innerHTML='<div class="bar-part">'+ib('show:tools',t('开始','Start'),'win')+ib('launch',t('搜索','Search'),'search')+'</div>'+
    '<div class="bar-part"><button class="ib" data-act="app-toggle" aria-label="'+product+'"><img src="/media/icon.png" alt=""></button>'+ib('show:wall',sections.wall,'wall')+ib('show:capture',sections.capture,'capture')+ib('show:note',sections.note,'note')+'</div>'+
    '<div class="bar-part">'+icon('signal')+'<time class="clock"></time></div>';
  tick();
}
function tick(){
  const clock=document.querySelector('.clock');if(clock)clock.textContent=new Date().toLocaleTimeString(document.documentElement.lang,{hour:'2-digit',minute:'2-digit',hour12:false});
  if(focusEnd){const left=Math.max(0,Math.ceil((focusEnd-Date.now())/1000));if($('timer-text'))$('timer-text').textContent=fmt(left);if(!left){focusEnd=0;toast(t('专注时间结束','Focus session complete'));if(state.tab==='tools')render();}}
}
function note(text='',initial=false){
  const id=++noteSeq,n={id,text,done:false};state.notes.push(n);
  const box=document.createElement('section');box.className='note';box.id='sticky-'+id;box.style.backgroundColor=state.noteColor;
  box.style.left=(initial?81:Math.min(58,20+(id%4)*8))+'%';box.style.top=(initial?19:16+(id%4)*7)+'%';
  box.innerHTML='<div class="note-head"><span>'+icon('pin')+' '+t('便签','Note')+'</span><div><input class="note-color" type="color" value="'+state.noteColor+'" title="'+t('便签颜色','Note color')+'" aria-label="'+t('便签颜色','Note color')+'">'+ib('done:'+id,t('标记完成','Mark done'),'check')+ib('remove:'+id,t('删除便签','Delete note'),'close')+'</div></div><textarea maxlength="1200" spellcheck="false" aria-label="'+t('便签正文','Note text')+'"></textarea>';
  const picker=box.querySelector('.note-color');picker.oninput=()=>{box.style.backgroundColor=picker.value;};
  const area=box.querySelector('textarea');area.value=text;area.oninput=()=>{n.text=area.value;if(state.tab==='note')render();};
  $('stickies').append(box);drag(box,box.firstElementChild,$('desk'));box.addEventListener('pointerdown',()=>box.style.zIndex=++appZ);
  if(!initial){box.style.zIndex=++appZ;area.focus();}return box;
}
function cursor(){
  const shapes={stream:'M4 2 23 15 14 16 10 24Z',prism:'M2 2 23 11 14 14 11 24Z M2 2 14 14',pixel:'M3 2H6V5H9V8H12V11H15V14H18V17H11V23H8V17H3Z'};
  const size=Math.round(state.size*.27),path=shapes[state.cursor];const desk=$('desk');
  if(path){const svg='<svg xmlns="http://www.w3.org/2000/svg" width="'+size+'" height="'+size+'" viewBox="0 0 26 26"><path d="'+path+'" fill="'+state.color+'" stroke="#132b29" stroke-width="1.8"/></svg>';
    desk.style.setProperty('--pointer','url("data:image/svg+xml,'+encodeURIComponent(svg)+'") 3 2, auto');
  }else desk.style.removeProperty('--pointer');
  desk.classList.toggle('custom-cursor',!!path);desk.style.setProperty('--particle-color',state.color);
}
let lastSpark=0;
$('desk').addEventListener('pointermove',e=>{
  if(state.effect==='off'||reduced.matches||e.pointerType==='touch'||performance.now()-lastSpark<36)return;
  lastSpark=performance.now();const p=document.createElement('i');p.className='particle '+state.effect;p.setAttribute('data-html2canvas-ignore','');
  const size=state.effect==='glow'?16:4;p.style.cssText='left:'+(e.clientX-size/2)+'px;top:'+(e.clientY-size/2)+'px;width:'+size+'px;height:'+size+'px;--dx:'+Math.round(Math.random()*16-8)+'px;--dy:'+(state.effect==='fall'?'32':'6')+'px';
  $('desk').append(p);p.addEventListener('animationend',()=>p.remove(),{once:true});setTimeout(()=>p.remove(),850);
});
function openFile(id){
  const file=state.files.find(f=>f.id===Number(id));if(!file)return;
  state.tool='files';show('tools');$('content').querySelector('[data-act="rename:'+file.id+'"]')?.focus();
}
function rename(id){
  const f=state.files.find(f=>f.id===Number(id));if(!f)return;
  const pane=$('rename-pane');pane.innerHTML='<form class="strip"><input maxlength="60" class="field" required aria-label="'+t('文件名','File name')+'"><button class="action">'+t('保存名称','Save name')+'</button></form>';
  const field=pane.querySelector('input');field.value=f.name;field.select();
  pane.firstChild.onsubmit=e=>{e.preventDefault();if(field.value.trim()){f.name=field.value.trim();files();render();}};
}
function handle(action){
  const [act,arg]=action.split(':');
  switch(act){
    case 'show':return show(arg);
    case 'hide':$('app').hidden=true;return;
    case 'app-toggle':if($('app').hidden)show(state.tab);else $('app').hidden=true;return;
    case 'wall':state.wall=Number(arg);state.playing=!reduced.matches;updateScene();break;
    case 'filter':state.filter=arg;break;
    case 'pause':state.playing=!state.playing;updateScene();break;
    case 'appearance':state.appearance=arg;break;
    case 'tool':state.tool=arg;break;
    case 'launch':state.tool='launch';show('tools');$('lookup').focus();return;
    case 'file':return openFile(arg);
    case 'rename':return rename(arg);
    case 'newfile':if(state.files.length>=9){toast(t('演示最多容纳九个文件','The demo holds nine files'));return;}state.files.push(nameFile(t('文档 ','File ')+(fileSeq+1)+'.txt'));files();break;
    case 'newnote':if(state.notes.length>=8){toast(t('演示最多八张便签','The demo holds eight notes'));return;}note();break;
    case 'edit':{const box=$('sticky-'+arg);box.style.zIndex=++appZ;box.querySelector('textarea').focus();return;}
    case 'remove':state.notes=state.notes.filter(n=>n.id!==Number(arg));$('sticky-'+arg)?.remove();break;
    case 'done':{const n=state.notes.find(n=>n.id===Number(arg));n.done=!n.done;$('sticky-'+arg).querySelector('textarea').style.textDecoration=n.done?'line-through':'none';break;}
    case 'capture':return startCapture();
    case 'record':return startRecording();
    case 'long':return longCapture();
    case 'clip-add':{const s=$('clip-text').value.trim();if(s)state.clips=[s,...state.clips.filter(c=>c!==s)].slice(0,8);break;}
    case 'clip':if(state.notes.length<8)note(state.clips[Number(arg)]);break;
    case 'timer':focusEnd=focusEnd?0:Date.now()+60000;break;
    case 'save-work':state.workspace={wall:state.wall,collected:state.collected,place:state.place};toast(t('已保存演示布局','Workspace saved'));break;
    case 'restore-work':if(state.workspace){Object.assign(state,state.workspace);updateScene();files();toast(t('已恢复布局','Workspace restored'));}else toast(t('请先保存一个布局','Save a workspace first'));break;
    default:return;
  }
  render();
}
let menu;
function closeMenu(){menu?.remove();menu=null;}
$('desk').addEventListener('contextmenu',e=>{
  if(e.target.closest('#app,.note,.result,#capture-layer,#bar'))return;
  e.preventDefault();closeMenu();menu=document.createElement('div');menu.className='desk-menu';
  menu.innerHTML=button('newnote',t('新建桌面便签','New desktop note'))+button('newfile',t('新建示例文件','New sample file'))+button('show:cursor',sections.cursor);
  $('desk').append(menu);menu.style.left=Math.max(0,Math.min(e.clientX,innerWidth-menu.offsetWidth))+'px';menu.style.top=Math.max(0,Math.min(e.clientY,innerHeight-menu.offsetHeight))+'px';
  menu.querySelector('button').focus();menu.onclick=e=>{const act=e.target.closest('[data-act]')?.dataset.act;e.stopPropagation();closeMenu();if(act)handle(act);};
});
document.addEventListener('pointerdown',e=>{if(menu&&!menu.contains(e.target))closeMenu();});
$('desk').addEventListener('click',e=>{if(e.target.closest('#capture-layer,#results,#rec-state'))return;const b=e.target.closest('[data-act]');if(b)handle(b.dataset.act);});
$('desk').addEventListener('input',e=>{
  if(e.target.id==='lookup')return matches(e.target.value);
  const key=e.target.dataset.key;if(!key||!Object.hasOwn(state,key))return;
  const input=e.target;state[key]=input.type==='checkbox'?input.checked:input.type==='range'?Number(input.value):input.value;
  if(['barColor','barAlpha','barRadius','styled'].includes(key))taskbar();
  if(['size','color','effect','cursor'].includes(key))cursor();
  if(['shelf','plate','place','collected'].includes(key))files();
});
$('app').addEventListener('pointerdown',()=>$('app').style.zIndex=++appZ);
drag($('app'),document.querySelector('.titlebar'),$('desk'));
document.addEventListener('keydown',e=>{if(e.key==='Escape'){if(menu)return closeMenu();if(!$('capture-layer').hidden)cancelCapture();else if(!$('rec-state').hidden)stopRecording();else $('app').hidden=true;}});
function keepInBounds(){const desk=$('desk');for(const box of desk.querySelectorAll('.window,.note,.result')){
  if(box.hidden)continue;
  if(box.offsetLeft+box.offsetWidth>desk.clientWidth)box.style.left=Math.max(0,desk.clientWidth-box.offsetWidth-2)+'px';
  if(box.offsetTop+30>desk.clientHeight-35)box.style.top=Math.max(0,desk.clientHeight-70)+'px';
}}
const observer=new IntersectionObserver(([entry])=>{shown=entry.isIntersecting;updateScene();if(!shown)stopRecording();});
observer.observe($('desk'));document.addEventListener('visibilitychange',updateScene);
window.addEventListener('resize',keepInBounds);
renderNav();render();files();taskbar();updateScene();note(t('周末计划\n去海边，拍一张新壁纸。','Weekend plan\nVisit the coast. Find a new wallpaper.'),true);keepInBounds();
setInterval(()=>{if(shown&&!document.hidden)tick();},1000);
