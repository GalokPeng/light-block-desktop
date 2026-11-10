"""Own the primary taskbar clip and its bottom work-area reservation."""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
from dataclasses import dataclass
import json
import sys

from . import dock_glass, win32

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
get_client = win32.bind(win32.user32, "GetClientRect", W.BOOL, W.HWND, C.POINTER(W.RECT))
send_timeout = win32.bind(win32.user32, "SendMessageTimeoutW", C.c_size_t, W.HWND, W.UINT, W.WPARAM,
                           W.LPARAM, W.UINT, W.UINT, C.POINTER(C.c_size_t))
create_mutex = win32.bind(win32.kernel32, "CreateMutexW", W.HANDLE, C.c_void_p, W.BOOL, W.LPCWSTR)
close_handle = win32.bind(win32.kernel32, "CloseHandle", W.BOOL, W.HANDLE)
monitor_from_rect = win32.bind(win32.user32, "MonitorFromRect", W.HANDLE, C.POINTER(W.RECT), W.DWORD)
window_at = win32.bind(win32.user32, "WindowFromPoint", W.HWND, W.POINT)
root_window = win32.bind(win32.user32, "GetAncestor", W.HWND, W.HWND, W.UINT)
ZORDER_ONLY = 0x0001 | 0x0002 | 0x0010 | 0x0200 | 0x0400
TOPMOST = 0x0008
HIDDEN_SHAPE = ((0, 0, 0, 0), 0)


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


def tray_fits(bounds, tray_bounds):
    left, top, right, bottom = bounds
    tray_left, tray_top, tray_right, tray_bottom = tray_bounds
    return (left <= tray_left < tray_right == right and tray_right - tray_left <= (right - left) // 2
            and tray_top == top and tray_bottom == bottom)


def relayout_taskbar(hwnd):
    rect = W.RECT()
    if get_client(hwnd, C.byref(rect)):
        result = C.c_size_t()
        # Explorer's client size stays the same when the HWND slides. Ask it
        # to realign the notification child if its coordinates became stale.
        send_timeout(hwnd, 0x0005, 0, (rect.bottom << 16) | rect.right, 0x0002, 120, C.byref(result))


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
    bounds = window_bounds(hwnd)
    tray_bounds = window_bounds(tray)
    x, y, width, height = win32.target_monitor()["rect"]
    pid = process_id(hwnd)
    same_shell = leased is not None and (hwnd, pid) == (leased.hwnd, leased.pid)
    if same_shell and not tray_fits(bounds, tray_bounds):
        relayout_taskbar(hwnd)
        bounds = window_bounds(hwnd)
        tray_bounds = window_bounds(tray)
    left, top, right, bottom = bounds
    owned_position = leased is not None and (hwnd, pid, left) == (leased.hwnd, leased.pid, shifted_x)
    if ((left != x and not owned_position) or right - left != width or bottom != y + height
            or not 0 < bottom - top < height // 4 or win32.GetWindowLong(hwnd, -20) & 0x400000):
        raise OSError("unsupported")
    if not tray_fits(bounds, tray_bounds):
        raise OSError("pending" if same_shell else "unsupported")
    return Taskbar(hwnd, tray, pid, bounds, tray_bounds, x)


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


def put_region(hwnd, box, radius=0, *, redraw=True):
    region = make_region(box, radius) if box else None
    if box and not region:
        raise OSError("failed")
    if not set_region(hwnd, region, redraw):
        if region:
            delete_region(region)
        raise OSError("failed")


def suppress_taskbar(hwnd):
    if not win32.SetWindowPos(hwnd, None, 0, 0, 0, 0, 0x0080 | 0x0417):
        raise OSError("failed")


def move_taskbar(hwnd, x, y, *, show=False):
    # Keep the shell's z order and focus. Explorer clamps the position in
    # WM_WINDOWPOSCHANGING, so skip that notification for this horizontal move.
    flags = 0x0415 | (0x0040 if show else 0)  # SWP_SHOWWINDOW
    if not win32.SetWindowPos(hwnd, None, x, y, 0, 0, flags):
        if show:
            win32.SetWindowPos(hwnd, None, 0, 0, 0, 0, 0x0457)
        raise OSError("failed")


def primary_work_area():
    rect = W.RECT()
    if not win32.SystemParametersInfo(0x0030, 0, C.byref(rect), 0):  # SPI_GETWORKAREA
        raise OSError("failed")
    return coords(rect)


def set_primary_work_area(area):
    rect = W.RECT(*area)
    # Keep the change in this session. Broadcasting WM_SETTINGCHANGE also
    # makes Explorer immediately reclaim its full-width taskbar reservation.
    if not win32.SystemParametersInfo(0x002F, 0, C.byref(rect), 0):  # SPI_SETWORKAREA
        raise OSError("failed")


def resize_maximized_windows(before, after):
    """Extend already maximized windows without changing their restore bounds."""
    rect = W.RECT(*before)
    monitor = monitor_from_rect(C.byref(rect), 0)
    if not monitor:
        return
    previous_bottom, next_bottom = before[3], after[3]

    @win32.ENUMPROC
    def visit(hwnd, _):
        if not is_visible(hwnd) or not win32.IsZoomed(hwnd) or win32.MonitorFromWindow(hwnd, 0) != monitor:
            return True
        frame = W.RECT()
        if win32.DwmGetWindowAttribute(hwnd, 9, C.byref(frame), C.sizeof(frame)) != 0:
            return True
        if abs(frame.bottom - previous_bottom) <= 2:
            bounds = W.RECT()
            if win32.GetWindowRect(hwnd, C.byref(bounds)):
                height = bounds.bottom - bounds.top + next_bottom - frame.bottom
                # NOMOVE, NOZORDER, NOACTIVATE, NOSENDCHANGING, ASYNCWINDOWPOS.
                win32.SetWindowPos(hwnd, None, 0, 0, bounds.right - bounds.left, height, 0x4416)
        return True

    win32.EnumWindows(visit, 0)


class BottomWorkArea:
    def __init__(self):
        self.original = None
        self.expanded = None
        self.screen = None

    def expand(self, bar):
        before = primary_work_area()
        if before[3] != bar.bounds[1]:
            return  # Already expanded, or another appbar also owns this edge.
        after = (*before[:3], bar.bounds[3])
        self.screen = win32.target_monitor()["name"]
        self.original, self.expanded = before, after
        set_primary_work_area(after)
        if primary_work_area() == after:
            resize_maximized_windows(before, after)

    def restore(self):
        if self.original is None:
            return
        if win32.target_monitor()["name"] == self.screen:
            current = primary_work_area()
            if current == self.expanded:
                set_primary_work_area(self.original)
                current = primary_work_area()
            if current == self.original:
                resize_maximized_windows(self.expanded, self.original)
        self.original = self.expanded = self.screen = None


class TaskbarLease:
    def __init__(self):
        self.applied = None
        self.original_rectangular = False
        self.shifted_x = None
        self.work_area = BottomWorkArea()
        self.original_material = self.applied_material = None
        self.read_material = self.write_material = None
        self.original_topmost = None
        self.shape = None

    def style_tray(self, bar, frame, accent):
        self.style_material(bar.hwnd, accent)
        if not frame or not is_visible(frame):
            return
        left, top, right, bottom = bar.tray_bounds
        hit = window_at(W.POINT((left + right) // 2, (top + bottom) // 2))
        root = root_window(hit, 2) if hit else None
        if root in (bar.hwnd, frame) or (root and process_id(root) == bar.pid):
            return  # Leave Explorer's own flyouts above the tray.
        if self.original_topmost is None:
            self.original_topmost = bool(win32.GetWindowLong(bar.hwnd, -20) & TOPMOST)
        # The frame is click-through. If an app is above the real tray,
        # raise Explorer and then place the frame immediately above it.
        if win32.SetWindowPos(bar.hwnd, -1, 0, 0, 0, 0, ZORDER_ONLY):
            win32.SetWindowPos(frame, -1, 0, 0, 0, 0, ZORDER_ONLY)

    def style_material(self, hwnd, accent):
        if self.read_material is None:
            if dock_glass.read_accent(hwnd) is not None:
                self.read_material, self.write_material = dock_glass.read_accent, dock_glass.write_accent
            else:
                self.read_material, self.write_material = dock_glass.read_backdrop, dock_glass.write_backdrop
        if self.read_material is dock_glass.read_accent:
            target = accent if accent is not None else (0, 0, 0, 0)
        else:
            target = 3 if accent is not None else 1  # DWMSBT_TRANSIENTWINDOW / DWMSBT_NONE
        if target != self.applied_material:
            current = self.read_material(hwnd)
            if current is not None and self.write_material(hwnd, target):
                if self.applied_material is None or current != self.applied_material:
                    self.original_material = current
                self.applied_material = target

    def update(self, span, hidden=False):
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
        owns_region = previous is not None and region_matches(current.hwnd, self.shape)
        if not owns_region:
            kind, box = window_region(current.hwnd)
            # Respect an existing taskbar mod. Native Explorer has either no
            # region or a full rectangle, which can adapt to a resolution change.
            if kind and (kind, box) != (2, (0, 0, right - left, bottom - top)):
                raise OSError("unsupported")
            self.original_rectangular = bool(kind)
        shape = HIDDEN_SHAPE if hidden else current.tray_shape
        presented = False
        if not owns_region or self.shape != shape:
            # SetWindowRgn sends WM_WINDOWPOSCHANGING, so Explorer can snap
            # the tray back to its original position while applying the clip.
            # Apply a visible clip while hidden, then place and show together.
            if not hidden:
                suppress_taskbar(current.hwnd)
            show_x = left
            try:
                put_region(current.hwnd, *shape, redraw=False)
                self.shape = shape
                self.applied = current
                show_x = target_x
            finally:
                if not hidden:
                    move_taskbar(current.hwnd, show_x, top, show=True)
                    self.shifted_x = show_x
                    presented = True
            if hidden and left == target_x:
                left = window_bounds(current.hwnd)[0]
        if not presented and left != target_x:
            move_taskbar(current.hwnd, target_x, top)
        self.shifted_x = target_x
        positioned = taskbar(current, target_x)
        if positioned.bounds[0] != target_x:
            raise OSError("pending")
        self.applied = positioned
        self.work_area.expand(positioned)
        return positioned

    def restore(self):
        current = self.applied
        try:
            if (current is not None and win32.FindWindow("Shell_TrayWnd", None) == current.hwnd
                    and process_id(current.hwnd) == current.pid):
                left, top, right, bottom = window_bounds(current.hwnd)
                if left == self.shifted_x and left != current.home_x:
                    move_taskbar(current.hwnd, current.home_x, top)
                if self.shape is not None and region_matches(current.hwnd, self.shape):
                    box = (0, 0, right - left, bottom - top) if self.original_rectangular else None
                    put_region(current.hwnd, box)
                if self.applied_material is not None and self.read_material(current.hwnd) == self.applied_material:
                    self.write_material(current.hwnd, self.original_material)
                if self.original_topmost is False and win32.GetWindowLong(current.hwnd, -20) & TOPMOST:
                    win32.SetWindowPos(current.hwnd, -2, 0, 0, 0, 0, ZORDER_ONLY)
                tray = win32.FindWindowEx(current.hwnd, None, "TrayNotifyWnd", None)
                if tray and not tray_fits(window_bounds(current.hwnd), window_bounds(tray)):
                    relayout_taskbar(current.hwnd)
        finally:
            self.work_area.restore()
        self.applied = self.shifted_x = None
        self.original_material = self.applied_material = self.original_topmost = None
        self.read_material = self.write_material = None
        self.shape = None


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
            request = json.loads(command)
            span = request["span"]
            if span is not None and (type(span) is not int or span <= 0):
                raise OSError("failed")
            hidden = request.get("hidden", False)
            try:
                bar = lease.update(span, hidden)
            except OSError as exc:
                if str(exc) != "pending":
                    raise
                reply({"waiting": True})
                continue
            if span is not None and "accent" in request:
                accent = request.get("accent")
                lease.style_tray(bar, request.get("frame"), tuple(accent) if accent is not None else None)
            reply({"hwnd": bar.hwnd, "tray": bar.tray, "bounds": bar.tray_bounds,
                   "positioned": span is not None, "hidden": hidden})
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
