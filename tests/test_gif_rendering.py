"""GIF must use the same native video output as working video wallpapers."""
from pathlib import Path
import tempfile
import unittest

from PySide6.QtCore import QEvent, QEventLoop, QTimer
from PySide6.QtGui import QMovie
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
        for _ in range(2):
            self.engine.apply(self.gif)
            surface = self.engine.surface
            self.assertIsInstance(surface, QVideoWidget)
            self.assertIsNotNone(self.engine.player)
            self.assertEqual(win32.GetParent(int(surface.winId())), self.engine.worker)
            colors, timestamps = [], []
            def frame_arrived(frame):
                if frame.isValid():
                    self.assertEqual((frame.width(), frame.height()), (480, 270))
                    image = frame.toImage()
                    colors.append(image.pixelColor(240, 135).rgba())
                    timestamps.append(frame.startTime())
            surface.videoSink().videoFrameChanged.connect(frame_arrived)
            # Backend startup can take longer during concurrent build/driver
            # initialization. Wait for observable frames, with a hard deadline.
            import time
            deadline = time.monotonic() + 4
            while time.monotonic() < deadline and (len(timestamps) < 4 or len(set(colors)) < 2):
                pump(0.1)
            self.assertTrue(self.engine.player.hasVideo())
            self.assertGreaterEqual(len(set(colors)), 2, colors)
            self.assertGreaterEqual(len(set(timestamps)), 3)
            self.assertEqual(Path(self.engine.player.source().toLocalFile()), item_path(self.gif))
            self.assertIsNone(self.engine.player.audioOutput())
            self.engine.pause()
            pump(0.1)
            frozen = self.engine.player.position()
            pump(0.3)
            self.assertEqual(self.engine.player.position(), frozen)
            self.engine.pause()
            pump(0.4)
            self.assertNotEqual(self.engine.player.position(), frozen)
        # Cross the GIF's original end boundary and verify native looping.
        pump(4.2)
        self.assertTrue(self.engine.player.isPlaying())
        self.assertEqual(self.engine.player.loops(), QMediaPlayer.Loops.Infinite)

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
