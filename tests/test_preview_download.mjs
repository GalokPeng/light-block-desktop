import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { setupUrl } from '../site/public/downloads.mjs';
test('current and archived downloads are separate', async () => {
  for (const path of ['index.html','en/index.html']) {
    const page=await readFile(new URL('../site/public/'+path,import.meta.url),'utf8');
    assert.ok(page.includes('2.0.0 · MIT'));
    assert.ok(page.includes('data-setup-download'));
    assert.ok(page.includes('data-archive-download'));
    assert.ok(page.includes('/download/v1.0.0/Qingkuai-Setup-x64.exe'));
    assert.ok(!page.includes('data-preview-download'));
  }
  assert.ok(setupUrl('gh-proxy','v1.0.0').endsWith('/download/v1.0.0/Qingkuai-Setup-x64.exe'));
  assert.ok(setupUrl('github').includes('/latest/download/'));
});
test('preview bookmarks redirect to stable pages', async () => {
  const config=JSON.parse(await readFile(new URL('../vercel.json',import.meta.url),'utf8'));
  for(const path of ['/preview/','/en/preview/']) assert.ok(config.redirects.some(r=>r.source===path&&r.permanent));
});
