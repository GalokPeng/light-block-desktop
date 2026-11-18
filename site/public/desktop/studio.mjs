import {$,t,button,toast} from './ui.mjs';
import {editFrame} from './editor.mjs';

// GIF89a, a global 3:3:2 palette and literal LZW blocks with a bounded code width.
function gifWriter(width,height,fps){
  const blocks=[],word=n=>[n&255,n>>8],ascii=s=>Array.from(s,c=>c.charCodeAt(0));
  const add=bytes=>blocks.push(Uint8Array.from(bytes));
  add([...ascii('GIF89a'),...word(width),...word(height),247,0,0]);
  const palette=[];for(let n=0;n<256;n++)palette.push(Math.round((n>>5)*255/7),Math.round((n>>2&7)*255/7),(n&3)*85);add(palette);
  add([33,255,11,...ascii('NETSCAPE2.0'),3,1,0,0,0]);
  return {
    frame(rgba){add([33,249,4,4,...word(Math.max(2,Math.round(100/fps))),0,0,44,0,0,0,0,...word(width),...word(height),0,8]);
      let pack=0,bits=0,bytes=[];
      const code=n=>{pack|=n<<bits;bits+=9;while(bits>=8){bytes.push(pack&255);pack>>=8;bits-=8;}};
      for(let i=0;i<rgba.length;i+=4){if((i/4)%250===0)code(256);code((rgba[i]&224)|(rgba[i+1]>>3&28)|(rgba[i+2]>>6));}
      code(257);if(bits)bytes.push(pack&255);for(let i=0;i<bytes.length;i+=255){const part=bytes.slice(i,i+255);add([part.length,...part]);}add([0]);
    },finish(){add([59]);return new Blob(blocks,{type:'image/gif'});}
  };
}
export const exportPreferences={resolution:0,fps:0};
const videoFormats=()=>['video/mp4','video/webm'].filter(m=>window.MediaRecorder?.isTypeSupported(m));
async function seek(video,time){
  if(Math.abs(video.currentTime-time)<.0005&&video.readyState>=2)return;
  await new Promise((resolve,reject)=>{
    const cleanup=()=>{clearTimeout(timer);video.removeEventListener('seeked',ok);video.removeEventListener('error',bad);};
    const ok=()=>{cleanup();resolve();},bad=()=>{cleanup();reject(Error('seek'));},timer=setTimeout(bad,4500);
    video.addEventListener('seeked',ok,{once:true});video.addEventListener('error',bad,{once:true});video.currentTime=time;
  });
}
export function recordingControls(box,video,seconds){
  const controls=document.createElement('section');controls.className='studio-controls';
  const labels=[t('裁剪起点','Trim start'),t('裁剪终点','Trim end')];
  controls.innerHTML=labels.map((s,i)=>'<label>'+s+' <output></output><input type="range" data-trim="'+i+'" min="0" step="0.04" max="1" value="'+i+'"></label>').join('')+
    '<div class="studio-actions">'+button('crop',t('裁剪画面','Crop frame'))+button('ink',t('涂鸦','Annotate'))+'<select data-format aria-label="'+t('格式','Format')+'">'+[...videoFormats(),'image/gif'].map(m=>'<option value="'+m+'">'+m.split('/')[1].toUpperCase()+'</option>').join('')+'</select>'+
    '<select data-fps aria-label="'+t('帧率','Frame rate')+'">'+[8,12,15,24,30,60].map(n=>'<option '+(n===(exportPreferences.fps||8)?'selected':'')+'>'+n+'</option>').join('')+'</select>'+
    '<select data-quality aria-label="'+t('画质','Quality')+'">'+[0,1080,720,480].map(n=>'<option value="'+n+'" '+(n===exportPreferences.resolution?'selected':'')+'>'+(n?n+'p':t('原始分辨率','Original'))+'</option>').join('')+'</select>'+button('export',t('导出','Export'),true)+'</div><small role="status"></small>';
  box.append(controls);
  const [start,end]=controls.querySelectorAll('[data-trim]'),status=controls.querySelector('small');
  let crop,ink,trimEnd=0,stopEdit,job=null;
  const extent=()=>Number.isFinite(video.duration)&&video.duration>0?video.duration:seconds;
  function initialize(){const duration=extent();if(!duration||!video.videoWidth)return;trimEnd=duration;
    for(const h of [start,end])h.max=duration;end.value=duration;out();crop={x:0,y:0,w:video.videoWidth,h:video.videoHeight};}
  function out(){for(const input of [start,end])input.previousElementSibling.textContent=(+input.value).toFixed(2)+' s';}
  if(video.readyState>=1)initialize();else video.addEventListener('loadedmetadata',initialize,{once:true});
  for(const h of [start,end])h.oninput=()=>{if(h===start)start.value=Math.min(+start.value,+end.value-.04);else end.value=Math.max(+start.value+.04,+end.value);
    trimEnd=+end.value;video.pause();video.currentTime=+h.value;out();};
  video.addEventListener('play',()=>{if(video.currentTime<+start.value||video.currentTime>=trimEnd-.02)video.currentTime=+start.value;});
  video.addEventListener('timeupdate',()=>{if(trimEnd&&video.currentTime>=trimEnd&&!video.paused)video.pause();});
  const frame=()=>{const c=document.createElement('canvas');c.width=video.videoWidth;c.height=video.videoHeight;const g=c.getContext('2d');g.drawImage(video,0,0);if(ink)g.drawImage(ink,0,0);return c;};
  function overlay(){
    box.querySelector('.video-ink')?.remove();if(!ink)return;
    const image=new Image();image.className='video-ink';image.src=ink.toDataURL();image.style.top=video.offsetTop+'px';image.style.height=video.clientHeight+'px';box.append(image);
  }
  controls.onclick=async e=>{const a=e.target.closest('[data-act]')?.dataset.act;if(!a||!crop||job)return;e.stopPropagation();video.pause();
    if(a==='crop'||a==='ink'){
      const r={x:0,y:0,w:video.videoWidth,h:video.videoHeight};box.style.visibility='hidden';
      stopEdit=editFrame(frame(),{initial:a==='crop'?{...crop}:r,pinEdit:true,mediaEdit:a,onClose(){box.style.visibility='';stopEdit=null;},onOutput(key,_,rect,layer){
        if(key==='done'){if(a==='crop'){crop=rect;status.textContent=rect.w+' × '+rect.h;}else{if(!ink)ink=layer;else ink.getContext('2d').drawImage(layer,0,0);overlay();}}
      }});return;
    }
    if(a!=='export')return;
    const format=controls.querySelector('[data-format]').value,fps=+controls.querySelector('[data-fps]').value,ratio=Math.min(1,(+controls.querySelector('[data-quality]').value||crop.h)/crop.h);
    const from=+start.value,to=+end.value;
    const clone=document.createElement('video');clone.src=video.currentSrc;clone.muted=true;clone.playsInline=true;
    const c=document.createElement('canvas'),scale=format==='image/gif'?Math.min(ratio,540/crop.w):ratio;
    c.width=Math.max(2,Math.round(crop.w*scale/2)*2);c.height=Math.max(2,Math.round(crop.h*scale/2)*2);const g=c.getContext('2d',{willReadFrequently:format==='image/gif'});
    if(format==='image/gif'&&(to-from)*fps*c.width*c.height>80000000){status.textContent=t('片段较长，请缩短范围或降低帧率后导出 GIF。','Shorten the range or lower the frame rate for GIF export.');return;}
    const abort=new AbortController();job=abort;let stream,rec,blob;const btn=e.target.closest('button');btn.disabled=true;
    const draw=()=>{g.drawImage(clone,crop.x,crop.y,crop.w,crop.h,0,0,c.width,c.height);if(ink)g.drawImage(ink,crop.x,crop.y,crop.w,crop.h,0,0,c.width,c.height);};
    try{
      await new Promise((resolve,reject)=>{clone.onloadeddata=resolve;clone.onerror=reject;clone.load();});await seek(clone,from);draw();
      if(format==='image/gif'){
        const writer=gifWriter(c.width,c.height,fps),n=Math.max(1,Math.ceil((to-from)*fps));
        for(let i=0;i<n;i++){if(abort.signal.aborted)return;await seek(clone,Math.min(to-.001,from+i/fps));draw();writer.frame(g.getImageData(0,0,c.width,c.height).data);status.textContent=Math.round((i+1)*100/n)+'%';}
        blob=writer.finish();
      }else{
        stream=c.captureStream(fps);rec=new MediaRecorder(stream,{mimeType:format,videoBitsPerSecond:Math.round(c.width*c.height*fps*.3)});const chunks=[];
        const done=new Promise((resolve,reject)=>{rec.ondataavailable=e=>chunks.push(e.data);rec.onstop=resolve;rec.onerror=reject;});rec.start();await clone.play();
        while(!abort.signal.aborted&&clone.currentTime<to&&!clone.ended){draw();status.textContent=Math.min(100,Math.round((clone.currentTime-from)*100/(to-from)))+'%';await new Promise(r=>setTimeout(r,1000/fps));}
        rec.stop();await done;blob=new Blob(chunks,{type:format});
      }
      if(abort.signal.aborted)return;const link=document.createElement('a');link.href=URL.createObjectURL(blob);link.download='light-block.'+format.split('/')[1];link.click();setTimeout(()=>URL.revokeObjectURL(link.href),30000);status.textContent=t('已导出','Exported');
    }catch{if(!abort.signal.aborted)toast(t('导出失败，请重新选择格式或范围','Export failed. Try another format or range.'));}
    finally{if(rec?.state==='recording')rec.stop();stream?.getTracks().forEach(tr=>tr.stop());clone.pause();clone.removeAttribute('src');clone.load();job=null;btn.disabled=false;}
  };
  box.addEventListener('dispose',()=>{job?.abort();stopEdit?.();},{once:true});
}
