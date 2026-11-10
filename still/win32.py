"""Small, explicitly typed Win32 surface. No pywin32, shell hooks or polling loops."""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import subprocess
import sys
import winreg

from .i18n import tr

user32 = C.WinDLL("user32", use_last_error=True)
kernel32 = C.WinDLL("kernel32", use_last_error=True)
dwmapi = C.WinDLL("dwmapi", use_last_error=True)
psapi = C.WinDLL("psapi", use_last_error=True)
LRESULT = C.c_ssize_t
ENUMPROC = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)


def bind(dll, name, restype, *args):
    func = getattr(dll, name)
    func.restype, func.argtypes = restype, list(args)
    return func


FindWindow = bind(user32, "FindWindowW", W.HWND, W.LPCWSTR, W.LPCWSTR)
FindWindowEx = bind(user32, "FindWindowExW", W.HWND, W.HWND, W.HWND, W.LPCWSTR, W.LPCWSTR)
EnumWindows = bind(user32, "EnumWindows", W.BOOL, ENUMPROC, W.LPARAM)
SendMessageTimeout = bind(user32, "SendMessageTimeoutW", LRESULT, W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT, C.POINTER(C.c_size_t))
GetParent = bind(user32, "GetParent", W.HWND, W.HWND)
SetParent = bind(user32, "SetParent", W.HWND, W.HWND, W.HWND)
GetWindowLong = bind(user32, "GetWindowLongPtrW", C.c_ssize_t, W.HWND, C.c_int)
SetWindowLong = bind(user32, "SetWindowLongPtrW", C.c_ssize_t, W.HWND, C.c_int, C.c_ssize_t)
SetWindowPos = bind(user32, "SetWindowPos", W.BOOL, W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT)
MapWindowPoints = bind(user32, "MapWindowPoints", C.c_int, W.HWND, W.HWND, C.POINTER(W.POINT), W.UINT)
GetWindowRect = bind(user32, "GetWindowRect", W.BOOL, W.HWND, C.POINTER(W.RECT))
IsWindow = bind(user32, "IsWindow", W.BOOL, W.HWND)
IsZoomed = bind(user32, "IsZoomed", W.BOOL, W.HWND)
GetForegroundWindow = bind(user32, "GetForegroundWindow", W.HWND)
SetForegroundWindow = bind(user32, "SetForegroundWindow", W.BOOL, W.HWND)
BringWindowToTop = bind(user32, "BringWindowToTop", W.BOOL, W.HWND)
SetActiveWindow = bind(user32, "SetActiveWindow", W.HWND, W.HWND)
AttachThreadInput = bind(user32, "AttachThreadInput", W.BOOL, W.DWORD, W.DWORD, W.BOOL)
GetClassName = bind(user32, "GetClassNameW", C.c_int, W.HWND, W.LPWSTR, C.c_int)
GetWindowThreadProcessId = bind(user32, "GetWindowThreadProcessId", W.DWORD, W.HWND, C.POINTER(W.DWORD))
SystemParametersInfo = bind(user32, "SystemParametersInfoW", W.BOOL, W.UINT, W.UINT, C.c_void_p, W.UINT)
MonitorFromWindow = bind(user32, "MonitorFromWindow", W.HANDLE, W.HWND, W.DWORD)
GetCurrentProcess = bind(kernel32, "GetCurrentProcess", W.HANDLE)
GetCurrentThreadId = bind(kernel32, "GetCurrentThreadId", W.DWORD)
DwmGetWindowAttribute = bind(dwmapi, "DwmGetWindowAttribute", C.c_long, W.HWND, W.DWORD, C.c_void_p, W.DWORD)
DwmSetWindowAttribute = bind(dwmapi, "DwmSetWindowAttribute", C.c_long, W.HWND, W.DWORD, C.c_void_p, W.DWORD)


def activate_from_tray(hwnd):
    """Honor an explicit tray command while keeping the window in its normal z-order band."""
    if GetForegroundWindow() == hwnd or SetForegroundWindow(hwnd):
        return
    foreground = GetForegroundWindow()
    current_thread = GetCurrentThreadId()
    foreground_thread = GetWindowThreadProcessId(foreground, None) if foreground else 0
    if not foreground_thread:
        return
    if foreground_thread == current_thread:
        SetActiveWindow(hwnd)
        return
    result = C.c_size_t()
    if not SendMessageTimeout(foreground, 0, 0, 0, 0x0003, 150, C.byref(result)):
        return  # An unresponsive foreground app must not block the settings UI.
    if GetForegroundWindow() != foreground or not AttachThreadInput(current_thread, foreground_thread, True):
        return
    try:
        BringWindowToTop(hwnd)
        SetActiveWindow(hwnd)
    finally:
        AttachThreadInput(current_thread, foreground_thread, False)


def theme_frame(hwnd, light):
    """Apply the app theme to the native title bar and border."""
    dark = W.BOOL(not light)
    if DwmSetWindowAttribute(hwnd, 20, C.byref(dark), C.sizeof(dark)):
        DwmSetWindowAttribute(hwnd, 19, C.byref(dark), C.sizeof(dark))
    if sys.getwindowsversion().build >= 22000:
        # COLORREF is stored as 0x00BBGGRR.
        colors = (0xD5DBD4, 0xF5F8F6, 0x312822) if light else (0x40342A, 0x211912, 0xF9F4ED)
        for attribute, color in zip((34, 35, 36), colors):
            value = W.DWORD(color)
            DwmSetWindowAttribute(hwnd, attribute, C.byref(value), C.sizeof(value))
    SetWindowPos(hwnd, None, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0004 | 0x0010 | 0x0020)


class MONITORINFOEX(C.Structure):
    _fields_ = [("cbSize", W.DWORD), ("rcMonitor", W.RECT), ("rcWork", W.RECT),
                ("dwFlags", W.DWORD), ("szDevice", W.WCHAR * 32)]


GetMonitorInfo = bind(user32, "GetMonitorInfoW", W.BOOL, W.HANDLE, C.POINTER(MONITORINFOEX))
EnumDisplayMonitors = user32.EnumDisplayMonitors
MONPROC = C.WINFUNCTYPE(W.BOOL, W.HANDLE, W.HDC, C.POINTER(W.RECT), W.LPARAM)
EnumDisplayMonitors.argtypes = [W.HDC, C.POINTER(W.RECT), MONPROC, W.LPARAM]
EnumDisplayMonitors.restype = W.BOOL


def monitors() -> list[dict]:
    result = []
    @MONPROC
    def callback(handle, _dc, _rect, _data):
        info = MONITORINFOEX()
        info.cbSize = C.sizeof(info)
        if GetMonitorInfo(handle, C.byref(info)):
            r = info.rcMonitor
            result.append({"name": info.szDevice, "rect": (r.left, r.top, r.right-r.left, r.bottom-r.top),
                           "primary": bool(info.dwFlags & 1)})
        return True
    EnumDisplayMonitors(None, None, callback, 0)
    return result


def target_monitor(name: str = "") -> dict:
    screens = monitors()
    if not screens:
        raise OSError(tr("无法获取显示器信息。"))
    return next((s for s in screens if s["name"] == name), next((s for s in screens if s["primary"]), screens[0]))


def desktop_worker() -> int:
    """Explorer's undocumented WorkerW topology: never fall back to an overlay."""
    progman = FindWindow("Progman", None)
    if not progman:
        raise OSError(tr("未找到 Windows Explorer 桌面，请稍后重试。"))
    result = C.c_size_t()
    SendMessageTimeout(progman, 0x052C, 0xD, 0, 0x2, 1000, C.byref(result))
    SendMessageTimeout(progman, 0x052C, 0xD, 1, 0x2, 1000, C.byref(result))
    workers = []
    @ENUMPROC
    def callback(hwnd, _data):
        if FindWindowEx(hwnd, None, "SHELLDLL_DefView", None):
            worker = FindWindowEx(None, hwnd, "WorkerW", None)
            if worker:
                workers.append(worker)
        return True
    EnumWindows(callback, 0)
    if workers:
        return workers[0]
    # New Windows 11 builds can nest the wallpaper WorkerW under Progman.
    worker = FindWindowEx(progman, None, "WorkerW", None)
    if worker and not FindWindowEx(worker, None, "SHELLDLL_DefView", None):
        return worker
    raise OSError(tr("当前 Explorer 桌面层结构不兼容，已取消动态壁纸；可继续使用静态壁纸。"))


def attach(hwnd: int, parent: int, rect: tuple[int, int, int, int]):
    style = GetWindowLong(hwnd, -16)
    SetWindowLong(hwnd, -16, (style & ~0x80000000 & ~0x00CF0000) | 0x40000000)
    ext = GetWindowLong(hwnd, -20)
    SetWindowLong(hwnd, -20, (ext & ~0x40000) | 0x08000000 | 0x80 | 0x20)
    C.set_last_error(0)
    previous = SetParent(hwnd, parent)
    error = C.get_last_error()
    if not previous and error:
        raise C.WinError(error)
    if GetParent(hwnd) != parent:
        raise OSError(tr("无法挂接桌面层。"))
    position(hwnd, parent, rect)


def position(hwnd: int, parent: int, rect):
    x, y, width, height = rect
    point = W.POINT(x, y)
    MapWindowPoints(None, parent, C.byref(point), 1)
    if not SetWindowPos(hwnd, None, point.x, point.y, width, height, 0x0010 | 0x0004 | 0x0020 | 0x0040):
        raise C.WinError(C.get_last_error())


def current_wallpaper() -> dict:
    buf = C.create_unicode_buffer(32768)
    SystemParametersInfo(0x0073, len(buf), C.cast(buf, C.c_void_p), 0)
    values = {"path": buf.value, "WallpaperStyle": "10", "TileWallpaper": "0"}
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Control Panel\Desktop") as key:
        for name in ("WallpaperStyle", "TileWallpaper"):
            try:
                values[name] = winreg.QueryValueEx(key, name)[0]
            except FileNotFoundError:
                pass
    return values


def set_wallpaper(path: str, fit: str = "fill", original: dict | None = None):
    if path and not Path(path).is_file():
        raise FileNotFoundError(tr("找不到壁纸文件：{0}").format(path))
    style = {"fill": "10", "fit": "6", "stretch": "2", "center": "0"}[fit]
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Control Panel\Desktop", 0, winreg.KEY_SET_VALUE) as key:
        winreg.SetValueEx(key, "WallpaperStyle", 0, winreg.REG_SZ, original["WallpaperStyle"] if original else style)
        winreg.SetValueEx(key, "TileWallpaper", 0, winreg.REG_SZ, original["TileWallpaper"] if original else "0")
    buf = C.create_unicode_buffer(path)
    if not SystemParametersInfo(0x0014, 0, C.cast(buf, C.c_void_p), 0x01 | 0x02):
        raise C.WinError(C.get_last_error())


RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
RUN_NAME = "StillWallpaper"


def startup_command() -> str:
    if getattr(sys, "frozen", False):
        return subprocess.list2cmdline([sys.executable, "--background"])
    executable = Path(sys.executable).with_name("pythonw.exe")
    return subprocess.list2cmdline([str(executable), str(Path(__file__).resolve().parent.parent / "main.py"), "--background"])


def startup_enabled() -> bool:
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as key:
            command = winreg.QueryValueEx(key, RUN_NAME)[0]
            return command == startup_command()
    except FileNotFoundError:
        return False


def set_startup(enabled: bool):
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as key:
        if enabled:
            winreg.SetValueEx(key, RUN_NAME, 0, winreg.REG_SZ, startup_command())
        else:
            try:
                winreg.DeleteValue(key, RUN_NAME)
            except FileNotFoundError:
                pass


class POWER(C.Structure):
    _fields_ = [("ACLineStatus", W.BYTE), ("BatteryFlag", W.BYTE), ("BatteryLifePercent", W.BYTE),
                ("SystemStatusFlag", W.BYTE), ("BatteryLifeTime", W.DWORD), ("BatteryFullLifeTime", W.DWORD)]


GetSystemPowerStatus = bind(kernel32, "GetSystemPowerStatus", W.BOOL, C.POINTER(POWER))
OpenInputDesktop = bind(user32, "OpenInputDesktop", W.HANDLE, W.DWORD, W.BOOL, W.DWORD)
CloseDesktop = bind(user32, "CloseDesktop", W.BOOL, W.HANDLE)
GetUserObjectInformation = bind(user32, "GetUserObjectInformationW", W.BOOL, W.HANDLE, C.c_int, C.c_void_p, W.DWORD, C.POINTER(W.DWORD))


def on_battery() -> bool:
    power = POWER()
    return bool(GetSystemPowerStatus(C.byref(power)) and power.ACLineStatus == 0)


def desktop_locked() -> bool:
    desktop = OpenInputDesktop(0, False, 0x0001)
    if not desktop:
        return True
    try:
        name = C.create_unicode_buffer(256)
        needed = W.DWORD()
        if GetUserObjectInformation(desktop, 2, C.cast(name, C.c_void_p), C.sizeof(name), C.byref(needed)):
            return name.value.lower() != "default"
        return False
    finally:
        CloseDesktop(desktop)


def fullscreen_on(rect, *, ignore_maximized=False) -> bool:
    hwnd = GetForegroundWindow()
    if not hwnd or (ignore_maximized and IsZoomed(hwnd)):
        return False
    pid = W.DWORD()
    GetWindowThreadProcessId(hwnd, C.byref(pid))
    if pid.value == os.getpid():
        return False
    classname = C.create_unicode_buffer(128)
    GetClassName(hwnd, classname, len(classname))
    if classname.value in {"Progman", "WorkerW", "Shell_TrayWnd", "Shell_SecondaryTrayWnd"}:
        return False
    bounds = W.RECT()
    if DwmGetWindowAttribute(hwnd, 9, C.byref(bounds), C.sizeof(bounds)) != 0:
        if not GetWindowRect(hwnd, C.byref(bounds)):
            return False
    x, y, width, height = rect
    return bounds.left <= x+2 and bounds.top <= y+2 and bounds.right >= x+width-2 and bounds.bottom >= y+height-2


class MEMORY(C.Structure):
    _fields_ = [("cb", W.DWORD), ("PageFaultCount", W.DWORD), ("PeakWorkingSetSize", C.c_size_t),
                ("WorkingSetSize", C.c_size_t), ("QuotaPeakPagedPoolUsage", C.c_size_t),
                ("QuotaPagedPoolUsage", C.c_size_t), ("QuotaPeakNonPagedPoolUsage", C.c_size_t),
                ("QuotaNonPagedPoolUsage", C.c_size_t), ("PagefileUsage", C.c_size_t), ("PeakPagefileUsage", C.c_size_t)]


GetProcessMemoryInfo = bind(psapi, "GetProcessMemoryInfo", W.BOOL, W.HANDLE, C.POINTER(MEMORY), W.DWORD)


def memory_mb() -> float:
    info = MEMORY()
    info.cb = C.sizeof(info)
    return info.WorkingSetSize / 1048576 if GetProcessMemoryInfo(GetCurrentProcess(), C.byref(info), info.cb) else 0.0
