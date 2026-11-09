from __future__ import annotations

from PySide6.QtCore import QPoint, QRect, QSize, Qt, QTimer
from PySide6.QtGui import QImage, QPainter, QPixmap
from PySide6.QtWidgets import QApplication, QComboBox, QHBoxLayout, QLabel, QVBoxLayout, QWidget

from .scroll_match import ScrollMatch, match_scroll


def estimate_scroll(previous, current):
    match = match_scroll(previous, current)
    return match.shift if match else None


def append_frame(composite, previous, current, overlap=None):
    match = match_scroll(previous, current) if overlap is None else ScrollMatch(current.height() - overlap)
    if match is None:
        raise ValueError("暂未找到接缝，往回滚一小段即可继续匹配。")
    return stitch_frame(composite, current, match), match.shift


def stitch_frame(composite, current, match):
    shift, footer = match.shift, match.footer
    if shift <= 0:
        return composite
    shift = min(shift, current.height())
    if composite.width()*(composite.height()+shift) > 40000000:
        raise ValueError("已达到长图尺寸上限，请保存当前长图后继续。")
    result = QImage(composite.width(), composite.height()+shift, QImage.Format.Format_ARGB32)
    painter = QPainter(result)
    painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_Source)
    painter.drawImage(0, 0, composite)
    source = QRect(0, current.height() - shift - footer, current.width(), shift + footer)
    painter.drawImage(QPoint(0, composite.height() - footer), current, source)
    painter.end()
    return result


class ScrollCapture(QWidget):
    PREVIEW_MARGIN = 22

    def __init__(self, workbench, image, rect, screen):
        super().__init__(None, Qt.WindowType.Tool | Qt.WindowType.WindowStaysOnTopHint |
                         Qt.WindowType.NoDropShadowWindowHint)
        from .ui import STYLE, button, label
        from .capture_overlay import beside_region, logical_region
        from .capture_frame import CaptureFrame, exclude_from_capture
        from .scroll_wheel import ScrollWheel
        self.workbench, self.composite, self.previous = workbench, image, image
        self.capture_rect, self.screen = rect, screen
        self.region = logical_region(rect, screen)
        self.preview_limit = min(320, max(110, screen.availableGeometry().height() // 2))
        self.pending = None
        self.pages = 1
        self.last_append = None
        self.capture_timer = QTimer(self)
        self.capture_timer.setSingleShot(True)
        self.capture_timer.timeout.connect(self.grab_next)
        self.wheel_timer = QTimer(self)
        self.wheel_timer.setSingleShot(True)
        self.wheel_timer.setInterval(190)
        self.wheel_timer.timeout.connect(self.auto_capture)
        self.sample_timer = QTimer(self)
        self.sample_timer.setSingleShot(True)
        self.sample_timer.setInterval(105)
        self.sample_timer.timeout.connect(self.auto_capture)
        self.preview_timer = QTimer(self)
        self.preview_timer.setSingleShot(True)
        self.preview_timer.setInterval(75)
        self.preview_timer.timeout.connect(self.update_thumbnail)
        self.setWindowTitle("轻块桌面 · 滚动长截图")
        self.setObjectName("root")
        self.setWindowIcon(workbench.controller.icon)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setStyleSheet(STYLE)
        self.setFixedWidth(310)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(15, 14, 15, 14)
        layout.addWidget(label("滚动长截图", "section"))
        self.hint = label("在蓝框内向下滚动，自动采集并拼接。", "muted", True)
        layout.addWidget(self.hint)
        self.thumbnail = QLabel()
        self.thumbnail.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.thumbnail.setStyleSheet("background:#202a36;border:1px solid #384858;border-radius:8px;")
        layout.addWidget(self.thumbnail)
        self.status = label("已采集 1 屏", "muted", True)
        layout.addWidget(self.status)
        row = QHBoxLayout()
        row.addWidget(button("采集下一屏", self.capture_next, "primary"))
        self.undo_button = button("撤销", self.undo_append)
        self.undo_button.setEnabled(False)
        row.addWidget(self.undo_button)
        row.addWidget(button("完成", self.finish))
        layout.addLayout(row)
        manual = QHBoxLayout()
        manual.addWidget(label("重叠 px"))
        self.overlap = QComboBox()
        self.overlap.setEditable(False)
        for value in range(image.height()):
            self.overlap.addItem(str(value), value)
        self.overlap.setCurrentIndex(image.height()//2)
        manual.addWidget(self.overlap)
        self.manual_button = button("手动拼接", self.manual)
        self.manual_button.setEnabled(False)
        manual.addWidget(self.manual_button)
        layout.addLayout(manual)
        self.update_thumbnail()
        self.adjustSize()
        self.outline = CaptureFrame(self.region, screen, parent=self)
        self.capture_excluded = exclude_from_capture(self)
        exclude_from_capture(self.outline)
        area = screen.availableGeometry()
        # Reserve the maximum card height so adding pages never has to re-dock it.
        max_size = QSize(self.width(), self.height() - self.thumbnail.height() + self.preview_limit + 32)
        position = beside_region(self.region, area, max_size, self.PREVIEW_MARGIN)
        if position is None:
            area = next((item.availableGeometry() for item in QApplication.screens() if item is not screen), area)
            position = area.topRight()-QPoint(self.width()+14, -14)
        self.move(position)
        self.wheel = ScrollWheel(self.capture_rect, self.wheel_down)
        if not self.wheel.start():
            self.hint.setText("滚轮监听不可用，可用「采集下一屏」。")

    def wheel_down(self):
        self.wheel_timer.start()
        if not self.sample_timer.isActive():
            self.sample_timer.start()

    def update_thumbnail(self):
        preview = self.composite.scaled(QSize(272, self.preview_limit), Qt.AspectRatioMode.KeepAspectRatio,
                                        Qt.TransformationMode.SmoothTransformation)
        height = max(64, preview.height() + 10)
        if self.thumbnail.height() != height:
            self.thumbnail.setFixedHeight(height)
        self.thumbnail.setPixmap(QPixmap.fromImage(preview))
        self.adjustSize()

    def showEvent(self, event):
        self.outline.show()
        super().showEvent(event)

    def hideEvent(self, event):
        self.outline.hide()
        super().hideEvent(event)

    def overlaps_capture(self):
        margin = self.PREVIEW_MARGIN
        return self.frameGeometry().adjusted(-margin, -margin, margin, margin).intersects(self.region)

    def auto_capture(self):
        if not self.capture_excluded and self.overlaps_capture():
            self.hint.setText("请移开预览面板，或点击「采集下一屏」。")
            return
        self.capture_next()

    def capture_next(self):
        if self.capture_timer.isActive():
            return
        if not self.capture_excluded and self.overlaps_capture():
            # Older Windows lacks capture exclusion. Hide only for a manual grab.
            self.hide()
            self.capture_timer.start(70)
        else:
            self.grab_next()

    def grab_next(self):
        if not self.screen or self.screen not in self.workbench.controller.app.screens():
            self.show()
            self.status.setText("原显示器已断开，请重新开始选区。")
            return
        from . import win32
        origin_x, origin_y = win32.target_monitor(self.screen.name())["rect"][:2]
        image = self.screen.grabWindow(0).toImage()
        image = image.copy(self.capture_rect.translated(-origin_x, -origin_y))
        image.setDevicePixelRatio(1)
        if not self.isVisible():
            self.show()
        if image.size() != self.previous.size():
            self.status.setText("捕获尺寸变化，请重新选择区域。")
            return
        self.pending = image
        try:
            match = match_scroll(self.previous, image)
            if match is None:
                raise ValueError("暂未找到接缝，往回滚一小段即可继续匹配。")
            if match.shift:
                self.accept_frame(image, match)
            else:
                self.pending = None
                self.manual_button.setEnabled(False)
                self.show_count()
        except ValueError as exc:
            self.status.setText(str(exc))
            self.manual_button.setEnabled(True)

    def show_count(self):
        self.status.setText(
            f"已采集 {self.pages} 屏 · {self.composite.width()} × {self.composite.height()} px"
        )

    def queue_thumbnail(self):
        if not self.preview_timer.isActive():
            self.preview_timer.start()

    def accept_frame(self, image, match):
        result = stitch_frame(self.composite, image, match)
        tail = None
        if match.footer:
            tail = self.composite.copy(0, self.composite.height() - match.footer,
                                       self.composite.width(), match.footer)
        self.last_append = (match.shift, self.previous, image, tail)
        self.composite, self.previous = result, image
        self.pages += 1
        self.pending = None
        self.show_count()
        self.manual_button.setEnabled(False)
        self.undo_button.setEnabled(True)
        self.queue_thumbnail()

    def manual(self):
        if self.pending:
            try:
                self.accept_frame(self.pending, ScrollMatch(self.pending.height() - self.overlap.currentData()))
            except ValueError as exc:
                self.status.setText(str(exc))

    def finish(self):
        self.workbench.edit_image(self.composite, wheel_sizes=False)
        self.close()

    def undo_append(self):
        if self.last_append:
            shift, previous, image, tail = self.last_append
            self.composite = self.composite.copy(0,0,self.composite.width(),self.composite.height()-shift)
            if tail:
                painter = QPainter(self.composite)
                painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_Source)
                painter.drawImage(0, self.composite.height() - tail.height(), tail)
                painter.end()
            self.previous, self.pending = previous, image
            self.pages -= 1
            self.last_append = None
            self.manual_button.setEnabled(True)
            self.undo_button.setEnabled(False)
            self.status.setText("已撤销最后一屏，可调整重叠高度后重新拼接。")
            self.queue_thumbnail()

    def closeEvent(self, event):
        self.wheel.close()
        self.wheel_timer.stop()
        self.sample_timer.stop()
        self.capture_timer.stop()
        self.preview_timer.stop()
        self.outline.close()
        event.accept()
