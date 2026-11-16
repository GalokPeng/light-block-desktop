"""C++ update UI plus isolated real Setup/helper regression; no PySide runtime."""
import argparse
import ctypes
from hashlib import sha256
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
from time import monotonic, sleep
import uuid

ROOT = Path(__file__).resolve().parents[1]
KERNEL = ctypes.WinDLL("kernel32", use_last_error=True)
KERNEL.CreateEventW.argtypes = (ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_wchar_p)
KERNEL.CreateEventW.restype = ctypes.c_void_p
KERNEL.WaitForSingleObject.argtypes = (ctypes.c_void_p, ctypes.c_ulong)
KERNEL.WaitForSingleObject.restype = ctypes.c_ulong
KERNEL.CloseHandle.argtypes = (ctypes.c_void_p,)
KERNEL.SetDllDirectoryW.argtypes = (ctypes.c_wchar_p,)


def parent_session(folder, helper, setup, digest):
    event_name = "Local\\Qingkuai.Update.QA." + uuid.uuid4().hex
    event = KERNEL.CreateEventW(None, True, False, event_name)
    if not event:
        raise ctypes.WinError(ctypes.get_last_error())
    env = dict(os.environ, QINGKUAI_UPDATE_PARENT_PID=str(os.getpid()))
    KERNEL.SetDllDirectoryW(str(folder))
    try:
        command = (str(helper), "--parent", str(os.getpid()), "--setup", str(setup), "--sha256", digest,
                   "--directory", str(folder / "installed app 空格"), "--profile", str(folder / "用户配置 空格"),
                   "--language", "english", "--mode", "installed", "--ready", event_name)
        subprocess.Popen(command, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
        if KERNEL.WaitForSingleObject(event, 15000) != 0:
            raise RuntimeError("Update helper did not verify Setup and signal readiness")
        (folder / "parent-ready.txt").write_text("verified-before-exit", encoding="utf-8")
    finally:
        KERNEL.CloseHandle(event)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iscc", type=Path)
    parser.add_argument("--parent", nargs=4)
    parser.add_argument("--helper-only", action="store_true", help="Local diagnostic only; does not satisfy the full release gate")
    args = parser.parse_args()
    if args.parent:
        folder, helper, setup, digest = args.parent
        return parent_session(Path(folder), Path(helper), Path(setup), digest)
    if not args.iscc or not args.iscc.is_file():
        raise SystemExit("Pass --iscc pointing to Inno Setup 6")
    runtime = ROOT / "dist/LightBlockDesktop"
    exe = runtime / "轻块桌面.exe"
    helper = runtime / "runtime/QingkuaiUpdate.exe"
    compiler = ROOT / "build/native-toolchain/Compiler/bin/clang++.exe"
    workspace = ROOT / "build/update-flow-qa"
    workspace.mkdir(parents=True, exist_ok=True)
    ui_report = None
    if not args.helper_only:
        ui_report = workspace / ("cpp-ui-" + uuid.uuid4().hex)
        ui_report.mkdir()
        subprocess.run((str(exe), "--update-flow-fixture", str(ui_report), "--data-dir", str(ui_report / "profile")),
                       check=True, timeout=120)
    with TemporaryDirectory(prefix="isolated-", dir=workspace) as temporary:
        folder = Path(temporary).resolve()
        if not folder.is_relative_to(workspace.resolve()):
            raise ValueError("Unexpected test directory")
        app = folder / "installed app 空格"
        (app / "data/profile-v2").mkdir(parents=True)
        preserved = app / "data/profile-v2/user-kept.txt"
        preserved.write_text("preserve user data", encoding="utf-8")
        fixture = folder / "轻块桌面.exe"
        subprocess.run((str(compiler), "--target=x86_64-w64-mingw32", "-std=c++23", "-DUNICODE", "-D_UNICODE", "-municode", "-static", "-Os", "-s",
                        str(ROOT / "native/tests/update_fixture.cpp"), "-o", str(fixture)), check=True)
        script = folder / "fixture.iss"
        script.write_text(f'''[Setup]
AppId=QingkuaiUpdateQA-{uuid.uuid4().hex}
AppName=Qingkuai isolated update fixture
AppVersion=9.8.7
PrivilegesRequired=lowest
DefaultDirName={app}
OutputDir={folder}
OutputBaseFilename=FixtureSetup
DisableProgramGroupPage=yes
CloseApplications=no
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
[Files]
Source: "{fixture}"; DestDir: "{{app}}"; Flags: ignoreversion
''', encoding="utf-8-sig")
        subprocess.run((str(args.iscc.resolve()), "/Qp", str(script)), check=True)
        setup = folder / "FixtureSetup.exe"
        digest = sha256(setup.read_bytes()).hexdigest()
        event_name = "Local\\Qingkuai.Update.QA." + uuid.uuid4().hex
        event = KERNEL.CreateEventW(None, True, False, event_name)
        if not event:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            rejected = subprocess.run((str(helper), "--parent", str(os.getpid()), "--setup", str(setup), "--sha256", "0" * 64,
                "--directory", str(app), "--profile", "", "--language", "english", "--mode", "installed", "--ready", event_name), timeout=15)
            if rejected.returncode != 4 or KERNEL.WaitForSingleObject(event, 0) != 258:
                raise RuntimeError("Tampered Setup was accepted or readiness was signaled")
        finally:
            KERNEL.CloseHandle(event)
        marker = folder / "restarted.txt"
        env = dict(os.environ, QINGKUAI_UPDATE_FIXTURE_LOG=str(marker), PYTHONUTF8="1")
        try:
            subprocess.run((sys.executable, str(Path(__file__).resolve()), "--parent", str(folder), str(helper), str(setup), digest), env=env, check=True, timeout=30)
            deadline = monotonic() + 60
            while not marker.is_file() and monotonic() < deadline:
                sleep(.1)
            if not marker.is_file():
                raise RuntimeError("The isolated Setup did not restart the synthetic app")
            content = marker.read_text(encoding="utf-8")
            for value in ("parent-exited\n", "independent-environment\n", "clean-dll-search\n", str(app / "轻块桌面.exe"), "--data-dir\n" + str(folder / "用户配置 空格")):
                if value not in content:
                    raise RuntimeError(f"Missing helper guarantee: {value}")
            if preserved.read_text(encoding="utf-8") != "preserve user data":
                raise RuntimeError("Setup changed the user's data")
            report = dict(passed=not args.helper_only, helper_passed=True, cpp_ui_fixture=str(ui_report) if ui_report else None, bad_hash_rejected=True, parent_exited_before_setup=True,
                          isolated_real_setup=True, independent_restart=True, unicode_profile_preserved=True, user_data_preserved=True)
            report_name = "helper-only-summary.json" if args.helper_only else "summary.json"
            (workspace / report_name).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
            print("PASS isolated helper guarantees; full UI gate still pending" if args.helper_only else "PASS stable update policy, tamper rejection, graceful exit, isolated Setup, data preservation and restart", flush=True)
        finally:
            uninstaller = next(app.glob("unins*.exe"), None)
            if uninstaller:
                subprocess.run((str(uninstaller), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"), check=True, timeout=60)
            sleep(.3)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
