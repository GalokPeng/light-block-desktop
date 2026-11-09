"""A non-activating app shelf alongside the real Windows notification area."""
from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path

from PySide6.QtCore import QEasingCurve, QEvent, QFileInfo, QPoint, QPointF, QProcess, QRectF, QSize, Qt, QTimer, QVariantAnimation
from PySide6.QtGui import QColor, QLinearGradient, QPainter, QPen, QPixmap
from PySide6.QtWidgets import QFileDialog, QFileIconProvider, QMenu, QMessageBox, QToolTip, QWidget

from . import dock_windows, win32
from .desktop_windows import open_target
from .dock_search import DockSearch
from .i18n import tr
from .model import APPS_FOLDER, dock_target_key, normalize_dock_pins
from .taskbar_style import TaskbarStyle
from .taskbar_style_native import tray_metrics

SLOT = 57
INSET = 12
HEIGHT = 86
BASE_ICON = 30
CONTROL_SIZE = 39
CONTROL_WIDTH = 128
GROUP_GAP = 6
EDGE_RESERVE = 24
POWER_SLOT, SEARCH_SLOT = -2, -3
EXPLORER = str(Path(os.environ.get("WINDIR", r"C:\Windows")) / "explorer.exe")


@dataclass(frozen=True)
class DockEntry:
    target: str
    pinned: bool
    windows: tuple[dock_windows.AppWindow, ...] = ()


def group_entries(pins, windows, shortcuts):
    groups = {}
    for window in windows:
        groups.setdefault(dock_target_key(window.target), []).append(window)
    aliases = {dock_target_key(group[0].executable): key for key, group in groups.items()}
    entries = []
    for target in pins:
        key = dock_target_key(target)
        executable = shortcuts.get(target, key)
        group_key = key if key in groups else aliases.get(executable, executable)
        entries.append(DockEntry(target, True, tuple(groups.pop(group_key, ()))))
    entries.extend(DockEntry(groups[key][0].target, False, tuple(groups[key])) for key in sorted(groups))
    return entries


def paint_pill(painter, pill, radius, light, tray=False):
    pill = pill.adjusted(0.5, 0.5, -0.5, -0.5)
    top, bottom = ((248, 252, 255), (226, 236, 240)) if light else ((44, 55, 66), (16, 23, 32))
    opacity = (54, 39) if tray else ((227, 217) if light else (223, 231))
    glass = QLinearGradient(pill.topLeft(), pill.bottomLeft())
    glass.setColorAt(0, QColor(*top, opacity[0]))
    glass.setColorAt(1, QColor(*bottom, opacity[1]))
    painter.setBrush(glass)
    painter.setPen(QPen(QColor(255, 255, 255, 218 if light else 71), 1))
    painter.drawRoundedRect(pill, radius, radius)


class TrayFrame(QWidget):
    def __init__(self, dock):
        super().__init__(dock, Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint |
                         Qt.WindowType.WindowStaysOnTopHint | Qt.WindowType.WindowDoesNotAcceptFocus |
                         Qt.WindowType.WindowTransparentForInput | Qt.WindowType.NoDropShadowWindowHint)
        self.dock = dock
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)

    def paintEvent(self, _):
        inset = self.dock.tray_inset
        pill = QRectF(0, inset, self.width(), self.height() - 2 * inset)
        with QPainter(self) as painter:
            painter.setRenderHint(QPainter.RenderHint.Antialiasing)
            paint_pill(painter, pill, self.dock.pill_radius, self.dock.light, tray=True)


class Dock(QWidget):
    def __init__(self, controller):
        super().__init__(None, Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint |
                         Qt.WindowType.WindowStaysOnTopHint | Qt.WindowType.WindowDoesNotAcceptFocus |
                         Qt.WindowType.NoDropShadowWindowHint)
        self.controller = controller
        self.settings = controller.store.settings
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setMouseTracking(True)
        self.setAcceptDrops(True)
        self.setWindowTitle(tr("应用 Dock"))
        self.entries = []
        self.visible_entries = []
        self.overflow = []
        self.visuals = {}
        self.icon_dpr = 1.0
        self.provider = QFileIconProvider()
        self.windows = []
        self.shortcuts = {}
        self.press = None
        self.drag_slot = -1
        self.menu = None
        self.hover = -1
        self.scales = []
        self.scale_start = []
        self.scale_end = []
        self.screen_source = None
        self.monitor_rect = (0, 0, 0, 0)
        self.tray_bounds = None
        self.tray_ready = False
        self.tray_height = 0
        self.tray_inset = 0
        self.pill_radius = 18
        self.tray_frame = TrayFrame(self)
        self.search = DockSearch(self)
        self.search.failed.connect(lambda: self.controller.report_error(tr("无法打开 Windows 搜索。请按 Win+S。")))
        self.app_left = 0
        self.app_width = 0
        self.animation = QVariantAnimation(self, startValue=0.0, endValue=1.0, duration=145)
        self.animation.setEasingCurve(QEasingCurve.Type.OutCubic)
        self.animation.valueChanged.connect(self.animate_frame)
        self.refresh_pending = False
        self.refresh_timer = QTimer(self, singleShot=True, interval=100)
        self.refresh_timer.timeout.connect(self.refresh_tasks)
        self.watcher = dock_windows.TaskWatcher(self)
        self.watcher.changed.connect(self.schedule_refresh)
        self.taskbar_style = TaskbarStyle(self)
        self.taskbar_style.layout_ready.connect(self.tray_layout)
        self.taskbar_style.failed.connect(self.style_failed)
        self.watcher.shell_changed.connect(self.taskbar_style.schedule_refresh)
        self.controller.app.primaryScreenChanged.connect(self.bind_screen)
        self.bind_screen(self.controller.app.primaryScreen())
        self.refresh_shortcuts()
        self.refresh_pending = True
        self.refresh_tasks()

    @property
    def pins(self):
        pins = self.settings.dock_pins
        return pins if pins is not None else [EXPLORER]

    def bind_screen(self, screen):
        if self.screen_source is not None and self.screen_source in self.controller.app.screens():
            for name in ("availableGeometryChanged", "geometryChanged", "logicalDotsPerInchChanged"):
                getattr(self.screen_source, name).disconnect(self.screen_changed)
        self.screen_source = screen
        if screen is not None:
            for name in ("availableGeometryChanged", "geometryChanged", "logicalDotsPerInchChanged"):
                getattr(screen, name).connect(self.screen_changed)
        self.screen_changed()

    def screen_changed(self, *_):
        screen = self.screen_source
        if screen is not None:
            self.monitor_rect = win32.target_monitor(screen.name())["rect"]
        self.layout_entries()
        self.sync_visibility()
        self.taskbar_style.schedule_refresh()

    def tray_layout(self, result):
        self.watcher.shell_windows = {result["hwnd"], result["tray"]}
        bounds = tuple(result["bounds"])
        positioned = result["positioned"]
        if bounds != self.tray_bounds or positioned != self.tray_ready:
            self.tray_bounds = bounds
            self.tray_ready = positioned
            self.layout_entries()
            self.sync_visibility()
        if self.tray_frame.isVisible():
            self.tray_frame.raise_()

    def style_failed(self, message):
        if self.controller.dock is self:
            self.controller.set_taskbar_style(False)
            self.controller.report_error(message)

    def schedule_refresh(self, tasks):
        self.refresh_pending |= tasks
        if not self.refresh_timer.isActive():
            self.refresh_timer.start()

    def refresh_tasks(self):
        if self.refresh_pending:
            self.refresh_pending = False
            self.windows = dock_windows.task_windows()
            self.watcher.known = {window.hwnd for window in self.windows}
            self.rebuild_entries()
        self.sync_visibility()

    def sync_visibility(self):
        visible = self.screen_source is not None and self.tray_ready and not win32.fullscreen_on(self.monitor_rect)
        if visible != self.isVisible():
            if not visible and self.menu is not None:
                self.menu.close()
            self.setVisible(visible)
            self.tray_frame.setVisible(visible)

    @property
    def light(self):
        return self.settings.settings_theme == "light"

    def refresh_theme(self):
        self.update()
        self.tray_frame.update()

    def refresh_shortcuts(self):
        self.shortcuts = {}
        for target in self.pins:
            if Path(target).suffix.lower() == ".lnk":
                resolved = QFileInfo(target).symLinkTarget()
                if resolved:
                    self.shortcuts[target] = dock_target_key(resolved)

    def rebuild_entries(self):
        entries = group_entries(self.pins, self.windows, self.shortcuts)
        if entries != self.entries:
            self.entries = entries
            self.layout_entries()

    def layout_entries(self):
        if self.screen_source is None or self.tray_bounds is None:
            return
        screen = self.screen_source
        area = screen.geometry()
        ratio = screen.devicePixelRatio()
        tray_x, top, tray_right, bottom = self.tray_bounds
        monitor_x, monitor_y, _, _ = self.monitor_rect
        tray_width = round((tray_right - tray_x) / ratio)
        panel_bottom = area.y() + round((bottom - monitor_y) / ratio)
        self.tray_height = round((bottom - top) / ratio)
        inset, radius = tray_metrics(bottom - top)
        self.tray_inset = inset / ratio
        self.pill_radius = radius / ratio
        self.app_left = CONTROL_WIDTH + GROUP_GAP
        remaining = area.width() - tray_width - 2 * EDGE_RESERVE - self.app_left - GROUP_GAP - 2 * INSET
        capacity = max(1, remaining // SLOT - 1)
        split = capacity - 1 if len(self.entries) > capacity else capacity
        self.visible_entries, self.overflow = self.entries[:split], self.entries[split:]
        count = len(self.visible_entries) + bool(self.overflow) + 1
        self.app_width = 2 * INSET + SLOT * count
        panel_width = self.app_left + self.app_width
        span = panel_width + GROUP_GAP
        panel_left = area.x() + round((tray_x - monitor_x) / ratio) - span
        if self.tray_ready:
            self.setGeometry(panel_left, panel_bottom - HEIGHT, panel_width, HEIGHT)
            self.tray_frame.setGeometry(panel_left + span, panel_bottom - self.tray_height,
                                        tray_width, self.tray_height)
            self.tray_frame.update()
        self.taskbar_style.set_span(round(span * ratio))
        self.prepare_visuals()
        if len(self.scales) != count:
            self.animation.stop()
            self.hover = -1
            self.scales = [1.0] * count
        self.update()

    def prepare_visuals(self):
        dpr = self.screen_source.devicePixelRatio()
        updated = {}
        for entry in self.visible_entries:
            key = (entry.target, dpr)
            updated[key] = self.visuals.get(key) or self.load_visual(entry.target, dpr)
        self.visuals = updated
        self.icon_dpr = dpr

    def load_visual(self, target, dpr):
        if target.startswith(APPS_FOLDER):
            name, image = dock_windows.package_visual(target)
            fallback = target[len(APPS_FOLDER):].split("_", 1)[0].rsplit(".", 1)[-1]
            if not image.isNull():
                return name or fallback, QPixmap.fromImage(image)
            entry = next((entry for entry in self.entries if entry.target == target), None)
            path = entry.windows[0].executable if entry and entry.windows else ""
            name = name or fallback
        else:
            path = target
            name = tr("文件资源管理器") if dock_target_key(path) == dock_target_key(EXPLORER) else Path(path).stem
        if path and Path(path).suffix.lower() == ".lnk":
            return name, self.provider.icon(QFileInfo(path)).pixmap(QSize(64, 64), dpr)
        return name, QPixmap.fromImage(dock_windows.executable_icon(path, round(64 * dpr))) if path else QPixmap()

    def name(self, entry):
        return (self.visuals.get((entry.target, self.icon_dpr)) or self.load_visual(entry.target, self.icon_dpr))[0]

    def set_pins(self, pins):
        previous = self.settings.dock_pins
        self.settings.dock_pins = normalize_dock_pins(pins)
        try:
            self.controller.store.save()
        except OSError as exc:
            self.settings.dock_pins = previous
            self.controller.report_error(str(exc))
            return
        self.refresh_shortcuts()
        self.rebuild_entries()

    def add_files(self):
        files, _ = QFileDialog.getOpenFileNames(None, tr("固定应用到 Dock"), "",
                                               tr("Windows 应用或快捷方式 (*.exe *.lnk)"))
        if files and self.controller.dock is self:
            self.set_pins([*self.pins, *files])

    def dragEnterEvent(self, event):
        if self.drop_targets(event):
            self.accept_pin_drop(event)

    def dropEvent(self, event):
        targets = self.drop_targets(event)
        if targets and self.accept_pin_drop(event):
            self.set_pins([*self.pins, *targets])

    @staticmethod
    def accept_pin_drop(event):
        # Pinning only creates a reference. Never acknowledge a shell move.
        for action in (Qt.DropAction.LinkAction, Qt.DropAction.CopyAction):
            if event.possibleActions() & action:
                event.setDropAction(action)
                event.accept()
                return True
        return False

    @staticmethod
    def drop_targets(event):
        return normalize_dock_pins([url.toLocalFile() for url in event.mimeData().urls() if url.isLocalFile()])

    def slot_at(self, point):
        if 4 <= point.x() < CONTROL_WIDTH - 4 and self.pill_top <= point.y() < self.pill_bottom:
            return POWER_SLOT if point.x() < CONTROL_SIZE + 5 else SEARCH_SLOT
        left = self.app_left + INSET
        index = int(point.x() - left) // SLOT
        if point.x() >= left and 8 <= point.y() < self.pill_bottom and 0 <= index < len(self.scales):
            return index
        return -1

    @property
    def pill_top(self):
        return HEIGHT - self.tray_height + self.tray_inset

    @property
    def pill_bottom(self):
        return HEIGHT - self.tray_inset

    def entry_at(self, index):
        return self.visible_entries[index] if 0 <= index < len(self.visible_entries) else None

    def set_hover(self, index):
        if index == self.hover:
            return
        self.hover = index
        self.scale_start = self.scales[:]
        weights = (0.16, 0.07, 0.025)
        self.scale_end = [1 + weights[abs(slot - index)] if index >= 0 and abs(slot - index) < len(weights)
                          else 1.0 for slot in range(len(self.scales))]
        self.animation.stop()
        if self.scale_end == self.scale_start:
            self.update()
        else:
            self.animation.start()

    def animate_frame(self, fraction):
        self.scales = [start + (end - start) * fraction for start, end in zip(self.scale_start, self.scale_end)]
        self.update()

    def leaveEvent(self, _):
        self.set_hover(-1)

    def hideEvent(self, event):
        self.search.stop()
        self.set_hover(-1)
        self.animation.stop()
        self.scales = [1.0] * len(self.scales)
        self.press = None
        self.drag_slot = -1
        super().hideEvent(event)

    def mouseMoveEvent(self, event):
        slot = self.slot_at(event.position())
        self.set_hover(slot)
        if self.press is not None and self.press[0] is not None and self.press[0].pinned:
            if (event.position() - self.press[2]).manhattanLength() >= self.controller.app.startDragDistance():
                entry = self.entry_at(slot)
                self.drag_slot = slot if entry is not None and entry.pinned else -1
                self.update()

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            slot = self.slot_at(event.position())
            self.press = self.entry_at(slot), slot, event.position()
            if slot in (POWER_SLOT, SEARCH_SLOT):
                self.update()

    def mouseReleaseEvent(self, event):
        if event.button() != Qt.MouseButton.LeftButton or self.press is None:
            return
        entry, origin, point = self.press
        slot = self.slot_at(event.position())
        moved = (event.position() - point).manhattanLength() >= self.controller.app.startDragDistance()
        self.press, self.drag_slot = None, -1
        self.update()
        destination = self.entry_at(slot)
        if moved and entry is not None and entry.pinned and destination is not None and destination.pinned:
            if entry.target in self.pins and entry.target != destination.target:
                pins = list(self.pins)
                index = pins.index(destination.target)
                pins.remove(entry.target)
                pins.insert(index, entry.target)
                self.set_pins(pins)
        elif not moved and slot == origin and slot != -1:
            if entry is not None:
                self.open_entry(entry, event.globalPosition().toPoint())
            elif slot == POWER_SLOT:
                self.show_power_menu(event.globalPosition().toPoint())
            elif slot == SEARCH_SLOT:
                QToolTip.hideText()
                self.search.open(self.tray_bounds[1])
            elif slot == len(self.scales) - 1:
                self.add_files()
            else:
                self.show_menu(event.globalPosition().toPoint(), overflow=True)

    def event(self, event):
        if event.type() == QEvent.Type.ToolTip:
            slot = self.slot_at(event.pos())
            entry = self.entry_at(slot)
            if slot != -1:
                if slot == POWER_SLOT:
                    title = tr("电源")
                elif slot == SEARCH_SLOT:
                    title = tr("Windows 搜索")
                elif entry is not None:
                    title = self.name(entry)
                else:
                    title = tr("固定应用到 Dock") if slot == len(self.scales) - 1 else tr("更多应用")
                QToolTip.showText(event.globalPos(), title, self)
                return True
        return super().event(event)

    def contextMenuEvent(self, event):
        slot = self.slot_at(event.pos())
        self.show_menu(event.globalPos(), self.entry_at(slot), bool(self.overflow) and slot == len(self.visible_entries))

    def launch(self, target):
        try:
            open_target(target)
        except OSError as exc:
            self.controller.report_error(str(exc))

    def open_entry(self, entry, point):
        if len(entry.windows) > 1:
            self.show_menu(point, entry, windows_only=True)
        elif entry.windows:
            dock_windows.activate(entry.windows[0])
        else:
            self.launch(entry.target)

    def entry_actions(self, menu, entry, windows_only=False):
        for window in entry.windows:
            title = dock_windows.window_title(window.hwnd) or window.title
            menu.addAction(title[:100].replace("&", "&&"), lambda w=window: dock_windows.activate(w))
        if windows_only:
            return
        if entry.windows:
            menu.addSeparator()
        menu.addAction(tr("启动应用") if not entry.windows else tr("再次启动应用"), lambda: self.launch(entry.target))
        if entry.pinned:
            menu.addAction(tr("取消固定"), lambda: self.set_pins([target for target in self.pins if target != entry.target]))
        else:
            menu.addAction(tr("固定到 Dock"), lambda: self.set_pins([*self.pins, entry.target]))

    def show_menu(self, point, entry=None, overflow=False, windows_only=False):
        menu = self.new_menu()
        if overflow:
            for item in self.overflow:
                self.entry_actions(menu.addMenu(self.name(item).replace("&", "&&")), item)
        elif entry is not None:
            self.entry_actions(menu, entry, windows_only)
        if not windows_only:
            if entry is not None or overflow:
                menu.addSeparator()
            menu.addAction(tr("固定应用到 Dock"), self.add_files)
            menu.addAction(tr("任务栏美化设置"), self.controller.show_organizer_settings)
            menu.addAction(tr("关闭任务栏美化"), lambda: self.controller.set_taskbar_style(False))
        self.popup_menu(menu, point)

    def show_power_menu(self, point):
        menu = self.new_menu()
        menu.addAction(tr("锁定电脑"), self.lock_pc)
        menu.addSeparator()
        menu.addAction(tr("重启"), lambda: self.power_action("/r", tr("重启")))
        menu.addAction(tr("关机"), lambda: self.power_action("/s", tr("关机")))
        self.popup_menu(menu, point)

    def lock_pc(self):
        if not dock_windows.lock_workstation():
            self.controller.report_error(tr("无法锁定电脑。"))

    def power_action(self, flag, title):
        yes, no = QMessageBox.StandardButton.Yes, QMessageBox.StandardButton.No
        if QMessageBox.question(None, title, tr("确定要{0}吗？请先保存正在编辑的内容。").format(title), yes | no, no) != yes:
            return
        shutdown = str(Path(EXPLORER).parent / "System32" / "shutdown.exe")
        started, _ = QProcess.startDetached(shutdown, [flag, "/t", "0"])
        if not started:
            self.controller.report_error(tr("无法执行电源操作。"))

    def new_menu(self):
        if self.menu is not None:
            self.menu.close()
        self.set_hover(-1)
        QToolTip.hideText()
        menu = QMenu(self)
        self.menu = menu
        light = self.settings.settings_theme == "light"
        background, foreground, highlight = ("#f5f8fa", "#18252c", "#dbeee7") if light else ("#20282e", "#e9f4ef", "#39534a")
        menu.setStyleSheet(f"QMenu {{ background: {background}; color: {foreground}; border: 1px solid {highlight}; padding: 5px; }}"
                           f"QMenu::item {{ padding: 6px 18px; }} QMenu::item:selected {{ background: {highlight}; }}")
        def dismissed():
            if self.menu is menu:
                self.menu = None
            menu.deleteLater()

        menu.aboutToHide.connect(dismissed)
        return menu

    def popup_menu(self, menu, point):
        menu.ensurePolished()
        top = self.geometry().top() + 17
        menu.popup(QPoint(point.x(), top - menu.sizeHint().height()))

    def paintEvent(self, _):
        with QPainter(self) as painter:
            self.paint_panel(painter)

    def paint_panel(self, painter):
        painter.setRenderHints(QPainter.RenderHint.Antialiasing | QPainter.RenderHint.SmoothPixmapTransform)
        y = self.pill_top
        height = self.pill_bottom - y
        paint_pill(painter, QRectF(0, y, CONTROL_WIDTH, height), self.pill_radius, self.light)
        paint_pill(painter, QRectF(self.app_left, y, self.app_width, height), self.pill_radius, self.light)
        ink = QColor("#24483c" if self.light else "#d5ede3")
        self.paint_controls(painter, ink)
        icon_bottom = self.pill_bottom - 8
        max_icon = max(1, icon_bottom - y - 2)
        for index, scale in enumerate(self.scales):
            center = self.app_left + INSET + SLOT * (index + 0.5)
            entry = self.entry_at(index)
            if index == self.drag_slot:
                painter.setBrush(QColor(86, 179, 143, 82))
                painter.setPen(Qt.PenStyle.NoPen)
                painter.drawRoundedRect(QRectF(center - 23, y + 3, 46, height - 6), 11, 11)
            size = min(BASE_ICON, max_icon / 1.16) * scale
            box = QRectF(center - size / 2, icon_bottom - size, size, size)
            if entry is not None:
                pixmap = self.visuals[entry.target, self.icon_dpr][1]
                if pixmap.isNull():
                    painter.setBrush(QColor("#4d7f70"))
                    painter.setPen(Qt.PenStyle.NoPen)
                    painter.drawRoundedRect(box.adjusted(3, 3, -3, -3), 10, 10)
                    painter.setPen(QPen(QColor("#d5ede3"), 1.5))
                    painter.drawRoundedRect(box.adjusted(size * 0.29, size * 0.32, -size * 0.29, -size * 0.32), 2, 2)
                else:
                    painter.drawPixmap(box, pixmap, QRectF(pixmap.rect()))
                if entry.windows:
                    painter.setPen(Qt.PenStyle.NoPen)
                    painter.setBrush(ink)
                    painter.drawEllipse(QRectF(center - 1.6, icon_bottom + 2, 3.2, 3.2))
            else:
                painter.setPen(QPen(ink, 1.7, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
                y = box.center().y()
                if index == len(self.scales) - 1:
                    painter.drawLine(QPoint(round(center - 5), round(y)), QPoint(round(center + 5), round(y)))
                    painter.drawLine(QPoint(round(center), round(y - 5)), QPoint(round(center), round(y + 5)))
                else:
                    painter.setBrush(ink)
                    for offset in (-6, 0, 6):
                        painter.drawEllipse(QRectF(center + offset - 1, y - 1, 2, 2))

    def paint_controls(self, painter, ink):
        y = (self.pill_top + self.pill_bottom) / 2
        button_height = min(32, self.pill_bottom - self.pill_top - 8)
        power = QRectF(6, y - button_height / 2, 33, button_height)
        search = QRectF(45, y - button_height / 2, CONTROL_WIDTH - 52, button_height)
        pressed = self.press[1] if self.press is not None else -1
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(86, 179, 143, 138 if pressed == POWER_SLOT else 90 if self.hover == POWER_SLOT else 45))
        painter.drawRoundedRect(power, 11, 11)
        painter.setBrush(QColor(127, 165, 161, 120 if pressed == SEARCH_SLOT else 75 if self.hover == SEARCH_SLOT else 29))
        painter.drawRoundedRect(search, 11, 11)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.setPen(QPen(ink, 1.8, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        x = power.center().x()
        painter.drawArc(QRectF(x - 7, y - 6, 14, 14), 125 * 16, 290 * 16)
        painter.drawLine(QPointF(x, y - 9), QPointF(x, y - 1))
        x = search.x() + 15
        painter.drawEllipse(QRectF(x - 7, y - 7, 11, 11))
        painter.drawLine(QPointF(x + 2.5, y + 2.5), QPointF(x + 7.5, y + 7.5))
        painter.drawText(search.adjusted(27, 0, -4, 0), Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft, tr("搜索"))

    def stop(self):
        self.search.stop()
        self.watcher.stop()
        self.refresh_timer.stop()
        self.taskbar_style.stop()
        self.controller.app.primaryScreenChanged.disconnect(self.bind_screen)
        self.bind_screen(None)
        if self.menu is not None:
            self.menu.close()
        self.hide()
        self.deleteLater()
