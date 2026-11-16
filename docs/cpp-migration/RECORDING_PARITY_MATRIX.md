# 录屏与媒体工作台：Python 1.0.0 行为对照

本表是功能核对记录，不是“已完整复现”的声明。只修改 `cpp-preview`；不发布、不覆盖 Python 正式版。用户新增的系统声音／麦克风、局部橡皮、双端时间裁剪和内嵌媒体工作台保留。

## 基线与证据规则

- 只读基线：原 Python 仓库 `still/recording_ui.py`、`recording_timeline.py`、`recording_options.py`、`media_tools.py`、`workbench.py`。该工作目录 HEAD 为 `799ac705a9a476333dbe9efcac93fdfc6d65c215`，保留 `v1.0.0` 标签。不得修改该目录。
- 下表的 Python 行号对应上述基线；C++ 以函数名定位，避免后续修复导致行号失效。
- “实现”表示源代码已有相应路径；“待回归”表示本轮刚修改，尚不能用旧报告宣称通过；“待实机”表示需要真实 Windows 点击、焦点、屏幕/DPI 或设备观察。
- 旧证据 `.local/native-audio-final-20261114-003418/unit.json` 的 97 项通过，只覆盖当时源码。它不能为本轮新增的保存、历史、裁剪预览和快捷键修复背书。
- 录音测试使用合成 PCM／FLOAT WAVE，不录取维护者真实系统声音或麦克风。用户按 Esc 停止 Computer Use 后，不再进行交互自动化。

## 区域录制与生命周期

| ID | Python 基线／要求 | C++ 当前路径与结论 | 验证方式 |
|---|---|---|---|
| R01 | `workbench.py:128,187` 截屏选区后开始录制，设置窗口不会重新弹出 | `recording::start`、主程序截屏入口；实现 | 真实区域录制结束后确认设置窗口不抢焦点 |
| R02 | `workbench.py:277,299` 选区后短延迟开录 | 用户新要求：准备栏分别选系统声／麦克风，点击 Start 才开设备；属于保留的改进 | `audio_ui_test`、音频未 Start 不触发设备路径 |
| R03 | `recording_ui.py:26` 浮动紧凑录制条、计时、停止 | `RecordingPanel` phase 0/1/10；实现 | 中英与 100/150/200% DPI 截图、时间递增 |
| R04 | `recording_ui.py:69` 停止后禁用重复停止 | `stopping`、`audio_merging`；实现 | Qt `stop_disabled_when_finalizing` |
| R05 | `workbench.py:293` 录制边框始终保留 | `frame_proc` 的透明录制边框；实现 | 合成窗口布局 + 实际录屏边界观察 |
| R06 | `recording_ui.py:44` 条尽量在选区外，无位置时排除采集，否则隐藏 | 本轮保存排除采集结果；条与选区重叠且排除失败时，在启动编码前用 Qt hide 隐藏，避免定时刷新又显示；停止热键／托盘保持可用；**待新回归** | 模拟 affinity 失败；全屏区域录制不能把控制条录进去 |
| R07 | `workbench.py:324` 全局 `Ctrl+Alt+R` 停止、托盘状态更新 | 停止热键和冲突处理已实现；托盘录制状态仍需核对 | `settings_runtime_tests`；真实热键／托盘文字 |
| R08 | `workbench.py:209` 原显示器断开时停止录屏 | 本轮保存原 HMONITOR 与几何，准备后改变需重新选区；录制中几何变化或移除会请求正常停止并保留内容；**待新回归** | 隔离注入显示器布局变更，不操作真实显示器 |
| R09 | `media_tools.py:207` 至少 2×2 选区 | 本轮恢复至少 2×2，包含录制、画面裁剪及元数据探测；**待新回归** | 2/7/8 px 边界测试 |
| R10 | `media_tools.py:236` 正常停止让编码器完成容器，关闭时取消并回收 | `media::run` stop/cancel、音频有界结束；实现 | 取消／停止音轨合成、无残留子进程 |
| R11 | 原版静音录制 | 新增 WASAPI loopback／mic／两者，默认均关闭；准备栏记住选择，失败保留旧值 | `settings_patch_test`、音频 synthetic mux、blocked worker tests |
| R12 | 声音非原版功能，但不能静默失败 | 设备消失、默认端点变化、拒绝访问有具体错误；卡死驱动与 Session 生命周期隔离 | 合成 DeviceChanges、prepare/finish 卡住闩锁；真实设备另验 |
| R13 | 录制输入与配置相同 FPS，支持 FFV1 无损 | `begin_recording`，默认 24 FPS；原尺寸记录，无损 MKV／MP4；实现 | 元数据／逐帧像素／时长检查 |

## 贴屏预览、编辑与时间轴

| ID | Python 基线／要求 | C++ 当前路径与结论 | 验证方式 |
|---|---|---|---|
| R14 | `recording_ui.py:138,244` 结束后原位置预览，工具栏贴近画面 | `show_preview`、`position_controls`；实现 | 真实 1080p／竖屏／多 DPI，边缘不溢出 |
| R15 | `RecordingCanvas.mousePressEvent:116` Move 工具拖动画面 | 本轮补 MF/Qt 视频子窗口的 RecordingFrame 专用拖动转发，ClientToScreen 后交给父框；图库与壁纸不受影响；待实机手感确认 | 点击画面中央拖动，工具条与标注必须同步 |
| R16 | `RecordingResult:176` 直接画笔／高亮／橡皮入口 | 本轮新增直接按钮，按指定初始工具进入同位置暂停帧编辑；**实现，待回归** | Qt `direct_pen_highlight_eraser_move_undo_redo`，真实点击每个入口 |
| R17 | `recording_ui.py:318` 绘制暂停视频，继续播放回 Move | 编辑时暂停；本轮去掉编辑结束后擅自恢复播放；**待回归** | 标注完成后仍暂停，点击 Play 才继续 |
| R18 | 原版同一 RecordingCanvas 上绘制和播放 | 当前为同位置 native 编辑层与播放器层交替；不是同一渲染器，不能称逐像素相同 | 真实逐步对照切换闪烁、位置、手感 |
| R19 | 原版标注 Undo/Redo，切换播放后历史保留 | 本轮 `Document::History` 值传递 + retain 回调，主预览 Undo/Redo；24 步及总 8 MiB 上限；**待回归** | editor→preview→editor，逐笔撤销／重做 |
| R20 | 画面裁剪属于可撤销操作 | 本轮使用 image_crop 元数据让编辑器接收全部 crop-aware 历史，原始位图/标注坐标不变；Undo/Redo 可跨裁剪，完成后剥除临时标记并保留各步 crop；**待新回归** | 画两笔→裁剪→再画→退回预览连续 Undo/Redo |
| R21 | `RecordingCanvas.paint_content:100` 预览显示实际裁剪 | 本轮 `preview_layout` 负坐标源画面 + clip、`scaled_overlay` 同裁剪 alpha；再次画笔编辑使用相同 crop viewport，不再跳回全图；**待新回归** | `parity_tests` 几何与真实 alpha 像素；实机裁剪后画面与导出比对 |
| R22 | 不把静态马赛克冒充视频逐帧遮挡 | 原版录屏 DrawingTools 只暴露笔／高亮／橡皮，没有视频马赛克；Qt/legacy 已禁用，native guard 和提示已补 | mode3 工具拒绝测试；截图马赛克仍可用 |
| R23 | `recording_timeline.py:74` 左右端点拖动，中间点击 seek | `TrimTimeline`；实现 | 首／末帧、最近端点、窄选段、交叉限制 |
| R24 | `recording_timeline.py:97` ←/→ 一帧、Shift 十帧 | 保留；本轮新增 Tab/Shift+Tab 端点和播放头可达性 | Qt `one_frame_keyboard`、`shift_ten_frames`、audio UI 键盘检查 |
| R25 | `recording_ui.py:286,300` seek/trim 暂停，reset 恢复全段 | `recording_command`13/14；实现，新增导出区间状态 | Qt/native range fixture、真实暂停状态 |
| R26 | `recording_ui.py:290` 播放只循环所选区间 | 本轮补 `trim_loop_due` + 限流 seek；此前只有视觉指针限制；**待回归** | `parity_tests`，真实连续播放 3 次不越过尾端 |
| R27 | `recording_ui.py:309` 导出起止时间可见 | 本轮补三位小数区间状态，时间轴两端本就显示毫秒 | 24/29.97 FPS 首帧、末帧和单帧片段 |
| R28 | `recording_ui.py:225` 画面和工具条都有 Esc／Copy／Save | Qt toolbar 已有；本轮 native/MF 与 Qt media worker 仅向 RecordingFrame 转发相应 action | 工具条焦点、foreign 视频画面焦点分别按键；其他预览不受影响 |
| R29 | `workbench.py:331` 多个录屏结果可并存，再次录制 | 本轮拆分一个活动录制与最多 6 个结果 Session，独立回调/源文件/裁剪/历史；最多 2 个导出任务；**待联合回归** | 录制 A→保留 A→录制 B→分别编辑导出 |
| R30 | 每个 RecordingResult 拥有独立 QMediaPlayer | 本轮增加独立 result player slots，不再复用素材/壁纸播放器；最多 2 个解码进程，其他结果保留最多 960×540 暂停画面，点击播放恢复；**待真实隐藏双 worker 回归** | 录屏预览与壁纸/素材预览同时存在、切换互不打断 |

## 导出、复制与安全

| ID | Python 基线／要求 | C++ 当前路径与结论 | 验证方式 |
|---|---|---|---|
| R31 | `recording_options.py` 原尺寸/1080/720/480，原 FPS/12/15/24/30/60 | Qt 选项与配置匹配、不可输入；实现 | Qt noneditable、native metadata |
| R32 | `recording_ui.py:198` MP4/GIF/无损 MKV，初值跟随源格式 | `choice_format`、FFV1；实现 | 真实 FFV1 各帧与原片像素比较 |
| R33 | GIF 快速／精细独立于分辨率和 FPS | 两次流式 palettegen/paletteuse，快速只减少分析采样；实现 | 两种模式帧数、尺寸、单帧 GIF 测试 |
| R34 | `recording_ui.py:354` 以帧边界导出，微秒向下取整，限定帧数 | `encode`；实现 | 24/29.97 FPS、1 帧／12 帧、trim frame count |
| R35 | `recording_ui.py:363` 无编辑复制直接复制原文件路径 | 本轮新增 `unchanged_export` 快路径，不再重新编码；**待回归** | 条件 pure test、实际复制到第三方应用另验 |
| R36 | 未编辑保存不重新压缩 | 本轮 `copy_original` 原字节复制，原版为 stream copy；画质与音轨保真 | 实际源/目标 SHA-256 相同，取消无产物 |
| R37 | `recording_ui.py:422` 文件 URL，≤16 MiB GIF 附 image/gif | 本轮 CF_HDROP + image/gif；此前只有文件路径；**待回归** | 合成 GIF MIME 数据验证；真实粘贴需另验，不触碰个人剪贴板 |
| R38 | `recording_ui.py:381` 绝不覆盖原文件或既有文件，自动正确扩展名 | 本轮拒绝已有目标、替换扩展名、原子 no-replace 完成；此前 overwrite；**待回归** | 源文件/目标 sentinel、重入对话框、文件名竞争测试 |
| R39 | `recording_ui.py:442` 成功保存后记住 capture_directory | 本轮 Settings/core 字段 + 成功后保存目录，失败回滚；截图共用 | 配置 reload、写失败回滚、Unicode 路径 |
| R40 | `recording_ui.py:414` 导出时禁用编辑，显示进度，可取消 | 当前实现；本轮导出开始暂停播放 | 忙碌不能重复提交，取消保留预览与已选范围 |
| R41 | `media_tools.py:152` 两 pass GIF 连贯百分比 | native report 把两阶段映射 0–50/50–100；实现 | 单调进度与最终状态 |
| R42 | `recording_ui.py:431` 失败保留源文件，清理 partial | encode/copy_original 原子临时文件；实现 | cancel/no-output/source SHA 不变 |
| R43 | `media_tools.py:164` 编码器错误文字可见 | 本轮失败保留 stderr 日志并读最多 6 KiB 尾部、显示最多 1800 字符细节；工作台显示末尾原因，路径错误使用系统说明；**待新回归** | 模拟无权限、无空间、解码失败，用户无需猜原因 |
| R44 | 退出/关闭取消任务并回收媒体进程 | `shutdown` + `media::Control` + WASAPI self-owned worker；实现 | 有界取消/挂起驱动合成测试，程序退出无 Session UAF |

## 媒体工作台

| ID | Python 基线／要求 | C++ 当前路径与结论 | 验证方式 |
|---|---|---|---|
| R45 | `media_tools.py:273` 选择素材、只读路径、异步探测尺寸时长 | `MediaStudioPage` + `studio_open`；实现，位于主程序页内 | 真实小型合成 MP4/GIF probe，坏素材错误 |
| R46 | 起止时间精确到 0.001 s，X/Y/宽/高裁剪 | 字段齐全，额外按源帧率设置步进；实现 | 超出时长／画面拒绝，无负值 |
| R47 | MP4/GIF/MKV，GIF 1–30 FPS，宽原始/320/640/960 | 一致，额外精细/快速 palette 选择 | `studio-width.gif` 实际宽320/高180/6帧 |
| R48 | 导出为新文件，不覆盖源素材／已有目标 | native/UI 双门禁已实现 | studio source/target protection fixtures |
| R49 | 原版 MP4 奇数宽高拒绝 | C++ 自动 padding 成偶数；保留全部输入像素，属于兼容改善，但输出可能多1px | `export_test` 新增 319×179 MP4→320×180、MKV/GIF→319×179，以及 2×2 MP4 的实际编码/探测；待新回归 |
| R50 | 取消、进度、成功目录说明，关闭取消任务 | 页面内取消有；导航暂存 draft 并允许后台任务完成，与原窗口关闭取消不同 | 导航往返不重复导出，主动退出可有界取消 |
| R51 | 原版媒体工作台没有播放预览 | C++ 增加内嵌播放并保留输入参数；属于改善 | 用户明确不弹新窗口，最大化/缩放后仍居中 |

## 分轮回归记录

- `.local/full-parity-unit-20261114-012022/unit.json`：109 项通过，包含前一轮多结果 Session、独立播放器 token、导出/复制/历史基础。
- 后续加入的显示器变化、2×2、奇数尺寸、FFmpeg 错误细节、真实双隐藏 worker、history 字节预算、跨裁剪编辑历史尚需重新编译运行，不能借用上述报告。

2026-11-16补验：上述待联合回归项已在112项原生检查和实际媒体导出门禁中通过，详见[最新验证记录](PARITY_VALIDATION_2026-11-16.md)。覆盖多结果独立生命周期/解码上限、裁剪确认/取消、trim-loop与crop-aware历史、导出对话框对象退出、2×2及奇数尺寸、并发同名文件竞争、FFV1逐帧像素和合成单/双音轨。原生媒体尺寸解析另修复了把`0x1`流编号认作尺寸的问题。下方真实鼠标/中文输入/硬件队列仍保留，不由这些合成断言替代。

## 本轮必须保留的人工验收队列

1. 在合成测试画面录制 5 秒：准备栏开关、Start、边框、Stop、预览原位置与缩放，不录取个人桌面内容。
2. 拖动两个时间轴端点，通过 Tab/方向键调整，播放 3 次仅循环选段，导出 MP4/GIF/MKV 检查帧数。
3. 笔、高亮、局部橡皮→完成→Undo/Redo→再次进入编辑，历史保留；裁剪→预览画面→导出与所见一致。
4. 视频画面与工具条分别取得焦点，验证 Esc、Space、Ctrl+C/S/Z/Y；主程序与壁纸窗口不得误收动作。
5. 快复制不启动 FFmpeg，GIF 在支持的目标应用粘贴为动图；已有目标始终保留；目录跨重启记住。
6. 多结果独立生命周期、显示器断开、排除采集失败均已有本轮修复；合成测试不能代替真实拖动/显示器热插拔与停止入口验收。

实际麦克风/系统声音、显示器热插拔、第三方剪贴板粘贴均没有用 synthetic fixture 代替真实验收。最终证据需绑定新 EXE/UI DLL 摘要后再记录。
