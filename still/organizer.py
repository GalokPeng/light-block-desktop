from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
from collections import OrderedDict
import json
from pathlib import Path

from PySide6.QtCore import (QAbstractListModel, QElapsedTimer, QEasingCurve, QEvent, QFileInfo, QFileSystemWatcher, QModelIndex, QMimeData, QObject, QPoint,
                          QPropertyAnimation, QRect, QRectF, QSize, Qt, QTimer, QVariantAnimation, Signal)
from PySide6.QtGui import QColor, QCursor, QDrag, QPainter, QPen, QPixmap
from PySide6.QtWidgets import (QAbstractButton, QAbstractItemView, QApplication, QFileDialog, QFileIconProvider,
    QGridLayout, QHBoxLayout, QInputDialog, QLineEdit, QListView, QMenu, QPushButton,
    QScrollArea, QSizePolicy, QStackedWidget, QStyle, QStyledItemDelegate, QVBoxLayout, QWidget)

from .i18n import tr
from .model import DEFAULT_ORGANIZER_TITLE
from . import desktop_windows as native
from .desktop_icons import DesktopIconLease
from .organizer_model import entry_name, group_name, named_entries, organizer_view, view_options, COLORS, Collections
from .organizer_colors import foreground, icon_background
from .scroll_wheel import MouseHook
from . import win32

MIME = "application/x-still-collection-items"
FOLDER_MIME = "application/x-still-collection-folder"
FRONT_COUNT = 10
CELL_MARGIN = 18
PANEL_PADDING = 20
HEADER_HEIGHT = 34


def search_symbol(color="#eef4fb"):
    pixmap = QPixmap(64, 64)
    pixmap.fill(Qt.GlobalColor.transparent)
    with QPainter(pixmap) as painter:
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.scale(2, 2)
        painter.setPen(QPen(QColor(color), 2.9, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawEllipse(QRectF(5.5, 5.5, 15, 15))
        painter.drawLine(QPoint(19, 19), QPoint(26, 26))
    return pixmap


class GroupItems(QAbstractListModel):
    ICON_CACHE_LIMIT = 128

    def __init__(self, view):
        super().__init__(view)
        self.view = view
        self.entries = ()
        self.folders = False
        self.show_search = False
        self.search_icon = search_symbol()
        self.icons = OrderedDict()
        self.pending = OrderedDict()
        self.placeholder = native.cropped_icon(view.style().standardIcon(QStyle.StandardPixmap.SP_FileIcon).pixmap(64))

    def set_entries(self, entries, show_search=False, folders=False):
        self.beginResetModel()
        self.entries = tuple(entries)
        self.show_search = show_search
        self.folders = folders
        self.icons.clear()
        self.pending.clear()
        self.endResetModel()

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.entries) + self.show_search

    def is_search(self, index):
        return index.isValid() and self.show_search and index.row() == len(self.entries)

    def data(self, index, role=Qt.ItemDataRole.DisplayRole):
        if not index.isValid():
            return None
        if self.is_search(index):
            if role == Qt.ItemDataRole.DecorationRole:
                return self.search_icon
            return tr("搜索") if role in (Qt.ItemDataRole.DisplayRole, Qt.ItemDataRole.ToolTipRole) else None
        row = index.row()
        entry = self.entries[row]
        if role == Qt.ItemDataRole.DisplayRole:
            return group_name(entry) if self.folders else entry_name(entry)
        if role == Qt.ItemDataRole.ToolTipRole:
            return group_name(entry) if self.folders else entry["target"]
        if role == Qt.ItemDataRole.TextAlignmentRole:
            return int(Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignTop)
        if role == Qt.ItemDataRole.DecorationRole:
            if row in self.icons:
                self.icons.move_to_end(row)
                return self.icons[row]
            first_request = not self.pending
            self.pending.setdefault(row, None)
            if first_request:
                self.view.panel.resume_icons()
            return self.placeholder
        return None

    def cache_icon(self, row, icon):
        self.icons[row] = icon
        if len(self.icons) > self.ICON_CACHE_LIMIT:
            self.icons.popitem(last=False)

    def flags(self, index):
        flags = super().flags(index)
        return flags | Qt.ItemFlag.ItemIsDragEnabled if index.isValid() and not self.folders and not self.is_search(index) else flags


class HoverIcon(QStyledItemDelegate):
    def __init__(self, view):
        super().__init__(view)
        self.view = view

    def sizeHint(self, option, index):
        return self.view.gridSize()

    def paint(self, painter, option, index):
        painter.save()
        painter.setClipRect(option.rect, Qt.ClipOperation.IntersectClip)
        painter.setRenderHints(QPainter.RenderHint.Antialiasing | QPainter.RenderHint.SmoothPixmapTransform)
        icon_size = self.view.iconSize().width()
        scale = self.view.hover_scales.get(index, 1.0)
        center = QPoint(option.rect.center().x(), option.rect.top() + 9 + icon_size // 2)
        plate = QRect(QPoint(), QSize(icon_size + 8, icon_size + 8) * scale)
        plate.moveCenter(center)
        selected = bool(option.state & QStyle.StateFlag.State_Selected)
        settings = self.view.panel.manager.store.settings
        painter.setBrush(icon_background(settings, selected, scale > 1.0))
        outline = QColor("#82b5e8") if selected else foreground(settings)
        outline.setAlpha(170 if selected else 65 if scale > 1.0 else 35)
        painter.setPen(QPen(outline, 1))
        radius = plate.width() * .23
        painter.drawRoundedRect(QRectF(plate), radius, radius)
        bounds = QRect(QPoint(), self.view.iconSize() * (scale * .84))
        bounds.moveCenter(center)
        native.paint_icon(painter, index.data(Qt.ItemDataRole.DecorationRole), bounds)
        label = option.rect.adjusted(4, icon_size + 17, -4, -2)
        painter.setFont(option.font)
        painter.setPen(foreground(settings))
        title = painter.fontMetrics().elidedText(index.data(), Qt.TextElideMode.ElideRight, label.width())
        painter.drawText(label, Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignTop, title)
        painter.restore()


class IconGrid(QListView):
    def __init__(self, panel):
        super().__init__(panel)
        self.panel = panel
        self.hover_scales = {}
        self.hover_start = {}
        self.hover_index = QModelIndex()
        self.hover = QVariantAnimation(self, startValue=0.0, endValue=1.0, duration=135)
        self.hover.setEasingCurve(QEasingCurve.Type.OutCubic)
        self.hover.valueChanged.connect(self.animate_icons)
        self.setMouseTracking(True)
        self.viewport().setMouseTracking(True)
        self.setItemDelegate(HoverIcon(self))
        self.setGridSize(QSize(74, 79))
        self.setViewMode(QListView.ViewMode.IconMode)
        self.setResizeMode(QListView.ResizeMode.Adjust)
        self.setMovement(QListView.Movement.Static)
        self.setUniformItemSizes(True)
        self.setLayoutMode(QListView.LayoutMode.Batched)
        self.setBatchSize(48)
        self.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        self.setWordWrap(False)
        self.setTextElideMode(Qt.TextElideMode.ElideRight)
        self.setDragEnabled(True)
        self.setAcceptDrops(True)
        self.setDropIndicatorShown(True)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.customContextMenuRequested.connect(self.menu)
        self.items = GroupItems(self)
        self.setModel(self.items)
        self.items.modelAboutToBeReset.connect(self.reset_hover)
        self.clicked.connect(self.single_click)
        self.setStyleSheet("""
            QListView { background:transparent; border:0; color:#f5f7fc; outline:0; font:12px 'Microsoft YaHei UI'; }
        """)

    def reset_hover(self):
        self.hover.stop()
        self.hover_index = QModelIndex()
        self.hover_start.clear()
        self.hover_scales.clear()

    def point_at(self, index):
        if index == self.hover_index:
            return
        self.hover.stop()
        self.hover_index = index
        self.hover_start = dict(self.hover_scales)
        if index.isValid():
            self.hover_start.setdefault(index, 1.0)
        if self.panel.animations_enabled():
            self.hover.start()
        else:
            self.animate_icons(1.0)

    def animate_icons(self, value):
        for index, before in self.hover_start.items():
            after = 1.17 if index == self.hover_index else 1.0
            scale = before + (after - before) * value
            if scale > 1.0:
                self.hover_scales[index] = scale
            else:
                self.hover_scales.pop(index, None)
            self.viewport().update(self.visualRect(index).adjusted(-5, -6, 5, 6))

    def mouseMoveEvent(self, event):
        self.point_at(self.indexAt(event.position().toPoint()))
        super().mouseMoveEvent(event)

    def leaveEvent(self, event):
        self.point_at(QModelIndex())
        super().leaveEvent(event)

    def single_click(self, index):
        if self.items.is_search(index):
            self.panel.toggle_search()
            return
        if self.items.folders:
            self.panel.open_group(self.items.entries[index.row()]["id"])
            return
        if QApplication.keyboardModifiers() == Qt.KeyboardModifier.NoModifier:
            # A native double click can emit itemClicked twice. Debounce only
            # the single-click gesture, not ordinary double-click mode.
            self.panel.manager.launch(self.items.entries[index.row()], debounce=True)

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton and not self.indexAt(event.position().toPoint()).isValid():
            self.panel.dismiss()
            return
        super().mousePressEvent(event)

    def startDrag(self, _):
        if self.panel.group is None or self.panel.search_editor:
            return
        data = {"group": self.panel.group["id"],
                "entries": [self.items.entries[i.row()]["id"] for i in self.selectedIndexes()]}
        mime = QMimeData()
        mime.setData(MIME, json.dumps(data).encode())
        drag = QDrag(self)
        drag.setMimeData(mime)
        if self.currentIndex().isValid():
            icon = self.currentIndex().data(Qt.ItemDataRole.DecorationRole)
            drag.setPixmap(icon.scaled(40, 40, Qt.AspectRatioMode.KeepAspectRatio,
                                       Qt.TransformationMode.SmoothTransformation))
        drag.exec(Qt.DropAction.MoveAction)

    def dragEnterEvent(self, event):
        if self.panel.group and not self.panel.search_editor and (event.mimeData().hasFormat(MIME) or event.mimeData().hasUrls()):
            event.setDropAction(Qt.DropAction.MoveAction if event.mimeData().hasFormat(MIME) else Qt.DropAction.CopyAction)
            event.accept()

    def dragMoveEvent(self, event):
        self.dragEnterEvent(event)

    def dropEvent(self, event):
        if self.panel.group is None or self.panel.search_editor:
            return
        try:
            if event.mimeData().hasFormat(MIME):
                data = json.loads(bytes(event.mimeData().data(MIME)))
                source = self.panel.manager.collections.find(data["group"])
                if source:
                    item = self.indexAt(event.position().toPoint())
                    index = item.row() if item.isValid() else None
                    self.panel.manager.collections.move_entries(source, self.panel.group, data["entries"], index)
                    self.panel.manager.refresh()
            else:
                self.panel.manager.add_paths(self.panel.group, [u.toLocalFile() for u in event.mimeData().urls() if u.isLocalFile()])
            event.setDropAction(Qt.DropAction.MoveAction if event.mimeData().hasFormat(MIME) else Qt.DropAction.CopyAction)
            event.accept()
        except (ValueError, OSError, KeyError) as exc:
            self.panel.manager.error.emit(str(exc))

    def menu(self, point):
        item = self.indexAt(point)
        if not item.isValid() or self.items.is_search(item) or self.items.folders:
            self.panel.show_menu(self.mapToGlobal(point))
            return
        entry = self.items.entries[item.row()]
        owner = self.panel.entry_owners[entry["id"]]
        menu = QMenu(self)
        menu.addAction(tr("打开"), lambda: self.panel.manager.launch(entry))
        if not entry.get("system"):
            menu.addAction(tr("在文件夹中显示"), lambda: native.reveal_file(entry["target"]))
        menu.addAction(tr("更改显示名称…"), lambda: self.panel.manager.rename_entry(owner, entry))
        menu.addSeparator()
        for group in self.panel.manager.collections.groups:
            if group["id"] != owner["id"]:
                menu.addAction(tr("移至「{0}」").format(group_name(group)), lambda checked=False, g=group: self.transfer(entry, owner, g))
        menu.addSeparator()
        menu.addAction(tr("还原到原桌面") if entry.get("managed") else tr("移除快捷入口"), lambda: self.panel.manager.remove_entry(owner, entry))
        self.panel.add_sort_menu(menu)
        menu.exec(self.mapToGlobal(point))

    def transfer(self, entry, owner, group):
        self.panel.manager.collections.move_entries(owner, group, [entry["id"]])
        self.panel.manager.refresh()


class FolderTile(QAbstractButton):
    def __init__(self, shelf, group):
        super().__init__(shelf)
        self.shelf, self.group = shelf, group
        self.press_point = None
        self.drop_hover = False
        self.zoom = 1.0
        self.hover = QVariantAnimation(self, duration=135)
        self.hover.setEasingCurve(QEasingCurve.Type.OutCubic)
        self.hover.valueChanged.connect(self.set_zoom)
        self.icons = [native.cropped_icon(shelf.manager.icon_for(entry).pixmap(48, 48)) for entry in group["entries"][:4]]
        self.setFixedSize(116, 114)
        self.update_name()
        self.setAcceptDrops(True)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.clicked.connect(lambda: shelf.open_group(group["id"]))

    def update_name(self):
        name = group_name(self.group) or tr("未命名分组")
        self.setToolTip(tr("{0} · {1} 个项目").format(name, len(self.group['entries'])))
        self.setAccessibleName(name)
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        painter.save()
        painter.translate(58, 41)
        painter.scale(self.zoom, self.zoom)
        painter.translate(-58, -41)
        plate = QRectF(21, 4, 74, 74)
        accent = QColor(COLORS.get(self.group.get("color"), COLORS["mint"]))
        tint = QColor(accent)
        tint.setAlpha(160 if self.isDown() else 145 if self.underMouse() else 112)
        outline = QColor("#9cc5ff") if self.drop_hover else QColor(224, 235, 255, 80 if self.underMouse() else 42)
        painter.setPen(QPen(outline, 2 if self.drop_hover else 1))
        painter.setBrush(tint)
        painter.drawRoundedRect(plate, 18, 18)
        icon_size = {32: 19, 44: 24, 60: 28}.get(self.shelf.manager.store.settings.organizer_icon_size, 24)
        for n in range(4):
            x, y = 43 + (n%2)*30, 26 + (n//2)*30
            bounds = QRect(x-icon_size//2, y-icon_size//2, icon_size, icon_size)
            if n < len(self.icons):
                native.paint_icon(painter, self.icons[n], bounds)
            else:
                painter.setPen(Qt.PenStyle.NoPen)
                painter.setBrush(QColor(232, 240, 255, 23))
                painter.drawRoundedRect(QRectF(bounds.adjusted(1, 1, -1, -1)), 7, 7)
        painter.restore()
        ink = foreground(self.shelf.manager.store.settings)
        painter.setPen(ink)
        title = painter.fontMetrics().elidedText(group_name(self.group), Qt.TextElideMode.ElideRight, 110)
        painter.drawText(QRect(3, 81, 110, 21), Qt.AlignmentFlag.AlignHCenter, title)
        ink.setAlpha(195)
        painter.setPen(ink)
        painter.drawText(QRect(3, 101, 110, 13), Qt.AlignmentFlag.AlignHCenter, str(len(self.group["entries"])))

    def enterEvent(self, event):
        self.animate_zoom(1.09)
        super().enterEvent(event)

    def leaveEvent(self, event):
        self.animate_zoom(1.0)
        super().leaveEvent(event)

    def animate_zoom(self, scale):
        self.hover.stop()
        if self.shelf.animations_enabled():
            self.hover.setStartValue(self.zoom)
            self.hover.setEndValue(scale)
            self.hover.start()
        else:
            self.set_zoom(scale)

    def set_zoom(self, value):
        self.zoom = value
        self.update()

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.press_point = event.position().toPoint()
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if (self.press_point is not None and event.buttons() & Qt.MouseButton.LeftButton
                and (event.position().toPoint()-self.press_point).manhattanLength() >= QApplication.startDragDistance()):
            self.press_point = None
            drag = QDrag(self)
            mime = QMimeData()
            mime.setData(FOLDER_MIME, self.group["id"].encode("utf-8"))
            drag.setMimeData(mime)
            drag.setPixmap(self.grab())
            drag.setHotSpot(event.position().toPoint())
            self.setDown(False)
            drag.exec(Qt.DropAction.MoveAction)
            return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        self.press_point = None
        super().mouseReleaseEvent(event)

    def folder_source(self, event):
        source = event.source()
        if (event.mimeData().hasFormat(FOLDER_MIME) and isinstance(source, FolderTile)
                and source.shelf is self.shelf and source is not self):
            return source
        return None

    def dragEnterEvent(self, event):
        if self.folder_source(event) is not None:
            self.drop_hover = True
            self.update()
            event.setDropAction(Qt.DropAction.MoveAction)
            event.accept()
        elif event.mimeData().hasUrls():
            event.setDropAction(Qt.DropAction.CopyAction)
            event.accept()

    def dragMoveEvent(self, event):
        if self.folder_source(event) is not None:
            event.setDropAction(Qt.DropAction.MoveAction)
            event.accept()
        elif event.mimeData().hasUrls():
            event.setDropAction(Qt.DropAction.CopyAction)
            event.accept()

    def dropEvent(self, event):
        self.drop_hover = False
        self.update()
        source = self.folder_source(event)
        if source is not None:
            self.shelf.manager.move_group(source.group["id"], self.group["id"])
            event.setDropAction(Qt.DropAction.MoveAction)
            event.accept()
            return
        paths = [url.toLocalFile() for url in event.mimeData().urls() if url.isLocalFile()]
        if paths:
            self.shelf.manager.add_paths(self.group, paths)
            event.setDropAction(Qt.DropAction.CopyAction)
            event.accept()

    def dragLeaveEvent(self, event):
        self.drop_hover = False
        self.update()
        super().dragLeaveEvent(event)


class ShelfTitle(QAbstractButton):
    def __init__(self, shelf):
        super().__init__(shelf)
        self.shelf = shelf
        self.setFixedHeight(30)
        self.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Fixed)
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.setAccessibleName(tr("编辑收纳标题"))
        self.setToolTip(tr("点击修改名称"))
        self.setCursor(Qt.CursorShape.IBeamCursor)

    def sizeHint(self):
        return QSize(max(88, min(280, self.fontMetrics().horizontalAdvance(self.text()) + 10)), 30)

    def minimumSizeHint(self):
        return QSize(88, 30)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        if self.underMouse():
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(QColor(255, 255, 255, 25))
            painter.drawRoundedRect(QRectF(self.rect()), 6, 6)
        painter.setPen(foreground(self.shelf.manager.store.settings))
        text = self.fontMetrics().elidedText(self.text(), Qt.TextElideMode.ElideRight, self.width() - 10)
        painter.drawText(self.rect().adjusted(5, 0, -5, 0), Qt.AlignmentFlag.AlignVCenter, text)


class ShelfTitleEditor(QLineEdit):
    def __init__(self, shelf, group_id):
        super().__init__(shelf)
        # The desktop window stays non-activating. The text field accepts focus
        # only while the user is explicitly editing a title.
        self.setWindowFlags(Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint |
                            Qt.WindowType.NoDropShadowWindowHint)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.shelf, self.group_id = shelf, group_id
        self.setMaxLength(40)
        self.setAccessibleName(tr("收纳名称"))
        self.setPlaceholderText(tr("留空可隐藏名称"))
        self.setStyleSheet("QLineEdit { background:#253247; color:#f4f7fc; font:600 19px 'Microsoft YaHei UI';"
                           " border:1px solid #91cbb6; border-radius:6px; padding:0 4px;"
                           " selection-background-color:#456e66; }")

    def keyPressEvent(self, event):
        if event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter, Qt.Key.Key_Escape):
            self.shelf.finish_title_edit(event.key() != Qt.Key.Key_Escape)
            event.accept()
            return
        super().keyPressEvent(event)

    def focusOutEvent(self, event):
        super().focusOutEvent(event)
        if event.reason() != Qt.FocusReason.PopupFocusReason:
            QTimer.singleShot(0, self.save_if_unfocused)

    def save_if_unfocused(self):
        if (self.shelf.title_editor is self and not self.hasFocus()
                and QApplication.activePopupWidget() is None):
            self.shelf.finish_title_edit()


class DesktopShelf(QWidget):
    """One desktop-owned window with a directional panel and a shared icon grid."""
    def __init__(self, manager):
        super().__init__(None, Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint | Qt.WindowType.WindowDoesNotAcceptFocus)
        self.manager = manager
        self.group = None
        self.alignment = manager.store.settings.organizer_alignment
        self.view_mode = organizer_view(manager.store.settings)
        self.expanded = False
        self.search_editor = None
        self.entry_owners = {}
        self.moving = False
        self.move_anchor = None
        self.move_origin = None
        self.title_editor = None
        self.board_columns = 0
        self.board_dirty = True
        self.color_signature = None
        self.setWindowTitle(tr("轻块桌面 · 桌面收纳"))
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setWindowOpacity(.68)
        self.motion = QVariantAnimation(self)
        self.motion.setEasingCurve(QEasingCurve.Type.OutCubic)
        self.motion.valueChanged.connect(lambda _: self.update())
        self.motion.finished.connect(self.settle)
        self.panel_rect = QRect()
        self.sprite = None
        self.icon_timer = QTimer(self)
        self.icon_timer.setInterval(16)
        self.icon_timer.timeout.connect(self.load_icons)
        self.fade = QPropertyAnimation(self, b"windowOpacity", self)
        self.fade.setDuration(170)
        self.fade.setEasingCurve(QEasingCurve.Type.OutCubic)
        self.layer_timer = QTimer(self)
        self.layer_timer.setSingleShot(True)
        self.layer_timer.timeout.connect(self.attach_to_desktop)
        self.collapse_timer = QTimer(self)
        self.collapse_timer.setSingleShot(True)
        self.collapse_timer.timeout.connect(self.dismiss)
        self.click_hook = MouseHook({0x0201, 0x0204}, self.check_outside_click)
        self.window_at = win32.bind(win32.user32, "WindowFromPoint", W.HWND, W.POINT)
        self.root_window = win32.bind(win32.user32, "GetAncestor", W.HWND, W.HWND, W.UINT)

        self.setStyleSheet("""
            QAbstractButton#shelfTitle { font:600 19px 'Microsoft YaHei UI'; }
            QPushButton { color:#f5f8ff;background:rgba(228,240,255,17);border:1px solid rgba(228,240,255,32);
                          border-radius:10px;padding:4px 8px; }
            QPushButton:hover { background:rgba(228,240,255,42); }
            QScrollArea,QScrollArea > QWidget,QScrollArea > QWidget > QWidget { background:transparent;border:0; }
        """)
        self.body = QWidget(self)
        self.body.setAttribute(Qt.WidgetAttribute.WA_NoSystemBackground)
        layout = QVBoxLayout(self.body)
        layout.setContentsMargins(CELL_MARGIN, PANEL_PADDING // 2, CELL_MARGIN, PANEL_PADDING // 2)
        layout.setSpacing(HEADER_HEIGHT - 30)
        self.header = QWidget(self.body)
        header = QHBoxLayout(self.header)
        header.setContentsMargins(0, 0, 0, 0)
        self.back = QPushButton(tr("返回"))
        self.back.clicked.connect(self.overview)
        header.addWidget(self.back)
        self.title = ShelfTitle(self)
        self.title.setObjectName("shelfTitle")
        self.title.clicked.connect(self.edit_title)
        header.addWidget(self.title)
        header.addStretch()
        self.search_slot = QWidget(self.header)
        self.search_slot.setFixedHeight(30)
        self.search_slot.setMinimumWidth(128)
        self.search_slot.setMaximumWidth(246)
        self.search_slot.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        header.addWidget(self.search_slot)
        layout.addWidget(self.header)
        self.pages = QStackedWidget()
        layout.addWidget(self.pages, 1)
        self.scroll = QScrollArea()
        self.scroll.setWidgetResizable(True)
        self.scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.board = QWidget()
        self.tiles = QGridLayout(self.board)
        self.tiles.setContentsMargins(0, 0, 0, 0)
        self.tiles.setSpacing(6)
        self.tiles.setAlignment(Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignTop)
        self.scroll.setWidget(self.board)
        self.pages.addWidget(self.scroll)
        self.grid = IconGrid(self)
        self.pages.addWidget(self.grid)
        if self.alignment == "bottom":
            layout.removeWidget(self.header)
            layout.addWidget(self.header)
        self.refresh()

    def animations_enabled(self):
        enabled = W.BOOL(1)
        win32.SystemParametersInfo(0x1042, 0, C.byref(enabled), 0)
        return bool(enabled.value)

    def placement(self, width, height, start, outgoing=None):
        area, center = self.location()
        available = QSize(max(250, area.width() - 28), max(180, area.height() - 28))
        # A constant layered HWND avoids reallocating the Windows backing store
        # at either end of a transition. Pixels outside the panel stay transparent.
        host = QRect(QPoint(), QSize((FRONT_COUNT + 1) * (60 + 30) + CELL_MARGIN * 2 + 1, 434).boundedTo(available))
        host.moveCenter(center)
        host.moveTopLeft(self.clamp_position(host.topLeft(), host.size(), area))
        if self.alignment == "top":
            host.moveTop(area.top() + 14)
        elif self.alignment == "bottom":
            host.moveBottom(area.bottom() - 14)
        if self.geometry() != host:
            self.setGeometry(host)
        target = QRect(QPoint(), QSize(width, height).boundedTo(available))
        target.moveCenter(self.rect().center())
        if self.alignment == "top":
            target.moveTop(0)
        elif self.alignment == "bottom":
            target.moveBottom(self.height() - 1)
        self.panel_rect = target
        self.body.setGeometry(target)
        if start is None or start == target:
            self.resume_icons()
            return

        self.fade.stop()
        self.body.layout().activate()
        self.sprite = outgoing if outgoing is not None else self.body.grab()
        self.body.setUpdatesEnabled(False)
        QApplication.instance().installEventFilter(self)
        self.motion.setDuration(170)
        self.motion.setStartValue(start)
        self.motion.setEndValue(target)
        self.motion.start()

    def location(self):
        saved = self.manager.store.settings.organizer_position if self.alignment == "center" else None
        point = QPoint(*saved) if saved is not None else None
        screen = QApplication.screenAt(point) if point is not None else None
        if native.live_screen(screen):
            return screen.availableGeometry(), point
        screen = native.current_screen()
        if screen is None:
            area = self.geometry()
            return area, area.center()
        area = screen.availableGeometry()
        # Legacy Dock reserved its own space after expanding the native work area.
        # dock = getattr(self.manager.controller, "dock", None)
        # if self.alignment == "bottom" and dock is not None:
        #     area.setBottom(min(area.bottom(), screen.geometry().bottom() - (dock.tray_height or 48)))
        return area, area.center()

    @staticmethod
    def clamp_position(point, size, area):
        return QPoint(max(area.left(), min(point.x(), area.right()-size.width()+1)),
                      max(area.top(), min(point.y(), area.bottom()-size.height()+1)))

    def stop_transition(self):
        self.motion.stop()
        if self.sprite is not None:
            QApplication.instance().removeEventFilter(self)
            self.sprite = None
            self.body.setUpdatesEnabled(True)

    def settle(self):
        self.stop_transition()
        self.animate_opacity(1 if self.panel_rect.contains(self.mapFromGlobal(QCursor.pos())) else .68)
        self.resume_icons()
        self.update()

    def eventFilter(self, watched, event):
        if (self.search_editor is not None and watched is self.search_editor
                and event.type() == QEvent.Type.KeyPress and event.key() == Qt.Key.Key_Escape):
            self.dismiss()
            return True
        inside = isinstance(watched, QWidget) and (watched is self or self.isAncestorOf(watched))
        if self.moving and inside and self.move_event(event):
            return True
        if (self.sprite is not None and event.type() == QEvent.Type.MouseButtonPress
                and inside):
            self.settle()
        return super().eventFilter(watched, event)

    def begin_move(self):
        self.finish_title_edit()
        self.collapse_timer.stop()
        if self.sprite is not None:
            self.settle()
        self.moving = True
        self.move_origin = self.pos()
        self.body.setCursor(Qt.CursorShape.SizeAllCursor)
        QApplication.instance().installEventFilter(self)

    def end_move(self, save=False):
        if not self.moving:
            return
        changed = self.pos() != self.move_origin
        self.moving = False
        if self.move_anchor is not None:
            self.releaseMouse()
            self.move_anchor = None
        self.body.unsetCursor()
        QApplication.instance().removeEventFilter(self)
        if save and changed:
            self.manager.set_position(self.geometry().center())
        else:
            self.refresh()

    def move_event(self, event):
        kind = event.type()
        if kind == QEvent.Type.MouseButtonPress and event.button() == Qt.MouseButton.LeftButton:
            point = event.globalPosition().toPoint()
            if self.panel_rect.contains(self.mapFromGlobal(point)):
                self.move_anchor = point-self.pos()
                self.grabMouse()
                return True
        elif kind == QEvent.Type.MouseMove and self.move_anchor is not None:
            point = event.globalPosition().toPoint()
            screen = native.current_screen(QApplication.screenAt(point))
            if screen is not None:
                self.move(self.clamp_position(point-self.move_anchor, self.size(), screen.availableGeometry()))
                self.place_search()
            return True
        elif kind == QEvent.Type.MouseButtonRelease and event.button() == Qt.MouseButton.LeftButton and self.move_anchor is not None:
            self.end_move(save=True)
            return True
        return False

    def populate_grid(self):
        settings = self.manager.store.settings
        size = settings.organizer_icon_size or 44
        compact = not self.expanded
        folders = compact and self.view_mode == "groups"
        if self.group and self.search_editor is None:
            matches = [(entry, self.group) for entry in self.group["entries"]]
        elif folders:
            matches = []
        else:
            query = self.search_editor.text() if self.search_editor else ""
            matches = named_entries(self.manager.collections.groups, query, order=self.view_mode)
        if compact:
            matches = matches[:FRONT_COUNT]
        entries = self.manager.collections.groups[:FRONT_COUNT] if folders else [entry for entry, _ in matches]
        area, _ = self.location()
        usable = max(size + 14, area.width() - 28 - CELL_MARGIN * 2 - 1)
        count = len(entries) + compact
        columns = min(FRONT_COUNT + compact, max(1, usable // (size + 14)))
        if compact:
            columns = min(columns, count)
        cell_width = min(size + 30, usable // columns)
        cell_height = size + 35
        self.grid.setIconSize(QSize(size, size))
        self.grid.setGridSize(QSize(cell_width, cell_height))
        self.entry_owners = {entry["id"]: group for entry, group in matches}
        self.grid.items.set_entries(entries, show_search=compact, folders=folders)
        self.grid.scrollToTop()
        rows = max(1, (count + columns - 1) // columns)
        chrome = PANEL_PADDING + (0 if self.header.isHidden() else HEADER_HEIGHT)
        # QListView wraps a tile ending exactly at the viewport boundary.
        return max(232, columns * cell_width + CELL_MARGIN * 2 + 1), chrome + min(4, rows) * cell_height

    def resume_icons(self):
        if (self.pages.currentWidget() is self.grid and self.sprite is None and self.grid.items.pending
                and not self.icon_timer.isActive()):
            self.icon_timer.start()

    def load_icons(self):
        if self.sprite is not None or self.pages.currentWidget() is not self.grid:
            self.icon_timer.stop()
            return
        items = self.grid.items
        visible = self.grid.viewport().rect()
        budget = QElapsedTimer()
        budget.start()
        ready = []
        for _ in range(min(32, len(items.pending))):
            row, _ = items.pending.popitem(last=False)
            index = items.index(row)
            if index.isValid() and self.grid.visualRect(index).intersects(visible):
                if items.folders:
                    pixmap = self.folder_icon(items.entries[row])
                else:
                    icon = self.manager.icon_for(items.entries[row])
                    pixmap = native.cropped_icon(icon.pixmap(self.grid.iconSize() * 1.5, self.grid.devicePixelRatioF()))
                items.cache_icon(row, pixmap if not pixmap.isNull() else items.placeholder)
                ready.append(row)
            if len(ready) >= 4 or budget.elapsed() >= 5:
                break
        if ready:
            items.dataChanged.emit(items.index(min(ready)), items.index(max(ready)), [Qt.ItemDataRole.DecorationRole])
        if not items.pending:
            self.icon_timer.stop()

    def folder_icon(self, group):
        pixmap = QPixmap(96, 96)
        pixmap.fill(Qt.GlobalColor.transparent)
        with QPainter(pixmap) as painter:
            painter.setRenderHint(QPainter.RenderHint.Antialiasing)
            painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
            tint = QColor(COLORS.get(group.get("color"), COLORS["mint"]))
            tint.setAlpha(145)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(tint)
            painter.drawRoundedRect(QRectF(1, 1, 94, 94), 20, 20)
            for index in range(4):
                rect = QRect(11 + 40*(index%2), 11 + 40*(index//2), 34, 34)
                if index < len(group["entries"]):
                    icon = self.manager.icon_for(group["entries"][index])
                    native.paint_icon(painter, native.cropped_icon(icon.pixmap(48)), rect)
                else:
                    painter.setBrush(QColor(240, 249, 255, 44))
                    painter.drawRoundedRect(rect, 8, 8)
        return pixmap

    def overview(self):
        self.collapse_timer.stop()
        if self.group is None and self.search_editor is None:
            return
        self.finish_title_edit()
        if self.search_editor is not None:
            self.expanded = self.search_was_expanded
            self.close_search()
        self.group = None
        self.refresh(animate=True)

    def dismiss(self):
        if self.expanded:
            self.set_expanded(False)

    def set_expanded(self, expanded):
        if not expanded:
            self.finish_title_edit()
            self.close_search()
            self.group = None
        self.expanded = expanded
        self.refresh(animate=True)

    def toggle_search(self):
        if self.search_editor is not None:
            self.overview()
            return
        self.finish_title_edit()
        self.group = None
        self.search_was_expanded = self.expanded
        self.expanded = True
        editor = QLineEdit(self)
        editor.setWindowFlags(Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint)
        editor.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        editor.setPlaceholderText(tr("搜索桌面项目"))
        editor.setAccessibleName(tr("搜索桌面项目"))
        editor.setMaxLength(120)
        editor.setStyleSheet("QLineEdit { background:#202d3d;color:#fff;border:1px solid #7f9bb8;border-radius:8px;padding:5px 9px; }")
        self.search_editor = editor
        self.refresh()
        editor.installEventFilter(self)
        editor.textChanged.connect(lambda _: self.populate_grid())
        editor.returnPressed.connect(self.open_search_result)
        editor.show()
        editor.raise_()
        editor.activateWindow()
        editor.setFocus(Qt.FocusReason.MouseFocusReason)

    def place_search(self):
        if self.search_editor:
            self.body.layout().activate()
            self.search_editor.setGeometry(QRect(self.search_slot.mapToGlobal(QPoint()), self.search_slot.size()))

    def close_search(self):
        editor, self.search_editor = self.search_editor, None
        if editor is not None:
            editor.close()

    def open_search_result(self):
        if self.grid.items.entries:
            self.manager.launch(self.grid.items.entries[0])

    def open_group(self, ident):
        group = self.manager.collections.find(ident)
        if group:
            self.finish_title_edit()
            self.collapse_timer.stop()
            self.group = group
            self.expanded = True
            self.refresh(animate=True)

    def refresh_colors(self):
        settings = self.manager.store.settings
        signature = settings.organizer_background_color, settings.organizer_icon_color
        if self.color_signature == signature:
            return
        self.color_signature = signature
        ink = foreground(settings).name()
        self.title.update()
        self.back.setStyleSheet("QPushButton { color:" + ink + "; }")
        self.grid.items.search_icon = search_symbol(ink)

    def refresh(self, animate=False):
        start = None
        outgoing = None
        if animate and self.isVisible() and self.animations_enabled():
            start = self.motion.currentValue() if self.sprite is not None else self.panel_rect
            if not self.expanded:
                outgoing = self.sprite if self.sprite is not None else self.body.grab()
        self.stop_transition()
        self.icon_timer.stop()
        settings = self.manager.store.settings
        self.refresh_colors()
        view = organizer_view(settings)
        if self.view_mode != view:
            self.view_mode = view
            self.close_search()
            self.group = None
            self.expanded = False
        if self.alignment != settings.organizer_alignment:
            self.close_search()
            self.group = None
            self.alignment = settings.organizer_alignment
            self.expanded = False
            layout = self.body.layout()
            layout.removeWidget(self.header)
            layout.insertWidget(1 if self.alignment == "bottom" else 0, self.header)
            start = None
        if not animate:
            self.board_dirty = True
            self.grid.items.pending.clear()
        if self.group is not None:
            self.group = self.manager.collections.find(self.group["id"])
        title = settings.organizer_title
        title = group_name(self.group) if self.group else "" if title == DEFAULT_ORGANIZER_TITLE else title
        self.title.setText(title)
        self.title.setVisible(bool(self.group or title))
        self.back.setVisible(bool(self.group))
        self.search_slot.setVisible(self.search_editor is not None)
        self.header.setVisible(bool(self.group or title or self.search_editor))
        if not self.expanded or self.group or self.search_editor or self.view_mode != "groups":
            self.pages.setCurrentWidget(self.grid)
            width, height = self.populate_grid()
            if self.search_editor:
                height = max(242, height)
            self.placement(width, height, start, outgoing)
        else:
            groups = self.manager.collections.groups
            area, _ = self.location()
            columns = min(4, max(1, (area.width()-84)//122), max(1, len(groups)))
            if self.board_dirty or self.board_columns != columns:
                while self.tiles.count():
                    tile = self.tiles.takeAt(0).widget()
                    tile.hide()
                    tile.deleteLater()
                for n, group in enumerate(groups):
                    self.tiles.addWidget(FolderTile(self, group), n//columns, n%columns)
                self.board_dirty = False
                self.board_columns = columns
            self.pages.setCurrentWidget(self.scroll)
            rows = max(1, min(3, (len(groups)+columns-1)//columns))
            self.placement(max(340, columns*122+54), 73+rows*120, start)
        if self.search_editor:
            self.place_search()
        if self.expanded:
            self.click_hook.start()
        else:
            self.click_hook.close()
        self.update()

    def showEvent(self, event):
        super().showEvent(event)
        self.layer_timer.start(0)
        self.setWindowOpacity(1 if self.panel_rect.contains(self.mapFromGlobal(QCursor.pos())) else .68)

    def attach_to_desktop(self):
        native.desktop_layer(int(self.winId()))

    def check_outside_click(self, event):
        if self.moving or QApplication.activePopupWidget() or QApplication.activeModalWidget():
            return
        hit = self.window_at(event.point)
        root = self.root_window(hit, 2) if hit else None
        if any(editor and root == int(editor.winId()) for editor in (self.title_editor, self.search_editor)):
            return
        if root != int(self.winId()):
            self.collapse_timer.start(0)

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.finish_title_edit()
            self.dismiss()
        super().mousePressEvent(event)

    def stop_interactions(self):
        if self.moving:
            self.moving = False
            if self.move_anchor is not None:
                self.releaseMouse()
                self.move_anchor = None
            QApplication.instance().removeEventFilter(self)
        self.finish_title_edit()
        self.close_search()
        self.click_hook.close()
        self.collapse_timer.stop()
        self.layer_timer.stop()
        self.stop_transition()
        self.icon_timer.stop()
        self.fade.stop()

    def nativeEvent(self, event_type, message):
        msg = W.MSG.from_address(int(message))
        if msg.message == 0x0021:  # WM_MOUSEACTIVATE
            return True, 3
        return False, 0

    def animate_opacity(self, opacity):
        if self.sprite is not None or abs(self.windowOpacity() - opacity) < .01:
            return
        self.fade.stop()
        if not self.animations_enabled():
            self.setWindowOpacity(opacity)
            return
        self.fade.setStartValue(self.windowOpacity())
        self.fade.setEndValue(opacity)
        self.fade.start()

    def enterEvent(self, event):
        self.animate_opacity(1)
        super().enterEvent(event)

    def leaveEvent(self, event):
        self.animate_opacity(.68)
        super().leaveEvent(event)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = QRectF(self.panel_rect if self.sprite is None else self.motion.currentValue())
        painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_Source)
        painter.fillRect(event.rect(), Qt.GlobalColor.transparent)
        painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceOver)
        accent = COLORS.get(self.group.get("color"), COLORS["mint"]) if self.group else None
        settings = self.manager.store.settings
        native.paint_desktop_panel(painter, rect, settings.organizer_opacity, accent, settings.organizer_background_color)
        if self.sprite is not None:
            # Show one fully lit page throughout the size change.
            painter.setClipRect(rect.adjusted(2, 2, -2, -2))
            anchor = rect.topLeft()
            if self.alignment == "bottom":
                anchor.setY(rect.bottom() - self.sprite.deviceIndependentSize().height())
            painter.drawPixmap(anchor, self.sprite)

    def contextMenuEvent(self, event):
        self.show_menu(event.globalPos())

    def edit_title(self):
        if self.title_editor is not None:
            return
        if self.sprite is not None:
            self.settle()
        editor = ShelfTitleEditor(self, self.group["id"] if self.group else None)
        self.title_editor = editor
        editor.setText(self.title.text())
        width = max(160, self.title.width())
        anchor = self.title.mapToGlobal(QPoint()) if self.title.isVisible() else self.body.mapToGlobal(
            QPoint(CELL_MARGIN, PANEL_PADDING // 2))
        editor.setGeometry(QRect(anchor, QSize(width, self.title.height())))
        editor.show()
        editor.raise_()
        editor.activateWindow()
        editor.setFocus(Qt.FocusReason.MouseFocusReason)
        editor.selectAll()

    def finish_title_edit(self, save=True):
        editor, self.title_editor = self.title_editor, None
        if editor is None:
            return
        text, group_id = editor.text(), editor.group_id
        editor.close()
        if not save:
            return
        if group_id is None:
            self.manager.rename_shelf(text)
        else:
            group = self.manager.collections.find(group_id)
            if group is not None:
                self.manager.rename_group(group, text)

    def update_group_name(self, group):
        if self.group is group:
            self.title.setText(group_name(group))
        for index in range(self.tiles.count()):
            tile = self.tiles.itemAt(index).widget()
            if tile.group is group:
                tile.update_name()
                break

    def reorder_tiles(self):
        tiles = {tile.group["id"]: tile for tile in
                 (self.tiles.itemAt(index).widget() for index in range(self.tiles.count()))}
        groups = self.manager.collections.groups
        if set(tiles) != {group["id"] for group in groups}:
            self.refresh()
            return
        for tile in tiles.values():
            self.tiles.removeWidget(tile)
        for index, group in enumerate(groups):
            row, column = divmod(index, self.board_columns)
            self.tiles.addWidget(tiles[group["id"]], row, column)

    def show_menu(self, point):
        self.finish_title_edit()
        menu = QMenu(self)
        if self.moving:
            menu.addAction(tr("取消移动位置"), self.end_move)
            menu.exec(point)
            return
        if self.alignment == "center":
            menu.addAction(tr("移动位置"), self.begin_move)
            if self.manager.store.settings.organizer_position is not None:
                menu.addAction(tr("恢复居中"), lambda: self.manager.set_position(None))
            menu.addSeparator()
        if self.group:
            group = self.group
            menu.addAction(tr("返回文件夹"), self.overview)
            menu.addAction(tr("添加应用或文件…"), lambda: self.manager.choose_files(group))
            menu.addAction(tr("添加文件夹…"), lambda: self.manager.choose_folder(group))
            menu.addAction(tr("重命名分组"), self.edit_title)
            menu.addAction(tr("按名称排序"), lambda: self.manager.sort(group))
        else:
            if not self.expanded:
                menu.addAction(tr("浏览桌面项目"), lambda: self.set_expanded(True))
            menu.addAction(tr("同步桌面分类"), lambda: self.manager.sync_desktop())
            menu.addAction(tr("编辑收纳标题"), self.edit_title)
        if self.expanded and self.search_editor is None:
            menu.addAction(tr("搜索桌面项目"), self.toggle_search)
        self.add_sort_menu(menu)
        menu.addAction(tr("添加新分组"), self.manager.create_group)
        menu.addAction(tr("打开收纳设置"), self.manager.open_settings)
        menu.addSeparator()
        controller = self.manager.controller
        menu.addAction(controller.icon, tr("新建便签"), lambda: controller.desktop_action("note"))
        sound = menu.addAction(controller.icon, tr("轻块桌面声音"))
        sound.setCheckable(True)
        sound.setChecked(controller.store.settings.wallpaper_sound)
        sound.toggled.connect(controller.set_wallpaper_sound)
        menu.addSeparator()
        menu.addAction(tr("关闭收纳并恢复桌面"), lambda: self.manager.set_enabled(False))
        menu.exec(point)

    def add_sort_menu(self, menu):
        sorting = menu.addMenu(tr("排列方式"))
        for key, title in view_options():
            action = sorting.addAction(title)
            action.setCheckable(True)
            action.setChecked(key == self.view_mode)
            action.triggered.connect(lambda checked=False, mode=key: self.manager.set_view(mode))


class OrganizerManager(QObject):
    changed = Signal()
    error = Signal(str)

    def __init__(self, controller):
        super().__init__()
        self.controller, self.store = controller, controller.store
        self.collections = Collections(self.store)
        self.shelf = None
        self.loose_icons = {}
        from .auto_collect import AutoCollector
        self.auto_collector = AutoCollector(self)
        self.desktop_icons = DesktopIconLease(self.store.directory)
        self.provider = QFileIconProvider()
        self.icons = OrderedDict()
        self.last_launch = (None, 0.0)
        self.watcher = native.DesktopWatcher(self)
        self.watcher.changed.connect(self.rebuild)
        self.desktop_paths = QFileSystemWatcher(self)
        self.desktop_snapshot = {}
        self.desktop_sync = QTimer(self, singleShot=True, interval=250)
        self.desktop_sync.timeout.connect(self.sync_new_desktop_entries)
        self.desktop_paths.directoryChanged.connect(lambda _: self.desktop_sync.start())
        self.repair_timer = QTimer(self)
        self.repair_timer.setSingleShot(True)
        self.repair_timer.setInterval(900)
        self.repair_timer.timeout.connect(self.repair)
        self.repair_attempts = 0
        self.controller.app.screenRemoved.connect(self.screen_changed)
        self.controller.app.screenAdded.connect(self.screen_changed)
        self.controller.app.primaryScreenChanged.connect(self.screen_changed)
    def initialize(self):
        if self.store.settings.organizer_enabled:
            self.set_enabled(True)
        elif self.desktop_icons.path.exists():
            try:
                self.desktop_icons.restore()
            except OSError as exc:
                self.error.emit(str(exc))

    @property
    def enabled(self):
        return bool(self.store.settings.organizer_enabled)

    def set_enabled(self, value):
        try:
            if value:
                self.restore_legacy_files()
                self.sync_desktop(refresh=False)
                if self.shelf is None:
                    self.shelf = DesktopShelf(self)
                self.desktop_icons.hide()
                self.watcher.start()
            else:
                self.desktop_icons.restore()
                self.watcher.stop()
                self.stop_desktop_updates()
                self.repair_timer.stop()
                self.restore_legacy_files()
            self.store.settings.organizer_enabled = bool(value)
            self.store.save()
        except OSError as exc:
            if value:
                self.store.settings.organizer_enabled = False
                self.watcher.stop()
                self.stop_desktop_updates()
                self.close_shelf()
                try:
                    if self.desktop_icons.handle:
                        self.desktop_icons.restore()
                except OSError as recovery:
                    self.error.emit(str(recovery))
            self.error.emit(str(exc))
        self.refresh()

    def sync_desktop(self, refresh=True, *, automatic=False):
        try:
            roots = native.desktop_folders()
            if self.enabled or not refresh:
                wanted = {path.as_posix() for path in roots}
                current = set(self.desktop_paths.directories())
                if current - wanted:
                    self.desktop_paths.removePaths(list(current - wanted))
                if wanted - current:
                    self.desktop_paths.addPaths(list(wanted - current))
            previous_count = sum(len(group["entries"]) for group in self.collections.groups)
            previous_loose = tuple((e["id"], e["target"]) for e in self.collections.loose)
            count = self.collections.classify_desktop(roots, snapshot=self.desktop_snapshot, only_new=automatic,
                                                       defer_new=automatic)
            if refresh and (not automatic or count or previous_count != sum(len(group["entries"]) for group in self.collections.groups)
                    or previous_loose != tuple((e["id"], e["target"]) for e in self.collections.loose)):
                self.refresh()
            elif automatic and self.shelf is not None and organizer_view(self.store.settings) == "time":
                self.shelf.refresh()
            return count
        except OSError as exc:
            if not refresh:
                raise
            self.error.emit(str(exc))
            return 0

    def sync_new_desktop_entries(self):
        if self.enabled:
            self.sync_desktop(automatic=True)

    def stop_desktop_updates(self):
        self.desktop_sync.stop()
        paths = self.desktop_paths.directories()
        if paths:
            self.desktop_paths.removePaths(paths)
        self.desktop_snapshot.clear()

    def set_view(self, view):
        settings = self.store.settings
        if view == organizer_view(settings):
            return
        previous = settings.organizer_views
        settings.organizer_views = {**previous, settings.organizer_alignment: view}
        try:
            self.store.save()
        except OSError as exc:
            settings.organizer_views = previous
            self.error.emit(str(exc))
        self.refresh()

    def restore_legacy_files(self):
        errors = []
        for group in self.collections.groups:
            for entry in list(group["entries"]):
                if entry.get("managed"):
                    try:
                        self.collections.restore_entry(group, entry, keep_reference=True)
                    except OSError as exc:
                        errors.append(str(exc))
        if errors:
            self.error.emit(tr("部分旧版文件仍在收纳目录。\\n")+"\\n".join(errors[:8]))

    def close_shelf(self):
        shelf, self.shelf = self.shelf, None
        if shelf is not None:
            shelf.stop_interactions()
            shelf.hide()
            shelf.deleteLater()

    def refresh_appearance(self):
        if self.shelf is not None:
            if self.shelf.sprite is not None:
                self.shelf.settle()
            self.shelf.refresh_colors()
            self.shelf.update()
            self.shelf.grid.viewport().update()
            for tile in self.shelf.board.findChildren(FolderTile):
                tile.update()
        self.changed.emit()

    def refresh(self):
        from .desktop_loose import refresh_loose_icons
        if self.enabled:
            if self.shelf is None:
                self.shelf = DesktopShelf(self)
            else:
                self.shelf.refresh()
            self.shelf.show()
        else:
            self.close_shelf()
        refresh_loose_icons(self)
        self.auto_collector.sync()
        used = {e["target"] for g in self.collections.groups for e in g["entries"]} if self.enabled else set()
        self.icons = OrderedDict((key, icon) for key, icon in self.icons.items() if key in used)
        self.changed.emit()

    def put_away(self, entry, group=None):
        try:
            self.collections.put_away(entry, group)
            self.refresh()
        except OSError as exc:
            self.error.emit(str(exc))

    def screen_changed(self, *_):
        if self.enabled:
            self.refresh()

    def rebuild(self):
        if self.enabled:
            self.repair_attempts = 0
            self.repair_timer.start()

    def repair(self):
        if not self.enabled:
            return
        try:
            self.desktop_icons.hide()
        except OSError as exc:
            self.repair_attempts += 1
            if self.repair_attempts < 8:
                self.repair_timer.start()
            else:
                self.error.emit(str(exc))
            return
        self.watcher.start()
        self.close_shelf()
        self.refresh()
        for icon in self.loose_icons.values():
            native.desktop_layer(int(icon.winId()))
        self.desktop_sync.start()

    def icon_for(self, entry):
        target = entry["target"]
        if target not in self.icons:
            if entry.get("system"):
                kind = QFileIconProvider.IconType.Computer if entry["system"] == "computer" else QFileIconProvider.IconType.Trashcan if entry["system"] == "recycle" else QFileIconProvider.IconType.Desktop
                self.icons[target] = self.provider.icon(kind)
            else:
                self.icons[target] = self.provider.icon(QFileInfo(target))
            if len(self.icons) > 256:
                self.icons.popitem(last=False)
        else:
            self.icons.move_to_end(target)
        return self.icons[target]

    def add_paths(self, group, paths):
        try:
            self.collections.add_paths(group, paths)
            self.refresh()
        except OSError as exc:
            self.error.emit(str(exc))

    def choose_files(self, group):
        paths, _ = QFileDialog.getOpenFileNames(None, tr("添加应用或文件入口 · 保留原文件"), "", tr("所有文件 (*)"))
        self.add_paths(group, paths)

    def choose_folder(self, group):
        path = QFileDialog.getExistingDirectory(None, tr("添加文件夹入口"))
        if path:
            self.add_paths(group, [path])

    def create_group(self):
        group = self.collections.create(save=False)
        try:
            self.collections.write_manifest()
        except OSError as exc:
            self.collections.groups.remove(group)
            self.error.emit(str(exc))
            return
        self.refresh()

    def launch(self, entry, debounce=False):
        import time
        now = time.monotonic()
        if debounce and self.last_launch[0] == entry["id"] and now-self.last_launch[1] < QApplication.doubleClickInterval()/1000 + 0.1:
            return
        self.last_launch = (entry["id"], now)
        try:
            native.open_target(entry["target"])
            if self.shelf is not None:
                self.shelf.dismiss()
        except OSError as exc:
            self.error.emit(str(exc))

    def rename(self, group):
        name, accepted = QInputDialog.getText(None, tr("重命名分组"), tr("分组名称"), text=group_name(group))
        if accepted:
            self.rename_group(group, name)

    def rename_shelf(self, text):
        name = text.strip()[:40]
        settings = self.store.settings
        previous = settings.organizer_title
        if name == (tr(previous) if previous == DEFAULT_ORGANIZER_TITLE else previous):
            return
        settings.organizer_title = name
        try:
            self.store.save()
        except OSError as exc:
            settings.organizer_title = previous
            self.error.emit(str(exc))
            return
        if self.shelf is not None and self.shelf.group is None:
            self.shelf.refresh()

    def rename_group(self, group, text):
        name = text.strip()[:40]
        previous = group["name"]
        if name == group_name(group):
            return
        group["name"] = name
        try:
            self.collections.write_manifest()
        except OSError as exc:
            group["name"] = previous
            self.error.emit(str(exc))
            return
        if self.shelf is not None:
            self.shelf.update_group_name(group)
        self.changed.emit()

    def rename_entry(self, group, entry):
        name, accepted = QInputDialog.getText(None, tr("更改显示名称"), tr("不会重命名原文件"), text=entry_name(entry))
        if accepted and name.strip():
            entry["name"] = name.strip()[:80]
            self.store.save()
            self.refresh()

    def set_group(self, group, key, value):
        previous = group.get(key)
        group[key] = value
        try:
            self.collections.write_manifest()
        except OSError as exc:
            group[key] = previous
            self.error.emit(str(exc))
        self.refresh()

    def sort(self, group):
        group["entries"].sort(key=lambda e: entry_name(e).casefold())
        self.store.save()
        self.refresh()

    def remove_entry(self, group, entry):
        try:
            self.collections.restore_entry(group, entry)
            self.refresh()
        except OSError as exc:
            self.error.emit(str(exc))

    def open_settings(self):
        self.controller.show_window()
        self.controller.window.pages.setCurrentIndex(1)

    def set_position(self, point):
        settings = self.store.settings
        previous = settings.organizer_position
        settings.organizer_position = None if point is None else [point.x(), point.y()]
        try:
            self.store.save()
        except OSError as exc:
            settings.organizer_position = previous
            self.error.emit(str(exc))
        if self.shelf is not None:
            self.shelf.refresh()

    def move_group(self, source_id, target_id):
        groups = self.collections.groups
        source = self.collections.find(source_id)
        target = self.collections.find(target_id)
        if source is None or target is None or source is target:
            return
        original = groups[:]
        start, end = groups.index(source), groups.index(target)
        groups.insert(end, groups.pop(start))
        try:
            self.collections.write_manifest()
        except OSError as exc:
            groups[:] = original
            self.error.emit(str(exc))
            return
        if self.shelf is not None:
            self.shelf.reorder_tiles()
        self.changed.emit()

    def cleanup(self):
        self.auto_collector.stop()
        self.watcher.stop()
        self.stop_desktop_updates()
        self.repair_timer.stop()
        self.close_shelf()
        if hasattr(self, "loose_refresh"):
            self.loose_refresh.stop()
        for icon in self.loose_icons.values():
            icon.hide()
            icon.deleteLater()
        self.loose_icons.clear()
        try:
            if self.desktop_icons.handle or self.desktop_icons.path.exists():
                self.desktop_icons.restore()
        except OSError as exc:
            import logging
            logging.error("Desktop icons restore pending: %s", exc)
            self.desktop_icons.release_handle()
