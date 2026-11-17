import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { bindDownloads, defaultSource, setupUrl, setupName } from '../site/public/downloads.mjs';

const version = 'v9.8.7';
const binary = Buffer.from('MZ verified test setup');
const digest = createHash('sha256').update(binary).digest('hex');
let counter = 0;

async function apiTest(path, { method = 'GET', corrupt = false, headSize = String(binary.length), apiSize = binary.length, apiBlocked = false, headHost = '' } = {}) {
  const requests = [];
  const methods = [];
  const original = globalThis.fetch;
  globalThis.fetch = async (input, options = {}) => {
    const url = String(input);
    requests.push(url);
    methods.push(options.method || 'GET');
    if (url === 'https://api.github.com/repos/GalokPeng/light-block-desktop/releases/latest') {
      if (apiBlocked) return new Response('Rate limited', { status: 403 });
      return Response.json({ tag_name: version, draft: false, prerelease: false, assets: [{
        name: setupName, state: 'uploaded', size: apiSize, digest: 'sha256:' + digest, browser_download_url: setupUrl('github', version),
      }] });
    }
    if (url.endsWith('/latest/download/' + setupName)) {
      return new Response(null, { status: 302, headers: { Location: setupUrl('github', version) } });
    }
    if (url.endsWith('SHA256SUMS.txt')) return new Response(`${digest}  ${setupName}\n`);
    if (url.endsWith('/' + setupName)) {
      if (options.method === 'HEAD') {
        if (headHost) return new Response(null, { status: 302, headers: { Location: headHost + '/setup.exe' } });
        return new Response(null, { headers: headSize === null ? {} : { 'Content-Length': headSize } });
      }
      return new Response(corrupt && url.startsWith('https://github.com/') ? 'broken' : binary);
    }
    throw new Error('Unexpected request ' + url);
  };
  try {
    const { default: handler } = await import('../api/setup.mjs?test=' + counter++);
    return { response: await handler.fetch(new Request('https://example.test/download/' + path, { method })), requests, methods };
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
  const archive = {};
  const control = { addEventListener(_event, handler) { this.change = handler; } };
  const values = new Map();
  const storage = { getItem: key => values.get(key), setItem: (key, value) => values.set(key, value) };
  const doc = { documentElement: { lang: 'zh-CN' }, querySelectorAll: selector => selector === '[data-download-source]' ? [control] : selector === '[data-archive-download]' ? [archive] : selector === '[data-setup-download]' ? links : [] };
  await bindDownloads(doc, storage, async () => ({ version, name: setupName, url: setupUrl('github', version) }));
  assert.equal(links[0].href, setupUrl('gh-proxy', version));
  control.value = 'ghfast';
  control.change();
  assert.ok(links.every(link => link.href === setupUrl('ghfast', version)));
  assert.equal(archive.href, setupUrl('ghfast', 'v1.0.0'));
  assert.equal(values.get('qingkuai-download-zh'), 'ghfast');
  doc.documentElement.lang = 'en';
  bindDownloads(doc, storage);
  assert.equal(links[0].href, setupUrl('github'));
});

test('offline metadata and blocked browser storage retain working default links', async () => {
  const link = {};
  const doc = { documentElement: { lang: 'en' }, querySelectorAll: selector => selector === '[data-setup-download]' ? [link] : [] };
  await bindDownloads(doc, { getItem() { throw Error('blocked'); } }, async () => { throw Error('offline'); });
  assert.equal(link.href, setupUrl('github'));
});

test('metadata and HEAD expose official hashes without downloading Setup', async () => {
  const { response, requests, methods } = await apiTest('?format=json', { apiBlocked: true });
  const body = await response.json();
  assert.equal(body.sha256, digest);
  assert.equal(body.version, version);
  assert.equal(body.url, setupUrl('github', version));
  assert.equal(body.sources.length, 3);
  // This is also the contract required by the already-installed v2.0.0–2.0.3 client.
  assert.equal(body.size, binary.length);
  assert.equal(requests.length, 3);
  assert.ok(requests.every((url, index) => !url.endsWith('.exe') || methods[index] === 'HEAD'));
  const head = await apiTest('', { method: 'HEAD' });
  assert.equal(head.response.headers.get('X-Qingkuai-SHA256'), digest);
  assert.equal(await head.response.text(), '');
});

test('explicit language routes redirect to versioned sources without proxying Setup', async () => {
  for (const [query, source] of [['?lang=en', 'github'], ['?lang=zh', 'gh-proxy'], ['?lang=zh&source=ghfast', 'ghfast']]) {
    const { response, requests } = await apiTest(query);
    assert.equal(response.status, 302);
    assert.equal(response.headers.get('Location'), setupUrl(source, version));
    assert.equal(requests.length, 3);
  }
});

test('metadata recovers via the official API when asset HEAD has no usable size', async () => {
  for (const headSize of [null, '0', '-1', '4096.5', '999999999999999999999']) {
    const { response, requests } = await apiTest('?format=json', { headSize });
    assert.equal(response.status, 200);
    assert.equal((await response.json()).size, binary.length);
    assert.ok(requests.some(url => url.startsWith('https://api.github.com/')));
  }
});

test('metadata does not advertise unusable lengths or follow foreign asset hosts', async () => {
  for (const apiSize of [null, 0, -1, 4096.5, 128 * 1024 * 1024 + 1, '4096']) {
    assert.equal((await apiTest('?format=json', { headSize: null, apiSize })).response.status, 503);
  }
  const result = await apiTest('?format=json', { headHost: 'https://foreign.example', apiBlocked: true });
  assert.equal(result.response.status, 503);
  assert.ok(!result.requests.some(url => url.startsWith('https://foreign.example')));
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
