"""Frame-aligned, constant-memory playback and two-handle trim control."""
from math import ceil

from PySide6.QtCore import QPointF, QRectF, Qt, Signal
from PySide6.QtGui import QColor, QPainter, QPen, QPolygonF
from PySide6.QtWidgets import QWidget

from .i18n import tr


class RecordingTimeline(QWidget):
    seekRequested = Signal(int)
    trimChanged = Signal(int, int)

    def __init__(self, fps, parent=None):
        super().__init__(parent)
        self.fps = max(1, float(fps))
        self.duration = self.frames = self.position = 0
        self.first = self.last = 0
        self.active = "play"
        self.dragging = False
        self.setMinimumWidth(180)
        self.setFixedHeight(55)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setAccessibleName(tr("录屏时间裁剪"))
        self.setToolTip(tr("拖动两端裁剪；点击中间定位。选中指针后，← / → 移动一帧，Shift 移动十帧。"))

    def ms(self, frame):
        return min(self.duration, round(frame * 1000 / self.fps))

    def setDuration(self, milliseconds):
        milliseconds = max(0, int(milliseconds))
        if milliseconds == self.duration:
            return
        full = self.last == self.frames
        self.duration = milliseconds
        self.frames = ceil(milliseconds * self.fps / 1000)
        self.last = self.frames if full else min(self.last, self.frames)
        self.first = max(0, min(self.first, self.last - 1))
        self.update()

    def setPosition(self, milliseconds):
        self.position = max(0, min(int(milliseconds), self.duration))
        self.update()

    def reset(self):
        self.first, self.last = 0, self.frames
        self.trimChanged.emit(0, self.duration)
        self.update()

    def x(self, frame):
        return 12 + (self.width() - 24) * frame / max(1, self.frames)

    def frame_at(self, x):
        return max(0, min(self.frames, round((x - 12) * self.frames / max(1, self.width() - 24))))

    def apply_frame(self, frame):
        if not self.frames:
            return
        old = (self.first, self.last)
        if self.active == "start":
            self.first = max(0, min(frame, self.last - 1))
            position = self.ms(self.first)
        elif self.active == "end":
            self.last = min(self.frames, max(frame, self.first + 1))
            position = self.ms(self.last - 1)
        else:
            position = self.ms(max(self.first, min(frame, self.last - 1)))
        if old != (self.first, self.last):
            self.trimChanged.emit(self.ms(self.first), self.ms(self.last))
        self.setPosition(position)
        self.seekRequested.emit(position)

    def mousePressEvent(self, event):
        if event.button() != Qt.MouseButton.LeftButton or not self.frames:
            return
        x = event.position().x()
        # When the range is very short, choose the nearest endpoint.
        distances = {"start": abs(x - self.x(self.first)), "end": abs(x - self.x(self.last))}
        nearest = min(distances, key=distances.get)
        self.active = nearest if distances[nearest] <= 12 else "play"
        self.dragging = True
        self.setFocus(Qt.FocusReason.MouseFocusReason)
        self.apply_frame(self.frame_at(x))
        event.accept()

    def mouseMoveEvent(self, event):
        if self.dragging:
            self.apply_frame(self.frame_at(event.position().x()))

    def mouseReleaseEvent(self, event):
        if self.dragging and event.button() == Qt.MouseButton.LeftButton:
            self.apply_frame(self.frame_at(event.position().x()))
            self.dragging = False
            event.accept()

    def keyPressEvent(self, event):
        if event.key() in (Qt.Key.Key_Left, Qt.Key.Key_Right):
            frame = self.first if self.active == "start" else self.last if self.active == "end" else round(self.position * self.fps / 1000)
            step = 10 if event.modifiers() & Qt.KeyboardModifier.ShiftModifier else 1
            self.apply_frame(frame + (step if event.key() == Qt.Key.Key_Right else -step))
            event.accept()
        else:
            super().keyPressEvent(event)

    def focusInEvent(self, event):
        super().focusInEvent(event)
        self.update()

    def focusOutEvent(self, event):
        super().focusOutEvent(event)
        self.update()

    def paintEvent(self, event):
        with QPainter(self) as painter:
            painter.setRenderHint(QPainter.RenderHint.Antialiasing)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(QColor("#30363c"))
            painter.drawRoundedRect(QRectF(12, 22, self.width() - 24, 18), 5, 5)
            left, right = self.x(self.first), self.x(self.last)
            painter.setBrush(QColor("#43685f"))
            painter.drawRoundedRect(QRectF(left, 22, max(1, right - left), 18), 4, 4)
            painter.setPen(QColor("#d5e7e1"))
            painter.drawText(QRectF(12, 0, self.width() / 2 - 12, 18), Qt.AlignmentFlag.AlignLeft,
                             f"{self.ms(self.first)/1000:.3f}s")
            painter.drawText(QRectF(self.width() / 2, 0, self.width() / 2 - 12, 18), Qt.AlignmentFlag.AlignRight,
                             f"{self.ms(self.last)/1000:.3f}s")
            for name, x in (("start", left), ("end", right)):
                painter.setBrush(QColor("#b6f0d7" if self.active == name and self.hasFocus() else "#88cdb4"))
                painter.setPen(Qt.PenStyle.NoPen)
                painter.drawRoundedRect(QRectF(x - 5, 19, 10, 24), 3, 3)
                painter.setPen(QPen(QColor("#203b30"), 1.5))
                painter.drawLine(QPointF(x, 26), QPointF(x, 36))
            x = self.x(self.position * self.fps / 1000)
            painter.setPen(QPen(QColor("#f3f7fa"), 1.5))
            painter.drawLine(QPointF(x, 22), QPointF(x, 46))
            painter.setBrush(QColor("#f3f7fa"))
            painter.drawPolygon(QPolygonF([QPointF(x - 4, 48), QPointF(x + 4, 48), QPointF(x, 44)]))
