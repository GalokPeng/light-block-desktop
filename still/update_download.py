"""Bounded, streaming Setup downloads with integrity checks and source failover."""
from hashlib import sha256
import os
from pathlib import Path
from time import monotonic
import uuid

from PySide6.QtCore import QObject, QTimer, QUrl, Signal
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest
from .model import VERSION

MAX_SETUP_BYTES = 128 * 1024 * 1024


def file_digest(path):
    digest = sha256()
    with Path(path).open("rb") as file:
        if file.read(2) != b"MZ":
            raise ValueError("Not a Windows executable")
        file.seek(0)
        for chunk in iter(lambda: file.read(256 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class SetupDownload(QObject):
    progress = Signal(int, int)
    source_changed = Signal(str)
    verifying = Signal()
    ready = Signal(str)
    failed = Signal(str)
    cancelled = Signal()

    def __init__(self, directory, parent=None, network=None):
        super().__init__(parent)
        self.directory = Path(directory)
        self.network = network or QNetworkAccessManager(self)
        self.reply = self.file = self.temporary = None
        self.timer = QTimer(self, interval=1000)
        self.timer.timeout.connect(self.watch_speed)
        self.sources = []
        self.index = 0
        self.received = self.total = self.expected_size = 0
        self.last_activity = self.started = 0.
        self.target = None
        self.closing = False

    @property
    def busy(self):
        return self.reply is not None

    def start(self, version, digest, sources, size=0):
        self.cancel(notify=False)
        self.closing = False
        self.digest, self.sources, self.index = digest, list(sources), 0
        self.expected_size = size or 0
        self.target = self.directory / f"Qingkuai-{version}-Setup-x64.exe"
        try:
            self.directory.mkdir(parents=True, exist_ok=True)
            if self.target.is_file() and 0 < self.target.stat().st_size <= MAX_SETUP_BYTES:
                self.verifying.emit()
                if file_digest(self.target) == digest:
                    self.ready.emit(str(self.target))
                    return
        except (OSError, ValueError):
            pass  # A damaged cache entry is replaced by a complete verified file.
        self.attempt()

    def attempt(self):
        self.received, self.total = 0, self.expected_size
        self.hash, self.prefix = sha256(), b""
        try:
            self.temporary = self.directory / (".setup-" + uuid.uuid4().hex + ".part")
            self.file = self.temporary.open("xb")
        except OSError:
            self.discard()
            self.failed.emit("disk")
            return
        name, url = self.sources[self.index]
        request = QNetworkRequest(QUrl(url))
        request.setAttribute(QNetworkRequest.Attribute.RedirectPolicyAttribute,
                             QNetworkRequest.RedirectPolicy.NoLessSafeRedirectPolicy)
        request.setTransferTimeout(12000)
        request.setRawHeader(b"Accept-Encoding", b"identity")
        request.setRawHeader(b"User-Agent", f"Light-Block-Desktop/{VERSION}".encode("ascii"))
        self.started = self.last_activity = monotonic()
        reply = self.network.get(request)
        self.reply = reply
        reply.setReadBufferSize(256 * 1024)
        reply.readyRead.connect(lambda r=reply: self.consume(r))
        reply.finished.connect(lambda r=reply: self.finished(r))
        self.timer.start()
        self.source_changed.emit(name)
        self.progress.emit(0, self.total)

    def consume(self, reply):
        if reply is not self.reply:
            return
        status = reply.attribute(QNetworkRequest.Attribute.HttpStatusCodeAttribute)
        if status != 200:
            self.next_source("network")
            return
        try:
            declared = int(bytes(reply.rawHeader("Content-Length")) or b"0")
        except ValueError:
            self.next_source("size")
            return
        if declared < 0 or declared > MAX_SETUP_BYTES or (self.expected_size and declared and declared != self.expected_size):
            self.next_source("size")
            return
        self.total = self.expected_size or declared
        while reply is self.reply and reply.bytesAvailable():
            chunk = bytes(reply.read(256 * 1024))
            self.prefix = (self.prefix + chunk)[:2]
            if (len(self.prefix) == 2 and self.prefix != b"MZ") or self.received + len(chunk) > MAX_SETUP_BYTES:
                self.next_source("integrity")
                return
            try:
                self.file.write(chunk)
            except OSError:
                self.discard()
                self.failed.emit("disk")
                return
            self.hash.update(chunk)
            self.received += len(chunk)
            self.last_activity = monotonic()
            self.progress.emit(self.received, self.total)

    def finished(self, reply):
        if reply is not self.reply:
            return
        self.consume(reply)
        if reply is not self.reply:
            return
        if (reply.error() != QNetworkReply.NetworkError.NoError or not self.received
                or (self.total and self.received != self.total)):
            self.next_source("network")
            return
        self.verifying.emit()
        if self.hash.hexdigest() != self.digest or self.prefix != b"MZ":
            self.next_source("integrity")
            return
        try:
            self.file.flush()
            os.fsync(self.file.fileno())
            self.file.close()
            self.file = None
            os.replace(self.temporary, self.target)
            self.temporary = None
        except OSError:
            self.discard()
            self.failed.emit("disk")
            return
        self.discard()
        self.ready.emit(str(self.target))

    def watch_speed(self):
        if self.reply is None:
            return
        elapsed = monotonic() - self.started
        stalled = monotonic() - self.last_activity > (6 if self.received == 0 else 12)
        slow = elapsed >= 10 and self.received < 2 * 1024 * 1024 and self.received / elapsed < 64 * 1024
        if stalled or (slow and self.index + 1 < len(self.sources)):
            self.next_source("network")

    def next_source(self, reason):
        self.discard()
        self.index += 1
        if self.index < len(self.sources) and not self.closing:
            self.attempt()
        else:
            self.failed.emit(reason)

    def discard(self):
        self.timer.stop()
        reply, self.reply = self.reply, None
        if reply is not None:
            if not reply.isFinished():
                reply.abort()
            reply.deleteLater()
        if self.file is not None:
            try:
                self.file.close()
            except OSError:
                pass
            self.file = None
        if self.temporary is not None:
            try:
                self.temporary.unlink(missing_ok=True)
            except OSError:
                pass
            self.temporary = None

    def cancel(self, *, notify=True):
        self.closing = True
        self.discard()
        if notify:
            self.cancelled.emit()
