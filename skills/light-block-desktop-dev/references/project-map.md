# 轻块桌面项目地图

Windows 10 / 11 x64，Python 3.11，PySide6。运行 `python main.py`，也可以传入 `--data-dir` 指向测试目录。运行依赖在 `requirements.txt`。开发机有交互桌面时可加测 `python tools/run_tests.py --desktop`。

| 领域 | 入口与验证重点 |
| --- | --- |
| 启动与设置 | `main.py` 调用 `still/app.py`。`still/model.py` 的 `Store` 保存设置和素材索引，`ui.py` 构建主设置页。读取旧设置后再检查新设置落盘 |
| 壁纸 | `engine.py` 和 `win32.py`。静态壁纸由 Windows 显示，GIF 和视频通过 Qt 原生输出读取原文件。测试 WorkerW 关系、连续帧、暂停和退出清理 |
| 在线壁纸 | `online_catalog.py` 校验 schema 4 的 URL、原生 1080p、20 MB、每类最多 20 项、授权与摘要；静图／视频要求正向热度，GIF 要求实际多帧循环和 16:9；旧 schema 可解析但不能替换当前目录。`online_wallpapers.py` 负责限流、流式下载及缓存复用，独立小预览不能充当来源文件。`online_ui.py` 只保留静态／GIF／视频和搜索；静图 20、视频 20，GIF 按壁纸观感重新筛选，演示类批次已撤下。播放器与媒体识别支持 WMV、OGV。Controller 按需创建服务，关闭设置后正式下载继续、退出时取消。测试 `test_online_wallpapers.py`；`tools/check_wallpaper_catalog.py` 校验，`tools/build_wallpaper_catalog.py --credits-only` 同步署名。评估见 `docs/wallpaper-evaluation.md`，维护见 `docs/online-wallpapers-plan.zh-CN.md`；媒体文件不进入仓库 |
| 桌面收纳和便签 | `organizer.py`、`organizer_model.py`、`desktop_icons.py`、`notes.py` 与 `desktop_windows.py`。收纳入口引用原文件，旧托管文件有恢复逻辑。面板由 Explorer 桌面层拥有，保持不抢焦点。验证关闭后原生桌面图标状态恢复，便签位置和分组正常重载 |
| 截屏与热键 | `workbench.py`、`hotkeys.py`、`capture_overlay.py`、`capture.py`、`capture_text.py`。验证原生全局热键、150% 缩放下的选区和导出像素。右键重选和滚轮调标注与长图滚动各自独立 |
| 长图与录屏 | `scroll_capture.py`、`scroll_match.py`、`capture_frame.py`、`recording_ui.py`、`media_tools.py`。长图保留最后成功拼接的位置，录屏边框在采集区域之外或被排除。FFmpeg 独立进程负责录制和导出 |
| 下载与更新 | `updates.py`、`update_download.py`、`update_installer.py`、`native/update_launcher.cpp`。检查可信发布元数据、按语言选择来源、下载流式写盘并校验 SHA-256；必须在旧进程正常退出后才启动 Setup。`tools/test_update_flow.py --iscc <ISCC.exe>` 使用独立 AppId、隔离目录和 localhost 验证升级，禁止用真实安装目录做覆盖测试 |
| 原生任务栏 | `native/taskbar_layout.cpp` 与 `native_taskbar_xaml.py`。左侧为电源、任务栏设置、任务管理器三个紧凑原生按钮，再接原生搜索；固定 Start 槽按按钮尺寸计算，禁止从可能尚未恢复的 ActualWidth 派生宽度，不改变搜索变换或应用项边距。`tools/test_native_taskbar.ps1` 验证隔离 XAML 布局及按钮调用；测试程序支持 `--preview` 检查真实工具启动。组件通过源码构建标识区分 Explorer 驻留通道，替换时等待前一组件恢复；不可仅检查安装目录 DLL 后就假定实际运行的是新布局。布局属性比较须容忍亚像素数值转换，避免后台反复修复 |
| 发布 | `still/model.py` 的 `VERSION` 是版本源。 `build.ps1` 调 PyInstaller，`installer/LightBlockDesktop.iss` 编译安装包，GitHub Actions 验证并发布 |

## 实例和数据边界

- 默认配置位于可执行文件旁的 `data`，其中包括原画质壁纸副本、设置、收纳清单及备份、文件恢复凭据、便签和剪贴板数据库。旧配置从 `%LOCALAPPDATA%/StillWallpaper` 复制，安装目录不可写时沿用该目录。真实内容不进入 Git、便携包或安装包。
- 程序的单实例通道按配置目录派生。自启动注册表名称和桌面图标租约有独立的稳定标识。发布 fork 并要求与官方版并存时，应显式区分配置、自启动和安装标识。桌面图标可见性属于同一个 Windows 会话，应保留互斥保护。
- 与桌面图标、WorkerW、全局热键和真实录屏有关的行为不能由纯离屏测试证明。共享 Windows 会话内用 `--data-dir` 隔离配置，也要隔离或暂缓改变真实桌面状态的测试。
- 原始壁纸文件不覆盖、不压缩。可逆文件操作继续使用现有清单、写入顺序和恢复凭据。
