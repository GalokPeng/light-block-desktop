<p align="center"><img src="assets/icon.png" width="88" alt="轻块桌面图标"></p>
<h1 align="center">轻块桌面</h1>
<p align="center">Windows 动态壁纸、桌面收纳、截屏和便签</p>
<p align="center"><a href="https://github.com/GalokPeng/light-block-desktop/releases/latest"><strong>下载最新版</strong></a> · <a href="https://light-block-desktop.vercel.app/?lang=zh">产品官网</a> · <a href="README.en.md">English</a> · <a href="CHANGES.md">更新日志</a></p>

![轻块桌面的壁纸库和显示设置](site/public/media/wallpaper-zh.webp)

## 下载与安装

1. 在 [GitHub Releases](https://github.com/GalokPeng/light-block-desktop/releases/latest) 下载 `Setup-x64.exe`。也可完整解压 `Windows-x64.zip` 后运行 `轻块桌面.exe`。
2. 支持 Windows 10 / 11 x64。安装向导自动跟随 Windows 显示语言，支持简体中文、繁体中文和英文。程序在启动时读取同一设置，中文系统显示简体中文，其他语言默认英文。
3. 升级前退出旧版，然后运行新安装包。配置保存在 `%LOCALAPPDATA%\StillWallpaper`，安装、升级和卸载都保留此目录。

Setup 默认安装到当前用户目录。壁纸播放和静态截屏可以直接使用。录屏和媒体导出需安装支持 `gdigrab`、`libx264`、`ffv1` 和调色板滤镜的 [FFmpeg 命令行工具](https://ffmpeg.org/download.html)，并将其加入 PATH。

## 功能

| 功能 | 用法 |
| --- | --- |
| 壁纸 | 导入本地静态图、GIF 或视频。动态壁纸直接读取原文件，可选显示器、铺放方式和自动暂停条件 |
| 桌面收纳 | 支持居上、居中和居下，可按名称、修改时间或分组排列。新增桌面项目会自动归类。关闭后恢复 Windows 原生图标的可见性 |
| 任务栏美化 | 可选 Dock 和原生托盘，支持智能隐藏、触底唤出、自定义颜色和磨砂效果 |
| 截屏与标注 | 默认 `Ctrl+Alt+A`，支持智能窗口、手动选区、画笔、文字、箭头、序号和马赛克。可保存 PNG、复制，或在选区原位贴图 |
| 滚动长截图 | 框选后在区域内滚动，自动尝试拼接，旁边显示预览。也可手动调整重叠高度 |
| 区域录屏 | `Ctrl+Alt+R` 停止后贴屏预览。可涂鸦、裁剪画面、截取片段，按选定画质与帧率保存或复制 MP4、GIF 和无损 MKV |
| 效率工具 | 快速启动与计算、可选剪贴板历史、桌面便签与任务、文件预览和批量改名、专注工作区 |

| 桌面收纳与便签 | 截屏工具栏 |
| --- | --- |
| ![桌面文件夹与置于桌面的便签](site/public/media/organizer-zh.webp) | ![框选、标注与截屏工具栏](site/public/media/capture-zh.webp) |

截图来自真实程序界面和虚构演示数据。程序在本机处理内容，无账户、广告或遥测。收纳和剪贴板记录需要主动开启。右上角的太阳或月亮可切换设置窗口主题。[完整使用指南](docs/usage.zh-CN.md)

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
