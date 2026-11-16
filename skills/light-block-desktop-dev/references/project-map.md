# 轻块桌面项目地图

当前正式实现为 Windows x64、C++23 / Qt Widgets / Win32，位于 `main`。`src/main.cpp` 是入口，`src/version.hpp` 是唯一版本源。用 `build.ps1` 构建，以 `--data-dir <全新测试目录>` 启动隔离实例。旧实现完整保留在 `python-v1.0.0` 分支和 `v1.0.0` 标签，不能移动归档引用或删除其发布资产。

| 领域 | 入口与关键约束 |
| --- | --- |
| 主窗口与即时设置 | `ui/frontend.cpp`、`src/frontend.cpp`、`src/settings.cpp`、`src/core.cpp`。单字段提交，保存失败回滚；页面缓存不能覆盖后台新状态。中英文成对，主题含系统标题栏。界面库与后端分属 GCC/LLVM，`src/ui_api.h` 只传带版本/大小的 POD 和明确所有权的句柄 |
| 数据与迁移 | `src/profile.cpp`。默认 data/profile-v2，不能写入时使用 LOCALAPPDATA/StillWallpaper/profile-v2。`src/migration.cpp` 从旧 settings.json 备份、校验并原子迁移，原数据不删除；重复启动不能覆盖新数据。显式测试目录不发现其他用户配置 |
| 壁纸 | `src/player.cpp`、`src/media.cpp`、`src/static_wallpaper.cpp`、`ui/media_worker.cpp`。静态系统调用与视频解码放监督子进程，旧进程退出后才能切换；新首帧/静态成功后才提交active_path。保留静态壁纸退出后的系统状态。默认无音频设备，启用声音需确认。MF不支持的编解码由独立Qt Multimedia worker兼容 |
| 图库与在线媒体 | `ui/online.hpp`、`src/online.cpp`、`src/thumbnails.cpp`、`assets/online-wallpapers-v2.json`。按需绘制和有界缩略图缓存；原文件下载有大小、来源、摘要校验与取消。旧 online-wallpapers.json 保持旧客户端兼容。授权见 docs/wallpaper-sources.md，热度不得虚构 |
| 收纳与系统恢复 | `src/organizer.cpp`、`src/organizer_window.cpp`、`src/desktop_lease.cpp`。引用原文件，保留图标可见性恢复凭据；Explorer重启有序退租和重挂。各布局保留排序方式。不可把改色操作变成重新分类或移动文件 |
| 原生任务栏 | `src/taskbar.cpp`、`native/taskbar_layout.cpp`。三段来自原生任务栏；显示/隐藏共享状态，恢复原工作区、区域和样式。Explorer会话按HWND和进程身份区分，不能沿用启动时计时器误判新会话超时 |
| 便签与工具 | `ui/notes.cpp`、`src/notes.cpp`，以及ui/src中的clipboard、file_tools、focus、launcher模块。便签共享文档且字段级更新；文件改名先计划、确认、日志、执行，取消和错误不丢恢复信息。剪贴板默认关闭，测试不能读取用户真实剪贴板 |
| 截屏与录屏 | `src/capture.cpp`、`src/annotation.cpp`、`src/recording.cpp`、`ui/capture_controls.cpp`、`ui/recording_controls.cpp`。默认Ctrl+Alt+A，可自定义；贴图原位原像素，DPI正确。录制边框在采集区域外或排除；拖动双端时间指针并逐帧微调，导出在受监督FFmpeg进程中完成 |
| 鼠标与粒子 | `src/pointer.cpp`、`src/particles.cpp`、`ui/pointer_settings.hpp`。3套轮廓、17角色、75–200%大小和热点一起缩放；主题/颜色与粒子独立。静止停绘，恢复系统指针，不占用旧实例租约 |
| 更新与安装 | `ui/updates.hpp`、`installer/LightBlockDesktop.iss`、`src/version.hpp`。显示下载进度、校验 SHA-256、正常退出后运行 Setup。保留正式 AppId、轻块桌面.exe 与 Qingkuai-Setup-x64.exe 固定资产；正式标签 vX.Y.Z、latest=true。完整约定见 docs/release.md |
| 官网 | `site/public`、`api/setup.mjs`。中英、明暗、直达最新Setup。保留旧版更新JSON和checksum契约；展示素材只能来自生成样例与内置素材 |

## 验证与数据边界

`tools/test.ps1`覆盖隔离原生、媒体和Qt UI回归；`tools/test_setup.py`、`tools/test_update_flow.py`验证独立AppId的真实安装升级。详细命令见docs/release.md。离屏帧、模拟系统调用不能证明所有真实Explorer/DPI/驱动组合，涉及系统特性仍需针对性实机检查。

安装身份集中在 `src/version.hpp`；正式升级与并存 fork 的身份策略不可混用。官网默认 v2 最新版，保留固定 v1.0.0 下载；旧测试入口跳转正式页。`/download/` JSON/checksum/Setup 契约兼容旧客户端。共享系统桌面需要会话互斥。测试资料、下载缓存、构建产物和真实用户内容不得提交。
