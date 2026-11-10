<p align="center"><img src="assets/icon.png" width="88" alt="轻块桌面图标"></p>
<h1 align="center">轻块桌面</h1>
<p align="center">Windows 壁纸、截屏录屏、桌面收纳、原生任务栏美化和便签</p>
<p align="center"><a href="https://light-block-desktop.vercel.app/download/"><strong>下载 Windows 安装包</strong></a> · <a href="#功能展示">功能展示</a> · <a href="https://light-block-desktop.vercel.app/?lang=zh">产品官网</a> · <a href="README.en.md">English</a> · <a href="CHANGES.md">更新日志</a></p>

## 下载与安装

1. 在 [GitHub Releases](https://github.com/GalokPeng/light-block-desktop/releases/latest) 下载 `Setup-x64.exe`。也可完整解压 `Windows-x64.zip` 后运行 `轻块桌面.exe`。
2. 支持 Windows 10 / 11 x64。安装向导自动跟随 Windows 显示语言，支持简体中文、繁体中文和英文。程序在启动时读取同一设置，中文系统显示简体中文，其他语言默认英文。
3. 升级前退出旧版，再启动新版。配置和壁纸库默认保存在安装目录的 `data` 下。旧配置会复制到新目录，安装目录不可写时沿用 `%LOCALAPPDATA%\StillWallpaper`。

Setup 默认安装到当前用户目录。安装版和免安装版均包含壁纸播放、截屏、录屏和媒体导出所需的组件，首次使用即可录屏。录屏组件的许可证和对应源代码见 [第三方声明](THIRD_PARTY_NOTICES.md)。

## 功能展示

### 设置壁纸

导入本地图片、GIF 或视频，保留原画质副本。可选显示器和铺放方式，支持从实际视频帧生成封面。视频默认静音，可在设置或桌面右键菜单中切换。支持登录后恢复壁纸和全屏自动暂停。

![轻块桌面的壁纸库和显示设置](site/public/media/wallpaper-zh.webp)

### 截屏与录屏

默认 `Ctrl+Alt+A` 打开截屏，可选窗口或区域、添加标注、滚动拼接长图、复制 PNG 或原位贴图。录屏可用 `Ctrl+Alt+R` 停止，完成后直接贴屏预览，可涂鸦、裁剪画面和截取时间片段，按选定画质与帧率导出 MP4、GIF 或无损 MKV。

| 截屏选区与标注 | 录屏预览与导出 |
| --- | --- |
| [![框选、标注与截屏工具栏](site/public/media/capture-zh.webp)](site/public/media/capture-zh.webp) | [![录屏预览、时间轴与导出设置](site/public/media/recording-zh.webp)](site/public/media/recording-zh.webp) |

### 桌面收纳与原生任务栏美化

收纳支持居上、居中和居下，可按名称、修改时间或分组排列，并搜索桌面项目。关闭收纳会恢复原生图标显示状态。

![示例桌面的收纳分组](site/public/media/organizer-zh.webp)

可选的任务栏美化在 Windows 11 22H2 或更新版本的 x64 主屏上，将电源与搜索、应用、系统托盘分三段居中。保留 Windows 搜索和托盘菜单，支持智能隐藏与触底唤出。

![原生任务栏布局示意，从左到右为电源与搜索、应用、系统托盘](site/public/media/taskbar-layout.svg)

*原生任务栏三段式布局示意*

### 桌面便签

右键桌面空白处即可新建并贴出便签。Windows 11 从「显示更多选项」进入。便签可拖动、双击编辑，支持搜索、到期提醒、完成标记和归档。

![便签与任务窗口以及贴在示例桌面上的本周计划](site/public/media/notes-zh.webp)

演示截图使用内置素材和虚构数据。程序在本机处理内容，无账户、广告或遥测。更多功能包括快速启动、剪贴板历史、文件工作台和专注计时。[完整使用指南](docs/usage.zh-CN.md)

## 开发与发布

```powershell
python -m pip install -r requirements.txt
python main.py
python tools/run_tests.py
```

Windows x64，Python 3.11 和 PySide6。修改之前可把 [二开 Skill](skills/light-block-desktop-dev/SKILL.md) 交给 Agent。上游 PR 遵循 [贡献指南](CONTRIBUTING.md)，安装包构建和自动发布见 [发布手册](docs/release.md)，官网配置见 [部署说明](docs/website.md)。

应用源码采用 [MIT](LICENSE)。随包运行库保留各自的 [第三方许可证](THIRD_PARTY_NOTICES.md)。

## Star 趋势

[![GitHub stars](https://img.shields.io/github/stars/GalokPeng/light-block-desktop?style=flat)](https://github.com/GalokPeng/light-block-desktop/stargazers)

[![Star History Chart](https://api.star-history.com/svg?repos=GalokPeng/light-block-desktop&type=Date)](https://www.star-history.com/#GalokPeng/light-block-desktop&Date)

# 致谢与交流

[Linux Do](https://linux.do/)
