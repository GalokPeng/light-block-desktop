# C++ 测试版构建与分发

[English](release.en.md)

展示名称为 **测试版（极度优化）**，同时明确标注：**不稳定、独立安装、不替换正式版**。正式 main、Python v1.0.0、v1.0.0 标签和归档分支保持原样。测试版只在 `cpp-preview` 开发，以 GitHub prerelease 分发，不进入正式用户更新通道，不能将本分支合并到 main。

## 固定身份

版本唯一来源为 `src/version.hpp`。显示版本采用 `1.1.0-beta.1`，Windows 文件和安装器数值版本为 `1.1.0.0`。后续测试版递增 beta.N 或 rc.N，不能省略预发布后缀。

| 内容 | 测试版约定 |
| --- | --- |
| AppId | `{C07C418A-08FE-4E9B-8136-348822397B6B}` |
| 程序文件 | `轻块桌面测试版.exe` |
| 默认安装目录 | `%LOCALAPPDATA%/Programs/Light Block Desktop Preview` |
| 数据/自启动/右键菜单命名空间 | `LightBlockDesktopPreview` |
| 本地构建目录 | `dist/LightBlockDesktopPreview` |
| 发布标签 | `preview-v1.1.0-beta.1` |
| 测试版固定下载名 | `Qingkuai-Preview-Setup-x64.exe` |

安装器拒绝覆盖包含正式版程序的目录、正式默认目录及已登记正式安装目录的子目录。测试版默认使用安装目录下的 `data-preview`，不可写时回退到 `%LOCALAPPDATA%/LightBlockDesktopPreview`。卸载只处理测试版注册项并保留这些用户数据。不能自动迁移、写入或删除正式版真实配置；需要测试迁移时只能使用复制的合成测试数据。

## 本地构建与验证

需要 Windows x64、PowerShell 7、Git、Python 3.11+（仅构建与测试脚本）、7-Zip；打包另需 Inno Setup 6。依赖在当前项目的 build 中下载和校验，不依赖相邻仓库。程序运行不要求安装 Python 或 FFmpeg。

```powershell
./build.ps1
./tools/test.ps1 -SkipBuild
node --test tests/test_website_downloads.mjs tests/test_website_theme.mjs tests/test_preview_download.mjs
python tools/check_wallpaper_catalog.py
```

测试使用独立配置和生成素材。实机的多 DPI、全屏、Explorer 重建、屏幕采集和异常恢复需另外确认；离屏测试不能证明全部电脑兼容。用户正在体验的实例不能擅自关闭或覆盖。

确认源码提交完整后，生成与该提交一致的包：

```powershell
./tools/release.ps1 -IsccPath 'C:/path/to/ISCC.exe' -SkipBuild -SkipTests
python tools/test_setup.py --iscc 'C:/path/to/ISCC.exe'
python tools/test_update_flow.py --iscc 'C:/path/to/ISCC.exe'
```

安装测试使用临时 AppId 和目录，验证系统语言/简中/繁中/英文、同测试版升级、卸载保留数据及拒绝覆盖正式目录。更新器测试只调用合成 Setup 验证工具隔离；测试版 UI 不提供正式版自动更新。没有通过当前提交的完整 QA，不能发布。

## 资产与许可证

`preview-release/` 必须包含：

- `Qingkuai-Preview-v<VERSION>-Setup-x64.exe` 和相同字节的 `Qingkuai-Preview-Setup-x64.exe`。
- `Qingkuai-Preview-v<VERSION>-Windows-x64.zip`。
- `Qingkuai-Preview-v<VERSION>-source.zip`、`-ffmpeg-source.zip`、`-qt-source.zip`。
- `Qingkuai-Preview-v<VERSION>-SHA256SUMS.txt`。

禁止生成或上传正式版固定资产 `Qingkuai-Setup-x64.exe`。打包校验程序资源中的测试版名称和版本，Setup 体积预算为 128 MiB。未配置签名时，须如实说明安装包未签名。

Qt 动态库和媒体解码用 LGPL FFmpeg 7.1、独立录屏用 GPL FFmpeg 8.1.3 分别附许可证和对应源码。不能把应用 MIT 许可证理解为覆盖全部依赖。

## 手动发布与官网

Checks 只监听 cpp-preview。测试分支或 preview-v* 标签推送仅构建，不自动公开；发布需要维护者在 **C++ preview prerelease** workflow 手动设置 publish=true。发布固定使用 `--prerelease --latest=false`，不能改为正式 latest。已经公开的同版本不能静默覆盖；修复后递增测试版编号。

官网默认下载、正式版更新 API 和原有稳定资产继续指向正式版本。测试包只能从额外的测试版入口下载，显示不稳定、独立安装及可能存在缺陷的说明。发布后核对预发布标记、标签提交、所有校验和及许可源码资产，确认 GitHub 正式 latest 和正式更新 API 没有变化。推送成功不代表分发完成。
