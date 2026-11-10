"""Decode and cache one small cover per video, using a single silent player."""
from __future__ import annotations

from hashlib import sha256
from pathlib import Path

from PySide6.QtCore import QObject, Qt, QTimer, QUrl, Signal
from PySide6.QtGui import QImage, QPixmap


class VideoThumbnails(QObject):
    ready = Signal(str, QPixmap)

    def __init__(self, directory, parent=None):
        super().__init__(parent)
        self.directory = directory / "thumbnails"
        self.pending = {}
        self.failed = set()
        self.player = None
        self.active = None
        self.first_frame = QImage()
        self.start_timer = QTimer(self, singleShot=True)
        self.start_timer.timeout.connect(self.start_next)
        self.timeout = QTimer(self, singleShot=True, interval=5000)
        self.timeout.timeout.connect(self.finish)

    def get(self, path):
        path = Path(path)
        try:
            info = path.stat()
        except OSError:
            return QPixmap()
        key = sha256(f"{path}\0{info.st_size}\0{info.st_mtime_ns}".encode()).hexdigest()
        cached = QPixmap(str(self.directory / (key + ".jpg")))
        if not cached.isNull() or key in self.failed or (self.active is not None and self.active[0] == key):
            return cached
        self.pending[key] = path
        if self.active is None and not self.start_timer.isActive():
            self.start_timer.start(0)
        return cached

    def start_next(self):
        if not self.pending or self.active is not None:
            return
        from PySide6.QtMultimedia import QMediaPlayer, QVideoSink

        if self.player is None:
            self.player = QMediaPlayer(self)
            self.player.mediaStatusChanged.connect(self.status_changed)
        key = next(iter(self.pending))
        path = self.pending.pop(key)
        sink = QVideoSink(self)
        sink.videoFrameChanged.connect(lambda frame: self.frame_changed(sink, frame))
        self.active = key, path, sink
        self.first_frame = QImage()
        self.timeout.start()
        self.player.setVideoSink(sink)
        self.player.setSource(QUrl.fromLocalFile(str(path)))
        if self.active is not None:
            self.player.play()

    def status_changed(self, status):
        if status in (self.player.MediaStatus.InvalidMedia, self.player.MediaStatus.EndOfMedia):
            self.finish()

    def frame_changed(self, sink, frame):
        if self.active is None or self.active[2] is not sink or not frame.isValid():
            return
        target = min(400, self.player.duration() // 8) * 1000
        early = 0 <= frame.startTime() < target
        if early and not self.first_frame.isNull():
            return
        image = frame.toImage()
        if image.isNull():
            return
        image = image.scaled(720, 480, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)
        if early:
            self.first_frame = image
        else:
            self.finish(image)

    def finish(self, image=None):
        if self.active is None:
            return
        key, path, sink = self.active
        self.active = None
        self.timeout.stop()
        self.player.stop()
        self.player.setVideoSink(None)
        self.player.setSource(QUrl())
        sink.deleteLater()
        cover = self.first_frame if image is None else image
        self.first_frame = QImage()
        if cover.isNull():
            self.failed.add(key)
        else:
            try:
                self.directory.mkdir(parents=True, exist_ok=True)
                cover.save(str(self.directory / (key + ".jpg")), "JPEG", 86)
            except OSError:
                pass
            self.ready.emit(str(path), QPixmap.fromImage(cover))
        if self.pending:
            self.start_timer.start(0)

    def close(self):
        self.pending.clear()
        self.start_timer.stop()
        self.finish(QImage())
