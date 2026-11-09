"""Desktop-owned tools, native glass and known-folder lookup. No busy polling."""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
import sys
import uuid
from pathlib import Path
from PySide6.QtCore import QAbstractNativeEventFilter, QCoreApplication, QObject, Signal
from PySide6.QtGui import QColor, QLinearGradient, QPen

from . import win32 as api


class MARGINS(C.Structure):
    _fields_ = [("left", C.c_int), ("right", C.c_int), ("top", C.c_int), ("bottom", C.c_int)]


def desktop_owner():
    windows = []
    @api.ENUMPROC
    def visit(hwnd, _):
        if api.FindWindowEx(hwnd, None, "SHELLDLL_DefView", None):
            windows.append(hwnd)
        return True
    api.EnumWindows(visit, 0)
    return windows[0] if windows else api.FindWindow("Progman", None)


def desktop_layer(hwnd):
    """Own, do not reparent: keep Qt's backing store and DWM top-level surface."""
    owner = desktop_owner()
    if owner:
        api.SetWindowLong(hwnd, -8, owner)
    extended = api.GetWindowLong(hwnd, -20)
    api.SetWindowLong(hwnd, -20, (extended & ~0x40000 & ~0x8) | 0x80 | 0x08000000)
    # An owned window stays above its owner. HWND_BOTTOM puts it under ordinary
    # applications, without hiding it underneath the desktop wallpaper window.
    api.SetWindowPos(hwnd, 1, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010)
    return owner


def paint_desktop_panel(painter, rect, opacity):
    alpha = min(241, max(149, int(opacity) + 27))
    fill = QLinearGradient(0, rect.top(), 0, rect.bottom())
    fill.setColorAt(0, QColor(39, 51, 71, alpha))
    fill.setColorAt(1, QColor(20, 28, 43, min(248, alpha + 20)))
    painter.setBrush(fill)
    painter.setPen(QPen(QColor(230, 239, 255, 78), 1))
    painter.drawRoundedRect(rect.adjusted(1, 1, -1, -1), 23, 23)


def native_glass(hwnd, enabled=True):
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize") as key:
            enabled = enabled and bool(winreg.QueryValueEx(key, "EnableTransparency")[0])
    except OSError:
        pass
    class HIGHCONTRAST(C.Structure):
        _fields_ = [("size", W.UINT), ("flags", W.DWORD), ("scheme", W.LPWSTR)]
    contrast = HIGHCONTRAST(C.sizeof(HIGHCONTRAST), 0, None)
    if api.SystemParametersInfo(0x0042, contrast.size, C.byref(contrast), 0) and contrast.flags & 1:
        enabled = False
    set_attribute = api.bind(api.dwmapi, "DwmSetWindowAttribute", C.c_long, W.HWND, W.DWORD, C.c_void_p, W.DWORD)
    for attribute, value in ((20, 1), (33, 2), (38, 3 if enabled else 1)):
        setting = C.c_int(value)
        result = set_attribute(hwnd, attribute, C.byref(setting), C.sizeof(setting))
        if attribute == 38:
            supported = result == 0 and enabled
    margins = MARGINS(-1, -1, -1, -1) if enabled else MARGINS(0, 0, 0, 0)
    api.bind(api.dwmapi, "DwmExtendFrameIntoClientArea", C.c_long, W.HWND, C.POINTER(MARGINS))(hwnd, C.byref(margins))
    # Desktop tools deliberately do not activate, while DWM Acrylic can become
    # solid when inactive. Accent blur keeps a small background panel frosted.
    # This compatibility API is optional; ordinary tinted glass is the fallback.
    class ACCENT(C.Structure):
        _fields_ = [("state", C.c_int), ("flags", C.c_int), ("color", W.DWORD), ("animation", C.c_int)]
    class DATA(C.Structure):
        _fields_ = [("attribute", C.c_int), ("data", C.c_void_p), ("size", C.c_size_t)]
    if hasattr(api.user32, "SetWindowCompositionAttribute"):
        accent = ACCENT(3 if enabled else 0, 0, 0, 0)
        data = DATA(19, C.cast(C.byref(accent), C.c_void_p), C.sizeof(accent))
        function = api.bind(api.user32, "SetWindowCompositionAttribute", W.BOOL, W.HWND, C.POINTER(DATA))
        supported = bool(function(hwnd, C.byref(data))) and enabled
    return supported


def known_folder(ident):
    shell = C.WinDLL("shell32")
    function = api.bind(shell, "SHGetKnownFolderPath", C.c_long, C.c_void_p, W.DWORD, W.HANDLE, C.POINTER(W.LPWSTR))
    guid = (C.c_ubyte * 16).from_buffer_copy(uuid.UUID(ident).bytes_le)
    result = W.LPWSTR()
    if function(C.byref(guid), 0, None, C.byref(result)) == 0:
        try:
            return Path(result.value)
        finally:
            ole = C.WinDLL("ole32")
            api.bind(ole, "CoTaskMemFree", None, C.c_void_p)(C.cast(result, C.c_void_p))
    return None


def desktop_folders():
    return [p for p in (known_folder("B4BFCC3A-DB2C-424C-B029-7FE99A87C641"),
                        known_folder("C4AA340D-F20F-4863-AFEF-F87EF2E6BA25")) if p and p.is_dir()]


def start_menu_folders():
    return [p for p in (known_folder("A77F5D77-2E2B-44C3-A6A2-ABA601054A51"),
                        known_folder("0139D44E-6AFE-49F2-8690-3DAFCAE6FFB8")) if p and p.is_dir()]


def open_target(target):
    shell = C.WinDLL("shell32", use_last_error=True)
    execute = api.bind(shell, "ShellExecuteW", C.c_ssize_t, W.HWND, W.LPCWSTR, W.LPCWSTR, W.LPCWSTR, W.LPCWSTR, C.c_int)
    # Pass paths as a single structured argument; never construct shell commands.
    result = execute(None, "open", str(target), None, None, 1)
    if result <= 32:
        raise OSError(f"无法打开该项目（Windows 错误 {result}）。")


def reveal_file(path):
    import subprocess
    subprocess.Popen(["explorer.exe", "/select,", str(path)])


class ShellEvents(QAbstractNativeEventFilter):
    def __init__(self, watcher):
        super().__init__()
        self.watcher = watcher
        self.message = api.bind(api.user32, "RegisterWindowMessageW", W.UINT, W.LPCWSTR)("TaskbarCreated")

    def nativeEventFilter(self, event_type, message):
        msg = W.MSG.from_address(int(message))
        if msg.message == self.message:
            self.watcher.changed.emit()
        return False, 0


class DesktopWatcher(QObject):
    changed = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.hook = None
        self.owner = None
        callback_type = C.WINFUNCTYPE(None, W.HANDLE, W.DWORD, W.HWND, W.LONG, W.LONG, W.DWORD, W.DWORD)
        self.callback_type = callback_type
        self.callback = callback_type(self._event)
        self.shell_events = ShellEvents(self)
        self.filter_installed = False

    def _event(self, hook, event, hwnd, object_id, child_id, thread, timestamp):
        if hwnd == self.owner and object_id == 0:
            self.changed.emit()

    def start(self):
        self.stop()
        app = QCoreApplication.instance()
        if app:
            app.installNativeEventFilter(self.shell_events)
            self.filter_installed = True
        self.owner = desktop_owner()
        if not self.owner:
            return
        process = W.DWORD()
        api.GetWindowThreadProcessId(self.owner, C.byref(process))
        function = api.bind(api.user32, "SetWinEventHook", W.HANDLE, W.DWORD, W.DWORD, W.HMODULE,
                            self.callback_type, W.DWORD, W.DWORD, W.DWORD)
        self.hook = function(0x8001, 0x8001, None, self.callback, process.value, 0, 0)

    def stop(self):
        if self.filter_installed:
            QCoreApplication.instance().removeNativeEventFilter(self.shell_events)
            self.filter_installed = False
        if self.hook:
            api.bind(api.user32, "UnhookWinEvent", W.BOOL, W.HANDLE)(self.hook)
            self.hook = None
