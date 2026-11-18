import { $, t, ib, button, drag, toast } from './ui.mjs';
import {recordingControls} from './studio.mjs';
import { editFrame } from './editor.mjs';

let resultZ=1,currentResult;
let renderer, busy=false, captureCleanup, recording=false, recordingSession, playingUrl;
const desk=$('desk'), layer=$('capture-layer');
function loadRenderer(){
  if(!renderer)renderer=new Promise((resolve,reject)=>{
    const script=document.createElement('script');
    script.src='./vendor/html2canvas.min.js';
    script.onload=()=>resolve(window.html2canvas);
    script.onerror=()=>{renderer=null;script.remove();reject(new Error('capture renderer'));};
    document.head.append(script);
  });
  return renderer;
}
async function snapshot(target=desk,options={}){
  const render=await loadRenderer();
  return render(target,{scale:1,useCORS:true,backgroundColor:'#1b2d32',logging:false,imageTimeout:4000,
    onclone(copy){const source=$('movie').hidden?$('wall'):$('movie'),w=source.videoWidth||source.naturalWidth,h=source.videoHeight||source.naturalHeight;
      if(!w||!h)return;const scene=copy.getElementById('scene'),frame=copy.createElement('canvas');frame.width=desk.clientWidth;frame.height=desk.clientHeight;
      const g=frame.getContext('2d'),fit=getComputedStyle(source).objectFit,sx=frame.width/w,sy=frame.height/h,scale=fit==='cover'?Math.max(sx,sy):fit==='contain'?Math.min(sx,sy):1;
      const W=fit==='fill'?frame.width:w*scale,H=fit==='fill'?frame.height:h*scale;g.drawImage(source,(frame.width-W)/2,(frame.height-H)/2,W,H);scene.replaceChildren(frame);
    },...options});
}
function failed(){toast(t('未能采集演示画面，请重试','Could not capture the demo. Try again.'));}
const pos=(e,canvas)=>{const r=canvas.getBoundingClientRect();return {x:Math.max(0,Math.min(canvas.width,(e.clientX-r.left)*canvas.width/r.width)),y:Math.max(0,Math.min(canvas.height,(e.clientY-r.top)*canvas.height/r.height))};};
function result(title,independent=false){
  if(!independent)clearResult();
  const box=document.createElement('div');box.className='result';
  box.innerHTML='<div class="result-head"><b></b>'+ib('result-close',t('关闭预览','Close preview'),'close')+'</div>';
  box.querySelector('b').textContent=title;
  box.style.zIndex=++resultZ;box.addEventListener('pointerdown',()=>box.style.zIndex=++resultZ);
  if(!independent)currentResult=box;
  box.querySelector('button').onclick=()=>clearResult(box);
  $('results').append(box);drag(box,box.firstElementChild,desk);return box;
}
function clearResult(box=currentResult){
  if(!box)return;box.dispatchEvent(new Event('dispose'));
  const movie=box.querySelector('video');if(movie){movie.pause();movie.removeAttribute('src');movie.load();}
  box.remove();if(box!==currentResult)return;currentResult=null;
  if(playingUrl){URL.revokeObjectURL(playingUrl);playingUrl=null;}
}
function download(box,url,name){
  const row=document.createElement('div');row.className='result-controls';
  const a=document.createElement('a');a.href=url;a.download=name;a.textContent=t('保存到本地 ↓','Save a copy ↓');row.append(a);box.append(row);return row;
}
// Use decoded GIF frames and their actual delays for the browser's video timeline.
async function gifTimeline(file,signal,status){
  if(!window.ImageDecoder||!window.MediaRecorder)throw Error('codec');
  const decoder=new ImageDecoder({type:'image/gif',data:await file.arrayBuffer()});
  let stream,rec;try{
    await decoder.tracks.ready;
    const count=decoder.tracks.selectedTrack.frameCount,first=(await decoder.decode({frameIndex:0})).image;
    const c=document.createElement('canvas'),scale=Math.min(1,720/first.displayWidth);c.width=Math.max(2,Math.floor(first.displayWidth*scale/2)*2);c.height=Math.max(2,Math.floor(first.displayHeight*scale/2)*2);
    const g=c.getContext('2d');g.drawImage(first,0,0,c.width,c.height);first.close();stream=c.captureStream(30);rec=new MediaRecorder(stream);const chunks=[];rec.ondataavailable=e=>chunks.push(e.data);const done=new Promise((resolve,reject)=>{rec.onstop=resolve;rec.onerror=reject;});rec.start();
    let seconds=0;for(let i=0;i<count&&seconds<12;i++){
      if(signal.aborted)throw Error('cancelled');
      const img=(await decoder.decode({frameIndex:i})).image,delay=Math.min(12-seconds,Math.max(.02,(img.duration||100000)/1e6));g.drawImage(img,0,0,c.width,c.height);img.close();seconds+=delay;
      status.textContent=t('正在读取 GIF 帧… ','Reading GIF frames… ')+Math.round(100*(i+1)/count)+'%';await new Promise(r=>setTimeout(r,delay*1000));
    }
    rec.stop();await done;return {blob:new Blob(chunks,{type:rec.mimeType}),seconds};
  }finally{if(rec?.state==='recording')rec.stop();stream?.getTracks().forEach(t=>t.stop());decoder.close();}
}
export function openStudio(host){
  let resource=null,abort=null,version=0;host.className='media-studio';
  host.innerHTML='<h2>'+t('媒体工作台','Media studio')+'</h2><p>'+t('让每个片段，刚刚好。','Make every clip just right.')+'</p><label class="studio-picker">'+t('选择视频或 GIF 文件','Choose a video or GIF')+'<input type="file" accept="video/*,image/gif"></label><small role="status"></small><div class="inline-studio"></div>';
  const slot=host.querySelector('.inline-studio'),status=host.querySelector('[role=status]');
  function reset(){version++;abort?.abort();slot.dispatchEvent(new Event('dispose'));const old=slot.querySelector('video');if(old){old.pause();old.removeAttribute('src');old.load();}slot.replaceChildren();if(resource)URL.revokeObjectURL(resource);resource=null;}
  host.querySelector('input').onchange=async e=>{
    const f=e.target.files[0];if(!f)return;reset();const job=version;abort=new AbortController();status.textContent=f.name;
    try{const source=f.type==='image/gif'||f.name.toLowerCase().endsWith('.gif')?await gifTimeline(f,abort.signal,status):{blob:f,seconds:0};
      if(job!==version)return;const v=document.createElement('video');v.controls=true;v.playsInline=true;resource=URL.createObjectURL(source.blob);v.src=resource;slot.append(v);status.textContent=f.name;recordingControls(slot,v,source.seconds);
      v.onerror=()=>{status.textContent=t('此浏览器无法解码这个视频','This browser cannot decode this video');};
    }catch{if(job===version)status.textContent=t('此浏览器无法读取该素材。','This browser cannot decode this media.');}
  };
  return reset;
}
export let copiedImage=null;
export function pinCopied(){if(copiedImage)pin(copiedImage);}
function savePng(canvas){
  const a=document.createElement('a');a.href=canvas.toDataURL('image/png');a.download='light-block.png';a.click();
}
async function copyPng(canvas){
  copiedImage=canvas;
  if(window.ClipboardItem&&navigator.clipboard?.write){
    try{const item=new ClipboardItem({'image/png':new Promise(resolve=>canvas.toBlob(resolve))});await navigator.clipboard.write([item]);toast(t('已复制','Copied'));return;}catch{}
  }
  toast(t('已复制到演示剪贴板','Copied to the demo clipboard'));
}
function pin(canvas,where,doc,long=false){
  const box=result(t('置顶贴图','Pinned capture'),true);
  box.classList.add('native-pin');box.style.width=canvas.width+'px';
  if(where){box.style.left=where.x+'px';box.style.top=where.y+'px';}
  box.firstElementChild.remove();
  const view=document.createElement('div');view.className='pinned-view';
  const img=new Image();img.src=canvas.toDataURL('image/png');img.alt=t('置顶贴图','Pinned capture');img.draggable=false;view.append(img);box.append(view);
  const more=document.createElement('button');more.className='pin-more';more.textContent='⋯';more.title=t('贴图菜单','Pin menu');box.append(more);box.tabIndex=0;
  drag(box,img,desk);
  let border=true,scale=1,menu=null;
  const dismiss=()=>{menu?.remove();menu=null;};
  const open=()=>{
    dismiss();menu=document.createElement('div');menu.className='pin-context';
    for(const [key,label] of [['edit',t('编辑','Edit')],['copy',t('复制','Copy')],['save',t('保存 PNG','Save PNG')],['edge',border?t('隐藏边框','Hide border'):t('显示边框','Show border')],['opacity',t('恢复不透明','Restore opacity')],['close',t('关闭','Close')]]){
      const b=document.createElement('button');b.textContent=label;b.dataset.pin=key;menu.append(b);
    }
    box.append(menu);menu.querySelector('button').focus();menu.onclick=e=>{
      const key=e.target.dataset.pin;dismiss();
      if(key==='close')return clearResult(box);
      if(key==='edge'){border=!border;box.classList.toggle('borderless',!border);}
      if(key==='opacity')box.style.opacity='1';
      if(key==='save')savePng(canvas);
      if(key==='copy')copyPng(canvas);
      if(key==='edit')startCapture({pin:doc?.base||canvas,marks:doc?.marks||[],box,commit:(next,model)=>{canvas=next;doc=model;img.src=canvas.toDataURL();box.style.width=canvas.width*scale+'px';}});
    };
  };
  more.onclick=open;box.oncontextmenu=e=>{e.preventDefault();open();};
  box.onkeydown=e=>{if(e.key==='Escape'){e.stopPropagation();menu?dismiss():clearResult(box);}if(e.key==='ContextMenu'){e.preventDefault();open();}};
  view.addEventListener('wheel',e=>{e.preventDefault();if(long&&!e.ctrlKey&&!e.shiftKey&&view.scrollHeight>view.clientHeight){view.scrollTop+=e.deltaY;return;}if(e.ctrlKey){scale=Math.max(.25,Math.min(4,scale*(e.deltaY<0?1.1:1/1.1)));box.style.width=canvas.width*scale+'px';}else box.style.opacity=Math.min(1,Math.max(.15,Number(box.style.opacity||1)+(e.deltaY<0?.05:-.05)));},{passive:false});
}
export function cancelCapture(){captureCleanup?.();}
export async function startCapture(options={}){
  if(busy||recording)return;
  busy=true;$('app').hidden=true;
  let transition=null;
  try{
    const rects=[...desk.querySelectorAll('.file-tile,.note,.result,.bar-part,.document-window')].filter(el=>el.offsetWidth&&el.offsetHeight).map(el=>{const r=el.getBoundingClientRect();return {x:Math.floor(r.x),y:Math.floor(r.y),w:Math.round(r.width),h:Math.round(r.height),window:true};});
    const frame=options.pin||await snapshot();if(document.hidden){busy=false;return;}
    if(options.pin){const r=options.box.getBoundingClientRect();Object.assign(layer.style,{inset:'auto',left:r.left+'px',top:r.top+'px',width:r.width+'px',height:r.height+'px'});layer.classList.add('pin-edit');options.box.style.visibility='hidden';}
    captureCleanup=editFrame(frame,{initialMarks:options.marks,intent:options.recordSelection?'record':options.scrollSelection?'longshot':'',targets:rects,pinEdit:!!options.pin,initial:options.pin?{x:0,y:0,w:frame.width,h:frame.height}:null,
      onClose(){captureCleanup=null;busy=false;layer.classList.remove('pin-edit');layer.removeAttribute('style');if(options.box)options.box.style.visibility='';transition?.();},
      async onOutput(action,out,rect,_ink,doc){
        if(action==='copy')await copyPng(out);
        if(action==='save')savePng(out);
        if(action==='pin')transition=()=>pin(out,rect,doc);
        if(action==='done')options.commit(out,doc);
        if(action==='record')transition=()=>startRecording(rect);
        if(action==='longshot')transition=()=>longCapture(rect,frame);
      }});
  }catch{busy=false;failed();}
}
function cropFrame(frame,r){
  const c=document.createElement('canvas');c.width=r.w;c.height=r.h;
  c.getContext('2d').drawImage(frame,r.x,r.y,r.w,r.h,0,0,c.width,c.height);return c;
}
function rowSignature(c){
  const small=document.createElement('canvas');small.width=24;small.height=c.height;const ctx=small.getContext('2d',{willReadFrequently:true});ctx.drawImage(c,0,0,small.width,small.height);return ctx.getImageData(0,0,small.width,small.height).data;
}
function overlap(a,b,h){
  const row=24*4,min=Math.min(h,Math.max(8,Math.round(h*.1)));let best=Infinity,winner=0;
  for(let n=h;n>=min;n--){let err=0,count=0;
    for(let y=0;y<n;y+=Math.max(1,Math.floor(n/18)))for(let x=0;x<row;x+=4){const i=(h-n+y)*row+x,j=y*row+x;err+=Math.abs(a[i]-b[j])+Math.abs(a[i+1]-b[j+1])+Math.abs(a[i+2]-b[j+2]);count+=3;}
    const score=err/count;if(score<best-.01){best=score;winner=n;}if(best===0)break;
  }return best<5?winner:0;
}
export async function longCapture(rect,initial){
  if(busy||recording)return;
  busy=true;let panel,timer,closed=false,pending=0,queue=Promise.resolve();
  const finish=()=>{if(closed)return;closed=true;clearTimeout(timer);panel?.remove();desk.removeEventListener('scroll',changed,true);document.removeEventListener('keydown',key,true);captureCleanup=null;busy=false;};
  const changed=e=>{if(panel?.contains(e.target))return;clearTimeout(timer);timer=setTimeout(()=>{timer=null;sample(false);},160);};
  const key=e=>{if(e.key==='Escape'){e.stopImmediatePropagation();finish();}};
  let output,signature,rect0,preview,status,count=1;
  function refresh(){const thumb=document.createElement('canvas');thumb.width=Math.min(128,output.width);thumb.height=Math.max(1,Math.min(370,Math.round(thumb.width*output.height/output.width)));thumb.getContext('2d').drawImage(output,0,0,thumb.width,thumb.height);preview.src=thumb.toDataURL();}
  function sample(manual){pending++;queue=queue.then(async()=>{
    if(closed)return;const next=cropFrame(await snapshot(),rect0);if(closed)return;const sig=rowSignature(next),n=overlap(signature,sig,next.height);
    if(n===next.height){status.textContent=t('画面相同，滚动或切换页面后再采集','Same frame. Scroll or switch pages first.');return;}
    if(!n&&!manual){status.textContent=t('请点击采集下一屏','Select Capture next screen');return;}
    const height=output.height+next.height-n;if(height>16000||height*next.width>16000000){status.textContent=t('已到预览上限，可完成贴图','Preview limit reached. Finish to pin.');return;}
    const combined=document.createElement('canvas');combined.width=next.width;combined.height=height;const g=combined.getContext('2d');g.drawImage(output,0,0);g.drawImage(next,0,n,next.width,next.height-n,0,output.height,next.width,next.height-n);output=combined;signature=sig;count++;refresh();status.textContent=count+t(' 屏已拼接',' frames stitched');
  }).catch(()=>{if(!closed)failed();}).finally(()=>pending--);return queue;}
  try{
    const frame=initial||await snapshot();rect0=rect||{x:0,y:0,w:frame.width,h:frame.height};
    output=cropFrame(frame,rect0);signature=rowSignature(output);
    panel=document.createElement('div');panel.className='long-session';panel.setAttribute('data-html2canvas-ignore','');
    panel.innerHTML='<img alt=""><small role="status"></small>'+button('next',t('采集下一屏','Capture next screen'))+button('finish',t('完成','Finish'),true)+button('cancel',t('取消','Cancel'));
    desk.append(panel);preview=panel.querySelector('img');status=panel.querySelector('small');status.textContent=t('已采集首屏，可滚动或切换页面','First frame captured. Scroll or switch pages.');refresh();
    panel.onclick=async e=>{e.stopPropagation();const a=e.target.closest('button')?.dataset.act;if(a==='cancel')finish();if(a==='next')sample(true);if(a==='finish'){if(timer){clearTimeout(timer);timer=null;sample(false);}if(pending)await queue;if(!closed){finish();pin(output,rect0,null,true);}}};
    desk.addEventListener('scroll',changed,true);document.addEventListener('keydown',key,true);captureCleanup=finish;
  }catch{finish();failed();}
}
export function stopRecording(){recordingSession?.stop();}
export async function startRecording(rect){
  if(!rect)return startCapture({recordSelection:true});
  if(recording||busy)return;
  if(!window.MediaRecorder||!HTMLCanvasElement.prototype.captureStream){toast(t('此浏览器不支持演示录屏','Recording is unavailable in this browser'));return;}
  busy=true;
  const badge=$('rec-state');let timer,stream,recorder;
  try{
    $('app').hidden=true;
    const first=cropFrame(await snapshot(),rect);if(document.hidden)return;
    const output=document.createElement('canvas');output.width=first.width;output.height=first.height;
    const ctx=output.getContext('2d');ctx.drawImage(first,0,0);
    stream=output.captureStream(8);
    const mime=['video/webm;codecs=vp8','video/webm','video/mp4'].find(type=>MediaRecorder.isTypeSupported(type));
    recorder=new MediaRecorder(stream,mime?{mimeType:mime}:undefined);
    const chunks=[];recorder.ondataavailable=e=>{if(e.data.size)chunks.push(e.data);};
    const finish=()=>{if(recorder.state!=='inactive')recorder.stop();};
    recordingSession={stop:finish};
    recorder.onstop=()=>{
      recording=false;recordingSession=null;clearTimeout(timer);stream.getTracks().forEach(track=>track.stop());badge.hidden=true;
      if(!chunks.length){failed();return;}
      const box=result(t('录屏回放','Recording playback'));const video=document.createElement('video');video.controls=true;video.playsInline=true;
      playingUrl=URL.createObjectURL(new Blob(chunks,{type:recorder.mimeType}));video.src=playingUrl;box.append(video);
      const ext=recorder.mimeType.includes('mp4')?'mp4':'webm';download(box,playingUrl,'light-block.'+ext);recordingControls(box,video,(performance.now()-started)/1000);
    };
    recorder.onerror=()=>{failed();finish();};
    const started=performance.now();
    badge.innerHTML='<span></span>'+button('stop',t('结束录制','Stop recording'));badge.hidden=false;badge.querySelector('button').onclick=finish;
    recorder.start(250);recording=true;
    async function tick(){
      if(recorder.state!=='recording')return;
      const seconds=Math.floor((performance.now()-started)/1000);
      badge.firstChild.textContent=t('录制中 ','Recording ')+seconds+' / 12 s';
      if(seconds>=12||document.hidden){finish();return;}
      try{const frame=await snapshot();if(recorder.state==='recording')ctx.drawImage(frame,rect.x,rect.y,rect.w,rect.h,0,0,output.width,output.height);}catch{finish();return;}
      if(recorder.state==='recording')timer=setTimeout(tick,150);
    }
    tick();
  }catch{stream?.getTracks().forEach(track=>track.stop());recording=false;recordingSession=null;badge.hidden=true;failed();}
  finally{busy=false;}
}
document.addEventListener('visibilitychange',()=>{if(document.hidden)stopRecording();});
window.addEventListener('pagehide',()=>{stopRecording();clearResult();});
