import {$,t,toast} from './ui.mjs';
import {tool,groups,defaults,limits,parameters,renderMark,textFont,bounds,makeCanvas} from './annotation.mjs';

const clamp=(v,min,max)=>Math.max(min,Math.min(max,v));
export function editFrame(frame,{intent='',mediaEdit='',initialMarks=[],targets=[],initial=null,onClose,onOutput,pinEdit=false}){
  const layer=$('capture-layer'), canvas=makeCanvas(frame.width,frame.height), ctx=canvas.getContext('2d');
  const ink=makeCanvas(frame.width,frame.height), brush=ink.getContext('2d',{willReadFrequently:true});
  const bar=document.createElement('div'),side=document.createElement('div');
  bar.className='native-capbar';side.className='native-capside';canvas.tabIndex=0;canvas.dataset.pointerRole='cross';
  layer.replaceChildren(canvas,bar,side);layer.hidden=false;
  const choices=groups.map(g=>g[0]), style={...defaults}, pixels=new Map(), undo=[],redo=[];
  const full=()=>({x:0,y:0,w:frame.width,h:frame.height});
  let region=initial||null, mode=mediaEdit==='crop'?'crop':region?'move':'select',smart=true,target=null,marks=structuredClone(initialMarks),gesture=null,input=null;
  function remember(){undo.push(structuredClone(marks));if(undo.length>24)undo.shift();redo.length=0;}
  function pixelSource(size){if(!pixels.has(size)){const c=makeCanvas(Math.max(1,Math.ceil(frame.width/size)),Math.max(1,Math.ceil(frame.height/size)));c.getContext('2d').drawImage(frame,0,0,c.width,c.height);pixels.set(size,c);}return pixels.get(size);}
  function point(e){const r=canvas.getBoundingClientRect();return {x:Math.round(clamp((e.clientX-r.left)*canvas.width/r.width,0,canvas.width)),y:Math.round(clamp((e.clientY-r.top)*canvas.height/r.height,0,canvas.height))};}
  function handles(){const {x,y,w,h}=region;return [[0,0],[.5,0],[1,0],[1,.5],[1,1],[.5,1],[0,1],[0,.5]].map(([u,v])=>({x:x+w*u,y:y+h*v,u,v}));}
  function inkLayer(){brush.clearRect(0,0,ink.width,ink.height);for(const mark of marks)renderMark(brush,mark,pixelSource);}
  function paint(){
    ctx.drawImage(frame,0,0);inkLayer();const r=region||(gesture?.kind==='select'?gesture.rect:smart&&target);
    if(!r)return;
    const {x,y,w,h}=r;
    if(region){ctx.fillStyle='#05091277';ctx.fillRect(0,0,canvas.width,canvas.height);if(w&&h)ctx.drawImage(frame,x,y,w,h,x,y,w,h);}
    ctx.save();ctx.beginPath();ctx.rect(x,y,w,h);ctx.clip();ctx.drawImage(ink,0,0);ctx.restore();
    ctx.strokeStyle='#46b9ff';ctx.lineWidth=1.5;ctx.strokeRect(x+.5,y+.5,w-1,h-1);
    if(region&&!pinEdit){ctx.fillStyle='#53bcff';for(const p of handles())ctx.fillRect(p.x-2.5,p.y-2.5,5,5);}
  }
  function position(){
    const W=layer.clientWidth,H=layer.clientHeight;
    const scaleX=W/frame.width,scaleY=H/frame.height;
    if(!region){bar.style.left=Math.max(2,(W-bar.offsetWidth)/2)+'px';bar.style.top='8px';return;}
    const l=region.x*scaleX,t=region.y*scaleY,r=(region.x+region.w)*scaleX,b=(region.y+region.h)*scaleY;
    bar.style.left=clamp(r-bar.offsetWidth,3,W-bar.offsetWidth-3)+'px';
    bar.style.top=clamp(b+5+bar.offsetHeight<=H?b+5:t-bar.offsetHeight-5>=0?t-bar.offsetHeight-5:b-bar.offsetHeight-5,2,H-bar.offsetHeight-2)+'px';
    side.style.left=clamp(r+5+side.offsetWidth<=W?r+5:l-side.offsetWidth-5>=0?l-side.offsetWidth-5:r-side.offsetWidth-5,2,W-side.offsetWidth-2)+'px';
    side.style.top=clamp(t,2,H-side.offsetHeight-2)+'px';
  }
  function controls(){
    let main,options='';
    if(!region)main=tool('smart','smart',smart)+tool('manual','manual',!smart)+tool('full')+tool('cancel');
    else{
      main=tool('move','move',mode==='move')+choices.map((k,i)=>tool(k,k,groups[i].includes(mode),groups[i].length>1)).join('')+
        '<i></i>'+tool('undo','undo',false,false,!undo.length)+tool('redo','redo',false,false,!redo.length)+'<i></i>'+
        (mediaEdit?(mediaEdit==='crop'?tool('crop'):'')+tool('done')+tool('cancel'): (pinEdit?tool('crop')+tool('done'):tool('longshot','longshot',intent==='longshot')+tool('record','record',intent==='record')+tool('pin'))+tool('save')+tool('cancel')+tool('copy'));
      if(mediaEdit==='crop')main=tool('crop','crop',true)+tool('done')+tool('cancel');
      options=parameters(mode,style);
    }
    bar.innerHTML='<div class="cap-strip">'+main+'</div>'+(options?'<div class="cap-strip cap-options">'+options+'</div>':'');
    side.hidden=!region||pinEdit;side.innerHTML=side.hidden?'':tool('move','move-region')+tool('full')+tool('reset');position();
  }
  function refresh(){paint();controls();}
  function output(){
    inkLayer();const r=region,out=makeCanvas(Math.max(1,r.w),Math.max(1,r.h)),c=out.getContext('2d');
    c.drawImage(frame,-r.x,-r.y);c.drawImage(ink,-r.x,-r.y);return out;
  }
  function endText(apply=true){
    if(!input)return;const {el,p,old}=input;input=null;
    if(apply&&el.value.trim()){remember();const next={type:'text',points:[p],style:{...style},text:el.value};if(old)marks[marks.indexOf(old)]=next;else marks.push(next);}
    el.remove();refresh();
  }
  function textAt(p,old){
    endText();
    if(old)Object.assign(style,old.style);
    const el=document.createElement('textarea');el.className='cap-text';el.setAttribute('aria-label',t('标注文字','Annotation text'));el.rows=1;
    el.style.cssText='left:'+p.x/frame.width*100+'%;top:'+p.y/frame.height*100+'%;font:'+textFont(style)+';color:'+style.color+';';
    el.value=old?.text||'';input={el,p,old};layer.append(el);
    const fit=()=>{ctx.font=textFont(style);el.style.width=Math.min(layer.clientWidth-el.offsetLeft,Math.max(28,...el.value.split('\n').map(s=>ctx.measureText(s).width+12)))+'px';el.style.height=Math.max(1,el.value.split('\n').length)*style.size*1.25+6+'px';};el.oninput=fit;fit();el.focus();el.onblur=()=>endText();
    el.onkeydown=e=>{if(e.key==='Escape'||(e.key==='Enter'&&!e.shiftKey&&!e.isComposing)){e.preventDefault();e.stopPropagation();endText(e.key!=='Escape');canvas.focus();}};
  }
  function pick(p){
    for(let i=marks.length-1;i>=0;i--){
      if(marks[i].type==='erase')continue;
      brush.clearRect(0,0,ink.width,ink.height);renderMark(brush,marks[i],pixelSource);
      const sample=brush.getImageData(clamp(p.x-2,0,ink.width-5),clamp(p.y-2,0,ink.height-5),5,5).data;
      if(sample.some((v,j)=>j%4===3&&v>0))return marks[i];
    }
  }
  function reset(){marks=[];undo.length=redo.length=0;region=null;mode='select';gesture=null;target=null;refresh();}
  async function command(key){
    if(!key)return;endText();
    if(key==='cancel'){close();return;}
    if(key==='reset'){reset();return;}
    if(key==='full'){region=full();mode='move';}
    else if(key==='smart'||key==='manual'){smart=key==='smart';target=null;}
    else if(key==='undo'&&undo.length){redo.push(marks);marks=undo.pop();}
    else if(key==='redo'&&redo.length){undo.push(marks);marks=redo.pop();}
    else if(['pin','save','copy','longshot','record','done'].includes(key)&&region){
      const out=output(),where={...region};
      try{let doc;if(key==='pin'||key==='done'){const base=makeCanvas(out.width,out.height);base.getContext('2d').drawImage(frame,-where.x,-where.y);doc={base,marks:marks.map(m=>({...structuredClone(m),points:m.points.map(p=>({x:p.x-where.x,y:p.y-where.y}))}))};}await onOutput(key,out,where,ink,doc);close();}catch{toast(t('操作未完成，请重试','Action failed. Try again.'));}return;
    }else if(key==='crop'){mode='crop';}
    else if(key==='move-region'||key==='move'){mode='move';}
    else{const n=groups.findIndex(g=>g.includes(key));if(n>=0){mode=key;choices[n]=key;}}
    canvas.dataset.pointerRole=mode==='text'?'text':['move'].includes(mode)?'move':['pen','highlight','erase'].includes(mode)?'pen':'cross';
    refresh();canvas.focus({preventScroll:true});
  }
  function click(e){const b=e.target.closest('button');if(!b)return;
    if(b.dataset.color){style.color=b.dataset.color;controls();return;}
    if(b.dataset.toggle){style[b.dataset.toggle]=!style[b.dataset.toggle];controls();return;}command(b.dataset.cmd);}
  bar.onclick=side.onclick=click;
  let editMenu;const dismissMenu=()=>{editMenu?.remove();editMenu=null;};
  bar.onchange=e=>{const k=e.target.dataset.opt;if(!k)return;const v=e.target.value;
    if(k==='dash'||k==='fill')style[k]=k==='dash'?v==='1':e.target.checked;else if(k==='color'||k==='font')style[k]=v;else style[k]=Math.max(k==='number'?1:0,Math.min(9999,Number(v)||0));};
  function hitTarget(p,alt){return targets.filter(r=>p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.w&&p.y<r.y+r.h&&(!alt||r.window)).sort((a,b)=>a.w*a.h-b.w*b.h)[0]||full();}
  canvas.onpointerdown=e=>{
    if(e.button!==0)return;const p=point(e);endText();canvas.setPointerCapture(e.pointerId);
    if(!region||mode==='crop'){gesture={kind:'select',p,rect:null,alt:e.altKey};return;}
    const handle=!pinEdit&&handles().find(h=>Math.max(Math.abs(h.x-p.x),Math.abs(h.y-p.y))<=5);
    if(handle){gesture={kind:'size',p,old:{...region},handle};return;}
    if(p.x<region.x||p.y<region.y||p.x>region.x+region.w||p.y>region.y+region.h)return;
    if(mode==='move'){
      const mark=pick(p);if(mark){remember();gesture={kind:'mark',p,mark,points:structuredClone(mark.points)};}
      else if(!pinEdit)gesture={kind:'pan',p,old:{...region}};return;
    }
    if(mode==='text'){const old=pick(p);if(old?.type==='text'){textAt(old.points[0],old);}else textAt(p);return;}
    remember();const mark={type:mode,points:[p],style:{...style}};marks.push(mark);gesture={kind:'draw',mark};
    if(mode==='number')style.number=Math.min(9999,style.number+1);paint();
  };
  canvas.onpointermove=e=>{
    const p=point(e);if(!gesture){const h=region&&!pinEdit&&handles().find(h=>Math.max(Math.abs(h.x-p.x),Math.abs(h.y-p.y))<=5);
      canvas.dataset.pointerRole=h?(h.u===.5?'vertical':h.v===.5?'horizontal':h.u===h.v?'diagonal1':'diagonal2'):mode==='text'?'text':mode==='move'?'move':['pen','erase','highlight'].includes(mode)?'pen':'cross';
      if(!region&&smart){target=hitTarget(p,e.altKey);paint();}return;}
    const g=gesture,dx=p.x-(g.p?.x||0),dy=p.y-(g.p?.y||0);
    if(g.kind==='select')g.rect=bounds(g.p,p);
    if(g.kind==='pan'){region.x=clamp(g.old.x+dx,0,canvas.width-region.w);region.y=clamp(g.old.y+dy,0,canvas.height-region.h);}
    if(g.kind==='size'){const {x,y,w,h}=g.old,{u,v}=g.handle;const L=u===0?clamp(p.x,0,x+w-2):x,R=u===1?clamp(p.x,x+2,canvas.width):x+w,T=v===0?clamp(p.y,0,y+h-2):y,B=v===1?clamp(p.y,y+2,canvas.height):y+h;region={x:L,y:T,w:R-L,h:B-T};}
    if(g.kind==='mark')g.mark.points=g.points.map(q=>({x:q.x+dx,y:q.y+dy}));
    if(g.kind==='draw'&&g.mark.type!=='number')g.mark.points.push(p);
    paint();if(region)position();
  };
  canvas.onpointerup=e=>{
    if(!gesture)return;canvas.onpointermove(e);const g=gesture;gesture=null;
    if(g.kind==='select'){const r=g.rect&&g.rect.w>2&&g.rect.h>2?g.rect:smart&&!pinEdit?hitTarget(g.p,g.alt):null;
      if(r){region={...r};mode='move';}}
    refresh();
  };
  canvas.onpointercancel=()=>{gesture=null;refresh();};
  canvas.ondblclick=e=>{const mark=pick(point(e));if(mark?.type==='text'){mode='text';textAt(mark.points[0],mark);}};
  layer.oncontextmenu=e=>{if(e.target.closest('textarea'))return;e.preventDefault();dismissMenu();
    if(pinEdit){editMenu=document.createElement('div');editMenu.className='pin-context';const r=layer.getBoundingClientRect();
      editMenu.innerHTML=[['done',t('完成编辑','Finish editing')],['crop',t('裁剪','Crop')],['cancel',t('取消编辑','Cancel editing')]].map(([k,v])=>'<button data-edit="'+k+'">'+v+'</button>').join('');layer.append(editMenu);editMenu.style.left=clamp(e.clientX-r.left,0,layer.clientWidth-editMenu.offsetWidth)+'px';editMenu.style.top=clamp(e.clientY-r.top,0,layer.clientHeight-editMenu.offsetHeight)+'px';
      editMenu.onclick=e=>{const k=e.target.dataset.edit;dismissMenu();command(k);};editMenu.querySelector('button').focus();
    }else if(!region&&!gesture)close();else reset();
  };
  layer.onpointerdown=e=>{if(editMenu&&!editMenu.contains(e.target))dismissMenu();};
  canvas.onwheel=e=>{
    if(!region)return;e.preventDefault();const hit=pick(point(e));const target=mode==='erase'?null:hit;
    const type=target?.type||mode,key=type==='erase'?'eraser':['text','number'].includes(type)?'size':type==='mosaic'?'block':'width';
    if(target)remember();const s=target?.style||style,values=limits[key];const larger=e.deltaY<0;s[key]=(larger?values.find(v=>v>s[key]):values.findLast(v=>v<s[key]))??s[key];refresh();
  };
  function keyboard(e){
    if(input||e.target.matches('input,select,textarea'))return;
    const k=e.key.toLowerCase(), shortcuts={z:e.shiftKey?'redo':'undo',y:'redo',s:'save',c:'copy'};
    const key=e.ctrlKey?shortcuts[k]:k==='escape'?'cancel':k==='enter'&&region?(pinEdit?'done':intent||'copy'):null;
    if(key){e.preventDefault();e.stopImmediatePropagation();command(key);}
  }
  function close(){if(layer.hidden)return;document.removeEventListener('keydown',keyboard,true);window.removeEventListener('resize',position);layer.oncontextmenu=layer.onpointerdown=null;layer.hidden=true;layer.replaceChildren();onClose();}
  document.addEventListener('keydown',keyboard,true);window.addEventListener('resize',position);refresh();canvas.focus({preventScroll:true});
  return close;
}
