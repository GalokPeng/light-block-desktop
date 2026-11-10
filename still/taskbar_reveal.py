"""Reuse the Dock's window coverage rules while Explorer owns the UI."""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W

from PySide6.QtCore import QElapsedTimer, QObject, QTimer, Signal

from . import dock_windows, win32
from .taskbar_style_native import HIDDEN_SHAPE, process_id, region_matches, root_window, window_at, window_bounds
from .taskbar_visibility import bottom_strip, get_cursor


class NativeTaskbarReveal(QObject):
    changed = Signal()

    def __init__(self, parent):
        super().__init__(parent)
        self.bar = self.monitor = self.strip = None
        self.hidden = self.revealed = self.covered = False
        self.grace = QElapsedTimer()
        self.check_timer = QTimer(self, singleShot=True, interval=30)
        self.check_timer.timeout.connect(self.check)
        self.edge_timer = QTimer(self, interval=60)
        self.edge_timer.timeout.connect(self.update_pointer)
        self.desktop_timer = QTimer(self, singleShot=True, interval=115)
        self.desktop_timer.timeout.connect(self.check)
        self.watcher = None

    def set_target(self, target):
        if not target:
            self.stop()
            return
        bar, monitor, strip = target
        if self.bar != bar or self.monitor != tuple(monitor):
            self.stop()
            self.bar, self.monitor, self.strip = bar, tuple(monitor), tuple(strip)
            self.watcher = dock_windows.TaskWatcher(self)
            self.watcher.shell_windows = {bar}
            self.watcher.changed.connect(self.schedule_check)
            self.watcher.shell_changed.connect(self.shell_changed)
            self.watcher.foreground_changed.connect(self.foreground_changed)
            self.check()
        else:
            self.strip = tuple(strip)

    def schedule_check(self, *_):
        if self.bar is not None and not self.check_timer.isActive():
            self.check_timer.start()

    def shell_changed(self):
        self.schedule_check()
        if self.hidden and self.bar is not None:
            try:
                if not region_matches(self.bar, HIDDEN_SHAPE):
                    self.changed.emit()
            except OSError:
                pass

    def foreground_changed(self, desktop):
        if desktop:
            self.desktop_timer.start()
            self.covered = False
            self.edge_timer.stop()
            self.set_state(False, False)
        else:
            self.desktop_timer.stop()
            self.schedule_check()

    def check(self):
        if self.bar is None or self.desktop_timer.isActive():
            return
        try:
            strip = bottom_strip(self.bar, window_bounds(self.bar), self.monitor)
        except OSError:
            strip = None
        if strip:
            self.strip = strip
        if not self.strip:
            return
        self.covered = bool(dock_windows.covering_window(self.strip))
        if self.covered:
            if not self.edge_timer.isActive():
                self.edge_timer.start()
        else:
            self.edge_timer.stop()
        self.update_pointer()

    def pointer_over_popup(self, cursor):
        hwnd = window_at(cursor)
        root = root_window(hwnd, 2) if hwnd else None
        return (root and root != self.bar and process_id(root) == process_id(self.bar)
                and dock_windows.window_class(root) in {"TopLevelWindowForOverflowXamlIsland", "Xaml_WindowedPopupClass",
                                                         "Windows.UI.Core.CoreWindow", "#32768"})

    def update_pointer(self):
        if not self.covered:
            self.grace.invalidate()
            self.set_state(False, False)
            return
        cursor = W.POINT()
        if not get_cursor(C.byref(cursor)):
            return
        left, top, right, bottom = self.strip
        edge = self.monitor[0] <= cursor.x < self.monitor[2] and bottom - 2 <= cursor.y < bottom
        on_bar = left - 4 <= cursor.x < right + 4 and top <= cursor.y < bottom
        if edge or (self.revealed and (on_bar or self.pointer_over_popup(cursor))):
            self.grace.start()
            self.set_state(False, True)
        elif not self.grace.isValid() or self.grace.elapsed() >= 230:
            self.set_state(True, False)

    def set_state(self, hidden, revealed):
        if (hidden, revealed) != (self.hidden, self.revealed):
            self.hidden, self.revealed = hidden, revealed
            self.changed.emit()

    def stop(self):
        self.check_timer.stop()
        self.edge_timer.stop()
        self.desktop_timer.stop()
        if self.watcher is not None:
            self.watcher.stop()
            self.watcher.deleteLater()
            self.watcher = None
        self.bar = self.monitor = self.strip = None
        self.hidden = self.revealed = self.covered = False
        self.grace.invalidate()
