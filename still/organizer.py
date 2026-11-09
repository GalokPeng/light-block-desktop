from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
from collections import OrderedDict
import json
from pathlib import Path

from PySide6.QtCore import (QAbstractListModel, QElapsedTimer, QEasingCurve, QEvent, QFileInfo, QModelIndex, QMimeData, QObject, QPoint,
                          QPropertyAnimation, QRect, QRectF, QSize, Qt, QTimer, QVariantAnimation, Signal)
from PySide6.QtGui import QColor, QCursor, QDrag, QIcon, QPainter, QPen
from PySide6.QtWidgets import (QAbstractButton, QAbstractItemView, QApplication, QFileDialog, QFileIconProvider,
    QGridLayout, QHBoxLayout, QInputDialog, QLabel, QLineEdit, QListView, QMenu, QPushButton,
    QScrollArea, QSizePolicy, QStackedWidget, QStyle, QVBoxLayout, QWidget)

from . import desktop_windows as native
from .desktop_icons import DesktopIconLease
from .organizer_model import COLORS, Collections
from .scroll_wheel import MouseHook
from . import win32

MIME = "application/x-still-collection-items"


class GroupItems(QAbstractListModel):
    ICON_CACHE_LIMIT = 128

    def __init__(self, view):
        super().__init__(view)
        self.view = view
        self.entries = ()
        self.icons = OrderedDict()
        self.pending = OrderedDict()
        self.placeholder = view.style().standardIcon(QStyle.StandardPixmap.SP_FileIcon)

    def set_entries(self, entries):
        self.beginResetModel()
        self.entries = tuple(entries)
        self.icons.clear()
        self.pending.clear()
        self.endResetModel()

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.entries)

    def data(self, index, role=Qt.ItemDataRole.DisplayRole):
        if not index.isValid():
            return None
        row = index.row()
        entry = self.entries[row]
        if role == Qt.ItemDataRole.DisplayRole:
            return entry["name"]
        if role == Qt.ItemDataRole.ToolTipRole:
            return entry["target"]
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
        return flags | Qt.ItemFlag.ItemIsDragEnabled if index.isValid() else flags


class IconGrid(QListView):
    def __init__(self, panel):
        super().__init__(panel)
        self.panel = panel
        self.setViewMode(QListView.ViewMode.IconMode)
        self.setResizeMode(QListView.ResizeMode.Adjust)
        self.setMovement(QListView.Movement.Static)
        self.setUniformItemSizes(True)
        self.setLayoutMode(QListView.LayoutMode.Batched)
        self.setBatchSize(48)
        self.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        self.setWordWrap(False)
        self.setTextElideMode(Qt.TextElideMode.ElideRight)
        self.setSpacing(3)
        self.setDragEnabled(True)
        self.setAcceptDrops(True)
        self.setDropIndicatorShown(True)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.customContextMenuRequested.connect(self.menu)
        self.items = GroupItems(self)
        self.setModel(self.items)
        self.clicked.connect(self.single_click)
        self.setStyleSheet("""
            QListView { background:transparent; border:0; color:#f5f7fc; outline:0; font:11px 'Microsoft YaHei UI'; }
            QListView::item { border:1px solid transparent; border-radius:12px; padding:5px; }
            QListView::item:hover { background:rgba(255,255,255,25); border-color:rgba(255,255,255,35); }
            QListView::item:selected { background:rgba(174,221,255,42); border-color:rgba(201,232,255,95); }
        """)

    def single_click(self, index):
        if QApplication.keyboardModifiers() == Qt.KeyboardModifier.NoModifier:
            # A native double click can emit itemClicked twice. Debounce only
            # the single-click gesture, not ordinary double-click mode.
            self.panel.manager.launch(self.items.entries[index.row()], debounce=True)

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton and not self.indexAt(event.position().toPoint()).isValid():
            self.panel.overview()
            return
        super().mousePressEvent(event)

    def startDrag(self, _):
        data = {"group": self.panel.group["id"],
                "entries": [self.items.entries[i.row()]["id"] for i in self.selectedIndexes()]}
        mime = QMimeData()
        mime.setData(MIME, json.dumps(data).encode())
        drag = QDrag(self)
        drag.setMimeData(mime)
        if self.currentIndex().isValid():
            drag.setPixmap(self.currentIndex().data(Qt.ItemDataRole.DecorationRole).pixmap(40, 40))
        drag.exec(Qt.DropAction.MoveAction)

    def dragEnterEvent(self, event):
        if event.mimeData().hasFormat(MIME) or event.mimeData().hasUrls():
            event.setDropAction(Qt.DropAction.MoveAction if event.mimeData().hasFormat(MIME) else Qt.DropAction.CopyAction)
            event.accept()

    def dragMoveEvent(self, event):
        if event.mimeData().hasFormat(MIME) or event.mimeData().hasUrls():
            event.setDropAction(Qt.DropAction.MoveAction if event.mimeData().hasFormat(MIME) else Qt.DropAction.CopyAction)
            event.accept()

    def dropEvent(self, event):
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
        if not item.isValid():
            self.panel.show_menu(self.mapToGlobal(point))
            return
        entry = self.items.entries[item.row()]
        menu = QMenu(self)
        menu.addAction("打开", lambda: self.panel.manager.launch(entry))
        if not entry.get("system"):
            menu.addAction("在文件夹中显示", lambda: native.reveal_file(entry["target"]))
        menu.addAction("更改显示名称…", lambda: self.panel.manager.rename_entry(self.panel.group, entry))
        menu.addSeparator()
        for group in self.panel.manager.collections.groups:
            if group["id"] != self.panel.group["id"]:
                menu.addAction("移至「" + group["name"] + "」", lambda checked=False, g=group: self.transfer(entry, g))
        menu.addSeparator()
        menu.addAction("还原到原桌面" if entry.get("managed") else "移除快捷入口", lambda: self.panel.manager.remove_entry(self.panel.group, entry))
        menu.exec(self.mapToGlobal(point))

    def transfer(self, entry, group):
        self.panel.manager.collections.move_entries(self.panel.group, group, [entry["id"]])
        self.panel.manager.refresh()


class FolderTile(QAbstractButton):
    def __init__(self, shelf, group):
        super().__init__(shelf)
        self.shelf, self.group = shelf, group
        self.icons = [shelf.manager.icon_for(entry) for entry in group["entries"][:4]]
        self.setFixedSize(116, 114)
        self.update_name()
        self.setAcceptDrops(True)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.clicked.connect(lambda: shelf.open_group(group["id"]))

    def update_name(self):
        name = self.group["name"] or "未命名分组"
        self.setToolTip(f"{name} · {len(self.group['entries'])} 个项目")
        self.setAccessibleName(name)
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        plate = QRectF(21, 4, 74, 74)
        accent = QColor(COLORS.get(self.group.get("color"), COLORS["mint"]))
        tint = QColor(accent)
        tint.setAlpha(95 if self.isDown() else 64 if self.underMouse() else 30)
        painter.setPen(QPen(QColor(224, 235, 255, 80 if self.underMouse() else 42), 1))
        painter.setBrush(tint)
        painter.drawRoundedRect(plate, 18, 18)
        for n in range(4):
            x, y = 29 + (n%2)*30, 12 + (n//2)*30
            if n < len(self.icons):
                self.icons[n].paint(painter, QRect(x+2, y+2, 24, 24))
            else:
                painter.setPen(Qt.PenStyle.NoPen)
                painter.setBrush(QColor(232, 240, 255, 23))
                painter.drawRoundedRect(QRectF(x+3, y+3, 22, 22), 7, 7)
        painter.setPen(QColor("#f4f7fc"))
        title = painter.fontMetrics().elidedText(self.group["name"], Qt.TextElideMode.ElideRight, 110)
        painter.drawText(QRect(3, 81, 110, 21), Qt.AlignmentFlag.AlignHCenter, title)
        painter.setPen(QColor("#c5d2df"))
        painter.drawText(QRect(3, 101, 110, 13), Qt.AlignmentFlag.AlignHCenter, str(len(self.group["entries"])))

    def enterEvent(self, event):
        self.update()
        super().enterEvent(event)

    def leaveEvent(self, event):
        self.update()
        super().leaveEvent(event)

    def dragEnterEvent(self, event):
        if event.mimeData().hasUrls():
            event.setDropAction(Qt.DropAction.CopyAction)
            event.accept()

    def dropEvent(self, event):
        paths = [url.toLocalFile() for url in event.mimeData().urls() if url.isLocalFile()]
        if paths:
            self.shelf.manager.add_paths(self.group, paths)
            event.setDropAction(Qt.DropAction.CopyAction)
            event.accept()


class ShelfTitle(QAbstractButton):
    def __init__(self):
        super().__init__()
        self.setFixedHeight(30)
        self.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Fixed)
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.setAccessibleName("编辑收纳标题")
        self.setToolTip("点击修改名称")
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
        painter.setPen(QColor("#f4f7fc"))
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
        self.setAccessibleName("收纳名称")
        self.setPlaceholderText("留空可隐藏名称")
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
    """One desktop-owned window. A folder opens as a larger centered grid."""
    def __init__(self, manager):
        super().__init__(None, Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint | Qt.WindowType.WindowDoesNotAcceptFocus)
        self.manager = manager
        self.group = None
        self.title_editor = None
        self.board_columns = 0
        self.board_dirty = True
        self.setWindowTitle("轻块桌面 · 桌面收纳")
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setWindowOpacity(.68)
        self.motion = QVariantAnimation(self)
        self.motion.setEasingCurve(QEasingCurve.Type.OutCubic)
        self.motion.valueChanged.connect(lambda _: self.update())
        self.motion.finished.connect(self.settle)
        self.panel_rect = QRect()
        self.sprite = None
        self.grid_group = None
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
        self.collapse_timer.timeout.connect(self.overview)
        self.click_hook = MouseHook({0x0201, 0x0204}, self.check_outside_click)
        self.window_at = win32.bind(win32.user32, "WindowFromPoint", W.HWND, W.POINT)
        self.root_window = win32.bind(win32.user32, "GetAncestor", W.HWND, W.HWND, W.UINT)

        self.setStyleSheet("""
            QLabel { color:#f4f7fc;background:transparent; }
            QAbstractButton#shelfTitle { font:600 19px 'Microsoft YaHei UI'; }
            QLabel#shelfDetail { color:#c7d4e2;font-size:11px; }
            QPushButton { color:#f5f8ff;background:#2b3a4d;border:1px solid #4d6179;
                          border-radius:8px;padding:6px 10px; }
            QPushButton:hover { background:#3a5069; }
            QScrollArea,QScrollArea > QWidget,QScrollArea > QWidget > QWidget { background:transparent;border:0; }
        """)
        self.body = QWidget(self)
        self.body.setAttribute(Qt.WidgetAttribute.WA_NoSystemBackground)
        layout = QVBoxLayout(self.body)
        layout.setContentsMargins(19, 16, 19, 15)
        layout.setSpacing(9)
        header = QHBoxLayout()
        self.back = QPushButton("返回")
        self.back.clicked.connect(self.overview)
        header.addWidget(self.back)
        self.title = ShelfTitle()
        self.title.setObjectName("shelfTitle")
        self.title.clicked.connect(self.edit_title)
        header.addWidget(self.title)
        header.addStretch()
        self.detail = QLabel()
        self.detail.setObjectName("shelfDetail")
        header.addWidget(self.detail)
        layout.addLayout(header)
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
        self.refresh()

    def animations_enabled(self):
        enabled = W.BOOL(1)
        win32.SystemParametersInfo(0x1042, 0, C.byref(enabled), 0)
        return bool(enabled.value)

    def placement(self, width, height, start):
        area = QApplication.primaryScreen().availableGeometry()
        available = QSize(max(250, area.width() - 28), max(180, area.height() - 28))
        # A constant layered HWND avoids reallocating the Windows backing store
        # at either end of a transition. Pixels outside the panel stay transparent.
        host = QRect(QPoint(), QSize(700, 433).boundedTo(available))
        host.moveCenter(area.center())
        if self.geometry() != host:
            self.setGeometry(host)
        target = QRect(QPoint(), QSize(width, height).boundedTo(available))
        target.moveCenter(self.rect().center())
        self.panel_rect = target
        self.body.setGeometry(target)
        if start is None or start == target:
            self.resume_icons()
            return

        self.fade.stop()
        self.body.layout().activate()
        self.sprite = self.body.grab()
        self.body.setUpdatesEnabled(False)
        QApplication.instance().installEventFilter(self)
        self.motion.setDuration(135 if self.group else 120)
        self.motion.setStartValue(start)
        self.motion.setEndValue(target)
        self.motion.start()

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
        if (self.sprite is not None and event.type() == QEvent.Type.MouseButtonPress
                and isinstance(watched, QWidget) and (watched is self or self.isAncestorOf(watched))):
            self.settle()
        return super().eventFilter(watched, event)

    def populate_grid(self):
        size = self.group.get("icon_size", 44)
        self.grid.setIconSize(QSize(size, size))
        self.grid.setGridSize(QSize(size + 52, size + 53))
        self.grid_group = self.group["id"]
        self.grid.items.set_entries(self.group["entries"])

    def resume_icons(self):
        if (self.group and self.sprite is None and self.grid.items.pending
                and not self.icon_timer.isActive()):
            self.icon_timer.start()

    def load_icons(self):
        if self.sprite is not None or self.group is None:
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
                icon = self.manager.icon_for(items.entries[row])
                pixmap = icon.pixmap(self.grid.iconSize(), self.grid.devicePixelRatioF())
                items.cache_icon(row, QIcon(pixmap) if not pixmap.isNull() else items.placeholder)
                ready.append(row)
            if len(ready) >= 4 or budget.elapsed() >= 5:
                break
        if ready:
            items.dataChanged.emit(items.index(min(ready)), items.index(max(ready)), [Qt.ItemDataRole.DecorationRole])
        if not items.pending:
            self.icon_timer.stop()

    def overview(self):
        self.collapse_timer.stop()
        if self.group is None:
            return
        self.finish_title_edit()
        self.click_hook.close()
        self.group = None
        self.refresh(animate=True)

    def open_group(self, ident):
        group = self.manager.collections.find(ident)
        if group:
            self.finish_title_edit()
            self.collapse_timer.stop()
            self.group = group
            self.refresh(animate=True)
            self.click_hook.start()

    def refresh(self, animate=False):
        start = None
        if animate and self.isVisible() and self.animations_enabled():
            start = self.motion.currentValue() if self.sprite is not None else self.panel_rect
        self.stop_transition()
        self.icon_timer.stop()
        if not animate:
            self.board_dirty = True
            self.grid_group = None
            self.grid.items.pending.clear()
        if self.group is not None:
            self.group = self.manager.collections.find(self.group["id"])
        if self.group:
            self.back.show()
            self.title.setText(self.group["name"])
            self.detail.setText(f"{len(self.group['entries'])} 个项目")
            self.detail.show()
            if self.grid_group != self.group["id"]:
                self.populate_grid()
            self.pages.setCurrentWidget(self.grid)
            self.placement(700, 420, start)
        else:
            self.click_hook.close()
            if self.grid_group is None:
                self.grid.items.set_entries(())
            self.back.hide()
            groups = self.manager.collections.groups
            self.title.setText(self.manager.store.settings.organizer_title)
            self.detail.hide()
            area = QApplication.primaryScreen().availableGeometry()
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
        self.update()

    def showEvent(self, event):
        super().showEvent(event)
        self.layer_timer.start(0)
        self.setWindowOpacity(1 if self.panel_rect.contains(self.mapFromGlobal(QCursor.pos())) else .68)

    def attach_to_desktop(self):
        native.desktop_layer(int(self.winId()))

    def check_outside_click(self, event):
        if self.group is None or QApplication.activePopupWidget() or QApplication.activeModalWidget():
            return
        hit = self.window_at(event.point)
        root = self.root_window(hit, 2) if hit else None
        if self.title_editor and root == int(self.title_editor.winId()):
            return
        if root != int(self.winId()):
            self.collapse_timer.start(0)

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.finish_title_edit()
            if self.group:
                self.overview()
        super().mousePressEvent(event)

    def stop_interactions(self):
        self.finish_title_edit()
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
        native.paint_desktop_panel(painter, rect, self.manager.store.settings.organizer_opacity)
        if self.sprite is not None:
            # Show one fully lit page throughout the size change.
            painter.setClipRect(rect.adjusted(2, 2, -2, -2))
            painter.drawPixmap(rect.topLeft(), self.sprite)

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
        editor.setGeometry(QRect(self.title.mapToGlobal(QPoint()), QSize(width, self.title.height())))
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
            self.title.setText(group["name"])
        for index in range(self.tiles.count()):
            tile = self.tiles.itemAt(index).widget()
            if tile.group is group:
                tile.update_name()
                break

    def show_menu(self, point):
        self.finish_title_edit()
        menu = QMenu(self)
        if self.group:
            group = self.group
            menu.addAction("返回文件夹", self.overview)
            menu.addAction("添加应用或文件…", lambda: self.manager.choose_files(group))
            menu.addAction("添加文件夹…", lambda: self.manager.choose_folder(group))
            menu.addAction("重命名分组", self.edit_title)
            menu.addAction("按名称排序", lambda: self.manager.sort(group))
        else:
            menu.addAction("同步桌面分类", lambda: self.manager.sync_desktop())
            menu.addAction("编辑收纳标题", self.edit_title)
        menu.addAction("添加新分组", self.manager.create_group)
        menu.addAction("打开收纳设置", self.manager.open_settings)
        menu.addSeparator()
        menu.addAction("关闭收纳并恢复桌面", lambda: self.manager.set_enabled(False))
        menu.exec(point)


class OrganizerManager(QObject):
    changed = Signal()
    error = Signal(str)

    def __init__(self, controller):
        super().__init__()
        self.controller, self.store = controller, controller.store
        self.collections = Collections(self.store)
        self.shelf = None
        self.desktop_icons = DesktopIconLease(self.store.directory)
        self.provider = QFileIconProvider()
        self.icons = {}
        self.last_launch = (None, 0.0)
        self.watcher = native.DesktopWatcher(self)
        self.watcher.changed.connect(self.rebuild)
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
                self.repair_timer.stop()
                self.restore_legacy_files()
            self.store.settings.organizer_enabled = bool(value)
            self.store.save()
        except OSError as exc:
            if value:
                self.store.settings.organizer_enabled = False
                self.watcher.stop()
                self.close_shelf()
                try:
                    if self.desktop_icons.handle:
                        self.desktop_icons.restore()
                except OSError as recovery:
                    self.error.emit(str(recovery))
            self.error.emit(str(exc))
        self.refresh()

    def sync_desktop(self, refresh=True):
        try:
            count = self.collections.classify_desktop(native.desktop_folders())
            if refresh:
                self.refresh()
            return count
        except OSError as exc:
            if not refresh:
                raise
            self.error.emit(str(exc))
            return 0

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
            self.error.emit("部分旧版文件仍在收纳目录。\\n"+"\\n".join(errors[:8]))

    def close_shelf(self):
        shelf, self.shelf = self.shelf, None
        if shelf is not None:
            shelf.stop_interactions()
            shelf.hide()
            shelf.deleteLater()

    def refresh(self):
        if self.enabled:
            if self.shelf is None:
                self.shelf = DesktopShelf(self)
            else:
                self.shelf.refresh()
            self.shelf.show()
        else:
            self.close_shelf()
        used = {e["target"] for g in self.collections.groups for e in g["entries"]} if self.enabled else set()
        self.icons = {key: icon for key, icon in self.icons.items() if key in used}
        self.changed.emit()

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

    def icon_for(self, entry):
        target = entry["target"]
        if target not in self.icons:
            if entry.get("system"):
                kind = QFileIconProvider.IconType.Computer if entry["system"] == "computer" else QFileIconProvider.IconType.Trashcan if entry["system"] == "recycle" else QFileIconProvider.IconType.Desktop
                self.icons[target] = self.provider.icon(kind)
            else:
                self.icons[target] = self.provider.icon(QFileInfo(target))
        return self.icons[target]

    def add_paths(self, group, paths):
        try:
            self.collections.add_paths(group, paths)
            self.refresh()
        except OSError as exc:
            self.error.emit(str(exc))

    def choose_files(self, group):
        paths, _ = QFileDialog.getOpenFileNames(None, "添加应用或文件入口 · 保留原文件", "", "所有文件 (*)")
        self.add_paths(group, paths)

    def choose_folder(self, group):
        path = QFileDialog.getExistingDirectory(None, "添加文件夹入口")
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
                self.shelf.overview()
        except OSError as exc:
            self.error.emit(str(exc))

    def rename(self, group):
        name, accepted = QInputDialog.getText(None, "重命名分组", "分组名称", text=group["name"])
        if accepted:
            self.rename_group(group, name)

    def rename_shelf(self, text):
        name = text.strip()[:40]
        settings = self.store.settings
        previous = settings.organizer_title
        if name == previous:
            return
        settings.organizer_title = name
        try:
            self.store.save()
        except OSError as exc:
            settings.organizer_title = previous
            self.error.emit(str(exc))
            return
        if self.shelf is not None and self.shelf.group is None:
            self.shelf.title.setText(name)

    def rename_group(self, group, text):
        name = text.strip()[:40]
        previous = group["name"]
        if name == previous:
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
        name, accepted = QInputDialog.getText(None, "更改显示名称", "不会重命名原文件", text=entry["name"])
        if accepted and name.strip():
            entry["name"] = name.strip()[:80]
            self.store.save()
            self.refresh()

    def set_group(self, group, key, value):
        group[key] = value
        self.store.save()
        self.refresh()

    def sort(self, group):
        group["entries"].sort(key=lambda e: e["name"].casefold())
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

    def cleanup(self):
        self.watcher.stop()
        self.repair_timer.stop()
        self.close_shelf()
        try:
            if self.desktop_icons.handle or self.desktop_icons.path.exists():
                self.desktop_icons.restore()
        except OSError as exc:
            import logging
            logging.error("Desktop icons restore pending: %s", exc)
            self.desktop_icons.release_handle()
