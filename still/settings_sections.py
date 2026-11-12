"""Small collapsible settings cards; no timers while settled."""
import ctypes as C
from ctypes import wintypes as W

from PySide6.QtCore import QEasingCurve, QPropertyAnimation, Qt
from PySide6.QtWidgets import QApplication, QFrame, QHBoxLayout, QSizePolicy, QToolButton, QVBoxLayout, QWidget


class SettingsSection(QFrame):
    def __init__(self, title, description="", extra=None, expanded=False, parent=None):
        super().__init__(parent)
        from .ui import label
        self.setObjectName("panel")
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Maximum)
        self.outer = QVBoxLayout(self)
        self.outer.setContentsMargins(20, 16, 20, 16)
        self.outer.setSpacing(0)
        header = QHBoxLayout()
        self.toggle = QToolButton()
        self.toggle.setText(title)
        self.toggle.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon)
        self.toggle.setCheckable(True)
        self.toggle.setChecked(expanded)
        self.toggle.setCursor(Qt.CursorShape.PointingHandCursor)
        self.toggle.setStyleSheet("QToolButton { background:transparent; border:0; padding:4px 2px; font-weight:600; }")
        self.toggle.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        self.toggle.clicked.connect(self.set_expanded)
        header.addWidget(self.toggle, 1)
        if extra is not None:
            header.addWidget(extra)
        self.outer.addLayout(header)
        self.content = QWidget()
        self.body = QVBoxLayout(self.content)
        self.body.setContentsMargins(0, 16, 0, 0)
        self.body.setSpacing(12)
        if description:
            self.body.addWidget(label(description, "muted", True))
        self.outer.addWidget(self.content)
        self.animation = QPropertyAnimation(self.content, b"maximumHeight", self)
        self.animation.setDuration(160)
        self.animation.setEasingCurve(QEasingCurve.Type.OutCubic)
        self.animation.finished.connect(self.settled)
        self.set_expanded(expanded, animate=False)

    def motion_enabled(self):
        if QApplication.platformName() == "offscreen":
            return False
        from . import win32
        enabled = W.BOOL()
        return bool(win32.SystemParametersInfo(0x1042, 0, C.byref(enabled), 0) and enabled.value)

    def set_expanded(self, expanded, *, animate=True):
        self.animation.stop()
        self.toggle.setChecked(expanded)
        self.toggle.setArrowType(Qt.ArrowType.DownArrow if expanded else Qt.ArrowType.RightArrow)
        self.content.setVisible(True)
        start = self.content.height() if self.content.maximumHeight() else 0
        target = self.content.sizeHint().height() if expanded else 0
        if animate and self.isVisible() and self.motion_enabled():
            self.animation.setStartValue(start)
            self.animation.setEndValue(target)
            self.animation.start()
        else:
            self.settled()

    def settled(self):
        expanded = self.toggle.isChecked()
        self.content.setMaximumHeight(16777215 if expanded else 0)
        self.content.setVisible(expanded)
