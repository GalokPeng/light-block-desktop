from pathlib import Path
from tempfile import TemporaryDirectory
from time import monotonic
import unittest

from PySide6.QtCore import QEvent, QEventLoop, QTimer
from PySide6.QtWidgets import QApplication

from still.app import Controller
from still.model import Store
from still.video_thumbnails import VideoThumbnails


class LibraryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])
        cls.app.setQuitOnLastWindowClosed(False)

    def test_video_cover_decodes_once_and_survives_reopening(self):
        with TemporaryDirectory() as folder:
            store = Store(Path(folder))
            video = store.path(next(item for item in store.items if item["kind"] == "video"))
            decoder = VideoThumbnails(store.directory)
            covers = []
            decoder.ready.connect(lambda path, pixmap: covers.append((path, pixmap)))
            try:
                self.assertTrue(decoder.get(video).isNull())
                deadline = monotonic() + 7
                while not covers and monotonic() < deadline:
                    loop = QEventLoop()
                    QTimer.singleShot(50, loop.quit)
                    loop.exec()
                self.assertEqual(len(covers), 1)
                self.assertEqual(covers[0][0], str(video))
                self.assertFalse(covers[0][1].isNull())
                self.assertFalse(VideoThumbnails(store.directory).get(video).isNull())
            finally:
                decoder.close()

    def test_theme_switch_uses_the_current_controller_without_a_legacy_dock(self):
        with TemporaryDirectory() as folder:
            store = Store(Path(folder))
            store.settings.capture_hotkey = False
            store.settings.restore_on_start = False
            controller = Controller(self.app, store, check_updates=False)
            try:
                window = controller.window
                window.change_theme()
                self.assertTrue(window.light)
                self.assertEqual(Store(store.directory).settings.settings_theme, "light")
                window.change_theme()
                self.assertFalse(window.light)
                self.assertEqual(Store(store.directory).settings.settings_theme, "dark")
            finally:
                controller.cleanup()
                controller.window.close()
                self.app.sendPostedEvents(None, QEvent.Type.DeferredDelete)
