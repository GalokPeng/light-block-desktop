import { createHash } from 'node:crypto';
import { Readable } from 'node:stream';
import { downloadSources, setupUrl } from '../site/public/downloads.mjs';

const github = 'https://github.com';
const releases = '/GalokPeng/light-block-desktop/releases';
const setupName = 'Qingkuai-Setup-x64.exe';
const officialOrigins = new Set([github, 'https://release-assets.githubusercontent.com', 'https://objects.githubusercontent.com']);
const mirrors = downloadSources.filter(source => source.id !== 'github').map(source => source.origin);
const maxInstallerBytes = 128 * 1024 * 1024;
let cachedRelease;

async function fetchAsset(url, allowed, signal) {
  for (let redirect = 0; redirect < 5; redirect++) {
    if (!allowed.has(url.origin)) throw new Error('Unexpected asset host');
    const response = await fetch(url, { redirect: 'manual', signal });
    if (![301, 302, 303, 307, 308].includes(response.status)) return response;
    await response.body?.cancel();
    url = new URL(response.headers.get('location'), url);
  }
  throw new Error('Too many asset redirects');
}

async function readBody(response, limit) {
  if (!response.ok || !response.body) {
    await response.body?.cancel();
    throw new Error('Asset unavailable');
  }
  const chunks = [];
  let size = 0;
  for await (const chunk of response.body) {
    size += chunk.byteLength;
    if (size > limit) throw new Error('Asset exceeds size limit');
    chunks.push(chunk);
  }
  return { chunks, size };
}

async function releaseFromChecksums() {
  const latest = new URL(releases + '/latest/download/' + setupName, github);
  const response = await fetch(latest, { method: 'HEAD', redirect: 'manual', signal: AbortSignal.timeout(6000) });
  const asset = new URL(response.headers.get('location'), latest);
  const match = asset.pathname.match(/^\/GalokPeng\/light-block-desktop\/releases\/download\/(v\d{1,5}\.\d{1,5}\.\d{1,5})\/Qingkuai-Setup-x64\.exe$/);
  if (response.status !== 302 || asset.origin !== github || !match) throw new Error('Release unavailable');
  const [, version] = match;
  const sums = new URL(releases + '/download/' + version + '/Qingkuai-' + version + '-SHA256SUMS.txt', github);
  const checksum = await fetchAsset(sums, officialOrigins, AbortSignal.timeout(8000));
  const { chunks } = await readBody(checksum, 16 * 1024);
  const entries = new Map();
  for (const line of Buffer.concat(chunks).toString('ascii').split(/\r?\n/)) {
    const fields = line.match(/^([a-f0-9]{64}) {2}([A-Za-z0-9._-]+)$/i);
    if (fields) entries.set(fields[2], fields[1].toLowerCase());
  }
  const sha256 = entries.get(setupName) || entries.get('Qingkuai-' + version + '-Setup-x64.exe');
  if (!sha256) throw new Error('Official Setup checksum unavailable');
  return { version, asset, sha256, size: null };
}

async function releaseFromApi() {
  const url = 'https://api.github.com/repos/GalokPeng/light-block-desktop/releases/latest';
  const response = await fetch(url, { redirect: 'error', signal: AbortSignal.timeout(6000) });
  const { chunks } = await readBody(response, 256 * 1024);
  const release = JSON.parse(Buffer.concat(chunks).toString('utf-8'));
  const version = release.tag_name;
  if (release.draft || release.prerelease || typeof version !== 'string' || !/^v\d{1,5}\.\d{1,5}\.\d{1,5}$/.test(version)) {
    throw new Error('Release unavailable');
  }
  const installer = release.assets.find(asset => asset.name === setupName && asset.state === 'uploaded');
  const digest = /^sha256:([a-f0-9]{64})$/.exec(installer?.digest);
  if (!digest) throw new Error('Official Setup checksum unavailable');
  const asset = new URL(releases + '/download/' + version + '/' + setupName, github);
  return { version, asset, sha256: digest[1], size: installer.size };
}

async function latestRelease() {
  if (cachedRelease?.until > Date.now()) return cachedRelease;
  const release = await releaseFromChecksums().catch(releaseFromApi);
  cachedRelease = { ...release, until: Date.now() + 60_000 };
  return cachedRelease;
}

async function verifiedSetup(release) {
  for (const origin of [github, ...mirrors]) {
    const official = origin === github;
    const url = official ? release.asset : new URL(origin + '/' + release.asset.href);
    const allowed = official ? officialOrigins : new Set([...officialOrigins, origin]);
    try {
      const response = await fetchAsset(url, allowed, AbortSignal.timeout(official ? 12000 : 20000));
      const file = await readBody(response, maxInstallerBytes);
      const hash = createHash('sha256');
      for (const chunk of file.chunks) hash.update(chunk);
      if (hash.digest('hex') === release.sha256) return file;
    } catch {
      // Try the next public source after an unavailable or interrupted download.
    }
  }
  throw new Error('No source returned the official Setup');
}

export default {
  async fetch(request) {
    if (request.method !== 'GET' && request.method !== 'HEAD') {
      return new Response(null, { status: 405, headers: { Allow: 'GET, HEAD' } });
    }
    try {
      const url = new URL(request.url);
      const format = url.searchParams.get('format');
      if (format && !['json', 'checksum'].includes(format)) return new Response('Unknown format', { status: 400 });
      const source = url.searchParams.get('source');
      const selected = source || (url.searchParams.get('lang') === 'zh' ? 'gh-proxy' : 'github');
      if (!downloadSources.some(item => item.id === selected)) return new Response('Unknown download source', { status: 400 });
      const release = await latestRelease();
      const headers = { 'X-Qingkuai-Version': release.version, 'X-Qingkuai-SHA256': release.sha256, 'Cache-Control': 'no-store' };
      if (request.method === 'HEAD') return new Response(null, { headers });
      if (!format && (source || url.searchParams.has('lang'))) {
        return new Response(null, { status: 302, headers: { ...headers, Location: setupUrl(selected, release.version) } });
      }
      if (format === 'json') {
        return Response.json({ version: release.version, name: setupName, sha256: release.sha256,
          size: release.size, url: release.asset.href,
          sources: downloadSources.map(source => ({ id: source.id, name: source.name, url: setupUrl(source.id, release.version) })),
        }, { headers });
      }
      if (format === 'checksum') {
        return new Response(`${release.sha256}  ${setupName}\n`, {
          headers: { ...headers, 'Content-Type': 'text/plain; charset=utf-8', 'X-Content-Type-Options': 'nosniff' },
        });
      }
      const { chunks, size } = await verifiedSetup(release);
      return new Response(Readable.toWeb(Readable.from(chunks)), {
        headers: {
          ...headers,
          'Content-Type': 'application/octet-stream',
          'Content-Disposition': 'attachment; filename="' + setupName + '"',
          'Content-Length': String(size),
          'X-Content-Type-Options': 'nosniff',
        },
      });
    } catch {
      return new Response('安装包暂不可用，请稍后重试。 Setup is temporarily unavailable. Please try again.', {
        status: 503,
        headers: { 'Content-Type': 'text/plain; charset=utf-8', 'Retry-After': '60', 'Cache-Control': 'no-store' },
      });
    }
  },
};
