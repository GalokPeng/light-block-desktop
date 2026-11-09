# 官网部署 · Website deployment

官网使用静态 HTML、CSS 和本地演示图。中文入口为 `/?lang=zh`，英文入口为 `/en/`。直接打开 `/` 时读取浏览器首选语言。

The static site ships Chinese and English HTML, shared CSS, and local demo images. It uses the browser language at `/`. Open `/?lang=zh` for Chinese or `/en/` for English.

中英文下载按钮都使用 `/download/`。Vercel 将请求转到 GitHub 最新正式版中的 `Qingkuai-Setup-x64.exe`，浏览器会直接下载文件。发布流程会将固定文件名的 Setup 与带版本号的 Setup 一起上传，并在 SHA-256 清单中记录两者。新版发布后，下载入口自动更新。

Both download buttons use `/download/`. Vercel redirects the request to `Qingkuai-Setup-x64.exe` in the latest GitHub release so the browser downloads the file. The workflow publishes the fixed and versioned filenames together and lists both in the SHA-256 manifest. The link picks up each new release automatically.

## Vercel

1. [用 GitHub 登录 Vercel / Sign in to Vercel with GitHub](https://vercel.com/login).
2. [导入项目 / Import a project](https://vercel.com/new) and select `GalokPeng/light-block-desktop`. If Vercel needs GitHub app access, select this repository.
3. 保持项目根目录为仓库根目录，生产分支为 `main`。Keep the repository root as the project root and use `main` for production. `vercel.json` publishes `site/public` without a build step or environment variables.
4. 核对首页、语言切换和 Setup 下载。Verify the landing page, language links and direct Setup download. Future pushes to `main` deploy through the GitHub integration.

## 本地预览 · Local preview

```powershell
python -m http.server 8080 --bind 127.0.0.1 --directory site/public
```

Visit [the local site](http://127.0.0.1:8080/). Test `/download/` on Vercel, where the redirect is configured. Replace images only with isolated app fixtures and bundled media. Do not include actual desktop icons, user paths or data.
