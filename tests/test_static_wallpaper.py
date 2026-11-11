"""Static switching stays responsive even when Windows or file I/O is slow."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

from PySide6.QtCore import QEventLoop, QTimer
from PySide6.QtGui import QImage
from PySide6.QtWidgets import QApplication

from still.engine import WallpaperEngine
from still.model import ASSETS, Store
from still.static_wallpaper import prepare

ORIGINAL = {"path": "", "WallpaperStyle": "10", "TileWallpaper": "0"}
HELPER = '''import json,sys,time
from pathlib import Path
request=json.loads(sys.stdin.readline())
print(json.dumps(dict(phase='prepared',original=request.get('original') or {original})),flush=True)
if sys.stdin.readline()!='apply\\n': sys.exit(1)
Path(sys.argv[2]).write_text('acknowledged')
print(json.dumps(dict(phase='windows')),flush=True)
mode=sys.argv[1]
if mode=='slow': time.sleep(1)
if mode=='hang': time.sleep(60)
print(json.dumps(dict(phase='done',ok=mode!='error',error='fixture failure' if mode=='error' else '')),flush=True)
sys.exit(3 if mode=='crash' else 1 if mode=='error' else 0)
'''.replace('{original}', repr(ORIGINAL))


def until(predicate, timeout=5):
    deadline = time.monotonic() + timeout
    while not predicate() and time.monotonic() < deadline:
        loop = QEventLoop()
        QTimer.singleShot(10, loop.quit)
        loop.exec()
    if not predicate():
        raise AssertionError("Operation did not complete before deadline")


class StaticWallpaperTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])
        cls.app.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.store = Store(self.root / "profile")
        self.store.settings.original_wallpaper = dict(ORIGINAL)
        self.engine = WallpaperEngine(self.store, self.app)
        self.script = self.root / "helper.py"
        self.script.write_text(HELPER, encoding="utf-8")
        self.marker = self.root / "windows-started.txt"
        self.errors, self.applied = [], []
        self.engine.error.connect(self.errors.append)
        self.engine.applied.connect(self.applied.append)
        self.old = self.store.items[1]
        self.engine.active = self.old
        self.engine.state = "static"
        self.store.settings.active_id = self.old["id"]

    def tearDown(self):
        self.engine.cleanup()
        self.engine.deleteLater()
        self.app.processEvents()
        self.temp.cleanup()

    def command(self, mode):
        return patch.object(self.engine.static_task, "command",
                            return_value=(sys.executable, [str(self.script), mode, str(self.marker)]))

    def test_slow_windows_call_keeps_gui_alive_and_only_then_commits(self):
        heartbeat = []
        timer = QTimer(interval=20)
        timer.timeout.connect(lambda: heartbeat.append(time.monotonic()))
        timer.start()
        try:
            with self.command("slow"):
                self.engine.apply(self.store.items[0])
                self.assertTrue(self.engine.busy)
                self.assertEqual(self.store.settings.active_id, self.old["id"])
                self.assertFalse(self.applied)
                with self.assertRaises(RuntimeError):
                    self.engine.apply(self.store.items[2])
                until(lambda: not self.engine.busy)
            self.assertTrue(self.marker.exists())
            self.assertGreater(len(heartbeat), 10)
            self.assertLess(max(b-a for a,b in zip(heartbeat, heartbeat[1:])), .5)
            self.assertEqual(self.engine.active["id"], self.store.items[0]["id"])
            self.assertEqual(Store(self.store.directory).settings.active_id, self.store.items[0]["id"])
            self.assertEqual(len(self.applied), 1)
            self.assertFalse(self.errors)
        finally:
            timer.stop()

    def test_failure_retains_previous_wallpaper_and_allows_retry(self):
        with self.command("error"):
            self.engine.apply(self.store.items[0])
            until(lambda: not self.engine.busy)
        self.assertEqual(self.engine.active, self.old)
        self.assertEqual(self.store.settings.active_id, self.old["id"])
        self.assertEqual(self.errors, ["fixture failure"])
        self.assertFalse(self.applied)
        with self.command("ok"):
            self.engine.apply(self.store.items[2])
            until(lambda: not self.engine.busy)
        self.assertEqual(self.engine.active, self.store.items[2])

    def test_timeout_ends_helper_without_committing_success(self):
        self.engine.static_task.prepared.connect(lambda _original: self.engine.static_task.timer.start(80))
        with self.command("hang"):
            self.engine.apply(self.store.items[0])
            until(lambda: not self.engine.busy)
        self.assertTrue(self.errors)
        self.assertFalse(self.applied)
        self.assertEqual(self.engine.active, self.old)

    def test_abnormal_helper_exit_cannot_report_success(self):
        with self.command('crash'):
            self.engine.apply(self.store.items[0])
            until(lambda:not self.engine.busy)
        self.assertTrue(self.errors)
        self.assertFalse(self.applied)
        self.assertEqual(self.engine.active,self.old)

    def test_backup_save_failure_never_acknowledges_windows_change(self):
        self.store.settings.original_wallpaper = None
        with self.command("ok"), patch.object(self.store, "save", side_effect=OSError("disk full")):
            self.engine.apply(self.store.items[0])
            until(lambda: not self.engine.busy)
        self.assertFalse(self.marker.exists())
        self.assertIsNone(self.store.settings.original_wallpaper)
        self.assertEqual(self.errors, ["disk full"])

    def test_stop_and_shutdown_cancel_without_late_activation(self):
        with self.command("hang"):
            self.engine.apply(self.store.items[0])
            until(self.marker.exists)
            self.engine.stop()
            until(lambda: not self.engine.busy)
        self.assertIsNone(self.engine.active)
        self.assertFalse(self.applied)
        self.assertFalse(self.errors)
        with self.command("hang"):
            self.engine.apply(self.store.items[0])
            self.engine.static_task.close()
        self.assertFalse(self.engine.busy)

    def test_restoration_finishes_before_reporting_success(self):
        restored=[]
        self.engine.restored.connect(lambda: restored.append(True))
        with self.command("slow"):
            self.engine.restore_original()
            self.assertFalse(restored)
            self.assertEqual(self.engine.active,self.old)
            until(lambda: not self.engine.busy)
        self.assertTrue(restored)
        self.assertIsNone(self.engine.active)
        self.assertEqual(self.store.settings.active_id, "")

    def test_preparation_preserves_original_and_full_resolution_pixels(self):
        source=self.root/'picture.webp'
        image=QImage(240,135,QImage.Format.Format_RGB32)
        image.fill(0xff3a78b4)
        self.assertTrue(image.save(str(source),'WEBP'))
        digest=hashlib.sha256(source.read_bytes()).digest()
        request=dict(path=str(source),directory=str(self.store.directory),fit='fill',original=None)
        original=dict(ORIGINAL,path=str(ASSETS/'night-drive.jpg'))
        with patch('still.win32.current_wallpaper',return_value=original), patch('still.win32.set_wallpaper') as setter:
            native,original=prepare(request)
            setter.assert_not_called()
        self.assertEqual(QImage(native),QImage(str(source)))
        self.assertEqual(QImage(native).size(),image.size())
        self.assertEqual(hashlib.sha256(source.read_bytes()).digest(),digest)
        self.assertEqual(Path(original['path']).read_bytes(),(ASSETS/'night-drive.jpg').read_bytes())

    def test_real_helper_exits_before_system_change_without_acknowledgement(self):
        request=dict(path=str(ASSETS/'night-drive.jpg'),directory=str(self.store.directory),fit='fill',original=ORIGINAL)
        helper=subprocess.run([sys.executable,str(Path(__file__).resolve().parents[1]/'main.py'),'--static-wallpaper-helper'],
            input=json.dumps(request).encode()+b'\n',stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=15)
        self.assertEqual(helper.returncode,1,helper.stderr.decode(errors='replace'))
        replies=[json.loads(line) for line in helper.stdout.splitlines()]
        self.assertEqual([r['phase'] for r in replies],['prepared'])

    def test_converted_wallpaper_does_not_overwrite_the_active_conversion(self):
        source=self.root/'picture.webp'
        image=QImage(80,45,QImage.Format.Format_RGB32)
        image.fill(0xff443322)
        self.assertTrue(image.save(str(source),'WEBP'))
        active=self.store.directory/'native-wallpaper-a.png'
        active.write_bytes(b'previous wallpaper bytes')
        request=dict(path=str(source),directory=str(self.store.directory),fit='fill',original=ORIGINAL)
        with patch('still.win32.current_wallpaper',return_value=dict(ORIGINAL,path=str(active))):
            output,_original=prepare(request)
        self.assertEqual(Path(output).name,'native-wallpaper-b.png')
        self.assertEqual(active.read_bytes(),b'previous wallpaper bytes')
