"""Manage the Windows 11 XAML taskbar component from the recovery helper."""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import re
import shutil
import sys
from tempfile import NamedTemporaryFile
import uuid

from . import win32
from .model import ROOT
from .native_taskbar import auto_hidden, monitor_bounds
from .taskbar_style_native import close_handle, process_id
from .taskbar_visibility import TaskbarVisibilityLease


class GUID(C.Structure):
    _fields_ = [("data", C.c_ubyte * 16)]


class Security(C.Structure):
    _fields_ = [("size", W.DWORD), ("descriptor", C.c_void_p), ("inherited", W.BOOL)]


def layout_library():
    if sys.getwindowsversion().build < 22621 or not native_x64():
        return None
    if getattr(sys, "frozen", False):
        path = ROOT / "runtime" / "QingkuaiTaskbarLayout.dll"
        return path if path.is_file() else None
    directory = ROOT / "build" / "native-taskbar"
    try:
        name = (directory / "current.txt").read_text(encoding="ascii").strip()
    except (OSError, ValueError):
        return None
    path = directory / name
    if re.fullmatch(r"QingkuaiTaskbarLayout-[0-9a-f]{12}\.dll", name) and path.is_file():
        return path
    return None


def native_x64():
    get_current_process = win32.bind(win32.kernel32, "GetCurrentProcess", W.HANDLE)
    machine_type = win32.bind(win32.kernel32, "IsWow64Process2", W.BOOL, W.HANDLE,
                              C.POINTER(W.USHORT), C.POINTER(W.USHORT))
    process, system = W.USHORT(), W.USHORT()
    return bool(machine_type(get_current_process(), C.byref(process), C.byref(system)) and system.value == 0x8664)


def stage_runtime_library(source):
    # Explorer keeps the diagnostics component mapped for its lifetime.
    # Map a per-user copy so Setup can replace or remove the bundled DLL.
    cache = Path(os.environ["LOCALAPPDATA"]) / "StillWallpaper" / "native-runtime"
    cache.mkdir(parents=True, exist_ok=True)
    for previous in cache.glob("taskbar-*.dll"):
        try:
            previous.unlink()
        except OSError:
            pass  # A live Explorer session can still have it mapped.
    with source.open("rb") as original, NamedTemporaryFile(
        mode="wb", prefix="taskbar-", suffix=".dll", dir=cache, delete=False,
    ) as copy:
        shutil.copyfileobj(original, copy)
        return copy.name


create_event = win32.bind(win32.kernel32, "CreateEventW", W.HANDLE, C.POINTER(Security), W.BOOL, W.BOOL, W.LPCWSTR)
set_event = win32.bind(win32.kernel32, "SetEvent", W.BOOL, W.HANDLE)
reset_event = win32.bind(win32.kernel32, "ResetEvent", W.BOOL, W.HANDLE)
wait_for = win32.bind(win32.kernel32, "WaitForSingleObject", W.DWORD, W.HANDLE, W.DWORD)
local_free = win32.bind(win32.kernel32, "LocalFree", W.HLOCAL, W.HLOCAL)
advapi = C.WinDLL("advapi32", use_last_error=True)
parse_sd = win32.bind(advapi, "ConvertStringSecurityDescriptorToSecurityDescriptorW", W.BOOL,
                     W.LPCWSTR, W.DWORD, C.POINTER(C.c_void_p), C.POINTER(W.DWORD))
layout_id = GUID((C.c_ubyte * 16).from_buffer_copy(uuid.UUID("1024237c-b66a-4f25-ba99-fb65e94e7603").bytes_le))


class NativeTaskbarLayout:
    def __init__(self, library):
        self.library = library
        self.pid = None
        self.events = {}
        self.visibility = TaskbarVisibilityLease()
        system_xaml = C.WinDLL("Windows.UI.Xaml.dll", winmode=0x800)
        self.attach = win32.bind(system_xaml, "InitializeXamlDiagnosticsEx", C.c_int32,
                                 W.LPCWSTR, W.DWORD, W.LPCWSTR, W.LPCWSTR, GUID, W.LPCWSTR)

    def make_events(self, pid):
        descriptor = C.c_void_p()
        # The shell runs at medium integrity even when the preview is elevated.
        if not parse_sd("D:(A;;GA;;;OW)(A;;GA;;;SY)S:(ML;;NW;;;ME)", 1, C.byref(descriptor), None):
            raise OSError("event security")
        try:
            security = Security(C.sizeof(Security), descriptor, False)
            for kind in ("Start", "Stop", "Ready", "Restored", "Resident"):
                name = f"Local\\Qingkuai.TaskbarLayout.{pid}.{kind}"
                handle = create_event(C.byref(security), kind not in ("Start", "Stop"), False, name)
                if not handle:
                    raise OSError("event creation")
                self.events[kind] = handle
        finally:
            local_free(descriptor)

    def update(self, hidden=False, revealed=False):
        bar = win32.FindWindow("Shell_TrayWnd", None)
        if not bar or not win32.FindWindowEx(bar, None, "Windows.UI.Composition.DesktopWindowContentBridge", None):
            return {"waiting": True}
        pid = process_id(bar)
        if not pid:
            return {"waiting": True}
        if self.pid is not None and pid != self.pid:
            self.restore(wait=False)
            return {"error": "xaml_restarted"}
        if pid != self.pid:
            self.restore(wait=False)
            self.pid = pid
            self.make_events(pid)
            if wait_for(self.events["Resident"], 0) != 0:
                module = stage_runtime_library(self.library)
                for index in range(1, 129):
                    code = self.attach(f"VisualDiagConnection{index}", pid, "", module, layout_id, None) & 0xffffffff
                    if code != 0x80070490:
                        break
                if code:
                    self.restore(wait=False)
                    if code in (0x80070490, 0x80070002, 0x80070102):
                        return {"waiting": True}
                    return {"error": "xaml"}
            reset_event(self.events["Stop"])
            reset_event(self.events["Ready"])
            reset_event(self.events["Restored"])
            set_event(self.events["Start"])
        if wait_for(self.events["Ready"], 700) != 0:
            return {"waiting": True}
        auto_hide = auto_hidden()
        monitor = monitor_bounds(bar)
        strip = self.visibility.update(bar, monitor, hidden, revealed, auto_hide)
        return {"auto_hide": auto_hide, "centered": True,
                "visibility": [bar, monitor, strip] if strip is not None else None}

    def restore(self, *, wait=True):
        try:
            if stop := self.events.get("Stop"):
                set_event(stop)
                if wait and (restored := self.events.get("Restored")):
                    wait_for(restored, 2300)
            self.visibility.restore()
        finally:
            for handle in self.events.values():
                close_handle(handle)
            self.events.clear()
            self.pid = None
