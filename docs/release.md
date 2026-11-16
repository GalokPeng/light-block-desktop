# 构建与发布

[English](release.en.md)

版本唯一来源为 `src/version.hpp`，正式版本采用 `X.Y.Z`，Windows 数值版本为 `X.Y.Z.0`。产品名称保持轻块桌面 / Light Block Desktop，不在产品入口强调实现语言。

## 安装与升级契约

- AppId：`{3CD16B31-077E-4CE9-BB01-578642D94876}`；程序 `轻块桌面.exe`；默认目录 `%LOCALAPPDATA%/Programs/Light Block Desktop`。
- 固定 Setup 下载名 `Qingkuai-Setup-x64.exe`；标签 `v<VERSION>`；正式发布必须为 latest，不能标记 prerelease。
- 自启动沿用 `StillWallpaper`，桌面菜单沿用 `LightBlockDesktop.note/sound`。未启用自启动时不能在升级时擅自开启。
- 默认数据 `data/profile-v2`；只读安装回退 `%LOCALAPPDATA%/StillWallpaper/profile-v2`。旧数据不删除；迁移备份、校验、原子提交并保持幂等。卸载保留用户数据。
- `python-v1.0.0` 分支、`v1.0.0` 标签和发布资产永久保留，不移动。官网提供固定 v1.0.0 历史下载。

## 验证

Windows x64、PowerShell 7、Git、Python 3.11+（构建工具）、7-Zip；打包另需 Inno Setup 6。依赖下载并校验固定版本，用户不用另装 Python/Qt/FFmpeg。

```powershell
./build.ps1
./tools/test.ps1 -SkipBuild
node --test tests/test_website_downloads.mjs tests/test_website_theme.mjs tests/test_preview_download.mjs
python tools/test_release_protocol.py
python tools/check_wallpaper_catalog.py
```

回归覆盖原生模块、实际媒体帧、切换与崩溃清理、Qt 界面、便签、独立进程退出、取消注销、自启动命令与设置落盘。系统消息测试只针对自己的隔离进程，不实际注销或重启。多 DPI、全屏、Explorer 重建、声音设备、截屏残影需额外实机确认。

源码完整提交后执行 `./tools/release.ps1 -SkipBuild -SkipTests`，再执行 `python tools/test_setup.py --iscc <ISCC.exe>` 与 `python tools/test_update_flow.py --iscc <ISCC.exe>`。安装测试使用随机 AppId 和独立目录，不能覆盖维护者真实安装。

## 自动发布

`main` 推送或 `v*` 标签触发构建与验证；仅版本比历史正式标签更新时发布。`cpp-preview` 可作准备分支，只构建不发布。手动 workflow 的 publish 仅对 main 或精确版本标签生效。每个 checkout 固定触发 SHA，不构建漂移的分支头。

`release/` 包含版本化 Setup、固定名 Setup、便携 ZIP、应用源码 ZIP、Qt/FFmpeg 对应源码 ZIP 和 SHA256SUMS。两份 Setup 字节必须一致，安装包不超过 128 MiB，产物不带用户数据或开发缓存。尚未配置代码签名，不宣称安装包已签名。每个公开版本不可静默覆盖；修复后递增版本。

发布完成必须确认 GitHub latest、全部资产与摘要、官网中英文下载与更新 JSON、v1.0.0 下载和归档仍可用。官网旧 `/preview/` 链接跳转正式页面。推送成功不代表发布成功。
