import {t,escape as esc,deploymentAssets} from './ui.mjs';
let entries=deploymentAssets?.catalog,query='',kind='image',selectedId,active=null;

const typeName={image:t('静态','Still'),gif:'GIF',video:t('视频','Video')};
const title=e=>t(e.title.zh,e.title.en);
export function leaveCatalog(){active?.();active=null;}
export function renderCatalog(host,{onApply,onDownload,localFor,onPreviewApply}){
  leaveCatalog();
  const body=document.createElement('section');body.className='catalog native-library';host.append(body);
  let req=null,player=null,previewing=false,epoch=0,live=true;
  function suspend(){epoch++;req?.abort();req=null;if(player){if(player.tagName==='VIDEO'){player.pause();player.removeAttribute('src');player.load();}else player.removeAttribute('src');player.remove();player=null;}previewing=false;}
  active=()=>{live=false;suspend();};
  function draw(){
    if(!live||!body.isConnected)return;suspend();
    const items=entries.filter(e=>e.kind===kind&&(e.title.zh+' '+e.title.en+' '+(e.tags||[]).join(' ')+' '+e.author).toLowerCase().includes(query.toLowerCase()));
    const e=items.find(item=>item.id===selectedId)||items[0];selectedId=e?.id;
    body.innerHTML='<div class="browse"><input type="search" class="field" placeholder="'+t('搜索在线壁纸、标题或标签…','Search wallpapers, titles or tags…')+'" aria-label="'+t('搜索在线壁纸','Search online wallpapers')+'" value="'+esc(query)+'"><div class="strip">'+Object.entries(typeName).map(([k,v])=>'<button class="choice" data-type="'+k+'" aria-pressed="'+(kind===k)+'">'+v+'</button>').join('')+'</div><div class="catalog-grid"></div><div class="online-player" hidden></div></div><aside class="inspector"></aside>';
    const grid=body.querySelector('.catalog-grid'),view=body.querySelector('.online-player'),panel=body.querySelector('aside');
    grid.innerHTML=items.map(it=>'<button class="thumb" data-entry="'+esc(it.id)+'" aria-pressed="'+(it===e)+'"><img loading="lazy" referrerpolicy="no-referrer" src="'+esc(it.cover||it.thumbnail_url)+'" alt=""><span>'+esc(title(it))+'</span><small>'+it.width+' × '+it.height+' · '+(it.bytes/1e6).toFixed(1)+' MB</small><small>'+ (!!localFor(it.id)?t('已下载','Downloaded'):typeName[it.kind])+'</small></button>').join('');
    if(e){
      panel.innerHTML='<b>'+esc(title(e))+'</b><img class="cover" src="'+esc(e.cover||e.thumbnail_url)+'" alt="">'+
        '<button data-preview class="pick">'+t('开始预览','Start preview')+'</button><small>'+e.width+' × '+e.height+' · '+(e.bytes/1e6).toFixed(1)+' MB</small>'+
        '<small><a href="'+esc(e.source_url)+'" rel="noopener" target="_blank">'+esc(e.author)+'</a> · <a href="'+esc(e.license_url)+'" rel="noopener" target="_blank">'+esc(e.license)+'</a></small>'+(e.preview?'<small>'+t('网页预览已缩放，动态素材为无声片段，沿用原许可。','Scaled preview. Motion excerpts are muted and retain the source license.')+'</small>':'')+
        '<button data-fetch class="action">'+(!!localFor(e.id)?t('已下载','Downloaded'):t('下载原文件','Download original'))+'</button><button data-apply class="pick">'+t('应用到桌面 →','Apply to desktop →')+'</button><small role="status"></small>';
    }else panel.textContent=t('没有匹配的壁纸','No matching wallpapers');
    body.oninput=ev=>{if(ev.target.type==='search'){query=ev.target.value;draw();body.querySelector('input').focus({preventScroll:true});}};
    body.onclick=async ev=>{
      const b=ev.target.closest('button');if(!b)return;
      if(b.dataset.type){kind=b.dataset.type;return draw();}
      if(b.dataset.entry){selectedId=b.dataset.entry;const scroll=grid.scrollTop;draw();body.querySelector('.catalog-grid').scrollTop=scroll;return;}
      if(!e)return;
      const msg=panel.querySelector('[role=status]');
      if(b.hasAttribute('data-preview')){
        if(previewing){draw();return;}
        suspend();const myEpoch=epoch;previewing=true;grid.hidden=true;view.hidden=false;
        b.textContent=t('停止预览 · 返回图库','Stop preview · Back');
        const clip=e.preview?.endsWith('.mp4')||e.kind==='video';
        player=document.createElement(clip?'video':'img');
        if(clip){player.muted=true;player.playsInline=true;player.controls=true;player.autoplay=true;player.loop=true;}
        else player.alt=title(e);
        msg.textContent=t('正在准备预览…','Preparing preview…');
        const ready=()=>{if(epoch===myEpoch)msg.textContent='';};
        player.onload=player.onloadeddata=ready;
        player.onerror=()=>{if(epoch===myEpoch){msg.textContent=t('来源暂时无法预览，可重试或打开作者来源。','Preview unavailable. Retry or open the source.');}};
        player.src=e.preview||localFor(e.id)?.url||e.download_url;view.replaceChildren(player);return;
      }
      if(b.hasAttribute('data-use-preview'))return onPreviewApply(e);
      if(req){suspend();return draw();}
      if(localFor(e.id)){if(b.hasAttribute('data-apply'))onApply(e);return;}
      suspend();grid.hidden=false;view.hidden=true;panel.querySelector('[data-preview]').textContent=t('开始预览','Start preview');
      const job=new AbortController();req=job;
      b.textContent=t('取消下载','Cancel download');msg.textContent=t('正在连接素材来源…','Connecting to the source…');
      const timeout=setTimeout(()=>job.abort(),30000);
      try{
        const response=await fetch(e.download_url,{signal:job.signal,credentials:'omit'});if(!response.ok||!response.body)throw Error();
        const reader=response.body.getReader(),parts=[];let total=0;
        for(;;){const {done,value}=await reader.read();if(done)break;total+=value.length;if(total>e.bytes){await reader.cancel();throw Error();}parts.push(value);msg.textContent=Math.round(total*100/e.bytes)+'%';}
        if(total!==e.bytes)throw Error();
        const blob=new Blob(parts,{type:response.headers.get('content-type')||({image:'image/jpeg',gif:'image/gif',video:'video/mp4'})[kind]});
        if(e.sha1){const digest=new Uint8Array(await crypto.subtle.digest('SHA-1',await blob.arrayBuffer()));if([...digest].map(n=>n.toString(16).padStart(2,'0')).join('')!==e.sha1.toLowerCase())throw Error();}
        if(req!==job||!live)return;
        onDownload(e,blob);if(b.hasAttribute('data-apply'))onApply(e);else draw();
      }catch{if(live&&req===job){msg.textContent=t('下载或校验未完成，可重试。','Download or verification failed. Try again.');b.textContent=b.hasAttribute('data-apply')?t('应用到桌面','Apply to desktop'):t('下载原文件','Download original');if(e.preview){const fallback=document.createElement('button');fallback.className='pick';fallback.dataset.usePreview='';fallback.textContent=t('使用轻量预览体验桌面','Try the preview on the desktop');msg.append(fallback);}}}
      finally{clearTimeout(timeout);if(req===job)req=null;}
    };
  }
  if(entries){draw();return;}
  body.textContent=t('正在打开图库…','Loading gallery…');
  fetch('./catalog.json').then(r=>r.json()).then(v=>{entries=v;draw();}).catch(()=>{body.textContent=t('图库暂时不可用','Gallery unavailable');});
}
