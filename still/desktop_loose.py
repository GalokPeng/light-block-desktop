"""Desktop-owned icons for newly created files awaiting explicit collection."""
import ctypes
from time import monotonic

from PySide6.QtCore import QPoint, QRectF, QSize, Qt, QTimer
from PySide6.QtGui import QColor, QCursor, QPainter
from PySide6.QtWidgets import QApplication, QMenu, QToolTip, QWidget

from . import desktop_windows as native, win32
from .i18n import tr
from .organizer_model import entry_name, group_name, organizer_view


def desktop_is_foreground():
    name = ctypes.create_unicode_buffer(128)
    win32.GetClassName(win32.GetForegroundWindow(), name, len(name))
    return name.value in {"Progman", "WorkerW"}


class LooseDesktopIcon(QWidget):
    def __init__(self, manager, entry):
        super().__init__(None, Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint |
                         Qt.WindowType.NoDropShadowWindowHint)
        self.manager, self.entry = manager, entry
        self.press = self.origin = None
        self.moved = False
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setWindowTitle(entry_name(entry))
        self.setFixedSize(100, 104)
        self.setToolTip(tr("右键可以放入收纳哦"))
        self.setAccessibleName(entry_name(entry))
        self.icon = manager.icon_for(entry).pixmap(QSize(64, 64))
        self.hint_timer = QTimer(self, singleShot=True, interval=500)
        self.hint_timer.timeout.connect(self.hint)

    def showEvent(self, event):
        super().showEvent(event)
        native.desktop_layer(int(self.winId()))

    def hint(self):
        if self.isVisible() and desktop_is_foreground():
            QToolTip.showText(self.mapToGlobal(QPoint(72, 70)), tr("右键可以放入收纳哦"), self, self.rect(), 4500)

    def paintEvent(self, event):
        with QPainter(self) as painter:
            painter.setRenderHint(QPainter.RenderHint.Antialiasing)
            if self.underMouse():
                painter.setPen(Qt.PenStyle.NoPen)
                painter.setBrush(QColor(200, 225, 255, 38))
                painter.drawRoundedRect(QRectF(self.rect()).adjusted(2, 2, -2, -2), 9, 9)
            native.paint_icon(painter, self.icon, QRectF(25, 7, 50, 50))
            text = self.fontMetrics().elidedText(entry_name(self.entry), Qt.TextElideMode.ElideRight, 92)
            area = self.rect().adjusted(4, 61, -4, -2)
            painter.setPen(QColor(0, 0, 0, 230))
            painter.drawText(area.translated(1, 1), Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignTop, text)
            painter.setPen(QColor("#ffffff"))
            painter.drawText(area, Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignTop, text)

    def enterEvent(self, event):
        self.update()
        super().enterEvent(event)

    def leaveEvent(self, event):
        self.update()
        super().leaveEvent(event)

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.press, self.origin = event.globalPosition().toPoint(), self.pos()
            self.moved = False

    def mouseMoveEvent(self, event):
        if self.press is None or not event.buttons() & Qt.MouseButton.LeftButton:
            return
        delta = event.globalPosition().toPoint() - self.press
        if self.moved or delta.manhattanLength() >= QApplication.startDragDistance():
            self.moved = True
            screen = native.current_screen(QApplication.screenAt(event.globalPosition().toPoint()))
            if screen:
                area = screen.availableGeometry()
                point = self.origin + delta
                self.move(max(area.left(), min(point.x(), area.right()-self.width()+1)),
                          max(area.top(), min(point.y(), area.bottom()-self.height()+1)))

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton and self.press is not None:
            self.press = None
            if self.moved:
                previous = self.entry.get("desktop_position")
                self.entry["desktop_position"] = [self.x(), self.y()]
                try:
                    self.manager.collections.write_manifest()
                except OSError as exc:
                    self.entry["desktop_position"] = previous
                    self.move(self.origin)
                    self.manager.error.emit(str(exc))

    def mouseDoubleClickEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.manager.launch(self.entry)

    def contextMenuEvent(self, event):
        menu = QMenu(self)
        menu.addAction(tr("打开"), lambda: self.manager.launch(self.entry))
        menu.addAction(tr("在文件夹中显示"), lambda: native.reveal_file(self.entry["target"]))
        menu.addSeparator()
        menu.addAction(tr("放入收纳"), lambda: self.manager.put_away(self.entry))
        if organizer_view(self.manager.store.settings) == "groups":
            for group in self.manager.collections.groups:
                menu.addAction(tr("放入「{0}」分组").format(group_name(group)),
                               lambda checked=False, g=group: self.manager.put_away(self.entry, g))
        menu.exec(event.globalPos())


def refresh_loose_icons(manager):
    if not hasattr(manager, "loose_refresh"):
        manager.loose_refresh = QTimer(manager, singleShot=True, interval=16)
        manager.loose_refresh.timeout.connect(lambda: refresh_loose_icons(manager))
    manager.loose_refresh.stop()
    entries = {e["id"]: e for e in manager.collections.loose} if manager.enabled else {}
    for ident in list(manager.loose_icons):
        icon = manager.loose_icons[ident]
        if ident not in entries or icon.entry != entries[ident]:
            manager.loose_icons.pop(ident)
            icon.hide()
            icon.deleteLater()
    added = []
    for ident, entry in entries.items():
        if ident in manager.loose_icons:
            continue
        # A pasted directory can contain many entries. Bound icon extraction
        # and widget creation per event-loop turn, keeping the desktop usable.
        if len(added) >= 12:
            manager.loose_refresh.start()
            break
        icon = LooseDesktopIcon(manager, entry)
        position = entry.get("desktop_position")
        valid_position = isinstance(position, list) and len(position) == 2 and all(type(n) is int for n in position)
        screen = native.current_screen(QApplication.screenAt(QPoint(*position) if valid_position else QCursor.pos()))
        if screen is None:
            icon.deleteLater()
            continue
        area = screen.availableGeometry()
        if valid_position:
            point = QPoint(*position)
        else:
            point = QCursor.pos() if desktop_is_foreground() else area.topLeft() + QPoint(20, 20)
        x = max(area.left(), min(point.x(), area.right()-icon.width()+1))
        y = max(area.top(), min(point.y(), area.bottom()-icon.height()+1))
        # Keep a newly created batch readable rather than stacking all icons.
        for _ in range(max(1, area.width()//100 * (area.height()//104))):
            box = QRectF(x, y, icon.width(), icon.height()).toRect()
            occupied = [w.geometry() for w in manager.loose_icons.values()]
            if manager.shelf is not None:
                occupied.append(manager.shelf.geometry())
            if not any(box.intersects(other) for other in occupied):
                break
            y += icon.height() + 8
            if y + icon.height() > area.bottom() + 1:
                y = area.top() + 12
                x += icon.width() + 8
                if x + icon.width() > area.right() + 1:
                    x = area.left() + 12
        icon.move(x, y)
        manager.loose_icons[ident] = icon
        icon.show()
        added.append(icon)
    if added and monotonic() - getattr(manager, "last_loose_hint", 0) > 6:
        manager.last_loose_hint = monotonic()
        added[0].hint_timer.start()
