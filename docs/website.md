# 官网部署 · Website deployment

官网使用静态 HTML、CSS 和本地演示图。中文入口为 `/?lang=zh`，英文入口为 `/en/`。直接打开 `/` 时读取浏览器首选语言。

The static site ships Chinese and English HTML, shared CSS, and local demo images. It uses the browser language at `/`. Open `/?lang=zh` for Chinese or `/en/` for English.

中英文页面和软件更新按钮共用 `/download/`。Vercel 优先从 GitHub 获取最新正式版 Setup，失败时依次尝试 `gh-proxy.com` 和 `ghfast.top`。发送安装包前，服务会比对官方 SHA-256。发布流程上传固定文件名和带版本号的安装包，并在校验清单中记录两者。

The website and app update button share `/download/`. Vercel fetches the latest stable Setup from GitHub first, then tries `gh-proxy.com` and `ghfast.top` if needed. It checks the file against the official SHA-256 before sending it. The release workflow publishes both filenames and their checksums.

## Vercel

1. [用 GitHub 登录 Vercel / Sign in to Vercel with GitHub](https://vercel.com/login).
2. [导入项目 / Import a project](https://vercel.com/new) and select `GalokPeng/light-block-desktop`. If Vercel needs GitHub app access, select this repository.
3. 保持项目根目录为仓库根目录，生产分支为 `main`。Keep the repository root as the project root and use `main` for production. `vercel.json` publishes `site/public` and routes downloads to `api/setup.mjs`.
4. 核对首页、语言切换和 Setup 下载。Verify the landing page, language links and direct Setup download. Future pushes to `main` deploy through the GitHub integration.

## 本地预览 · Local preview

```powershell
python -m http.server 8080 --bind 127.0.0.1 --directory site/public
```

Visit [the local site](http://127.0.0.1:8080/). Test `/download/` on Vercel, where the download function runs. Replace images only with isolated app fixtures and bundled media. Do not include actual desktop icons, user paths or data.
