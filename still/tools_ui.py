from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QKeySequence
from PySide6.QtWidgets import QApplication, QCheckBox, QFrame, QGridLayout, QHBoxLayout, QKeySequenceEdit, QScrollArea, QVBoxLayout, QWidget

from .i18n import tr
from .recording_options import choice, export_choices
from .ui import button, label


def card(title, description, action_text, callback, eyebrow=""):
    frame = QFrame()
    frame.setObjectName("panel")
    layout = QVBoxLayout(frame)
    layout.setContentsMargins(23, 21, 23, 21)
    layout.setSpacing(12)
    if eyebrow:
        layout.addWidget(label(eyebrow, "eyebrow"))
    layout.addWidget(label(title, "section"))
    layout.addWidget(label(description, "muted", True))
    layout.addStretch()
    layout.addWidget(button(action_text, callback))
    return frame


class ProductivityPage(QScrollArea):
    def __init__(self, window):
        super().__init__()
        self.setWidgetResizable(True)
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 7, 0)
        layout.setSpacing(18)
        layout.addWidget(label(tr("少一点切换，多一点顺手。"), "section"))
        grid = QGridLayout()
        grid.setSpacing(16)
        tools = [
            (tr("快速启动"), tr("搜索开始菜单应用、收纳文件与轻块桌面工具。直接输入算式即可计算并复制结果。"), tr("打开启动器  ↗"), "launcher", tr("01 / 启动")),
            (tr("剪贴板历史"), tr("本地保存文字和图片，搜索、固定与再次复制。默认关闭，开启后才开始记录。"), tr("管理剪贴板  ↗"), "clipboard", tr("02 / 收藏")),
            (tr("便签与任务"), tr("把想法写下来，设置到期提醒、完成状态或贴到桌面。内容自动保存，支持全文搜索。"), tr("打开便签任务  ↗"), "notes", tr("03 / 便签")),
            (tr("文件工作台"), tr("只读预览图片与文本、查看属性、计算校验值。批量重命名前先预览，并保留撤销日志。"), tr("预览与整理文件  ↗"), "files", tr("04 / 整理")),
            (tr("专注工作区"), tr("保存壁纸与分组布局，一键切换工作场景。专注和休息计时、暂停动态壁纸并记录完成情况。"), tr("进入专注工作区  ↗"), "focus", tr("05 / 专注")),
        ]
        for index, (title, description, text, action, eyebrow) in enumerate(tools):
            grid.addWidget(card(title, description, text, lambda checked=False, a=action: window.controller.get_workbench().invoke(a), eyebrow), index//2, index%2, 1, 2 if index == 4 else 1)
        layout.addLayout(grid)
        layout.addWidget(label(tr("所有工具在本机运行。录屏和媒体转换仅在使用期间启动独立进程；剪贴板记录可随时关闭。"), "muted", True))
        self.setWidget(page)


class CapturePage(QScrollArea):
    def __init__(self, window):
        super().__init__()
        self.window = window
        self.setWidgetResizable(True)
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 7, 0)
        layout.setSpacing(17)
        layout.addWidget(label(tr("截屏"), "section"))
        grid = QGridLayout()
        grid.setSpacing(15)
        tools = [
            (tr("开始截屏"), tr("按设定的快捷键选择区域。贴边图标栏可以标注、贴图、滚轮长截图和录屏。"), tr("打开截屏工具"), "capture", "CAPTURE"),
            (tr("媒体编辑"), tr("编辑剪贴板图片，或打开媒体工作室裁剪视频并导出 MP4、MKV 或 GIF。"), tr("打开媒体工作室"), "media", "EDIT"),
        ]
        for index, (title, description, text, action, eyebrow) in enumerate(tools):
            grid.addWidget(card(title, description, text, lambda checked=False, a=action: window.controller.get_workbench().invoke(a), eyebrow), index//2, index%2)
        layout.addLayout(grid)
        options = QFrame()
        options.setObjectName("panel")
        form = QVBoxLayout(options)
        form.setContentsMargins(22, 18, 22, 18)
        row = QHBoxLayout()
        self.capture_hotkey = QCheckBox(tr("全局截屏快捷键"))
        self.capture_hotkey.setChecked(window.store.settings.capture_hotkey)
        self.capture_hotkey.toggled.connect(lambda value: self.hotkeys_changed("capture_hotkey", value))
        row.addWidget(self.capture_hotkey)
        self.shortcut = QKeySequenceEdit(QKeySequence(window.store.settings.capture_shortcut))
        self.shortcut.setMaximumSequenceLength(1)
        self.shortcut.setAccessibleName(tr("自定义截屏快捷键"))
        self.shortcut.setToolTip(tr("点击后按下一组按键，再点击应用"))
        row.addWidget(self.shortcut, 1)
        row.addWidget(button(tr("应用"), self.apply_shortcut, "primary"))
        form.addLayout(row)
        self.shortcut_status = label(tr("可使用 Ctrl、Alt、Win 组合键或功能键。"), "muted", True)
        form.addWidget(self.shortcut_status)
        row = QHBoxLayout()
        self.hotkeys = QCheckBox(tr("其他全局快捷键"))
        self.hotkeys.setChecked(window.store.settings.hotkeys_enabled)
        self.hotkeys.toggled.connect(lambda value: self.hotkeys_changed("hotkeys_enabled", value))
        row.addWidget(self.hotkeys)
        form.addLayout(row)
        row = QHBoxLayout()
        settings = window.store.settings
        source = choice(tr("录屏源格式"), ((tr("高画质 MP4"), False), (tr("无损 MKV"), True)), settings.record_lossless)
        quality, fps = export_choices(settings)
        for field, key, title in ((source, "record_lossless", tr("录屏源格式")),
                                  (quality, "record_resolution", tr("导出画质")),
                                  (fps, "record_fps", tr("导出帧率"))):
            row.addWidget(label(title, "muted"))
            row.addWidget(field, 1)
            field.currentIndexChanged.connect(lambda _, k=key, f=field: self.option(k, f.currentData()))
        form.addLayout(row)
        form.addWidget(label(tr("画质和帧率会作为录屏预览的默认值，导出前可以调整。原始帧率以 24 fps 录制。"), "muted", True))
        form.addWidget(label(tr("截屏快捷键默认开启。录屏保留选区框，可用 Ctrl+Alt+R 结束。"), "muted", True))
        layout.addWidget(options)
        layout.addWidget(button(tr("编辑剪贴板图片"), lambda: window.controller.get_workbench().invoke("clipboard_image")))
        layout.addStretch()
        self.setWidget(page)

    def receive_hotkey(self, action):
        focus = QApplication.focusWidget()
        if focus is None or (focus is not self.shortcut and not self.shortcut.isAncestorOf(focus)):
            return False
        from .hotkeys import OTHER_KEYS
        value = self.window.store.settings.capture_shortcut if action == "capture" else next(
            label for _, key_action, label in OTHER_KEYS.values() if key_action == action)
        self.shortcut.setKeySequence(QKeySequence(value))
        return True

    def apply_shortcut(self):
        from .hotkeys import shortcut_label
        value = self.shortcut.keySequence().toString(QKeySequence.SequenceFormat.PortableText)
        try:
            self.window.controller.set_capture_shortcut(value)
        except (OSError, ValueError) as exc:
            self.shortcut_status.setText(str(exc))
            return
        self.shortcut_status.setText(tr("已保存截屏快捷键 {0}").format(shortcut_label(value)))

    def option(self, key, value):
        setattr(self.window.store.settings, key, value)
        self.window.store.save()

    def hotkeys_changed(self, key, value):
        self.option(key, value)
        self.window.controller.configure_hotkeys()
