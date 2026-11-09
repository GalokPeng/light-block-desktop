"""Open native Windows Search with the input field inside the search panel."""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
from pathlib import Path
import sys

from PySide6.QtCore import QObject, QTimer, Signal

from . import dock_windows, win32
from .desktop_icons import OUT, PTR, Variant, guid, method

START_SEARCH_IDS = ("StartMenuSearchBox", "SearchBoxToggleButton")
CLSID_AUTOMATION = guid("ff48dba4-60ef-4201-aa87-54103eef594e")
IID_AUTOMATION = guid("30cbe57d-d9d0-452a-ab13-7ac5ac4825ee")
IID_INVOKE = guid("fb377fbe-8ea6-46d5-9c73-6499642d3059")
IID_TOGGLE = guid("94cf8058-9b8d-4ab9-8bfd-4cd0a33c8c70")


def invoke_start_search(hwnd):
    """Invoke the Start menu's own accessible search button when present."""
    ole = C.WinDLL("ole32")
    automation = C.WinDLL("oleaut32")
    initialize = win32.bind(ole, "CoInitializeEx", C.c_long, PTR, W.DWORD)
    uninitialize = win32.bind(ole, "CoUninitialize", None)
    create = win32.bind(ole, "CoCreateInstance", C.c_long, PTR, PTR, W.DWORD, PTR, OUT)
    alloc = win32.bind(automation, "SysAllocString", PTR, W.LPCWSTR)
    free = win32.bind(automation, "SysFreeString", None, PTR)
    initialized = initialize(None, 2)
    if initialized < 0 and initialized != C.c_long(0x80010106).value:  # RPC_E_CHANGED_MODE
        return False
    client, root = PTR(), PTR()
    try:
        if create(C.byref(CLSID_AUTOMATION), None, 1, C.byref(IID_AUTOMATION), C.byref(client)) < 0:
            return False
        if method(client, 6, C.c_long, W.HWND, OUT)(client, hwnd, C.byref(root)) < 0:  # ElementFromHandle
            return False
        for name in START_SEARCH_IDS:
            value = alloc(name)
            if not value:
                continue
            prop = Variant()
            prop.vt = 8  # VT_BSTR
            PTR.from_buffer(prop, 8).value = value
            condition, element = PTR(), PTR()
            try:
                if method(client, 23, C.c_long, C.c_int, Variant, OUT)(
                        client, 30011, prop, C.byref(condition)) < 0:  # AutomationId
                    continue
                if method(root, 5, C.c_long, C.c_int, PTR, OUT)(
                        root, 4, condition, C.byref(element)) < 0 or not element:  # Descendants
                    continue
                get_pattern = method(element, 14, C.c_long, C.c_int, PTR, OUT)
                for pattern, iid in ((10000, IID_INVOKE), (10015, IID_TOGGLE)):
                    action = PTR()
                    try:
                        if get_pattern(element, pattern, C.byref(iid), C.byref(action)) >= 0 and action:
                            if method(action, 3, C.c_long)(action) >= 0:
                                return True
                    finally:
                        if action:
                            method(action, 2, W.ULONG)(action)
            finally:
                for obj in (element, condition):
                    if obj:
                        method(obj, 2, W.ULONG)(obj)
                free(value)
        return False
    finally:
        for obj in (root, client):
            if obj:
                method(obj, 2, W.ULONG)(obj)
        if initialized >= 0:
            uninitialize()


class DockSearch(QObject):
    failed = Signal()

    def __init__(self, parent):
        super().__init__(parent)
        self.timer = QTimer(self, interval=40)
        self.timer.timeout.connect(self.check_start)
        self.taskbar_top = 0
        self.attempts = 0
        self.last_window = None
        self.last_program = ""

    def open(self, taskbar_top):
        self.stop()
        self.taskbar_top = taskbar_top
        if sys.getwindowsversion().build < 22000:
            self.fallback()
        elif self.inline_search(win32.GetForegroundWindow()):
            return
        elif dock_windows.send_key_chord(0x11, 0x1B):  # Ctrl+Esc opens Start.
            self.attempts = 0
            self.timer.start()
        else:
            self.fallback()

    def program(self, hwnd):
        if hwnd != self.last_window:
            self.last_window = hwnd
            self.last_program = Path(dock_windows.process_target(dock_windows.window_pid(hwnd))[1]).name.lower() if hwnd else ""
        return self.last_program

    def inline_search(self, hwnd):
        if self.program(hwnd) != "searchhost.exe":
            return False
        bounds = W.RECT()
        return bool(win32.GetWindowRect(hwnd, C.byref(bounds)) and bounds.bottom <= self.taskbar_top + 2)

    def check_start(self):
        self.attempts += 1
        hwnd = win32.GetForegroundWindow()
        if self.inline_search(hwnd) or (self.program(hwnd) == "startmenuexperiencehost.exe" and invoke_start_search(hwnd)):
            self.stop()
        elif self.attempts >= 15:
            self.stop()
            self.fallback()

    def fallback(self):
        if not dock_windows.open_windows_search():
            self.failed.emit()

    def stop(self):
        self.timer.stop()
        self.last_window = None
        self.last_program = ""
