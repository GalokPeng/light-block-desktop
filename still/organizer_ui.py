from __future__ import annotations

from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import (QComboBox, QFrame, QHBoxLayout, QInputDialog, QLineEdit, QListWidget,
    QListWidgetItem, QScrollArea, QSlider, QVBoxLayout, QWidget)

from .i18n import tr
from .organizer_model import SYSTEM_ENTRIES, group_name
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
        top = QHBoxLayout(intro)
        top.setContentsMargins(23, 20, 23, 20)
        titles = QVBoxLayout()
        titles.addWidget(label(tr("桌面文件夹"), "section"))
        titles.addWidget(label(tr("开启后在桌面中央显示分类，并隐藏 Windows 原生图标。"), "muted", True))
        top.addLayout(titles, 1)
        self.enabled = Toggle(self.manager.enabled)
        self.enabled.setAccessibleName(tr("开启桌面收纳"))
        self.enabled.toggled.connect(self.toggle)
        top.addWidget(label(tr("开启桌面收纳")))
        top.addWidget(self.enabled)
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
        self.icon_size = QComboBox()
        for size, title in ((32, tr("紧凑图标")), (44, tr("标准图标")), (60, tr("大图标"))):
            self.icon_size.addItem(title, size)
        self.icon_size.currentIndexChanged.connect(lambda: self.group_option("icon_size", self.icon_size.currentData()))
        rv.addWidget(self.icon_size)
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
        for widget in (self.name, self.color, self.icon_size):
            widget.setEnabled(group is not None)
        if group:
            self.name.setText(group_name(group))
            self.color.setCurrentIndex(max(0, self.color.findData(group["color"])))
            self.icon_size.setCurrentIndex(max(0, self.icon_size.findData(group.get("icon_size", 44))))
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
        setattr(self.manager.store.settings, key, value)
        self.manager.store.save()
        self.manager.refresh()

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
