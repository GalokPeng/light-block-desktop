from __future__ import annotations

from PySide6.QtCore import QSignalBlocker, Qt, QTimer
from PySide6.QtWidgets import (QColorDialog, QComboBox, QFrame, QHBoxLayout, QInputDialog, QLineEdit, QListWidget,
    QListWidgetItem, QScrollArea, QSlider, QVBoxLayout, QWidget)

from .i18n import tr
from .model import TASKBAR_RADIUS_LIMITS
from .organizer_model import SYSTEM_ENTRIES, group_name, organizer_view, view_options
from .ui import Toggle, button, label
from .settings_sections import SettingsSection
from PySide6.QtGui import QColor


class OrganizerPage(QWidget):
    def __init__(self, window):
        super().__init__()
        self.window = window
        self.manager = window.controller.get_organizer()
        self.updating = False
        self.selected_id = ""
        settings = window.store.settings
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        canvas = QWidget()
        cards = QVBoxLayout(canvas)
        cards.setContentsMargins(0, 0, 6, 0)
        cards.setSpacing(14)
        self.enabled = Toggle(self.manager.enabled)
        self.enabled.setAccessibleName(tr("开启桌面收纳"))
        self.enabled.toggled.connect(self.toggle)
        desktop = SettingsSection(tr("桌面收纳设置"), tr("收纳布局、图标与新增内容的处理方式。"), extra=self.enabled)
        cards.addWidget(desktop)
        self.desktop_section = desktop
        self.alignment = QComboBox()
        for key, title in (("top", tr("居上")), ("center", tr("居中")), ("bottom", tr("居下"))):
            self.alignment.addItem(title, key)
        self.alignment.currentIndexChanged.connect(lambda: self.global_option("organizer_alignment", self.alignment.currentData()))
        self.field(desktop.body, tr("桌面布局"), self.alignment)
        self.view = QComboBox()
        for key, title in view_options():
            self.view.addItem(title, key)
        self.view.currentIndexChanged.connect(self.set_view)
        self.field(desktop.body, tr("排列方式"), self.view)
        self.icon_size = QSlider(Qt.Orientation.Horizontal)
        self.icon_size.setAccessibleName(tr("桌面图标大小"))
        self.icon_size.setRange(0, 2)
        self.icon_size.setTickPosition(QSlider.TickPosition.TicksBelow)
        self.icon_size.setTickInterval(1)
        self.icon_size.valueChanged.connect(lambda value: self.global_option("organizer_icon_size", (32, 44, 60)[value]))
        self.field(desktop.body, tr("图标大小 · 小 / 中 / 大"), self.icon_size)
        self.auto_collect = Toggle(settings.organizer_auto_collect)
        self.auto_collect.setAccessibleName(tr("自动收纳新增内容"))
        self.auto_collect.toggled.connect(lambda value: self.global_option("organizer_auto_collect", value))
        self.field(desktop.body, tr("自动收纳新增内容"), self.auto_collect)
        desktop.body.addWidget(label(tr("新增内容先留在桌面，1 分钟后放入收纳。正在拖动或操作菜单时暂缓。"), "muted", True))
        self.opacity, self.opacity_save = self.slider("organizer_opacity", 75, 235, settings.organizer_opacity)
        self.field(desktop.body, tr("面板透明度"), self.opacity)
        desktop.body.addWidget(label(tr("关闭收纳会恢复 Windows 原有图标显示状态，原文件始终保留。"), "muted", True))

        self.taskbar_toggle = Toggle(settings.taskbar_style_enabled)
        self.taskbar_toggle.setAccessibleName(tr("开启任务栏美化"))
        self.taskbar_toggle.toggled.connect(window.controller.set_taskbar_style)
        taskbar = SettingsSection(tr("任务栏设置"), extra=self.taskbar_toggle)
        self.taskbar_section = taskbar
        cards.addWidget(taskbar)
        self.taskbar_intro = label("", "muted", True)
        taskbar.body.addWidget(self.taskbar_intro)
        self.taskbar_color = button("", self.choose_taskbar_color)
        self.field(taskbar.body, tr("背景颜色"), self.taskbar_color)
        self.taskbar_opacity, self.taskbar_opacity_save = self.slider("taskbar_opacity", 0, 100, settings.taskbar_opacity)
        self.taskbar_opacity_label = label("", "muted")
        taskbar.body.addWidget(self.taskbar_opacity_label)
        taskbar.body.addWidget(self.taskbar_opacity)
        self.taskbar_opacity.valueChanged.connect(lambda value: self.taskbar_opacity_label.setText(tr("背景不透明度 · {0}%").format(value)))
        self.taskbar_radius, self.taskbar_radius_save = self.slider("taskbar_radius", *TASKBAR_RADIUS_LIMITS, settings.taskbar_radius)
        self.taskbar_radius_label = label("", "muted")
        self.taskbar_radius.setAccessibleName(tr("任务栏圆角"))
        self.taskbar_radius.valueChanged.connect(lambda value: self.taskbar_radius_label.setText(tr("任务栏圆角 · {0}").format(value)))
        taskbar.body.addWidget(self.taskbar_radius_label)
        taskbar.body.addWidget(self.taskbar_radius)
        self.taskbar_status = label("", "muted", True)
        taskbar.body.addWidget(self.taskbar_status)
        taskbar.body.addWidget(label(tr("调色仅作用于背景，文字、应用图标与托盘保持清晰。"), "muted", True))

        groups = SettingsSection(tr("分组管理"), tr("重命名、选择分组颜色，或添加应用和文件。"))
        self.groups_section = groups
        cards.addWidget(groups)
        heading = QHBoxLayout()
        heading.addWidget(button(tr("同步分类"), self.sync_desktop))
        heading.addStretch()
        heading.addWidget(button(tr("＋ 新建"), self.create, "primary"))
        groups.body.addLayout(heading)
        columns = QHBoxLayout()
        self.groups = QListWidget()
        self.groups.setSpacing(7)
        self.groups.setMinimumWidth(235)
        self.groups.setMaximumHeight(225)
        self.groups.setObjectName("collectionList")
        self.groups.currentItemChanged.connect(self.group_selected)
        columns.addWidget(self.groups, 1)
        editor = QVBoxLayout()
        self.name = QLineEdit()
        self.name.setPlaceholderText(tr("分组名称"))
        self.name.setMaxLength(40)
        self.name.setMinimumHeight(34)
        self.name.editingFinished.connect(self.save_name)
        editor.addWidget(self.name)
        self.color = QComboBox()
        for key, title in (("mint", tr("薄荷绿")), ("blue", tr("海盐蓝")), ("violet", tr("鸢尾紫")), ("amber", tr("暖砂金")), ("rose", tr("蔷薇粉"))):
            self.color.addItem(title, key)
        self.color.currentIndexChanged.connect(lambda: self.group_option("color", self.color.currentData()))
        editor.addWidget(self.color)
        editor.addWidget(button(tr("重命名"), self.rename))
        editor.addWidget(button(tr("移除分组"), self.remove))
        editor.addStretch()
        columns.addLayout(editor, 1)
        groups.body.addLayout(columns)
        actions = QHBoxLayout()
        actions.addWidget(button(tr("＋ 添加应用 / 文件"), self.files, "primary"))
        actions.addWidget(button(tr("添加文件夹"), self.folder))
        actions.addWidget(button(tr("添加系统入口"), self.system_entry))
        groups.body.addLayout(actions)
        groups.body.addWidget(button(tr("还原旧版文件"), self.restore_all))
        cards.addStretch()
        scroll.setWidget(canvas)
        outer.addWidget(scroll)
        self.manager.changed.connect(self.refresh)
        self.refresh()

    @staticmethod
    def field(layout, title, control):
        row = QHBoxLayout()
        row.addWidget(label(title, "muted"), 1)
        row.addWidget(control, 1 if isinstance(control, (QComboBox, QSlider)) else 0)
        layout.addLayout(row)

    def slider(self, key, low, high, value):
        slider = QSlider(Qt.Orientation.Horizontal)
        slider.setRange(low, high)
        slider.setValue(value)
        timer = QTimer(self, singleShot=True, interval=180)
        timer.timeout.connect(lambda: self.global_option(key, slider.value()))
        slider.valueChanged.connect(lambda _: timer.start() if not self.updating else None)
        def finish():
            timer.stop()
            self.global_option(key, slider.value())
        slider.sliderReleased.connect(finish)
        return slider, timer

    def choose_taskbar_color(self):
        color = QColorDialog.getColor(QColor(self.window.store.settings.taskbar_color), self, tr("任务栏背景颜色"))
        if color.isValid():
            self.global_option("taskbar_color", color.name())
            self.refresh_taskbar_options()


    def group(self):
        return self.manager.collections.find(self.selected_id)

    def refresh(self):
        self.updating = True
        self.enabled.setChecked(self.manager.enabled)
        settings = self.manager.store.settings
        self.alignment.setCurrentIndex(self.alignment.findData(settings.organizer_alignment))
        self.view.setCurrentIndex(self.view.findData(organizer_view(settings)))
        self.icon_size.setValue((32, 44, 60).index(settings.organizer_icon_size or 44))
        self.auto_collect.setChecked(settings.organizer_auto_collect)
        if not self.opacity_save.isActive():
            self.opacity.setValue(settings.organizer_opacity)
        self.refresh_taskbar_options()
        self.groups.clear()
        for group in self.manager.collections.groups:
            item = QListWidgetItem(tr("{0}\n{1} 个项目").format(group_name(group), len(group['entries'])))
            item.setData(Qt.ItemDataRole.UserRole, group["id"])
            self.groups.addItem(item)
            if group["id"] == self.selected_id:
                self.groups.setCurrentItem(item)
        if not self.groups.currentItem() and self.groups.count():
            self.groups.setCurrentRow(0)
        self.updating = False
        self.group_selected(self.groups.currentItem())

    def refresh_taskbar_options(self):
        controller = self.window.controller
        settings = controller.store.settings
        with QSignalBlocker(self.taskbar_toggle):
            self.taskbar_toggle.setChecked(settings.taskbar_style_enabled)
        if not self.taskbar_radius_save.isActive():
            with QSignalBlocker(self.taskbar_radius):
                self.taskbar_radius.setValue(settings.taskbar_radius)
            self.taskbar_radius_label.setText(tr("任务栏圆角 · {0}").format(settings.taskbar_radius))
        style = controller.native_taskbar_style
        from .native_taskbar_xaml import layout_library
        appearance = layout_library() is not None
        self.taskbar_color.setText(settings.taskbar_color.upper())
        self.taskbar_color.setStyleSheet("QPushButton { border:2px solid " + settings.taskbar_color + "; }")
        self.taskbar_color.setEnabled(appearance and settings.taskbar_style_enabled)
        self.taskbar_opacity.setEnabled(appearance and settings.taskbar_style_enabled)
        if not self.taskbar_opacity_save.isActive():
            with QSignalBlocker(self.taskbar_opacity):
                self.taskbar_opacity.setValue(settings.taskbar_opacity)
            self.taskbar_opacity_label.setText(tr("背景不透明度 · {0}%").format(settings.taskbar_opacity))
        auto_hide = style is not None and style.auto_hide
        centered = style is not None and style.centered
        self.taskbar_intro.setText(tr("电源和搜索、应用、托盘已在同一条原生任务栏内居中。") if centered else
                                   tr("为 Windows 原生任务栏添加圆角和留边，保留系统搜索、应用按钮和托盘。"))
        self.taskbar_radius_label.setVisible(not centered)
        self.taskbar_radius.setVisible(not centered)
        self.taskbar_radius.setEnabled(settings.taskbar_style_enabled and not auto_hide)
        if centered:
            message = (tr("三段一起显示和隐藏，跟随 Windows 的自动隐藏设置。") if auto_hide else
                       tr("窗口盖住底栏时三段一起隐藏，鼠标移到屏幕底部可唤出。"))
        elif auto_hide:
            message = tr("Windows 已开启自动隐藏，圆角暂时停用。")
        else:
            message = tr("当前系统保留 Windows 原生任务栏配色。") if not appearance else tr("开启任务栏美化后可调整背景颜色与不透明度。")
        self.taskbar_status.setText(message)

    def taskbar_radius_changed(self, value):
        self.taskbar_radius_label.setText(tr("任务栏圆角 · {0}").format(value))
        if not self.updating:
            self.taskbar_radius_save.start()

    def group_selected(self, item, previous=None):
        if self.updating:
            return
        self.selected_id = item.data(Qt.ItemDataRole.UserRole) if item else ""
        group = self.group()
        self.updating = True
        for widget in (self.name, self.color):
            widget.setEnabled(group is not None)
        if group:
            self.name.setText(group_name(group))
            self.color.setCurrentIndex(max(0, self.color.findData(group["color"])))
        self.updating = False

    def toggle(self, value):
        if not self.updating:
            self.manager.set_enabled(value)

    def sync_desktop(self):
        count = self.manager.sync_desktop()
        self.window.show_notice(tr("桌面分类新增 {0} 个入口").format(count))

    def create(self):
        name, accepted = QInputDialog.getText(self, tr("新建桌面分组"), tr("分组名称"), text=tr("新的分组"))
        if accepted:
            group = self.manager.collections.create(name)
            self.selected_id = group["id"]
            self.manager.refresh()

    def rename(self):
        if self.group():
            self.manager.rename(self.group())

    def save_name(self):
        if not self.updating and self.group():
            self.manager.rename_group(self.group(), self.name.text())

    def group_option(self, key, value):
        if not self.updating and self.group():
            self.manager.set_group(self.group(), key, value)

    def global_option(self, key, value):
        if self.updating:
            return
        settings = self.manager.store.settings
        previous = getattr(settings, key)
        if previous == value:
            return
        setattr(settings, key, value)
        try:
            self.manager.store.save()
        except OSError as exc:
            setattr(settings, key, previous)
            self.refresh()
            self.manager.error.emit(str(exc))
            return
        if key in ("taskbar_radius", "taskbar_color", "taskbar_opacity"):
            style = self.window.controller.native_taskbar_style
            if style is not None:
                style.schedule_refresh()
            self.refresh_taskbar_options()
        else:
            self.manager.refresh()

    def set_view(self):
        if not self.updating:
            self.manager.set_view(self.view.currentData())

    def remove(self):
        if self.group():
            errors = self.manager.collections.remove_group(self.group())
            self.manager.refresh()
            if errors:
                self.manager.error.emit(tr("部分项目无法还原，分组已保留：\n")+"\n".join(errors))

    def files(self):
        if self.group():
            self.manager.choose_files(self.group())
        else:
            self.window.show_notice(tr("请先创建或选择一个分组。"))

    def folder(self):
        if self.group():
            self.manager.choose_folder(self.group())

    def system_entry(self):
        group = self.group()
        if group:
            choices = list(SYSTEM_ENTRIES)
            names = [tr(SYSTEM_ENTRIES[key][0]) for key in choices]
            name, accepted = QInputDialog.getItem(self, tr("添加系统入口"), tr("选择入口"), names, editable=False)
            if accepted:
                self.manager.collections.add_system(group, choices[names.index(name)])
                self.manager.refresh()

    def restore_all(self):
        self.manager.restore_legacy_files()
        self.manager.refresh()
