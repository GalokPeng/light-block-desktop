"""Supported Windows Settings entry points and conservative availability checks."""
import ctypes as C
from ctypes import wintypes as W
import sys
import winreg

from PySide6.QtCore import QUrl
from PySide6.QtGui import QDesktopServices
from . import win32
from .i18n import tr


class DisplayDevice(C.Structure):
    _fields_ = [("size", W.DWORD), ("name", W.WCHAR*32), ("description", W.WCHAR*128),
                ("flags", W.DWORD), ("ident", W.WCHAR*128), ("key", W.WCHAR*128)]


def night_light_available():
    version = sys.getwindowsversion()
    if version.major < 10 or version.build < 15063 or version.product_type != 1:
        return False, tr("此 Windows 版本不提供系统夜间模式。")
    for hive in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
        try:
            with winreg.OpenKey(hive, r"Software\Microsoft\Windows\CurrentVersion\Policies\Explorer") as key:
                policy = winreg.QueryValueEx(key, "SettingsPageVisibility")[0].casefold()
            kind, _, pages = policy.partition(":")
            pages = set(pages.split(";"))
            if (kind == "hide" and "nightlight" in pages) or (kind == "showonly" and "nightlight" not in pages):
                return False, tr("系统策略未开放夜间模式设置。")
        except (OSError, AttributeError):
            pass
    if win32.bind(win32.user32, "GetSystemMetrics", C.c_int, C.c_int)(0x1000):
        return False, tr("远程显示环境不提供本机夜间模式。")
    enumerate_devices = win32.bind(win32.user32, "EnumDisplayDevicesW", W.BOOL, W.LPCWSTR,
                                   W.DWORD, C.POINTER(DisplayDevice), W.DWORD)
    for index in range(32):
        device = DisplayDevice(C.sizeof(DisplayDevice))
        if not enumerate_devices(None, index, C.byref(device), 0):
            break
        if not device.flags & 4:  # Active primary adapter.
            continue
        description = device.description.casefold()
        values = [description, device.ident.casefold()]
        prefix = "\\Registry\\Machine\\"
        if device.key.startswith(prefix):
            try:
                with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, device.key[len(prefix):]) as key:
                    for value in ("InstalledDisplayDrivers", "DriverDesc", "Service"):
                        try: values.append(str(winreg.QueryValueEx(key, value)[0]).casefold())
                        except OSError: pass
            except OSError:
                pass
        if any(word in " ".join(values) for word in ("basicdisplay", "basic display", "displaylink", "基本显示")):
            return False, tr("当前显示驱动不支持系统夜间模式。")
    return True, tr("由 Windows 管理暖色强度和时间计划。")


def open_night_light():
    available, message = night_light_available()
    if not available:
        raise OSError(message)
    if not QDesktopServices.openUrl(QUrl("ms-settings:nightlight")):
        raise OSError(tr("无法打开系统夜间模式设置。"))
