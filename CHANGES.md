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
