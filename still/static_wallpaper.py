"""Isolate blocking image I/O and Windows wallpaper broadcasts from the GUI."""
from __future__ import annotations

import json
import logging
from pathlib import Path
import shutil
import sys
from time import monotonic
import uuid

from PySide6.QtCore import QObject, QProcess, QProcessEnvironment, QTimer, Signal

from .i18n import display_language, set_language, tr


def prepare(request):
    from PySide6.QtGui import QImageReader
    from . import win32

    directory = Path(request["directory"])
    directory.mkdir(parents=True, exist_ok=True)
    if request.get("restore"):
        original = request["original"]
        return original["path"], original
    path = Path(request["path"])
    if not path.is_file():
        raise FileNotFoundError(tr("原文件已移动或删除，请重新导入。"))
    reader = QImageReader(str(path))
    reader.setAutoTransform(True)
    if not reader.canRead():
        raise ValueError(tr("无法读取这张图片，请检查文件内容。"))
    native_path = path
    convert = path.suffix.lower() not in {".jpg", ".jpeg", ".png", ".bmp"}
    current = win32.current_wallpaper() if convert or request.get("original") is None else None
    if convert:
        image = reader.read()
        if image.isNull():
            raise ValueError(reader.errorString())
        # Alternate two full-resolution files so failed preparation never
        # overwrites the converted image Windows is currently displaying.
        native_path = directory / "native-wallpaper-a.png"
        if current["path"] and Path(current["path"]).resolve() == native_path.resolve():
            native_path = directory / "native-wallpaper-b.png"
        temporary = directory / ("native-wallpaper-" + uuid.uuid4().hex + ".tmp")
        try:
            if not image.save(str(temporary), "PNG"):
                raise OSError(tr("无法写入原分辨率 PNG。"))
            temporary.replace(native_path)
        finally:
            temporary.unlink(missing_ok=True)
    original = request.get("original")
    if original is None:
        original = current
        source = Path(original["path"])
        if original["path"] and source.is_file():
            backup = directory / ("original-wallpaper" + (source.suffix or ".bmp"))
            if source.resolve() != backup.resolve():
                temporary = directory / ("original-wallpaper-" + uuid.uuid4().hex + ".tmp")
                try:
                    shutil.copyfile(source, temporary)
                    temporary.replace(backup)
                finally:
                    temporary.unlink(missing_ok=True)
            original["path"] = str(backup)
    return str(native_path), original


def run_helper():
    from PySide6.QtCore import QCoreApplication
    from .taskbar_style_native import pipe_stream
    from . import win32

    app = QCoreApplication(sys.argv[:1])  # Image-format plugin discovery, no GUI.
    reader, writer = pipe_stream(sys.stdin, -10, "rb"), pipe_stream(sys.stdout, -11, "wb")

    def reply(message):
        writer.write(json.dumps(message, ensure_ascii=True).encode("ascii") + b"\n")
        writer.flush()

    try:
        request = json.loads(reader.readline(262145))
        set_language(request.get("language", "system"))
        if request.get("fit") not in {"fill", "fit", "stretch", "center"}:
            raise ValueError("Invalid wallpaper fit")
        path, original = prepare(request)
        reply({"phase": "prepared", "original": original})
        # The GUI must persist the original before any system setting changes.
        if reader.readline(32) != b"apply\n":
            return 1
        reply({"phase": "windows"})
        win32.set_wallpaper(path, request["fit"], original=original if request.get("restore") else None)
        reply({"phase": "done", "ok": True})
        return 0
    except Exception as exc:
        reply({"phase": "done", "ok": False, "error": str(exc)[:2048]})
        return 1


class StaticWallpaperTask(QObject):
    prepared = Signal(object)
    finished = Signal(bool, str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.process = None
        self.timer = QTimer(self, singleShot=True)
        self.timer.timeout.connect(lambda: self.abort(tr("Windows 壁纸切换超时，请重试。")))
        self.buffer = b""
        self.outcome = None
        self.started_at = 0.
        self.phase = "idle"
        self.aborted = False

    @property
    def busy(self):
        return self.process is not None

    def command(self):
        args = ["--static-wallpaper-helper"]
        if not getattr(sys, "frozen", False):
            args.insert(0, str(Path(__file__).resolve().parents[1] / "main.py"))
        return sys.executable, args

    def start(self, request):
        if self.busy:
            raise RuntimeError(tr("正在切换壁纸，请稍候再试。"))
        self.buffer, self.outcome = b"", None
        self.aborted = False
        self.started_at, self.phase = monotonic(), "prepare"
        process = QProcess(self)
        self.process = process
        environment = QProcessEnvironment.systemEnvironment()
        environment.insert("PYINSTALLER_RESET_ENVIRONMENT", "1")
        process.setProcessEnvironment(environment)
        payload = json.dumps(dict(request, language=display_language())).encode("ascii") + b"\n"
        process.started.connect(lambda p=process: p.write(payload))
        process.readyReadStandardOutput.connect(lambda p=process: self.read(p))
        process.readyReadStandardError.connect(lambda p=process: logging.warning(
            "Static wallpaper helper: %s", bytes(p.readAllStandardError()).decode("utf-8", errors="replace")[-4096:]))
        process.finished.connect(lambda code, _status, p=process: self.ended(p, code))
        process.errorOccurred.connect(lambda error, p=process: self.failed_start(p, error))
        self.timer.start(30000)
        logging.info("Wallpaper switch started: preparing static image in helper")
        process.start(*self.command())

    def read(self, process):
        if process is not self.process:
            return
        self.buffer += bytes(process.readAllStandardOutput())
        if self.aborted:
            self.buffer = b""
            return
        if len(self.buffer) > 262144:
            self.abort(tr("壁纸切换任务返回了无效结果。"))
            return
        while b"\n" in self.buffer:
            line, self.buffer = self.buffer.split(b"\n", 1)
            try:
                message = json.loads(line)
                phase = message["phase"]
                if phase == "prepared":
                    self.prepared.emit(message["original"])
                elif phase == "windows":
                    self.phase = "windows"
                    logging.info("Wallpaper switch: Windows call started after %.3fs", monotonic() - self.started_at)
                elif phase == "done":
                    self.outcome = (message.get("ok") is True, str(message.get("error", "")))
                else:
                    raise ValueError("Unknown helper phase")
            except (ValueError, KeyError, TypeError):
                self.abort(tr("壁纸切换任务返回了无效结果。"))
                return

    def acknowledge(self):
        if self.process is not None:
            self.timer.start(15000)
            self.process.write(b"apply\n")
            self.process.closeWriteChannel()

    def abort(self, message=""):
        if self.process is not None:
            self.aborted = True
            self.outcome = (False, message)
            logging.warning("Wallpaper switch cancelled/failed at %s after %.3fs", self.phase, monotonic() - self.started_at)
            self.process.kill()

    def failed_start(self, process, error):
        if error == QProcess.ProcessError.FailedToStart and process is self.process:
            self.outcome = (False, process.errorString())
            self.ended(process, -1)

    def ended(self, process, code):
        if process is not self.process:
            return
        self.read(process)
        success, error = self.outcome or (False, tr("壁纸切换任务意外结束，请重试。"))
        success = success and code == 0
        if not success and not error and not self.aborted:
            error = tr("壁纸切换任务意外结束，请重试。")
        self.timer.stop()
        self.process = None
        process.deleteLater()
        logging.info("Wallpaper switch completed: success=%s phase=%s elapsed=%.3fs", success, self.phase, monotonic() - self.started_at)
        self.finished.emit(success, error)

    def close(self):
        self.timer.stop()
        if self.process is not None:
            process, self.process = self.process, None
            process.blockSignals(True)
            process.kill()
            process.waitForFinished(1000)
            process.deleteLater()
