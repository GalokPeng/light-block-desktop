import { $, t, escape as esc, icon, ib, button, choice, drag, toast } from './ui.mjs';
import { copiedImage, pinCopied, openStudio, startCapture, startRecording, longCapture, cancelCapture, stopRecording } from './capture.mjs';

import {exportPreferences} from './studio.mjs';
import { desktopPointer } from './pointer.mjs';
import { renderCatalog, leaveCatalog } from './catalog.mjs';

const product=t('轻块桌面','Light Block Desktop');
document.title=product+' · '+t('在线体验','Desktop playground');
const wallpapers=[
  ['night-drive',t('夜驰 · 城郊高速跑车','Night drive'),'image'],
  ['milky-way',t('银河 · 高山湖泊星空','Milky Way'),'image'],
  ['mountain-bay',t('山海 · 晨光与海湾','Mountain bay'),'image'],
  ['rain-coast',t('海岸 · 雨夜弯道','Rain coast'),'image'],
  ['startrail',t('星轨弧光','Star trail · GIF'),'gif'],
  ['motion',t('光的呼吸','Motion · Video'),'video']
];
const builtinInfo={'night-drive':'1920 × 1080 · 0.56 MB · JPG','milky-way':'1920 × 1080 · 0.92 MB · JPG','mountain-bay':'1920 × 1080 · 1.15 MB · JPG','rain-coast':'1920 × 1080 · 0.98 MB · JPG'};
const sections={wall:t('壁纸','Wallpapers'),cursor:t('美化','Appearance'),capture:t('截屏','Capture'),tools:t('工具','Tools'),settings:t('设置','Settings'),about:t('关于','About')};
const typeLabel={image:t('静态','Still'),gif:'GIF',video:t('视频','Video')};
const state={tab:'wall',library:'local',filter:'all',query:'',selected:0,wall:2,fit:'cover',playing:true,preview:false,sound:false,
  theme:'dark',appearance:'grid',styled:true,barColor:'#253143',barAlpha:88,barRadius:12,collected:false,place:'bottom',plate:33,shelf:55,shelfColor:'#19242f',plateColor:'#354454',iconSize:1,sort:'name',autoCollect:true,files:[],notes:[],noteColor:'#f9e9a6',cursor:'stream',color:'#d9f2ed',particles:false,particleColor:'#97efcf',effect:'dot',size:100,tool:'home',clips:[],workspace:null,fullPause:true,batteryPause:true,resume:true,restoreNotes:true,night:false,hotkey:true,chord:'Ctrl+Alt+A',fps:'0',resolution:'0'};
const reduced=matchMedia('(prefers-reduced-motion: reduce)');
let battery;
navigator.getBattery?.().then(b=>{battery=b;b.addEventListener('chargingchange',updateScene);updateScene();}).catch(()=>{});
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
$('desk').ariaLabel=t('虚拟 Windows 桌面','Virtual Windows desktop');
$('desk').addEventListener('pointerenter',e=>{if(e.pointerType==='mouse'&&!document.hasFocus())$('desk').focus({preventScroll:true});});
$('desk').addEventListener('pointerleave',()=>{if(document.activeElement===$('desk'))$('desk').blur();});
$('win-actions').innerHTML=ib('hide',t('最小化','Minimize'),'minus')+ib('maximize',t('最大化 / 还原','Maximize / restore'),'grid')+ib('hide',t('关闭窗口','Close window'),'close');
$('rail').setAttribute('aria-label',t('演示功能','Demo features'));
function renderNav(){
  $('rail').innerHTML='<div class="rail-brand"><img src="/media/icon.png" alt=""><div><b>'+product+'</b><small>'+t('桌面工作台','Desktop workspace')+'</small></div></div><small class="rail-label">'+t('工作空间','Workspace')+'</small>'+Object.entries(sections).map(([k,v])=>'<button data-act="show:'+k+'" title="'+v+'" aria-label="'+v+'" aria-pressed="'+(k===state.tab)+'">'+icon(k)+'<span>'+v+'</span></button>').join('')+'<button class="rail-update" data-act="version">'+t('已是最新版本','Up to date')+'</button>';
}
function show(tab){
  if(tab==='note'){state.tool='notes';tab='tools';}
  if(!sections[tab])return;
  if(tab==='tools'&&state.tab!==tab&&state.tool!=='notes'&&state.tool!=='launch'&&state.tool!=='files')state.tool='home';
  if(state.tab!==tab)state.preview=false;
  state.tab=tab;$('app').hidden=false;deskPolicy();$('app').style.zIndex=++appZ;
  renderNav();render();$('content').scrollTop=0;
}
function mediaUrl(i){const [id,,kind,meta]=wallpapers[i];return meta?.playUrl||meta?.url||(kind==='image'?'/media/wallpapers/'+id+'.jpg':'./media/'+id+(kind==='gif'?'.gif':'.mp4'));}
function updateScene(){
  const video=$('movie'),image=$('wall'),motion=state.playing&&shown&&!document.hidden&&!(state.fullPause&&$('app').classList.contains('maximized')&&!$('app').hidden)&&!(state.batteryPause&&battery&&!battery.charging);
  const i=state.wall, kind=wallpapers[i]?.[2],url=kind?mediaUrl(i):'/media/wallpapers/mountain-bay.webp';
  image.style.objectFit=video.style.objectFit=state.fit;
  const poster=kind==='video'?thumb(i):kind==='gif'&&!motion?thumb(i):url;
  if(image.getAttribute('src')!==poster)image.src=poster;
  if(kind==='video'){
    if(video.getAttribute('src')!==url){video.src=url;video.poster=poster;}
    video.hidden=false;video.muted=!state.sound;if(motion)video.play().catch(()=>{});else video.pause();
  }else{video.pause();video.hidden=true;if(video.hasAttribute('src')){video.removeAttribute('src');video.load();}}
}
function renderWalls(){
  const tabs=barButtons([choice('library:local',t('本地壁纸','Local wallpapers'),state.library==='local'),choice('library:online',t('在线壁纸','Online wallpapers'),state.library==='online')]);
  if(state.library==='online')return tabs;
  const i=state.selected,[,name,kind]=wallpapers[i];
  const media=kind==='video'&&state.preview?'<video src="'+esc(mediaUrl(i))+'" autoplay loop muted playsinline controls></video>':'<img src="'+esc(kind!=='video'?mediaUrl(i):thumb(i))+'" alt="">';
  return tabs+'<div class="native-library"><div class="browse"><div class="spot">'+media+'<div><small>'+typeLabel[kind]+' / '+t('原文件','Original')+'</small><strong>'+esc(name)+'</strong><small>'+t('让每一次回到桌面，都有一点不同。','A fresh view every time you return.')+'</small></div></div>'+
  (kind==='video'?button('preview',state.preview?t('停止预览','Stop preview'):t('播放预览','Play preview')):'')+
  '<div class="gallery-head"><b>'+t('我的壁纸','My wallpapers')+'</b>'+barButtons(['all','image','gif','video'].map(k=>choice('filter:'+k,k==='all'?t('全部','All'):typeLabel[k],state.filter===k)))+'</div>'+
  '<input type="search" class="field" id="wall-search" aria-label="'+t('搜索本地壁纸','Search local wallpapers')+'" placeholder="'+t('搜索壁纸…','Search wallpapers…')+'" value="'+esc(state.query)+'"><div class="gallery">'+wallpapers.map((w,n)=>'<button class="thumb" data-act="wall:'+n+'" '+((state.filter==='all'||state.filter===w[2])&&w[1].toLowerCase().includes(state.query.toLowerCase())?'':'hidden')+' aria-pressed="'+(i===n)+'"><img src="'+esc(thumb(n))+'" alt=""><span>'+esc(w[1])+'</span></button>').join('')+'</div></div>'+
  '<aside class="inspector"><h2>'+t('壁纸设置','Wallpaper settings')+'</h2><b>'+esc(name)+'</b><small>'+typeLabel[kind]+(builtinInfo[wallpapers[i][0]]?' · '+builtinInfo[wallpapers[i][0]]:'')+'</small>'+
  select('fit',t('显示方式','Fit'),{cover:t('填充屏幕','Fill screen'),contain:t('完整显示','Fit inside'),fill:t('拉伸铺满','Stretch'),none:t('原始大小','Original size')})+
  row(t('显示器','Monitor'),'<select><option>'+t('主显示器','Primary display')+'</option></select>')+
  button('apply',t('应用到桌面','Apply to desktop'),true)+button('stop-wall',t('停止壁纸','Stop wallpaper'))+button('restore-wall',t('恢复系统壁纸','Restore system wallpaper'))+
  (kind!=='image'?button('pause',state.playing?t('暂停播放','Pause playback'):t('继续播放','Resume playback')):'')+(kind==='video'?button('sound',state.sound?t('关闭声音','Mute'):t('开启声音','Enable sound')):'')+'</aside></div>';
}
function settingsGroup(key,title,body,open=false){
  return '<details class="settings-group" data-group="'+key+'" '+(open?'open':'')+'><summary>'+title+'</summary><div>'+body+'</div></details>';
}
function renderAppearance(){
  return settingsGroup('grid',t('桌面收纳','Desktop organizer'),check('collected',t('启用桌面收纳','Enable organizer'))+
    select('place',t('布局位置','Position'),{top:t('居上','Top'),center:t('居中','Center'),bottom:t('居下','Bottom')})+
    select('sort',t('排列方式','Sort by'),{name:t('名称','Name'),time:t('时间','Date'),group:t('分组','Group')})+
    range('iconSize',t('图标大小 · 小 / 中 / 大','Icon size · S / M / L'),0,2,state.iconSize)+
    color('shelfColor',t('收纳背景颜色','Shelf color'))+range('shelf',t('背景不透明度','Background opacity'),0,100,state.shelf)+color('plateColor',t('图标底板颜色','Icon tile color'))+range('plate',t('图标底板不透明度','Tile opacity'),0,100,state.plate)+check('autoCollect',t('一分钟后自动收纳新增内容','Collect new items after one minute')))+
    settingsGroup('bar',t('原生任务栏','Native taskbar'),check('styled',t('启用任务栏美化','Style taskbar'))+color('barColor',t('任务栏颜色','Taskbar color'))+range('barAlpha',t('不透明度','Opacity'),0,100,state.barAlpha)+range('barRadius',t('圆角','Corner radius'),0,20,state.barRadius))+
    settingsGroup('pointer',t('鼠标与指针','Mouse and pointer'),'<div class="pointer-samples">'+['system','stream','prism','pixel'].map((k,n)=>'<button data-act="scheme:'+k+'" aria-pressed="'+(state.cursor===k)+'">'+(n?'<img src="./art/'+(k==='prism'?'outline':k)+'-preview.png" alt="">':'<svg viewBox="0 0 84 84" aria-hidden="true"><path d="M57 27a22 22 0 1 0 5 23m6-32v16H52" fill="none" stroke="#80a995" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/></svg>')+'<span>'+[t('系统方案','System scheme'),t('流线 · 圆润实心','Stream · Rounded solid'),t('棱镜 · 几何镂空','Outline · Geometric hollow'),t('像素 · 阶梯复古','Pixel · Retro steps')][n]+'</span></button>').join('')+'</div>'+color('color',t('指针颜色','Pointer color'))+range('size',t('指针大小','Pointer size'),75,200,state.size)+check('particles',t('鼠标粒子特效','Pointer particles'))+select('effect',t('粒子样式','Particle style'),{dot:t('点点','Dots'),fall:t('飘落','Falling'),glow:t('光芒','Glow')})+color('particleColor',t('粒子颜色','Particle color')))+
    settingsGroup('groups',t('分组管理','Groups'),'<div id="groups"></div>'+button('group-add',t('添加新分组','Add group')));
}
let studioOpen=false,disposeStudio;
function renderCapture(){
  if(studioOpen)return button('close-studio',t('← 截屏与录屏','← Capture and recording'))+'<section id="media-studio"></section>';
  const options=settingsGroup('capture-options',t('截屏与录屏设置','Capture and recording settings'),check('hotkey',t('启用截屏快捷键','Enable capture hotkey'))+row(t('截屏快捷键','Capture shortcut'),button('set-hotkey',state.chord))+
    select('resolution',t('导出画质','Export resolution'),{'0':t('原始分辨率','Original'),1080:'1080p',720:'720p',480:'480p'})+
    select('fps',t('导出帧率','Export frame rate'),{'0':t('原始帧率','Original'),12:'12 fps',15:'15 fps',24:'24 fps',30:'30 fps',60:'60 fps'}));
  return options+ '<div class="group"><h2>'+t('截取桌面区域','Capture a region')+'</h2><p>'+t('拖动框选后，可以涂鸦、画框并贴到桌面。','Drag a selection, annotate it, then pin it to the desktop.')+'</p>'+
    barButtons([button('capture',t('开始截屏','Take screenshot'),true),button('long',t('截取示例长图','Capture sample scroll'))])+'</div>'+
    '<div class="group"><h2>'+t('录制虚拟桌面','Record the virtual desktop')+'</h2><p>'+t('开始后可以打开应用、移动便签。最多录制 12 秒，结束后直接回放。','Open the app or move notes while recording. Play back up to 12 seconds when you stop.')+'</p>'+
    barButtons([button('record',t('开始录屏','Start recording'),true),button('studio',t('媒体工作台','Media studio'))])+'<small>'+t('此处录制虚拟画面。系统声音与麦克风可在安装版中选取。','The demo records its own desktop. System and microphone audio are available in the installed app.')+'</small></div>';
}
let activeNote=null,archiveView=false,focusMinutes=25,focusEnd=0,clipQuery='',clipPinned=new Set(),fileFilter='',filePick=null,renameHistory=[],batchIds=new Set(),batchPrefix='';
const toolNames={launch:t('快速启动','Launcher'),clips:t('剪贴板历史','Clipboard history'),notes:t('便签与任务','Notes and tasks'),files:t('文件工作台','File workbench'),focus:t('专注工作区','Focus workspace')};
const fmt=s=>Math.floor(s/60)+':'+String(s%60).padStart(2,'0');
function renderNotes(){
  const n=state.notes.find(n=>n.id===activeNote&&!n.archived===!archiveView);
  return barButtons([button('newnote',t('新建便签','New note'),true),choice('archives',t('已归档','Archived'),archiveView)])+color('noteColor',t('新便签颜色','New note color'))+
    '<div class="work-split"><div class="list">'+state.notes.filter(n=>!!n.archived===archiveView).map(n=>button('manage:'+n.id,(n.done?'✓ ':'')+(n.text.split('\n')[0]||t('未命名便签','Untitled note')))).join('')+'</div><div>'+
    (n?'<textarea data-note-body aria-label="'+t('便签内容','Note text')+'">'+esc(n.text)+'</textarea>'+row(t('提醒时间','Reminder'),'<input type="datetime-local" data-due value="'+esc(n.due||'')+'">')+
    barButtons([button('done:'+n.id,n.done?t('设为未完成','Mark incomplete'):t('完成','Complete')),button('archive:'+n.id,n.archived?t('恢复','Restore'):t('归档','Archive')),...(!n.archived?[button('edit:'+n.id,t('贴到桌面','Pin to desktop'))]:[])]):'')+'</div></div>';
}
function renderTools(){
  if(state.tool==='home')return '<div class="tool-grid">'+Object.entries(toolNames).map(([k,v])=>button('tool:'+k,v+'  ›')).join('')+'</div>';
  const tabs=barButtons([button('tool:home',t('← 工具','← Tools'))])+'<h2>'+toolNames[state.tool]+'</h2>';
  if(state.tool==='notes')return tabs+renderNotes();
  if(state.tool==='launch')return tabs+searchField('lookup',t('搜索应用、文件或计算表达式','Search apps, files or calculate'))+'<div class="list" id="matches"></div>';
  if(state.tool==='clips')return tabs+searchField('clip-search',t('搜索剪贴板','Search clipboard'))+'<div class="strip"><input class="field" id="clip-text" maxlength="500" aria-label="'+t('演示文字','Demo text')+'">'+button('clip-add',t('存入剪贴板','Save snippet'))+'</div><div class="list" id="clip-list"></div>';
  if(state.tool==='files')return tabs+searchField('file-filter',t('按名称筛选当前文件夹…','Filter this folder by name…'))+
    '<div class="work-split"><div id="file-list"></div><div id="file-info"></div></div><div id="rename-pane"></div>'+
    barButtons([button('newfile',t('新建','New')),button('batch',t('批量重命名…','Batch rename…')),button('undo-rename',t('撤销最近重命名','Undo rename'))]);
  return tabs+'<div class="group"><h2 id="timer-text">'+fmt(focusEnd?Math.max(0,Math.ceil((focusEnd-Date.now())/1000)):focusMinutes*60)+'</h2>'+
    row(t('专注时长（分钟）','Duration (minutes)'),'<input type="number" id="focus-mins" min="1" max="180" value="'+focusMinutes+'">')+
    barButtons([button('timer',focusEnd?t('结束专注','End focus'):t('开始专注','Start focus'),true)])+'</div>'+barButtons([button('save-work',t('保存当前桌面布局','Save workspace')),button('restore-work',t('恢复布局','Restore workspace'))]);
}
function drawClips(){
  if(!$('clip-list'))return;
  const list=state.clips.map((text,i)=>({text,i})).filter(c=>c.text.toLowerCase().includes(clipQuery.toLowerCase())).sort((a,b)=>Number(clipPinned.has(b.text))-Number(clipPinned.has(a.text)));
  $('clip-list').innerHTML=list.map(({text,i})=>'<div class="row">'+button('clip:'+i,text)+barButtons([button('clip-pin:'+i,clipPinned.has(text)?t('取消固定','Unpin'):t('固定','Pin')),button('clip-delete:'+i,t('删除','Delete'))])+'</div>').join('');
  if(copiedImage){const a=document.createElement('button');a.textContent=t('截图 · 查看已复制的图片','Screenshot · View copied image');a.onclick=()=>pinCopied();$('clip-list').prepend(a);}
}
function drawFiles(){
  if(!$('file-list'))return;
  $('file-list').innerHTML=state.files.filter(f=>f.name.toLowerCase().includes(fileFilter.toLowerCase())).map(f=>'<div class="row"><input type="checkbox" data-batch="'+f.id+'" aria-label="'+esc(f.name)+'" '+(batchIds.has(f.id)?'checked':'')+'>'+button('inspect:'+f.id,f.name)+'</div>').join('');
  const f=state.files.find(f=>f.id===filePick);$('file-info').innerHTML=f?'<b>'+esc(f.name)+'</b><p>'+esc(f.folder?t('文件夹','Folder'):(f.text||t('空白文档','Empty document')))+'</p>'+barButtons([button('file:'+f.id,t('打开','Open')),button('rename:'+f.id,t('重命名','Rename')),...(f.folder?[]:[button('hash:'+f.id,'SHA-256')])])+'<output id="digest"></output>':'';
}
function calculator(str){
  const tokens=str.replace(/\s/g,'').match(/(?:\d+(?:\.\d*)?|\.\d+)|[+\-*/()]/g);
  if(!tokens||tokens.join('')!==str.replace(/\s/g,'')||tokens.length>90||!/[+*/()-]/.test(str))return null;
  let i=0;function atom(){const a=tokens[i++];if(a==='-')return -atom();if(a==='+')return atom();if(a==='('){const n=sum();if(tokens[i++]!==')')throw Error();return n;}if(!a||!/^\d|^\./.test(a))throw Error();return Number(a);}
  function product(){let n=atom();while(['*','/'].includes(tokens[i])){const op=tokens[i++],v=atom();n=op==='*'?n*v:n/v;}return n;}
  function sum(){let n=product();while(['+','-'].includes(tokens[i])){const op=tokens[i++],v=product();n=op==='+'?n+v:n-v;}return n;}
  try{const n=sum();return i===tokens.length&&Number.isFinite(n)?Number(n.toPrecision(12)):null;}catch{return null;}
}
function matches(q=''){
  if(!$('matches'))return;
  const answer=calculator(q);if(answer!==null){$('matches').textContent=q+' = '+answer;return;}
  const items=[...Object.entries(sections).map(([k,v])=>[v,'show:'+k]),...state.files.map(f=>[f.name,'file:'+f.id])];
  $('matches').innerHTML=items.filter(([name])=>name.toLowerCase().includes(q.trim().toLowerCase())).map(([name,act])=>button(act,name)).join('')||'<p>'+t('没有匹配项','No matches')+'</p>';
}
function settings(){
  return settingsGroup('play',t('播放与系统','Playback and system'),check('fullPause',t('前台全屏时暂停动态壁纸','Pause wallpaper during fullscreen'))+check('batteryPause',t('使用电池时暂停动态壁纸','Pause when on battery'))+check('resume',t('启动时恢复上次的壁纸','Restore wallpaper on startup'))+check('night',t('虚拟显示器夜间模式','Virtual display night light'))+'<small>'+t('开机启动和系统级护眼配置可在安装版中设置。','Set Windows startup and system night light in the installed app.')+'</small>',true)+settingsGroup('tool-prefs',t('工具偏好','Tool preferences'),check('restoreNotes',t('启动时恢复已贴桌面的便签','Restore pinned notes on startup')));
}
function about(){
  return '<div class="about-card"><img src="/media/icon.png" alt=""><h2>'+product+' / v2.0.9</h2><h3>'+t('给桌面一点风景，给工作一点安静。','A little scenery for your desktop. A little calm for your work.')+'</h3><p>'+t('纯属个人娱乐，有BUG随便提issues，自己用得不顺就改改','A personal project. Found a bug? Open an issue, or adapt it to your liking.')+'</p><a href="https://github.com/GalokPeng/light-block-desktop" target="_blank" rel="noopener">GitHub ↗</a></div>';
}
const groups=[{name:t('常用','Favorites'),color:'#9cdcc2'}];
let shelfExpanded=false,shelfQuery='',selectedFile=null,shelfMove=false,shelfOrigin=null;
const openSections=new Set();
function render(){
  disposeStudio?.();disposeStudio=null;
  $('content').removeAttribute('aria-busy');
  const panels={wall:renderWalls,cursor:renderAppearance,capture:renderCapture,tools:renderTools,settings,about};
  const actions=ib('theme',t('切换明暗主题','Toggle theme'),state.theme==='dark'?'moon':'sun')+button('language',t('EN','中'));
  $('content').innerHTML='<header class="page-head"><div><small>'+t('给桌面留一点安静','A QUIETER DESKTOP')+'</small><h1>'+(state.tab==='capture'?t('截屏与录屏','Capture and recording'):sections[state.tab])+'</h1></div><div>'+actions+(state.tab==='wall'?button('import',t('＋ 导入壁纸','＋ Import'),true):state.tab==='tools'?button('newnote',t('＋ 新建便签','＋ New note'),true):'')+'</div></header>'+panels[state.tab]();
  $('content').querySelectorAll('details').forEach(d=>{if(openSections.has(d.dataset.group))d.open=true;d.addEventListener('toggle',()=>{if(d.isConnected){if(d.open)openSections.add(d.dataset.group);else openSections.delete(d.dataset.group);}});});
  if($('groups'))$('groups').innerHTML=groups.map((g,i)=>'<div class="row"><input maxlength="24" aria-label="'+t('分组名称','Group name')+'" data-group-name="'+i+'" value="'+esc(g.name)+'"><input type="color" data-group-color="'+i+'" aria-label="'+t('分组颜色','Group color')+'" value="'+g.color+'">'+button('group-up:'+i,'↑')+button('group-down:'+i,'↓')+(groups.length>1?button('group-remove:'+i,t('移除','Remove')):'')+'</div>').join('');
  if(state.tab==='wall'&&state.library==='online'){
    const index=id=>wallpapers.findIndex(w=>w[0]===id);
    const apply=i=>{state.selected=state.wall=i;state.playing=true;updateScene();toast(t('已应用到桌面','Applied to desktop'));render();};
    renderCatalog($('content'),{
      localFor:id=>wallpapers[index(id)]?.[3],
      onDownload(e,blob){if(index(e.id)>=0)return;const url=URL.createObjectURL(blob),supported=e.kind!=='video'||document.createElement('video').canPlayType(blob.type);
        wallpapers.push([e.id,t(e.title.zh,e.title.en),e.kind,{url,playUrl:!supported?e.preview:null,poster:e.kind==='image'?url:e.cover||e.thumbnail_url}]);},
      onApply:e=>apply(index(e.id)),
      onPreviewApply(e){
        const key=e.id+'-web-preview';let i=index(key);
        if(i<0)i=wallpapers.push([key,t(e.title.zh,e.title.en)+t(' · 预览',' · Preview'),e.preview.endsWith('.mp4')?'video':'image',{url:e.preview,poster:e.cover}])-1;
        apply(i);
      }
    });
  }else leaveCatalog();
  if($('media-studio'))disposeStudio=openStudio($('media-studio'));
  matches();drawClips();drawFiles();
  if($('clip-search'))$('clip-search').value=clipQuery;if($('file-filter'))$('file-filter').value=fileFilter;
}
function tile(f){
  if(!f)return '<button class="file-tile shortcut" data-shortcut title="'+product+'" aria-label="'+product+'"><i><img src="/media/icon.png" alt=""><em>↗</em></i><span>'+product+'</span></button>';
  return '<button class="file-tile" data-file="'+f.id+'" '+(state.collected&&!f.loose?'data-act="file:'+f.id+'"':'')+'><i>'+icon(f.folder?'folder':'file')+'</i><span>'+esc(f.name)+'</span></button>';
}
function files(){
  const shelf=$('files'),desk=$('desk'), rgba=(hex,alpha)=>hex+Math.round(alpha*2.55).toString(16).padStart(2,'0');
  shelf.classList.toggle('collected',state.collected);shelf.dataset.place=state.place;
  shelf.style.setProperty('--tile-size',[26,34,42][state.iconSize]+'px');
  shelf.style.setProperty('--plate-fill',rgba(state.plateColor,state.plate));shelf.style.setProperty('--shelf-fill',rgba(state.shelfColor,state.shelf));
  const list=state.files.filter(f=>!state.collected||!f.loose).sort((a,b)=>state.sort==='time'?b.id-a.id:a.name.localeCompare(b.name,document.documentElement.lang,{numeric:true}));
  const found=list.filter(f=>f.name.toLowerCase().includes(shelfQuery.toLowerCase()));
  const search=state.collected?ib('shelf-search',t('搜索','Search'),'search'):'';
  let tiles=tile()+found.slice(0,state.collected&&!shelfExpanded?9:undefined).map(tile).join('');
  if(state.sort==='group'&&state.collected)tiles=tile()+groups.map((g,i)=>'<div class="tile-group" style="border-color:'+g.color+'"><input class="group-title" data-group-name="'+i+'" aria-label="'+t('分组名称','Group name')+'" maxlength="24" value="'+esc(g.name)+'">'+found.filter(f=>(f.group||0)===i).map(tile).join('')+'</div>').join('');
  shelf.innerHTML=(state.collected&&shelfExpanded?'<input id="shelf-query" type="search" aria-label="'+t('搜索收纳','Search organizer')+'" value="'+esc(shelfQuery)+'">':'')+tiles+search;
  let loose=$('loose-files');if(!loose){loose=document.createElement('div');loose.id='loose-files';desk.append(loose);}
  loose.innerHTML=state.collected?state.files.filter(f=>f.loose).map(f=>'<div style="left:'+f.x+'px;top:'+f.y+'px">'+tile(f)+'<button class="collect-tip" data-act="collect:'+f.id+'"><span>'+(state.autoCollect?t('一分钟后自动收纳','Collect in one minute'):t('放入收纳','Collect'))+'</span><b>'+t('点击直接收纳','Collect now')+'</b></button></div>').join(''):'';
}
function taskbar(){
  const bar=$('bar');bar.classList.toggle('styled',state.styled);
  bar.style.setProperty('--bar-color',state.barColor+Math.round(state.barAlpha*2.55).toString(16).padStart(2,'0'));bar.style.setProperty('--bar-radius',state.barRadius+'px');
  bar.innerHTML='<div class="bar-part">'+ib('power',t('电源','Power'),'power')+ib('show:settings',sections.settings,'settings')+ib('tasks',t('任务管理器','Task Manager'),'tasks')+ib('launch',t('搜索','Search'),'search')+'</div>'+
    '<div class="bar-part"><button class="ib" data-act="app-toggle" aria-label="'+product+'"><img src="/media/icon.png" alt=""></button>'+ib('show:wall',sections.wall,'wall')+ib('show:capture',sections.capture,'capture')+ib('show:note',t('便签','Notes'),'note')+'</div>'+
    '<div class="bar-part">'+icon('signal')+'<time class="clock"></time></div>';
  tick();
}
function tick(){
  const clock=document.querySelector('.clock');if(clock)clock.textContent=new Date().toLocaleTimeString(document.documentElement.lang,{hour:'2-digit',minute:'2-digit',hour12:false});
  for(const n of state.notes){if(n.due&&!n.archived&&!n.done&&Date.parse(n.due)<=Date.now()){n.due='';toast(t('便签提醒 · ','Reminder · ')+n.text.slice(0,40));}}
  if(focusEnd){const left=Math.max(0,Math.ceil((focusEnd-Date.now())/1000));if($('timer-text'))$('timer-text').textContent=fmt(left);if(!left){focusEnd=0;toast(t('专注时间结束','Focus session complete'));if(state.tab==='tools')render();}}
}
function note(text='',initial=false){
  const id=++noteSeq,n={id,text,done:false};state.notes.push(n);if(!initial){activeNote=id;archiveView=false;}
  const box=document.createElement('section');box.className='note';box.id='sticky-'+id;box.style.backgroundColor=state.noteColor;
  box.style.left=(initial?81:Math.min(58,20+(id%4)*8))+'%';box.style.top=(initial?19:16+(id%4)*7)+'%';
  box.innerHTML='<div class="note-head"><span>'+icon('pin')+' '+t('便签','Note')+'</span><div><input class="note-color" type="color" value="'+state.noteColor+'" title="'+t('便签颜色','Note color')+'" aria-label="'+t('便签颜色','Note color')+'">'+ib('done:'+id,t('标记完成','Mark done'),'check')+ib('remove:'+id,t('删除便签','Delete note'),'close')+'</div></div><textarea maxlength="1200" spellcheck="false" aria-label="'+t('便签正文','Note text')+'"></textarea>';
  const picker=box.querySelector('.note-color');picker.oninput=()=>{box.style.backgroundColor=picker.value;};
  const area=box.querySelector('textarea');area.value=text;area.oninput=()=>{n.text=area.value;if(state.tab==='tools'&&state.tool==='notes')render();};
  $('stickies').append(box);drag(box,box.firstElementChild,$('desk'));box.addEventListener('pointerdown',()=>box.style.zIndex=++appZ);
  if(!initial){box.style.zIndex=++appZ;area.focus();}return box;
}
const cursor=desktopPointer($('desk'),state);
function deskPolicy(){
  const fullscreen=$('app').classList.contains('maximized')&&!$('app').hidden;
  $('bar').classList.toggle('edge-hide',state.styled&&fullscreen);updateScene();
}
$('desk').addEventListener('pointermove',e=>{if(e.clientY>=innerHeight-3)$('bar').classList.add('revealed');else if(e.clientY<innerHeight-48)$('bar').classList.remove('revealed');});
new MutationObserver(()=>{deskPolicy();if($('app').hidden){state.preview=false;leaveCatalog();$('content').querySelectorAll('video').forEach(v=>v.pause());}}).observe($('app'),{attributes:true,attributeFilter:['hidden']});
document.querySelector('.titlebar').addEventListener('dblclick',e=>{if(!e.target.closest('button'))handle('maximize');});
function openFile(id){
  const f=state.files.find(f=>f.id===Number(id));if(!f)return;
  $('file-viewer')?.remove();const box=document.createElement('section');box.id='file-viewer';box.className='document-window';box.style.zIndex=++appZ;
  const head=document.createElement('header');head.className='result-head';head.append(document.createTextNode(f.name));const x=document.createElement('button');x.textContent='×';x.ariaLabel=t('关闭','Close');x.onclick=()=>box.remove();head.append(x);box.append(head);
  const page=document.createElement('article');page.tabIndex=0;
  if(f.folder){for(let i=0;i<4;i++){const h=document.createElement('h3'),img=new Image();h.textContent=wallpapers[i]?.[1]||f.name;img.src=wallpapers[i]?thumb(i):'';page.append(h,img);}}
  else{const input=document.createElement('textarea');input.value=f.text||'';input.placeholder=t('开始输入…','Start typing…');input.oninput=()=>f.text=input.value;page.append(input);}
  box.append(page);$('workspace').append(box);drag(box,head,$('desk'));box.onpointerdown=()=>box.style.zIndex=++appZ;
}
function rename(id){
  const f=state.files.find(f=>f.id===Number(id));if(!f)return;
  const pane=$('rename-pane');pane.innerHTML='<form class="strip"><input maxlength="60" class="field" required aria-label="'+t('文件名','File name')+'"><button class="action">'+t('保存名称','Save name')+'</button></form>';
  const field=pane.querySelector('input');field.value=f.name;field.select();
  pane.firstChild.onsubmit=e=>{e.preventDefault();if(field.value.trim()){f.name=field.value.trim();files();render();}};
}
function minimize(){state.preview=false;leaveCatalog();$('content').querySelectorAll('video').forEach(v=>v.pause());$('app').hidden=true;deskPolicy();}
function handle(action){
  const [act,arg]=action.split(':');
  switch(act){
    case 'show':if(arg==='capture')studioOpen=false;if(arg==='tools')state.tool='home';return show(arg);
    case 'hide':minimize();return;
    case 'app-toggle':if($('app').hidden)show(state.tab);else minimize();return;
    case 'wall':state.selected=Number(arg);state.preview=false;break;
    case 'library':state.library=arg;state.preview=false;break;
    case 'preview':state.preview=!state.preview;break;
    case 'apply':state.wall=state.selected;state.playing=true;updateScene();toast(t('已应用到桌面','Applied to desktop'));break;
    case 'stop-wall':state.playing=false;updateScene();break;
    case 'restore-wall':state.wall=-1;state.playing=false;updateScene();break;
    case 'sound':state.sound=!state.sound;updateScene();break;
    case 'theme':state.theme=state.theme==='dark'?'light':'dark';$('app').dataset.theme=state.theme;break;
    case 'language':changeLanguage();return;
    case 'version':return show('about');
    case 'import':return importFiles();
    case 'scheme':state.cursor=arg;cursor();break;
    case 'shelf-move':shelfMove=!shelfMove;$('files').classList.toggle('moving',shelfMove);return;
    case 'group-up':case 'group-down':case 'group-remove':{const from=Number(arg),to=from+(act==='group-up'?-1:1);if(act!=='group-remove'&&!groups[to])return;
      const old=[...groups];if(act==='group-remove'){groups.splice(from,1);}else [groups[from],groups[to]]=[groups[to],groups[from]];
      for(const f of state.files)f.group=Math.max(0,groups.indexOf(old[f.group||0]));files();break;}
    case 'group-add':groups.push({name:t('新分组','New group'),color:'#88b8e8'});files();break;
    case 'shelf-search':shelfExpanded=!shelfExpanded;shelfQuery='';files();$('shelf-query')?.focus();return;
    case 'collect':{const f=state.files.find(f=>f.id===Number(arg));if(f){f.loose=false;files();}return;}
    case 'shortcut':return show(state.tab);
    case 'maximize':$('app').classList.toggle('maximized');deskPolicy();return;
    case 'power':return virtualDialog(t('电源','Power'),t('锁定、关机与重启由 Windows 提供。','Windows provides lock, shut down and restart.'));
    case 'tasks':return virtualDialog(t('任务管理器','Task Manager'),product+' · '+t('正在运行','Running'));
    case 'properties':{const f=state.files.find(f=>f.id===Number(arg));return virtualDialog(t('属性','Properties'),(f?.name||product)+' · '+(f?.folder?t('文件夹','Folder'):f?t('文本文档','Text document'):t('应用程序快捷方式','Application shortcut')));}
    case 'delete-file':state.files=state.files.filter(f=>f.id!==Number(arg));files();break;
    case 'to-group':{const f=state.files.find(f=>f.id===selectedFile);if(f){f.group=Number(arg);f.loose=false;files();}return;}
    case 'remove-wall':return removeWall(Number(arg));
    case 'filter':state.filter=arg;break;
    case 'pause':state.playing=!state.playing;updateScene();break;
    case 'appearance':state.appearance=arg;break;
    case 'tool':state.tool=arg;break;
    case 'launch':state.tool='launch';show('tools');$('lookup').focus();return;
    case 'file':return openFile(arg);
    case 'rename':return rename(arg);
    case 'rename-file':state.tool='files';show('tools');return rename(arg);
    case 'newfile':if(state.files.length>=9){toast(t('演示最多容纳九个文件','The demo holds nine files'));return;}{const f=nameFile(t('文档 ','File ')+(fileSeq+1)+'.txt');Object.assign(f,{loose:state.collected,x:spawnPoint.x,y:spawnPoint.y});state.files.push(f);if(f.loose&&state.autoCollect)scheduleCollect(f);files();}break;
    case 'newnote':if(state.notes.length>=8){toast(t('演示最多八张便签','The demo holds eight notes'));return;}note();break;
    case 'edit':{const box=$('sticky-'+arg);box.style.zIndex=++appZ;box.querySelector('textarea').focus();return;}
    case 'remove':state.notes=state.notes.filter(n=>n.id!==Number(arg));$('sticky-'+arg)?.remove();break;
    case 'done':{const n=state.notes.find(n=>n.id===Number(arg));n.done=!n.done;$('sticky-'+arg).querySelector('textarea').style.textDecoration=n.done?'line-through':'none';break;}
    case 'capture':return startCapture();
    case 'record':return startRecording();
    case 'set-hotkey':{const b=$('content').querySelector('[data-act=set-hotkey]');b.dataset.listening='';b.textContent=t('请按组合键…','Press a shortcut…');b.focus();return;}
    case 'studio':studioOpen=true;break;
    case 'close-studio':studioOpen=false;break;
    case 'long':return startCapture({scrollSelection:true});
    case 'manage-note':activeNote=Number(arg);state.tool='notes';return show('tools');
    case 'manage':activeNote=Number(arg);break;
    case 'archives':archiveView=!archiveView;break;
    case 'archive':{const n=state.notes.find(n=>n.id===Number(arg));n.archived=!n.archived;$('sticky-'+n.id).hidden=n.archived;break;}
    case 'inspect':filePick=Number(arg);drawFiles();return;
    case 'hash':{const f=state.files.find(f=>f.id===Number(arg)),output=$('digest');crypto.subtle.digest('SHA-256',new TextEncoder().encode(f.text||'')).then(buf=>{if(output.isConnected)output.textContent=Array.from(new Uint8Array(buf),v=>v.toString(16).padStart(2,'0')).join('');});return;}
    case 'batch':{const pane=$('rename-pane');pane.innerHTML='<div class="strip"><input id="prefix" maxlength="40" aria-label="'+t('名称前缀','Filename prefix')+'" placeholder="'+t('名称前缀','Filename prefix')+'">'+button('batch-preview',t('预览改名','Preview rename'))+'</div><div id="batch-preview"></div>';return;}
    case 'batch-preview':{batchPrefix=$('prefix').value.trim();const selected=state.files.filter(f=>batchIds.has(f.id));if(!selected.length||!batchPrefix)return toast(t('先选择文件并输入前缀','Select files and enter a prefix'));$('batch-preview').innerHTML=selected.map((f,i)=>'<p>'+esc(f.name)+' → '+esc(batchPrefix)+'-'+(i+1)+(f.folder?'':'.txt')+'</p>').join('')+button('batch-apply',t('应用重命名','Apply rename'));return;}
    case 'batch-apply':{renameHistory=state.files.filter(f=>batchIds.has(f.id)).map(f=>[f.id,f.name]);renameHistory.forEach(([id],i)=>{const f=state.files.find(f=>f.id===id);f.name=batchPrefix+'-'+(i+1)+(f.folder?'':'.txt');});files();break;}
    case 'undo-rename':for(const [id,name] of renameHistory){const f=state.files.find(f=>f.id===id);if(f)f.name=name;}renameHistory=[];files();break;
    case 'clip-pin':{const text=state.clips[Number(arg)];if(clipPinned.has(text))clipPinned.delete(text);else clipPinned.add(text);break;}
    case 'clip-delete':clipPinned.delete(state.clips[Number(arg)]);state.clips.splice(Number(arg),1);break;
    case 'clip-add':{const s=$('clip-text').value.trim();if(s)state.clips=[s,...state.clips.filter(c=>c!==s)].slice(0,8);break;}
    case 'clip':if(state.notes.length<8)note(state.clips[Number(arg)]);break;
    case 'timer':focusEnd=focusEnd?0:Date.now()+focusMinutes*60000;break;
    case 'save-work':state.workspace={wall:state.wall,collected:state.collected,place:state.place};toast(t('已保存演示布局','Workspace saved'));break;
    case 'restore-work':if(state.workspace){Object.assign(state,state.workspace);updateScene();files();toast(t('已恢复布局','Workspace restored'));}else toast(t('请先保存一个布局','Save a workspace first'));break;
    default:return;
  }
  render();
}
let menu,spawnPoint={x:15,y:15};
function closeMenu(){menu?.remove();menu=null;}
function context(items,e){
  closeMenu();menu=document.createElement('div');menu.className='desk-menu';menu.setAttribute('role','menu');
  menu.innerHTML=items.map(([action,label])=>button(action,label)).join('');$('desk').append(menu);
  menu.style.left=Math.max(0,Math.min(e.clientX,innerWidth-menu.offsetWidth))+'px';menu.style.top=Math.max(0,Math.min(e.clientY,innerHeight-menu.offsetHeight))+'px';
  menu.querySelector('button').focus();menu.onclick=e=>{const act=e.target.closest('[data-act]')?.dataset.act;e.stopPropagation();closeMenu();if(act)handle(act);};
}
function virtualDialog(name,info){
  $('virtual-dialog')?.remove();const el=document.createElement('dialog');el.id='virtual-dialog';const head=document.createElement('h3'),p=document.createElement('p'),b=document.createElement('button');head.textContent=name;p.textContent=info;b.textContent=t('确定','OK');b.onclick=()=>el.close();el.onclose=()=>el.remove();el.append(head,p,b);$('desk').append(el);el.showModal();
}
function scheduleCollect(f){clearTimeout(f.timer);f.timer=setTimeout(()=>{if(state.autoCollect&&f.loose){f.loose=false;files();}},60000);}
function importFiles(){
  const picker=document.createElement('input');picker.type='file';picker.multiple=true;picker.accept='image/*,video/*';
  picker.onchange=async()=>{for(const file of picker.files){const kind=file.type.startsWith('video/')?'video':file.type==='image/gif'?'gif':file.type.startsWith('image/')?'image':null;if(!kind)continue;
    const url=URL.createObjectURL(file);let poster=url;
    if(kind==='video'){poster='./media/motion.webp';const video=document.createElement('video');video.muted=true;video.preload='auto';video.src=url;video.onloadeddata=()=>{const c=document.createElement('canvas');c.width=240;c.height=Math.max(1,240*video.videoHeight/video.videoWidth);c.getContext('2d').drawImage(video,0,0,c.width,c.height);const item=wallpapers.find(w=>w[3]?.url===url);if(item){item[3].poster=c.toDataURL();if(state.tab==='wall')render();}video.removeAttribute('src');video.load();};}
    state.selected=wallpapers.push([crypto.randomUUID(),file.name,kind,{url,poster,user:true}])-1;}
    state.library='local';state.query='';state.filter='all';show('wall');};picker.click();
}
function removeWall(i){
  if(wallpapers.length<=1)return;
  const removed=wallpapers.splice(i,1)[0];if(!removed)return;
  for(const key of ['wall','selected']){if(state[key]===i)state[key]=key==='wall'?-1:0;else if(state[key]>i)state[key]--;}
  updateScene();render();if(removed[3])URL.revokeObjectURL(removed[3].url);
}
function changeLanguage(){
  // Browser object URLs belong to their document. Carry the originals across a UI-language reload.
  Promise.all(wallpapers.map(async ([id,name,kind,meta])=>({id,name,kind,meta:meta&&{blob:await (await fetch(meta.url)).blob(),playUrl:meta.playUrl,poster:meta.poster.startsWith('blob:')?'':meta.poster}}))).then(library=>{
    const notes=state.notes.map(n=>{const box=$('sticky-'+n.id);return {...n,left:box.style.left,top:box.style.top,width:box.offsetWidth,height:box.offsetHeight,color:box.style.backgroundColor};});
    const u=new URL(location.href);u.searchParams.set('lang',t('en','zh'));
    history.replaceState({switching:{state,library,notes,groups,open:[...openSections],focusEnd}},'',u);location.reload();
  }).catch(()=>toast(t('未能切换语言，请重试','Could not change language. Try again.')));
}
$('desk').addEventListener('contextmenu',e=>{
  const tile=e.target.closest('.file-tile'),wall=e.target.closest('.thumb[data-act^="wall:"]');
  if(wall){e.preventDefault();return context([['remove-wall:'+wall.dataset.act.split(':')[1],t('从壁纸库移除','Remove from library')]],e);}
  const sticky=e.target.closest('.note');if(sticky&&!e.target.closest('textarea,input')){e.preventDefault();const id=sticky.id.split('-')[1];return context([['manage-note:'+id,t('管理便签与提醒','Manage notes and reminders')],['done:'+id,t('切换完成状态','Toggle completed')],['archive:'+id,t('归档','Archive')]],e);}
  if(e.target.closest('#app,.note,.result,#capture-layer,#rec-state,.long-session,.document-window'))return;
  e.preventDefault();
  spawnPoint={x:Math.max(0,Math.min(e.clientX,innerWidth-100)),y:Math.max(0,Math.min(e.clientY,innerHeight-95))};
  if(tile?.hasAttribute('data-shortcut'))return context([['shortcut',t('打开','Open')],['properties:0',t('属性','Properties')]],e);
  if(tile){selectedFile=Number(tile.dataset.file||tile.dataset.act.split(':')[1]);return context([['file:'+selectedFile,t('打开','Open')],['rename-file:'+selectedFile,t('重命名','Rename')],['delete-file:'+selectedFile,t('删除','Delete')],['properties:'+selectedFile,t('属性','Properties')],...(state.collected?groups.map((g,i)=>['to-group:'+i,t('放入 ','Move to ')+g.name]):[])],e);}
  if(e.target.closest('#files.collected'))return context([['shelf-move',shelfMove?t('固定位置','Lock position'):t('移动位置','Move organizer')],['group-add',t('添加新分组','Add group')]],e);
  if(e.target.closest('#bar'))return context([['tasks',t('任务管理器','Task Manager')],['show:cursor',t('任务栏设置','Taskbar settings')]],e);
  context([['newnote',t('新建桌面便签','New desktop note')],['sound',t('轻块桌面声音','Wallpaper sound')+(state.sound?' ✓':'')],['newfile',t('新建文本文档','New text document')],['show:cursor',sections.cursor]],e);
});
const shortcut=e=>e.target.closest('[data-shortcut]');
$('desk').addEventListener('dblclick',e=>{if(shortcut(e))show(state.tab);else if(e.target.closest('.file-tile[data-file]:not([data-act])'))openFile(e.target.closest('[data-file]').dataset.file);});
$('desk').addEventListener('keydown',e=>{if(e.key==='Enter'&&e.target.closest('.file-tile[data-file]:not([data-act])')){openFile(e.target.closest('[data-file]').dataset.file);return;}if(shortcut(e)&&e.key==='Enter'){e.preventDefault();show(state.tab);}});
$('desk').addEventListener('click',e=>{if(shortcut(e)&&(state.collected||matchMedia('(pointer:coarse)').matches))show(state.tab);else if(matchMedia('(pointer:coarse)').matches&&e.target.closest('.file-tile[data-file]:not([data-act])'))openFile(e.target.closest('[data-file]').dataset.file);});
document.addEventListener('pointerdown',e=>{if(menu&&!menu.contains(e.target))closeMenu();
  if(shelfExpanded&&!e.target.closest('#files,.desk-menu')){shelfExpanded=false;shelfQuery='';files();}});
$('files').addEventListener('pointerdown',e=>{if(!shelfMove||e.button!==0)return;const el=$('files'),r=el.getBoundingClientRect();shelfOrigin={dx:e.clientX-r.x,dy:e.clientY-r.y};el.setPointerCapture(e.pointerId);});
$('files').addEventListener('pointermove',e=>{if(!shelfOrigin)return;const el=$('files');Object.assign(el.style,{left:Math.max(0,Math.min(innerWidth-el.offsetWidth,e.clientX-shelfOrigin.dx))+'px',top:Math.max(0,Math.min(innerHeight-el.offsetHeight-45,e.clientY-shelfOrigin.dy))+'px',bottom:'auto',transform:'none'});});
$('files').addEventListener('lostpointercapture',()=>shelfOrigin=null);
$('desk').addEventListener('click',e=>{if(e.target.closest('#capture-layer,#results,#rec-state'))return;const b=e.target.closest('[data-act]');if(b)handle(b.dataset.act);});
$('desk').addEventListener('input',e=>{
  if(e.target.id==='clip-search'){clipQuery=e.target.value;return drawClips();}
  if(e.target.id==='file-filter'){fileFilter=e.target.value;return drawFiles();}
  if(e.target.dataset.batch!==undefined){const id=Number(e.target.dataset.batch);if(e.target.checked)batchIds.add(id);else batchIds.delete(id);$('batch-preview')?.replaceChildren();return;}
  if(e.target.id==='focus-mins'){focusMinutes=Math.min(180,Math.max(1,Number(e.target.value)||1));if(!focusEnd)$('timer-text').textContent=fmt(focusMinutes*60);return;}
  if(e.target.hasAttribute('data-note-body')){const n=state.notes.find(n=>n.id===activeNote);n.text=e.target.value;$('sticky-'+n.id).querySelector('textarea').value=n.text;return;}
  if(e.target.hasAttribute('data-due')){state.notes.find(n=>n.id===activeNote).due=e.target.value;return;}
  if(e.target.id==='lookup')return matches(e.target.value);
  if(e.target.id==='shelf-query'){shelfQuery=e.target.value;files();$('shelf-query').focus();return;}
  if(e.target.id==='wall-search'){state.query=e.target.value;for(const b of $('content').querySelectorAll('.gallery button')){const w=wallpapers[Number(b.dataset.act.split(':')[1])];b.hidden=!(state.filter==='all'||state.filter===w[2])||!w[1].toLowerCase().includes(state.query.toLowerCase());}return;}
  if(e.target.dataset.groupName!==undefined){groups[e.target.dataset.groupName].name=e.target.value;if(!e.target.closest('#files'))files();return;}
  if(e.target.dataset.groupColor!==undefined){groups[e.target.dataset.groupColor].color=e.target.value;files();return;}
  const key=e.target.dataset.key;if(!key||!Object.hasOwn(state,key))return;
  const input=e.target;state[key]=input.type==='checkbox'?input.checked:input.type==='range'?Number(input.value):input.value;
  if(['barColor','barAlpha','barRadius','styled'].includes(key))taskbar();
  if(['size','color','effect','cursor','particles','particleColor'].includes(key))cursor();
  if(['shelf','plate','place','collected','shelfColor','plateColor','iconSize','sort'].includes(key))files();
  if(['fps','resolution'].includes(key))Object.assign(exportPreferences,{fps:Number(state.fps),resolution:Number(state.resolution)});
  if(['fit','fullPause','batteryPause'].includes(key))updateScene();
  if(key==='styled')deskPolicy();
  if(key==='night')$('desk').classList.toggle('night',state.night);
  if(key==='place'||key==='collected'){for(const prop of ['left','top','bottom','transform'])$('files').style.removeProperty(prop);shelfMove=false;$('files').classList.remove('moving');}
  if(key==='autoCollect')state.files.filter(f=>f.loose).forEach(f=>state.autoCollect?scheduleCollect(f):clearTimeout(f.timer));
  if(key==='cursor')$('content').querySelectorAll('[data-act^="scheme:"]').forEach(b=>b.setAttribute('aria-pressed',b.dataset.act==='scheme:'+state.cursor));
});
$('app').addEventListener('pointerdown',()=>$('app').style.zIndex=++appZ);
drag($('app'),document.querySelector('.titlebar'),$('desk'));
document.addEventListener('keydown',e=>{const k=e.key.toUpperCase(),chord=[e.ctrlKey?'Ctrl':'',e.altKey?'Alt':'',e.shiftKey?'Shift':'',k].filter(Boolean).join('+');
  const listening=e.target.closest('[data-listening]');if(listening){e.preventDefault();if(e.key==='Escape'){render();return;}if((e.ctrlKey||e.altKey)&&/^(?:[A-Z0-9]|F[1-9])$/.test(k)){state.chord=chord;render();}return;}
  if(chord===state.chord&&state.hotkey&&$('capture-layer').hidden){e.preventDefault();return startCapture();}
  if(e.key==='Escape'){if(menu)return closeMenu();if(!$('capture-layer').hidden)cancelCapture();else if(!$('rec-state').hidden)stopRecording();else $('app').hidden=true;}});
function keepInBounds(){const desk=$('desk');for(const box of desk.querySelectorAll('.window,.note,.result')){
  if(box.hidden)continue;
  if(box.offsetLeft+box.offsetWidth>desk.clientWidth)box.style.left=Math.max(0,desk.clientWidth-box.offsetWidth-2)+'px';
  if(box.offsetTop+30>desk.clientHeight-35)box.style.top=Math.max(0,desk.clientHeight-70)+'px';
}}
const observer=new IntersectionObserver(([entry])=>{shown=entry.isIntersecting;updateScene();if(!shown)stopRecording();});
observer.observe($('desk'));document.addEventListener('visibilitychange',updateScene);
window.addEventListener('resize',keepInBounds);
const storedKey='lightblock.demo.session.v2';
const preferences=['collected','place','autoCollect','sort','iconSize','plate','plateColor','shelf','shelfColor','styled','barColor','barAlpha','barRadius','cursor','size','color','particles','particleColor','effect','theme','fit','hotkey','chord','resolution','fps','resume','restoreNotes','night','fullPause','batteryPause'];
let remembered;
try{remembered=JSON.parse(sessionStorage.getItem(storedKey)||'null');}catch{}
if(remembered?.prefs){for(const key of preferences){const v=remembered.prefs[key];if(typeof v===typeof state[key]&&(typeof v!=='string'||/^[\w%+#. -]{1,40}$/.test(v)))state[key]=v;}
  const i=wallpapers.findIndex(w=>w[0]===remembered.wall);if(state.resume&&i>=0)state.wall=i;
}
function rememberSession(){
  try{sessionStorage.setItem(storedKey,JSON.stringify({prefs:Object.fromEntries(preferences.map(k=>[k,state[k]])),wall:wallpapers[state.wall]?.[0],
    notes:state.notes.map(n=>{const el=$('sticky-'+n.id);return {...n,left:el.style.left,top:el.style.top,width:el.offsetWidth||parseFloat(el.style.width),height:el.offsetHeight||parseFloat(el.style.height),color:el.style.backgroundColor};})}));}catch{}
}
window.addEventListener('pagehide',rememberSession);
function restoreNote(n){const b=note(n.text,true);Object.assign(b.style,{left:n.left,top:n.top,width:n.width?n.width+'px':'',height:n.height?n.height+'px':'',backgroundColor:n.color});
  Object.assign(state.notes.at(-1),{done:n.done,archived:n.archived,due:n.due});b.hidden=!!n.archived;if(n.done)b.querySelector('textarea').style.textDecoration='line-through';}
const saved=history.state?.switching;
if(saved){
  history.replaceState(null,'',location.href);
  const builtIn=new Map(wallpapers.map(w=>[w[0],w]));wallpapers.length=0;
  for(const entry of saved.library){const w=builtIn.get(entry.id);if(w)wallpapers.push(w);else if(entry.meta){const url=URL.createObjectURL(entry.meta.blob);wallpapers.push([entry.id,entry.name,entry.kind,{url,playUrl:entry.meta.playUrl,poster:entry.meta.poster||url}]);}}
  Object.assign(state,saved.state,{notes:[],preview:false});groups.splice(0,groups.length,...saved.groups);saved.open.forEach(k=>openSections.add(k));focusEnd=saved.focusEnd;
  fileSeq=Math.max(0,...state.files.map(f=>f.id));state.files.filter(f=>f.loose&&state.autoCollect).forEach(scheduleCollect);
  saved.notes.forEach(restoreNote);
}else if(remembered&&state.restoreNotes){remembered.notes?.forEach(restoreNote);}else if(!remembered)note(t('周末计划\n去海边，拍一张新壁纸。','Weekend plan\nVisit the coast. Find a new wallpaper.'),true);
Object.assign(exportPreferences,{fps:Number(state.fps),resolution:Number(state.resolution)});
$('app').dataset.theme=state.theme;$('desk').classList.toggle('night',state.night);cursor();renderNav();render();files();taskbar();updateScene();keepInBounds();
setInterval(()=>{if(shown&&!document.hidden)tick();},1000);
