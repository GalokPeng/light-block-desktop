"""Region recording controls and post-recording export."""
from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
from uuid import uuid4

from PySide6.QtCore import QElapsedTimer, QMimeData, QPoint, QProcess, QRect, QRectF, QSignalBlocker, QTimer, QUrl, Qt
from PySide6.QtGui import QColor, QImage, QKeySequence, QPainter, QShortcut
from PySide6.QtMultimedia import QMediaPlayer, QVideoFrame, QVideoSink
from PySide6.QtWidgets import QApplication, QComboBox, QFileDialog, QLabel, QHBoxLayout, QProgressBar, QSlider, QVBoxLayout, QWidget

from .capture import AnnotationCanvas, PIN_GLOW_MARGIN, paint_pin_glow
from .capture_icons import capture_icon
from .capture_tools import DOCK_STYLE, DrawingTools, divider, icon_button, strip
from .i18n import tr
from .capture_frame import exclude_from_capture
from .capture_overlay import beside_region, logical_region
from .media_tools import FFmpegJob, video_commands


class RecordingBar(QWidget):
    def __init__(self, workbench, physical, screen):
        super().__init__(None, Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint |
                         Qt.WindowType.WindowStaysOnTopHint | Qt.WindowType.WindowDoesNotAcceptFocus)
        from .ui import STYLE, button, label
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setWindowTitle(tr("轻块桌面 · 录屏控制"))
        self.setObjectName("root")
        self.setStyleSheet(STYLE)
        row = QHBoxLayout(self)
        row.setContentsMargins(9, 6, 9, 6)
        self.status = label(tr("正在准备"), "muted")
        row.addWidget(self.status, 1)
        self.stop_button = button(tr("结束录屏"), workbench.record, "primary")
        row.addWidget(self.stop_button)
        self.setFixedWidth(225)
        self.adjustSize()
        area = screen.availableGeometry()
        position = beside_region(logical_region(physical, screen), area, self.size())
        if position is None:
            others = [s.availableGeometry() for s in QApplication.screens() if s is not screen]
            if others:
                position = others[0].topLeft()+QPoint(12, 12)
        self.move(position if position is not None else area.topRight()-QPoint(self.width()+12, -12))
        self.elapsed = QElapsedTimer()
        self.timer = QTimer(self)
        self.timer.setInterval(500)
        self.timer.timeout.connect(self.update_time)
        if position is not None:
            self.show()
        elif exclude_from_capture(self):
            self.show()

    def start_clock(self):
        self.elapsed.start()
        self.timer.start()
        self.update_time()

    def update_time(self):
        seconds = self.elapsed.elapsed()//1000
        self.status.setText(tr("录屏中  {0:02d}:{1:02d}").format(seconds // 60, seconds % 60))

    def finishing(self):
        self.timer.stop()
        self.status.setText(tr("正在完成录制"))
        self.stop_button.setEnabled(False)


class RecordingCanvas(AnnotationCanvas):
    def __init__(self, parent, size):
        layer = QImage(size, QImage.Format.Format_ARGB32_Premultiplied)
        layer.fill(Qt.GlobalColor.transparent)
        super().__init__(layer, pin=parent)
        self.frame = QVideoFrame()
        self.frame_options = QVideoFrame.PaintOptions()
        self.frame_options.aspectRatioMode = Qt.AspectRatioMode.IgnoreAspectRatio
        self.crop = self.source.rect()
        self.drag_offset = None

    def receive_frame(self, frame):
        if frame.isValid():
            self.frame = frame
            self.update()

    def rebuild(self):
        self.crop = self.source.rect()
        for operation in self.operations:
            if operation["kind"] == "crop":
                rect = QRect(*operation["rect"]).intersected(QRect(QPoint(), self.crop.size()))
                if rect.width() >= 2 and rect.height() >= 2:
                    self.crop = rect.translated(self.crop.topLeft())
        super().rebuild()

    def paint_content(self, painter, background):
        area = self.image_area
        painter.fillRect(area, QColor("#151a23"))
        if self.frame.isValid():
            scale = area.width() / self.crop.width()
            full = QRectF(area.x() - self.crop.x()*scale, area.y() - self.crop.y()*scale,
                          self.frame.width()*scale, self.frame.height()*scale)
            painter.save()
            painter.setClipRect(area)
            self.frame.paint(painter, full, self.frame_options)
            painter.restore()
        painter.drawImage(area, background)

    def adjust_wheel(self, event, point):
        return self.sizing.adjust(event, point) if not self.current else False

    def mousePressEvent(self, event):
        if self.tool != "move":
            return super().mousePressEvent(event)
        if event.button() == Qt.MouseButton.LeftButton:
            self.drag_offset = event.globalPosition().toPoint() - self.window().pos()
            self.setCursor(Qt.CursorShape.ClosedHandCursor)

    def mouseMoveEvent(self, event):
        if self.drag_offset is not None:
            self.window().move(event.globalPosition().toPoint() - self.drag_offset)
        elif self.tool != "move":
            super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        if self.tool == "move":
            self.drag_offset = None
            self.setCursor(Qt.CursorShape.OpenHandCursor)
        else:
            super().mouseReleaseEvent(event)


class RecordingResult(QWidget):
    def __init__(self, workbench, source, duration, geometry, source_size, source_fps):
        flags = Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint | Qt.WindowType.WindowStaysOnTopHint
        super().__init__(None, flags)
        self.workbench, self.source, self.duration = workbench, Path(source), max(1, duration)
        self.source_fps = source_fps
        self.trim_start = 0
        self.trim_end = None
        self.setWindowTitle(tr("轻块桌面 · 录屏预览"))
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.job = FFmpegJob(self)
        self.job.finished.connect(self.done)
        self.scratch = None
        self.partial = None
        self.canvas = RecordingCanvas(self, source_size)
        self.player = QMediaPlayer(self)
        self.sink = QVideoSink(self)
        self.player.setVideoSink(self.sink)
        self.sink.videoFrameChanged.connect(self.canvas.receive_frame)
        self.player.setLoops(QMediaPlayer.Loops.Infinite)

        self.toolbar = QWidget(self, flags)
        self.toolbar.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.toolbar.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.toolbar.setWindowTitle(tr("录屏预览工具栏"))
        self.toolbar.setStyleSheet(DOCK_STYLE)
        self.toolbar.setMinimumWidth(350)
        self.bars = QVBoxLayout(self.toolbar)
        self.bars.setContentsMargins(0, 0, 0, 0)
        self.bars.setSpacing(4)
        controls, row = strip()
        self.bars.addWidget(controls)
        self.play_button = icon_button("play", tr("播放"), self.toggle_playback)
        row.addWidget(self.play_button)
        row.addWidget(icon_button("move", tr("移动预览"), lambda: self.select_tool("move")))
        self.canvas.drawing = DrawingTools(row, self.attach_options, self.select_tool, None, crop=True,
                                           groups=(("pen", "highlight"), ("erase",)))
        divider(row)
        row.addWidget(icon_button("undo", tr("撤销"), self.canvas.undo))
        row.addWidget(icon_button("redo", tr("重做"), self.canvas.redo))
        row.addStretch()
        row.addWidget(icon_button("cancel", tr("关闭预览"), self.close))

        timeline, row = strip()
        self.timeline = QSlider(Qt.Orientation.Horizontal)
        self.timeline.setRange(0, 0)
        self.timeline.setAccessibleName(tr("播放进度"))
        self.timeline.sliderReleased.connect(lambda: self.player.setPosition(self.timeline.value()))
        self.timeline.valueChanged.connect(self.seek)
        row.addWidget(self.timeline, 1)
        self.clock = QLabel("0:00 / 0:00")
        row.addWidget(self.clock)
        row.addWidget(icon_button("trim_start", tr("设为片段起点"), lambda: self.mark_trim(start=True)))
        row.addWidget(icon_button("trim_end", tr("设为片段终点"), lambda: self.mark_trim(start=False)))
        row.addWidget(icon_button("reset", tr("取消时间裁剪"), self.clear_trim))
        self.bars.addWidget(timeline)

        exports, row = strip()
        self.format = self.choice(tr("导出格式"), ((tr("MP4 视频"), "mp4"), (tr("GIF 动图"), "gif"), (tr("无损 MKV"), "mkv")))
        self.quality = self.choice(tr("导出画质"), ((tr("原始分辨率"), 0), ("1080p", 1080),
                                                   ("720p", 720), ("480p", 480)))
        self.fps = self.choice(tr("导出帧率"), [(tr("原始帧率"), None)] + [(f"{value} fps", value) for value in (12, 15, 24, 30, 60)])
        for field in (self.format, self.quality, self.fps):
            row.addWidget(field, 1)
        self.copy_button = icon_button("copy", tr("复制文件"), lambda: self.export(True))
        self.save_button = icon_button("save", tr("保存录屏"), lambda: self.export(False))
        row.addWidget(self.copy_button)
        row.addWidget(self.save_button)
        self.bars.addWidget(exports)
        status_bar, row = strip()
        self.status_bar = status_bar
        self.status = QLabel()
        self.status.setWordWrap(True)
        self.status.setMaximumWidth(425)
        row.addWidget(self.status, 1)
        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.setFixedWidth(85)
        self.job.progress.connect(self.progress.setValue)
        row.addWidget(self.progress)
        self.cancel_button = icon_button("cancel", tr("取消导出"), self.job.cancel)
        row.addWidget(self.cancel_button)
        self.bars.addWidget(status_bar)
        self.set_busy(False)
        status_bar.hide()
        for target in (self, self.toolbar):
            QShortcut(QKeySequence("Escape"), target, self.close)
            QShortcut(QKeySequence.StandardKey.Copy, target, lambda: self.export(True))
            QShortcut(QKeySequence.StandardKey.Save, target, lambda: self.export(False))
        self.player.playbackStateChanged.connect(self.update_play_button)
        self.player.positionChanged.connect(self.update_timeline)
        self.player.durationChanged.connect(self.timeline.setMaximum)
        self.player.errorOccurred.connect(lambda _, message: self.set_status(message))
        self.select_tool("move")
        margin = PIN_GLOW_MARGIN
        self.setGeometry(geometry.adjusted(-margin, -margin, margin, margin))
        self.player.setSource(QUrl.fromLocalFile(str(self.source)))
        self.player.play()

    @staticmethod
    def choice(name, items):
        combo = QComboBox()
        combo.setAccessibleName(name)
        combo.setToolTip(name)
        for text, value in items:
            combo.addItem(text, value)
        return combo

    def attach_options(self, panel):
        self.bars.insertWidget(1, panel)

    def position_toolbar(self):
        if not hasattr(self, "toolbar"):
            return
        self.toolbar.adjustSize()
        screen = QApplication.screenAt(self.geometry().center()) or self.screen()
        area = screen.availableGeometry()
        size = self.toolbar.size()
        gap = 7
        x = max(area.left(), min(self.geometry().center().x() - size.width()//2, area.right() - size.width() + 1))
        below, above = self.geometry().bottom() + gap + 1, self.y() - size.height() - gap
        if below + size.height() <= area.bottom() + 1:
            point = QPoint(x, below)
        elif above >= area.top():
            point = QPoint(x, above)
        else:
            point = beside_region(self.geometry(), area, size, gap) or QPoint(x, max(area.top(), area.bottom()-size.height()+1))
        self.toolbar.move(point)

    def showEvent(self, event):
        super().showEvent(event)
        self.position_toolbar()
        self.toolbar.show()

    def moveEvent(self, event):
        super().moveEvent(event)
        self.position_toolbar()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        margin = PIN_GLOW_MARGIN
        self.canvas.setGeometry(self.rect().adjusted(margin, margin, -margin, -margin))
        self.position_toolbar()

    def paintEvent(self, event):
        with QPainter(self) as painter:
            paint_pin_glow(painter, self.rect())

    def seek(self, value):
        if not self.timeline.isSliderDown():
            self.player.setPosition(value)

    def update_timeline(self, milliseconds):
        if (self.player.isPlaying() and not self.timeline.isSliderDown()
                and (milliseconds < self.trim_start or self.trim_end is not None and milliseconds >= self.trim_end)):
            self.player.setPosition(self.trim_start)
            return
        if not self.timeline.isSliderDown():
            with QSignalBlocker(self.timeline):
                self.timeline.setValue(milliseconds)
        elapsed, total = milliseconds//1000, self.player.duration()//1000
        self.clock.setText(f"{elapsed//60}:{elapsed%60:02d} / {total//60}:{total%60:02d}")

    def mark_trim(self, start):
        total = self.player.duration()
        step = round(1000 / self.source_fps)
        if total < step:
            return
        self.player.pause()
        at = self.player.position()
        if start:
            self.trim_start = min(at, (self.trim_end if self.trim_end is not None else total) - step)
        else:
            self.trim_end = min(total, max(at, self.trim_start + step))
        self.update_trim_label()

    def clear_trim(self):
        self.trim_start, self.trim_end = 0, None
        self.update_trim_label()

    def update_trim_label(self):
        if self.trim_start or self.trim_end is not None:
            end = self.trim_end if self.trim_end is not None else self.player.duration()
            label = tr("导出片段 {0:.2f} 秒 – {1:.2f} 秒").format(self.trim_start/1000, end/1000)
        else:
            label = tr("导出完整录屏")
        self.clock.setToolTip(label)
        self.set_status(label)

    def select_tool(self, tool):
        if tool != "move":
            self.player.pause()
        self.canvas.set_tool(tool)
        if tool == "move":
            self.canvas.setCursor(Qt.CursorShape.OpenHandCursor)
        self.position_toolbar()

    def toggle_playback(self):
        if self.player.isPlaying():
            self.player.pause()
        else:
            self.select_tool("move")
            self.player.play()

    def update_play_button(self, state):
        self.play_button.setIcon(capture_icon("pause" if self.player.isPlaying() else "play"))
        label = tr("暂停") if self.player.isPlaying() else tr("播放")
        self.play_button.setToolTip(label)
        self.play_button.setAccessibleName(label)

    def set_status(self, message):
        self.status.setText(message)
        self.status_bar.setVisible(bool(message))
        self.position_toolbar()

    def export(self, copy):
        if self.job.running:
            return
        mode = self.format.currentData()
        suffix = "."+mode
        height = self.quality.currentData()
        crf = {720: 21, 480: 24}.get(height, 18)
        fps = self.fps.currentData()
        start = self.trim_start / 1000
        length = (self.trim_end - self.trim_start) / 1000 if self.trim_end is not None else None
        unchanged = (self.source.suffix == suffix and not self.canvas.operations and not height and fps is None
                     and not start and length is None)
        if copy and unchanged:
            try:
                self.copy_file(self.source)
            except OSError as exc:
                self.set_status(str(exc))
            return
        if copy:
            dest = self.source.with_name(self.source.stem+tr("-剪贴板-")+uuid4().hex[:6]+suffix)
        else:
            store = self.workbench.controller.store
            directory = Path(store.settings.capture_directory or store.directory)
            choice, _ = QFileDialog.getSaveFileName(self, tr("保存录屏"), str(directory/(self.source.stem+suffix)),
                                                   tr("{0} 文件 (*{1})").format(mode.upper(), suffix))
            if not choice:
                return
            dest = Path(choice)
            if dest.suffix.lower() != suffix:
                dest = dest.with_suffix(suffix)
            if dest.exists():
                self.set_status(tr("该路径已有文件，请选择一个新文件名。"))
                return
        self.destination, self.copy_after = dest, copy
        self.partial = dest.with_name(".Still-export-"+uuid4().hex[:8]+suffix)
        try:
            if unchanged:
                commands = [["-hide_banner", "-n", "-i", str(self.source), "-map", "0", "-c", "copy",
                             "-progress", "pipe:1", str(self.partial)]]
            else:
                overlay = palette = None
                marked = any(operation["kind"] != "crop" for operation in self.canvas.operations)
                if marked or mode == "gif":
                    self.scratch = TemporaryDirectory(prefix="still-record-export-")
                    folder = Path(self.scratch.name)
                    if marked:
                        overlay = folder / "annotation.png"
                        if not self.canvas.result().save(str(overlay), "PNG"):
                            raise OSError(tr("无法暂存录屏标注。"))
                    if mode == "gif":
                        palette = folder / "palette.png"
                commands = video_commands(self.source, self.partial, start, length, self.canvas.crop.getRect(),
                                          "lossless" if mode == "mkv" else mode, fps=fps or self.source_fps,
                                          palette=palette, overlay=overlay, max_height=height, output_fps=fps, crf=crf)
            self.player.pause()
            self.set_busy(True)
            self.set_status(tr("正在导出 ")+mode.upper()+"…")
            duration = self.player.duration()/1000 or self.duration
            self.job.start(commands, length if length is not None else max(0.1, duration-start))
        except (OSError, ValueError) as exc:
            self.done(False, str(exc))

    def set_busy(self, busy):
        for widget in (self.format, self.quality, self.fps, self.save_button, self.copy_button):
            widget.setEnabled(not busy)
        self.progress.setVisible(busy)
        self.cancel_button.setVisible(busy)
        if busy:
            self.progress.setValue(0)

    def copy_file(self, path):
        size = path.stat().st_size
        mime = QMimeData()
        mime.setUrls([QUrl.fromLocalFile(str(path))])
        if path.suffix == ".gif" and size <= 16*1024*1024:
            mime.setData("image/gif", path.read_bytes())
        QApplication.clipboard().setMimeData(mime)
        self.set_status(tr("已复制 {0} 文件").format(path.suffix[1:].upper()))

    def done(self, ok, message):
        self.set_busy(False)
        try:
            if ok:
                self.partial.rename(self.destination)  # Windows refuses to replace an existing file.
                if self.copy_after:
                    self.copy_file(self.destination)
                else:
                    store = self.workbench.controller.store
                    store.settings.capture_directory = str(self.destination.parent)
                    store.save()
                    self.set_status(tr("已保存到 ")+str(self.destination))
            else:
                self.set_status(message)
        except OSError as exc:
            self.set_status(str(exc))
        finally:
            if self.partial:
                self.partial.unlink(missing_ok=True)
            if self.scratch:
                self.scratch.cleanup()
                self.scratch = None

    def closeEvent(self, event):
        self.player.stop()
        self.player.setSource(QUrl())
        self.toolbar.hide()
        if self.job.process.state() != QProcess.ProcessState.NotRunning:
            self.job.cancel()
            self.job.process.waitForFinished(3000)
        event.accept()
