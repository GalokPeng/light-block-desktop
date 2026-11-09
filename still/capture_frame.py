"""A live, click-through capture outline outside the sampled area."""
import sys
from ctypes import wintypes as W

from PySide6.QtCore import QRectF, Qt
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtWidgets import QWidget

from . import win32


def exclude_from_capture(widget):
    if sys.getwindowsversion().build < 19041:
        return False
    apply = win32.bind(win32.user32, "SetWindowDisplayAffinity", W.BOOL, W.HWND, W.DWORD)
    return bool(apply(int(widget.winId()), 0x11))


class CaptureFrame(QWidget):
    def __init__(self, region, screen, color="#398bff", parent=None, recording=False):
        super().__init__(parent, Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint |
                         Qt.WindowType.WindowStaysOnTopHint | Qt.WindowType.WindowTransparentForInput |
                         Qt.WindowType.WindowDoesNotAcceptFocus)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.color = QColor(color)
        self.inner = recording and not screen.geometry().contains(region.adjusted(-3, -3, 3, 3))
        self.setGeometry(region if self.inner else region.adjusted(-3, -3, 3, 3))
        if self.inner:
            exclude_from_capture(self)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setPen(QPen(self.color, 2))
        painter.drawRect(QRectF(self.rect()).adjusted(1, 1, -1, -1))
