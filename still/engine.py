from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
import logging

from PySide6.QtCore import QAbstractNativeEventFilter, QObject, Qt, QTimer, Signal
from PySide6.QtWidgets import QWidget

from .i18n import tr
from . import win32
from .model import Store


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
    applied = Signal(object)
    restored = Signal()

    def __init__(self, store: Store, app):
        super().__init__()
        self.store, self.app = store, app
        self.surface = None
        self.player = None
        self.audio_output = None
        self.worker = None
        self.render_rect = None
        self.current_screen = ""
        self.active = None
        self.manual_pause = False
        self._reason = "尚未应用壁纸"
        self.state = "idle"
        from .static_wallpaper import StaticWallpaperTask
        self.static_task = StaticWallpaperTask(self)
        self.static_task.prepared.connect(self._static_prepared)
        self.static_task.finished.connect(self._static_complete)
        self._static_request = None
        from .media_process import MediaProcess
        self.media = MediaProcess(self)
        self.media.ready.connect(self._media_ready)
        self.media.error.connect(self._media_error)
        self.media.finished.connect(self._media_finished)
        self._dynamic_request = self._after_release = None
        self._waiting_media = self._queued_item = None
        self.closing = False
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

    def _release(self, *, cancel_request=True):
        self.tick.stop()
        if cancel_request:
            self._dynamic_request = None
        self.player = self.surface = self.worker = None
        self.audio_output = None
        self.render_rect = None
        self.media.stop()

    @property
    def busy(self):
        return self.static_task.busy or self.media.busy or self._dynamic_request is not None or self._after_release is not None or self._queued_item is not None

    def _begin_static(self, item=None):
        if self.busy:
            raise RuntimeError(tr("正在切换壁纸，请稍候再试。"))
        self._static_request = item
        self.static_task.start({"path": str(self.store.path(item)) if item else "",
            "directory": str(self.store.directory), "fit": self.store.settings.fit,
            "original": self.store.settings.original_wallpaper, "restore": item is None})
        self.changed.emit()

    def _static_prepared(self, original):
        try:
            if self.store.settings.original_wallpaper is None:
                if not isinstance(original, dict) or any(not isinstance(original.get(k), str)
                        for k in ("path", "WallpaperStyle", "TileWallpaper")):
                    raise ValueError(tr("壁纸切换任务返回了无效结果。"))
                self.store.settings.original_wallpaper = original
                try:
                    self.store.save()
                except OSError:
                    self.store.settings.original_wallpaper = None
                    raise
            self.static_task.acknowledge()
        except (OSError, ValueError) as exc:
            self.static_task.abort(str(exc))

    def _static_complete(self, success, error):
        item, self._static_request = self._static_request, None
        if not success:
            if error:
                self.error.emit(error)
            self.changed.emit()
            return
        self._after_release = lambda: self._commit_static(item)
        self._release()
        if self.media.process is None:
            self._media_finished()

    def _commit_static(self, item):
        self.active = item
        self.store.settings.active_id = item['id'] if item else ''
        self._reason, self.state = (('Windows 原生静态壁纸 · 无后台解码', 'static') if item else
                                   ('已恢复原来的 Windows 壁纸', 'idle'))
        try:
            self.store.save()
        except OSError as exc:
            self.error.emit(str(exc))
        else:
            if item:
                logging.info('Applied image wallpaper: %s', item['name'])
                self.applied.emit(item)
            else:
                self.restored.emit()
        self.changed.emit()

    def _media_finished(self):
        callback, self._after_release = self._after_release, None
        if callback and not self.closing:
            callback()
        self.changed.emit()

    def apply(self, item: dict, wait_for=None):
        if self.busy:
            raise RuntimeError(tr('正在切换壁纸，请稍候再试。'))
        if wait_for is not None and wait_for.process is not None:
            self._waiting_media, self._queued_item = wait_for, dict(item)
            wait_for.finished.connect(self._resume_apply)
            wait_for.stop()
            self.changed.emit()
            if wait_for.process is None:self._resume_apply()
            return
        if item['kind'] == 'image':
            self._begin_static(dict(item))
            return
        path = self.store.path(item)
        if not path.is_file():
            raise FileNotFoundError(tr('原文件已移动或删除，请重新导入。'))
        monitor = win32.target_monitor(self.store.settings.screen)
        self._dynamic_request = dict(item)
        self._requested_rect = monitor['rect']
        self.current_screen = self.store.settings.screen
        self.manual_pause = False
        self._release(cancel_request=False)
        self._reason, self.state = '正在读取原文件…', 'loading'
        self.media.start({'mode':'desktop', 'path':str(path), 'rect':monitor['rect'],
                          'fit':self.store.settings.fit,
                          'sound':item['kind']=='video' and self.store.settings.wallpaper_sound})
        self.changed.emit()

    def _clear_waiting(self):
        waiting, self._waiting_media = self._waiting_media, None
        self._queued_item = None
        if waiting is not None:
            try:waiting.finished.disconnect(self._resume_apply)
            except (RuntimeError,TypeError):pass

    def _resume_apply(self):
        item = self._queued_item
        self._clear_waiting()
        if item is None or self.closing:return
        try:self.apply(item)
        except (OSError,ValueError,RuntimeError) as exc:self.error.emit(str(exc))
        self.changed.emit()

    def _media_ready(self, result):
        item, self._dynamic_request = self._dynamic_request, None
        if item is None or self.closing:
            self.media.stop()
            return
        self.surface, self.worker = result['handle'], result['worker']
        self.player, self.active = self.media, item
        self.render_rect = self._requested_rect
        self.tick.start()
        self.evaluate()
        if self.closing or self.active is not item or self.media.stopping:
            return
        self.store.settings.active_id = item['id']
        try:
            self.store.save()
        except OSError as exc:
            self.error.emit(str(exc))
        else:
            logging.info('Applied %s wallpaper after first frame: %s', item['kind'], item['name'])
            self.applied.emit(item)
        self.changed.emit()

    def sync_sound(self):
        if self._dynamic_request is not None:
            enabled = self._dynamic_request['kind']=='video' and self.store.settings.wallpaper_sound
            if self.media.pending is not None:
                self.media.pending['sound'] = enabled
            elif self.media.request is not None and self.media.request.get('sound') != enabled:
                self.media.start(dict(self.media.request, sound=enabled))
        elif self.player is not None and self.active and self.active['kind']=='video':
            if self.media.request.get('sound') != self.store.settings.wallpaper_sound:
                # Configure the audio output before opening the new source.
                # Hot-detaching a playing WASAPI sink caused the reproduced hang.
                self.apply(dict(self.active))

    def _media_error(self, text):
        if self.closing:
            return
        item = self._dynamic_request or self.active
        kind = 'GIF' if item and item['kind']=='gif' else tr('视频')
        self._release()
        if not self.active or self.active['kind'] != 'image':
            self.active = None
            self.store.settings.active_id = ''
        self._reason, self.state = '播放失败 · 已回到 Windows 桌面', 'error'
        try:
            self.store.save()
        except OSError as exc:
            logging.warning('Could not save media failure state: %s', exc)
        self.error.emit(tr('{0}播放失败：{1}\n请检查文件是否完整；解码支持取决于媒体格式和显卡驱动。').format(kind, text))
        self.changed.emit()

    @property
    def reason(self):
        return tr("正在切换壁纸…") if self.busy else tr(self._reason)

    def pause(self):
        if not self.surface:
            return
        self.manual_pause = not self.manual_pause
        self.evaluate()

    def evaluate(self):
        if not self.surface or self.closing:
            return
        settings = self.store.settings
        monitor = win32.target_monitor(self.current_screen)
        if not win32.IsWindow(self.worker):
            self._release()
            self.active = None
            self._reason, self.state = '桌面层已重建，请重新应用壁纸', 'error'
            self.changed.emit()
            self.error.emit(tr('Explorer 桌面层已重建。打开设置，重新应用壁纸即可。'))
            return
        if monitor['rect'] != self.render_rect:
            self.media.control('geometry', rect=monitor['rect'])
            self.render_rect = monitor['rect']
        reason = ''
        if self.manual_pause:
            reason = '已手动暂停'
        elif self.events.locked or win32.desktop_locked():
            reason = '锁屏时自动暂停'
        elif self.events.display_off:
            reason = '显示器关闭时自动暂停'
        elif settings.battery_pause and win32.on_battery():
            reason = '电池供电时自动暂停'
        elif settings.fullscreen_pause and win32.fullscreen_on(monitor['rect']):
            reason = '全屏应用运行中 · 自动暂停'
        state = 'paused' if reason else 'playing'
        if self.state != state:
            self.media.control('pause', paused=bool(reason))
        message = reason or ('GIF 壁纸播放中 · 原文件直读' if self.active['kind']=='gif' else '视频壁纸播放中 · 原文件直读')
        if (self.state,self._reason)!=(state,message):
            self.state,self._reason=state,message
            logging.info('Playback policy: %s / %s',state,self.reason)
            self.changed.emit()

    def stop(self):
        self._clear_waiting()
        self._static_request = self._after_release = None
        self.static_task.abort()
        self._release()
        self.active = None
        self.store.settings.active_id = ''
        self.store.save()
        self._reason, self.state = '动态壁纸已停止 · Windows 桌面已显示', 'idle'
        self.changed.emit()

    def restore_original(self):
        original = self.store.settings.original_wallpaper
        if original is not None:
            self._begin_static()
            return
        self.stop()
        self._reason = '已显示 Windows 壁纸'
        if self.media.process is None:
            self.restored.emit()
        else:
            self._after_release = self.restored.emit

    def cleanup(self):
        if self.closing:
            return
        self.closing = True
        self._clear_waiting()
        self._after_release = None
        self.static_task.close()
        self._release()
        self.media.shutdown()
        self.app.removeNativeEventFilter(self.events)
        if self.power_handle:
            win32.bind(win32.user32, 'UnregisterPowerSettingNotification', W.BOOL, W.HANDLE)(self.power_handle)
            self.power_handle = None
        wts = C.WinDLL('wtsapi32')
        win32.bind(wts,'WTSUnRegisterSessionNotification',W.BOOL,W.HWND)(int(self.notifier.winId()))
        self.notifier.close()
        self.notifier.deleteLater()
