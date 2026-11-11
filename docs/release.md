# Windows 构建与发布

[English](release.en.md)

发布版本取自 `still/model.py` 中的 `VERSION`，例如 `0.5.0`。PyInstaller 的文件版本、便携包目录、Setup 文件名和 GitHub 标签都由它生成。

## 本地验证

需要 Windows x64、Python 3.11、Git、7-Zip 和 [Inno Setup 6](https://jrsoftware.org/isdl.php)。先提交要打包的源码，使源码 ZIP 与可执行文件来自同一份 Git 代码。

```powershell
python -m pip install -r requirements.txt
python tools/run_tests.py
# 在有 Explorer 桌面的 Windows 会话执行实机回归
python tools/run_tests.py --desktop
# 在源码根目录构建，或用 -IsccPath 指定编译器路径
.\tools\release.ps1
```

`release/` 包含 Setup、免安装 ZIP、同一提交的源码 ZIP、FFmpeg 对应源码 ZIP 和 SHA-256。Setup 默认按当前用户安装，卸载保留应用配置目录。便携版移到安装版后，如已有自启动，可以在新版重新开启自启动以更新路径。安装包未签名。安装向导跟随 Windows 显示语言，并支持英文、简体中文和繁体中文。

首次构建会下载经过 SHA-256 校验的原生组件工具链和 FFmpeg 源码。Setup 和便携包自带原生任务栏 DLL 和录屏所需的 FFmpeg。安装验证会检查 DLL 加载和随包 FFmpeg 的捕获设备与编码能力。三段式布局需要 Windows 11 22H2 或更新的 x64 系统。

上游源码站不可达时，构建脚本可从本项目已公开的 v0.8.1 FFmpeg 对应源码包恢复固定版本的原始源码。外层源码包和内部文件分别校验 SHA-256，不执行备用包的构建脚本。更换上游源码版本时需同时核对备用记录。

没有已安装版本的测试机上可执行 `python tools/test_setup.py`，在隔离目录检查自动选语言及中英文安装、启动、卸载和配置保留。

## GitHub 首次接入

1. 在 GitHub 建立空的 `light-block-desktop` 仓库，将本仓库的 `main` 推送到 `origin`。仓库已有 MIT 文件，创建时可保留空仓库选项。
2. 在 Actions 手动运行 **Windows release**，保持 `publish` 关闭，先下载构建产物并验证 Setup。
3. 在 Settings → Secrets and variables → Actions → Variables 将 `PUBLISH_RELEASES` 设为 `true`。用 **Windows release** 手动运行一次并打开 `publish`，发布当前版本。GitHub Actions 由内置 `GITHUB_TOKEN` 发布，无需保存个人访问令牌。

## 后续发布

更新 `CHANGES.md`，递增 `still/model.py` 的 `VERSION`，提交并推送到 `main`。若使用受保护分支，合入发布 PR 同样会触发流程。

GitHub Actions 会验证版本、运行 Windows 测试、构建便携包和 Setup、验证安装与卸载，然后以触发提交创建对应标签与 Release。相同版本已经有标签时会跳过发布。普通 PR 和主题分支保持测试流程，仓库的发布权限只授予主分支的发布任务。

如果发布中断，先检查 Actions 日志和 Releases 是否残留草稿。保留已经发布的标签和资产。维护者可以补齐同一提交的草稿，或者递增版本重新发布。已安装的应用可从 Releases 下载新版 Setup。
