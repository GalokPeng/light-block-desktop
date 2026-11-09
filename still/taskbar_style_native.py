"""Own a temporary clip on Explorer's primary taskbar, preserving its live tray."""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
from dataclasses import dataclass
import json
import sys

from . import win32

gdi32 = C.WinDLL("gdi32", use_last_error=True)
shell32 = C.WinDLL("shell32", use_last_error=True)
rect_region = win32.bind(gdi32, "CreateRectRgn", W.HRGN, C.c_int, C.c_int, C.c_int, C.c_int)
round_region = win32.bind(gdi32, "CreateRoundRectRgn", W.HRGN, *([C.c_int] * 6))
equal_region = win32.bind(gdi32, "EqualRgn", W.BOOL, W.HRGN, W.HRGN)
delete_region = win32.bind(gdi32, "DeleteObject", W.BOOL, W.HGDIOBJ)
region_box = win32.bind(gdi32, "GetRgnBox", C.c_int, W.HRGN, C.POINTER(W.RECT))
get_region = win32.bind(win32.user32, "GetWindowRgn", C.c_int, W.HWND, W.HRGN)
set_region = win32.bind(win32.user32, "SetWindowRgn", C.c_int, W.HWND, W.HRGN, W.BOOL)
set_dpi = win32.bind(win32.user32, "SetThreadDpiAwarenessContext", W.HANDLE, W.HANDLE)
is_visible = win32.bind(win32.user32, "IsWindowVisible", W.BOOL, W.HWND)
create_mutex = win32.bind(win32.kernel32, "CreateMutexW", W.HANDLE, C.c_void_p, W.BOOL, W.LPCWSTR)
close_handle = win32.bind(win32.kernel32, "CloseHandle", W.BOOL, W.HANDLE)


class AppBarData(C.Structure):
    _fields_ = [("size", W.DWORD), ("hwnd", W.HWND), ("callback", W.UINT), ("edge", W.UINT),
                ("rect", W.RECT), ("param", W.LPARAM)]


appbar_message = win32.bind(shell32, "SHAppBarMessage", C.c_size_t, W.DWORD, C.POINTER(AppBarData))


@dataclass(frozen=True)
class Taskbar:
    hwnd: int
    tray: int
    pid: int
    bounds: tuple[int, int, int, int]
    tray_bounds: tuple[int, int, int, int]
    home_x: int = 0

    @property
    def clip(self):
        left, top, _, bottom = self.bounds
        return self.tray_bounds[0] - left, 0, self.tray_bounds[2] - left, bottom - top

    @property
    def tray_shape(self):
        left, _, right, height = self.clip
        inset, radius = tray_metrics(height)
        return (left, inset, right, height - inset), radius


def tray_metrics(height):
    inset = round(height / 16)
    radius = min(round(height * 0.38), (height - 2 * inset) // 2)
    return inset, radius


def coords(rect):
    return rect.left, rect.top, rect.right, rect.bottom


def window_bounds(hwnd):
    rect = W.RECT()
    if not win32.GetWindowRect(hwnd, C.byref(rect)):
        raise OSError("unsupported")
    return coords(rect)


def process_id(hwnd):
    process = W.DWORD()
    win32.GetWindowThreadProcessId(hwnd, C.byref(process))
    return process.value


def taskbar(leased=None, shifted_x=None):
    hwnd = win32.FindWindow("Shell_TrayWnd", None)
    if not hwnd or not is_visible(hwnd):
        raise OSError("pending")
    appbar = AppBarData(C.sizeof(AppBarData))
    if appbar_message(4, C.byref(appbar)) & 1:  # ABM_GETSTATE / ABS_AUTOHIDE
        raise OSError("unsupported")
    tray = win32.FindWindowEx(hwnd, None, "TrayNotifyWnd", None)
    if not tray or not is_visible(tray):
        raise OSError("pending")
    left, top, right, bottom = window_bounds(hwnd)
    tray_left, tray_top, tray_right, tray_bottom = window_bounds(tray)
    x, y, width, height = win32.target_monitor()["rect"]
    pid = process_id(hwnd)
    owned_position = leased is not None and (hwnd, pid, left) == (leased.hwnd, leased.pid, shifted_x)
    if ((left != x and not owned_position) or right - left != width or bottom != y + height
            or not 0 < bottom - top < height // 4
            or not 0 < tray_right - tray_left <= width // 2 or tray_left < left or tray_right != right
            or tray_top != top or tray_bottom != bottom or win32.GetWindowLong(hwnd, -20) & 0x400000):
        raise OSError("unsupported")
    return Taskbar(hwnd, tray, pid, (left, top, right, bottom), (tray_left, top, right, bottom), x)


def window_region(hwnd):
    region = rect_region(0, 0, 0, 0)
    if not region:
        raise OSError("failed")
    try:
        kind = get_region(hwnd, region)
        if not kind:
            return 0, None
        box = W.RECT()
        region_box(region, C.byref(box))
        return kind, coords(box)
    finally:
        delete_region(region)


def make_region(box, radius):
    if radius:
        return round_region(*box, 2 * radius, 2 * radius)
    return rect_region(*box)


def region_matches(hwnd, shape):
    existing = rect_region(0, 0, 0, 0)
    expected = make_region(*shape)
    try:
        if not existing or not expected:
            raise OSError("failed")
        return bool(get_region(hwnd, existing) and equal_region(existing, expected))
    finally:
        if existing:
            delete_region(existing)
        if expected:
            delete_region(expected)


def put_region(hwnd, box, radius=0):
    region = make_region(box, radius) if box else None
    if box and not region:
        raise OSError("failed")
    if not set_region(hwnd, region, True):
        if region:
            delete_region(region)
        raise OSError("failed")


def move_taskbar(hwnd, x, y):
    # Keep the shell's z order and focus. Explorer clamps the position in
    # WM_WINDOWPOSCHANGING, so skip that notification for this horizontal move.
    if not win32.SetWindowPos(hwnd, None, x, y, 0, 0, 0x0001 | 0x0004 | 0x0010 | 0x0400):
        raise OSError("failed")


class TaskbarLease:
    def __init__(self):
        self.applied = None
        self.original_rectangular = False
        self.shifted_x = None

    def update(self, span):
        previous = self.applied
        current = taskbar(previous, self.shifted_x)
        if span is None:
            return current
        if previous is not None and (previous.hwnd, previous.pid) != (current.hwnd, current.pid):
            self.restore()
            previous = None
        left, top, right, bottom = current.bounds
        tray_width = current.clip[2] - current.clip[0]
        free = right - left - span - tray_width
        if free < 0:
            raise OSError("unsupported")
        target_x = current.home_x + free // 2 + span - current.clip[0]
        owns_region = previous is not None and region_matches(current.hwnd, previous.tray_shape)
        if not owns_region:
            kind, box = window_region(current.hwnd)
            # Respect an existing taskbar mod. Native Explorer has either no
            # region or a full rectangle, which can adapt to a resolution change.
            if kind and (kind, box) != (2, (0, 0, right - left, bottom - top)):
                raise OSError("unsupported")
            self.original_rectangular = bool(kind)
        if not owns_region or previous.tray_shape != current.tray_shape:
            put_region(current.hwnd, *current.tray_shape)
        self.applied = current
        if left != target_x:
            move_taskbar(current.hwnd, target_x, top)
        self.shifted_x = target_x
        positioned = taskbar(current, target_x)
        if positioned.bounds[0] != target_x:
            raise OSError("pending")
        return positioned

    def restore(self):
        current = self.applied
        if current is None:
            return
        if win32.FindWindow("Shell_TrayWnd", None) == current.hwnd and process_id(current.hwnd) == current.pid:
            left, top, right, bottom = window_bounds(current.hwnd)
            if left == self.shifted_x and left != current.home_x:
                move_taskbar(current.hwnd, current.home_x, top)
            if region_matches(current.hwnd, current.tray_shape):
                box = (0, 0, right - left, bottom - top) if self.original_rectangular else None
                put_region(current.hwnd, box)
        self.applied = None
        self.shifted_x = None


def pipe_stream(stream, number, mode):
    if stream is not None:
        return stream.buffer
    # A frozen, windowed executable can have no Python stdio objects even
    # when QProcess supplied inheritable standard handles.
    import msvcrt
    import os
    handle = win32.bind(win32.kernel32, "GetStdHandle", W.HANDLE, W.DWORD)(number & 0xFFFFFFFF)
    if not handle or handle == C.c_void_p(-1).value:
        raise OSError("failed")
    access = os.O_RDONLY if mode == "rb" else os.O_WRONLY
    return os.fdopen(msvcrt.open_osfhandle(handle, access | os.O_BINARY), mode)


def run_helper():
    reader = pipe_stream(sys.stdin, -10, "rb")
    writer = pipe_stream(sys.stdout, -11, "wb")

    def reply(message):
        writer.write(json.dumps(message).encode("ascii") + b"\n")
        writer.flush()

    if not set_dpi(-4):  # GDI region coordinates must be physical at mixed DPI.
        reply({"error": "failed"})
        return 1
    mutex = create_mutex(None, False, "Local\\Still.TaskbarStyle")
    existed = C.get_last_error() == 183
    if not mutex or existed:
        if mutex:
            close_handle(mutex)
        reply({"error": "busy" if existed else "failed"})
        return 1
    lease = TaskbarLease()
    try:
        for command in reader:
            span = json.loads(command)["span"]
            if span is not None and (type(span) is not int or span <= 0):
                raise OSError("failed")
            try:
                bar = lease.update(span)
            except OSError as exc:
                if str(exc) != "pending":
                    raise
                reply({"waiting": True})
                continue
            reply({"hwnd": bar.hwnd, "tray": bar.tray, "bounds": bar.tray_bounds, "positioned": span is not None})
    except (OSError, ValueError, TypeError, KeyError) as exc:
        lease.restore()
        if not isinstance(exc, BrokenPipeError):
            reply({"error": str(exc) if str(exc) in {"unsupported", "failed"} else "failed"})
        return 1
    finally:
        try:
            lease.restore()
        finally:
            close_handle(mutex)
    return 0
