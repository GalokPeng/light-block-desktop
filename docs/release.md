# Windows 构建与发布

发布版本取自 `still/model.py` 中的 `VERSION`，例如 `0.4.5`。PyInstaller 的文件版本、便携包目录、Setup 文件名和 GitHub 标签都由它生成。

## 本地验证

需要 Windows x64、Python 3.11、Git 和 [Inno Setup 6](https://jrsoftware.org/isdl.php)。先提交要打包的源码，使源码 ZIP 与可执行文件来自同一份 Git 代码。

```powershell
python -m pip install -r requirements.txt
python tools/run_tests.py
# 在有 Explorer 桌面的 Windows 会话执行实机回归
python tools/run_tests.py --desktop
# 在源码根目录构建，或用 -IsccPath 指定编译器路径
.\tools\release.ps1
```

`release/` 包含 Setup、免安装 ZIP、同一提交的源码 ZIP 和 SHA-256。Setup 默认按当前用户安装，卸载保留应用配置目录。便携版移到安装版后，如已有自启动，可以在新版重新开启自启动以更新路径。安装包未签名。录屏和媒体导出需要单独安装带 `gdigrab`、`libx264` 和 `ffv1` 的 FFmpeg，并让 `ffmpeg.exe` 在 PATH 中可用。

没有已安装版本的测试机上可执行 `python tools/test_setup.py`，在隔离目录检查安装、启动、卸载和配置保留。

## GitHub 首次接入

1. 在 GitHub 建立空的 `light-block-desktop` 仓库，将本仓库的 `main` 推送到 `origin`。仓库已有 MIT 文件，创建时可保留空仓库选项。
2. 在 Actions 手动运行 **Windows release**，保持 `publish` 关闭，先下载构建产物并验证 Setup。
3. 在 Settings → Secrets and variables → Actions → Variables 将 `PUBLISH_RELEASES` 设为 `true`。用 **Windows release** 手动运行一次并打开 `publish`，发布当前版本。GitHub Actions 由内置 `GITHUB_TOKEN` 发布，无需保存个人访问令牌。

## 后续发布

更新 `CHANGES.md`，递增 `still/model.py` 的 `VERSION`，提交并推送到 `main`。若使用受保护分支，合入发布 PR 同样会触发流程。

GitHub Actions 会验证版本、运行 Windows 测试、构建便携包和 Setup、验证安装与卸载，然后以触发提交创建对应标签与 Release。相同版本已经有标签时会跳过发布。普通 PR 和主题分支保持测试流程，仓库的发布权限只授予主分支的发布任务。

如果发布中断，先检查 Actions 日志和 Releases 是否残留草稿。保留已经发布的标签和资产。维护者可以补齐同一提交的草稿，或者递增版本重新发布。已安装的应用可从 Releases 下载新版 Setup。
