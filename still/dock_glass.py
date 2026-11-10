"""Optional compositor material for the Dock and the native taskbar."""
import ctypes as C
from ctypes import wintypes as W
import winreg

from . import win32


class Accent(C.Structure):
    _fields_ = [("state", C.c_int), ("flags", C.c_int), ("color", W.DWORD), ("animation", C.c_int)]


class Attribute(C.Structure):
    _fields_ = [("attribute", C.c_int), ("value", C.c_void_p), ("size", C.c_size_t)]


def enabled_by_system():
    class Contrast(C.Structure):
        _fields_ = [("size", W.UINT), ("flags", W.DWORD), ("scheme", W.LPWSTR)]

    contrast = Contrast(C.sizeof(Contrast), 0, None)
    if win32.SystemParametersInfo(0x0042, contrast.size, C.byref(contrast), 0) and contrast.flags & 1:
        return False
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize") as key:
            return bool(winreg.QueryValueEx(key, "EnableTransparency")[0])
    except OSError:
        return True


def accent_attribute(hwnd, policy, write):
    name = "SetWindowCompositionAttribute" if write else "GetWindowCompositionAttribute"
    if not hasattr(win32.user32, name):
        return False
    call = win32.bind(win32.user32, name, W.BOOL, W.HWND, C.POINTER(Attribute))
    attribute = Attribute(19, C.cast(C.byref(policy), C.c_void_p), C.sizeof(policy))  # WCA_ACCENT_POLICY
    return bool(call(hwnd, C.byref(attribute)))


def read_accent(hwnd):
    policy = Accent()
    if accent_attribute(hwnd, policy, False):
        return policy.state, policy.flags, policy.color, policy.animation
    return None


def write_accent(hwnd, values):
    return accent_attribute(hwnd, Accent(*values), True)


def read_backdrop(hwnd):
    value = C.c_int()
    if win32.DwmGetWindowAttribute(hwnd, 38, C.byref(value), C.sizeof(value)) == 0:
        return value.value
    return None


def write_backdrop(hwnd, mode):
    value = C.c_int(mode)
    set_attribute = win32.bind(win32.dwmapi, "DwmSetWindowAttribute", C.c_long, W.HWND,
                               W.DWORD, C.c_void_p, W.DWORD)
    return set_attribute(hwnd, 38, C.byref(value), C.sizeof(value)) == 0


def acrylic(rgb, opacity):
    # ACCENT_POLICY uses ABGR. The custom paint adds the remaining tint.
    abgr = ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | (rgb >> 16)
    return 4, 0, (max(1, round(opacity * 1.15)) << 24) | abgr, 0
