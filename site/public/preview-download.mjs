export const previewTag = 'preview-v1.1.0-beta.1';
export const previewSetup = 'Qingkuai-Preview-Setup-x64.exe';
const roots = { github: '', 'gh-proxy': 'https://gh-proxy.com/', ghfast: 'https://ghfast.top/' };
export function previewUrl(source = 'github') {
  if (!Object.hasOwn(roots, source)) throw new Error('Unknown preview download source');
  return roots[source] + 'https://github.com/GalokPeng/light-block-desktop/releases/download/' + previewTag + '/' + previewSetup;
}
export function bindPreview(doc) {
  const controls = [...doc.querySelectorAll('[data-preview-source]')];
  const apply = value => {
    const url = previewUrl(value);
    for (const link of doc.querySelectorAll('[data-preview-download]')) link.href = url;
    for (const control of controls) control.value = value;
  };
  apply(/^zh(?:-|$)/i.test(doc.documentElement.lang) ? 'gh-proxy' : 'github');
  for (const control of controls) control.addEventListener('change', () => apply(control.value));
}
if (typeof document !== 'undefined') bindPreview(document);
