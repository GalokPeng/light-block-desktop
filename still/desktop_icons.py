"""Use the native Explorer desktop view's icon visibility flag."""
from __future__ import annotations

from contextlib import contextmanager
import ctypes as C
from ctypes import wintypes as W
import json
import os
import uuid

from . import win32

NO_ICONS = 0x1000
DESKTOP = 0x20
STATE_FILE = "desktop-icon-state.json"
PTR = C.c_void_p
OUT = C.POINTER(PTR)


class Variant(C.Structure):
    # VARIANT's first eight bytes are the tag, followed by its value union.
    _fields_ = [("vt", W.WORD), ("reserved", W.WORD * 3),
                ("value", C.c_long), ("padding", C.c_byte * 12)]


def guid(value):
    return (C.c_ubyte * 16).from_buffer_copy(uuid.UUID(value).bytes_le)


def method(obj, slot, result, *arguments):
    table = C.cast(obj, C.POINTER(C.POINTER(PTR))).contents
    return C.WINFUNCTYPE(result, PTR, *arguments)(table[slot])


def checked(result, operation):
    if result < 0:
        raise OSError(f"{operation}失败（0x{result & 0xffffffff:08X}）")


@contextmanager
def folder_view():
    ole = C.WinDLL("ole32")
    initialize = win32.bind(ole, "CoInitializeEx", C.c_long, PTR, W.DWORD)
    create = win32.bind(ole, "CoCreateInstance", C.c_long, PTR, PTR, W.DWORD, PTR, OUT)
    uninitialize = win32.bind(ole, "CoUninitialize", None)
    initialized = initialize(None, 2)
    checked(initialized, "初始化桌面接口")
    interfaces = []

    def interface():
        obj = PTR()
        interfaces.append(obj)
        return obj

    def query(obj, ident):
        result = interface()
        checked(method(obj, 0, C.c_long, PTR, OUT)(obj, C.byref(guid(ident)), C.byref(result)), "获取桌面接口")
        return result

    try:
        windows = interface()
        checked(create(C.byref(guid("9ba05972-f6a8-11cf-a442-00a0c90a8f39")), None, 5,
                       C.byref(guid("85cb6900-4d95-11cf-960c-0080c7f4ee85")), C.byref(windows)), "连接资源管理器")
        location = Variant()
        location.vt = 3  # VT_I4, CSIDL_DESKTOP = 0
        root = Variant()
        desktop, hwnd = interface(), C.c_long()
        checked(method(windows, 15, C.c_long, PTR, PTR, C.c_int, C.POINTER(C.c_long), C.c_int, OUT)(
            windows, C.byref(location), C.byref(root), 8, C.byref(hwnd), 1, C.byref(desktop)), "查找原生桌面")
        if not desktop:
            raise OSError("资源管理器的桌面尚未就绪。")
        provider = query(desktop, "6d5140c1-7436-11ce-8034-00aa006009fa")
        browser = interface()
        checked(method(provider, 3, C.c_long, PTR, PTR, OUT)(
            provider, C.byref(guid("4c96be40-915c-11cf-99d3-00aa004ae837")),
            C.byref(guid("000214e2-0000-0000-c000-000000000046")), C.byref(browser)), "连接桌面视图")
        view = interface()
        checked(method(browser, 15, C.c_long, OUT)(browser, C.byref(view)), "取得桌面视图")
        if not view:
            raise OSError("资源管理器的桌面视图尚未就绪。")
        yield query(view, "1af3a467-214f-4298-908e-06b03e0b39f9")
    finally:
        for obj in reversed(interfaces):
            if obj:
                method(obj, 2, W.ULONG)(obj)
        uninitialize()


def read_flags(view):
    flags = W.DWORD()
    checked(method(view, 25, C.c_long, C.POINTER(W.DWORD))(view, C.byref(flags)), "读取桌面图标状态")
    if not flags.value & DESKTOP:
        raise OSError("未取得 Windows 桌面视图。")
    return flags.value


def icons_hidden(hidden=None):
    with folder_view() as view:
        previous = bool(read_flags(view) & NO_ICONS)
        if hidden is not None and previous != hidden:
            checked(method(view, 24, C.c_long, W.DWORD, W.DWORD)(
                view, NO_ICONS, NO_ICONS if hidden else 0), "切换原生桌面图标")
            if bool(read_flags(view) & NO_ICONS) != hidden:
                raise OSError("资源管理器未应用桌面图标状态。")
        return previous


class DesktopIconLease:
    """Record the original state before hiding icons and restore it on exit."""
    def __init__(self, directory):
        self.path = directory / STATE_FILE
        self.handle = None

    def claim(self):
        if self.handle:
            return
        create = win32.bind(win32.kernel32, "CreateMutexW", W.HANDLE, PTR, W.BOOL, W.LPCWSTR)
        C.set_last_error(0)
        handle = create(None, False, "Local\\Still.DesktopIconVisibility")
        error = C.get_last_error()
        if not handle:
            raise C.WinError(error)
        self.handle = handle
        if error == 183:
            self.release_handle()
            raise OSError("此 Windows 会话已有轻块桌面收纳正在管理桌面图标。")

    def release_handle(self):
        if self.handle:
            win32.bind(win32.kernel32, "CloseHandle", W.BOOL, W.HANDLE)(self.handle)
            self.handle = None

    def hide(self):
        self.claim()
        if self.path.exists():
            self.saved_state()
        else:
            original = icons_hidden()
            temp = self.path.with_suffix(".tmp")
            with temp.open("w", encoding="utf-8") as file:
                json.dump({"hidden": original}, file)
                file.flush()
                os.fsync(file.fileno())
            os.replace(temp, self.path)
        icons_hidden(True)

    def saved_state(self):
        try:
            data = json.loads(self.path.read_text(encoding="utf-8"))
            if type(data.get("hidden")) is not bool:
                raise ValueError("invalid desktop state")
        except (ValueError, AttributeError) as exc:
            raise OSError("桌面显示状态记录无法读取。可在桌面右键的「查看」中手动恢复图标。") from exc
        return data["hidden"]

    def restore(self):
        if not self.path.exists():
            self.release_handle()
            return
        self.claim()
        icons_hidden(self.saved_state())
        self.path.unlink()
        self.release_handle()
