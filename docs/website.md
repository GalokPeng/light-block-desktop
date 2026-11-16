# 官网部署 · Website deployment

官网使用静态 HTML、CSS 和本地演示图。中文入口为 `/?lang=zh`，英文入口为 `/en/`。直接打开 `/` 时读取浏览器首选语言。

The static site ships Chinese and English HTML, shared CSS, and local demo images. It uses the browser language at `/`. Open `/?lang=zh` for Chinese or `/en/` for English.

官网默认跟随系统明暗主题；太阳／月亮按钮直接切换并记忆选择，显示器按钮恢复系统跟随。`theme.js` 在样式加载前确定主题，展示图随主题切换。应用本身的主题和布局不受官网样式影响。

The site follows the system appearance by default. The sun/moon button remembers a manual choice; the monitor button restores system mode. Theme selection runs before CSS, and demo images follow the palette. Website styling does not change the desktop application's layout or appearance.

英文页面默认从 GitHub 官方下载，中文页面默认 `gh-proxy.com` 加速。下载按钮旁可以切换 `GitHub`、`gh-proxy.com`、`ghfast.top`，选择按页面语言分别保存在浏览器中。下载由浏览器直接连接所选源，不再要求整个安装包先经过 Vercel 转发。不同网络的实际速度不同，不保证某个加速源始终最快。

English defaults to official GitHub downloads; Chinese defaults to `gh-proxy.com`. The selector offers GitHub, `gh-proxy.com`, and `ghfast.top`, with separate browser preferences for each language. Downloads connect directly to the selected source rather than relaying the complete installer through Vercel. Mirror speed depends on the user's network.

页面加载后从 `/download/?format=json` 获取最新正式版信息，将按钮固定到具体版本的文件 URL，减少加速源缓存 `latest` 的影响。获取失败时保留可用的 `latest/download` 链接。`/download/?format=checksum` 提供从官方发布取得的 SHA-256。直链下载本身不执行浏览器端文件校验；软件内更新会在运行安装包之前校验完整文件。

The page resolves versioned asset links using `/download/?format=json`, retaining stable `latest/download` links when metadata is unavailable. `/download/?format=checksum` exposes the official release checksum. Direct browser downloads do not perform automatic file verification; the in-app updater verifies the complete installer before running it.

兼容入口 `/download/`（不带参数）仍通过服务器获取并验证完整安装包，供旧客户端使用。带 `lang=en`、`lang=zh` 或合法 `source` 参数时返回到对应版本与来源的重定向。HEAD 保留版本与校验值响应头。发布流程继续上传固定文件名、带版本号安装包及其校验清单。

The parameter-free `/download/` endpoint retains the verified server relay for older clients. Explicit language/source routes redirect to versioned downloads. HEAD responses retain version and checksum headers.

## 版本入口

官网默认下载 GitHub latest 正式版（当前 v2.0.0），并提供固定 v1.0.0 历史安装包。两者随线路切换但不能混用版本。`/preview/` 与 `/en/preview/` 永久跳转到对应语言正式页。更新 JSON 与固定 Setup 文件名保持旧客户端兼容。

## Vercel

1. [用 GitHub 登录 Vercel / Sign in to Vercel with GitHub](https://vercel.com/login).
2. [导入项目 / Import a project](https://vercel.com/new) and select `GalokPeng/light-block-desktop`. If Vercel needs GitHub app access, select this repository.
3. 保持项目根目录为仓库根目录，生产分支为 `main`。Keep the repository root as the project root and use `main` for production. `vercel.json` publishes `site/public` and routes downloads to `api/setup.mjs`.
4. 核对首页、语言切换和 Setup 下载。Verify the landing page, language links and direct Setup download. Deploy the verified main source to the existing Vercel project after its stable assets are available.

## 本地预览 · Local preview

```powershell
python -m http.server 8080 --bind 127.0.0.1 --directory site/public
```

Visit [the local site](http://127.0.0.1:8080/). Test `/download/` on Vercel, where the download function runs. Replace images only with isolated app fixtures and bundled media. Do not include actual desktop icons, user paths or data.

Run `node --test tests/test_website_downloads.mjs tests/test_website_theme.mjs tests/test_preview_download.mjs` to verify downloads and appearance without external services. C++ isolated UI fixtures are generated using `轻块桌面.exe --ui-snapshots <new-output-directory> --data-dir <new-profile-directory>`. Retained older-version illustrations are identified separately. See [demo media provenance](../site/public/media/README.md); never publish the actual desktop.
