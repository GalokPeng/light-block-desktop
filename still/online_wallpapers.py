"""On-demand catalog and bounded streaming downloads, owned by the controller."""
from __future__ import annotations

from hashlib import sha256, new as new_hash
import json
import os
from pathlib import Path
from time import monotonic
from urllib.parse import urlsplit
import uuid

from PySide6.QtCore import QObject, QTimer, QUrl, Signal
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest

from .model import VERSION, managed_wallpaper
from .online_catalog import (CATALOG_URL, MAX_CATALOG_BYTES, MAX_PREVIEW_BYTES, MEDIA_HOSTS, SEED,
    catalog_version, checksum_for, checksum_key, https_url, local_id, media_signature, parse_catalog)


class GifCoverFrame:
    """Find one complete GIF frame incrementally, without decoding the animation."""
    def __init__(self):
        self.data, self.offset, self.stage = bytearray(), 0, "header"
        self.image_blocks = False
        self.not_gif = False

    def feed(self, chunk):
        self.data.extend(chunk)
        while True:
            data, pos = self.data, self.offset
            if self.stage == "header":
                if len(data) < 6:
                    return None
                if data[:6] not in (b"GIF87a", b"GIF89a"):
                    self.not_gif = True
                    self.data.clear()
                    return None
                if len(data) < 13:
                    return None
                self.offset = 13 + (3 * (2 ** ((data[10] & 7) + 1)) if data[10] & 128 else 0)
                self.stage = "block"
            elif self.stage == "block":
                if len(data) <= pos:
                    return None
                if data[pos] == 0x21:  # extension label, followed by data sub-blocks
                    if len(data) < pos + 2:
                        return None
                    self.offset, self.stage, self.image_blocks = pos + 2, "subblocks", False
                elif data[pos] == 0x2c:
                    if len(data) < pos + 10:
                        return None
                    flags = data[pos + 9]
                    self.offset = pos + 10 + (3 * (2 ** ((flags & 7) + 1)) if flags & 128 else 0)
                    self.stage = "lzw"
                else:
                    raise ValueError("GIF has no complete first image")
            elif self.stage == "lzw":
                if len(data) <= pos:
                    return None
                self.offset, self.stage, self.image_blocks = pos + 1, "subblocks", True
            else:
                if len(data) <= pos or len(data) < pos + 1 + data[pos]:
                    return None
                self.offset = pos + 1 + data[pos]
                if data[pos] == 0:
                    if self.image_blocks:
                        return bytes(data[:self.offset]) + b";"
                    self.stage = "block"


class StreamJob(QObject):
    ready = Signal(str)
    failed = Signal(str)
    progress = Signal(object, object)

    def __init__(self, network, target, limit, *, size=0, digest="", algorithm="sha256", kind="", hosts=MEDIA_HOSTS, parent=None):
        super().__init__(parent)
        self.network, self.target, self.limit = network, Path(target), limit
        self.expected_size, self.digest, self.kind, self.hosts = size, digest, kind, hosts
        self.reply = self.file = self.part = None
        self.received, self.prefix, self.hash = 0, b"", new_hash(algorithm)
        self.retry_after = 60
        self.cover_frame = GifCoverFrame() if kind == "cover" else None

    def start(self, url):
        try:
            self.target.parent.mkdir(parents=True, exist_ok=True)
            self.part = self.target.with_name(".online-" + uuid.uuid4().hex + ".part")
            self.file = self.part.open("xb")
            request = QNetworkRequest(QUrl(url))
            request.setTransferTimeout(20000)
            request.setAttribute(QNetworkRequest.Attribute.RedirectPolicyAttribute,
                                 QNetworkRequest.RedirectPolicy.UserVerifiedRedirectPolicy)
            request.setRawHeader(b"Accept-Encoding", b"identity")
            request.setRawHeader(b"User-Agent", f"LightBlockDesktop/{VERSION} (https://github.com/GalokPeng/light-block-desktop)".encode())
            reply = self.network.get(request)
            self.reply = reply
            reply.setReadBufferSize(256 * 1024)
            reply.redirected.connect(lambda url, r=reply: self.redirect(r, url))
            reply.readyRead.connect(lambda r=reply: self.consume(r))
            reply.finished.connect(lambda r=reply: self.finish(r))
        except OSError:
            self.fail("disk")

    def redirect(self, reply, url):
        if reply is not self.reply:
            return
        if https_url(url.toString(), self.hosts):
            reply.redirectAllowed.emit()
        else:
            self.fail("source")

    def consume(self, reply):
        if reply is not self.reply:
            return
        status = reply.attribute(QNetworkRequest.Attribute.HttpStatusCodeAttribute)
        if status in (301, 302, 303, 307, 308):
            reply.readAll()
            return
        if status == 429:
            try:
                self.retry_after = max(60, min(3600, int(bytes(reply.rawHeader("Retry-After")) or b"60")))
            except ValueError:
                pass
            self.fail("rate_limit")
            return
        if status != 200:
            self.fail("network")
            return
        try:
            declared = int(bytes(reply.rawHeader("Content-Length")) or b"0")
            if declared < 0 or (declared > self.limit and self.kind != "cover") or (self.expected_size and declared and declared != self.expected_size):
                self.fail("size")
                return
            while reply is self.reply and reply.bytesAvailable():
                chunk = bytes(reply.read(256 * 1024))
                if self.received + len(chunk) > self.limit:
                    self.fail("size")
                    return
                self.prefix = (self.prefix + chunk)[:32]
                self.file.write(chunk)
                self.hash.update(chunk)
                self.received += len(chunk)
                if self.cover_frame is not None:
                    frame = self.cover_frame.feed(chunk)
                    if self.cover_frame.not_gif:
                        self.cover_frame = None
                    elif frame is not None:
                        # Stop the response before the remaining animation frames.
                        # This only transforms cached covers, never original downloads.
                        self.file.seek(0)
                        self.file.write(frame)
                        self.file.truncate()
                        self.received = len(frame)
                        self.commit()
                        return
            self.progress.emit(self.received, self.expected_size or declared)
        except (OSError, ValueError):
            self.fail("disk")

    def finish(self, reply):
        if reply is not self.reply:
            return
        self.consume(reply)
        if reply is not self.reply:
            return
        if reply.error() != QNetworkReply.NetworkError.NoError or not self.received:
            self.fail("network")
            return
        self.commit()

    def commit(self):
        if ((self.expected_size and self.received != self.expected_size)
                or (self.digest and self.hash.hexdigest() != self.digest)
                or (self.kind and not media_signature(self.prefix, self.kind))):
            self.fail("integrity")
            return
        try:
            self.file.flush()
            os.fsync(self.file.fileno())
            self.file.close()
            self.file = None
            os.replace(self.part, self.target)
            self.part = None
        except OSError:
            self.fail("disk")
            return
        self.cancel()
        self.ready.emit(str(self.target))

    def fail(self, reason):
        self.cancel()
        self.failed.emit(reason)

    def cancel(self):
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
        if self.part is not None:
            try:
                self.part.unlink(missing_ok=True)
            except OSError:
                pass
            self.part = None


class CachedFileJob(StreamJob):
    """Recheck and promote preview bytes in small event-loop slices."""
    def __init__(self, *args, cached, **kwargs):
        super().__init__(*args, **kwargs)
        self.cached, self.source = Path(cached), None
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.copy_chunk)

    def start(self, _url):
        try:
            self.target.parent.mkdir(parents=True, exist_ok=True)
            self.part = self.target.with_name(".online-" + uuid.uuid4().hex + ".part")
            self.file = self.part.open("xb")
            self.source = self.cached.open("rb")
            self.timer.start(0)
        except OSError:
            self.fail("disk")

    def copy_chunk(self):
        try:
            chunk = self.source.read(256 * 1024)
            if not chunk:
                self.commit()
                return
            if self.received + len(chunk) > self.limit:
                self.fail("size")
                return
            self.file.write(chunk)
            self.prefix = (self.prefix + chunk)[:32]
            self.hash.update(chunk)
            self.received += len(chunk)
            self.progress.emit(self.received, self.expected_size)
        except OSError:
            self.fail("disk")

    def cancel(self):
        self.timer.stop()
        if self.source is not None:
            self.source.close()
            self.source = None
        super().cancel()


class OnlineLibrary(QObject):
    changed = Signal()
    media_ready = Signal(str, str)
    imported = Signal(object)
    preview_ready = Signal(str, str)
    preview_failed = Signal(str)
    preview_progress = Signal(str, object, object)

    def __init__(self, store, parent=None, network=None):
        super().__init__(parent)
        self.store = store
        self.directory = store.directory / "online-cache"
        self.network = network or QNetworkAccessManager(self)
        self.jobs, self.states, self.progress = {}, {}, {}
        self.thumb_queue, self.thumb_failed = {}, set()
        self.cooldowns, self.invalid_previews = {}, set()
        self.catalog_state, self.activated, self.thumbnails_active = "bundled", False, False
        self.catalog = parse_catalog(SEED.read_bytes())
        try:
            cached = self.directory / "catalog.json"
            if cached.stat().st_size <= MAX_CATALOG_BYTES:
                candidate = parse_catalog(cached.read_bytes())
                if catalog_version(candidate) >= catalog_version(self.catalog):
                    self.catalog = candidate
                    self.catalog_state = "cached"
        except (OSError, ValueError, TypeError):
            pass

    @property
    def entries(self):
        return self.catalog["entries"]

    @property
    def busy(self):
        return any(key[0] == "download" for key in self.jobs)

    def limited(self, url):
        return self.cooldowns.get(urlsplit(url).hostname, 0) > monotonic()

    def run_job(self, key, url, target, limit, done, *, failed=None, cached=None, **options):
        self.cancel_job(key)
        if cached is None and self.limited(url):
            if failed:
                QTimer.singleShot(0, lambda: failed("rate_limit"))
            return
        job = (StreamJob(self.network, target, limit, parent=self, **options) if cached is None else
               CachedFileJob(self.network, target, limit, parent=self, cached=cached, **options))
        self.jobs[key] = job

        def complete(path):
            if self.jobs.get(key) is not job:
                return
            self.jobs.pop(key)
            job.deleteLater()
            done(Path(path))

        def error(reason):
            if self.jobs.get(key) is not job:
                return
            self.jobs.pop(key)
            if reason == "rate_limit":
                self.cooldowns[urlsplit(url).hostname] = monotonic() + job.retry_after
            job.deleteLater()
            if failed:
                failed(reason)
        job.ready.connect(complete)
        job.failed.connect(error)
        if key[0] == "download":
            job.progress.connect(lambda received, total: self.download_progress(key[1], received, total))
        elif key[0] == "preview":
            job.progress.connect(lambda received, total: self.preview_progress.emit(key[1], received, total))
        job.start(url)

    def activate(self):
        self.thumbnails_active = True
        if not self.activated:
            self.activated = True
            self.refresh()

    def refresh(self):
        if ("catalog", "") in self.jobs:
            return
        self.thumb_failed.clear()
        self.catalog_state = "refreshing"
        self.changed.emit()
        candidate = self.directory / "catalog-candidate.json"

        def loaded(path):
            try:
                catalog = parse_catalog(path.read_bytes())
                if catalog_version(catalog) < catalog_version(self.catalog):
                    raise ValueError("Catalog schema downgrade")
                os.replace(path, self.directory / "catalog.json")
                self.catalog = catalog
                self.catalog_state = "live"
                self.thumb_failed.clear()
            except (OSError, ValueError, TypeError):
                path.unlink(missing_ok=True)
                self.catalog_state = "offline"
            self.changed.emit()

        def failed(_reason):
            self.catalog_state = "offline"
            self.changed.emit()
        self.run_job(("catalog", ""), CATALOG_URL, candidate, MAX_CATALOG_BYTES, loaded,
                     failed=failed, hosts={"raw.githubusercontent.com"})

    def thumbnail_path(self, entry):
        return self.directory / "covers" / (sha256(entry["thumbnail_url"].encode()).hexdigest() + ".cache")

    def thumbnail(self, entry):
        path = self.thumbnail_path(entry)
        if path.is_file():
            return path
        key = ("cover", entry["id"])
        if self.thumbnails_active and key not in self.jobs and entry["id"] not in self.thumb_failed:
            self.thumb_queue[entry["id"]] = entry
            self.pump_thumbnails()
        return None

    def pump_thumbnails(self):
        while self.thumbnails_active and self.thumb_queue and sum(k[0] == "cover" for k in self.jobs) < 3:
            ident, entry = self.thumb_queue.popitem()

            def ready(path, ident=ident):
                self.media_ready.emit(ident, str(path))
                self.prune("covers", 64 * 1024 * 1024)
                self.pump_thumbnails()

            def failed(_reason, ident=ident):
                self.thumb_failed.add(ident)
                self.media_ready.emit(ident, "")
                self.pump_thumbnails()
            self.run_job(("cover", ident), entry["thumbnail_url"], self.thumbnail_path(entry), 8 * 1024 * 1024,
                         ready, failed=failed, kind="cover")

    def local_item(self, entry):
        item = self.store.find(local_id(entry))
        if item is None or item.get("online_checksum", item.get("online_sha256")) != checksum_key(entry):
            return None
        try:
            stat = self.store.path(item).stat()
            if stat.st_size == entry["bytes"] and stat.st_mtime_ns == item.get("verified_mtime"):
                return item
        except OSError:
            pass
        return None

    def state(self, entry):
        if self.local_item(entry):
            return "downloaded"
        if entry["id"] in self.states:
            return self.states[entry["id"]]
        if any(item.get("online_id") == entry["id"] and
               (item.get("online_revision") != entry["revision"] or item.get("online_checksum", item.get("online_sha256")) != checksum_key(entry))
               and self.store.path(item).is_file() for item in self.store.settings.library):
            return "update"
        return "available"

    def download(self, entry):
        if self.local_item(entry) or ("download", entry["id"]) in self.jobs:
            return
        if sum(k[0] == "download" for k in self.jobs) >= 2:
            self.states[entry["id"]] = "limit"
            self.changed.emit()
            return
        self.states[entry["id"]] = "downloading"
        self.progress[entry["id"]] = 0, entry["bytes"]
        target = self.store.directory / "wallpapers" / local_id(entry) / entry["filename"]
        if not target.resolve().is_relative_to(self.store.directory / "wallpapers"):
            self.states[entry["id"]] = "disk"
            self.changed.emit()
            return

        def completed(path):
            try:
                item = self.register(entry, path)
                self.states.pop(entry["id"], None)
                self.imported.emit(item)
            except (OSError, ValueError):
                path.unlink(missing_ok=True)
                self.states[entry["id"]] = "disk"
            self.changed.emit()

        def failed(reason):
            if cached is not None and reason in ("integrity", "size"):
                self.invalid_previews.add(cached)
            self.states[entry["id"]] = reason
            self.changed.emit()
        preview = self.preview_path(entry)
        cached = preview if not entry.get("preview") and preview.is_file() and preview not in self.invalid_previews else None
        self.run_job(("download", entry["id"]), entry["download_url"], target, entry["bytes"], completed,
                     failed=failed, cached=cached, size=entry["bytes"], digest=checksum_for(entry)[1],
                     algorithm=checksum_for(entry)[0], kind=entry["kind"])
        self.changed.emit()

    def register(self, entry, path):
        ident = local_id(entry)
        relative = path.relative_to(self.store.directory)
        if not managed_wallpaper(relative, ident) or not path.resolve().is_relative_to(self.store.directory / "wallpapers"):
            raise ValueError("Invalid managed path")
        item = dict(id=ident, name=entry["title"]["zh"], path=str(relative), kind=entry["kind"], builtin=False,
                    online_id=entry["id"], online_revision=entry["revision"], online_checksum=checksum_key(entry),
                    online_sha256=entry.get("sha256", ""),
                    online_titles=entry["title"], origin=entry, verified_mtime=path.stat().st_mtime_ns)
        old = self.store.settings.library
        self.store.settings.library = [i for i in old if i["id"] != ident] + [item]
        origin = path.parent / "origin.json"
        temporary = path.parent / ".origin.tmp"
        try:
            temporary.write_text(json.dumps(entry, ensure_ascii=False, indent=2), encoding="utf-8")
            os.replace(temporary, origin)
            self.store.save()
        except OSError:
            self.store.settings.library = old
            temporary.unlink(missing_ok=True)
            origin.unlink(missing_ok=True)
            raise
        return item

    def download_progress(self, ident, received, total):
        self.progress[ident] = received, total
        self.changed.emit()

    def cancel_download(self, entry):
        self.cancel_job(("download", entry["id"]))
        self.states.pop(entry["id"], None)
        self.changed.emit()

    def preview_path(self, entry):
        media = entry.get("preview") or entry
        return self.directory / "previews" / (checksum_key(media) + Path(media["filename"]).suffix.lower())

    def has_preview(self, entry):
        path = self.preview_path(entry)
        try:
            return path not in self.invalid_previews and path.stat().st_size == (entry.get("preview") or entry)["bytes"]
        except OSError:
            return False

    def preview_media(self, entry):
        self.cancel_preview()
        item = self.local_item(entry)
        media = entry if item else (entry.get("preview") or entry)
        path = self.store.path(item) if item else self.preview_path(entry)
        if path not in self.invalid_previews and path.is_file() and path.stat().st_size == media["bytes"]:
            self.preview_ready.emit(entry["id"], str(path))
            return
        if media["bytes"] > MAX_PREVIEW_BYTES:
            self.preview_failed.emit(entry["id"])
            return
        def ready(path):
            self.invalid_previews.discard(path)
            self.prune("previews", 128 * 1024 * 1024)
            self.preview_ready.emit(entry["id"], str(path))
        self.run_job(("preview", entry["id"]), media["download_url"], path, media["bytes"],
            ready,
            failed=lambda _reason: self.preview_failed.emit(entry["id"]),
            size=media["bytes"], digest=checksum_for(media)[1], algorithm=checksum_for(media)[0], kind=entry["kind"])
        self.prune("previews", 128 * 1024 * 1024)

    def cancel_preview(self):
        for key in list(self.jobs):
            if key[0] == "preview":
                self.cancel_job(key)

    def pause_browsing(self):
        self.thumbnails_active = False
        self.thumb_queue.clear()
        self.cancel_preview()
        for key in list(self.jobs):
            if key[0] == "cover":
                self.cancel_job(key)

    def cancel_job(self, key):
        job = self.jobs.pop(key, None)
        if job:
            job.cancel()
            job.deleteLater()

    def prune(self, folder, limit):
        try:
            files = sorted((self.directory / folder).glob("*"), key=lambda p: p.stat().st_mtime, reverse=True)
            total = 0
            active = {job.target for job in self.jobs.values()}
            for path in files:
                if not path.is_file() or path.name.startswith("."):
                    continue
                total += path.stat().st_size
                if total > limit and path not in active:
                    path.unlink(missing_ok=True)
        except OSError:
            pass

    def close(self):
        self.thumbnails_active = False
        self.thumb_queue.clear()
        for key in list(self.jobs):
            self.cancel_job(key)
