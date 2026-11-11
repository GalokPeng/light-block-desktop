from hashlib import sha256
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, Mock, patch

from PySide6.QtCore import QEvent, QObject, Signal
from PySide6.QtNetwork import QNetworkReply, QNetworkRequest
from PySide6.QtWidgets import QApplication

from still.app import Controller
from still.ui import MainWindow
from still.update_download import MAX_SETUP_BYTES, SetupDownload
from still.update_installer import UpdateInstaller, installed_directory
from still.updates import ReleaseCheck, SETUP_NAME, newer_version, release_metadata, setup_sources

PAYLOAD = b"MZ" + bytes(range(256)) * 3000
DIGEST = sha256(PAYLOAD).hexdigest()


class Reply(QObject):
    readyRead = Signal()
    finished = Signal()

    def __init__(self, size=len(PAYLOAD), status=200):
        super().__init__()
        self.data = bytearray()
        self.length, self.status = size, status
        self.done, self.aborted = False, False
        self.code = QNetworkReply.NetworkError.NoError

    def setReadBufferSize(self, _size):
        pass

    def attribute(self, _attribute):
        return self.status

    def rawHeader(self, _name):
        return str(self.length).encode()

    def bytesAvailable(self):
        return len(self.data)

    def read(self, size):
        chunk = bytes(self.data[:size])
        del self.data[:size]
        return chunk

    def feed(self, data):
        self.data.extend(data)
        self.readyRead.emit()

    def error(self):
        return self.code

    def isFinished(self):
        return self.done

    def complete(self):
        self.done = True
        self.finished.emit()

    def abort(self):
        self.aborted = True
        self.code = QNetworkReply.NetworkError.OperationCanceledError
        self.complete()


class Network:
    def __init__(self, *replies):
        self.replies, self.requests = list(replies), []

    def get(self, request):
        self.requests.append(request)
        return self.replies.pop(0)


class UpdateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def test_installer_retains_qobject_event_dispatch_before_preparing_an_update(self):
        installer = UpdateInstaller()
        self.app.sendEvent(installer, QEvent(QEvent.Type.User))
        self.assertIsNone(installer.ready_event)
        installer.cancel()

    def test_terminated_launcher_does_not_request_exit_even_if_its_ready_event_was_set(self):
        installer = UpdateInstaller()
        installer.process = Mock()
        installer.process.poll.return_value = 4
        installer.ready_event = 123
        failed, prepared = [], []
        installer.failed.connect(failed.append)
        installer.prepared.connect(lambda: prepared.append(True))
        with (patch("still.update_installer.wait_for", return_value=0),
              patch("still.update_installer.close_handle")):
            installer.poll()
        self.assertEqual(failed, ["prepare"])
        self.assertFalse(prepared)
        self.assertFalse(installer.committed)

    def test_portable_copy_and_a_verified_inno_install_with_a_missing_registry_key(self):
        with TemporaryDirectory() as folder, patch("still.update_installer.winreg.OpenKey", side_effect=FileNotFoundError):
            directory = Path(folder).resolve()
            app = directory / "轻块桌面.exe"
            self.assertIsNone(installed_directory(app))
            (directory / "unins000.exe").write_bytes(b"MZ")
            record = directory / "unins000.dat"
            record.write_bytes(b"Inno Setup Uninstall Log (b) 64-bit\0{3CD16B31-077E-4CE9-BB01-578642D94876}")
            self.assertEqual(installed_directory(app), directory)
            record.write_bytes(b"Inno Setup Uninstall Log (b) 64-bit\0OTHER-APP")
            self.assertIsNone(installed_directory(app))

    def test_portable_copy_does_not_replace_another_registered_installation(self):
        with TemporaryDirectory() as folder:
            directory = Path(folder).resolve()
            app = directory / "轻块桌面.exe"
            (directory / "unins000.exe").write_bytes(b"MZ")
            (directory / "unins000.dat").write_bytes(b"Inno Setup Uninstall Log\0{3CD16B31-077E-4CE9-BB01-578642D94876}")
            with (patch("still.update_installer.winreg.OpenKey", return_value=MagicMock()),
                  patch("still.update_installer.winreg.QueryValueEx", return_value=(str(directory / "other"), 1))):
                self.assertIsNone(installed_directory(app))

    def test_registered_installation_keeps_its_existing_directory(self):
        with TemporaryDirectory() as folder:
            directory = Path(folder).resolve()
            with (patch("still.update_installer.winreg.OpenKey", return_value=MagicMock()),
                  patch("still.update_installer.winreg.QueryValueEx", return_value=(str(directory), 1))):
                self.assertEqual(installed_directory(directory / "轻块桌面.exe"), directory)

    def test_language_priority_and_metadata_cannot_select_another_executable(self):
        en, zh = setup_sources("v9.8.7", "en"), setup_sources("v9.8.7", "zh")
        self.assertEqual(en[0][0], "GitHub")
        self.assertEqual(zh[0][0], "gh-proxy.com")
        self.assertEqual(zh[-1][1], en[0][1])
        info = dict(version="v9.8.7", name=SETUP_NAME, url=en[0][1], sha256=DIGEST, size=len(PAYLOAD))
        self.assertEqual(release_metadata(json.dumps(info).encode()), ("v9.8.7", DIGEST, len(PAYLOAD)))
        for fields in ({"url": "https://evil.test/setup.exe"}, {"version": "v9.8.7-beta"}, {"sha256": ""},
                       {"size": MAX_SETUP_BYTES + 1}, {"size": True}, {"name": "other.exe"}):
            with self.subTest(fields=fields), self.assertRaises(ValueError):
                release_metadata(json.dumps(dict(info, **fields)).encode())
        self.assertIsNone(newer_version("v0.7.4", "0.7.4"))
        self.assertEqual(newer_version("v0.8.0", "0.7.4"), "v0.8.0")

    def test_streaming_progress_verified_cache_and_no_duplicate_download(self):
        with TemporaryDirectory() as folder:
            reply = Reply()
            network = Network(reply)
            download = SetupDownload(folder, network=network)
            progress, ready = [], []
            download.progress.connect(lambda done, size: progress.append((done, size)))
            download.ready.connect(ready.append)
            download.start("v9.8.7", DIGEST, setup_sources("v9.8.7", "en"), len(PAYLOAD))
            reply.feed(PAYLOAD[:300000])
            self.assertFalse(ready)
            reply.feed(PAYLOAD[300000:])
            reply.complete()
            self.assertEqual(Path(ready[0]).read_bytes(), PAYLOAD)
            self.assertEqual(progress[-1], (len(PAYLOAD), len(PAYLOAD)))
            self.assertGreater(len(progress), 2)
            self.assertFalse(list(Path(folder).glob("*.part")))
            download.start("v9.8.7", DIGEST, setup_sources("v9.8.7", "en"), len(PAYLOAD))
            self.assertEqual(len(network.requests), 1)
            self.assertEqual(len(ready), 2)
            download.cancel(notify=False)

    def test_corrupt_mirror_falls_back_without_ever_reporting_the_bad_file_ready(self):
        with TemporaryDirectory() as folder:
            bad, good = Reply(), Reply()
            network = Network(bad, good)
            download = SetupDownload(folder, network=network)
            ready = []
            download.ready.connect(ready.append)
            download.start("v9.8.7", DIGEST, setup_sources("v9.8.7", "zh"))
            bad.feed(b"MZ" + b"x" * (len(PAYLOAD) - 2))
            bad.complete()
            self.assertFalse(ready)
            good.feed(PAYLOAD)
            good.complete()
            self.assertEqual(len(ready), 1)
            self.assertEqual(Path(ready[0]).read_bytes(), PAYLOAD)
            self.assertEqual(len(network.requests), 2)
            download.cancel(notify=False)

    def test_slow_source_switches_and_late_aborted_reply_cannot_finish_the_next_request(self):
        with TemporaryDirectory() as folder:
            slow, good = Reply(), Reply()
            network = Network(slow, good)
            download = SetupDownload(folder, network=network)
            ready = []
            download.ready.connect(ready.append)
            download.start("v9.8.7", DIGEST, setup_sources("v9.8.7", "zh"))
            slow.feed(b"MZ")
            download.started, download.last_activity = 80, 99
            with patch("still.update_download.monotonic", return_value=100):
                download.watch_speed()
            self.assertTrue(slow.aborted)
            slow.finished.emit()
            self.assertIs(download.reply, good)
            good.feed(PAYLOAD)
            good.complete()
            self.assertEqual(len(ready), 1)
            download.cancel(notify=False)

    def test_cancel_removes_partial_file_and_does_not_launch_or_advance_sources(self):
        with TemporaryDirectory() as folder:
            reply = Reply()
            network = Network(reply)
            download = SetupDownload(folder, network=network)
            ready = []
            download.ready.connect(ready.append)
            download.start("v9.8.7", DIGEST, setup_sources("v9.8.7", "en"))
            reply.feed(PAYLOAD[:100])
            download.cancel()
            self.assertTrue(reply.aborted)
            self.assertFalse(ready)
            self.assertEqual(len(network.requests), 1)
            self.assertFalse(list(Path(folder).iterdir()))

    def test_oversized_or_html_responses_cannot_be_saved_as_installers(self):
        for size, payload in ((MAX_SETUP_BYTES + 1, b"MZ"), (5, b"<html")):
            with self.subTest(size=size), TemporaryDirectory() as folder:
                reply = Reply(size)
                download = SetupDownload(folder, network=Network(reply))
                failed, ready = [], []
                download.failed.connect(failed.append)
                download.ready.connect(ready.append)
                download.start("v9.8.7", DIGEST, setup_sources("v9.8.7", "en")[:1])
                reply.feed(payload)
                self.assertTrue(failed)
                self.assertFalse(ready)
                self.assertFalse(list(Path(folder).iterdir()))

    def test_button_reports_percent_and_download_can_continue_after_settings_close(self):
        window = SimpleNamespace(updater=SimpleNamespace(state="downloading", received=42, total=100, source="GitHub"),
                                 update_button=Mock())
        with patch("still.i18n.display_language", return_value="en"):
            MainWindow.refresh_update(window)
        window.update_button.setText.assert_called_once_with("Downloading 42%")
        window.update_button.setEnabled.assert_called_once_with(True)
        controller = SimpleNamespace(engine=SimpleNamespace(surface=None), store=SimpleNamespace(settings=SimpleNamespace(organizer_enabled=False)),
                                     release_check=SimpleNamespace(busy=True), native_taskbar_style=None, hotkeys=None, workbench=None)
        self.assertTrue(Controller.needs_background(controller))

    def test_clicking_update_downloads_in_app_and_clicking_again_cancels(self):
        with TemporaryDirectory() as folder:
            check = ReleaseCheck(self.app, folder)
            check.state, check.version, check.sha256 = "update", "v9.8.7", DIGEST
            with patch.object(check.download, "start") as start:
                check.activate()
                start.assert_called_once()
                self.assertEqual(check.state, "downloading")
            check.activate()
            self.assertEqual(check.state, "update")
            check.close()
