"""Asynchronous lookup of the latest official Windows Setup."""
import re
from time import monotonic

from PySide6.QtCore import QObject, QUrl, Signal
from PySide6.QtGui import QDesktopServices
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest

from .model import VERSION

REPO_SLUG = "/GalokPeng/light-block-desktop"
REPO_URL = f"https://github.com{REPO_SLUG}"
REPO_PATH = f"{REPO_SLUG}/releases"
DOWNLOAD_URL = "https://light-block-desktop.vercel.app/download/"
SETUP_NAME = "Qingkuai-Setup-x64.exe"
LATEST_SETUP = f"https://github.com{REPO_PATH}/latest/download/{SETUP_NAME}"
VERSIONED_SETUP = re.compile(
    rf"{REPO_PATH}/download/v([0-9]{{1,5}})\.([0-9]{{1,5}})\.([0-9]{{1,5}})/{re.escape(SETUP_NAME)}"
)
RELEASE_TAG = re.compile(r"v([0-9]{1,5})\.([0-9]{1,5})\.([0-9]{1,5})")


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

    def __init__(self, parent):
        super().__init__(parent)
        self.network = QNetworkAccessManager(self)
        self.reply = None
        self.state = "idle"
        self.version = None
        self.checked_at = None

    def check(self, force=False):
        if self.reply is not None:
            return
        interval = 180 if self.state == "error" else 3600
        if not force and self.checked_at is not None and monotonic() - self.checked_at < interval:
            return
        self.request(QUrl(DOWNLOAD_URL), self.resolve_site, QNetworkRequest.RedirectPolicy.ManualRedirectPolicy)
        self.state = "checking"
        self.changed.emit()

    def request(self, url, finished, redirect_policy):
        request = QNetworkRequest(url)
        request.setAttribute(QNetworkRequest.Attribute.RedirectPolicyAttribute, redirect_policy)
        request.setRawHeader(b"User-Agent", f"Light-Block-Desktop/{VERSION}".encode("ascii"))
        request.setTransferTimeout(8000)
        self.reply = self.network.head(request)
        self.reply.finished.connect(finished)

    def take_reply(self):
        reply, self.reply = self.reply, None
        reply.deleteLater()
        return reply

    def resolve_site(self):
        reply = self.take_reply()
        tag = bytes(reply.rawHeader("X-Qingkuai-Version")).decode("ascii", errors="replace")
        if reply.error() == QNetworkReply.NetworkError.NoError and tag:
            try:
                version = newer_version(tag, VERSION)
            except ValueError:
                pass
            else:
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
        if self.state == "update":
            return QDesktopServices.openUrl(QUrl(DOWNLOAD_URL))
        self.check(force=True)
        return True
