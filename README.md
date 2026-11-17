<p align="center"><img src="assets/icon.png" width="88" alt="轻块桌面图标"></p>
<h1 align="center">轻块桌面</h1>
<p align="center">Windows 壁纸、截屏录屏、桌面收纳、原生任务栏美化和便签</p>
<p align="center"><a href="https://github.com/GalokPeng/light-block-desktop/releases"><strong>下载最新安装包</strong></a> · <a href="#功能展示">功能展示</a> · <a href="https://light-block-desktop.vercel.app/?lang=zh">产品官网</a> · <a href="README.en.md">English</a> · <a href="CHANGES.md">更新日志</a></p>


**v2.0.4 正式版**：壁纸、截屏录屏、桌面美化和便签，集成所需播放与录制组件。

## 下载与安装

1. 从[官网](https://light-block-desktop.vercel.app/)或[GitHub](https://github.com/GalokPeng/light-block-desktop/releases/latest/download/Qingkuai-Setup-x64.exe)直接下载 Setup；免安装包完整解压后运行 `轻块桌面.exe`。
2. 支持 Windows 10 / 11 x64。安装向导跟随系统语言，支持简中、繁中和英文；软件内可切换中英文及明暗主题。无需另装 FFmpeg 或虚拟声卡。
3. 旧用户可在软件左下角检查更新、查看下载进度，校验成功后自动退出并启动安装。安装身份保持兼容，v1.0.0 原数据保留，新版使用 `data/profile-v2`；目录不可写时使用 `%LOCALAPPDATA%/StillWallpaper/profile-v2`。迁移有备份、原子提交与重复启动保护。

[下载历史版本 v1.0.0](https://github.com/GalokPeng/light-block-desktop/releases/download/v1.0.0/Qingkuai-Setup-x64.exe)。回退前先退出当前版本，建议另选目录安装；新版产生的修改不会反向同步到旧版数据。旧源码保留在 [`python-v1.0.0`](https://github.com/GalokPeng/light-block-desktop/tree/python-v1.0.0) 分支。

## 功能展示

下图为 v2.0.0 实际界面；后续功能示意含历史版本截图，均使用内置素材或虚构数据。

![v2.0.0 壁纸界面](site/public/media/v2-wallpaper.webp)

### 设置壁纸

导入本地图片、GIF 或视频，保留原画质副本。可选显示器和铺放方式，支持从实际视频帧生成封面。视频声音默认关闭，可在设置或桌面右键调整；开启前需要确认，确认后的偏好单独保存，改变声音会重新加载桌面视频。动态播放与设置界面隔离，旧播放器退出并取得新首帧后才确认应用。支持登录后恢复壁纸和全屏自动暂停。

自己导入的壁纸可右键删除库内副本，原文件保留。删除正在使用的壁纸时先恢复 Windows 壁纸。在线精选支持预览、搜索、原文件下载与来源署名。内置静态壁纸为「夜驰」「银河」「山海」「海岸」四张 1920×1080 原图，原有动态示例保留。

![轻块桌面的壁纸库和显示设置](site/public/media/wallpaper-zh.webp)

### 截屏与录屏

默认 `Ctrl+Alt+A` 打开截屏，可选窗口或区域、添加标注、滚动拼接长图、复制 PNG 或原位贴图。录制前可选择系统声音、麦克风或同时录制，默认关闭；录屏可用 `Ctrl+Alt+R` 停止，完成后直接贴屏预览，可涂鸦、裁剪画面和拖动双端时间指针截取片段。点击指针后可用方向键逐帧微调，Shift＋方向键移动十帧。按选定画质与帧率导出 MP4、GIF 或无损 MKV；GIF 提供快速／精细调色。

| 截屏选区与标注 | 录屏预览与导出 |
| --- | --- |
| [![框选、标注与截屏工具栏](site/public/media/capture-zh.webp)](site/public/media/capture-zh.webp) | [![录屏预览、时间轴与导出设置](site/public/media/recording-zh.webp)](site/public/media/recording-zh.webp) |

### 桌面收纳与原生任务栏美化

收纳支持居上、居中和居下，可按名称、修改时间或分组排列，并搜索桌面项目。新增桌面文件先显示为图标，默认一分钟后自动收纳，可关闭该选项，或右键放入指定分组。收纳、任务栏、分组管理按折叠区展示。图标底板和整体收纳背景支持独立调色、一键恢复默认；分组颜色单独保留，浅色背景自动适配深色文字。关闭收纳会恢复原生图标显示状态。

![示例桌面的收纳分组](site/public/media/organizer-zh.webp)

可选的任务栏美化在 Windows 11 22H2 或更新版本的 x64 主屏上，将电源／设置／任务管理器／原生搜索、应用、系统托盘分三段居中。保留 Windows 搜索和托盘菜单，支持智能隐藏与触底唤出，可调整背景颜色与不透明度。

![原生任务栏布局示意，从左到右为快捷工具与搜索、应用、系统托盘](site/public/media/taskbar-layout-zh-light.svg)

*原生任务栏三段式布局示意*

### 鼠标与美化

在「美化 → 鼠标与指针」选择流线、棱镜或像素，分别设置颜色和大小。点点／飘落／光芒可与任意指针组合、独立调色。光芒读取当前原生指针的轮廓，仅在外缘显示流光；飘落使用更细小的荧光微粒和柔和淡入淡出。停止移动后停绘，关闭后卸载监听，退出恢复系统方案。

![原创指针外轮廓光放大示意](site/public/media/cursor-glow-zh-dark.webp)

[查看美化设置界面](site/public/media/appearance-zh-light.webp)

### 桌面便签

右键桌面空白处即可新建并贴出便签。Windows 11 从「显示更多选项」进入。便签可拖动、双击标题编辑、拖动边缘改变大小；长正文可滚轮浏览且不显示滚动条，支持自选颜色、搜索、到期提醒、完成标记和归档。

![便签与任务窗口以及贴在示例桌面上的本周计划](site/public/media/notes-zh.webp)

功能示意包含历史版本画面，使用内置素材和虚构数据。设置和下载的壁纸保存在本机，无账户、广告或遥测。在线图库按需访问素材来源。「美化」提供流线、棱镜、像素三种原创指针，支持独立调色、75%～200% 大小；点点、飘落、光芒三种粒子可自由搭配并单独调色，静止后停绘。「设置」保留 Windows 夜间模式入口。更多功能包括快速启动、剪贴板历史、文件工作台和专注计时。[完整使用指南](docs/usage.zh-CN.md)

v2.0.4 改善任务栏长期运行和关闭美化后的恢复：避免尺寸变化留下裁剪，过滤托盘临时零宽度与隐藏状态，开关时核对恢复确认。桌面右键后新增内容优先显示在右键位置附近，气泡悬停显示“点击直接收纳”，可直接点击；一分钟自动收纳继续保留。

修复更新检查反复提示重试的问题：官网补齐旧客户端所需的安装包大小，新版增加双官网入口回退与超时诊断，下载后继续校验 SHA-256，再退出并启动安装。

「本地壁纸 / 在线壁纸」支持切换：当前静态 52 张、GIF 20 款、视频 33 段，仅按媒体类型筛选。10 款 GIF 按维护者确认保留，与本地复看版一致。所有来源文件至少 1920 × 1080，单项不超过 20 MB；GIF 要求原始 1080p、16:9 的完整循环场景。GIF 不以点赞／收藏作为门槛，公开热度如实记录；静图与原有视频参考公开支持票或收藏；新增动态素材核验来源、画面、原文件和公开页面浏览数据，未公开的指标保留为空。沿用来源文件，不自行压缩、裁短或放大。支持中英文搜索、按需预览、下载进度与取消、已下载状态及应用。GitHub 只保存 URL 和授权目录。关闭设置时正式下载继续，退出软件会取消未完成下载。[评估与筛选记录](docs/wallpaper-evaluation.md)

![在线壁纸精选与来源信息](site/public/media/online-wallpaper-zh.webp)

## 开发与发布

```powershell
./build.ps1
& ".\dist\LightBlockDesktop\轻块桌面.exe" --data-dir "$PWD\.local\dev-profile"
./tools/test.ps1 -SkipBuild
```

当前运行时为 Windows x64、C++23、Qt Widgets 与 Win32；Python 3.11 只用于构建、打包和验证工具，用户运行软件无需安装 Python。完整旧实现永久保存在 [`python-v1.0.0`](https://github.com/GalokPeng/light-block-desktop/tree/python-v1.0.0) 分支和 [`v1.0.0`](https://github.com/GalokPeng/light-block-desktop/tree/v1.0.0) 标签。修改之前可把 [二开 Skill](skills/light-block-desktop-dev/SKILL.md) 交给 Agent。上游 PR 遵循 [贡献指南](CONTRIBUTING.md)，安装包构建和自动发布见 [发布手册](docs/release.md)，官网配置见 [部署说明](docs/website.md)。

应用源码采用 [MIT](LICENSE)。随包运行库保留各自的 [第三方许可证](THIRD_PARTY_NOTICES.md)。

## Star 趋势

[![GitHub stars](https://img.shields.io/github/stars/GalokPeng/light-block-desktop?style=flat)](https://github.com/GalokPeng/light-block-desktop/stargazers)

[![Star History Chart](https://api.star-history.com/svg?repos=GalokPeng/light-block-desktop&type=Date)](https://www.star-history.com/#GalokPeng/light-block-desktop&Date)

# 致谢与交流

[Wikimedia Commons](https://commons.wikimedia.org/) — 在线壁纸的原始媒体与预览来源。作品版权归各原作者，分别依照其公开许可使用；软件详情与下载记录保留作者、来源及许可。[逐项素材署名](docs/wallpaper-sources.md)

[Internet Archive](https://archive.org/) · [Jeffrey Beach / Beachfront Productions](https://beachfrontprod.blogspot.com/) — 原作者公开提供的 CC BY 3.0 高清视频、预览及浏览与收藏记录，软件保留来源和署名。

[NASA Scientific Visualization Studio](https://svs.gsfc.nasa.gov/) — 极光、月夜、地球和黑洞等动态影像来源。按其[复用说明](https://svs.gsfc.nasa.gov/help/)及逐项署名使用；新增作品无音乐或音轨经核验为静音，不表示 NASA 为本软件背书。

[CSU/CIRA Satellite Library](https://satlib.cira.colostate.edu/) · [NOAA](https://www.noaa.gov/) · [JMA](https://www.jma.go.jp/jma/indexe.html) — 卫星云图与地球动画的原始来源，使用 Commons 明确标注许可的原始副本，保留逐项作者和来源标识。

[Linux Do](https://linux.do/)
