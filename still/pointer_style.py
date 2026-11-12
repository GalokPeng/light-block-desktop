"""Opt-in native cursor themes and a bounded, idle-stopping pointer trail."""
import logging
from pathlib import Path
import re
import sys
from time import monotonic
import math

from PySide6.QtCore import QObject, QPoint, QPointF, QRect, QRectF, Qt, QProcess, QTimer, Signal
from PySide6.QtGui import QColor, QCursor, QPainter, QPen
from PySide6.QtWidgets import QApplication, QWidget

from .i18n import tr
from .model import ASSETS, ROOT
from .scroll_wheel import MouseHook

from .cursor_art import THEMES as ART_THEMES
from .cursor_cache import CursorBuild
from .pointer_particles import ParticleField, LIFETIMES, STYLES
from .cursor_outline import CursorOutlineCache, paint_outline

THEMES = ("system",) + ART_THEMES


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
        self.field = ParticleField()
        self.color = QColor("#a6e4ca")
        self.outlines = CursorOutlineCache()
        self.outline = None
        self.timer = QTimer(self, interval=33)
        self.timer.timeout.connect(self.sample)
        self.hook = MouseHook({0x0200}, self.moved)
        self.wake.connect(self.begin, Qt.ConnectionType.QueuedConnection)

    def set_enabled(self, enabled):
        self.enabled = bool(enabled)
        self.timer.stop()
        self.hook.close()
        self.hide()
        self.field.clear()
        self.pending = False
        self.outline = None
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

    @property
    def points(self):
        return self.field.points

    def configure(self, style, color):
        if style != self.field.style:
            self.field = ParticleField(style)
        self.color = QColor(color)
        self.outlines.clear()

    def sample(self):
        if not self.enabled:
            return
        now, point = monotonic(), QCursor.pos()
        if not self.field.step(now, (point.x(),point.y())):
            self.hide()
            self.timer.stop()
            self.hook.start()
            return
        if self.field.style == "glow":
            screen = QApplication.screenAt(point)
            scale = screen.devicePixelRatio() if screen else 1.
            self.outline = self.outlines.current(scale, self.color)
            if self.outline is None:
                self.hide()
                return
            origin = QPointF(point)-self.outline.hotspot
            bounds = QRectF(origin,self.outline.glow.deviceIndependentSize()).toAlignedRect()
        else:
            xs, ys = [p.x for p in self.points], [p.y for p in self.points]
            bounds = QRect(math.floor(min(xs))-10, math.floor(min(ys))-10,
                           math.ceil(max(xs)-min(xs))+21, math.ceil(max(ys)-min(ys))+21)
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
        style = self.field.style
        for particle in self.points:
            age = min(1.,max(0.,(now-particle.born)/LIFETIMES[style]))
            center = QPointF(particle.x-self.x(),particle.y-self.y())
            color = QColor(self.color)
            painter.setPen(Qt.PenStyle.NoPen)
            if style == "glow":
                if self.outline is not None:
                    paint_outline(painter,self.outline,center,self.color,now,(1-age)**.8)
            elif style == "fall":
                fade=min(1,age/.08)*(1-age)**1.6
                radius=.55+.3*(.5+.5*math.sin(particle.phase))
                glow=QColor(color);glow.setAlpha(round(28*fade))
                painter.setBrush(glow);painter.drawEllipse(center,radius*3.2,radius*3.2)
                color.setAlpha(round((185+25*math.sin(now*5+particle.phase)**2)*fade));painter.setBrush(color)
                painter.drawEllipse(center,radius,radius)
                color.setAlpha(round(60*fade))
                painter.setPen(QPen(color,.55,Qt.PenStyle.SolidLine,Qt.PenCapStyle.RoundCap))
                painter.drawLine(center,center-QPointF(0,min(1.6,max(0,particle.vy)/80)))
            else:
                color.setAlpha(round(150*(1-age)**2));painter.setBrush(color)
                radius=1.2+1.7*(1-age);painter.drawEllipse(center,radius,radius)

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
        self.builder = None
        self.builds = set()
        self.pending_directory = None
        self.pending_color = None
        self.pending_scale = None
        self.actual_theme = "system"
        self.buffer = b""
        self.closing = False
        self.intentional = set()
        self.timeout = QTimer(self, singleShot=True, interval=8000)
        self.timeout.timeout.connect(lambda: self.failed(tr("鼠标主题应用超时，请重试。")))

    @property
    def active(self):
        return self.pending is not None or self.process is not None or bool(self.trail and self.trail.enabled)

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
                    self.trail.configure(self.settings.particle_style, self.settings.particle_color)
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

    def set_particle_options(self, style=None, color=None):
        if style is not None:
            if style not in STYLES:
                return
            self.settings.particle_style = style
        if color is not None:
            if not re.fullmatch(r"#[0-9a-fA-F]{6}", color):
                return
            self.settings.particle_color = color.lower()
        if self.trail:
            self.trail.configure(self.settings.particle_style, self.settings.particle_color)
        self.controller.store.save()
        self.changed.emit()

    def set_color(self, color):
        if not re.fullmatch(r"#[0-9a-fA-F]{6}", color) or self.pending is not None:
            return
        if self.actual_theme == "system":
            self.settings.cursor_color = color.lower()
            self.controller.store.save()
            self.changed.emit()
        else:
            self.set_theme(self.actual_theme, color.lower())

    def set_size(self, percent):
        if type(percent) is not int or not 75 <= percent <= 200 or self.pending is not None:
            return
        if self.actual_theme == "system":
            self.settings.cursor_scale = percent
            self.controller.store.save()
            self.changed.emit()
        else:
            self.set_theme(self.actual_theme, scale=percent)

    def set_theme(self, theme, color=None, scale=None):
        if self.closing or theme not in THEMES or self.pending is not None or self.process in self.intentional:
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
        self.pending_color = color or self.settings.cursor_color
        self.pending_scale = scale if scale is not None else self.settings.cursor_scale
        self.pending_directory = None
        if theme != "system":
            job = CursorBuild(self.controller.store.directory/"cursor-cache", theme, self.pending_color, self)
            self.builder = job
            self.builds.add(job)
            job.ready.connect(lambda path, j=job: self.built(j, path))
            job.failed.connect(lambda message, j=job: self.build_failed(j, message))
            job.finished.connect(lambda j=job: self.build_finished(j))
            job.start()
        else:
            self.start_native(helper)
        self.changed.emit()

    def built(self, job, path):
        if self.closing or job is not self.builder or self.pending is None:
            return
        self.builder = None
        self.pending_directory = path
        self.start_native(pointer_host())

    def build_failed(self, job, message):
        if self.closing or job is not self.builder:
            return
        logging.warning("Could not prepare cursor artwork: %s", message)
        self.builder = None
        self.pending = self.pending_color = None
        self.pending_scale = None
        self.error.emit(tr("无法生成自定义指针，请重试。"))
        self.changed.emit()

    def build_finished(self, job):
        self.builds.discard(job)
        job.deleteLater()

    def start_native(self, helper):
        if helper is None:
            self.failed(tr("鼠标主题组件不可用，请重新安装。"))
            return
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

    def send(self):
        if self.process is not None and self.pending is not None:
            if self.pending != "system" and self.pending_directory is None:
                return
            command = "RESTORE\n" if self.pending == "system" else "APPLY\t"+self.pending_directory+"\t"+str(self.pending_scale)+"\n"
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
                if self.pending_color:
                    self.settings.cursor_color = self.pending_color
                if self.pending_scale:
                    self.settings.cursor_scale = self.pending_scale
                self.pending = None
                self.pending_color = self.pending_directory = None
                self.pending_scale = None
                if self.trail:
                    self.trail.outlines.clear()
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
        self.builder = None
        self.pending_scale = None
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
                self.builder=None
                self.pending_scale=None
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
        self.builder = None
        self.pending_scale = None
        for job in tuple(self.builds):
            job.requestInterruption()
        for job in tuple(self.builds):
            job.wait()  # Finite local QImage generation, never a network/native call.
        if self.trail:
            self.trail.close_effect()
            self.trail=None
        self.stop_native(wait=True)
