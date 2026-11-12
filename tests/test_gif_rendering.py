"""GIF must use the same native video output as working video wallpapers."""
from pathlib import Path
import tempfile
import unittest

from PySide6.QtCore import QEvent, QEventLoop, QTimer
from PySide6.QtGui import QImageReader, QMovie
from PySide6.QtMultimedia import QMediaPlayer
from PySide6.QtMultimediaWidgets import QVideoWidget
from PySide6.QtWidgets import QApplication

from still import win32
from still.engine import WallpaperEngine
from still.model import Store, item_path
from still.ui import Preview


def pump(seconds):
    loop = QEventLoop()
    QTimer.singleShot(int(seconds * 1000), loop.quit)
    loop.exec()


class GifRenderingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])
        cls.app.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.store = Store(Path(self.directory.name))
        self.store.settings.battery_pause = False
        self.store.settings.fullscreen_pause = False
        self.engine = WallpaperEngine(self.store, self.app)
        self.gif = next(i for i in self.store.items if i["kind"] == "gif")

    def tearDown(self):
        self.engine.cleanup()
        self.engine.deleteLater()
        self.app.sendPostedEvents(None, QEvent.Type.DeferredDelete)
        self.directory.cleanup()

    def test_gif_uses_native_video_output_with_changing_pixels(self):
        import time
        source_size = QImageReader(str(item_path(self.gif))).size()
        snapshots = []
        self.engine.media.message.connect(lambda result: snapshots.append(result) if result.get("phase")=="snapshot" else None)
        def snapshot():
            previous = len(snapshots)
            self.engine.media.control("snapshot")
            deadline = time.monotonic()+4
            while len(snapshots)==previous and time.monotonic()<deadline:
                pump(.03)
            self.assertGreater(len(snapshots), previous)
            return snapshots[-1]
        for _ in range(2):
            self.engine.apply(self.gif)
            deadline = time.monotonic()+8
            while self.engine.busy and time.monotonic()<deadline:
                pump(.05)
            self.assertIsNotNone(self.engine.surface)
            self.assertIsNotNone(self.engine.media.process)
            self.assertEqual(win32.GetParent(self.engine.surface),self.engine.worker)
            frames = []
            for _ in range(4):
                frames.append(snapshot())
                pump(.15)
            self.assertTrue(all((f["width"],f["height"])==(source_size.width(),source_size.height()) for f in frames))
            self.assertGreater(len({f["digest"] for f in frames}),1)
            self.assertGreater(frames[-1]["frames"],frames[0]["frames"])
            self.assertTrue(all(not f["audio_output"] and f["audio_track"]==-1 for f in frames))
            self.assertEqual(Path(self.engine.media.request["path"]),item_path(self.gif))
            self.assertFalse(self.engine.media.request["sound"])
            self.engine.pause();pump(.2)
            frozen = snapshot()["position"]
            pump(.3)
            self.assertEqual(snapshot()["position"],frozen)
            self.engine.pause();pump(.4)
            self.assertNotEqual(snapshot()["position"],frozen)
        pump(4.2)
        self.assertEqual(self.engine.state,"playing")
        self.assertGreater(snapshot()["frames"],frames[-1]["frames"])

    def test_preview_animates_only_when_visible(self):
        preview = Preview()
        preview.resize(640, 360)
        preview.set_item(self.gif)
        self.assertEqual(preview.movie.state(), QMovie.MovieState.NotRunning)
        preview.show()
        pump(0.4)
        first = preview.pixmap.cacheKey()
        pump(0.4)
        self.assertNotEqual(preview.pixmap.cacheKey(), first)
        preview.hide()
        pump(0.1)
        self.assertEqual(preview.movie.state(), QMovie.MovieState.Paused)
        frozen = preview.movie.currentFrameNumber()
        pump(0.3)
        self.assertEqual(preview.movie.currentFrameNumber(), frozen)
        preview.show()
        pump(0.3)
        self.assertEqual(preview.movie.state(), QMovie.MovieState.Running)
        old_movie = preview.movie
        preview.set_item(self.store.items[0])
        self.assertEqual(old_movie.state(), QMovie.MovieState.NotRunning)
        self.assertIsNone(preview.movie)
        preview.close()
        preview.deleteLater()


if __name__ == "__main__":
    unittest.main()
