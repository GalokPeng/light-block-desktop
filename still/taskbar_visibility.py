"""Visibility and work-area ownership for the single native Windows taskbar."""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W

from . import win32
from .taskbar_style_native import (BottomWorkArea, HIDDEN_SHAPE, TOPMOST, ZORDER_ONLY,
                                   process_id, put_region, region_matches, window_bounds, window_region)


def bottom_strip(bar, bounds, monitor):
    """The XAML columns center the group. Explorer tracks its left edge on Start."""
    left, top, right, bottom = bounds
    if not monitor or (left, right, bottom) != (monitor[0], monitor[2], monitor[3]) or top >= bottom:
        return None
    start = win32.FindWindowEx(bar, None, "Start", None) or win32.FindWindowEx(bar, None, "ReBarWindow32", None)
    if not start:
        return None
    first = window_bounds(start)[0]
    if not left < first < (left + right) // 2:
        return None
    return first, top, left + right - first, bottom


class TaskbarVisibilityLease:
    def __init__(self):
        self.bar = self.pid = None
        self.rectangular = None
        self.was_topmost = None
        self.work_area = BottomWorkArea()

    def update(self, bar, monitor, hidden, revealed, native_auto_hide):
        pid = process_id(bar)
        if (bar, pid) != (self.bar, self.pid):
            self.restore()
            self.bar, self.pid = bar, pid
        bounds = window_bounds(bar)
        strip = bottom_strip(bar, bounds, monitor)
        if native_auto_hide or strip is None:
            self.restore_region()
            self.restore_z_order()
            if native_auto_hide:
                self.work_area = BottomWorkArea()  # Windows now owns the reservation.
            else:
                self.work_area.restore()
            return None
        self.work_area.expand_bounds(bounds)
        if hidden:
            self.hide(bounds)
            self.restore_z_order()
        else:
            self.restore_region()
            if revealed and self.was_topmost is None:
                self.was_topmost = bool(win32.GetWindowLong(bar, -20) & TOPMOST)
                win32.SetWindowPos(bar, -1, 0, 0, 0, 0, ZORDER_ONLY)
            elif not revealed:
                self.restore_z_order()
        return strip

    def hide(self, bounds):
        if self.rectangular is not None and region_matches(self.bar, HIDDEN_SHAPE):
            return
        left, top, right, bottom = bounds
        kind, box = window_region(self.bar)
        if kind and (kind, box) != (2, (0, 0, right - left, bottom - top)):
            return
        put_region(self.bar, *HIDDEN_SHAPE)
        self.rectangular = kind == 2

    def restore_region(self):
        if self.rectangular is not None and self.same_bar() and region_matches(self.bar, HIDDEN_SHAPE):
            left, top, right, bottom = window_bounds(self.bar)
            box = (0, 0, right - left, bottom - top) if self.rectangular else None
            put_region(self.bar, box)
        self.rectangular = None

    def same_bar(self):
        return (self.bar is not None and win32.FindWindow("Shell_TrayWnd", None) == self.bar
                and process_id(self.bar) == self.pid)

    def restore_z_order(self):
        if self.was_topmost is False and self.same_bar() and win32.GetWindowLong(self.bar, -20) & TOPMOST:
            win32.SetWindowPos(self.bar, -2, 0, 0, 0, 0, ZORDER_ONLY)
        self.was_topmost = None

    def restore(self):
        try:
            self.restore_region()
            self.restore_z_order()
        finally:
            self.work_area.restore()
            self.bar = self.pid = None


get_cursor = win32.bind(win32.user32, "GetCursorPos", W.BOOL, C.POINTER(W.POINT))
