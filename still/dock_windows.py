"""Windows task discovery and activation for the Dock."""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
from dataclasses import dataclass
import logging
import os

from PySide6.QtCore import QObject, Signal
from PySide6.QtGui import QImage

from . import win32 as api
from .model import APPS_FOLDER

EVENT_FOREGROUND = 0x0003
EVENT_MINIMIZE_START, EVENT_MINIMIZE_END = 0x0016, 0x0017
EVENT_DESTROY, EVENT_SHOW, EVENT_HIDE = 0x8001, 0x8002, 0x8003
EVENT_LOCATION = 0x800B
EVENT_CLOAKED, EVENT_UNCLOAKED = 0x8017, 0x8018
SHELL_CLASSES = {"Progman", "WorkerW", "Shell_TrayWnd", "Shell_SecondaryTrayWnd"}

shell32 = C.WinDLL("shell32", use_last_error=True)
ole32 = C.WinDLL("ole32")
get_window = api.bind(api.user32, "GetWindow", W.HWND, W.HWND, W.UINT)
is_visible = api.bind(api.user32, "IsWindowVisible", W.BOOL, W.HWND)
get_text = api.bind(api.user32, "GetWindowTextW", C.c_int, W.HWND, W.LPWSTR, C.c_int)
is_iconic = api.bind(api.user32, "IsIconic", W.BOOL, W.HWND)
show_async = api.bind(api.user32, "ShowWindowAsync", W.BOOL, W.HWND, C.c_int)
set_foreground = api.bind(api.user32, "SetForegroundWindow", W.BOOL, W.HWND)
flash_window = api.bind(api.user32, "FlashWindow", W.BOOL, W.HWND, W.BOOL)
open_process = api.bind(api.kernel32, "OpenProcess", W.HANDLE, W.DWORD, W.BOOL, W.DWORD)
close_handle = api.bind(api.kernel32, "CloseHandle", W.BOOL, W.HANDLE)
process_image = api.bind(api.kernel32, "QueryFullProcessImageNameW", W.BOOL,
                         W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD))
process_app_id = api.bind(api.kernel32, "GetApplicationUserModelId", C.c_long,
                          W.HANDLE, C.POINTER(W.UINT), W.LPWSTR)

WinEventProc = C.WINFUNCTYPE(None, W.HANDLE, W.DWORD, W.HWND, W.LONG, W.LONG, W.DWORD, W.DWORD)
set_hook = api.bind(api.user32, "SetWinEventHook", W.HANDLE, W.DWORD, W.DWORD, W.HMODULE,
                    WinEventProc, W.DWORD, W.DWORD, W.DWORD)
unhook = api.bind(api.user32, "UnhookWinEvent", W.BOOL, W.HANDLE)
lock_workstation = api.bind(api.user32, "LockWorkStation", W.BOOL)
destroy_icon = api.bind(api.user32, "DestroyIcon", W.BOOL, W.HICON)
extract_icon = api.bind(shell32, "SHDefExtractIconW", C.c_long, W.LPCWSTR, C.c_int, W.UINT,
                        C.POINTER(W.HICON), C.POINTER(W.HICON), W.UINT)
parse_shell_name = api.bind(shell32, "SHParseDisplayName", C.c_long, W.LPCWSTR, C.c_void_p,
                            C.POINTER(C.c_void_p), W.DWORD, C.POINTER(W.DWORD))
free_shell_item = api.bind(ole32, "CoTaskMemFree", None, C.c_void_p)


class ShellFileInfo(C.Structure):
    _fields_ = [("icon", W.HICON), ("index", C.c_int), ("attributes", W.DWORD),
                ("display_name", W.WCHAR * 260), ("type_name", W.WCHAR * 80)]


get_shell_info = api.bind(shell32, "SHGetFileInfoW", C.c_size_t, C.c_void_p, W.DWORD,
                          C.POINTER(ShellFileInfo), W.UINT, W.UINT)


def take_icon(icon):
    if not icon:
        return QImage()
    try:
        return QImage.fromHICON(icon)
    finally:
        destroy_icon(icon)


def executable_icon(path, size):
    icon = W.HICON()
    extract_icon(path, 0, 0, C.byref(icon), None, size)
    return take_icon(icon.value)


def package_visual(target):
    """Resolve a packaged Start-menu entry through the Windows shell namespace."""
    pidl = C.c_void_p()
    if parse_shell_name(target, None, C.byref(pidl), 0, None) != 0:
        return "", QImage()
    try:
        info = ShellFileInfo()
        if not get_shell_info(pidl, 0, C.byref(info), C.sizeof(info), 0x308):  # ICON | DISPLAYNAME | PIDL
            return "", QImage()
        return info.display_name, take_icon(info.icon)
    finally:
        free_shell_item(pidl)


@dataclass(frozen=True)
class AppWindow:
    hwnd: int
    pid: int
    target: str
    executable: str
    title: str


def window_pid(hwnd):
    pid = W.DWORD()
    api.GetWindowThreadProcessId(hwnd, C.byref(pid))
    return pid.value


def window_title(hwnd):
    title = C.create_unicode_buffer(512)
    get_text(hwnd, title, len(title))
    return title.value.strip()


def window_class(hwnd):
    name = C.create_unicode_buffer(128)
    api.GetClassName(hwnd, name, len(name))
    return name.value


def task_candidate(hwnd):
    if not is_visible(hwnd) or api.GetWindowLong(hwnd, -16) & 0x40000000:  # WS_CHILD
        return False
    style = api.GetWindowLong(hwnd, -20)  # GWL_EXSTYLE
    app_window = bool(style & 0x40000)  # WS_EX_APPWINDOW
    return (app_window or not (style & 0x80 or get_window(hwnd, 4))) and not style & 0x8000000


def process_target(pid):
    handle = open_process(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return "", ""
    try:
        image = C.create_unicode_buffer(32768)
        length = W.DWORD(len(image))
        if not process_image(handle, 0, image, C.byref(length)):
            return "", ""
        app_id = C.create_unicode_buffer(256)
        size = W.UINT(len(app_id))
        if process_app_id(handle, C.byref(size), app_id) == 0:
            return APPS_FOLDER + app_id.value, image.value
        return image.value, image.value
    finally:
        close_handle(handle)


def task_windows() -> list[AppWindow]:
    windows = []
    targets = {}
    own_pid = os.getpid()

    @api.ENUMPROC
    def visit(hwnd, _):
        if not task_candidate(hwnd):
            return True
        pid = window_pid(hwnd)
        if not pid or pid == own_pid:
            return True
        cloaked = W.DWORD()
        if api.DwmGetWindowAttribute(hwnd, 14, C.byref(cloaked), C.sizeof(cloaked)) == 0 and cloaked.value:
            return True
        title = window_title(hwnd)
        name = window_class(hwnd)
        if not title or name in SHELL_CLASSES:
            return True
        app_pid = pid
        if name == "ApplicationFrameWindow":
            child = api.FindWindowEx(hwnd, None, "Windows.UI.Core.CoreWindow", None)
            if not child:
                return True
            app_pid = window_pid(child)
        if app_pid not in targets:
            targets[app_pid] = process_target(app_pid)
        target, image = targets[app_pid]
        if target:
            windows.append(AppWindow(hwnd, pid, target, image, title))
        return True

    api.EnumWindows(visit, 0)
    return windows


def activate(window: AppWindow):
    if window_pid(window.hwnd) != window.pid:
        return
    if is_iconic(window.hwnd):
        show_async(window.hwnd, 9)  # SW_RESTORE
    if not set_foreground(window.hwnd):
        flash_window(window.hwnd, True)


class KeyboardInput(C.Structure):
    _fields_ = [("key", W.WORD), ("scan", W.WORD), ("flags", W.DWORD),
                ("time", W.DWORD), ("extra", C.c_size_t)]


class MouseInput(C.Structure):
    _fields_ = [("x", W.LONG), ("y", W.LONG), ("data", W.DWORD), ("flags", W.DWORD),
                ("time", W.DWORD), ("extra", C.c_size_t)]


class InputValue(C.Union):
    _fields_ = [("keyboard", KeyboardInput), ("mouse", MouseInput)]


class Input(C.Structure):
    _fields_ = [("type", W.DWORD), ("value", InputValue)]


send_input = api.bind(api.user32, "SendInput", W.UINT, W.UINT, C.POINTER(Input), C.c_int)


def send_key_chord(modifier, key):
    sequence = ((modifier, 0), (key, 0), (key, 2), (modifier, 2))
    inputs = (Input * len(sequence))(*(Input(1, InputValue(keyboard=KeyboardInput(key, 0, flags)))
                                       for key, flags in sequence))
    return send_input(len(inputs), inputs, C.sizeof(Input)) == len(inputs)


def open_windows_search():
    return send_key_chord(0x5B, 0x53)  # Win+S


class TaskWatcher(QObject):
    changed = Signal(bool)  # True when the task list needs refreshing as well.
    shell_changed = Signal()

    def __init__(self, parent):
        super().__init__(parent)
        self.known = set()
        self.shell_windows = set()
        self.callback = WinEventProc(self._event)
        self.hooks = []
        for first, last in ((EVENT_FOREGROUND, EVENT_FOREGROUND),
                            (EVENT_MINIMIZE_START, EVENT_MINIMIZE_END),
                            (EVENT_DESTROY, EVENT_HIDE),
                            (EVENT_LOCATION, EVENT_LOCATION),
                            (EVENT_CLOAKED, EVENT_UNCLOAKED)):
            handle = set_hook(first, last, None, self.callback, 0, 0, 0)
            if handle:
                self.hooks.append(handle)
            else:
                logging.warning("Dock WinEvent hook failed for %#x-%#x", first, last)

    def _event(self, hook, event, hwnd, object_id, child_id, thread, timestamp):
        if not hwnd or object_id or child_id:
            return
        if hwnd in self.shell_windows and event in (EVENT_LOCATION, EVENT_SHOW, EVENT_HIDE, EVENT_DESTROY):
            self.shell_changed.emit()
        if event == EVENT_LOCATION:
            if hwnd == api.GetForegroundWindow():
                self.changed.emit(False)
        elif event == EVENT_FOREGROUND or hwnd in self.known:
            self.changed.emit(True)
        elif event in (EVENT_SHOW, EVENT_UNCLOAKED) and task_candidate(hwnd) and window_pid(hwnd) != os.getpid():
            self.changed.emit(True)

    def stop(self):
        for hook in self.hooks:
            unhook(hook)
        self.hooks.clear()
        self.known.clear()
        self.shell_windows.clear()
