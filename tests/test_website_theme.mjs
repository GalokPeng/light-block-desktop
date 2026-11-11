import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const script = readFileSync(new URL('../site/public/theme.js', import.meta.url), 'utf8');
function page({ dark = false, saved = null, blocked = false } = {}) {
  const events = new Map();
  const element = () => ({ dataset: {}, attrs: {}, handlers: {}, setAttribute(k,v) { this.attrs[k]=v; },
    getAttribute(k) { return this.attrs[k] ?? null; }, addEventListener(k,fn) { this.handlers[k]=fn; } });
  const toggle=element(), system=element(), image=element(), link={};
  image.dataset.themeImage='/media/wallpaper-en'; image.closest=()=>link;
  const root={lang:'en',dataset:{},style:{}};
  const storage=new Map(saved ? [['lbd-theme',saved]] : []);
  const media={matches:dark,addEventListener(_event,fn){this.change=fn;}};
  const doc={documentElement:root,querySelectorAll(selector){return {
    '[data-theme-toggle]':[toggle],'[data-theme-system]':[system],'[data-theme-image]':[image],
  }[selector] || [];},addEventListener(name,fn){events.set(name,fn);}};
  const localStorage={getItem(k){if(blocked)throw new Error('blocked');return storage.get(k)||null;},
    setItem(k,v){if(blocked)throw new Error('blocked');storage.set(k,v);},removeItem(k){if(blocked)throw new Error('blocked');storage.delete(k);}};
  vm.runInNewContext(script,{document:doc,window:{matchMedia:()=>media},localStorage});
  events.get('DOMContentLoaded')();
  return {root,toggle,system,image,link,storage,media};
}

test('system theme is applied before body paint and follows OS changes',()=>{
  const p=page({dark:true});
  assert.equal(p.root.dataset.theme,'dark');
  assert.equal(p.root.style.colorScheme,'dark');
  assert.equal(p.image.src,'/media/wallpaper-en-dark.webp');
  assert.equal(p.link.href,p.image.src);
  assert.equal(p.system.attrs['aria-pressed'],'true');
  p.media.matches=false;p.media.change();
  assert.equal(p.root.dataset.theme,'light');
});
test('manual toggle persists, updates accessible labels and overrides OS changes',()=>{
  const p=page();p.toggle.handlers.click();
  assert.equal(p.root.dataset.theme,'dark');
  assert.equal(p.storage.get('lbd-theme'),'dark');
  assert.equal(p.toggle.attrs['aria-label'],'Switch to light theme');
  assert.equal(p.system.attrs['aria-pressed'],'false');
  p.media.matches=false;p.media.change();
  assert.equal(p.root.dataset.theme,'dark');
  assert.equal(page({saved:'dark'}).root.dataset.theme,'dark');
});
test('system button clears preference; blocked storage still permits switching',()=>{
  const p=page({saved:'dark'});p.system.handlers.click();
  assert.equal(p.storage.has('lbd-theme'),false);
  assert.equal(p.root.dataset.theme,'light');
  const blocked=page({blocked:true});blocked.toggle.handlers.click();
  assert.equal(blocked.root.dataset.theme,'dark');
});
