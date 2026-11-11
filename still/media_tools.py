"""On-demand recording and bounded-memory, two-pass GIF export."""
from __future__ import annotations

import os
from math import floor
from pathlib import Path
import re
import shutil
import sys
import tempfile
import uuid

from PySide6.QtCore import QObject, QProcess, QRect, Qt, QTimer, Signal
from PySide6.QtWidgets import (QComboBox, QDoubleSpinBox, QFileDialog, QFormLayout, QHBoxLayout,
    QLabel, QLineEdit, QProgressBar, QPushButton, QSpinBox, QVBoxLayout, QWidget)

from .i18n import tr
from .model import ROOT


def ffmpeg_path():
    bundled = ROOT / "runtime" / "ffmpeg.exe"
    if bundled.is_file():
        return str(bundled)
    if getattr(sys, "frozen", False):
        raise FileNotFoundError(tr("录屏组件缺失，请重新安装轻块桌面。"))
    path = shutil.which("ffmpeg")
    if not path:
        raise FileNotFoundError(tr("录屏和导出需要 FFmpeg。请安装支持 gdigrab、libx264 和 ffv1 的 Windows 版 FFmpeg 并加入 PATH。"))
    return path


def parse_media_info(text):
    duration = re.search(r"Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?)", text)
    video_lines = [line for line in text.splitlines() if "Video:" in line and "attached pic" not in line]
    resolution = re.search(r"\b(\d{2,5})x(\d{2,5})\b", video_lines[0]) if video_lines else None
    return {"duration": sum(float(value)*factor for value, factor in zip(duration.groups(), (3600, 60, 1))) if duration else 0,
            "width": int(resolution[1]) if resolution else 0, "height": int(resolution[2]) if resolution else 0}


def video_commands(source, output, start, duration, crop, mode, fps=12, gif_width=0, palette=None, *,
                   overlay=None, max_height=0, output_fps=None, crf=18, gif_fast=True, frame_limit=None):
    source, output = Path(source), Path(output)
    if source.resolve() == output.resolve():
        raise ValueError(tr("输出文件必须与原文件不同。"))
    if duration is not None and duration <= 0:
        raise ValueError(tr("结束时间必须晚于开始时间。"))
    if frame_limit is not None and (type(frame_limit) is not int or frame_limit < 1):
        raise ValueError("Frame limit must be a positive integer")
    filters = []
    # Discard unused frames before crop, annotation compositing and scaling.
    if mode == "gif" or output_fps:
        filters.append(f"fps={int(fps if mode == 'gif' else output_fps)}")
    x, y, width, height = crop
    if width > 0 and height > 0:
        filters.append(f"crop={int(width)}:{int(height)}:{int(x)}:{int(y)}:exact=1")
    # Millisecond rounding can skip the selected frame (e.g. frame 1 at 24
    # fps is 41.666… ms). Seek just before its microsecond boundary instead.
    seek = ["-ss", f"{floor(start * 1000000) / 1000000:.6f}"]
    if duration is not None:
        seek += ["-t", f"{max(1, floor(duration * 1000000)) / 1000000:.6f}"]
    seek += ["-i", str(source)]
    common = ["-hide_banner", "-n"]
    graph = ""
    video = "[0:v]"
    if overlay is not None:
        seek += ["-i", str(overlay)]
        graph = f"[0:v]{','.join(filters) or 'null'}[base];[base][1:v]overlay=format=auto:alpha=straight[marked];"
        video = "[marked]"
        filters.clear()
    if max_height:
        filters.append(f"scale=-2:'min(ih,{int(max_height)})':flags=lanczos")
    if mode == "gif":
        if gif_width:
            filters.append(f"scale={int(gif_width)}:-1:flags=lanczos")
        chain = ",".join(filters) or "null"
        if palette is None:
            raise ValueError(tr("GIF 导出需要独立调色板临时文件。"))
        palette_input = 2 if overlay is not None else 1
        analysis = f"{graph}{video}{chain}"
        if gif_fast:
            # Only the color analysis is sampled. The output still uses every
            # requested frame and pixel. Separate passes bound memory on long
            # recordings; a split/global-palette graph queues the whole video.
            # Round upward at EOF so even a one-frame trim has a palette.
            # Reuse fps from the bundled runtime; no extra codec/filter install.
            analysis = analysis.replace(f"fps={int(fps)}", f"fps={min(8, int(fps))}:round=up", 1)
            analysis += ",scale=w='min(iw,640)':h=-2:flags=area"
        dithering = "bayer:bayer_scale=3" if gif_fast else "sierra2_4a"
        return [common + seek + ["-filter_complex_threads", "1", "-filter_complex",
                                 f"{analysis},palettegen=stats_mode=diff", "-frames:v", "1",
                                 "-progress", "pipe:1", str(palette)],
                common + seek + ["-i", str(palette), "-filter_complex_threads", "1", "-filter_complex",
                                 f"{graph}{video}{chain}[v];[v][{palette_input}:v]paletteuse=dither={dithering}:diff_mode=rectangle",
                                 "-an", "-loop", "0"] + (["-frames:v", str(frame_limit)] if frame_limit is not None else []) +
                                ["-progress", "pipe:1", str(output)]]
    args = common + seek
    if mode != "lossless":
        filters.append("pad=ceil(iw/2)*2:ceil(ih/2)*2")
    if graph:
        args += ["-filter_complex_threads", "1", "-filter_complex", f"{graph}{video}{','.join(filters) or 'null'}[v]",
                 "-map", "[v]", "-map", "0:a?"]
    elif filters:
        args += ["-vf", ",".join(filters)]
    if mode == "lossless":
        args += ["-c:v", "ffv1", "-level", "3", "-c:a", "flac"]
    else:
        args += ["-c:v", "libx264", "-preset", "veryfast", "-crf", str(crf), "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart"]
    if frame_limit is not None:
        args += ["-frames:v", str(frame_limit)]
    return [args + ["-progress", "pipe:1", str(output)]]


class FFmpegJob(QObject):
    progress = Signal(int)
    finished = Signal(bool, str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.process = QProcess(self)
        self.process.readyReadStandardError.connect(self.read_error)
        self.process.readyReadStandardOutput.connect(self.read_progress)
        self.process.finished.connect(self.process_finished)
        self.process.errorOccurred.connect(self.process_error)
        self.commands = []
        self.total = 0
        self.index = 0
        self.duration = 1
        self.errors = ""
        self.stdout_buffer = ""
        self.cancelled = False
        self.running = False

    def start(self, commands, duration):
        self.executable = ffmpeg_path()
        self.commands, self.total, self.index = list(commands), len(commands), 0
        self.duration = max(duration, 0.1)
        self.cancelled = False
        self.running = True
        self.next()

    def next(self):
        self.errors = ""
        self.stdout_buffer = ""
        self.process.setProgram(self.executable)
        self.process.setArguments(self.commands[self.index])
        self.process.start()

    def read_error(self):
        self.errors = (self.errors + bytes(self.process.readAllStandardError()).decode("utf-8", "replace"))[-12000:]

    def read_progress(self):
        self.stdout_buffer += bytes(self.process.readAllStandardOutput()).decode("utf-8", "replace")
        lines = self.stdout_buffer.split("\n")
        self.stdout_buffer = lines.pop()
        for line in lines:
            if line.startswith("out_time_us="):
                try:
                    fraction = min(1.0, int(line.partition("=")[2])/1000000/self.duration)
                    self.progress.emit(round((self.index+fraction)/self.total*100))
                except ValueError:
                    pass

    def process_finished(self, code, status):
        self.read_error()
        if not self.running:
            return
        if self.cancelled or code != 0 or status != QProcess.ExitStatus.NormalExit:
            self.running = False
            self.finished.emit(False, tr("操作已取消。") if self.cancelled else self.errors[-3000:] or tr("媒体任务失败。"))
            return
        self.index += 1
        if self.index < self.total:
            self.progress.emit(round(self.index/self.total*100))
            self.next()
        else:
            self.running = False
            self.progress.emit(100)
            self.finished.emit(True, tr("完成"))

    def process_error(self, error):
        if error == QProcess.ProcessError.FailedToStart and self.running:
            self.running = False
            self.finished.emit(False, self.process.errorString())

    def cancel(self):
        self.cancelled = True
        self.process.kill()


class Recorder(QObject):
    changed = Signal()
    completed = Signal(str)
    error = Signal(str)

    def __init__(self, workbench):
        super().__init__()
        self.workbench = workbench
        self.process = QProcess(self)
        self.process.readyReadStandardError.connect(self.read_error)
        self.process.finished.connect(self.finished)
        self.process.errorOccurred.connect(self.failed_start)
        self.active = False
        self.stopping = False
        self.errors = ""

    def start(self, rect, destination, lossless=False, fps=24):
        if self.active:
            raise RuntimeError(tr("已有录屏正在进行。"))
        destination = Path(destination)
        if destination.exists():
            raise FileExistsError(tr("该输出文件已存在，请选择另一个名称。"))
        destination.parent.mkdir(parents=True, exist_ok=True)
        self.destination = destination
        self.partial = destination.with_name(".Still-recording-"+uuid.uuid4().hex[:8]+destination.suffix)
        width, height = rect.width(), rect.height()
        if width < 2 or height < 2:
            raise ValueError(tr("录屏选区太小。"))
        args = ["-hide_banner", "-n", "-f", "gdigrab", "-framerate", str(fps), "-draw_mouse", "1",
                "-offset_x", str(rect.x()), "-offset_y", str(rect.y()), "-video_size", f"{width}x{height}", "-i", "desktop", "-an"]
        if lossless:
            args += ["-c:v", "ffv1", "-level", "3"]
        else:
            args += ["-vf", "pad=ceil(iw/2)*2:ceil(ih/2)*2", "-c:v", "libx264", "-preset", "veryfast",
                     "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart"]
        args.append(str(self.partial))
        self.process.setProgram(ffmpeg_path())
        self.process.setArguments(args)
        self.active, self.stopping, self.errors = True, False, ""
        self.process.start()
        self.changed.emit()

    def read_error(self):
        self.errors = (self.errors + bytes(self.process.readAllStandardError()).decode("utf-8", "replace"))[-12000:]

    def stop(self):
        if self.active and not self.stopping:
            self.stopping = True
            self.process.write(b"q\n")
            self.changed.emit()

    def failed_start(self, error):
        if error == QProcess.ProcessError.FailedToStart:
            self.active = False
            self.error.emit(self.process.errorString())
            self.changed.emit()

    def finished(self, code, status):
        self.active = False
        self.stopping = False
        self.read_error()
        if code == 0 and status == QProcess.ExitStatus.NormalExit and self.partial.exists() and self.partial.stat().st_size:
            try:
                os.rename(self.partial, self.destination)
                self.completed.emit(str(self.destination))
            except OSError as exc:
                self.error.emit(tr("录屏保留在 {0}：{1}").format(self.partial, exc))
        else:
            self.error.emit(tr("录屏未正常完成。临时文件若已生成会保留。\n")+self.errors[-2000:])
        self.changed.emit()

    def shutdown(self):
        self.stop()
        if self.process.state() != QProcess.ProcessState.NotRunning:
            if not self.process.waitForFinished(5000):
                self.process.kill()
                self.process.waitForFinished(1500)
                self.active = False
                self.stopping = False
                self.changed.emit()


class MediaStudio(QWidget):
    def __init__(self, workbench):
        super().__init__()
        from .ui import STYLE, button, label
        self.workbench = workbench
        self.setWindowTitle(tr("轻块桌面 · 视频裁剪与 GIF 导出"))
        self.setObjectName("root")
        self.setWindowIcon(workbench.controller.icon)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setStyleSheet(STYLE)
        self.resize(760, 650)
        self.info = {"duration": 0, "width": 0, "height": 0}
        self.metadata = QProcess(self)
        self.metadata.finished.connect(self.metadata_finished)
        self.metadata.errorOccurred.connect(self.metadata_error)
        self.job = FFmpegJob(self)
        self.job.progress.connect(self.set_progress)
        self.job.finished.connect(self.done)
        self.temporary = None
        outer = QVBoxLayout(self)
        outer.setContentsMargins(25, 23, 25, 23)
        outer.setSpacing(16)
        outer.addWidget(label(tr("让每个片段，刚刚好。"), "title"))
        outer.addWidget(label(tr("时间截取 · 画面裁剪 · 视频转 GIF · 独立导出，保留原素材"), "muted", True))
        row = QHBoxLayout()
        self.source = QLineEdit()
        self.source.setReadOnly(True)
        self.source.setPlaceholderText(tr("选择视频或 GIF 文件"))
        row.addWidget(self.source, 1)
        self.browse = button(tr("选择素材…"), self.choose)
        row.addWidget(self.browse)
        outer.addLayout(row)
        self.details = label(tr("尚未选择素材"), "muted", True)
        outer.addWidget(self.details)
        form = QFormLayout()
        self.mode = QComboBox()
        for name, key in ((tr("高画质 MP4"), "mp4"), (tr("动态 GIF（最多 256 色）"), "gif"), (tr("无损 MKV（文件较大）"), "lossless")):
            self.mode.addItem(name, key)
        form.addRow(tr("导出格式"), self.mode)
        self.start_time, self.end_time = QDoubleSpinBox(), QDoubleSpinBox()
        for spin in (self.start_time, self.end_time):
            spin.setDecimals(3)
            spin.setRange(0, 86400)
            spin.setSuffix(tr(" 秒"))
        times = QHBoxLayout()
        times.addWidget(self.start_time)
        times.addWidget(QLabel(tr("至")))
        times.addWidget(self.end_time)
        form.addRow(tr("时间范围"), times)
        crop_row = QHBoxLayout()
        self.crop = [QSpinBox() for _ in range(4)]
        for name, spin in zip(("X", "Y", tr("宽"), tr("高")), self.crop):
            spin.setRange(0, 32768)
            spin.setPrefix(name+" ")
            crop_row.addWidget(spin)
        form.addRow(tr("画面裁剪"), crop_row)
        self.fps = QSpinBox()
        self.fps.setRange(1, 30)
        self.fps.setValue(12)
        form.addRow(tr("GIF 帧率"), self.fps)
        self.gif_width = QComboBox()
        for name, value in ((tr("保持源分辨率"), 0), (tr("宽 320 px"), 320), (tr("宽 640 px"), 640), (tr("宽 960 px"), 960)):
            self.gif_width.addItem(name, value)
        form.addRow(tr("GIF 尺寸"), self.gif_width)
        outer.addLayout(form)
        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        outer.addWidget(self.progress)
        self.message = label(tr("GIF 使用两次流式处理，不把整段视频帧缓存在内存里。"), "muted", True)
        outer.addWidget(self.message)
        actions = QHBoxLayout()
        self.cancel_button = button(tr("取消任务"), self.job.cancel)
        self.cancel_button.setEnabled(False)
        actions.addWidget(self.cancel_button)
        actions.addStretch()
        self.export = button(tr("导出新文件…"), self.run, "primary")
        self.export.setEnabled(False)
        actions.addWidget(self.export)
        outer.addLayout(actions)
        outer.addStretch()

    def choose(self):
        path, _ = QFileDialog.getOpenFileName(self, tr("选择素材"), "", tr("视频和 GIF (*.mp4 *.mkv *.mov *.webm *.avi *.m4v *.gif);;所有文件 (*)"))
        if path:
            if self.metadata.state() != QProcess.ProcessState.NotRunning:
                self.metadata.blockSignals(True)
                self.metadata.kill()
                self.metadata.waitForFinished(1500)
                self.metadata.blockSignals(False)
            self.metadata.readAllStandardError()
            self.metadata.readAllStandardOutput()
            self.source.setText(path)
            self.export.setEnabled(False)
            try:
                self.metadata.setProgram(ffmpeg_path())
                self.metadata.setArguments(["-hide_banner", "-i", path])
                self.browse.setEnabled(False)
                self.metadata.start()
            except OSError as exc:
                self.workbench.error(str(exc))

    def metadata_error(self, error):
        if error == QProcess.ProcessError.FailedToStart:
            self.browse.setEnabled(True)
            self.details.setText(tr("素材探测无法启动：")+self.metadata.errorString())

    def metadata_finished(self, *_):
        self.browse.setEnabled(True)
        self.info = parse_media_info(bytes(self.metadata.readAllStandardError()).decode("utf-8", "replace"))
        info = self.info
        self.details.setText(tr("{0} × {1} px  ·  {2:.3f} 秒").format(info['width'], info['height'], info['duration']))
        self.start_time.setValue(0)
        self.end_time.setValue(info["duration"])
        for spin, value in zip(self.crop, (0, 0, info["width"], info["height"])):
            spin.setValue(value)
        self.export.setEnabled(bool(info["width"] and info["duration"]))

    def run(self):
        if self.job.running:
            return
        mode = self.mode.currentData()
        suffix = ".gif" if mode == "gif" else ".mkv" if mode == "lossless" else ".mp4"
        source = Path(self.source.text())
        output, _ = QFileDialog.getSaveFileName(self, tr("导出新文件"), str(source.with_name(source.stem+"_Still"+suffix)), tr("媒体文件 (*")+suffix+")")
        if not output:
            return
        output = Path(output)
        if output.suffix.lower() != suffix:
            output = output.with_suffix(suffix)
        try:
            if output.exists():
                raise FileExistsError(tr("输出文件已存在，请使用新名称；原素材不会被覆盖。"))
            if self.end_time.value() > self.info["duration"]+0.05:
                raise ValueError(tr("结束时间超出素材长度。"))
            x, y, width, height = [spin.value() for spin in self.crop]
            if not width or not height or x+width > self.info["width"] or y+height > self.info["height"]:
                raise ValueError(tr("裁剪区域必须完整位于原画面内。"))
            if mode == "mp4" and (width % 2 or height % 2):
                raise ValueError(tr("MP4 裁剪宽高请使用偶数；无损 MKV 可保留奇数尺寸。"))
            self.temporary = tempfile.TemporaryDirectory(prefix="still-export-")
            self.destination = output
            self.partial = output.with_name(".Still-export-"+uuid.uuid4().hex[:8]+suffix)
            commands = video_commands(source, self.partial, self.start_time.value(), self.end_time.value()-self.start_time.value(),
                                      (x, y, width, height), mode, self.fps.value(), self.gif_width.currentData(), Path(self.temporary.name)/"palette.png")
            self.job.start(commands, self.end_time.value()-self.start_time.value())
            self.browse.setEnabled(False)
            self.export.setEnabled(False)
            self.cancel_button.setEnabled(True)
            self.message.setText(tr("处理中…原素材保持不变。"))
        except (OSError, ValueError) as exc:
            self.workbench.error(str(exc))

    def set_progress(self, value):
        self.progress.setValue(value)

    def done(self, success, message):
        self.browse.setEnabled(True)
        self.export.setEnabled(True)
        self.cancel_button.setEnabled(False)
        if success:
            try:
                os.rename(self.partial, self.destination)
                self.message.setText(tr("已导出：")+str(self.destination))
            except OSError as exc:
                self.message.setText(tr("导出完成，文件保留在 {0}：{1}").format(self.partial, exc))
        else:
            self.partial.unlink(missing_ok=True)
            self.message.setText(message)
        if self.temporary:
            self.temporary.cleanup()
            self.temporary = None

    def closeEvent(self, event):
        if self.job.running:
            self.job.cancel()
            self.job.process.waitForFinished(3000)
        if self.metadata.state() != QProcess.ProcessState.NotRunning:
            self.metadata.kill()
            self.metadata.waitForFinished(1000)
        event.accept()
