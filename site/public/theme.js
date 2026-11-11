(() => {
  'use strict';
  const root = document.documentElement;
  const media = window.matchMedia('(prefers-color-scheme: dark)');
  let preference = null;
  try {
    const saved = localStorage.getItem('lbd-theme');
    if (saved === 'light' || saved === 'dark') preference = saved;
  } catch (_) { /* Private browsing can disable storage. */ }

  function apply() {
    const theme = preference || (media.matches ? 'dark' : 'light');
    root.dataset.theme = theme;
    root.style.colorScheme = theme;
    const chinese = root.lang.toLowerCase().startsWith('zh');
    document.querySelectorAll('[data-theme-toggle]').forEach(button => {
      const title = theme === 'dark'
        ? (chinese ? '切换到浅色主题' : 'Switch to light theme')
        : (chinese ? '切换到深色主题' : 'Switch to dark theme');
      button.title = title;
      button.setAttribute('aria-label', title);
    });
    document.querySelectorAll('[data-theme-system]').forEach(button => {
      button.setAttribute('aria-pressed', String(preference === null));
    });
    document.querySelectorAll('[data-theme-image]').forEach(img => {
      const url = `${img.dataset.themeImage}-${theme}.${img.dataset.themeExt || 'webp'}`;
      if (img.getAttribute('src') !== url) img.src = url;
      const link = img.closest('a[data-theme-link]');
      if (link) link.href = url;
    });
  }

  function save(value) {
    preference = value;
    try {
      if (value) localStorage.setItem('lbd-theme', value);
      else localStorage.removeItem('lbd-theme');
    } catch (_) { /* The current page still switches without storage. */ }
    apply();
  }
  apply(); // Set the palette before CSS and body paint.
  document.addEventListener('DOMContentLoaded', () => {
    apply();
    document.querySelectorAll('[data-theme-toggle]').forEach(button => {
      button.addEventListener('click', () => save(root.dataset.theme === 'dark' ? 'light' : 'dark'));
    });
    document.querySelectorAll('[data-theme-system]').forEach(button => {
      button.addEventListener('click', () => save(null));
    });
  });
  if (media.addEventListener) media.addEventListener('change', () => { if (!preference) apply(); });
  else if (media.addListener) media.addListener(() => { if (!preference) apply(); });
})();
