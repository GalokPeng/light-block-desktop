import { $, t, ib, button, drag, toast } from './ui.mjs';

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
  return render(target,{scale:1,backgroundColor:'#1b2d32',logging:false,imageTimeout:4000,...options});
}
function failed(){toast(t('未能采集演示画面，请重试','Could not capture the demo. Try again.'));}
const pos=(e,canvas)=>{const r=canvas.getBoundingClientRect();return {x:Math.max(0,Math.min(canvas.width,(e.clientX-r.left)*canvas.width/r.width)),y:Math.max(0,Math.min(canvas.height,(e.clientY-r.top)*canvas.height/r.height))};};
function result(title){
  clearResult();
  const box=document.createElement('div');box.className='result';
  box.innerHTML='<div class="result-head"><b></b>'+ib('result-close',t('关闭预览','Close preview'),'close')+'</div>';
  box.querySelector('b').textContent=title;
  box.querySelector('button').onclick=clearResult;
  $('results').append(box);drag(box,box.firstElementChild,desk);return box;
}
function clearResult(){
  const movie=$('results').querySelector('video');if(movie){movie.pause();movie.removeAttribute('src');movie.load();}
  $('results').replaceChildren();
  if(playingUrl){URL.revokeObjectURL(playingUrl);playingUrl=null;}
}
function download(box,url,name){
  const row=document.createElement('div');row.className='result-controls';
  const a=document.createElement('a');a.href=url;a.download=name;a.textContent=t('保存到本地 ↓','Save a copy ↓');row.append(a);box.append(row);return row;
}
function pin(canvas){
  const box=result(t('置顶贴图','Pinned capture'));const img=new Image();img.alt=t('演示桌面截图','Demo desktop capture');img.src=canvas.toDataURL('image/png');const view=document.createElement('div');view.className='pinned-view';view.append(img);box.append(view);
  const controls=download(box,img.src,'light-block.png');
  const zoom=document.createElement('input');zoom.type='range';zoom.min=50;zoom.max=250;zoom.value=100;zoom.setAttribute('aria-label',t('贴图缩放','Capture zoom'));zoom.title=zoom.getAttribute('aria-label');
  const resize=()=>{img.style.width=zoom.value+'%';img.style.maxHeight='none';};
  zoom.oninput=resize;controls.append(zoom);
  view.addEventListener('wheel',e=>{if(e.ctrlKey){e.preventDefault();zoom.value=Number(zoom.value)+(e.deltaY<0?10:-10);resize();}else if(!view.scrollHeight||view.scrollHeight<=view.clientHeight){e.preventDefault();box.style.opacity=Math.max(.25,Math.min(1,Number(box.style.opacity||1)+(e.deltaY<0?.05:-.05)));}},{passive:false});
}
export function cancelCapture(){captureCleanup?.();}
export async function startCapture(){
  if(busy||recording)return;
  busy=true;$('app').hidden=true;
  try{
    const frame=await snapshot();if(document.hidden)return;
    const canvas=document.createElement('canvas');canvas.width=frame.width;canvas.height=frame.height;
    const ctx=canvas.getContext('2d');const bar=document.createElement('div');bar.className='capbar';
    layer.replaceChildren(canvas,bar);layer.hidden=false;
    let start,rect,mode='select',drawing=false,strokes=[];
    function controls(){
      const cancel=button('cancel',t('取消','Cancel'));
      bar.innerHTML=mode==='select'?'<span>'+t('拖动框选桌面区域','Drag to select an area')+'</span>'+button('all',t('全选','Select all'))+cancel:
        button('ink',t('画笔','Pen'))+button('box',t('矩形','Rectangle'))+button('undo',t('撤销','Undo'))+button('again',t('重选','Reselect'))+button('pin',t('贴到桌面','Pin'),true)+cancel;
      bar.querySelector('[data-act="'+mode+'"]')?.setAttribute('aria-pressed','true');
    }
    function paint(){
      ctx.clearRect(0,0,canvas.width,canvas.height);ctx.drawImage(frame,0,0);ctx.fillStyle='rgba(8,15,19,.48)';ctx.fillRect(0,0,canvas.width,canvas.height);
      if(!rect||rect.w<1||rect.h<1)return;
      ctx.save();ctx.beginPath();ctx.rect(rect.x,rect.y,rect.w,rect.h);ctx.clip();ctx.drawImage(frame,0,0);ctx.lineWidth=3;ctx.lineCap='round';ctx.lineJoin='round';ctx.strokeStyle='#fc795b';
      for(const {tool,points} of strokes){
        const first=points[0],last=points.at(-1);
        if(tool==='box'){ctx.strokeRect(first.x,first.y,last.x-first.x,last.y-first.y);continue;}
        ctx.beginPath();ctx.moveTo(first.x,first.y);for(const p of points)ctx.lineTo(p.x,p.y);ctx.stroke();
      }
      ctx.restore();ctx.strokeStyle='#9efad1';ctx.lineWidth=1;ctx.strokeRect(rect.x-.5,rect.y-.5,rect.w+1,rect.h+1);
    }
    function reset(){start=null;drawing=false;mode='select';rect=null;strokes=[];paint();controls();}
    captureCleanup=()=>{layer.hidden=true;layer.replaceChildren();captureCleanup=null;busy=false;$('bar').querySelector('[data-act="show:capture"]')?.focus();};
    bar.onclick=e=>{
      const act=e.target.closest('[data-act]')?.dataset.act;
      if(act==='cancel')return cancelCapture();
      if(act==='all'){rect={x:0,y:0,w:canvas.width,h:canvas.height};mode='ink';}
      if(act==='again')return reset();
      if(act==='ink'||act==='box')mode=act;
      if(act==='undo')strokes.pop();
      if(act==='pin'&&rect){
        const out=document.createElement('canvas');out.width=rect.w;out.height=rect.h;out.getContext('2d').drawImage(canvas,rect.x,rect.y,rect.w,rect.h,0,0,out.width,out.height);
        cancelCapture();pin(out);return;
      }
      paint();controls();
    };
    canvas.onpointerdown=e=>{
      if(e.button!==0)return;
      const p=pos(e,canvas);start=p;canvas.setPointerCapture(e.pointerId);
      if(mode!=='select'){strokes.push({tool:mode,points:[p]});drawing=true;}paint();
    };
    canvas.onpointermove=e=>{
      if(!start)return;const p=pos(e,canvas);
      if(mode==='select'){rect={x:Math.floor(Math.min(start.x,p.x)),y:Math.floor(Math.min(start.y,p.y)),w:Math.round(Math.abs(p.x-start.x)),h:Math.round(Math.abs(p.y-start.y))};}
      else if(drawing)strokes.at(-1).points.push(p);
      paint();
    };
    canvas.onpointerup=e=>{if(!start)return;canvas.onpointermove(e);start=null;drawing=false;if(mode==='select'&&rect?.w>4&&rect?.h>4)mode='ink';paint();controls();};
    canvas.onpointercancel=()=>{start=null;drawing=false;};
    canvas.oncontextmenu=e=>{e.preventDefault();mode==='select'?cancelCapture():reset();};
    paint();controls();bar.querySelector('button').focus();
  }catch{failed();}finally{if(!captureCleanup)busy=false;}
}
export async function longCapture(){
  if(busy||recording)return;
  busy=true;
  try{
    const page=document.createElement('div');page.style.cssText='position:absolute;top:0;left:-2000px;width:440px;padding:16px;background:#17282b;color:white';
    const title=document.createElement('h2');title.textContent=t('桌面壁纸手册','Desktop wallpaper journal');page.append(title);
    for(const [key,zh,en] of [['mountain-bay','晨光海湾','Morning bay'],['rain-coast','海岸弯道','Coastal roads'],['milky-way','银河星空','Milky Way']]){
      const label=document.createElement('p');label.textContent=t(zh,en);page.append(label);
      const img=new Image();img.src='/media/wallpapers/'+key+'.webp';img.width=408;img.height=230;page.append(img);
    }
    document.body.append(page);
    try{await Promise.all([...page.querySelectorAll('img')].map(i=>i.decode()));pin(await snapshot(page));$('app').hidden=true;}finally{page.remove();}
  }catch{failed();}finally{busy=false;}
}
export function stopRecording(){recordingSession?.stop();}
export async function startRecording(){
  if(recording||busy)return;
  if(!window.MediaRecorder||!HTMLCanvasElement.prototype.captureStream){toast(t('此浏览器不支持演示录屏','Recording is unavailable in this browser'));return;}
  busy=true;
  const badge=$('rec-state');let timer,stream,recorder;
  try{
    $('app').hidden=true;
    const first=await snapshot();if(document.hidden)return;
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
      download(box,playingUrl,'light-block.'+(recorder.mimeType.includes('mp4')?'mp4':'webm'));
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
      try{const frame=await snapshot();if(recorder.state==='recording')ctx.drawImage(frame,0,0,output.width,output.height);}catch{finish();return;}
      if(recorder.state==='recording')timer=setTimeout(tick,150);
    }
    tick();
  }catch{stream?.getTracks().forEach(track=>track.stop());recording=false;recordingSession=null;badge.hidden=true;failed();}
  finally{busy=false;}
}
document.addEventListener('visibilitychange',()=>{if(document.hidden)stopRecording();});
window.addEventListener('pagehide',()=>{stopRecording();clearResult();});
