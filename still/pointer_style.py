"""Opt-in native cursor themes and a bounded, idle-stopping pointer trail."""
from collections import deque
import logging
from pathlib import Path
import re
import sys
from time import monotonic

from PySide6.QtCore import QObject, QPoint, QRect, Qt, QProcess, QTimer, Signal
from PySide6.QtGui import QColor, QCursor, QPainter
from PySide6.QtWidgets import QWidget

from .i18n import tr
from .model import ASSETS, ROOT
from .scroll_wheel import MouseHook

THEMES = ("system", "frost", "graphite", "mint")


def pointer_host():
    if getattr(sys, "frozen", False):
        path = ROOT / "runtime/QingkuaiPointer.exe"
    else:
        directory = ROOT / "build/pointer-host"
        try:
            name = (directory / "current.txt").read_text(encoding="ascii").strip()
        except OSError:
            return None
        if not re.fullmatch(r"QingkuaiPointer-[0-9a-f]{12}\.exe", name):
            return None
        path = directory / name
    return path if path.is_file() else None


class PointerTrail(QWidget):
    wake = Signal()

    def __init__(self):
        super().__init__(None, Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint |
                         Qt.WindowType.WindowStaysOnTopHint | Qt.WindowType.WindowTransparentForInput |
                         Qt.WindowType.WindowDoesNotAcceptFocus | Qt.WindowType.NoDropShadowWindowHint)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
        self.enabled = self.pending = False
        self.points = deque(maxlen=20)
        self.last = None
        self.color = QColor("#a6e4ca")
        self.timer = QTimer(self, interval=33)
        self.timer.timeout.connect(self.sample)
        self.hook = MouseHook({0x0200}, self.moved)
        self.wake.connect(self.begin, Qt.ConnectionType.QueuedConnection)

    def set_enabled(self, enabled):
        self.enabled = bool(enabled)
        self.timer.stop()
        self.hook.close()
        self.hide()
        self.points.clear()
        self.last = None
        self.pending = False
        if self.enabled and not self.hook.start():
            self.enabled = False
            raise OSError(tr("无法监听鼠标移动，请重试。"))

    def moved(self, _event):
        if self.enabled and not self.pending:
            self.pending = True
            self.wake.emit()

    def begin(self):
        if not self.enabled:
            return
        self.hook.close()  # High-polling-rate mice do not feed Python while drawing.
        self.pending = False
        self.sample()
        if self.points:
            self.timer.start()

    def sample(self):
        if not self.enabled:
            return
        now, point = monotonic(), QCursor.pos()
        if self.last is None or point != self.last:
            if self.last is not None and (point-self.last).manhattanLength() > 240:
                self.points.clear()
            self.points.append((QPoint(point), now))
            self.last = QPoint(point)
        while self.points and now-self.points[0][1] > .42:
            self.points.popleft()
        if not self.points:
            self.hide()
            self.timer.stop()
            self.hook.start()
            return
        xs, ys = [p.x() for p,_ in self.points], [p.y() for p,_ in self.points]
        bounds = QRect(min(xs)-10, min(ys)-10, max(xs)-min(xs)+21, max(ys)-min(ys)+21)
        if bounds.width()>420 or bounds.height()>420:
            self.points.clear()
            return
        self.setGeometry(bounds)
        if not self.isVisible():
            self.show()
        self.update()

    def showEvent(self, event):
        from . import win32
        import ctypes as C
        from ctypes import wintypes as W
        # Exclude the decorative window from captures on supported Windows.
        exclude = win32.bind(win32.user32, "SetWindowDisplayAffinity", W.BOOL, W.HWND, W.DWORD)
        exclude(int(self.winId()), 0x11)
        super().showEvent(event)

    def paintEvent(self, event):
        now = monotonic()
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setPen(Qt.PenStyle.NoPen)
        for point, born in self.points:
            age = min(1., (now-born)/.42)
            color = QColor(self.color)
            color.setAlpha(round(150*(1-age)**2))
            painter.setBrush(color)
            radius = 1.2+1.7*(1-age)
            painter.drawEllipse(point-self.pos(), radius, radius)

    def close_effect(self):
        self.set_enabled(False)
        self.close()
        self.deleteLater()


class PointerStyle(QObject):
    changed = Signal()
    error = Signal(str)

    def __init__(self, controller):
        super().__init__(controller.app)
        self.controller, self.settings = controller, controller.store.settings
        self.process = self.trail = None
        self.pending = None
        self.actual_theme = "system"
        self.buffer = b""
        self.closing = False
        self.intentional = set()
        self.timeout = QTimer(self, singleShot=True, interval=8000)
        self.timeout.timeout.connect(lambda: self.failed(tr("鼠标主题应用超时，请重试。")))

    @property
    def active(self):
        return self.process is not None or bool(self.trail and self.trail.enabled)

    def configure(self):
        self.set_particles(self.settings.cursor_particles)
        if self.settings.cursor_theme != "system":
            self.set_theme(self.settings.cursor_theme)

    def set_particles(self, enabled):
        if enabled and self.trail is None:
            self.trail = PointerTrail()
        try:
            if self.trail is not None:
                if enabled:
                    self.trail.set_enabled(True)
                else:
                    self.trail.close_effect()
                    self.trail = None
            self.settings.cursor_particles = bool(enabled)
            self.controller.store.save()
        except OSError as exc:
            if self.trail:
                self.trail.close_effect()
                self.trail = None
            self.settings.cursor_particles = False
            self.error.emit(str(exc))
        self.changed.emit()

    def set_theme(self, theme):
        if theme not in THEMES or self.pending is not None or self.process in self.intentional:
            return
        if theme == "system" and self.process is None:
            self.settings.cursor_theme = self.actual_theme = "system"
            self.controller.store.save()
            self.changed.emit()
            return
        helper = pointer_host()
        if helper is None:
            self.error.emit(tr("鼠标主题组件不可用，请重新安装。"))
            return
        self.pending = theme
        self.timeout.start()
        if self.process is None:
            process = QProcess(self)
            self.process = process
            self.buffer = b""
            process.started.connect(self.send)
            process.readyReadStandardOutput.connect(lambda p=process: self.read(p))
            process.errorOccurred.connect(lambda error, p=process: self.process_error(p, error))
            process.finished.connect(lambda _code, _status, p=process: self.ended(p))
            process.start(str(helper), [])
        else:
            self.send()
        self.changed.emit()

    def send(self):
        if self.process is not None and self.pending is not None:
            command = "RESTORE\n" if self.pending == "system" else "APPLY\t"+str(ASSETS/"cursors"/self.pending)+"\n"
            self.process.write(command.encode("utf-8"))

    def read(self, process):
        if process is not self.process:
            return
        self.buffer += bytes(process.readAllStandardOutput())
        if len(self.buffer)>4096:
            self.failed(tr("无法应用鼠标主题，请重试。"))
            return
        while b"\n" in self.buffer:
            line, self.buffer = self.buffer.split(b"\n", 1)
            if line == b"OK" and self.pending is not None:
                self.timeout.stop()
                self.settings.cursor_theme = self.actual_theme = self.pending
                self.pending = None
                if self.trail:
                    self.trail.color = QColor("#9ec8e7" if self.actual_theme in ("frost","graphite") else "#a6e4ca")
                try:
                    self.controller.store.save()
                except OSError as exc:
                    self.failed(str(exc))
                    return
                if self.actual_theme == "system":
                    self.stop_native()
                self.changed.emit()
            elif line.startswith((b"ERROR",b"BUSY")):
                logging.warning("Cursor host: %s", line.decode(errors="replace"))
                self.failed(tr("鼠标主题正被另一个轻块实例使用。") if line==b"BUSY" else tr("无法应用鼠标主题，请重试。"))
                return

    def process_error(self, process, error):
        if process is self.process and process not in self.intentional and not self.closing:
            self.failed(tr("无法启动鼠标主题组件。"))
            if error == QProcess.ProcessError.FailedToStart:
                self.ended(process)

    def failed(self, message):
        needs_restore = self.actual_theme != "system" and (
            self.process is None or self.process.state() == QProcess.ProcessState.NotRunning)
        self.timeout.stop()
        self.pending = None
        self.actual_theme = self.settings.cursor_theme = "system"
        self.stop_native()
        if needs_restore and (helper := pointer_host()):
            QProcess.startDetached(str(helper), ["--restore"])
        try:
            self.controller.store.save()
        except OSError:
            pass
        self.error.emit(message)
        self.changed.emit()

    def stop_native(self, wait=False):
        process = self.process
        if process is None:
            return
        self.intentional.add(process)
        if process.state()!=QProcess.ProcessState.NotRunning:
            process.write(b"QUIT\n")
            process.closeWriteChannel()
        if wait:
            if process.state()!=QProcess.ProcessState.NotRunning and not process.waitForFinished(1200):
                self.force_stop(process)
        else:
            QTimer.singleShot(1500, lambda p=process: self.force_stop(p) if p is self.process else None)

    def force_stop(self, process):
        if process.state()!=QProcess.ProcessState.NotRunning:
            process.kill()
            process.waitForFinished(250)
            helper=pointer_host()
            if helper:
                QProcess.startDetached(str(helper),["--restore"])

    def ended(self, process):
        intentional = process in self.intentional
        self.intentional.discard(process)
        if process is self.process:
            self.process = None
            self.timeout.stop()
            if not intentional and not self.closing:
                if self.actual_theme != "system" and (helper := pointer_host()):
                    QProcess.startDetached(str(helper), ["--restore"])
                self.actual_theme=self.settings.cursor_theme="system"
                self.pending=None
                try:
                    self.controller.store.save()
                except OSError:
                    logging.warning("Could not save cursor recovery preference", exc_info=True)
                self.error.emit(tr("鼠标主题组件已退出，请重新选择。"))
            self.changed.emit()
        process.deleteLater()

    def cleanup(self):
        self.closing = True
        self.timeout.stop()
        self.pending = None
        if self.trail:
            self.trail.close_effect()
            self.trail=None
        self.stop_native(wait=True)
