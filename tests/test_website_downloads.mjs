import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { bindDownloads, defaultSource, setupUrl, setupName } from '../site/public/downloads.mjs';

const version = 'v9.8.7';
const binary = Buffer.from('MZ verified test setup');
const digest = createHash('sha256').update(binary).digest('hex');
let counter = 0;

async function apiTest(path, { method = 'GET', corrupt = false } = {}) {
  const requests = [];
  const original = globalThis.fetch;
  globalThis.fetch = async (input, options = {}) => {
    const url = String(input);
    requests.push(url);
    if (url.endsWith('/latest/download/' + setupName)) {
      return new Response(null, { status: 302, headers: { Location: setupUrl('github', version) } });
    }
    if (url.endsWith('SHA256SUMS.txt')) return new Response(`${digest}  ${setupName}\n`);
    if (url.endsWith('/' + setupName)) return new Response(corrupt && url.startsWith('https://github.com/') ? 'broken' : binary);
    throw new Error('Unexpected request ' + url);
  };
  try {
    const { default: handler } = await import('../api/setup.mjs?test=' + counter++);
    return { response: await handler.fetch(new Request('https://example.test/download/' + path, { method })), requests };
  } finally { globalThis.fetch = original; }
}

test('language defaults and versioned URLs stay on known sources', () => {
  assert.equal(defaultSource('en'), 'github');
  assert.equal(defaultSource('zh-CN'), 'gh-proxy');
  assert.equal(defaultSource('zh-TW'), 'gh-proxy');
  assert.match(setupUrl('github'), /^https:\/\/github.com\/.+\/latest\/download\/Qingkuai-Setup-x64.exe$/);
  assert.match(setupUrl('ghfast', version), /^https:\/\/ghfast.top\/https:\/\/github.com\/.+\/v9.8.7\//);
  assert.throws(() => setupUrl('https://evil.test'));
  assert.throws(() => setupUrl('github', '../other'));
});

test('HTML defaults work with JavaScript disabled', async () => {
  const zh = await readFile(new URL('../site/public/index.html', import.meta.url), 'utf8');
  const en = await readFile(new URL('../site/public/en/index.html', import.meta.url), 'utf8');
  assert.equal([...zh.matchAll(/data-setup-download href="([^"]+)"/g)].length, 2);
  for (const [, url] of zh.matchAll(/data-setup-download href="([^"]+)"/g)) assert.equal(url, setupUrl('gh-proxy'));
  for (const [, url] of en.matchAll(/data-setup-download href="([^"]+)"/g)) assert.equal(url, setupUrl('github'));
  assert.match(zh, /value="gh-proxy" selected/);
  assert.match(en, /value="github" selected/);
});

test('switching sources updates every download link, with language-specific storage', async () => {
  const links = [{}, {}];
  const control = { addEventListener(_event, handler) { this.change = handler; } };
  const values = new Map();
  const storage = { getItem: key => values.get(key), setItem: (key, value) => values.set(key, value) };
  const doc = { documentElement: { lang: 'zh-CN' }, querySelectorAll: selector => selector.includes('source') ? [control] : links };
  await bindDownloads(doc, storage, async () => ({ version, name: setupName, url: setupUrl('github', version) }));
  assert.equal(links[0].href, setupUrl('gh-proxy', version));
  control.value = 'ghfast';
  control.change();
  assert.ok(links.every(link => link.href === setupUrl('ghfast', version)));
  assert.equal(values.get('qingkuai-download-zh'), 'ghfast');
  doc.documentElement.lang = 'en';
  bindDownloads(doc, storage);
  assert.equal(links[0].href, setupUrl('github'));
});

test('offline metadata and blocked browser storage retain working default links', async () => {
  const link = {};
  const doc = { documentElement: { lang: 'en' }, querySelectorAll: selector => selector.includes('source') ? [] : [link] };
  await bindDownloads(doc, { getItem() { throw Error('blocked'); } }, async () => { throw Error('offline'); });
  assert.equal(link.href, setupUrl('github'));
});

test('metadata and HEAD expose official hashes without downloading Setup', async () => {
  const { response, requests } = await apiTest('?format=json');
  const body = await response.json();
  assert.equal(body.sha256, digest);
  assert.equal(body.version, version);
  assert.equal(body.url, setupUrl('github', version));
  assert.equal(body.sources.length, 3);
  assert.equal(requests.length, 2);
  const head = await apiTest('', { method: 'HEAD' });
  assert.equal(head.response.headers.get('X-Qingkuai-SHA256'), digest);
  assert.equal(await head.response.text(), '');
});

test('explicit language routes redirect to versioned sources without proxying Setup', async () => {
  for (const [query, source] of [['?lang=en', 'github'], ['?lang=zh', 'gh-proxy'], ['?lang=zh&source=ghfast', 'ghfast']]) {
    const { response, requests } = await apiTest(query);
    assert.equal(response.status, 302);
    assert.equal(response.headers.get('Location'), setupUrl(source, version));
    assert.equal(requests.length, 2);
  }
});

test('legacy downloads still verify the full file and reject a corrupt upstream', async () => {
  const { response, requests } = await apiTest('', { corrupt: true });
  assert.equal(response.status, 200);
  assert.deepEqual(Buffer.from(await response.arrayBuffer()), binary);
  assert.ok(requests.some(url => url.startsWith('https://gh-proxy.com/')));
});

test('unknown sources and formats are rejected and checksum text stays separate from Setup', async () => {
  assert.equal((await apiTest('?source=arbitrary')).response.status, 400);
  assert.equal((await apiTest('?format=arbitrary')).response.status, 400);
  const { response } = await apiTest('?format=checksum');
  assert.equal(await response.text(), `${digest}  ${setupName}\n`);
});
