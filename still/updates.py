"""Discover, download, verify and install the latest official Windows Setup."""
import json
import os
from pathlib import Path
import re
from time import monotonic

from PySide6.QtCore import QObject, QUrl, Signal
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest

from .model import VERSION
from .i18n import display_language, tr
from .update_download import MAX_SETUP_BYTES, SetupDownload
from .update_installer import UpdateInstaller

REPO_SLUG = "/GalokPeng/light-block-desktop"
REPO_URL = f"https://github.com{REPO_SLUG}"
REPO_PATH = f"{REPO_SLUG}/releases"
DOWNLOAD_URL = "https://light-block-desktop.vercel.app/download/"
METADATA_URL = DOWNLOAD_URL + "?format=json"
SETUP_NAME = "Qingkuai-Setup-x64.exe"
LATEST_SETUP = f"https://github.com{REPO_PATH}/latest/download/{SETUP_NAME}"
VERSIONED_SETUP = re.compile(
    rf"{REPO_PATH}/download/v([0-9]{{1,5}})\.([0-9]{{1,5}})\.([0-9]{{1,5}})/{re.escape(SETUP_NAME)}"
)
RELEASE_TAG = re.compile(r"v([0-9]{1,5})\.([0-9]{1,5})\.([0-9]{1,5})")
MIRRORS = (("gh-proxy.com", "https://gh-proxy.com"), ("ghfast.top", "https://ghfast.top"))


def setup_sources(version, language):
    if not RELEASE_TAG.fullmatch(version):
        raise ValueError("Invalid release version")
    official = f"{REPO_URL}/releases/download/{version}/{SETUP_NAME}"
    mirrors = [(name, origin + "/" + official) for name, origin in MIRRORS]
    return mirrors + [("GitHub", official)] if language == "zh" else [("GitHub", official)] + mirrors


def release_metadata(body):
    if len(body) > 64 * 1024:
        raise ValueError("Oversized release metadata")
    data = json.loads(body)
    if not isinstance(data, dict):
        raise ValueError("Invalid release metadata")
    version, digest, size = data.get("version"), data.get("sha256"), data.get("size")
    if not isinstance(version, str) or not RELEASE_TAG.fullmatch(version):
        raise ValueError("Invalid release version")
    if data.get("name") != SETUP_NAME or data.get("url") != setup_sources(version, "en")[0][1]:
        raise ValueError("Unexpected installer")
    if not isinstance(digest, str) or not re.fullmatch(r"[a-f0-9]{64}", digest):
        raise ValueError("Missing official checksum")
    if size is not None and (type(size) is not int or not 2 <= size <= MAX_SETUP_BYTES):
        raise ValueError("Invalid Setup size")
    return version, digest, size or 0


def newer_version(tag, current):
    match = RELEASE_TAG.fullmatch(tag)
    if not match:
        raise ValueError("Invalid release version")
    return tag if tuple(map(int, match.groups())) > tuple(map(int, current.split("."))) else None


def available_version(target, current):
    match = VERSIONED_SETUP.fullmatch(target.path())
    if target.scheme() != "https" or target.host() != "github.com" or not match:
        raise ValueError("Expected an official Windows Setup URL")
    return newer_version("v" + ".".join(match.groups()), current)


class ReleaseCheck(QObject):
    changed = Signal()
    failed = Signal(str)
    downloaded = Signal(str)
    install_ready = Signal()

    def __init__(self, parent, directory=None):
        super().__init__(parent)
        self.network = QNetworkAccessManager(self)
        self.reply = None
        self.state = "idle"
        self.version = None
        self.checked_at = None
        self.sha256, self.size = None, 0
        self.received = self.total = 0
        self.source, self.error = "", ""
        self.setup = None
        self.body = bytearray()
        self.oversized = False
        cache = directory or Path(os.environ["LOCALAPPDATA"]) / "StillWallpaper" / "updates"
        self.download = SetupDownload(cache, self, self.network)
        self.download.progress.connect(self.download_progress)
        self.download.source_changed.connect(self.download_source)
        self.download.verifying.connect(lambda: self.set_state("verifying"))
        self.download.ready.connect(self.download_ready)
        self.download.failed.connect(self.download_failed)
        self.download.cancelled.connect(lambda: self.set_state("update"))
        self.installer = UpdateInstaller(self)
        self.installer.prepared.connect(self.prepared)
        self.installer.failed.connect(self.download_failed)

    @property
    def busy(self):
        return self.state in {"downloading", "verifying", "preparing", "installing"}

    def set_state(self, state):
        self.state = state
        self.changed.emit()

    def check(self, force=False):
        if self.reply is not None or self.busy:
            return
        interval = 180 if self.state == "error" else 3600
        if not force and self.checked_at is not None and monotonic() - self.checked_at < interval:
            return
        self.sha256, self.size = None, 0
        self.request(QUrl(METADATA_URL), self.resolve_site, QNetworkRequest.RedirectPolicy.NoLessSafeRedirectPolicy, get=True)
        self.state = "checking"
        self.changed.emit()

    def request(self, url, finished, redirect_policy, *, get=False):
        request = QNetworkRequest(url)
        request.setAttribute(QNetworkRequest.Attribute.RedirectPolicyAttribute, redirect_policy)
        request.setRawHeader(b"User-Agent", f"Light-Block-Desktop/{VERSION}".encode("ascii"))
        request.setTransferTimeout(8000)
        self.body, self.oversized = bytearray(), False
        self.reply = self.network.get(request) if get else self.network.head(request)
        reply = self.reply
        if get:
            reply.setReadBufferSize(64 * 1024)
            reply.readyRead.connect(lambda r=reply: self.read_body() if self.reply is r else None)
        reply.finished.connect(lambda r=reply: finished() if self.reply is r else None)

    def read_body(self):
        if self.reply is None:
            return
        self.body.extend(bytes(self.reply.readAll()))
        if len(self.body) > 64 * 1024:
            self.oversized = True
            self.body.clear()
            self.reply.abort()

    def take_reply(self):
        reply, self.reply = self.reply, None
        reply.deleteLater()
        return reply

    def resolve_site(self):
        self.read_body()
        reply = self.take_reply()
        if reply.error() == QNetworkReply.NetworkError.NoError and not self.oversized:
            try:
                tag, digest, size = release_metadata(bytes(self.body))
                version = newer_version(tag, VERSION)
            except (ValueError, TypeError, AttributeError):
                pass
            else:
                self.sha256, self.size = digest, size
                self.finish("update" if version else "latest", version)
                return
        self.request(QUrl(LATEST_SETUP), self.resolve_latest, QNetworkRequest.RedirectPolicy.ManualRedirectPolicy)

    def resolve_latest(self):
        reply = self.take_reply()
        redirect = reply.attribute(QNetworkRequest.Attribute.RedirectionTargetAttribute)
        if reply.error() != QNetworkReply.NetworkError.NoError or not isinstance(redirect, QUrl):
            self.finish("error")
            return
        target = reply.url().resolved(redirect)
        try:
            version = available_version(target, VERSION)
        except ValueError:
            self.finish("error")
            return
        if version is None:
            self.finish("latest")
        else:
            self.request(target, lambda: self.verify_asset(version),
                         QNetworkRequest.RedirectPolicy.NoLessSafeRedirectPolicy)

    def verify_asset(self, version):
        reply = self.take_reply()
        disposition = bytes(reply.rawHeader("Content-Disposition"))
        if reply.error() == QNetworkReply.NetworkError.NoError and disposition.lower().startswith(b"attachment"):
            self.finish("update", version)
        else:
            self.finish("error")

    def finish(self, state, version=None):
        self.checked_at = monotonic()
        self.state, self.version = state, version
        self.changed.emit()

    def activate(self):
        if self.state == "downloading":
            self.download.cancel()
            return True
        if self.state in {"update", "download_error"} and self.version:
            if self.sha256:
                self.start_download()
            elif self.reply is None:
                url = f"{REPO_URL}/releases/download/{self.version}/Qingkuai-{self.version}-SHA256SUMS.txt"
                self.set_state("verifying")
                self.request(QUrl(url), self.resolve_checksum, QNetworkRequest.RedirectPolicy.NoLessSafeRedirectPolicy, get=True)
            return True
        if self.busy:
            return True
        self.check(force=True)
        return True

    def resolve_checksum(self):
        self.read_body()
        reply = self.take_reply()
        entries = {}
        if reply.error() == QNetworkReply.NetworkError.NoError and not self.oversized:
            for line in self.body.decode("ascii", errors="replace").splitlines():
                match = re.fullmatch(r"([a-fA-F0-9]{64}) {2}([A-Za-z0-9._-]+)", line)
                if match:
                    entries[match[2]] = match[1].lower()
        self.sha256 = entries.get(SETUP_NAME) or entries.get(f"Qingkuai-{self.version}-Setup-x64.exe")
        if self.sha256:
            self.start_download()
        else:
            self.download_failed("checksum")

    def start_download(self):
        self.error = ""
        self.received = self.total = 0
        self.set_state("downloading")
        self.download.start(self.version, self.sha256, setup_sources(self.version, display_language()), self.size)

    def download_progress(self, received, total):
        self.received, self.total = received, total
        self.set_state("downloading")

    def download_source(self, source):
        self.source = source
        self.set_state("downloading")

    def download_ready(self, path):
        self.setup = path
        self.set_state("preparing")
        self.downloaded.emit(path)

    def prepare_install(self, path, profile):
        self.installer.prepare(path, self.sha256, profile, display_language())

    def prepared(self):
        self.set_state("installing")
        self.install_ready.emit()

    def download_failed(self, reason):
        messages = {
            "disk": tr("无法保存更新安装包，请检查磁盘空间和写入权限。"),
            "prepare": tr("无法启动更新安装程序，软件已保留运行。请重试更新。"),
            "integrity": tr("安装包校验失败，未运行任何安装程序。请重试更新。"),
            "checksum": tr("无法取得官方安装包校验值，请稍后重试更新。"),
        }
        self.error = messages.get(reason, tr("更新下载失败，请检查网络后重试。"))
        self.set_state("download_error")
        self.failed.emit(self.error)

    def close(self):
        if self.reply is not None:
            reply, self.reply = self.reply, None
            reply.finished.disconnect()
            reply.abort()
            reply.deleteLater()
        self.download.cancel(notify=False)
        self.installer.cancel()
