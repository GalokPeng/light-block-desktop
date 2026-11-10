"""Manage the native taskbar helper from the Qt event loop."""
from __future__ import annotations

from ctypes import wintypes as W
import json
from pathlib import Path
import sys

from PySide6.QtCore import QObject, QProcess, QTimer, Signal

from .desktop_windows import ShellEvents
from .i18n import tr
from .model import ROOT
from .taskbar_reveal import NativeTaskbarReveal


class TaskbarEvents(ShellEvents):
    def nativeEventFilter(self, event_type, message):
        msg = W.MSG.from_address(int(message))
        if msg.message in (0x001A, 0x007E, 0x02E0, 0x031A):  # settings, display, DPI, theme
            self.watcher.changed.emit()
        return super().nativeEventFilter(event_type, message)


class NativeTaskbarStyle(QObject):
    changed = Signal()
    failed = Signal(str)
    status = Signal()

    def __init__(self, app, settings):
        super().__init__(app)
        self.app, self.settings = app, settings
        self.stopping = False
        self.pending = False
        self.dirty = False
        self.auto_hide = False
        self.centered = False
        self.reveal = NativeTaskbarReveal(self)
        self.reveal.changed.connect(self.refresh)
        self.retries = 0
        self.events = TaskbarEvents(self)
        self.timer = QTimer(self, singleShot=True, interval=80)
        self.timer.timeout.connect(self.refresh)
        self.changed.connect(self.schedule_refresh)
        self.process = QProcess(self)
        self.process.started.connect(self.refresh)
        self.process.readyReadStandardOutput.connect(self.read_reply)
        self.process.errorOccurred.connect(lambda *_: self.fail("failed"))
        self.process.finished.connect(lambda *_: self.fail("failed"))

    def start(self):
        self.app.installNativeEventFilter(self.events)
        args = ["--native-taskbar-style-helper"]
        executable = sys.executable
        if not getattr(sys, "frozen", False):
            executable = str(Path(executable).with_name("pythonw.exe"))
            args.insert(0, str(ROOT / "main.py"))
        self.process.start(executable, args)

    def schedule_refresh(self):
        if not self.stopping and not self.timer.isActive():
            self.timer.start(80)

    def refresh(self):
        if self.stopping or self.process.state() != QProcess.ProcessState.Running:
            return
        if self.pending:
            self.dirty = True
            return
        self.dirty = False
        self.pending = True
        request = {"radius": self.settings.taskbar_radius, "hidden": self.reveal.hidden,
                   "revealed": self.reveal.revealed}
        self.process.write(json.dumps(request).encode("ascii") + b"\n")

    def read_reply(self):
        while not self.stopping and self.process.canReadLine():
            self.pending = False
            try:
                reply = json.loads(bytes(self.process.readLine()))
            except ValueError:
                self.fail("failed")
                return
            if "error" in reply:
                self.fail(reply["error"])
                return
            if reply.get("waiting"):
                self.retries += 1
                if self.retries > 12:
                    self.fail("failed")
                else:
                    self.timer.start(250)
                return
            self.retries = 0
            auto_hide, centered = reply["auto_hide"], reply.get("centered", False)
            self.reveal.set_target(reply.get("visibility"))
            if (auto_hide, centered) != (self.auto_hide, self.centered):
                self.auto_hide, self.centered = auto_hide, centered
                self.status.emit()
            if self.dirty:
                self.refresh()

    def fail(self, code):
        if self.stopping:
            return
        if code == "busy":
            message = tr("已有另一个轻块桌面实例在美化任务栏。")
        elif code == "occupied":
            message = tr("原生任务栏已有自定义形状，请关闭其他任务栏美化工具。")
        elif code == "xaml":
            message = tr("无法连接 Explorer 的原生任务栏布局。")
        elif code == "xaml_restarted":
            message = tr("Explorer 已重启，原生任务栏预览已关闭。")
        else:
            message = tr("无法启动任务栏美化，Windows 任务栏已保留。")
        self.failed.emit(message)

    def stop(self):
        self.stopping = True
        self.timer.stop()
        self.reveal.stop()
        self.app.removeNativeEventFilter(self.events)
        if self.process.state() != QProcess.ProcessState.NotRunning:
            self.process.closeWriteChannel()
            self.process.waitForFinished(3600)
        self.deleteLater()
