"""Localhost + synthetic Inno installation. Never replaces the real application."""
import argparse
from hashlib import sha256
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
from threading import Thread
from time import monotonic, sleep
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))


def parent_session(folder, base, helper):
    import ctypes
    sys.path.insert(0, str(ROOT))
    os.environ["QT_QPA_PLATFORM"] = "offscreen"
    os.environ["QINGKUAI_UPDATE_PARENT_PID"] = str(os.getpid())
    from unittest.mock import patch
    from PySide6.QtCore import QTimer, QUrl
    from PySide6.QtNetwork import QNetworkAccessManager, QNetworkRequest
    from PySide6.QtWidgets import QApplication
    from still.app import Controller
    from still.model import Store

    class Network(QNetworkAccessManager):
        def createRequest(self, operation, request, outgoing=None):
            request = QNetworkRequest(request)
            if "format=json" in request.url().toString():
                suffix = "/metadata"
            elif request.url().path().endswith("Setup-x64.exe"):
                suffix = "/setup"
            else:
                raise AssertionError("Unexpected external request")
            request.setUrl(QUrl(base + suffix))
            return super().createRequest(operation, request, outgoing)

    app = QApplication([])
    app.setQuitOnLastWindowClosed(False)
    profile = folder / "用户配置 空格"
    store = Store(profile)
    store.settings.capture_hotkey = store.settings.restore_on_start = False
    store.settings.settings_language = "en"
    store.save()
    controller = Controller(app, store, check_updates=False)
    updater = controller.updater()
    updater.network = Network(updater)
    updater.download.network = updater.network
    updater.download.directory = folder / "cache"
    trace = []
    started = False

    def record():
        nonlocal started
        trace.append({"state": updater.state, "button": controller.window.update_button.text(), "source": updater.source})
        if updater.state == "update" and not started:
            started = True
            controller.window.update_button.click()

    updater.changed.connect(record)
    updater.failed.connect(lambda _message: app.quit())
    app.aboutToQuit.connect(lambda: (folder / "trace.json").write_text(json.dumps(trace), encoding="utf-8"))
    QTimer.singleShot(30000, app.quit)
    # Simulate the frozen bootloader's inherited nonstandard DLL directory.
    ctypes.windll.kernel32.SetDllDirectoryW(str(folder))
    with (patch("still.update_installer.launcher_library", return_value=helper),
          patch("still.update_installer.installed_directory", return_value=folder / "installed app 空格"),
          patch("still.update_installer.sys.frozen", True, create=True)):
        QTimer.singleShot(0, lambda: updater.check(force=True))
        app.exec()
    if controller.window is not None:
        controller.window.close()
    return 0 if trace and trace[-1]["state"] == "installing" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iscc", type=Path)
    parser.add_argument("--parent", nargs=3)
    args = parser.parse_args()
    if args.parent:
        return parent_session(Path(args.parent[0]), args.parent[1], Path(args.parent[2]))
    if args.iscc is None or not args.iscc.is_file():
        raise SystemExit("Pass --iscc with the Inno Setup compiler path.")
    helper = subprocess.check_output(["pwsh", "-NoProfile", "-File", str(ROOT / "tools/build_update_launcher.ps1")], text=True).strip()
    compiler = ROOT / "build/native-toolchain/Compiler/bin/clang++.exe"
    workspace = ROOT / "build/update-flow-qa"
    workspace.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(prefix="isolated-", dir=workspace) as temporary:
        folder = Path(temporary).resolve()
        assert folder.is_relative_to(workspace.resolve())
        app_dir = folder / "installed app 空格"
        app_dir.mkdir()
        (app_dir / "data").mkdir()
        preserved = app_dir / "data/user-kept.txt"
        preserved.write_text("preserve this user data", encoding="utf-8")
        fixture = folder / "轻块桌面.exe"
        subprocess.run([str(compiler), "--target=x86_64-w64-mingw32", "-std=c++23", "-DUNICODE", "-D_UNICODE",
                        "-municode", "-static", "-Os", "-s", str(ROOT / "native/tests/update_fixture.cpp"),
                        "-o", str(fixture)], check=True)
        test_id = "QingkuaiUpdateQA-" + uuid.uuid4().hex
        script = folder / "fixture.iss"
        script.write_text(f'''[Setup]
AppId={test_id}
AppName=Qingkuai isolated update fixture
AppVersion=9.8.7
PrivilegesRequired=lowest
DefaultDirName={app_dir}
OutputDir={folder}
OutputBaseFilename=FixtureSetup
DisableProgramGroupPage=yes
CloseApplications=no
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
[Files]
Source: "{fixture}"; DestDir: "{{app}}"; Flags: ignoreversion
''', encoding="utf-8-sig")
        subprocess.run([str(args.iscc.resolve()), "/Qp", str(script)], check=True, stdout=subprocess.DEVNULL)
        installer = folder / "FixtureSetup.exe"
        binary = installer.read_bytes()
        digest = sha256(binary).hexdigest()
        from still.update_installer import close_handle, create_event, wait_for
        event_name = "Local\\Qingkuai.Update.QA." + uuid.uuid4().hex
        event = create_event(None, True, False, event_name)
        assert event
        try:
            rejected = subprocess.run([helper, "--parent", str(os.getpid()), "--setup", str(installer),
                "--sha256", "0" * 64, "--directory", str(app_dir), "--profile", "", "--language", "english",
                "--mode", "installed", "--ready", event_name], timeout=10)
            assert rejected.returncode == 4 and wait_for(event, 0) == 258
            print("PASS native launcher rejects a tampered installer before requesting app exit", flush=True)
        finally:
            close_handle(event)
        requests = []

        class Server(BaseHTTPRequestHandler):
            def do_GET(self):
                requests.append(self.path)
                if self.path == "/metadata":
                    data = json.dumps(dict(version="v9.8.7", name="Qingkuai-Setup-x64.exe", sha256=digest,
                        size=len(binary), url="https://github.com/GalokPeng/light-block-desktop/releases/download/v9.8.7/Qingkuai-Setup-x64.exe")).encode()
                else:
                    # First source returns a same-size but corrupted executable.
                    data = binary if requests.count("/setup") > 1 else binary[:100] + bytes([binary[100] ^ 255]) + binary[101:]
                self.send_response(200)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                try:
                    for start in range(0, len(data), 64 * 1024):
                        self.wfile.write(data[start:start + 64 * 1024])
                        self.wfile.flush()
                        sleep(.005)
                except (BrokenPipeError, ConnectionResetError):
                    pass

            def log_message(self, *_):
                pass

        server = ThreadingHTTPServer(("127.0.0.1", 0), Server)
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        marker = folder / "restarted.txt"
        env = dict(os.environ, QINGKUAI_UPDATE_FIXTURE_LOG=str(marker), PYTHONUTF8="1")
        try:
            child = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--parent", str(folder),
                                    f"http://127.0.0.1:{server.server_port}", helper], env=env, timeout=40,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8")
            if child.returncode:
                raise RuntimeError(child.stdout)
            end = monotonic() + 30
            while not marker.is_file() and monotonic() < end:
                sleep(.1)
            if not marker.is_file():
                raise RuntimeError("Setup did not finish and restart the synthetic app.")
            result = marker.read_text(encoding="utf-8")
            assert result.startswith("parent-exited\n"), result
            assert "independent-environment\n" in result and "clean-dll-search\n" in result, result
            assert str(app_dir / "轻块桌面.exe") in result, result
            assert "--data-dir\n" + str(folder / "用户配置 空格") in result, result
            assert preserved.read_text(encoding="utf-8") == "preserve this user data"
            trace = json.loads((folder / "trace.json").read_text(encoding="utf-8"))
            percentages = [int(item["button"].removeprefix("Downloading ").removesuffix("%"))
                           for item in trace if item["button"].startswith("Downloading ") and item["button"].endswith("%")]
            assert any(0 < value < 100 for value in percentages) and 100 in percentages, trace
            assert {"verifying","preparing","installing"}.issubset({item["state"] for item in trace}), trace
            assert trace[-1]["state"] == "installing", trace
            assert requests.count("/setup") == 2, requests
            print("PASS button progress, checksum rejection and source failover, graceful process exit, real silent Setup, preserved data and restart", flush=True)
        finally:
            server.shutdown()
            server.server_close()
            uninstall = next(app_dir.glob("unins*.exe"), None)
            if uninstall:
                assert uninstall.resolve().is_relative_to(folder)
                subprocess.run([str(uninstall), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"], check=True, timeout=30)
            # Let the detached helper finish closing its own cache executable.
            sleep(.3)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
