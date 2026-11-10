from __future__ import annotations

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QColor, QIcon, QPainter, QPixmap
from PySide6.QtWidgets import (QColorDialog, QComboBox, QFrame, QHBoxLayout, QInputDialog, QLineEdit, QListWidget,
    QListWidgetItem, QScrollArea, QSlider, QVBoxLayout, QWidget)

from .i18n import tr
from .organizer_model import SYSTEM_ENTRIES, group_name, organizer_view, view_options
from .ui import Toggle, button, label


class OrganizerPage(QWidget):
    def __init__(self, window):
        super().__init__()
        self.window = window
        self.manager = window.controller.get_organizer()
        self.updating = False
        self.selected_id = ""
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(16)
        intro = QFrame()
        intro.setObjectName("panel")
        intro_layout = QVBoxLayout(intro)
        intro_layout.setContentsMargins(23, 17, 23, 17)
        intro_layout.setSpacing(12)
        top = QHBoxLayout()
        titles = QVBoxLayout()
        titles.addWidget(label(tr("桌面文件夹"), "section"))
        titles.addWidget(label(tr("开启后在桌面显示收纳面板，并隐藏 Windows 原生图标。"), "muted", True))
        top.addLayout(titles, 1)
        self.enabled = Toggle(self.manager.enabled)
        self.enabled.setAccessibleName(tr("开启桌面收纳"))
        self.enabled.toggled.connect(self.toggle)
        top.addWidget(label(tr("开启桌面收纳")))
        top.addWidget(self.enabled)
        intro_layout.addLayout(top)
        line = QFrame()
        line.setFrameShape(QFrame.Shape.HLine)
        intro_layout.addWidget(line)
        taskbar = QHBoxLayout()
        taskbar_text = QVBoxLayout()
        taskbar_text.addWidget(label(tr("任务栏美化"), "section"))
        taskbar_text.addWidget(label(tr("在主屏底部显示电源、搜索和应用 Dock，右侧保留系统托盘。"), "muted", True))
        taskbar.addLayout(taskbar_text, 1)
        self.taskbar_toggle = Toggle(window.store.settings.taskbar_style_enabled)
        self.taskbar_toggle.setAccessibleName(tr("开启任务栏美化"))
        self.taskbar_toggle.toggled.connect(window.controller.set_taskbar_style)
        taskbar.addWidget(label(tr("开启任务栏美化")))
        taskbar.addWidget(self.taskbar_toggle)
        intro_layout.addLayout(taskbar)
        outer.addWidget(intro)
        body = QHBoxLayout()
        body.setSpacing(18)
        left = QFrame()
        left.setObjectName("panel")
        lv = QVBoxLayout(left)
        lv.setContentsMargins(20, 20, 20, 20)
        lv.setSpacing(12)
        heading = QHBoxLayout()
        heading.addWidget(label(tr("我的分组"), "section"))
        heading.addStretch()
        heading.addWidget(button(tr("同步分类"), self.sync_desktop))
        heading.addWidget(button(tr("＋ 新建"), self.create, "primary"))
        lv.addLayout(heading)
        self.groups = QListWidget()
        self.groups.setSpacing(7)
        self.groups.setMinimumWidth(240)
        self.groups.setObjectName("collectionList")
        self.groups.currentItemChanged.connect(self.group_selected)
        lv.addWidget(self.groups, 1)
        actions = QHBoxLayout()
        actions.addWidget(button(tr("重命名"), self.rename))
        actions.addWidget(button(tr("移除分组"), self.remove))
        lv.addLayout(actions)
        lv.addWidget(label(tr("点击桌面上的文件夹展开应用网格。鼠标移出面板时，面板和文字会一起淡出。"), "muted", True))
        body.addWidget(left, 1)
        right = QFrame()
        right.setObjectName("panel")
        rv = QVBoxLayout(right)
        rv.setContentsMargins(21, 20, 21, 20)
        rv.setSpacing(12)
        rv.addWidget(label(tr("分组与外观"), "section"))
        self.name = QLineEdit()
        self.name.setPlaceholderText(tr("分组名称"))
        self.name.setMaxLength(40)
        self.name.setMinimumHeight(32)
        self.name.editingFinished.connect(self.save_name)
        rv.addWidget(self.name)
        self.color = QComboBox()
        for key, title in (("mint", tr("薄荷绿")), ("blue", tr("海盐蓝")), ("violet", tr("鸢尾紫")), ("amber", tr("暖砂金")), ("rose", tr("蔷薇粉"))):
            self.color.addItem(title, key)
        self.color.currentIndexChanged.connect(lambda: self.group_option("color", self.color.currentData()))
        rv.addWidget(self.color)
        rv.addSpacing(8)
        rv.addWidget(label(tr("桌面布局"), "muted"))
        self.alignment = QComboBox()
        for key, title in (("top", tr("居上")), ("center", tr("居中")), ("bottom", tr("居下"))):
            self.alignment.addItem(title, key)
        self.alignment.currentIndexChanged.connect(lambda: self.global_option("organizer_alignment", self.alignment.currentData()))
        rv.addWidget(self.alignment)
        self.view = QComboBox()
        for key, title in view_options():
            self.view.addItem(title, key)
        self.view.currentIndexChanged.connect(self.set_view)
        rv.addWidget(self.view)
        rv.addWidget(label(tr("桌面图标大小"), "muted"))
        self.icon_size = QSlider(Qt.Orientation.Horizontal)
        self.icon_size.setAccessibleName(tr("桌面图标大小"))
        self.icon_size.setRange(0, 2)
        self.icon_size.setTickPosition(QSlider.TickPosition.TicksBelow)
        self.icon_size.setTickInterval(1)
        self.icon_size.valueChanged.connect(lambda value: self.global_option("organizer_icon_size", (32, 44, 60)[value]))
        rv.addWidget(self.icon_size)
        sizes = QHBoxLayout()
        for index, name in enumerate((tr("小"), tr("中"), tr("大"))):
            if index:
                sizes.addStretch()
            sizes.addWidget(label(name, "muted"))
        rv.addLayout(sizes)
        rv.addSpacing(8)
        rv.addWidget(label(tr("面板透明度"), "muted"))
        self.opacity = QSlider(Qt.Orientation.Horizontal)
        self.opacity.setRange(75, 235)
        self.opacity.setValue(self.manager.store.settings.organizer_opacity)
        self.opacity_save = QTimer(self)
        self.opacity_save.setSingleShot(True)
        self.opacity_save.setInterval(180)
        self.opacity_save.timeout.connect(lambda: self.global_option("organizer_opacity", self.opacity.value()))
        self.opacity.valueChanged.connect(lambda _: self.opacity_save.start())
        rv.addWidget(self.opacity)
        rv.addSpacing(8)
        rv.addWidget(label(tr("Dock 和托盘"), "section"))
        glass_row = QHBoxLayout()
        glass_row.addWidget(label(tr("磨砂玻璃")))
        glass_row.addStretch()
        self.glass = Toggle(window.store.settings.dock_glass)
        self.glass.setAccessibleName(tr("磨砂玻璃"))
        self.glass.toggled.connect(lambda value: self.global_option("dock_glass", value))
        glass_row.addWidget(self.glass)
        rv.addLayout(glass_row)
        self.dock_color = button(tr("底栏颜色"), self.choose_dock_color)
        rv.addWidget(self.dock_color)
        rv.addWidget(label(tr("底栏不透明度"), "muted"))
        self.dock_opacity = QSlider(Qt.Orientation.Horizontal)
        self.dock_opacity.setAccessibleName(tr("底栏不透明度"))
        self.dock_opacity.setRange(20, 100)
        self.dock_opacity.setSingleStep(5)
        self.dock_opacity_save = QTimer(self, singleShot=True, interval=180)
        self.dock_opacity_save.timeout.connect(lambda: self.global_option("dock_opacity", self.dock_opacity.value()))
        self.dock_opacity.valueChanged.connect(lambda _: self.dock_opacity_save.start() if not self.updating else None)
        rv.addWidget(self.dock_opacity)
        rv.addWidget(label(tr("收纳中的项目以入口形式打开。关闭收纳会恢复 Windows 桌面图标的原显示状态。"), "muted", True))
        rv.addStretch()
        # Keep form controls readable on small displays; scroll the settings
        # card rather than squeezing text fields and explanatory copy.
        right.setMinimumHeight(290)
        details = QScrollArea()
        details.setFrameShape(QFrame.Shape.NoFrame)
        details.setWidgetResizable(True)
        details.setWidget(right)
        body.addWidget(details, 1)
        outer.addLayout(body, 1)
        tools = QFrame()
        tools.setObjectName("panel")
        tv = QVBoxLayout(tools)
        tv.setContentsMargins(20, 17, 20, 17)
        row = QHBoxLayout()
        row.addWidget(button(tr("＋ 添加应用 / 文件"), self.files, "primary"))
        row.addWidget(button(tr("添加文件夹"), self.folder))
        row.addWidget(button(tr("添加系统入口"), self.system_entry))
        row.addWidget(button(tr("还原旧版文件"), self.restore_all))
        tv.addLayout(row)
        tv.addWidget(label(tr("桌面项目按文件类型自动归类。旧版本移入托管目录的文件可以还原到原桌面目录。"), "muted", True))
        outer.addWidget(tools)
        self.manager.changed.connect(self.refresh)
        self.refresh()

    def group(self):
        return self.manager.collections.find(self.selected_id)

    def refresh(self):
        self.updating = True
        self.enabled.setChecked(self.manager.enabled)
        settings = self.manager.store.settings
        self.alignment.setCurrentIndex(self.alignment.findData(settings.organizer_alignment))
        self.view.setCurrentIndex(self.view.findData(organizer_view(settings)))
        self.icon_size.setValue((32, 44, 60).index(settings.organizer_icon_size or 44))
        self.glass.setChecked(settings.dock_glass)
        self.show_dock_color()
        if not self.dock_opacity_save.isActive():
            self.dock_opacity.setValue(settings.dock_opacity)
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
        if key in ("dock_glass", "dock_opacity", "dock_color"):
            dock = self.window.controller.dock
            if dock is not None:
                dock.refresh_theme()
        else:
            self.manager.refresh()

    def set_view(self):
        if not self.updating:
            self.manager.set_view(self.view.currentData())

    def choose_dock_color(self):
        settings = self.manager.store.settings
        color = QColorDialog.getColor(QColor(settings.dock_color), self, tr("底栏颜色"))
        if color.isValid():
            self.global_option("dock_color", color.name())
            self.show_dock_color()

    def show_dock_color(self):
        swatch = QPixmap(20, 20)
        swatch.fill(Qt.GlobalColor.transparent)
        with QPainter(swatch) as painter:
            painter.setRenderHint(QPainter.RenderHint.Antialiasing)
            painter.setBrush(QColor(self.manager.store.settings.dock_color))
            painter.setPen(QColor("#9aa8ba"))
            painter.drawRoundedRect(1, 1, 17, 17, 5, 5)
        self.dock_color.setIcon(QIcon(swatch))

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
