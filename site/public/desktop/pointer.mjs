// Same RGBA projection and shadow exception as src/cursor_art.cpp::tint_cur.
function tint(canvas, color) {
  if (color === '#d9f2ed') return;
  const ctx = canvas.getContext('2d');
  const image = ctx.getImageData(0, 0, canvas.width, canvas.height);
  const rgb = color.match(/\w\w/g).map(c => parseInt(c, 16));
  const edge = rgb[0]*299 + rgb[1]*587 + rgb[2]*114 > 125000 ? [21,33,44] : [243,249,255];
  const original = [21,33,44], vector = [196,209,193];
  const norm = vector.reduce((sum, c) => sum+c*c, 0);
  const d = image.data;
  for (let i=0; i<d.length; i+=4) {
    if (!d[i+3] || (d[i+3]<=120 && Math.abs(d[i]-15)<3 && Math.abs(d[i+1]-24)<3 && Math.abs(d[i+2]-36)<3)) continue;
    const mix = Math.min(1, Math.max(0, vector.reduce((s,c,j)=>s+(d[i+j]-original[j])*c,0)/norm));
    for(let j=0;j<3;j++) d[i+j]=Math.round(edge[j]*(1-mix)+rgb[j]*mix);
  }
  ctx.putImageData(image,0,0);
}
const sheets=new Map();
function sheetFor(theme) {
  if(!sheets.has(theme)){
    const image=new Image();image.src='./art/'+theme+'.png';
    sheets.set(theme,Promise.all([image.decode().then(()=>image),fetch('./art/'+theme+'.json').then(r=>r.json())])
      .catch(error=>{sheets.delete(theme);throw error;}));
  }
  return sheets.get(theme);
}
function roleAt(target){
  const explicit=target.closest('[data-pointer-role]');if(explicit)return explicit.dataset.pointerRole;
  if(target.closest(':disabled,[aria-disabled=true]'))return 'unavailable';
  if(target.closest('textarea,input:not([type]),input[type=text],input[type=search],input[type=number]'))return 'text';
  if(target.closest('a,button,summary,select,label,input'))return 'hand';
  if(target.closest('.titlebar,.note-head,.result-head'))return 'move';
  return 'normal';
}
export function desktopPointer(desk,state){
  let generation=0, key='', images={}, role='normal', lastPoint=null, serial=0,timer,active=false;
  const motion=matchMedia('(prefers-reduced-motion: reduce)');
  function useRole(){
    clearInterval(timer);
    const framesForRole=images[role]||images.normal;
    if(!framesForRole)return;
    let n=0;
    const apply=()=>desk.style.setProperty('--desk-pointer',framesForRole[n++%framesForRole.length].cursor);
    apply();
    if(active&&framesForRole.length>1&&!motion.matches)timer=setInterval(apply,83);
  }
  async function refresh(){
    const name=state.cursor==='prism'?'outline':state.cursor;
    desk.style.setProperty('--particle-color',state.particleColor);
    const next=[name,state.size,state.color].join('.');
    if(next===key)return;
    key=next;const epoch=++generation;clearInterval(timer);
    if(name==='system'){desk.classList.remove('native-pointer');images={};return;}
    try{
      const [image,metadata]=await sheetFor(name);if(epoch!==generation)return;
      const atlas=document.createElement('canvas');atlas.width=image.width;atlas.height=image.height;
      atlas.getContext('2d').drawImage(image,0,0);tint(atlas,state.color);
      const size=Math.round(32*state.size/100), map={};
      for(const [role,[start,count,x,y]] of Object.entries(metadata)){
        const hx=Math.round(x*size/64),hy=Math.round(y*size/64);
        map[role]=Array.from({length:count},(_,index)=>{
          const c=document.createElement('canvas');c.width=c.height=size;
          const ctx=c.getContext('2d');ctx.imageSmoothingEnabled=name!=='pixel';
          const n=start+index;ctx.drawImage(atlas,n%8*64,Math.floor(n/8)*64,64,64,0,0,size,size);
          const url='url("'+c.toDataURL()+'")';return {url,hx,hy,size,cursor:url+' '+hx+' '+hy+', auto'};
        });
      }
      images=map;desk.classList.add('native-pointer');useRole();
    }catch{desk.classList.remove('native-pointer');key='';}
  }
  function clear(){
    active=false;clearInterval(timer);lastPoint=null;desk.querySelectorAll('.cursor-speck').forEach(p=>p.remove());
  }
  desk.addEventListener('pointerover',e=>{if(e.pointerType==='touch')return;active=true;const next=roleAt(e.target);if(role!==next||!timer){role=next;useRole();}});
  desk.addEventListener('pointerleave',clear);window.addEventListener('blur',clear);
  desk.addEventListener('pointermove',e=>{
    if(e.pointerType==='touch')return;
    const nextRole=roleAt(e.target);if(nextRole!==role){role=nextRole;useRole();}
    if(motion.matches||!state.particles)return;
    const now=performance.now(),dx=lastPoint?e.clientX-lastPoint.x:0,dy=lastPoint?e.clientY-lastPoint.y:0;
    if(lastPoint&&now-lastPoint.t<16)return;
    const dt=Math.max(.016,Math.min(.05,(now-(lastPoint?.t||now))/1000));
    lastPoint={x:e.clientX,y:e.clientY,t:now};
    const style=state.effect;
    const amount=style==='fall'?Math.max(1,Math.min(2,Math.round(Math.hypot(dx,dy)/14))):1;
    for(let i=0;i<amount;i++){
      const p=document.createElement('i');p.className='cursor-speck';p.setAttribute('data-html2canvas-ignore','');
      p.style.left=(e.clientX-dx*(1-(i+1)/amount))+'px';p.style.top=(e.clientY-dy*(1-(i+1)/amount))+'px';
      const phase=++serial*2.39996323;
      let transforms=[{opacity:.8},{opacity:0}],life=420;
      if(style==='glow'){
        desk.querySelector('.cursor-speck.contour')?.remove();p.classList.add('contour');
        const sprite=images[role]?.[0]||images.normal?.[0];
        if(sprite){p.style.width=p.style.height=sprite.size+'px';p.style.marginLeft=-sprite.hx+'px';p.style.marginTop=-sprite.hy+'px';p.style.backgroundImage=sprite.url;p.style.backgroundSize='contain';}
        life=1050;transforms=[{opacity:.7},{opacity:.2,offset:.45},{opacity:0}];
      }else if(style==='fall'){
        const vx=Math.min(42,Math.max(-42,dx/dt*.07))+Math.sin(phase)*10,vy=Math.min(15,Math.max(-25,dy/dt*.04))-10;
        const size=1.1+.6*(.5+.5*Math.sin(phase));p.style.width=p.style.height=size+'px';life=1250;
        transforms=Array.from({length:9},(_,n)=>{const s=n/8*1.25;return {transform:'translate('+vx*s+'px,'+(vy*s+44*s*s)+'px)',opacity:1-n/8};});
      }
      desk.append(p);const a=p.animate(transforms,{duration:life,fill:'both'});a.onfinish=()=>p.remove();
    }
  });
  return refresh;
}
