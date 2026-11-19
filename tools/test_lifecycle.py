"""Windows lifecycle regression, using only fresh, system-inert preview profiles.

No reboot, logoff, Explorer takeover, real startup registration, or screenshots.
WM_ENDSESSION is sent only to the child fixture, never broadcast to Windows.
"""
from __future__ import annotations

import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import subprocess
import time
import winreg

user = c.WinDLL("user32", use_last_error=True)
kernel = c.WinDLL("kernel32", use_last_error=True)
CALLBACK = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
user.EnumWindows.argtypes = [CALLBACK, w.LPARAM]
user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
user.IsWindowVisible.argtypes = [w.HWND]
user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
user.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t)]
user.SendMessageTimeoutW.restype = c.c_ssize_t
user.GetPropW.argtypes = [w.HWND, w.LPCWSTR]
user.GetPropW.restype = w.HANDLE


class ProcessEntry(c.Structure):
    _fields_ = [("size", w.DWORD), ("usage", w.DWORD), ("pid", w.DWORD),
                ("heap", c.c_size_t), ("module", w.DWORD), ("threads", w.DWORD),
                ("parent", w.DWORD), ("priority", w.LONG), ("flags", w.DWORD),
                ("name", w.WCHAR * 260)]


kernel.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
kernel.CreateToolhelp32Snapshot.restype = w.HANDLE
kernel.Process32FirstW.argtypes = [w.HANDLE, c.POINTER(ProcessEntry)]
kernel.Process32NextW.argtypes = [w.HANDLE, c.POINTER(ProcessEntry)]
kernel.CloseHandle.argtypes = [w.HANDLE]


def processes():
    handle = kernel.CreateToolhelp32Snapshot(2, 0)
    if handle == c.c_void_p(-1).value:
        raise c.WinError(c.get_last_error())
    try:
        item = ProcessEntry()
        item.size = c.sizeof(item)
        result = {}
        ok = kernel.Process32FirstW(handle, c.byref(item))
        while ok:
            result[item.pid] = (item.parent, item.name)
            ok = kernel.Process32NextW(handle, c.byref(item))
        return result
    finally:
        kernel.CloseHandle(handle)


def descendants(pid):
    entries = processes()
    found = {pid}
    while True:
        new = {p for p, (parent, _) in entries.items() if parent in found}
        if new.issubset(found):
            return sorted((found - {pid}) & entries.keys())
        found |= new


def windows(pid):
    result = []

    @CALLBACK
    def collect(hwnd, _):
        owner = w.DWORD()
        user.GetWindowThreadProcessId(hwnd, c.byref(owner))
        if owner.value == pid:
            name = c.create_unicode_buffer(256)
            user.GetClassNameW(hwnd, name, len(name))
            result.append((hwnd, name.value, bool(user.IsWindowVisible(hwnd))))
        return True

    user.EnumWindows(collect, 0)
    return result


def wait_until(predicate, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(.025)
    raise AssertionError("Lifecycle condition timed out")


def startup_snapshot():
    values = {}
    for key in (r"Software\Microsoft\Windows\CurrentVersion\Run",
                r"Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run"):
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key) as opened:
                for name in ("StillWallpaper", "LightBlockDesktopPreview"):
                    try:
                        values[(key, name)] = winreg.QueryValueEx(opened, name)
                    except FileNotFoundError:
                        pass
        except FileNotFoundError:
            pass
    return values


def run(executable, root, scenarios):
    before = startup_snapshot()
    results = []
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    for backend in ("qt", "native"):
        for scenario in scenarios:
            profile = root / (backend + "-" + scenario)
            assert not profile.exists(), "Fresh test profile required"
            flags = ["--legacy-ui"] if backend == "native" else []
            launch_time = time.monotonic()
            proc = subprocess.Popen([str(executable), "--lifecycle-fixture", "--background", "--data-dir", str(profile), *flags], startupinfo=startup)
            controller = None
            seen_children = set()
            try:
                # The HWND is published by CreateWindow before main sets its fixture
                # property. Wait for both; never send messages to an untagged HWND.
                controller = wait_until(lambda: next((h for h, name, _ in windows(proc.pid) if name == "LightBlockDesktopNative.Main.v1" and user.GetPropW(h, "LB.LifecycleFixture")), None))
                assert user.GetPropW(controller, "LB.LifecycleFixture"), "Refusing to test a non-fixture window"
                log = profile / "native.log"
                # The constructor creates the HWND before the Qt library is ready.
                def initialized():
                    seen_children.update(descendants(proc.pid))
                    logs = list(profile.glob("*.log"))
                    return any("app start C++23" in p.read_text(encoding="utf-8", errors="replace") for p in logs)
                wait_until(initialized, 30)
                startup_ms = round((time.monotonic() - launch_time) * 1000)
                assert not any(visible for _, _, visible in windows(proc.pid)), "Background launch exposed UI"
                duplicate = subprocess.run([str(executable), "--background", "--data-dir", str(profile)], startupinfo=startup, timeout=10)
                assert duplicate.returncode == 0
                time.sleep(.2)
                assert not any(visible for _, _, visible in windows(proc.pid)), "Duplicate autostart opened settings"
                duplicate = subprocess.run([str(executable), "--data-dir", str(profile)], startupinfo=startup, timeout=10)
                assert duplicate.returncode == 0
                visible = wait_until(lambda: next((h for h, name, shown in windows(proc.pid) if shown and (name == "LightBlockDesktopNative.Main.v1" or name.startswith("Qt"))), None))
                result = c.c_size_t()
                assert user.SendMessageTimeoutW(visible, 0x11, 0, 0, 2, 4000, c.byref(result)) and result.value == 1, "Session query rejected"
                user.SendMessageTimeoutW(visible, 0x16, 0, 0, 2, 4000, c.byref(result))
                assert proc.poll() is None and user.IsWindowVisible(visible), "Cancelled logoff closed the app"
                assert user.PostMessageW(visible, 0x10, 0, 0)
                wait_until(lambda: not any(shown for _, _, shown in windows(proc.pid)))
                assert proc.poll() is None, "Ordinary close must keep background services alive"
                # Test exit after a second UI lifetime as well (Qt releases hidden UI).
                user.PostMessageW(controller, 0x111, 901, 0)
                visible = wait_until(lambda: next((h for h, name, shown in windows(proc.pid) if shown and (name == "LightBlockDesktopNative.Main.v1" or name.startswith("Qt"))), None))
                seen_children.update(descendants(proc.pid))
                began = time.monotonic()
                if scenario == "session-end":
                    assert user.SendMessageTimeoutW(visible, 0x11, 0, 0, 2, 4000, c.byref(result)) and result.value == 1
                    user.SendMessageTimeoutW(visible, 0x16, 1, 0, 2, 4500, c.byref(result))
                else:
                    if scenario == "capture-then-exit":
                        user.PostMessageW(controller, 0x111, 701, 0)
                    user.PostMessageW(controller, 0x111, 900, 0)
                    user.PostMessageW(controller, 0x111, 900, 0)
                code = proc.wait(timeout=10)
                elapsed = round((time.monotonic() - began) * 1000)
                assert code == 0, f"Exit returned {code}"
                wait_until(lambda: not descendants(proc.pid) and not (seen_children & processes().keys()), 5)
                assert (profile / "settings.ini").is_file()
                results.append(dict(backend=backend, scenario=scenario, startup_ms=startup_ms, exit_ms=elapsed, child_cleanup=True,
                                    background_quiet=True, duplicate_background_quiet=True, manual_reopen=True,
                                    close_to_tray=True, cancelled_session_preserved=True, settings_saved=True))
                print(f"{backend}/{scenario}: exited in {elapsed} ms; child processes gone", flush=True)
            finally:
                if proc.poll() is None:
                    if controller:
                        user.PostMessageW(controller, 0x111, 900, 0)
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait()
    assert before == startup_snapshot(), "Test changed actual startup configuration"
    return dict(passed=True, cases=results, user_startup_unchanged=True,
                scope="Isolated child windows; simulated session messages; no real reboot/logoff, desktop takeover or installation.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scenario", action="append", choices=("tray-exit", "capture-then-exit", "session-end"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    report = run(args.exe.resolve(), args.output.resolve(), args.scenario or ("tray-exit", "capture-then-exit", "session-end"))
    (args.output / "lifecycle.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
