# 官网部署 · Website deployment

官网使用静态 HTML、CSS 和本地演示图。中文入口为 `/?lang=zh`，英文入口为 `/en/`。直接打开 `/` 时读取浏览器首选语言。下载链接始终指向 GitHub 的最新 Release。

The static site ships Chinese and English HTML, shared CSS, and local demo images. It uses the browser language at `/`; `/?lang=zh` and `/en/` are explicit choices. Downloads point to the latest GitHub Release.

## Vercel

1. [用 GitHub 登录 Vercel / Sign in to Vercel with GitHub](https://vercel.com/login).
2. [导入项目 / Import a project](https://vercel.com/new) and select `GalokPeng/light-block-desktop`. If Vercel needs GitHub app access, select this repository.
3. 保持项目根目录为仓库根目录，生产分支为 `main`。Keep the repository root as the project root and use `main` for production. `vercel.json` publishes `site/public` without a build step or environment variables.
4. 核对首页、语言切换和下载链接。Verify the landing page, language links and Releases download. Future pushes to `main` deploy through the GitHub integration.

## 本地预览 · Local preview

```powershell
python -m http.server 8080 --bind 127.0.0.1 --directory site/public
```

Visit [the local site](http://127.0.0.1:8080/). Replace images only with isolated app fixtures and bundled media. Do not include actual desktop icons, user paths or data.
