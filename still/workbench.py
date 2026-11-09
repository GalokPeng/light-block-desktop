from __future__ import annotations

from datetime import datetime
from pathlib import Path
import time
import uuid

from shiboken6 import isValid
from PySide6.QtCore import QObject, QTimer, Signal
from PySide6.QtGui import QCursor
from PySide6.QtWidgets import QApplication, QFileDialog, QMessageBox

from .productivity_model import ToolsDB


class Workbench(QObject):
    apps_indexed = Signal(list)
    changed = Signal()

    def __init__(self, controller):
        super().__init__()
        self.controller = controller
        self.db = ToolsDB(controller.store.directory)
        self.windows = {}
        self.selector = None
        self._clipboard = self._notes = self._focus = self._recorder = None
        self.record_bar = None
        self.record_frame = None
        self.record_pending = None
        self.record_started = 0
        self.record_timer = QTimer(self)
        self.record_timer.setSingleShot(True)
        self.record_timer.timeout.connect(self.start_recording)
        self.app_index = []
        self.indexer = None
        self.background_threads = []
        from .productivity_model import RenameJournal
        self.last_rename = RenameJournal(controller.store.directory).latest()
        self.closing = False
        self.selection_pending = False
        self.selection_request = None
        self.selection_timer = QTimer(self)
        self.selection_timer.setSingleShot(True)
        self.selection_timer.timeout.connect(self.begin_selection)
        if controller.store.settings.clipboard_enabled:
            self.clipboard()
        if controller.store.settings.notes_background:
            self.notes()
        if controller.store.settings.focus_active:
            self.focus()

    def error(self, message):
        self.controller.report_error(message)

    def show(self, key, factory, *, activate=True):
        if key in self.windows and isValid(self.windows[key]):
            window = self.windows[key]
        else:
            window = factory()
            self.windows[key] = window
            window.destroyed.connect(lambda *_: self.window_gone(key))
        if activate:
            window.showNormal()
        else:
            window.show()
        window.raise_()
        if activate:
            window.activateWindow()
        return window

    def window_gone(self, key):
        self.windows.pop(key, None)
        if not self.closing:
            QTimer.singleShot(0, self.controller.maybe_quit)

    def invoke(self, action):
        try:
            if action in {"capture", "longshot"}:
                self.open_capture(action)
            elif action == "record":
                self.record()
            elif action == "media":
                from .media_tools import MediaStudio
                self.show("media", lambda: MediaStudio(self))
            elif action == "launcher":
                from .launcher import Launcher
                self.show("launcher", lambda: Launcher(self))
            elif action == "clipboard":
                from .clipboard_tools import ClipboardWindow
                self.show("clipboard", lambda: ClipboardWindow(self))
            elif action == "notes":
                from .notes import NotesWindow
                self.show("notes", lambda: NotesWindow(self))
            elif action == "files":
                from .file_tools import FileWorkbench
                self.show("files", lambda: FileWorkbench(self))
            elif action == "focus":
                from .focus import FocusWindow
                self.show("focus", lambda: FocusWindow(self))
            elif action == "clipboard_image":
                image = QApplication.clipboard().image()
                if image.isNull():
                    raise ValueError("剪贴板中没有图片。")
                self.edit_image(image)
        except (OSError, ValueError, RuntimeError) as exc:
            self.error(str(exc))

    def edit_image(self, image, *, wheel_sizes=True):
        if image.isNull():
            return
        from .capture import CaptureEditor
        return self.show("editor-"+uuid.uuid4().hex, lambda: CaptureEditor(self, image, wheel_sizes))

    def pin(self, image):
        if not image.isNull():
            from .capture import PinWindow
            return self.show("pin-"+uuid.uuid4().hex, lambda: PinWindow(self, image))

    @property
    def recording(self):
        return self.record_pending is not None or bool(self._recorder and self._recorder.active)

    def open_capture(self, mode):
        if self.closing or self.selection_pending or self.recording:
            return
        if "longshot" in self.windows:
            self.windows["longshot"].raise_()
            return
        if self.selector is not None and isValid(self.selector):
            self.selector.raise_()
            return
        screen = QApplication.screenAt(QCursor.pos()) or QApplication.primaryScreen()
        self.selection_pending = True
        window = self.controller.window
        was_visible = window is not None and window.isVisible() and not window.isMinimized()
        if was_visible:
            window.hide()
        self.selection_request = (screen, mode)
        self.selection_timer.start(180 if was_visible else 0)

    def begin_selection(self):
        if self.closing or self.selection_request is None:
            return
        screen, mode = self.selection_request

        def release():
            self.selector = None
            self.selection_pending = False
            self.selection_request = None

        def cancelled():
            release()
            if not self.closing:
                QTimer.singleShot(0, self.controller.maybe_quit)

        def action_selected(action, image, rect):
            release()
            if not self.closing:
                self.finish_capture(action, image, rect, screen)
                QTimer.singleShot(0, self.controller.maybe_quit)

        if screen is None or not isValid(screen) or screen not in self.controller.app.screens():
            cancelled()
            return
        try:
            from .capture_overlay import CaptureOverlay
            selector = CaptureOverlay(screen, mode)
            self.selector = selector
            selector.finished.connect(action_selected)
            selector.cancelled.connect(cancelled)
            selector.show()
            selector.raise_()
            selector.activateWindow()
        except (RuntimeError, OSError) as exc:
            cancelled()
            self.error(str(exc))

    def finish_capture(self, action, image, rect, screen):
        try:
            if action == "record":
                self.record_region(rect, screen)
                return
            if action == "longshot":
                from .scroll_capture import ScrollCapture
                self.show("longshot", lambda: ScrollCapture(self, image, rect, screen), activate=False)
                return
            if action == "pin":
                self.pin(image)
            elif action == "copy":
                QApplication.clipboard().setImage(image)
            elif action == "save":
                self.save_capture(image)
        except (OSError, RuntimeError, ValueError) as exc:
            self.error(str(exc))

    def save_capture(self, image):
        store = self.controller.store
        directory = Path(store.settings.capture_directory or store.directory)
        name = "Still_"+datetime.now().strftime("%Y%m%d_%H%M%S")+".png"
        path, _ = QFileDialog.getSaveFileName(None, "保存截图", str(directory/name), "PNG 图片 (*.png)")
        if path:
            path = Path(path)
            if path.suffix.lower() != ".png":
                path = path.with_suffix(".png")
                if path.exists():
                    raise FileExistsError("该 PNG 路径已有文件，请选择另一个名称。")
            if not image.save(str(path), "PNG"):
                raise OSError("截图保存失败，请检查路径和剩余空间。")
            store.settings.capture_directory = str(path.parent)
            store.save()

    def clipboard(self):
        if self._clipboard is None:
            from .clipboard_tools import ClipboardEngine
            self._clipboard = ClipboardEngine(self)
        return self._clipboard

    def notes(self):
        if self._notes is None:
            from .notes import NotesEngine
            self._notes = NotesEngine(self)
        return self._notes

    def focus(self):
        if self._focus is None:
            from .focus import FocusEngine
            self._focus = FocusEngine(self)
        return self._focus

    def recorder(self):
        if self._recorder is None:
            from .media_tools import Recorder
            self._recorder = Recorder(self)
            self._recorder.changed.connect(self.record_changed)
            self._recorder.completed.connect(self.record_completed)
            self._recorder.error.connect(self.record_error)
        return self._recorder

    def record(self):
        if self.record_pending is not None:
            self.record_timer.stop()
            self.record_pending = None
            self.close_record_bar()
            self.record_changed()
            return
        if self._recorder is not None and self._recorder.active:
            self._recorder.stop()
            if self.record_bar:
                self.record_bar.finishing()
            return
        self.open_capture("record")

    def record_region(self, rect, screen):
        from .recording_ui import RecordingBar
        from .capture_frame import CaptureFrame
        from .capture_overlay import logical_region
        settings = self.controller.store.settings
        lossless = settings.record_lossless
        suffix = ".mkv" if lossless else ".mp4"
        directory = self.controller.store.directory / "recordings"
        name = "Still_"+datetime.now().strftime("%Y%m%d_%H%M%S")+"_"+uuid.uuid4().hex[:6]+suffix
        self.record_bar = RecordingBar(self, rect, screen)
        self.record_frame = CaptureFrame(logical_region(rect, screen), screen, recording=True)
        self.record_frame.show()
        self.record_pending = (rect, directory/name, lossless, settings.record_fps)
        self.record_changed()
        self.record_timer.start(180)

    def start_recording(self):
        request, self.record_pending = self.record_pending, None
        if self.closing or request is None:
            return
        rect, path, lossless, fps = request
        try:
            self.record_started = time.monotonic()
            self.recorder().start(rect, path, lossless=lossless, fps=fps)
            if self.record_bar:
                self.record_bar.start_clock()
        except (OSError, ValueError, RuntimeError) as exc:
            self.record_error(str(exc))
            self.record_changed()

    def close_record_bar(self):
        bar, self.record_bar = self.record_bar, None
        if bar is not None and isValid(bar):
            bar.close()
            bar.deleteLater()
        frame, self.record_frame = self.record_frame, None
        if frame is not None and isValid(frame):
            frame.close()

    def record_changed(self):
        active = self.recording
        self.controller.record_action.setEnabled(active)
        self.controller.configure_hotkeys()
        self.controller.tray.setToolTip("轻块桌面 · 录屏中，Ctrl+Alt+R 停止" if active else "轻块桌面")
        self.changed.emit()

    def record_completed(self, path):
        self.close_record_bar()
        if self.closing or self.controller.quit_timer.isActive():
            return
        from .recording_ui import RecordingResult
        duration = time.monotonic()-self.record_started
        self.show("recording-result-"+uuid.uuid4().hex, lambda: RecordingResult(self, path, duration))

    def record_error(self, message):
        self.close_record_bar()
        if self.closing:
            import logging
            logging.error(message)
            return
        self.error(message)

    def ensure_app_index(self):
        if self.indexer is None:
            from .launcher import AppIndexer
            self.indexer = AppIndexer(self)
            self.indexer.indexed.connect(self.index_ready)
            self.indexer.finished.connect(self.thread_finished)
            self.indexer.start()

    def index_ready(self, entries):
        self.app_index = entries
        self.apps_indexed.emit(entries)

    def track_thread(self, thread):
        self.background_threads.append(thread)
        thread.finished.connect(self.thread_finished)

    def thread_finished(self):
        thread = self.sender()
        if thread is self.indexer:
            self.indexer = None
        if thread in self.background_threads:
            self.background_threads.remove(thread)
        thread.deleteLater()
        if not self.closing:
            QTimer.singleShot(0, self.controller.maybe_quit)

    def threads_running(self):
        return bool((self.indexer is not None and self.indexer.isRunning()) or
                    any(isValid(thread) and thread.isRunning() for thread in self.background_threads))

    def interrupt_threads(self):
        if self.indexer is not None and self.indexer.isRunning():
            self.indexer.requestInterruption()
        for thread in self.background_threads:
            if isValid(thread) and thread.isRunning():
                thread.requestInterruption()

    def needs_background(self):
        return bool(self.windows or self.selector is not None or self.selection_pending or self.threads_running() or self.controller.store.settings.clipboard_enabled or
                    self.recording or
                    (self._notes is not None and (self._notes.pins or self._notes.reminder.isActive())) or
                    (self._focus is not None and self._focus.active))

    def cleanup(self):
        self.closing = True
        self.record_timer.stop()
        self.record_pending = None
        self.close_record_bar()
        self.selection_timer.stop()
        self.selection_pending = False
        self.selection_request = None
        if self.selector is not None and isValid(self.selector):
            self.selector.close()
        self.selector = None
        if self._recorder is not None:
            self._recorder.shutdown()
        for window in list(self.windows.values()):
            if isValid(window):
                window.close()
        if self._clipboard is not None:
            self._clipboard.cleanup()
        if self._notes is not None:
            self._notes.cleanup()
        if self._focus is not None:
            self._focus.cleanup()
        if self.indexer is not None and self.indexer.isRunning():
            self.indexer.requestInterruption()
            if not self.indexer.wait():
                raise RuntimeError("应用仍在等待索引线程安全结束。")
        for thread in self.background_threads:
            if isValid(thread) and thread.isRunning():
                thread.requestInterruption()
                if not thread.wait():
                    raise RuntimeError("应用仍在等待文件线程安全结束。")
        self.db.connection.close()
