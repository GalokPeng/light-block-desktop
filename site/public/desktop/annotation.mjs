import { t } from './ui.mjs';
import {paths} from './art/capture-icons.mjs';
export const names={
 smart:t('智能窗口','Smart window selection'),manual:t('手动框选','Manual region'),full:t('当前屏幕','Current screen'),cancel:t('关闭 · Esc','Close · Esc'),
 move:t('选择与移动','Select and move'),rect:t('矩形','Rectangle'),ellipse:t('圆形','Ellipse'),pen:t('画笔','Pen'),highlight:t('高亮笔','Highlighter'),
 line:t('直线','Line'),arrow:t('箭头','Arrow'),text:t('文字','Text'),number:t('序号','Number'),mosaic:t('框选马赛克','Mosaic region'),
 mosaic_brush:t('涂鸦马赛克','Mosaic brush'),erase:t('橡皮擦','Eraser'),undo:t('撤销 · Ctrl+Z','Undo · Ctrl+Z'),redo:t('重做 · Ctrl+Y','Redo · Ctrl+Y'),
 longshot:t('滚动长截图','Scrolling capture'),record:t('开始录屏','Record screen'),pin:t('贴到屏幕','Pin to screen'),save:t('保存 PNG','Save PNG'),
 copy:t('复制 · Ctrl+C','Copy · Ctrl+C'),reset:t('重新选区','Reselect'),crop:t('裁剪图片','Crop image'),done:t('完成编辑','Finish editing')
};
export function tool(key,action=key,on=false,group=false,disabled=false){
 const path=paths[key==='manual'?'rect':key];
 return '<button type="button" title="'+names[key]+'" aria-label="'+names[key]+'" data-cmd="'+action+'" aria-pressed="'+on+'" '+(disabled?'disabled':'')+'><svg viewBox="0 0 24 24" aria-hidden="true">'+path+'</svg>'+(group?'⌄':'')+'</button>';
}
export const groups=[['rect','ellipse'],['pen','highlight'],['line','arrow'],['text'],['number'],['mosaic','mosaic_brush'],['erase']];
export const defaults={color:'#ff6868',width:3,radius:0,fill:false,dash:false,font:'Microsoft YaHei UI',size:22,number:1,block:12,bold:false,italic:false,underline:false,eraser:16};
export const limits={width:[1,2,3,4,6,8,10,12,16,20,24,32,36,48,64,80],radius:[0,4,8,12,16,24,32,48,64,80],block:[6,8,10,12,14,16,24,32,48,64],eraser:[1,2,4,8,12,16,24,32,48,64,80,120,160],size:[8,10,12,14,16,18,20,22,24,28,32,36,40,48,56,64,80,96,120,160]};
function select(key,title,values,value){
 return '<label>'+title+' <select data-opt="'+key+'" aria-label="'+title+'">'+values.map(v=>'<option value="'+v+'" '+(v===value?'selected':'')+'>'+v+'</option>').join('')+'</select></label>';
}
export function parameters(mode,s){
 const index=groups.findIndex(g=>g.includes(mode));if(index<0)return '';
 const g=groups[index];let controls=g.length>1?g.map(k=>tool(k,k,mode===k)).join(''):'';
 const width=select('width',t('粗细','Width'),limits.width,s.width);
 if(index===0)controls+='<label><input type="checkbox" data-opt="fill" '+(s.fill?'checked':'')+'>'+t('填充','Fill')+'</label>';
 if(index===0||index===2)controls+='<select data-opt="dash" aria-label="'+t('线型','Line style')+'"><option value="0">'+t('实线','Solid')+'</option><option value="1" '+(s.dash?'selected':'')+'>'+t('虚线','Dashed')+'</option></select>';
 if([0,1,2].includes(index)||mode==='mosaic_brush')controls+=width;
 if(mode==='rect'||mode==='mosaic')controls+=select('radius',t('圆角','Radius'),limits.radius,s.radius);
 if(index===5)return controls+select('block',t('像素块','Pixel size'),limits.block,s.block);
 if(index===6)return select('eraser',t('橡皮大小','Eraser size'),limits.eraser,s.eraser);
 if(mode==='text'){
   controls+=select('font',t('字体','Font'),['Microsoft YaHei UI','Segoe UI','Arial','Georgia','monospace'],s.font);
   controls+=select('size',t('字号','Size'),limits.size,s.size);
   controls+=['bold','italic','underline'].map((v,i)=>'<button data-toggle="'+v+'" aria-label="'+[t('加粗','Bold'),t('斜体','Italic'),t('下划线','Underline')][i]+'" aria-pressed="'+s[v]+'">'+'BIU'[i]+'</button>').join('');
 }
 if(mode==='number')controls+=select('size',t('大小','Size'),limits.size,s.size)+'<input type="number" aria-label="'+t('起始序号','Start number')+'" min="1" max="9999" data-opt="number" value="'+s.number+'">';
 return controls+'<input type="color" aria-label="'+t('自定义颜色','Custom color')+'" data-opt="color" value="'+s.color+'">'+['#ff6868','#ffd382','#a6e4ca','#68adff','#151515','#ffffff'].map(c=>'<button class="swatch" style="background:'+c+'" data-color="'+c+'" aria-label="'+c+'"></button>').join('');
}
export const bounds=(p,q)=>({x:Math.min(p.x,q.x),y:Math.min(p.y,q.y),w:Math.abs(p.x-q.x),h:Math.abs(p.y-q.y)});
export function makeCanvas(w,h){const c=document.createElement('canvas');c.width=w;c.height=h;return c;}
export function textFont(s){return (s.italic?'italic ':'')+(s.bold?'bold ':'')+s.size+'px "'+s.font+'"';}
export function renderMark(ctx,mark,pixelSource){
 const {type,style:s,points}=mark,a=points[0],b=points.at(-1),r=bounds(a,b);
 ctx.save();ctx.strokeStyle=ctx.fillStyle=s.color;ctx.lineWidth=s.width;ctx.lineCap=ctx.lineJoin='round';
 const brush=()=>{ctx.beginPath();ctx.moveTo(a.x,a.y);for(const p of points)ctx.lineTo(p.x,p.y);if(points.length===1)ctx.lineTo(a.x+.02,a.y);ctx.stroke();};
 if(type==='erase'){ctx.globalCompositeOperation='destination-out';ctx.lineWidth=s.eraser;brush();}
 else if(type==='mosaic'||type==='mosaic_brush'){
   if(type==='mosaic'){ctx.beginPath();ctx.roundRect(r.x,r.y,r.w,r.h,Math.min(s.radius,r.w/2,r.h/2));ctx.clip();}
   else{const path=new Path2D();const radius=Math.max(1,s.width/2);let prev=a;
     for(const p of points){const steps=Math.max(1,Math.ceil(Math.hypot(p.x-prev.x,p.y-prev.y)/radius));for(let i=1;i<=steps;i++){const x=prev.x+(p.x-prev.x)*i/steps,y=prev.y+(p.y-prev.y)*i/steps;path.moveTo(x+radius,y);path.arc(x,y,radius,0,Math.PI*2);}prev=p;}ctx.clip(path);}
   ctx.imageSmoothingEnabled=false;ctx.drawImage(pixelSource(s.block),0,0,ctx.canvas.width,ctx.canvas.height);
 }else if(type==='rect'||type==='ellipse'){
   if(s.dash)ctx.setLineDash([s.width*3,s.width*2]);ctx.beginPath();
   if(type==='rect')ctx.roundRect(r.x,r.y,r.w,r.h,Math.min(s.radius,r.w/2,r.h/2));else ctx.ellipse(r.x+r.w/2,r.y+r.h/2,r.w/2,r.h/2,0,0,Math.PI*2);
   if(s.fill)ctx.fill();ctx.stroke();
 }else if(type==='line'||type==='arrow'){
   if(s.dash)ctx.setLineDash([s.width*3,s.width*2]);ctx.beginPath();ctx.moveTo(a.x,a.y);ctx.lineTo(b.x,b.y);ctx.stroke();ctx.setLineDash([]);
   if(type==='arrow'){const theta=Math.atan2(b.y-a.y,b.x-a.x),len=Math.min(Math.hypot(b.x-a.x,b.y-a.y),s.width*4+8);for(const d of [-.55,.55]){ctx.beginPath();ctx.moveTo(b.x,b.y);ctx.lineTo(b.x-len*Math.cos(theta+d),b.y-len*Math.sin(theta+d));ctx.stroke();}}
 }else if(type==='text'){
   ctx.font=textFont(s);ctx.textBaseline='top';(mark.text||'').split('\n').forEach((line,i)=>{const y=a.y+i*s.size*1.2;ctx.fillText(line,a.x,y);if(s.underline)ctx.fillRect(a.x,y+s.size,ctx.measureText(line).width,1);});
 }else if(type==='number'){
   const w=Math.max(s.size,String(s.number).length*s.size*.37+8);ctx.beginPath();ctx.roundRect(a.x-w/2,a.y-s.size/2,w,s.size,s.size/2);ctx.fill();
   const nums=s.color.slice(1).match(/../g).map(c=>parseInt(c,16));ctx.fillStyle=nums[0]*.299+nums[1]*.587+nums[2]*.114>150?'#152028':'white';ctx.font='bold '+Math.round(s.size*.68)+'px "Segoe UI"';ctx.textAlign='center';ctx.textBaseline='middle';ctx.fillText(String(s.number),a.x,a.y);
 }else{if(type==='highlight'){ctx.globalAlpha=.38;ctx.lineWidth=Math.max(8,s.width*3);}brush();}
 ctx.restore();
}
