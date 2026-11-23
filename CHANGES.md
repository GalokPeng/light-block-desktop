# v2.0.9 · 存储优化与剪贴板修复 / Smaller installs and more reliable clipboard history

- 精简安装文件。升级时校验并清理旧版程序组件和已知安装包缓存，保留用户壁纸、便签和配置。重复导入相同壁纸时复用已有文件。
- 从历史记录复制图片时先完成转换，转换失败会保留剪贴板原有内容。剪贴板被短暂占用时会自动重试。
- 支持仅提供 PNG 的剪贴板图片，保留透明度，可直接打开编辑。长截图历史支持最多 33,554,432 像素、PNG 最大 16 MiB，超限时会提示，仍可从系统剪贴板粘贴。
- 本页提供用于手动提交 Microsoft Store 的 x64 MSIX 和对应的 SHA-256 校验值。
- Smaller installs and verified cleanup of obsolete components and cached installers on upgrade. Wallpaper imports reuse identical files. Personal wallpapers, notes and settings are preserved.
- Copying an image from history prepares it before replacing the clipboard. History retries when another app briefly holds the clipboard.
- PNG-only images retain transparency and can be opened for editing. History supports long screenshots up to 33,554,432 pixels and 16 MiB of PNG data. Larger images remain available to paste and show a history-limit notice.
- The x64 MSIX for manual Microsoft Store submission is included with the release checksums.

# v2.0.8 · 修复截图复制粘贴问题 / Fix screenshot copy-paste

- 修复截图与贴图复制到系统剪贴板后，部分外部应用无法粘贴图片的问题。
- 修复剪贴板历史中的图片再次复制后无法在外部应用粘贴的问题。
- Fix an issue where copied screenshots and pinned images could not be pasted into some external applications.
- Fix the same clipboard-paste problem when copying an image from clipboard history.

# v2.0.7 · 重启后的桌面与旧版升级 / Desktop recovery and profile upgrades

- 关机或注销时提前恢复原有桌面图标显示状态，取消关机后继续收纳。登录时桌面目录暂不可用会重试，避免空扫描覆盖已有分组。
- 打开收纳图标时核对按下和松开时选中的项目。快捷方式改名保留扩展名，按回车确认文件改名。中断后恢复单文件改名时会校验文件身份。
- 修复早期便签数据库缺少尺寸列导致的升级启动失败。已有的便签内容、位置和尺寸会迁移，缺失的尺寸沿用旧版默认值。
- GIF 播放复用解码缓冲区，按局部帧范围备份像素。优化视频暂停和恢复处理，保留导入文件的原始画质。
- Restores the user's desktop icon visibility before shutdown or sign-out and resumes organizing after a cancelled shutdown. A temporarily unavailable desktop folder no longer clears saved entries.
- Organizer clicks verify the selected item after a layout change. Shortcut renames keep the file extension and require Enter to confirm. Interrupted single-file renames verify file identity during recovery.
- Fixes startup when migrating older notes databases with missing size columns. Existing note content and geometry are preserved, with legacy defaults for missing dimensions.
- Reuses GIF decode buffers, saves only the affected pixels when restoring a frame, and improves video pause and resume handling while retaining the original media files.

# v2.0.6 · 收纳底板与桌面层级 / Organizer plates and desktop layering

- 新增“图标底板不透明度”0–100% 滑块，独立于整个收纳背景；图标和文字保持原有清晰度。分组底板同步支持，调整即时生效并保存，旧配置默认保持原样。
- 修复收纳框第一次显示及运行中被抬到其他窗口前面的问题：约束首次显示之外的层级变更，避免鼠标激活自动置前，并修复此前只能向上、不能向下纠正层级的检查。
- 本次按维护者要求仅执行收纳透明度、设置和层级专项验证；不重复未改动的录屏、壁纸、任务栏及安装升级功能测试。发布保留完整构建、打包与公开资产校验。
- Adds an independent 0–100% icon plate opacity slider, including group plates, with immediate application and persistence. Icons and labels remain crisp; existing configurations keep their appearance.
- Guards organizer z-order changes throughout its lifetime, avoids activation raises and repairs drift both above and below the desktop layer.
- This patch uses focused organizer regression as requested. Unchanged features are not retested; build, package and public asset integrity checks remain in place.

# v2.0.5 · 背景透明度与全屏任务栏 / Background opacity and fullscreen taskbar

- 收纳背景使用独立透明度合成，图标、图标底板与文字保持清晰；抗锯齿圆角继续使用逐像素透明，收起后缩减绘制缓冲区。
- 触底唤出以显示器实际底边为准，并检查最近 350 毫秒的有限鼠标轨迹，避免快速移动在轮询间隙被漏掉；前台窗口切换后重新核对任务栏层级。
- 持续检查后来打开的最大化和贴满旧工作区的无边框窗口，补齐底部空间；通知应用更新工作区时避开 Explorer，防止它立刻重新占回任务栏预留区域。对拒绝调整的窗口限制重试次数，关闭美化时恢复工作区。
- 修复启动恢复便签和收纳时短暂遮挡其他应用：在首次显示前固定桌面层级，不激活窗口；主动打开设置与编辑便签仍按正常交互置前。
- 官网保留 v1.0.0 下载，并说明 v2.0.0 起采用 C++ 重构。没有同场景完整对照数据，未宣传普遍“内存减半”。
- Restored notes and the organizer are placed in the desktop layer before their first frame, without stealing focus. Explicitly opened settings and note editing retain normal foreground behavior.
- Organizer background opacity no longer fades icons, icon plates or labels. Antialiased corners retain per-pixel transparency; collapsed panels trim their drawing buffers.
- Taskbar reveal uses the physical monitor edge and a bounded 350 ms mouse history, with z-order rechecked across foreground transitions.
- Late maximized and work-area-filling borderless windows are reconciled with the expanded desktop. Notifications exclude Explorer to avoid immediately re-reserving the strip. Resize retries are bounded, and disabling styling restores the work area.

# v2.0.4 · 任务栏恢复与快捷收纳 / Taskbar recovery and quick collection

- 修复隐藏期间显示器/DPI 尺寸变化后，旧窗口裁剪矩形可能截掉任务栏右侧。恢复时解除本组件的整窗裁剪，开关时检查遗留的过期矩形；保留非本组件的偏移或复杂裁剪。
- 托盘临时零宽度、最大宽度为零、隐藏或透明时恢复可用布局，不再把这些异常值保存为退出后的原生状态。部分重建的布局列不覆盖原生恢复基线；恢复列、属性与布局完成后才确认关闭。
- 美化开启前检查上次布局会话，关闭时等待还原确认；超时保留记录并重试恢复，不直接丢弃会话。此修复不重启 Explorer，也不结束用户的其他应用。
- 最近 30 秒内在桌面右键后新增的内容优先显示在点击附近，并限制在显示器工作区内；没有有效位置时保留默认排布。气泡悬停显示“点击直接收纳”，点击立即收纳；拖出气泡释放会取消点击。一分钟自动收纳与原文件均保留。
- 自定义鼠标设计仅提供后续方案，不包含在本版功能中。
- 修复“检查更新 · 重试”：官网校验清单回退同时提供真实安装包大小，兼容已安装版本；客户端兼容旧接口的空大小，依次尝试两个官网域名及 GitHub，并限制请求总时长、记录失败原因。下载仍受大小上限、官方地址与 SHA-256 校验保护。
- Restores full taskbar visibility using current dimensions instead of replaying a stale pixel region after display/DPI changes. Toggle checks repair legacy rectangular clipping while preserving offset/complex regions.
- Repairs transient zero-sized, hidden or transparent trays without saving the broken state as the native restoration baseline. Partial column rebuilds cannot replace a valid baseline; layout restoration completes before Stop is acknowledged.
- Styling toggles recover recorded sessions and retain unacknowledged restoration for retry, without restarting Explorer or other applications.
- New items use a recent desktop right-click position (30 seconds), clamped to the work area, with the existing fallback otherwise. Hover the one-minute hint to collect immediately; release outside to cancel. Automatic collection and original files are preserved.
- Fixes update checks that repeatedly offered Retry: the official checksum feed now supplies the real installer size for existing clients. New clients accept the legacy unknown-size feed, try both official website domains and GitHub, bound request duration and retain failure details. Installer origin, size limits and SHA-256 verification remain enforced.

# v2.0.3 · 新增内容收纳与工具体验 / Desktop arrivals and tools

- 修复桌面主动新建、下载或复制新增内容的发现与显示：先显示临时桌面图标和“1 分钟后自动收纳”气泡，到时加入收纳框并更新布局。自动收纳默认开启，可在美化设置关闭；手动还原到桌面的项目不会被计时收回。菜单、拖动和属性对话框操作期间延后收纳。
- 收纳图标与临时桌面图标右键接入 Windows 原生菜单，支持属性、删除、重命名、剪切、复制及已安装的 Shell 扩展；保留分组和放入收纳操作。原生删除遵循 Windows 自身的确认与回收站行为。
- 原生菜单的重命名在图标附近直接输入，后台执行真实文件改名并保留分组，拒绝覆盖已有同名文件；修复退出过程中迟到的缩略图请求重新创建后台线程。
- 按 DPI 和实际显示尺寸缓存高分辨率图标，改善系统图标、缩放与悬停时的清晰度。第三方文件仍受其原始图标分辨率限制。
- 快速启动直接嵌入工具页面，新增明显的返回按钮和 Esc 返回。修复失焦后独立小窗口难关闭的问题。
- 目录监听先就绪再接收变化，缺失目录可恢复监听；连续新增采用有上限的合并刷新。后台启动不预热未显示的缩略图；快速启动按需索引，返回后停刷，闲置 30 秒释放索引、监听与图标缓存。
- Fixes manually created, downloaded and copied desktop items: show temporary icons with a one-minute collection hint, then update the organizer layout. Automatic collection remains optional; explicitly restored items stay on the desktop. Menus, dragging and owned dialogs defer collection safely.
- Organizer and temporary-item menus expose native Windows shell actions, including Properties, Delete, Rename, Cut, Copy and installed shell extensions, alongside grouping and collection actions.
- Native-menu Rename uses an inline editor and a background file operation, retaining grouping and refusing occupied targets. Late thumbnail requests cannot restart workers during shutdown.
- DPI-aware icon caching improves system icons and hover sharpness. Third-party icons remain limited by their source resolution.
- Embeds the launcher in Tools with a clear Back button and Esc navigation. Watches are armed before accepting changes and recover missing roots; bursts use bounded debounce. Background startup avoids unused thumbnail prewarming, and idle launcher resources are released after 30 seconds.

# v2.0.2 · 透明圆角、交互动画与在线图库 / Rounded surfaces, motion and wallpapers

- 重新处理收纳外框：移除会产生矩形残影的整窗系统亚克力层，改为逐像素透明的抗锯齿圆角和自绘柔光材质；保留颜色、透明度与任务栏避让。柔光为半透明渐变，不再使用系统实时背景模糊。
- 展开/收起从当前位置和速度继续，搜索标题区随布局平滑移动；增加按下反馈、悬停透明度过渡和设置开关动画。跟随系统减少动画偏好，静止后停止动画计时，复用绘制缓冲区。
- 在线图库增加 9 款 GIF、4 段原始 MP4，共 52 张静图、20 款 GIF、33 段视频。新 GIF 包括数字雨、磁带、月海、城市雨景、台风云眼、海上火山和地球晨昏；每项保留作者、许可、原始链接及真实可用热度。原文件至少 1920×1080、单个不超过 20 MB，不转换或放大凑数。
- Adds per-pixel antialiased organizer corners and a painted translucent material, eliminating the rectangular native-acrylic layer while retaining color, opacity and taskbar clearance. The soft material is a translucent gradient, not live OS background blur.
- Interruptible expansion, coordinated search-header motion, pressed/hover feedback and animated settings toggles respect reduced motion. Animation timers stop at rest and drawing buffers are reused.
- Adds nine original GIFs and four MP4 clips: 52 images, 20 GIFs and 33 videos total. HD originals remain byte-for-byte unchanged and under 20 MB. Author, license, source and available popularity evidence accompany each item; page views are not likes or downloads.

# v2.0.1 · 收纳布局与圆角修复 / Organizer layout and rounded surfaces

- 修复重新开启收纳、调整图标大小或展开收起后与任务栏重叠：按所在显示器的任务栏实际边界独立预留位置，普通窗口仍可铺满原工作区。上、中、下布局及自定义位置均限制在安全范围；任务栏位置变化时自动校准。
- 修复自绘圆角与系统非客户区边框叠加的角部残影，统一绘制与裁剪边界。收纳缩放期间只更新裁剪，不反复初始化玻璃效果；同样检查并处理截屏、录屏、长图工具栏与便签的自绘窗口边缘。
- 增加多 DPI、多显示器、四边任务栏、自动隐藏、反复尺寸切换和透明角像素回归。兼容 1.0.0 / 2.0.0 正式版覆盖升级，保留用户数据。
- Prevents organizer/taskbar overlap after reopening, resizing icons or expanding/collapsing, while retaining the full work area for other applications. Reserves the taskbar edge per monitor and reconciles layout changes.
- Removes competing system borders on custom rounded surfaces, aligns painted and clipped bounds, and avoids reinitializing acrylic during shelf resize animations. Covers capture/recording/scroll panels and notes.
- Adds DPI, multi-monitor, taskbar-edge, repeated resize and transparent-corner checks. Existing stable installs can upgrade in place with their data preserved.

# v2.0.0 · 更顺畅的桌面工作台 / A smoother desktop workspace

- 作为正式版提供壁纸、截屏录屏、桌面收纳、原生任务栏美化、便签与效率工具；安装包含播放、录制和导出组件，无需另装 FFmpeg 或虚拟声卡。
- 修复托盘退出停留在“正在结束后台任务”：任务栏回收不再被反复计时延期；退出前保存即时设置与便签，监督的媒体子进程同步回收。重复后台启动不再弹出设置。
- 截屏等待设置窗口隐藏和桌面合成帧完成，减少残影；本轮实机已由维护者确认。长图可直接完成贴图、滚动浏览、缩放和调整透明度；换页采集自动拼接或追加。
- 录屏可选择系统声音和麦克风，结束后贴屏预览、拖动时间轴逐帧裁剪、标注并导出 MP4/GIF/MKV。
- 在线图库 52 张静图、11 款 GIF、29 段视频。新增经授权、原文件尺寸和体积核验的 NASA 动态影像；每项不超过 20 MB，保留作者、许可和可核验热度。下载显示连接、进度、等待与校验状态。
- 兼容原正式版安装与更新入口。升级迁移设置、本地素材、便签、分组及支持的历史，保留旧数据与备份。官网提供 v1.0.0 历史下载，归档源码分支保留；测试版入口下架。
- Windows 10/11 x64；安装包目前未签名。实际多屏、Explorer 版本和音频驱动差异仍可能影响兼容性，可通过 Issues 反馈。

- Stable release with wallpapers, capture/recording, desktop organization, native taskbar styling, notes and local tools. Playback and export dependencies are included.
- Fixes shutdown getting stuck after restoring the desktop, saves pending edits before exit and reaps supervised media processes. Duplicate background starts stay quiet.
- Capture waits for the settings window to hide and compositor frames to finish. The maintainer verified the residual-shadow fix. Long captures finish as scrollable pins with zoom and opacity controls.
- Record system audio and microphone, preview in place, trim using frame-accurate handles, annotate and export MP4/GIF/MKV.
- The online gallery now includes 52 images, 11 GIFs and 29 videos. Licensed motion additions preserve original bytes, meet the HD and 20 MB limits, and include attribution and available popularity evidence.
- Upgrade-compatible Setup and update feeds preserve v1.0.0 data and backups. The website retains the v1.0.0 download and the original source branch; preview downloads are retired.
- Windows 10/11 x64. Setup is currently unsigned. Hardware, multi-monitor and Explorer/driver differences still require real-world compatibility testing.

# v1.1.0-beta.2 · 长截图与任务栏修复 / Capture and taskbar fixes

- 独立、不稳定的测试版；Python v1.0.0 继续作为正式版，main、正式安装和自动更新通道保持不变。请使用独立目录安装。
- 长截图未滚动、末帧无法匹配或读取失败时，也可完成并保留已采集内容。“采集下一屏”自动匹配，换页没有重叠时整页追加；移除手动拼接按钮。
- 完成长截图直接生成贴图。普通滚轮浏览、Ctrl+滚轮缩放、Shift+滚轮透明度，复制和保存保留整张原图；同步预览布局并复用绘图缓冲，修复缩略图与按钮重叠。
- 在线壁纸从连接开始显示等待时间、下载字节与进度；网络停滞和校验保存阶段有明确提示，可取消重试。新增32张有公开授权、原图尺寸和热度依据的静态壁纸，图库共52张静图、10个GIF、20个视频。
- 修复任务栏隐藏标记与实际区域不一致，以及系统重新预留底部工作区后未处理的问题。工作区每秒最多复核一次；隐藏时仅触及底边唤出，布局短暂重建期间继续检查显隐。
- 本地合成回归覆盖连续100次隐藏区域重置和100次工作区回退；真实Explorer长时间、多屏及其他任务栏工具兼容性仍需目标机器测试。安装包未签名。
- Separate, unstable preview. Python v1.0.0 remains the stable release; stable installs and updates are unchanged.
- Scroll capture can finish without scrolling or a matched terminal frame. Capture next stitches matching content or appends a new page; manual overlap controls are removed.
- Finished long captures open as scrollable pins with Ctrl-wheel zoom and Shift-wheel opacity. Full-image copy/save is preserved; thumbnail growth no longer overlaps the controls.
- Wallpaper downloads show connection time, byte progress, network stalls and verification. Adds 32 licensed static images, bringing the catalog to 52 images, 10 GIFs and 20 videos.
- Reconciles taskbar region/work-area resets and requires the screen edge to reveal a hidden bar. Synthetic regression coverage does not certify long-running Explorer, multi-monitor or third-party customization compatibility. Setup is unsigned.

# v1.1.0-beta.1 · 测试版（极度优化） / Preview (Extensive optimization)

- 独立 C++23 / Qt Widgets / Win32 测试分支，尚不稳定，以 Python 1.0.0 为功能与交互参照；不替换正式 `main`、官网、安装或更新渠道。
- 安装器、程序名、自启动与数据目录分离，不自动读取或迁移正式版用户数据。测试包仅发布为 Pre-release；正式版仍为 Python 1.0.0。
- 壁纸和工具页采用内嵌预览，截屏／录屏使用紧凑图标工具栏，设置即改即存；媒体工作台回到主窗口内。
- 贴图补齐普通滚轮透明度、Ctrl滚轮锚点缩放、原位编辑、可隐藏的圆角微光和非破坏裁剪；局部橡皮精确擦除标注层，矩形/自由马赛克保留独立参数。长图缩略图不再因GDI透明通道显示成白块，恢复4000万像素上限。
- 录屏补齐多结果独立预览、跨裁剪撤销/重做、逐帧双端时间轴和未修改文件直接复制；修复媒体尺寸误读MP4流编号、关闭编辑窗口后的迟到回调以及并发导出同名临时文件冲突。
- 修复录屏裁剪后工具栏被撑高、旧预览遮挡原位标注画面；内置FFmpeg提前刷新退出输出，发布时增加320次能力输出稳定性检查。
- 录屏准备栏可分别选择系统声音和麦克风，两项默认关闭，点击开始才采集。使用 Windows WASAPI，无需虚拟声卡；停止后复制视频流合并音轨，预览可试听／静音，MP4 与 MKV 导出保留声音，GIF 不含音轨。
- 音频按共同时间起点对齐，静音和缺口补零；权限不足、设备断开或默认设备切换会明确报错。驱动无响应时有界结束任务，后台只保留自身状态，不阻塞应用退出，也不删除仍在写入的缓存。
- 录屏编码线程和输入队列有上限，音频采集与合并在后台处理；新增合成单／双音轨、波形对齐、视频流完整性及音频线程超时回归。实机声音设备与音画启动延迟仍需在目标硬件上验证，不据此声称所有环境均能硬件编码或零时差。
- 美化折叠标题与开关分别居左、居右。收纳图标以小／中／大横向滑块调节，重算面板高度和布局，悬停放大留足边界。
- 显示器展示友好名称、分辨率和缩放，去除原始设备路径与重复主屏选项。页标题跟随导航，修正中英主题按钮的可用绘制空间；关于页恢复品牌、图标、说明与项目链接，并明确测试状态。
- 媒体隔离、数据迁移算法与故障恢复仍有专用夹具；迁移算法留作开发验证，本测试版不作为正式用户的升级入口。测试结果与未验证环境必须分别记录，不声称完整逐像素等价。
- Independent and unstable preview of the C++ implementation, using Python 1.0.0 as its interaction reference. The stable branch, website, installer identity and update feed remain unchanged.
- Separate installation/profile/autorun identifiers; prerelease assets only, with no automatic stable-profile migration or stable-user update delivery.
- Inline preview/tool pages, immediate settings, compact capture controls and an integrated media studio; improved appearance headers, discrete icon sizing, hover bounds, display names and bilingual chrome.
- Pins restore wheel opacity, cursor-anchored Ctrl-wheel zoom, in-place editing, optional rounded glow and non-destructive crop. Precise partial erasing and independent rectangular/freehand mosaic controls; scrolling thumbnails preserve opaque GDI pixels and the original 40-million-pixel limit.
- Recording restores independent result previews, crop-aware undo/redo, frame-accurate trim handles and unchanged-file copy. Fixes MP4 stream IDs mistaken for image dimensions, stale callbacks and concurrent export temporary-file collisions.
- Fixes oversized recording controls after crop and the previous preview occluding annotation editing. The bundled FFmpeg flushes output before DLL teardown; packaging verifies 320 capability responses without retries.
- Recording can capture system audio, microphone, or both through Windows WASAPI. Both inputs default off and open only on Start. Audio is combined without re-encoding the video; preview supports listening/muting, MP4/MKV keep audio, and GIF remains silent.
- Common-clock audio alignment, silence padding, explicit permission/device-change failures, bounded driver-timeout handling and bounded recording queues/threads. Synthetic audio, video-stream preservation and stalled-worker lifecycle tests cover these paths; real-device compatibility and startup synchronization still require hardware testing.
- Publication requires preview-specific checks. A successful build or synthetic fixture is not complete hardware or visual acceptance.

# v1.0.0 · 指针轮廓光与外观自定义 / Cursor outline glow and appearance

- “光芒”改为沿当前 Windows 指针的真实外轮廓流动，跟随箭头、文本、链接等角色与指针大小，仅绘制外缘，不覆盖本体。按形状缓存轮廓，避免逐帧读取位图，关闭释放资源。
- “飘落”使用更细小的荧光微粒、柔和淡入淡出、更轻的惯性与下落速度，取消十字闪烁。保留“点点”，三种效果可独立调色、自由搭配，静止后停绘。
- 三种不同轮廓的原创指针：流线（圆润实心）、棱镜（几何镂空）、像素（阶梯复古），覆盖 17 个角色和多 DPI 尺寸。支持自选颜色及 75%～200% 大小，退出恢复 Windows 方案；自选颜色后台生成并限量缓存。
- 导航统一为“壁纸／美化／截屏／工具／设置／关于”，鼠标与指针移入美化。新收纳配置默认居下，已有布局保留。
- 收纳图标底板与面板背景可独立调色及恢复默认，分组颜色保留；浅色面板自动适配标题、名称、返回与搜索。改色只重绘，不清空图标缓存或重建布局。
- 修复原生任务栏开启美化后隐藏圆角控件、圆角参数未接入的问题，三段背景同步实时调整；关闭美化时也可预设颜色、透明度和圆角。
- 修复便签自定义颜色标签截断，显示短标签、色块与完整色值提示。回归验证软件内更新按钮进度、校验换源、正常退出、静默安装和保留配置重启。
- 修复媒体退出迟到回调可能访问已销毁 QTimer 的问题，补充销毁阶段保护。
- 官网与中英文 README 新增鼠标、美化和配色介绍，更新隔离演示截图；不包含真实桌面或个人数据。
- Glow now follows the actual native cursor silhouette, including text and link roles, without painting over the cursor body. Cached outer-edge sprites avoid per-frame bitmap reads; owned GDI bitmaps are always released.
- Falling uses finer luminous specks, softer fades and gentler motion. Three structurally different original cursor themes support custom colors and 75%–200% sizes; effects are independently configurable and stop painting when idle.
- Adds independent organizer icon/panel colors with automatic text contrast, fixes live native-taskbar radius controls and custom-note color labels, reorganizes navigation, and updates bilingual website/README demonstrations. In-app progress, verified downloads and automatic installed-copy upgrades were regression-tested with isolated Setup fixtures.

# v0.9.3 · 壁纸切换稳定性与桌面优化 / Playback stability and desktop improvements

- 修复开启音轨后切换视频可能卡住整个设置窗口的问题。GIF、视频、在线动态预览和视频封面改由受保护的媒体进程处理，退出、加载和控制均有超时；主进程意外退出时回收播放器。
- 静图、GIF、视频统一管理切换顺序：旧桌面播放器退出后才打开下一个，首帧成功后才提交“已应用”；从在线预览应用也先等待预览退出。恢复、删除当前素材等待文件句柄释放，避免迟到回调提交旧结果。原文件画质、分辨率、帧率不变。
- 声音每次启动默认关闭，开启前提示确认；改变音轨状态会重新加载视频，避免热插拔正在使用的音频输出。补充切换与进程退出日志，修复日志文件可能为空的问题。
- 默认开启“自动收纳新增内容”：新增桌面项目保留一分钟后自动收纳，拖动、悬停或操作菜单时暂缓。使用单次截止定时器，不增加周期性桌面扫描。
- 桌面便签支持拖动边缘改变大小、长正文滚轮浏览且不显示滚动条、自选颜色；尺寸与位置保存。分组颜色同时作用于小卡片和展开面板。
- 桌面收纳设置分为收纳、任务栏和分组管理三个折叠区；兼容的 Windows 11 原生任务栏可调背景颜色和 0–100% 不透明度，文字与图标保持清晰。
- 新增原创“霜白、曜黑、薄荷”三套系统指针和可选鼠标粒子。指针由原生辅助程序管理，退出恢复系统方案；粒子停止移动后淡出停绘，关闭后释放监听。
- “播放与性能”改名“设置”，增加 Windows 原生夜间模式设置入口；已知不支持的系统、显示驱动、远程会话或策略环境禁用入口。开关、强度与计划由 Windows 页面管理。
- Fixes a reproduced settings-window hang while retiring an audio-enabled video. GIFs, videos, online motion previews and thumbnail decoding now run in supervised media processes with bounded startup, control and shutdown. Switching waits for the old renderer and commits only after a valid frame; current-file deletion waits for handles to close. Original media quality and frame rate are preserved.
- Wallpaper sound starts muted each session and requires confirmation to enable. Sound changes reload the video. Adds reliable lifecycle logs, one-minute automatic collection, resizable and scrollable notes with custom colors, visible group tinting and collapsible settings sections.
- Adds background color/opacity controls for compatible Windows 11 taskbars, three original cursor themes and an optional idle-stopping mouse trail. Playback Settings becomes Settings, with an availability-checked link to native Windows Night light controls.

# v0.9.1 · 静态壁纸切换卡死修复 / Static wallpaper switching fix

- 静态壁纸的原壁纸备份、全尺寸格式转换及 Windows 设置调用移至独立任务，避免系统或文件访问等待阻塞界面。任务具有准备及系统调用超时，退出时清理子进程。
- 保存原壁纸记录后才允许修改系统设置；成功后才更新当前壁纸并提示完成。切换期间禁止重复应用，失败可重试；删除当前壁纸等待恢复成功后再移除库内文件。
- 增加切换阶段与耗时日志。GIF／视频旧渲染器先断开输出及信号再释放。
- Static image preparation and Windows calls run in a bounded helper process. The GUI stays responsive, records the original before system changes, and reports success only after completion. Active-wallpaper deletion waits for restoration; failures allow retry. Switching stages and elapsed times are logged for diagnosis.

# v0.9.0 · 壁纸精选与交互优化 / Wallpapers and interaction improvements

- 原有三张内置静态壁纸替换为「夜驰」「银河」「山海」「海岸」四张 1920×1080 JPEG，保留原文件字节。现有 GIF、视频和应用布局不变，旧内置壁纸选择自动迁移。
- 官网更新无个人信息的中英文功能展示图，新增雾白／石墨明暗主题、跟随系统和记忆选择。
- 构建时为不可达的上游源码站增加已发布对应源码包备用来源，两层 SHA-256 校验保持原始源码一致。
- 优化录屏 GIF 导出：默认快速调色，另提供精细调色；在所选尺寸与帧率不变的情况下减少调色计算，限制抖色计算区域，两个阶段显示进度，保持流式内存使用。
- 录屏预览使用双端拖动裁剪条，点击手柄后左右键逐帧微调，Shift＋左右键移动十帧，显示毫秒级起止时间。
- 收纳开启期间新增的桌面文件先显示为可拖动的桌面图标，气泡提示右键放入收纳；分组模式可指定目标分组。原文件始终保留在桌面，待收纳状态随清单保存。
- 原生任务栏取消两端留白，在电源右侧增加任务栏设置、任务管理器图标按钮，再接原生搜索。三个按钮采用固定紧凑尺寸，不改变应用图标间距。驻留组件按构建标识隔离，更新时先恢复旧布局再应用新组件。
- 新增本地／在线壁纸切换，仅保留静态、GIF、视频三个入口，支持中英标题及标签搜索、页内预览、原文件下载、进度取消和下载状态同步。
- 在线精选包含静态 20 张、GIF 10 款、视频 20 段，全部原生至少 1920 × 1080、单项不超过 20 MB。按维护者确认保留本地复看版的 10 款 GIF；GIF 不设点赞／收藏门槛，仍要求原始 1080p、16:9 多帧循环动画。静图／视频继续按公开支持票或收藏结合观感筛选，详情保留公开统计与依据链接。不自行压缩、放大或裁短凑数。
- 移除原 300 项和内容分类，schema 4 拒绝旧目录及大文件缓存恢复条目，并强制体积、像素、数量与热度证据。新增 Internet Archive 原作者 CC BY 3.0 高清视频来源及 README 友链，支持原始 WMV、OGV 播放与识别。
- 来源提供的较小视频预览使用独立缓存；下载原文件不会复用较小预览，保留原文件字节、分辨率与来源记录。
- GIF 列表封面在取得首个完整画面后停止传输，避免加载整个动画；同一时刻只保留一个主动预览。
- 动态预览按需缓存后播放，限制缓存容量；正式下载复用并重新校验预览缓存，遇到来源限流时暂停请求。停止预览或离开页面释放播放器。
- Replaces only the three bundled static wallpapers with four maintainer-supplied, byte-preserved 1920×1080 JPEGs. Existing GIF/video assets and application layout remain unchanged. The website has new bilingual demo images and light/dark/system themes.
- Adds a reviewed URL-only gallery with 20 images, 10 GIFs and 20 videos, all at least native 1920 × 1080 and within 20 MB. The maintainer approved retaining the ten GIFs from the local review build; GIFs require native 1080p, 16:9 looping frames, without a likes/favorites threshold. Images/videos retain their public-vote or favorite-based selection; available metrics accompany all attribution records. Schema 4 retires the old 300 entries, content categories and oversized selections. Original GIF, WMV, WebM and OGV bytes are preserved without app-side recompression.
- Motion previews use bounded, verified local caches. Original downloads can reuse and reverify cached bytes. Rate-limited sources are paused; stopping previews releases their players.

# v0.8.1 · 更新重启与启动命令修复 / Update restart and startup commands

- 包含 v0.8.0 的软件内更新、官网分语言下载、壁纸右键删除与任务栏稳定性改进。旧版本首次取得更新器仍需手动安装一次。
- 更新启动器清除旧程序继承的 DLL 搜索路径，重启时使用独立的打包运行环境。
- 接收程序启动期间已排队的进程间命令；发送端等待接收端处理后再关闭连接，改善首次打开设置与退出命令的可靠性。
- The update launcher clears inherited DLL search paths and restarts the application with an independent frozen environment.
- Includes v0.8.0 in-app updates, language-aware website downloads, wallpaper deletion and taskbar stability improvements. Older versions need one manual installation to obtain the updater.
- Startup drains already-queued instance commands, and senders wait for the receiver to consume them before closing the connection. This improves the reliability of initial settings and quit commands.

# v0.8.0 · 软件内更新与任务栏稳定性 / In-app updates and taskbar stability

- 点击「立即更新」直接下载安装包，按钮显示进度，支持取消、失败重试和低速换源。安装包通过官方 SHA-256 校验后，软件正常退出，再启动 Setup；安装版沿用原目录并保留配置，完成后重新打开。便携版和源码运行进入正常安装向导。
- 官网英文默认 GitHub 官方，中文默认 `gh-proxy.com`，可切换 `ghfast.top` 或官方源；下载地址随最新正式版更新。
- 原生任务栏在运行中检测宽度、对齐和控件重建并恢复三段布局，背景跟随实际图标范围。持续冲突时恢复原生任务栏并记录诊断，修复全屏唤出后偶尔落到窗口后方的问题。
- 壁纸库支持右键删除自己导入的壁纸，只删除库内副本并保留原文件。删除正在使用的壁纸时先恢复 Windows 壁纸，释放 GIF 预览和视频封面解码器。
- 从旧版本首次升级到此版本，需先下载安装一次。之后可在软件内完成更新。在线图库目前仍为方案，尚未接入外部素材。
- Click Update now to download Setup with button progress, cancellation, retries and slow-source fallback. The app verifies the official SHA-256, exits normally, then starts Setup. Installed copies update in place, preserve their configuration and restart. Portable and source copies open the installation wizard.
- The website defaults to GitHub for English and `gh-proxy.com` for Chinese, with selectable `ghfast.top` and official sources. Downloads track the latest stable release.
- Native taskbar styling detects runtime width, alignment and control changes and restores the centered layout. Backgrounds follow the actual buttons. Repeated conflicts restore the native taskbar with diagnostics. Fixed edge reveal sometimes remaining behind fullscreen windows.
- Right-click an imported wallpaper to delete its library copy while keeping the original. Deleting the active wallpaper first restores Windows wallpaper and releases GIF preview and video-cover decoders.
- Older versions need one manual installation to obtain the updater. Subsequent updates can run inside the app. Online wallpaper browsing remains a design proposal.

# v0.7.4 · 安装后直接录屏 / Recording ready after installation

- Windows 安装包和免安装版内置 FFmpeg。安装或解压后即可录屏，并导出 MP4、GIF 和无损 MKV。
- 修复停止录屏后预览打不开的问题。
- 设置窗口标题和侧栏收起版本号，版本信息集中到「关于」。更新按钮显示当前更新状态。
- Windows Setup and the portable package include FFmpeg for recording and MP4, GIF, and lossless MKV export.
- Fixed a failure to open the preview after stopping a recording.
- The About page shows the version number. The settings window title and update button show the app name and update status respectively.

# v0.7.3 · 桌面便签与壁纸体验 / Desktop notes and wallpaper improvements

- 右键桌面空白处可新建便签并直接贴到桌面，也可切换视频壁纸声音。声音默认关闭，与「播放与性能」同步。Windows 11 从「显示更多选项」进入。
- 壁纸库保存原画质副本，视频封面从实际画面生成。默认 GIF 更新为 25 帧/秒的「星轨弧光」。
- 配置和壁纸库默认保存在安装目录的 `data`。首次启动复制旧配置，安装目录不可写时沿用原数据目录。
- 录屏预览采用「截屏」设置中的画质和帧率，导出前仍可调整。
- 修复明暗主题切换报错，「关于」页新增 GitHub 项目入口。
- Right-click the desktop background to pin a new note or toggle video wallpaper sound. Sound is off by default and stays in sync with Playback Settings. On Windows 11, open Show more options.
- The wallpaper library saves full-quality copies and generates covers from video frames. Orbital Glow replaces the default GIF with a 25 fps loop.
- Settings and wallpapers live in the `data` folder beside the app. The first launch copies existing settings. A read-only app folder keeps using the previous location.
- Recording previews inherit the quality and frame rate chosen in Screen Capture settings. Both can be adjusted before export.
- Fixed an error when changing the theme. About now links to the GitHub project.

# v0.7.2 · 原生任务栏优化 / Native taskbar improvements

- Windows 11 x64 主屏的电源与搜索、应用和系统托盘现在由同一条原生任务栏居中显示，保留 Windows 搜索、应用切换和托盘菜单。
- 全屏、最大化或窗口遮挡底栏时三段一起隐藏。鼠标触底唤出且保留当前应用焦点，回到桌面时恢复显示。已开启 Windows 自动隐藏时沿用系统的显隐行为。
- 关闭美化或退出时恢复原生布局和工作区。较早版本的 Windows 保留圆角和留边美化。
- On Windows 11 x64, Power and Search, apps, and the system tray are centered on the same native taskbar on the primary display. Windows Search, app switching, and tray menus stay native.
- All sections hide together when fullscreen, maximized, or other windows cover the bar. Move the pointer to the bottom edge to reveal it without moving focus. The bar returns on the desktop. If Windows auto-hide is enabled, the bar follows that setting.
- Turning off taskbar styling or exiting restores the original layout and work area. Earlier versions of Windows retain rounded taskbar styling.

# v0.7.1 · 显示器切换修复 / Display switching fix

- 修复显示器断开或切换主屏时，Dock 刷新可能反复提示「操作未完成」的问题。显示布局稳定后，Dock 与托盘会重新定位。
- 截屏会结束已失效的选区。长截图保留已采集的内容，进行中的录屏会结束并保留已录片段。
- Fixed repeated error notifications from the Dock when a display disconnects or the primary display changes. The Dock and tray reposition once the display layout settles.
- Screen capture closes an invalid selection. Scrolling capture keeps the images already collected, and an active recording finishes with the footage already captured.

# v0.7.0 · 桌面收纳与录屏贴屏预览 / Desktop organizer and pinned recording previews

- 桌面收纳支持居上、居中和居下，可分别按名称、修改时间或分组排列。新增桌面文件、快捷方式和文件夹会自动归类，收纳栏内可直接搜索。
- Dock 与原生托盘联动显隐。应用窗口覆盖底栏时自动隐藏，鼠标触底唤出。支持独立设置底栏颜色、透明度和磨砂效果。
- 录屏结束后在选区贴屏预览。工具栏支持涂鸦、画面裁剪、截取时间片段和撤销，可按所选画质与帧率保存或复制 MP4、GIF 和无损 MKV。
- 截图贴图沿用选区的原位置和尺寸，边框为淡荧光。通过系统托盘打开设置时，窗口会切到前台并保留原有最大化状态。
- Place the organizer at the top, center or bottom of the desktop, and sort each layout by name, modification time or group. New desktop items are grouped automatically and can be found with the built-in search.
- The Dock and native tray coordinate visibility when windows cover the bottom edge. Move the pointer to the edge to reveal them. Customize the bar's color, opacity and frosted glass independently of the settings theme.
- Finished recordings play in a pinned preview. Draw, crop, trim and undo edits, then save or copy MP4, GIF or lossless MKV at the selected quality and frame rate.
- Pinned screenshots keep the selection's position and size and use a subtle glow. Opening settings from the system tray brings the window forward and preserves its maximized state.

# v0.6.0 · 任务栏美化 / Taskbar Dock

- 在「桌面收纳」中开启任务栏美化。主屏底部居中排列电源与搜索、应用 Dock 和原生系统托盘。
- Dock 支持固定应用、拖动排序和切换多个窗口。搜索打开 Windows 原生面板，托盘保留系统菜单和输入法指示。
- 功能默认关闭。关闭美化或退出后恢复原生任务栏。目前支持主屏底部布局，需关闭 Windows 任务栏自动隐藏。
- Enable Taskbar Style in Desktop Organizer to center power and search, the app dock, and the native system tray on the primary display.
- Pin and reorder apps, and switch between multiple windows. Search opens the Windows panel, and the tray keeps its system menus and input indicator.
- Taskbar Style is off by default. Turning it off or exiting restores the original taskbar. It supports the primary display's bottom taskbar with Windows auto-hide turned off.

# v0.5.1 · 截屏工具栏、贴图编辑与更新检查 / Capture tools, pin editing and update checks

- 截屏工具按功能显示独立选项，矩形与圆形支持填充和虚实线，矩形支持圆角。马赛克支持圆角框选与画笔涂抹。
- 贴图支持 Ctrl + 滚轮缩放。右键可在原窗口编辑，复制和导出保留原图分辨率。
- 设置可以在系统语言、简体中文和英文之间切换。明暗主题同步到 Windows 窗框，左下角显示更新状态。
- 收纳默认居中，右键开启移动后可拖动面板。小收纳框支持拖动排序，位置和顺序会保存。
- 官网和软件共用安装包下载入口，优先使用 GitHub 官方源，失败时尝试经 SHA-256 校验的公开加速源。
- Capture tools have their own settings. Rectangles and ellipses support fill and dashed or solid outlines. Mosaic supports rounded selections and brush strokes.
- Use Ctrl + scroll to zoom a pinned image, and right-click to edit it in place. Copies and exports retain the original resolution.
- Settings can follow the system language or use Simplified Chinese or English. The native frame follows the app theme, and the sidebar shows update status.
- Desktop collections start centered. Right-click to enable panel movement and drag folders to reorder them. Both settings are saved.
- Website and app downloads use GitHub first and fall back to public mirrors with SHA-256 verification.

# v0.5.0 · 中英文界面与安装向导 / Bilingual app and Setup

- 程序启动时按 Windows 显示语言切换简体中文与英文。覆盖设置、托盘、收纳、截屏、录屏与效率工具。
- Setup 自动匹配简体中文、繁体中文或英文。升级沿用旧版配置，默认安装路径保持兼容。
- 更新中英文 README 和使用指南，新增真实程序界面演示、GitHub Star 趋势和双语产品官网源码。
- The app follows the Windows display language for its Settings, tray, organizer, capture and productivity tools.
- Setup automatically selects English, Simplified Chinese or Traditional Chinese and preserves existing settings.
- Added bilingual guides, privacy-safe screenshots, GitHub Star history and a bilingual product site.

# v0.4.5 · MIT 源码与 Windows Setup

- 应用源码改用 MIT，Qt 绑定迁到 PySide6。沿用旧版设置、壁纸库和收纳清单。
- 增加二开 Skill、上游 PR 约定和 Windows 安装包。录屏和导出通过单独安装的 FFmpeg 命令行工具运行。
- GitHub Actions 验证版本后生成 Setup、免安装包、源码包和 SHA-256。启用发布后，递增版本并推送到 main 即可创建 Release。

# v0.4.4 · 截屏热键、标注滚轮与桌面便签

- 桌面收纳隐藏滚动条，保留滚轮浏览。桌面便签使用与收纳一致的半透明卡片，并记住拖动位置。
- 截屏页可更改全局截屏快捷键。注册冲突时保留旧键，托盘和设置页同步显示当前组合。
- 设置窗口以太阳和月亮图标一键切换明暗主题。
- 普通截屏中，滚轮可调光标所指的文字、箭头、直线、画笔、矩形、高亮或序号。选区大小由原有手柄调整，长截图滚轮用于采集。

# v0.4.3 · 轻块桌面

- 软件更名为「轻块桌面」，设置窗口右上角可切换并记住深色或浅色外观。
- 关于页说明更新为「纯属个人娱乐，有BUG随便提issues，自己用得不顺就改改」。
- 收纳标题和分组名称支持点击后直接编辑，允许留空。收纳主页取消右上角按钮和文件夹总数，右键可添加新分组。

# v0.4.2 · 收纳过渡与右键重选

- 收纳展开与收回共用固定大小的原生窗口。过渡中只绘制一页内容，取消图标和文字交替变暗。
- 大分组由列表模型提供条目。仅解析视口内未缓存的图标，滚动后按需加载。
- 截屏时右键清空选区与标注并回到框选。尚未框选时右键退出，也可在拉框过程中右键中断本次拉框。

# v0.4.1 · 收纳动画与截屏体验

- 用预先排版的图标画面做收纳过渡，动画期间保持原生窗口和内容布局稳定。展开后分批加载未缓存的文件图标。
- 长图缩略图随拼接高度伸缩，合并预览刷新。支持捕获排除的 Windows 在预览面板可见时即可采集。
- 截屏、长截图和录屏结束后，设置窗口留在后台。初始拉框保留屏幕原亮度。

# v0.4.0 · 收纳动效、滚动拼接和行内文字

- 收纳网格在点击空白、点到面板外或成功打开项目后自动收回。展开和收回可以从当前动画位置接续，支持 Windows 减少动画设置。
- 长截图在滚动中采样，按多列内容寻找接缝，容忍局部刷新，固定页脚只保留一次。匹配失败可回滚后自动接续。
- 截图选区和截图工作室直接编辑文字。点击空白提交，拖动调整位置，双击继续编辑，工具栏可设置字体、字号、颜色和字形。

# v0.3.0 · 统一截屏与桌面文件夹

- 参照选区示意改为贴边图标栏和蓝色手柄。可选项用只读下拉框。
- 录屏保持蓝色选区边框。长截图随选区内滚轮滚动自动采集。
- 居中手机式文件夹总览，点击放大应用网格，鼠标移出时面板及文字一起淡化。
- 通过 Windows 原生视图开关隐藏和恢复桌面图标。

# v0.2.0 · 截图选区与桌面自动分类

- Ctrl+Alt+A 默认开启。小工具栏提供智能窗口、手动区域和当前屏幕三种选区方式，支持拖动和八个缩放手柄。
- 选区工具栏内可绘制高亮、直线、箭头、矩形、文字、序号和马赛克，并直接贴图、复制、保存或切换到录屏和长截图。
- 双击贴图关闭并复制，右键切换边框。录屏完成后保存或复制 GIF、MP4，长截图显示实时拼接缩略图。
- 首次打开收纳时按类型和文件后缀归类桌面入口。透明纱窗面板折叠显示微图标，展开独立选择小、中、大。

# v0.1.0 — 桌面工作台

- 可选原生玻璃桌面收纳：分组、命名、拖放、排序、折叠、锁定、缩放、透明度与强调色；入口引用和可还原的桌面文件托管。
- 独立清单、备份、逐文件恢复凭据和同名冲突保护；仅同盘移动，关闭面板不丢分组。
- 截图、标注、马赛克、裁剪、撤销 / 重做、复制和 PNG 保存；可移动、缩放、调透明度的贴图。
- 手动滚动分段采集，可靠重叠匹配和手动回退的长截图。
- 区域静音录屏，高质量 MP4 / 无损 MKV；视频裁剪、截取、两遍调色板 GIF 导出；附带 FFmpeg，无需另外安装。
- 五个按需效率模块：启动器、剪贴板历史、便签任务、文件工作台、专注工作区。
- 文件后台扫描与分页；批量重命名预览、持久撤销、崩溃恢复、再次中断恢复及硬链接身份保护。
- 可选全局快捷键、托盘录屏停止、退出等待录屏收尾、线程安全释放、收纳和便签桌面所有者重建修复。
- 原生矢量导航图标，FreeType 字体后端保留多语言支持并降低 DirectWrite 字体库开销。GIF 保持 v0.0.2 已验证的原生媒体输出。
- 三位独立 reviewer 全部通过；具体范围和实测见 AUDIT.md / TEST_REPORT.md。

升级：退出旧版，完整解压后运行新版。配置和素材库保留；需在新版重新启用自启动以更新路径。收纳、剪贴板和全局快捷键默认关闭。

# v0.0.2 — GIF 显示与动态预览修复

- 修复 GIF 挂到 WorkerW 后不显示：原来使用普通 QWidget 光栅绘制，即使收到 paintEvent 也没有正确显示到 Explorer 桌面合成层。现在 GIF 和已正常显示的视频共用 QMediaPlayer / QVideoWidget 原生媒体输出。
- FFmpeg 直接解码原 GIF，保持源分辨率和原帧时间，不生成临时视频、不转码，不改写原素材。循环、静音、暂停和恢复与视频共用同一实现。
- 设置界面的 GIF 封面改为动态预览。预览保留 CacheNone；离开壁纸库、隐藏窗口时暂停，切换素材时释放旧播放器。桌面继续读取原分辨率素材，预览缩放不影响桌面画质。
- 增加 GIF 必须使用原生视频输出的回归测试，检查多个有效帧、变化的像素、原始尺寸、直接读取原路径、反复应用、循环及暂停/继续。原本仅检查解码状态或 paintEvent 均不足以证明桌面显示，本版不再使用该光栅路径。
- 用内置 GIF 和原先导入的本地 GIF 验证解码、循环。用户已在实际桌面确认新通道能够显示并播放；用户素材不纳入发行包。
- 动态壁纸支持填充、完整显示、拉伸。原始尺寸选项仅适用于静态图片。

升级：完整解压 v0.0.2，退出旧版后运行新版 Still.exe。壁纸库和设置沿用 `%LOCALAPPDATA%\StillWallpaper`，无需重新导入。
如果开机自启原先指向旧目录，请在新版里重新启用开关，以更新程序路径。
