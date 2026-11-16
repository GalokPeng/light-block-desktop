# 轻块桌面项目地图

本 C++ 测试分支 `cpp-preview` 为 Windows x64、C++23 / Qt Widgets / Win32。`src/main.cpp` 是入口，`src/version.hpp` 是唯一版本源。用 `build.ps1` 构建，以 `--data-dir <全新测试目录>` 启动隔离实例。正式版 `main`、`python-v1.0.0` 分支和 `v1.0.0` 标签继续保留 Python；禁止合入或覆盖。

| 领域 | 入口与关键约束 |
| --- | --- |
| 主窗口与即时设置 | `ui/frontend.cpp`、`src/frontend.cpp`、`src/settings.cpp`、`src/core.cpp`。单字段提交，保存失败回滚；页面缓存不能覆盖后台新状态。中英文成对，主题含系统标题栏。界面库与后端分属 GCC/LLVM，`src/ui_api.h` 只传带版本/大小的 POD 和明确所有权的句柄 |
| 数据隔离 | `src/profile.cpp`。默认安装目录/data-preview，不能写入时使用 LOCALAPPDATA/LightBlockDesktopPreview。显式含 Python settings.json 的目录拒绝打开。`src/migration.cpp` 仅用于合成回归，不在测试版启动时迁移用户数据 |
| 壁纸 | `src/player.cpp`、`src/media.cpp`、`src/static_wallpaper.cpp`、`ui/media_worker.cpp`。静态系统调用与视频解码放监督子进程，旧进程退出后才能切换；新首帧/静态成功后才提交active_path。保留静态壁纸退出后的系统状态。默认无音频设备，启用声音需确认。MF不支持的编解码由独立Qt Multimedia worker兼容 |
| 图库与在线媒体 | `ui/online.hpp`、`src/online.cpp`、`src/thumbnails.cpp`、`assets/online-wallpapers.json`。按需绘制和有界缩略图缓存；原文件下载有大小、来源、摘要校验与取消。当前精选20静图/10GIF/20视频，不改变已确认素材而不说明。授权见docs/wallpaper-sources.md |
| 收纳与系统恢复 | `src/organizer.cpp`、`src/organizer_window.cpp`、`src/desktop_lease.cpp`。引用原文件，保留图标可见性恢复凭据；Explorer重启有序退租和重挂。各布局保留排序方式。不可把改色操作变成重新分类或移动文件 |
| 原生任务栏 | `src/taskbar.cpp`、`native/taskbar_layout.cpp`。三段来自原生任务栏；显示/隐藏共享状态，恢复原工作区、区域和样式。Explorer会话按HWND和进程身份区分，不能沿用启动时计时器误判新会话超时 |
| 便签与工具 | `ui/notes.cpp`、`src/notes.cpp`，以及ui/src中的clipboard、file_tools、focus、launcher模块。便签共享文档且字段级更新；文件改名先计划、确认、日志、执行，取消和错误不丢恢复信息。剪贴板默认关闭，测试不能读取用户真实剪贴板 |
| 截屏与录屏 | `src/capture.cpp`、`src/annotation.cpp`、`src/recording.cpp`、`ui/capture_controls.cpp`、`ui/recording_controls.cpp`。默认Ctrl+Alt+A，可自定义；贴图原位原像素，DPI正确。录制边框在采集区域外或排除；拖动双端时间指针并逐帧微调，导出在受监督FFmpeg进程中完成 |
| 鼠标与粒子 | `src/pointer.cpp`、`src/particles.cpp`、`ui/pointer_settings.hpp`。3套轮廓、17角色、75–200%大小和热点一起缩放；主题/颜色与粒子独立。静止停绘，恢复系统指针，不占用旧实例租约 |
| 更新与安装 | `ui/updates.hpp`、`installer/LightBlockDesktop.iss`、`src/version.hpp`。测试版不请求或安装正式更新；独立 Preview AppId、exe、目录、配置、自启动和桌面菜单。发行只用 preview-v* tag、Preview 资产、prerelease=true/latest=false，禁止覆盖正式 alias |
| 官网 | `site/public`、`api/setup.mjs`。中英、明暗、直达最新Setup。保留旧版更新JSON和checksum契约；展示素材只能来自生成样例与内置素材 |

## 验证与数据边界

`tools/test.ps1`覆盖隔离原生、媒体和Qt UI回归；`tools/test_setup.py`、`tools/test_update_flow.py`验证独立AppId的真实安装升级。详细命令见docs/release.md。离屏帧、模拟系统调用不能证明所有真实Explorer/DPI/驱动组合，涉及系统特性仍需针对性实机检查。

测试版安装身份集中在 `src/version.hpp`；不得改回正式版标识。官网 `/preview/` 与 `/en/preview/` 单独提供测试版，正式 `/download/` JSON/checksum/Setup 契约保持 Python 1.0.0。共享系统桌面仍需要会话互斥。测试资料、下载缓存、构建产物和真实用户内容不得提交。
