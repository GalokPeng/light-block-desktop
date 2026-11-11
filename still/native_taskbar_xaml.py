"""Manage the Windows 11 XAML taskbar component from the recovery helper."""
from __future__ import annotations

import ctypes as C
import json
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
CHANNEL = "Local\\Qingkuai.TaskbarLayout.v3."
open_event = win32.bind(win32.kernel32, "OpenEventW", W.HANDLE, W.DWORD, W.BOOL, W.LPCWSTR)


def component_channel(library):
    """Each native build has its own resident diagnostics subscription."""
    module = C.WinDLL(str(Path(library).resolve()))
    try:
        try:
            get_build = module.QingkuaiLayoutBuild
        except AttributeError:
            return CHANNEL  # Compatibility with the previously bundled component.
        get_build.argtypes, get_build.restype = [], C.c_uint64
        build = get_build()
        if not 0 <= build < 2**48:
            raise OSError("invalid native component build")
        return f"{CHANNEL}{build}."
    finally:
        win32.bind(win32.kernel32, "FreeLibrary", W.BOOL, W.HMODULE)(module._handle)


def stop_resident(prefix, pid):
    """Restore an older build before attaching its replacement; never restart Explorer."""
    handles = []
    try:
        resident = open_event(0x100000, False, f"{prefix}{pid}.Resident")
        if resident:
            handles.append(resident)
        if not resident or wait_for(resident, 0) != 0:
            return
        for kind in ("Stop", "Restored"):
            handle = open_event(0x100002, False, f"{prefix}{pid}.{kind}")
            if not handle:
                raise OSError("previous native component event missing")
            handles.append(handle)
        pending_start = open_event(0x2, False, f"{prefix}{pid}.Start")
        if pending_start:
            handles.append(pending_start)
            reset_event(pending_start)
        reset_event(handles[2])
        set_event(handles[1])
        if wait_for(handles[2], 2300) != 0:
            raise OSError("previous native component has not restored")
    finally:
        for handle in handles:
            close_handle(handle)
create_mapping = win32.bind(win32.kernel32, "CreateFileMappingW", W.HANDLE, W.HANDLE,
                            C.POINTER(Security), W.DWORD, W.DWORD, W.DWORD, W.LPCWSTR)
map_view = win32.bind(win32.kernel32, "MapViewOfFile", C.c_void_p, W.HANDLE, W.DWORD,
                     W.DWORD, W.DWORD, C.c_size_t)
unmap_view = win32.bind(win32.kernel32, "UnmapViewOfFile", W.BOOL, C.c_void_p)


class LayoutStatus(C.Structure):
    # Keep aligned with native/taskbar_layout.cpp. A sequence lock avoids torn reads.
    _fields_ = [(name, C.c_int32) for name in ("version", "sequence", "state", "repairs", "rebuilds", "error", "changes", "reserved")] + [
        (name, C.c_double) for name in ("host_width", "frame_width", "tray_width", "group_left", "group_right", "scale")]

    @classmethod
    def read(cls, address):
        for _ in range(3):
            before = cls.from_address(address).sequence
            if before & 1:
                continue
            snapshot = cls.from_buffer_copy(C.string_at(address, C.sizeof(cls)))
            if snapshot.sequence != before or cls.from_address(address).sequence != before:
                continue
            if snapshot.version == 3:
                result = {name: (round(getattr(snapshot, name), 2) if kind is C.c_double else getattr(snapshot, name))
                          for name, kind in cls._fields_ if name not in ("version", "sequence", "reserved")}
                if snapshot.reserved:
                    result.update(controls_gutter_left=(snapshot.reserved & 0xffff)/100,
                                  controls_gutter_right=((snapshot.reserved >> 16) & 0xffff)/100)
                return result
        return None


class NativeTaskbarLayout:
    def __init__(self, library):
        self.library = library
        self.pid = None
        self.events = {}
        self.mapping = self.view = None
        self.channel = CHANNEL
        self.visibility = TaskbarVisibilityLease()
        system_xaml = C.WinDLL("Windows.UI.Xaml.dll", winmode=0x800)
        self.attach = win32.bind(system_xaml, "InitializeXamlDiagnosticsEx", C.c_int32,
                                 W.LPCWSTR, W.DWORD, W.LPCWSTR, W.LPCWSTR, GUID, W.LPCWSTR)

    def make_events(self, pid):
        self.channel = component_channel(self.library)
        # The helper owns the global taskbar mutex at this point. A stable
        # record also allows recovery if the preceding helper crashed.
        cache = Path(os.environ["LOCALAPPDATA"]) / "StillWallpaper" / "native-runtime"
        session = cache / "layout-session.json"
        prefixes = {CHANNEL, self.channel}
        try:
            if session.stat().st_size > 4096:
                raise ValueError("oversized component session")
            previous = json.loads(session.read_text(encoding="utf-8"))
            prefix = previous.get("channel", "")
            if previous.get("pid") == pid and isinstance(prefix, str) and re.fullmatch(re.escape(CHANNEL)+r"(?:\d{1,15}\.)?", prefix):
                prefixes.add(prefix)
        except (OSError, ValueError, TypeError, AttributeError):
            pass
        for prefix in sorted(prefixes):
            stop_resident(prefix, pid)
        descriptor = C.c_void_p()
        # The shell runs at medium integrity even when the preview is elevated.
        if not parse_sd("D:(A;;GA;;;OW)(A;;GA;;;SY)S:(ML;;NW;;;ME)", 1, C.byref(descriptor), None):
            raise OSError("event security")
        try:
            security = Security(C.sizeof(Security), descriptor, False)
            for kind in ("Start", "Stop", "Ready", "Restored", "Resident"):
                name = f"{self.channel}{pid}.{kind}"
                handle = create_event(C.byref(security), kind not in ("Start", "Stop"), False, name)
                if not handle:
                    raise OSError("event creation")
                self.events[kind] = handle
            self.mapping = create_mapping(W.HANDLE(-1), C.byref(security), 4, 0, C.sizeof(LayoutStatus),
                                          f"{self.channel}{pid}.Status")  # PAGE_READWRITE
            if not self.mapping:
                raise OSError("status mapping creation")
            self.view = map_view(self.mapping, 6, 0, 0, C.sizeof(LayoutStatus))  # FILE_MAP_READ | WRITE
            if not self.view:
                raise OSError("status mapping view")
            cache.mkdir(parents=True, exist_ok=True)
            temporary = session.with_name(".layout-session-"+uuid.uuid4().hex+".tmp")
            try:
                temporary.write_text(json.dumps({"pid": pid, "channel": self.channel}), encoding="utf-8")
                os.replace(temporary, session)
            finally:
                temporary.unlink(missing_ok=True)
        finally:
            local_free(descriptor)

    def update(self, hidden=False, revealed=False):
        bar = win32.FindWindow("Shell_TrayWnd", None)
        if not bar or not win32.FindWindowEx(bar, None, "Windows.UI.Composition.DesktopWindowContentBridge", None):
            return {"waiting": True}
        pid = process_id(bar)
        if not pid:
            return {"waiting": True}
        if pid != self.pid:
            self.restore(wait=False)
            self.pid = pid
            try:
                self.make_events(pid)
            except OSError:
                self.restore(wait=False)
                raise
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
        ready = wait_for(self.events["Ready"], 700) == 0
        diagnostic = LayoutStatus.read(self.view)
        if diagnostic and diagnostic["state"] in (3, 4):
            return {"error": "xaml_conflict" if diagnostic["state"] == 4 else "xaml_layout",
                    "diagnostic": diagnostic}
        if not ready:
            return {"waiting": True, "diagnostic": diagnostic}
        auto_hide = auto_hidden()
        monitor = monitor_bounds(bar)
        strip = self.visibility.update(bar, monitor, hidden, revealed, auto_hide, diagnostic)
        return {"auto_hide": auto_hide, "centered": True,
                "diagnostic": diagnostic,
                "visibility": [bar, monitor, strip] if strip is not None else None}

    def restore(self, *, wait=True):
        try:
            if stop := self.events.get("Stop"):
                reset_event(self.events["Start"])
                set_event(stop)
                if wait and (restored := self.events.get("Restored")):
                    wait_for(restored, 2300)
            self.visibility.restore()
        finally:
            if self.view:
                unmap_view(self.view)
                self.view = None
            if self.mapping:
                close_handle(self.mapping)
                self.mapping = None
            for handle in self.events.values():
                close_handle(handle)
            self.events.clear()
            self.pid = None
