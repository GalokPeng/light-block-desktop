"""Prepare a small detached native launcher before the application exits."""
import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from time import monotonic
import uuid
import winreg

from PySide6.QtCore import QObject, QTimer, Signal

from . import win32
from .model import ROOT

UNINSTALL_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\{3CD16B31-077E-4CE9-BB01-578642D94876}_is1"
create_event = win32.bind(win32.kernel32, "CreateEventW", W.HANDLE, C.c_void_p, W.BOOL, W.BOOL, W.LPCWSTR)
wait_for = win32.bind(win32.kernel32, "WaitForSingleObject", W.DWORD, W.HANDLE, W.DWORD)
close_handle = win32.bind(win32.kernel32, "CloseHandle", W.BOOL, W.HANDLE)


def installed_directory(executable):
    """Keep a portable copy from taking over another registered installation."""
    executable = Path(executable).resolve()
    registered = False
    for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
        for flags in (winreg.KEY_WOW64_64KEY, winreg.KEY_WOW64_32KEY):
            try:
                with winreg.OpenKey(hive, UNINSTALL_KEY, access=winreg.KEY_READ | flags) as key:
                    try:
                        location = winreg.QueryValueEx(key, "InstallLocation")[0]
                    except FileNotFoundError:
                        location = winreg.QueryValueEx(key, "Inno Setup: App Path")[0]
                if not isinstance(location, str) or not Path(location).is_absolute():
                    continue
                directory = Path(location)
                registered = True
                if directory.resolve() == executable.parent:
                    return directory
            except (OSError, ValueError):
                continue
    if registered or executable.name.casefold() != "轻块桌面.exe".casefold():
        return None
    # Older installs can retain their uninstall record after a registry entry
    # is lost. Require our AppId in an Inno log plus its matching uninstaller.
    for record in executable.parent.glob("unins*.dat"):
        try:
            if not record.with_suffix(".exe").is_file():
                continue
            with record.open("rb") as file:
                header = file.read(8192)
            if (header.startswith(b"Inno Setup Uninstall Log") and
                    b"{3CD16B31-077E-4CE9-BB01-578642D94876}" in header):
                return executable.parent
        except OSError:
            continue
    return None


def launcher_library():
    if getattr(sys, "frozen", False):
        path = ROOT / "runtime" / "QingkuaiUpdate.exe"
    else:
        directory = ROOT / "build" / "update-launcher"
        name = (directory / "current.txt").read_text(encoding="ascii").strip()
        if not re.fullmatch(r"QingkuaiUpdate-[0-9a-f]{12}\.exe", name):
            raise OSError("Invalid update launcher")
        path = directory / name
    if not path.is_file():
        raise OSError("Missing update launcher")
    return path


class UpdateInstaller(QObject):
    prepared = Signal()
    failed = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.process = self.ready_event = None
        self.committed = False
        self.timer = QTimer(self, interval=50)
        self.timer.timeout.connect(self.poll)

    def prepare(self, setup, digest, profile, language):
        self.cancel()
        self.committed = False
        try:
            setup, profile = Path(setup).resolve(), Path(profile).resolve()
            helper = setup.parent / ("QingkuaiUpdate-" + uuid.uuid4().hex + ".exe")
            shutil.copyfile(launcher_library(), helper)
            event_name = "Local\\Qingkuai.Update." + uuid.uuid4().hex
            self.ready_event = create_event(None, True, False, event_name)
            if not self.ready_event:
                raise OSError("Cannot create update event")
            directory = installed_directory(sys.executable) if getattr(sys, "frozen", False) else None
            forwarded = "" if directory and profile == directory.resolve() / "data" else str(profile)
            command = [str(helper), "--parent", str(os.getpid()), "--setup", str(setup),
                       "--sha256", digest, "--directory", str(directory) if directory else "",
                       "--profile", forwarded, "--language", "chinesesimplified" if language == "zh" else "english",
                       "--mode", "installed" if directory else "interactive", "--ready", event_name]
            self.process = subprocess.Popen(command, creationflags=subprocess.CREATE_NO_WINDOW)
            self.started = monotonic()
            self.timer.start()
        except (OSError, ValueError):
            self.cancel()
            self.failed.emit("prepare")

    def poll(self):
        if self.process is None or self.process.poll() is not None:
            self.cancel()
            self.failed.emit("prepare")
        elif self.ready_event and wait_for(self.ready_event, 0) == 0:
            self.timer.stop()
            close_handle(self.ready_event)
            self.ready_event = None
            self.committed = True
            self.prepared.emit()
        elif monotonic() - self.started > 15:
            self.cancel()
            self.failed.emit("prepare")

    def cancel(self):
        self.timer.stop()
        if not self.committed and self.process is not None and self.process.poll() is None:
            try:
                self.process.terminate()
            except OSError:
                pass
        if self.ready_event:
            close_handle(self.ready_event)
            self.ready_event = None
