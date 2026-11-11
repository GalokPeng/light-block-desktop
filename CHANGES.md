# v0.9.0 · 壁纸精选与交互优化 / Wallpapers and interaction improvements

- 原有三张内置静态壁纸替换为「夜驰」「银河」「山海」「海岸」四张 1920×1080 JPEG，保留原文件字节。现有 GIF、视频和应用布局不变，旧内置壁纸选择自动迁移。
- 官网更新无个人信息的中英文功能展示图，新增雾白／石墨明暗主题、跟随系统和记忆选择。
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
