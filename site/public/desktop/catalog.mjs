import {t,escape as esc} from './ui.mjs';
let entries,query='',kind='image',page=0,selected=null,controller;
const typeName={image:t('静态','Still'),gif:'GIF',video:t('视频','Video')};
const title=e=>t(e.title.zh,e.title.en);
export function leaveCatalog(){controller?.abort();controller=null;}
export function renderCatalog(host,onApply){
  const body=document.createElement('section');body.className='catalog';host.append(body);
  function draw(){
    if(!body.isConnected)return;
    const items=entries.filter(e=>e.kind===kind&&(e.title.zh+' '+e.title.en+' '+e.author).toLowerCase().includes(query.toLowerCase()));
    const start=page*4;
    body.innerHTML='<div class="strip">'+Object.entries(typeName).map(([k,v])=>'<button class="choice" data-type="'+k+'" aria-pressed="'+(kind===k)+'">'+v+'</button>').join('')+'</div>'+
      '<input aria-label="'+t('搜索在线壁纸','Search online wallpapers')+'" class="field" type="search" placeholder="'+t('搜索标题或作者','Search titles or authors')+'" value="'+esc(query)+'"><div class="list">'+
      items.slice(start,start+4).map(e=>'<button data-entry="'+esc(e.id)+'">'+esc(title(e))+' <small>· '+(e.bytes/1e6).toFixed(1)+' MB</small></button>').join('')+'</div>'+
      '<div class="strip"><button data-step="-1" '+(!page?'disabled':'')+'>‹</button><span>'+Math.min(page+1,Math.ceil(items.length/4))+' / '+Math.ceil(items.length/4)+'</span><button data-step="1" '+(start+4>=items.length?'disabled':'')+'>›</button></div>';
    if(selected){
      const e=selected;
      const card=document.createElement('div');card.className='group';card.innerHTML='<b>'+esc(title(e))+'</b><p><a target="_blank" rel="noopener" href="'+esc(e.source_url)+'">'+esc(e.author)+'</a> · <a target="_blank" rel="noopener" href="'+esc(e.license_url)+'">'+esc(e.license)+'</a></p><div class="strip"><button class="action" data-fetch>'+t('加载并应用到演示桌面','Load onto demo desktop')+'</button><a href="'+esc(e.download_url)+'" target="_blank" rel="noopener">'+t('打开原文件','Open original')+'</a></div><small role="status"></small>';
      body.append(card);host.scrollTop=host.scrollHeight;
    }
    body.oninput=e=>{if(e.target.type==='search'){query=e.target.value;page=0;selected=null;draw();const field=body.querySelector('input');field.focus({preventScroll:true});}};
    body.onclick=async event=>{
      const b=event.target.closest('button');if(!b)return;
      if(b.dataset.type){leaveCatalog();kind=b.dataset.type;page=0;selected=null;return draw();}
      if(b.dataset.step){leaveCatalog();page+=Number(b.dataset.step);selected=null;return draw();}
      if(b.dataset.entry){leaveCatalog();selected=entries.find(e=>e.id===b.dataset.entry);return draw();}
      if(!b.hasAttribute('data-fetch'))return;
      leaveCatalog();const req=new AbortController();controller=req;const chosen=selected;
      const label=body.querySelector('[role=status]');b.disabled=true;label.textContent=t('正在连接素材来源…','Connecting to the source…');
      const timeout=setTimeout(()=>req.abort(),30000);
      try{
        const response=await fetch(chosen.download_url,{signal:req.signal,credentials:'omit'});
        if(!response.ok||!response.body)throw Error('source');
        const reader=response.body.getReader(),parts=[];let total=0;
        for(;;){const {done,value}=await reader.read();if(done)break;total+=value.length;if(total>chosen.bytes){await reader.cancel();throw Error('size');}parts.push(value);label.textContent=Math.round(total*100/chosen.bytes)+'%';}
        if(total!==chosen.bytes)throw Error('size');
        const blob=new Blob(parts,{type:response.headers.get('Content-Type')||({gif:'image/gif',image:'image/jpeg',video:'video/mp4'})[chosen.kind]});
        if(chosen.sha1){const digest=new Uint8Array(await crypto.subtle.digest('SHA-1',await blob.arrayBuffer()));if([...digest].map(b=>b.toString(16).padStart(2,'0')).join('')!==chosen.sha1.toLowerCase())throw Error('integrity');}
        if(req===controller&&body.isConnected)onApply(chosen,blob);
      }catch{if(body.isConnected&&req===controller)label.textContent=t('此来源暂不可用，可点击“打开原文件”。','Source unavailable. You can open the original file.');}
      finally{clearTimeout(timeout);b.disabled=false;}
    };
  }
  if(entries){draw();return;}
  body.textContent=t('正在打开图库…','Loading gallery…');
  fetch('./catalog.json').then(r=>r.json()).then(v=>{entries=v;draw();}).catch(()=>{body.textContent=t('图库暂时不可用','Gallery unavailable');});
}
