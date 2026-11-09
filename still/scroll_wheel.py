"""Short-lived observers for Windows pointer events."""
import ctypes as C
from ctypes import wintypes as W

from . import win32


class MouseEvent(C.Structure):
    _fields_ = [("point", W.POINT), ("data", W.DWORD), ("flags", W.DWORD),
                ("time", W.DWORD), ("extra", C.c_size_t)]


HOOKPROC = C.WINFUNCTYPE(C.c_ssize_t, C.c_int, W.WPARAM, W.LPARAM)


class MouseHook:
    def __init__(self, messages, handler):
        self.messages, self.handler = messages, handler
        self.handle = None
        self.callback = HOOKPROC(self._event)
        self.next_hook = win32.bind(win32.user32, "CallNextHookEx", C.c_ssize_t,
                                    W.HANDLE, C.c_int, W.WPARAM, W.LPARAM)

    def start(self):
        if self.handle:
            return True
        install = win32.bind(win32.user32, "SetWindowsHookExW", W.HANDLE, C.c_int, HOOKPROC, W.HMODULE, W.DWORD)
        module = win32.bind(win32.kernel32, "GetModuleHandleW", W.HMODULE, W.LPCWSTR)(None)
        self.handle = install(14, self.callback, module, 0)  # WH_MOUSE_LL
        return bool(self.handle)

    def _event(self, code, message, payload):
        try:
            if code == 0 and message in self.messages:
                self.handler(MouseEvent.from_address(payload))
        except (RuntimeError, OSError):
            # A window can disappear while its hook is still being unwound.
            pass
        return self.next_hook(self.handle, code, message, payload)

    def close(self):
        if self.handle:
            win32.bind(win32.user32, "UnhookWindowsHookEx", W.BOOL, W.HANDLE)(self.handle)
            self.handle = None


class ScrollWheel(MouseHook):
    def __init__(self, region, wheel_down):
        self.region, self.wheel_down = region, wheel_down
        super().__init__({0x020A}, self.scrolled)  # WM_MOUSEWHEEL

    def scrolled(self, event):
        if event.data >> 16 and self.region.contains(event.point.x, event.point.y):
            self.wheel_down()
