"""Cache small video covers using one short-lived, supervised decoder process."""
from __future__ import annotations

from hashlib import sha256
from pathlib import Path
from PySide6.QtCore import QObject, QTimer, Signal
from PySide6.QtGui import QPixmap
from .media_process import MediaProcess


class VideoThumbnails(QObject):
    ready = Signal(str, QPixmap)

    def __init__(self, directory, parent=None):
        super().__init__(parent)
        self.directory = directory / "thumbnails"
        self.pending = {}
        self.failed = set()
        self.active = None
        self.worker = MediaProcess(self)
        self.worker.ready.connect(self.cover_ready)
        self.worker.error.connect(lambda _error: self.finish())
        self.worker.finished.connect(self.schedule)
        self.start_timer = QTimer(self, singleShot=True)
        self.start_timer.timeout.connect(self.start_next)

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
        self.schedule()
        return cached

    def schedule(self):
        if self.pending and self.active is None and self.worker.process is None and not self.start_timer.isActive():
            self.start_timer.start(0)

    def start_next(self):
        if not self.pending or self.active is not None or self.worker.process is not None:
            return
        key = next(iter(self.pending))
        path = self.pending.pop(key)
        self.active = key, path
        self.worker.start({"mode":"thumbnail", "path":str(path), "output":str(self.directory/(key+".jpg"))})

    def cover_ready(self, result):
        if self.active is None:
            return
        key, path = self.active
        cover = QPixmap(str(self.directory/(key+".jpg")))
        self.finish(failed=cover.isNull())
        if not cover.isNull():
            self.ready.emit(str(path), cover)

    def finish(self, *, cancelled=False, failed=True):
        if self.active is None:
            return
        key, _path = self.active
        self.active = None
        if failed and not cancelled:
            self.failed.add(key)
        self.worker.stop()
        self.schedule()

    def cancel(self, path):
        path = Path(path)
        self.pending = {key:candidate for key,candidate in self.pending.items() if candidate != path}
        if self.active is not None and self.active[1] == path:
            self.finish(cancelled=True)

    def close(self):
        self.pending.clear()
        self.start_timer.stop()
        self.finish(cancelled=True)
        self.worker.shutdown()
