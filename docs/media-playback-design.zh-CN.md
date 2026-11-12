# 壁纸播放与切换：排查记录和技术方向

本轮修复与验证纳入 v0.9.3。下文记录开发期的复现结果、技术取舍和后续方向。

## 已复现的问题

开启音轨，依次播放内置视频、GIF、在线库「流银水珠」OGV（Theora / Vorbis），再切换到「林间画眉」WebM。原实现的 GUI 线程停在 `WallpaperEngine._release → QMediaPlayer.setAudioOutput(None)`。12、24、36 秒的 faulthandler 记录均停在同一位置，界面心跳停止；不能将这次阻塞只归因于视频体积或 Python 运算速度。

另一个风险是视频封面在 `videoFrameChanged` 回调中立即调用 `stop / setSource`。原实现多格式封面生成测试的界面心跳最大间隔约 1003 ms。本轮未证明所有机器都发生相同死锁，也没有认定根因是特定品牌显卡或音频设备。

## 已实现

```text
设置界面 / 状态与策略
  ├─ 静图：现有异步 Windows 壁纸任务
  ├─ GIF / 视频：受 Windows Job Object 保护的 Qt 媒体子进程
  ├─ 在线动态预览：独立、始终静音的媒体子进程
  └─ 视频封面：串行短任务，生成一次后使用磁盘缓存
```

- 从在线预览应用壁纸时也先等预览进程退出；即使关闭设置窗口，排队的应用请求仍由引擎持有。动态切换先异步退出旧进程，等退出后才建立下一个桌面解码器。正常退出等待上限 2 秒，超过则结束该媒体进程；主界面不调用音频管线的销毁函数。启动或控制无响应的截止时间为 15 秒。
- 首帧有效后才提交当前壁纸 ID 和“已应用”通知。停用、恢复、删除当前素材须等媒体进程结束后才继续，避免删除仍被打开的文件。迟到的旧进程消息不提交到新请求。
- 父进程被意外结束时，Windows 关闭 Job Object 并清理媒体子进程；GUI 不能只依赖子进程自行响应退出指令。
- 播放仍使用现有 Qt 原生视频输出与 FFmpeg 库，不转码、不降低原文件分辨率或帧率。常规播放不把每帧图像传回 Python 界面；测试才按需请求小幅诊断快照。录屏的独立 FFmpeg CLI 与这条播放路径不同，本轮没有改录屏运行时。
- 音轨每次启动默认关闭。开启前确认，开关变化重新载入当前视频，让音频输出在打开媒体源之前配置好。关闭音轨后不保留上一播放进程的音频设备。GIF 和在线动态预览始终无音轨输出。
- 首帧处理、封面生成和轨道切换离开原生帧/轨道信号栈后执行；不在收到视频帧的回调中直接销毁解码器。
- 启动日志使用明确的文件配置，防止更早创建的日志 handler 导致 `still.log` 为空。

本机初步结果：15 次开启音轨的动态切换，GUI 心跳最大间隔约 43 ms；静图/GIF/视频混合测试约 56 ms；封面测试约 47 ms。扩大到 40 次跨类型切换、预览、封面、损坏文件及清理的综合测试时，整个流程的最大间隔约 340 ms；这是另一组更宽的测试，不能把前者当成全流程上限。均未复现旧版超过 12 秒的主界面阻塞。后续系统广播、磁盘和驱动仍可能造成短暂停顿。

## 对照其他项目的判断

| 项目与源码 | 实际做法 | 对本项目的启发 |
| --- | --- | --- |
| [Lively 的 MPV 播放器](https://github.com/rocksdanister/lively/blob/25a0a5f4ed1988ff42117d3a17feb321062237fe/src/Lively/Lively/Core/Wallpapers/VideoMpvPlayer.cs) | 启动外部 mpv，用 JSON IPC 控制暂停、播放、音量与退出；可用 `hwdec=auto-safe`，等待加载有超时 | 播放器与主界面分离，硬解按能力选择，退出有边界；不把播放器更换等同于 UI 重写 |
| [Lively 的暂停策略](https://github.com/rocksdanister/lively/blob/25a0a5f4ed1988ff42117d3a17feb321062237fe/src/Lively/Lively/Core/Suspend/Playback.cs) | 可检查前台窗口、可见窗口或网格覆盖率，并处理锁屏等状态 | 可在现有全屏/电池暂停基础上，增加多窗口共同遮住桌面时暂停 |
| [Hidamari 服务端](https://github.com/jeffshee/hidamari/blob/master/src/hidamari/server.py) 和 [播放器](https://github.com/jeffshee/hidamari/blob/master/src/hidamari/player/video_player.py) | Python 控制服务与播放器进程分离；视频使用 VLC，换播放器时有 terminate / join 超时 / kill | Python 可以负责界面和调度，解码器仍由原生库处理；重要的是生命周期与故障隔离 |
| [Qt 的 FFmpeg 配置说明](https://doc.qt.io/qt-6.11/advanced-ffmpeg-configuration.html) | 根据可用性、稳定性和效率选择硬件后端；调试环境变量属于私有接口 | 默认沿用 Qt 的硬解选择，不为所有机器强行指定一种 GPU 后端；软件回退不等于原文件被压缩 |

以上是参考结构和公开文档，未复制这些项目的 GPL 源码。

## 下一步优先级

1. **先收集真实负载。** 按文件编码、分辨率、帧率、是否有音轨，记录启动、首帧、停止耗时和进程资源；文件只有 20 MB 并不能说明它解码轻。重点比较 H.264、VP9、AV1、Theora 和大幅 GIF。
2. **增加桌面遮挡暂停。** 多个普通窗口合计遮住壁纸时也暂停，加入短暂稳定时间，防止窗口拖动时反复启停。完全不可见时不做无用解码，通常比换界面语言更有价值。
3. **再比较播放器后端。** 保留本轮的监督进程与控制协议，分别测试 Qt 和 mpv，核对启动、CPU、显存、耗电、兼容性和画质。mpv 的 [IPC](https://mpv.io/manual/master/#json-ipc) 与 [硬解选项](https://mpv.io/manual/master/#options-hwdec) 可以作为候选，但需要额外处理发行体积、许可和更新维护。
4. **有证据再用 C++ 收紧常驻开销。** 原生 Qt / Media Foundation 辅助程序可省掉媒体进程的 Python 解释器开销。Media Foundation 的 Windows 依赖较直接，但需要补齐本项目使用的媒体格式、GIF、渲染与恢复逻辑；并非改成 C++ 就自动降低视频帧缓冲和解码开销。

多进程会增加进程与解释器开销，不能宣传为必然更省内存。它首先解决主界面被底层解码器拖死的问题；后续比较总工作集、私有内存和显存，不能只看主进程数字变小。

## 验证入口

```powershell
python tools/run_tests.py --desktop
python tools/test_wallpaper_switching.py --extra-file <本地已下载的测试视频>
```

后者暂时应用演示壁纸，并在 `finally` 中恢复测试开始时的 Windows 壁纸；使用临时配置，不读取或上传桌面截图。异常进程、超时、迟到结果、音轨开关、文件占用和父进程崩溃回收由 `tests/test_media_process.py` 验证。
