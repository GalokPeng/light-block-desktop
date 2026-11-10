"""Qt side of the Explorer taskbar lease."""
from __future__ import annotations

import json
from pathlib import Path
import sys

from PySide6.QtCore import QProcess, QObject, QTimer, Signal

from .desktop_windows import ShellEvents
from .i18n import tr
from .model import ROOT


class TaskbarStyle(QObject):
    changed = Signal()
    layout_ready = Signal(object)
    failed = Signal(str)

    def __init__(self, dock):
        super().__init__(dock)
        self.app = dock.controller.app
        self.stopping = False
        self.pending = False
        self.dirty = False
        self.retries = 0
        self.span = None
        self.frame = self.accent = None
        self.hidden = True
        self.shell_events = ShellEvents(self)
        self.refresh_timer = QTimer(self, singleShot=True, interval=20)
        self.refresh_timer.timeout.connect(self.refresh)
        self.changed.connect(self.schedule_refresh)
        self.process = QProcess(self)
        self.process.started.connect(self.refresh)
        self.process.readyReadStandardOutput.connect(self.read_replies)
        self.process.errorOccurred.connect(lambda _: self.fail("failed"))
        self.process.finished.connect(lambda *_: self.fail("failed"))
        self.app.installNativeEventFilter(self.shell_events)

    def start(self):
        args = ["--taskbar-style-helper"]
        exe = sys.executable
        if not getattr(sys, "frozen", False):
            exe = str(Path(exe).with_name("pythonw.exe"))
            args.insert(0, str(ROOT / "main.py"))
        self.process.start(exe, args)

    def schedule_refresh(self):
        if not self.stopping and not self.refresh_timer.isActive():
            self.refresh_timer.start(20)

    def set_span(self, span):
        if self.span != span:
            self.span = span
            self.refresh()

    def set_tray_state(self, frame, accent, hidden):
        if (frame, accent, hidden) != (self.frame, self.accent, self.hidden):
            visibility_changed = self.hidden != hidden
            self.frame, self.accent, self.hidden = frame, accent, hidden
            if visibility_changed:
                self.refresh()
            else:
                self.schedule_refresh()

    def refresh(self):
        if (self.stopping or self.parent().screen_area is None
                or self.process.state() != QProcess.ProcessState.Running):
            return
        if self.pending:
            self.dirty = True
            return
        self.pending = True
        request = {"span": self.span, "frame": self.frame, "accent": self.accent, "hidden": self.hidden}
        self.process.write(json.dumps(request).encode("ascii") + b"\n")

    def read_replies(self):
        while self.process.canReadLine() and not self.stopping:
            try:
                result = json.loads(bytes(self.process.readLine()))
            except ValueError:
                self.fail("failed")
                return
            self.pending = False
            if "error" in result:
                self.fail(result["error"])
                return
            if result.get("waiting"):
                if self.parent().screen_area is None:
                    self.retries = 0
                    return
                self.retries += 1
                if self.retries > 12:
                    self.fail("failed")
                else:
                    self.refresh_timer.start(250)
                return
            self.retries = 0
            self.layout_ready.emit(result)
            if self.dirty:
                self.dirty = False
                self.refresh()

    def fail(self, code):
        if not self.stopping:
            if code == "busy":
                message = tr("已有另一个轻块桌面实例在美化任务栏。")
            elif code == "unsupported":
                message = tr("当前任务栏不适用美化。请使用主屏幕底部的原生任务栏，并关闭自动隐藏。")
            else:
                message = tr("无法启动任务栏美化，Windows 任务栏已保留。")
            self.failed.emit(message)

    def stop(self):
        self.stopping = True
        self.refresh_timer.stop()
        self.app.removeNativeEventFilter(self.shell_events)
        if self.process.state() != QProcess.ProcessState.NotRunning:
            # EOF also reaches the helper if the main process crashes.
            self.process.closeWriteChannel()
            self.process.waitForFinished(1000)
