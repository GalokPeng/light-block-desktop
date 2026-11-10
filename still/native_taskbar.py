"""Round Explorer's real taskbar windows without creating a replacement bar."""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
from dataclasses import dataclass
import json
import sys

from . import win32
from .model import TASKBAR_RADIUS_LIMITS
from .taskbar_style_native import (AppBarData, appbar_message, close_handle, create_mutex, pipe_stream,
                                   process_id, put_region, region_matches, set_dpi, window_bounds, window_region)


get_dpi = win32.bind(win32.user32, "GetDpiForWindow", W.UINT, W.HWND)
is_window = win32.bind(win32.user32, "IsWindow", W.BOOL, W.HWND)


def explorer_taskbars():
    primary = win32.FindWindow("Shell_TrayWnd", None)
    if not primary:
        return {}
    pid = process_id(primary)
    if not pid:
        return {}
    windows = {primary: pid}
    secondary = win32.FindWindowEx(None, None, "Shell_SecondaryTrayWnd", None)
    while secondary:
        if process_id(secondary) == pid:
            windows[secondary] = pid
        secondary = win32.FindWindowEx(None, secondary, "Shell_SecondaryTrayWnd", None)
    return windows


def auto_hidden():
    data = AppBarData(C.sizeof(AppBarData))
    return bool(appbar_message(4, C.byref(data)) & 1)  # ABM_GETSTATE / ABS_AUTOHIDE


def monitor_bounds(hwnd):
    monitor = win32.MonitorFromWindow(hwnd, 2)  # MONITOR_DEFAULTTONEAREST
    info = win32.MONITORINFOEX(C.sizeof(win32.MONITORINFOEX))
    if monitor and win32.GetMonitorInfo(monitor, C.byref(info)):
        area = info.rcMonitor
        return area.left, area.top, area.right, area.bottom
    return None


@dataclass(frozen=True)
class ShapeLease:
    pid: int
    rectangular: bool
    shape: tuple[tuple[int, int, int, int], int]


class NativeTaskbarRegions:
    def __init__(self):
        self.leases = {}

    def update(self, radius):
        windows = explorer_taskbars()
        for hwnd in self.leases.keys() - windows.keys():
            self.restore_window(hwnd)
        if not windows:
            return {"waiting": True}
        if auto_hidden():
            # Explorer needs its original hit area to reveal an auto-hidden bar.
            self.restore()
            return {"auto_hide": True}
        primary = next(iter(windows))
        for hwnd, pid in windows.items():
            if not self.apply_window(hwnd, pid, radius) and hwnd == primary:
                return {"error": "occupied"}
        return {"auto_hide": False}

    def apply_window(self, hwnd, pid, radius):
        left, top, right, bottom = window_bounds(hwnd)
        width, height = right - left, bottom - top
        full = (0, 0, width, height)
        scale = (get_dpi(hwnd) or 96) / 96
        thickness = min(width, height)
        # Show a narrow gap on the content side of the bar. Leave the screen
        # edge intact so native taskbar buttons are still reachable there.
        inset = min(round(6 * scale), thickness // 8)
        pixels = min(round(radius * scale), (thickness - inset) // 2)
        if pixels < 2:
            raise OSError("pending")
        display = monitor_bounds(hwnd)
        x, y, w, h = full
        if width >= height:
            if display and abs(top - display[1]) < abs(bottom - display[3]):
                h -= inset
            else:
                y += inset
        elif display and abs(right - display[2]) < abs(left - display[0]):
            x += inset
        else:
            w -= inset
        # CreateRoundRectRgn leaves the last row and column outside the region.
        shape = ((x, y, w + 1, h + 1), pixels)
        previous = self.leases.get(hwnd)
        if previous and previous.pid == pid and region_matches(hwnd, previous.shape):
            if previous.shape == shape:
                return True
            rectangular = previous.rectangular
        else:
            kind, box = window_region(hwnd)
            if kind and (kind, box) != (2, full):
                return False
            rectangular = kind == 2
        put_region(hwnd, *shape)
        self.leases[hwnd] = ShapeLease(pid, rectangular, shape)
        return True

    def restore_window(self, hwnd):
        lease = self.leases[hwnd]
        if is_window(hwnd) and process_id(hwnd) == lease.pid and region_matches(hwnd, lease.shape):
            # An explicit native rectangle is relative to the current HWND size.
            box = None
            if lease.rectangular:
                left, top, right, bottom = window_bounds(hwnd)
                box = (0, 0, right - left, bottom - top)
            put_region(hwnd, box)
        del self.leases[hwnd]

    def restore(self):
        failure = None
        for hwnd in list(self.leases):
            try:
                self.restore_window(hwnd)
            except OSError as exc:
                if is_window(hwnd):
                    failure = exc
        if failure is not None:
            raise failure


def run_helper():
    from .native_taskbar_xaml import layout_library, NativeTaskbarLayout

    reader = pipe_stream(sys.stdin, -10, "rb")
    writer = pipe_stream(sys.stdout, -11, "wb")

    def reply(result):
        writer.write(json.dumps(result).encode("ascii") + b"\n")
        writer.flush()

    if not set_dpi(-4):
        reply({"error": "failed"})
        return 1
    mutex = create_mutex(None, False, "Local\\Still.TaskbarStyle")
    occupied = C.get_last_error() == 183
    if not mutex or occupied:
        if mutex:
            close_handle(mutex)
        reply({"error": "busy" if occupied else "failed"})
        return 1

    regions = NativeTaskbarRegions()
    library = layout_library()
    layout = None
    try:
        if library is not None:
            layout = NativeTaskbarLayout(library)
        for line in reader:
            request = json.loads(line)
            radius = request["radius"]
            if type(radius) is not int or not TASKBAR_RADIUS_LIMITS[0] <= radius <= TASKBAR_RADIUS_LIMITS[1]:
                raise ValueError("invalid radius")
            try:
                if layout:
                    result = layout.update(request.get("hidden") is True, request.get("revealed") is True)
                else:
                    result = regions.update(radius)
            except OSError:
                result = {"waiting": True}
            reply(result)
    except BrokenPipeError:
        pass  # The app exited. Release any taskbar windows we modified.
    except (OSError, ValueError, TypeError, KeyError):
        reply({"error": "failed"})
        return 1
    finally:
        try:
            if layout is not None:
                layout.restore()
            regions.restore()
        finally:
            close_handle(mutex)
    return 0
