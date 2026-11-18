// Embed the reviewed catalog and cursor sprites during website deployment so that
// entering the virtual monitor and opening its library do not need extra round trips.
import {readFileSync, writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const desktop=fileURLToPath(new URL('../site/public/desktop/',import.meta.url));
const read=(name,encoding)=>readFileSync(resolve(desktop,name),encoding);
const themes=['stream','outline','pixel'];
const cursors=Object.fromEntries(themes.map(theme=>[theme,{
  url:'data:image/png;base64,'+read('art/'+theme+'.png').toString('base64'),
  roles:JSON.parse(read('art/'+theme+'.json','utf8'))
}]));
const data=JSON.stringify({catalog:JSON.parse(read('catalog.json','utf8')),cursors}).replaceAll('<','\\u003c');
const template=read('index.html','utf8');
const placeholder='<!-- deployment-assets -->';
if(!template.includes(placeholder))throw Error('Desktop template has no asset slot');
const html=template.replace(/<script type="application\/json" id="desktop-assets">[\s\S]*?<\/script>/,'')
  .replace(placeholder,()=>placeholder+'<script type="application/json" id="desktop-assets">'+data+'</script>');
writeFileSync(process.argv[2]||resolve(desktop,'index.html'),html);
