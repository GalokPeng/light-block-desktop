from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
import logging
from pathlib import Path

from PySide6.QtCore import QAbstractNativeEventFilter, QObject, Qt, QTimer, QUrl, Signal
from PySide6.QtGui import QImageReader
from PySide6.QtWidgets import QWidget

from . import win32
from .model import Store, item_path


class NativeEvents(QAbstractNativeEventFilter):
    """Suspend animation on session lock / display power-off without frame polling."""
    def __init__(self, owner):
        super().__init__()
        self.owner = owner
        self.locked = False
        self.display_off = False

    def nativeEventFilter(self, event_type, message):
        msg = W.MSG.from_address(int(message))
        if msg.message == 0x02B1:  # WM_WTSSESSION_CHANGE
            if msg.wParam in (7, 8):
                self.locked = msg.wParam == 7
                QTimer.singleShot(0, self.owner.evaluate)
        elif msg.message == 0x0218:  # WM_POWERBROADCAST
            if msg.wParam == 0x8013 and msg.lParam:
                # POWERBROADCAST_SETTING: GUID (16), DWORD length, DWORD data.
                length = W.DWORD.from_address(msg.lParam + 16).value
                if length == 4:
                    state = W.DWORD.from_address(msg.lParam + 20).value
                    self.display_off = state == 0
                    QTimer.singleShot(0, self.owner.evaluate)
            elif msg.wParam in (7, 18):
                self.display_off = False
                QTimer.singleShot(0, self.owner.evaluate)
        return False, 0


class WallpaperEngine(QObject):
    changed = Signal()
    error = Signal(str)

    def __init__(self, store: Store, app):
        super().__init__()
        self.store, self.app = store, app
        self.surface = None
        self.player = None
        self.worker = None
        self.render_rect = None
        self.current_screen = ""
        self.active = None
        self.manual_pause = False
        self.reason = "尚未应用壁纸"
        self.state = "idle"
        self.tick = QTimer(self)
        self.tick.setInterval(2000)
        self.tick.setTimerType(Qt.TimerType.CoarseTimer)
        self.tick.timeout.connect(self.evaluate)
        self.events = NativeEvents(self)
        app.installNativeEventFilter(self.events)
        self.notifier = QWidget(None, Qt.WindowType.Tool)
        self.notifier.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen)
        handle = int(self.notifier.winId())
        wts = C.WinDLL("wtsapi32")
        win32.bind(wts, "WTSRegisterSessionNotification", W.BOOL, W.HWND, W.DWORD)(handle, 0)
        # GUID_CONSOLE_DISPLAY_STATE, see Windows power setting notifications.
        import uuid
        guid = (C.c_ubyte * 16).from_buffer_copy(uuid.UUID("6fe69556-704a-47a0-8f24-c28d936fda47").bytes_le)
        register = win32.bind(win32.user32, "RegisterPowerSettingNotification", W.HANDLE, W.HANDLE, C.c_void_p, W.DWORD)
        self.power_handle = register(handle, C.byref(guid), 0)
        app.primaryScreenChanged.connect(lambda *_: self.evaluate())
        app.screenAdded.connect(lambda *_: self.evaluate())
        app.screenRemoved.connect(lambda *_: self.evaluate())
        self.app.aboutToQuit.connect(self.cleanup)

    def _release(self):
        self.tick.stop()
        player, surface = self.player, self.surface
        self.player = self.surface = self.worker = None
        self.render_rect = None
        if player:
            player.stop()
            player.setSource(QUrl())
            player.setVideoOutput(None)
            player.deleteLater()
        if surface:
            surface.hide()
            surface.close()
            surface.deleteLater()

    def _remember_original(self):
        if self.store.settings.original_wallpaper is None:
            import shutil
            original = win32.current_wallpaper()
            source = Path(original["path"])
            if original["path"] and source.is_file():
                backup = self.store.directory / ("original-wallpaper" + (source.suffix or ".bmp"))
                shutil.copyfile(source, backup)
                original["path"] = str(backup)
            self.store.settings.original_wallpaper = original
            self.store.save()

    def apply(self, item: dict):
        path = item_path(item)
        if not path.is_file():
            raise FileNotFoundError("原文件已移动或删除，请重新导入。")
        fit = self.store.settings.fit
        if item["kind"] == "image":
            reader = QImageReader(str(path))
            reader.setAutoTransform(True)
            if not reader.canRead():
                raise ValueError("无法读取这张图片，请检查文件内容。")
            # Windows reads these originals directly. Other supported formats use
            # a full-resolution lossless PNG; no JPEG encoding or thumbnail input.
            native_path = path
            if path.suffix.lower() not in {".jpg", ".jpeg", ".png", ".bmp"}:
                image = reader.read()
                if image.isNull():
                    raise ValueError(reader.errorString())
                native_path = self.store.directory / "native-wallpaper.png"
                if not image.save(str(native_path), "PNG"):
                    raise OSError("无法写入原分辨率 PNG。")
                del image
            self._remember_original()
            win32.set_wallpaper(str(native_path), fit)
            self._release()
            self.active = item
            self.reason, self.state = "Windows 原生静态壁纸 · 无后台解码", "static"
        else:
            monitor = win32.target_monitor(self.store.settings.screen)
            worker = win32.desktop_worker()
            # GIF and video must share the native video presentation path. A
            # raster QWidget can receive paintEvent yet remain invisible under
            # Explorer's desktop compositor. FFmpeg reads GIF frames directly;
            # there is no conversion, temporary video, or re-encoding step.
            from PySide6.QtMultimedia import QMediaPlayer
            from PySide6.QtMultimediaWidgets import QVideoWidget
            candidate = QVideoWidget(None)
            candidate.setWindowFlags(Qt.WindowType.FramelessWindowHint | Qt.WindowType.Tool | Qt.WindowType.WindowDoesNotAcceptFocus)
            candidate.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
            candidate.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
            candidate.setAspectRatioMode({"fill": Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                                          "fit": Qt.AspectRatioMode.KeepAspectRatio,
                                          "center": Qt.AspectRatioMode.KeepAspectRatio,
                                          "stretch": Qt.AspectRatioMode.IgnoreAspectRatio}[fit])
            player = QMediaPlayer(self)
            player.setVideoOutput(candidate)
            player.setLoops(QMediaPlayer.Loops.Infinite)
            player.tracksChanged.connect(lambda p=player: p.setActiveAudioTrack(-1))
            player.errorOccurred.connect(lambda code, text, p=player: self._media_error(p, text))
            player.mediaStatusChanged.connect(lambda status, p=player: self._media_status(p, status))
            candidate.resize(monitor["rect"][2], monitor["rect"][3])
            try:
                # Never present an unparented fullscreen window, even for one frame.
                win32.attach(int(candidate.winId()), worker, monitor["rect"])
            except Exception:
                if player:
                    player.deleteLater()
                candidate.deleteLater()
                raise
            self._release()
            self.surface, self.player, self.worker, self.active = candidate, player, worker, item
            self.current_screen = self.store.settings.screen
            self.render_rect = monitor["rect"]
            self.manual_pause = False
            self.reason, self.state = "正在读取原文件…", "loading"
            candidate.show()
            win32.position(int(candidate.winId()), worker, monitor["rect"])
            player.setSource(QUrl.fromLocalFile(str(path)))
            player.play()
            self.tick.start()
            self.evaluate()
        self.store.settings.active_id = item["id"]
        self.store.save()
        logging.info("Applied %s wallpaper: %s", item["kind"], item["name"])
        self.changed.emit()

    def _media_status(self, player, status):
        if player is not self.player:
            return
        from PySide6.QtMultimedia import QMediaPlayer
        logging.info("Media status: %s; hasVideo=%s", status.name, player.hasVideo())
        if status == QMediaPlayer.MediaStatus.LoadedMedia and not player.hasVideo():
            self._media_error(player, "所选文件没有视频轨道。")

    def _media_error(self, player, text):
        if player is not self.player:
            return
        kind = "GIF" if self.active and self.active["kind"] == "gif" else "视频"
        self._release()
        self.active = None
        self.store.settings.active_id = ""
        self.store.save()
        self.reason, self.state = "播放失败 · 已回到 Windows 桌面", "error"
        self.error.emit(f"{kind}播放失败：{text}\n请检查文件是否完整；解码支持取决于媒体格式和显卡驱动。")
        self.changed.emit()

    def pause(self):
        if not self.surface:
            return
        self.manual_pause = not self.manual_pause
        self.evaluate()

    def evaluate(self):
        if not self.surface:
            return
        settings = self.store.settings
        monitor = win32.target_monitor(self.current_screen)
        if not win32.IsWindow(self.worker):
            # Explorer may destroy child windows during restart: rebuild, do not
            # call methods on the old Qt native handle or put a window on top.
            item = self.active
            self._release()
            self.active = None
            self.reason, self.state = "桌面层已重建，请重新应用壁纸", "error"
            self.changed.emit()
            self.error.emit("Explorer 桌面层已重建。打开设置，重新应用壁纸即可。")
            return
        if monitor["rect"] != self.render_rect:
            win32.position(int(self.surface.winId()), self.worker, monitor["rect"])
            self.render_rect = monitor["rect"]
        reason = ""
        if self.manual_pause:
            reason = "已手动暂停"
        elif self.events.locked or win32.desktop_locked():
            reason = "锁屏时自动暂停"
        elif self.events.display_off:
            reason = "显示器关闭时自动暂停"
        elif settings.battery_pause and win32.on_battery():
            reason = "电池供电时自动暂停"
        elif settings.fullscreen_pause and win32.fullscreen_on(monitor["rect"]):
            reason = "全屏应用运行中 · 自动暂停"
        if self.player:
            from PySide6.QtMultimedia import QMediaPlayer
            if reason and self.player.playbackState() == QMediaPlayer.PlaybackState.PlayingState:
                self.player.pause()
            elif not reason and self.player.playbackState() == QMediaPlayer.PlaybackState.PausedState:
                self.player.play()
        state = "paused" if reason else "playing"
        message = reason or ("GIF 壁纸播放中 · 原文件直读" if self.active["kind"] == "gif" else "视频壁纸播放中 · 原文件直读")
        if (self.state, self.reason) != (state, message):
            self.state, self.reason = state, message
            logging.info("Playback policy: %s / %s", state, message)
            self.changed.emit()

    def stop(self):
        self._release()
        self.active = None
        self.store.settings.active_id = ""
        self.store.save()
        self.reason, self.state = "动态壁纸已停止 · Windows 桌面已显示", "idle"
        self.changed.emit()

    def restore_original(self):
        original = self.store.settings.original_wallpaper
        if original is not None:
            win32.set_wallpaper(original["path"], original=original)
        self.stop()
        self.reason = "已恢复原来的 Windows 壁纸" if original else "已显示 Windows 壁纸"
        self.changed.emit()

    def cleanup(self):
        self._release()
        self.app.removeNativeEventFilter(self.events)
        if self.power_handle:
            win32.bind(win32.user32, "UnregisterPowerSettingNotification", W.BOOL, W.HANDLE)(self.power_handle)
            self.power_handle = None
        wts = C.WinDLL("wtsapi32")
        win32.bind(wts, "WTSUnRegisterSessionNotification", W.BOOL, W.HWND)(int(self.notifier.winId()))
