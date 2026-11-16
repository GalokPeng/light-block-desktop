import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { previewUrl, previewTag, bindPreview } from '../site/public/preview-download.mjs';
import { setupUrl, setupName } from '../site/public/downloads.mjs';
test('preview downloads use only the preview tag and asset', () => {
  assert.match(previewTag, /^preview-v\d+\.\d+\.\d+-beta\.\d+$/);
  for (const source of ['github', 'gh-proxy', 'ghfast']) {
    const url = previewUrl(source);
    assert.ok(url.endsWith('/' + previewTag + '/Qingkuai-Preview-Setup-x64.exe'));
    assert.ok(!url.includes('/latest/'));
    assert.ok(!url.endsWith('/' + setupName));
  }
  assert.throws(() => previewUrl('javascript:alert(1)'));
  assert.match(setupUrl('github'), /\/latest\/download\/Qingkuai-Setup-x64\.exe$/);
});
test('preview pages and download target match the application version', async () => {
  const source = await readFile(new URL('../src/version.hpp', import.meta.url), 'utf8');
  const version = source.match(/^#define LB_VERSION_UTF8 "([^"]+)"/m)?.[1];
  assert.ok(version); assert.equal(previewTag, 'preview-v' + version);
  for (const path of ['preview/index.html', 'en/preview/index.html']) {
    const page = await readFile(new URL('../site/public/' + path, import.meta.url), 'utf8');
    assert.ok(page.includes(previewTag + '/Qingkuai-Preview-Setup-x64.exe'));
    assert.ok(page.includes('Qingkuai-Preview-v' + version + '-SHA256SUMS.txt'));
  }
});
test('preview locale defaults and switching do not touch stable downloads', () => {
  for (const [lang, expected] of [['en', 'github'], ['zh-CN', 'gh-proxy']]) {
    const preview = {}, stable = { href: 'stable-sentinel' }, events = {};
    const control = { value: '', addEventListener: (event, listener) => { events[event] = listener; } };
    bindPreview({ documentElement: { lang }, querySelectorAll: selector => selector === '[data-preview-source]' ? [control] : selector === '[data-preview-download]' ? [preview] : [stable] });
    assert.equal(control.value, expected); assert.equal(preview.href, previewUrl(expected));
    control.value = 'ghfast'; events.change(); assert.equal(preview.href, previewUrl('ghfast'));
    assert.equal(stable.href, 'stable-sentinel');
  }
});
test('stable pages and preview warnings remain distinct', async () => {
  for (const path of ['index.html', 'en/index.html']) {
    const page = await readFile(new URL('../site/public/' + path, import.meta.url), 'utf8');
    assert.ok(page.includes('1.0.0 · MIT')); assert.ok(page.includes('data-setup-download'));
    assert.ok(!page.includes('data-preview-download'));
  }
  for (const path of ['preview/index.html', 'en/preview/index.html']) {
    const page = await readFile(new URL('../site/public/' + path, import.meta.url), 'utf8');
    assert.ok(page.includes('preview-warning')); assert.ok(page.includes('data-preview-download'));
    assert.ok(!page.includes('data-setup-download')); assert.ok(page.includes('1.0.0'));
  }
});
