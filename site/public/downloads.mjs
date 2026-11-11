export const releaseRoot = 'https://github.com/GalokPeng/light-block-desktop/releases';
export const setupName = 'Qingkuai-Setup-x64.exe';
export const downloadSources = Object.freeze([
  { id: 'github', name: 'GitHub', origin: 'https://github.com' },
  { id: 'gh-proxy', name: 'gh-proxy.com', origin: 'https://gh-proxy.com' },
  { id: 'ghfast', name: 'ghfast.top', origin: 'https://ghfast.top' },
]);

export function setupUrl(sourceId = 'github', version = 'latest') {
  const source = downloadSources.find(item => item.id === sourceId);
  if (!source || (version !== 'latest' && !/^v\d{1,5}\.\d{1,5}\.\d{1,5}$/.test(version))) {
    throw new Error('Invalid download source or version');
  }
  const official = version === 'latest' ? `${releaseRoot}/latest/download/${setupName}`
    : `${releaseRoot}/download/${version}/${setupName}`;
  return source.id === 'github' ? official : `${source.origin}/${official}`;
}

export function defaultSource(language) {
  return /^zh(?:-|$)/i.test(language) ? 'gh-proxy' : 'github';
}

export function bindDownloads(doc, storage, fetchRelease) {
  const language = doc.documentElement.lang;
  const key = /^zh(?:-|$)/i.test(language) ? 'qingkuai-download-zh' : 'qingkuai-download-en';
  let selected = defaultSource(language);
  let version = 'latest';
  try {
    const saved = storage?.getItem(key);
    if (downloadSources.some(source => source.id === saved)) selected = saved;
  } catch { /* Downloads still work when browser storage is unavailable. */ }
  const controls = [...doc.querySelectorAll('[data-download-source]')];
  function apply(value) {
    if (!downloadSources.some(source => source.id === value)) return;
    selected = value;
    for (const link of doc.querySelectorAll('[data-setup-download]')) link.href = setupUrl(value, version);
    for (const control of controls) control.value = value;
  }
  apply(selected);
  for (const control of controls) control.addEventListener('change', () => {
    apply(control.value);
    try { storage?.setItem(key, control.value); } catch { /* Optional preference. */ }
  });
  if (fetchRelease) return Promise.resolve().then(fetchRelease).then(release => {
    if (release.name !== setupName || !/^v\d{1,5}\.\d{1,5}\.\d{1,5}$/.test(release.version)
        || release.url !== setupUrl('github', release.version)) return;
    version = release.version;
    apply(selected);
  }).catch(() => { /* Stable latest links remain usable if metadata is offline. */ });
}

if (typeof document !== 'undefined') {
  let storage;
  try { storage = globalThis.localStorage; } catch { /* Disabled storage. */ }
  bindDownloads(document, storage, async () => {
    const response = await fetch('/download/?format=json', { cache: 'no-store', signal: AbortSignal.timeout(6000) });
    if (!response.ok) throw new Error('Release unavailable');
    return response.json();
  });
}
