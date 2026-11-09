"""Region recording controls and post-recording export."""
from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
from uuid import uuid4

from PySide6.QtCore import QElapsedTimer, QMimeData, QPoint, QProcess, QTimer, QUrl, Qt
from PySide6.QtWidgets import QApplication, QComboBox, QFileDialog, QHBoxLayout, QProgressBar, QVBoxLayout, QWidget

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


class RecordingResult(QWidget):
    def __init__(self, workbench, source, duration):
        super().__init__()
        from .ui import STYLE, button, label
        self.workbench, self.source, self.duration = workbench, Path(source), max(1, duration)
        self.setWindowTitle(tr("轻块桌面 · 录屏已完成"))
        self.setObjectName("root")
        self.setStyleSheet(STYLE)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.resize(490, 255)
        self.job = FFmpegJob(self)
        self.job.finished.connect(self.done)
        self.scratch = None
        self.partial = None
        layout = QVBoxLayout(self)
        layout.setContentsMargins(22, 20, 22, 20)
        layout.addWidget(label(tr("录屏已完成"), "title"))
        layout.addWidget(label(tr("选择 GIF 或 MP4。复制后可在支持文件粘贴的应用中使用。"), "muted", True))
        row = QHBoxLayout()
        self.format = QComboBox()
        self.format.addItem(tr("MP4 视频"), "mp4")
        self.format.addItem(tr("GIF 动图"), "gif")
        if self.source.suffix == ".mkv":
            self.format.addItem(tr("原始无损 MKV"), "mkv")
        row.addWidget(self.format, 1)
        self.save_button = button(tr("保存…"), lambda: self.export(False), "primary")
        self.copy_button = button(tr("复制文件"), lambda: self.export(True))
        row.addWidget(self.save_button)
        row.addWidget(self.copy_button)
        layout.addLayout(row)
        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.job.progress.connect(self.progress.setValue)
        self.progress.hide()
        layout.addWidget(self.progress)
        self.status = label(tr("原始录屏已暂存。"), "muted", True)
        layout.addWidget(self.status)
        from .desktop_windows import reveal_file
        layout.addWidget(button(tr("在文件夹中查看原始录屏"), lambda: reveal_file(self.source), "text"))
        self.cancel_button = button(tr("取消导出"), self.job.cancel)
        self.cancel_button.hide()
        layout.addWidget(self.cancel_button)
        layout.addStretch()

    def export(self, copy):
        if self.job.running:
            return
        mode = self.format.currentData()
        suffix = "."+mode
        if copy and self.source.suffix == suffix:
            try:
                self.copy_file(self.source)
            except OSError as exc:
                self.status.setText(str(exc))
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
                self.status.setText(tr("该路径已有文件，请选择一个新文件名。"))
                return
        self.destination, self.copy_after = dest, copy
        self.partial = dest.with_name(".Still-export-"+uuid4().hex[:8]+suffix)
        try:
            if self.source.suffix == suffix:
                commands = [["-hide_banner", "-n", "-i", str(self.source), "-map", "0", "-c", "copy",
                             "-progress", "pipe:1", str(self.partial)]]
            elif mode == "mp4":
                # yuv420p requires even dimensions, including when the original MKV is odd-sized.
                commands = [["-hide_banner", "-n", "-i", str(self.source), "-an",
                             "-vf", "pad=ceil(iw/2)*2:ceil(ih/2)*2", "-c:v", "libx264", "-crf", "18",
                             "-preset", "veryfast", "-pix_fmt", "yuv420p", "-movflags", "+faststart",
                             "-progress", "pipe:1", str(self.partial)]]
            else:
                self.scratch = TemporaryDirectory(prefix="still-record-gif-")
                commands = video_commands(self.source, self.partial, 0, None, (0,0,0,0), "gif",
                                          palette=Path(self.scratch.name)/"palette.png")
            self.set_busy(True)
            self.status.setText(tr("正在导出 ")+mode.upper()+"…")
            self.job.start(commands, self.duration)
        except (OSError, ValueError) as exc:
            self.done(False, str(exc))

    def set_busy(self, busy):
        for widget in (self.format, self.save_button, self.copy_button):
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
        self.status.setText(tr("已复制 {0} 文件").format(path.suffix[1:].upper()))

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
                    self.status.setText(tr("已保存到 ")+str(self.destination))
            else:
                self.status.setText(message)
        except OSError as exc:
            self.status.setText(str(exc))
        finally:
            if self.partial:
                self.partial.unlink(missing_ok=True)
            if self.scratch:
                self.scratch.cleanup()
                self.scratch = None

    def closeEvent(self, event):
        if self.job.process.state() != QProcess.ProcessState.NotRunning:
            self.job.cancel()
            self.job.process.waitForFinished(3000)
        event.accept()
