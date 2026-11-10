from __future__ import annotations

import argparse
import gc
import logging
import os
from logging.handlers import RotatingFileHandler
from pathlib import Path
import sys

from PySide6.QtCore import QTimer, Qt
from PySide6.QtGui import QAction, QColor, QFont, QIcon, QPainter, QPen, QPixmap
from PySide6.QtNetwork import QLocalServer, QLocalSocket
from PySide6.QtWidgets import QApplication, QMenu, QMessageBox, QSystemTrayIcon

from . import win32
from .i18n import install_qt_translation, set_language, tr
from .engine import WallpaperEngine
from .model import APP_NAME, Store, VERSION


def make_icon():
    pix = QPixmap(64, 64)
    pix.fill(Qt.GlobalColor.transparent)
    p = QPainter(pix)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(QColor("#a6e4ca"))
    p.drawRoundedRect(2, 2, 60, 60, 18, 18)
    p.setPen(QPen(QColor("#1d3b31"), 4, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
    p.drawArc(15, 13, 35, 35, 20*16, 140*16)
    p.drawLine(14, 43, 26, 31)
    p.drawLine(26, 31, 39, 45)
    p.drawLine(39, 45, 51, 36)
    p.end()
    return QIcon(pix)


class Controller:
    def __init__(self, app, store, background=False, check_updates=True):
        self.app, self.store = app, store
        self.apply_language()
        self.check_updates = check_updates
        self.release_check = None
        self.window = None
        self.organizer = None
        self.native_taskbar_style = None
        self.workbench = None
        self.hotkeys = None
        self.closing = False
        self.quit_timer = QTimer(app)
        self.quit_timer.setInterval(100)
        self.quit_timer.timeout.connect(self.finish_quit)
        self.icon = make_icon()
        app.setWindowIcon(self.icon)
        self.engine = WallpaperEngine(store, app)
        self.engine.error.connect(self.report_error)
        self.tray = QSystemTrayIcon(self.icon, app)
        self.menu = None
        self.build_tray_menu()
        self.tray.activated.connect(self.activated)
        self.engine.changed.connect(self.update_tray)
        self.tray.show()
        app.aboutToQuit.connect(self.cleanup)
        if store.settings.clipboard_enabled or store.settings.notes_background or store.settings.focus_active:
            self.get_workbench()
        if store.settings.hotkeys_enabled or store.settings.capture_hotkey:
            self.configure_hotkeys()
        if store.settings.organizer_enabled or (store.directory / "desktop-icon-state.json").exists():
            self.get_organizer()
        if store.settings.taskbar_style_enabled:
            # self.open_dock()  # Legacy Dock and tray.
            self.open_native_taskbar_style()
        self.update_tray()
        if not background or not QSystemTrayIcon.isSystemTrayAvailable():
            self.show_window()
        # Let Explorer finish starting on login, with a bounded retry window.
        self.restore_attempts = 0
        if store.settings.restore_on_start and store.settings.active_id:
            QTimer.singleShot(1800 if background else 150, self.restore_last)
        elif background and self.window is None and not self.needs_background():
            QTimer.singleShot(200, app.quit)
        if store.warning:
            QTimer.singleShot(300, lambda: self.report_error(store.warning))

    def build_tray_menu(self):
        old_menu = self.menu
        menu = QMenu()
        self.menu = menu
        open_action = QAction(tr("打开轻块桌面设置"), menu)
        open_action.triggered.connect(lambda: QTimer.singleShot(0, self.show_from_tray))
        menu.addAction(open_action)
        self.pause_action = QAction(tr("暂停动态壁纸"), menu)
        self.pause_action.triggered.connect(self.engine.pause)
        menu.addAction(self.pause_action)
        self.stop_action = QAction(tr("停止动态壁纸"), menu)
        self.stop_action.triggered.connect(self.stop)
        menu.addAction(self.stop_action)
        self.organizer_action = QAction(tr("开启桌面收纳"), menu)
        self.organizer_action.setCheckable(True)
        self.organizer_action.setChecked(self.store.settings.organizer_enabled)
        self.organizer_action.toggled.connect(lambda value: self.get_organizer().set_enabled(value))
        menu.addAction(self.organizer_action)
        menu.addSeparator()
        for text, action in ((tr("截屏"), "capture"), (tr("快速启动  Ctrl+Alt+Space"), "launcher"), (tr("剪贴板历史"), "clipboard"), (tr("便签与任务"), "notes")):
            item = QAction(text, menu)
            item.triggered.connect(lambda checked=False, a=action: self.get_workbench().invoke(a))
            menu.addAction(item)
            if action == "capture":
                self.capture_action = item
        self.update_capture_shortcut()
        self.record_action = QAction(tr("结束录屏  Ctrl+Alt+R"), menu)
        self.record_action.setEnabled(bool(self.workbench and self.workbench.recording))
        self.record_action.triggered.connect(lambda: self.get_workbench().record())
        menu.addAction(self.record_action)
        menu.addSeparator()
        restore = QAction(tr("恢复 Windows 壁纸"), menu)
        restore.triggered.connect(self.restore)
        menu.addAction(restore)
        menu.addSeparator()
        exit_action = QAction(tr("退出轻块桌面"), menu)
        exit_action.triggered.connect(self.request_quit)
        menu.addAction(exit_action)
        self.tray.setContextMenu(menu)
        if old_menu is not None:
            old_menu.deleteLater()

    def apply_language(self):
        set_language(self.store.settings.settings_language)
        self.app.setApplicationDisplayName(tr(APP_NAME))
        install_qt_translation(self.app)

    def change_language(self, preference):
        settings = self.store.settings
        previous = settings.settings_language
        if previous == preference:
            return
        settings.settings_language = preference
        try:
            self.store.save()
        except OSError as exc:
            settings.settings_language = previous
            self.report_error(str(exc))
            return
        self.apply_language()
        self.build_tray_menu()
        self.update_tray()
        if self.window is not None:
            old = self.window
            new = self.make_window()
            new.restoreGeometry(old.saveGeometry())
            new.select(old.selected)
            page = old.pages.currentIndex()
            new.nav_buttons[page].setChecked(True)
            new.pages.setCurrentIndex(page)
            self.window = new
            old.closed.disconnect(self.window_closed)
            old.close()
            new.show()
            new.activateWindow()

    def updater(self):
        if self.release_check is None:
            from .updates import ReleaseCheck
            self.release_check = ReleaseCheck(self.app)
        return self.release_check

    def activated(self, reason):
        if reason == QSystemTrayIcon.ActivationReason.DoubleClick:
            self.show_from_tray()

    def show_from_tray(self):
        self.show_window(from_tray=True)

    def show_window(self, *, from_tray=False):
        self.quit_timer.stop()
        if self.window is None:
            self.window = self.make_window()
        if self.window.isMinimized():
            self.window.setWindowState(self.window.windowState() & ~Qt.WindowState.WindowMinimized)
        self.window.show()
        self.window.raise_()
        if from_tray:
            win32.activate_from_tray(int(self.window.winId()))
        else:
            self.window.activateWindow()

    def make_window(self):
        from .ui import MainWindow
        window = MainWindow(self)
        window.closed.connect(self.window_closed)
        return window

    def window_closed(self):
        self.window = None
        if not self.needs_background():
            QTimer.singleShot(0, self.app.quit)
        else:
            QTimer.singleShot(0, gc.collect)

    def needs_background(self):
        return (self.engine.surface is not None or self.store.settings.organizer_enabled or
                self.native_taskbar_style is not None or
                bool(self.hotkeys and self.hotkeys.registered)
                or (self.workbench is not None and self.workbench.needs_background()))

    def maybe_quit(self):
        if self.window is None and not self.needs_background() and not self.closing:
            self.request_quit()

    def request_quit(self):
        if self.workbench is not None:
            self.workbench.interrupt_threads()
            recorder = self.workbench._recorder
            if recorder is not None and recorder.active:
                recorder.stop()
            if self.workbench.threads_running() or (recorder is not None and recorder.active):
                self.quit_timer.start()
                self.tray.showMessage(tr("轻块桌面"), tr("正在保存录屏并结束文件任务，完成后退出。"))
                return
        self.app.quit()

    def finish_quit(self):
        recorder = self.workbench._recorder if self.workbench is not None else None
        if (self.workbench is None or not self.workbench.threads_running()) and not (recorder is not None and recorder.active):
            self.quit_timer.stop()
            self.app.quit()

    def get_workbench(self):
        if self.workbench is None:
            from .workbench import Workbench
            self.workbench = Workbench(self)
        return self.workbench

    def ensure_hotkeys(self):
        if self.hotkeys is None:
            from .hotkeys import Hotkeys
            self.hotkeys = Hotkeys(self)
            self.hotkeys.triggered.connect(self.handle_hotkey)
        return self.hotkeys

    def handle_hotkey(self, action):
        recording = bool(self.workbench and self.workbench.recording)
        if self.window and self.window.isVisible() and self.window.pages.currentIndex() == 2:
            if not (action == "record" and recording) and self.window.pages.widget(2).receive_hotkey(action):
                return
        self.get_workbench().invoke(action)

    def update_capture_shortcut(self):
        from .hotkeys import shortcut_label
        settings = self.store.settings
        label = shortcut_label(settings.capture_shortcut) if settings.capture_hotkey else ""
        self.capture_action.setText(tr("截屏  {0}").format(label) if label else tr("截屏"))

    def set_capture_shortcut(self, portable):
        from .hotkeys import capture_binding, shortcut_label
        capture_binding(portable)
        settings = self.store.settings
        previous = settings.capture_shortcut
        if settings.capture_hotkey and not self.ensure_hotkeys().replace_capture(portable):
            raise ValueError(tr("无法注册 {0}。它可能已被系统或其他应用占用。").format(shortcut_label(portable)))
        settings.capture_shortcut = portable
        try:
            self.store.save()
        except OSError:
            settings.capture_shortcut = previous
            self.configure_hotkeys()
            raise
        self.update_capture_shortcut()

    def configure_hotkeys(self):
        if self.closing:
            return
        settings = self.store.settings
        recording = bool(self.workbench and self.workbench.recording)
        capture = settings.capture_shortcut if settings.capture_hotkey else None
        failed = self.ensure_hotkeys().configure(settings.hotkeys_enabled, recording, capture)
        self.update_capture_shortcut()
        if failed:
            text = tr("快捷键已被其他应用占用：")+"、".join(failed)+tr("。仍可从界面或托盘使用。")
            if self.window:
                self.window.show_notice(text)
            else:
                self.tray.showMessage(tr("轻块桌面"), text)

    def cleanup(self):
        self.closing = True
        self.close_native_taskbar_style()
        if self.hotkeys is not None:
            self.hotkeys.cleanup()
        if self.workbench is not None:
            self.workbench.cleanup()

    # def open_dock(self):
    #     from .dock import Dock
    #     self.dock = Dock(self)
    #     self.dock.taskbar_style.start()

    # def close_dock(self):
    #     if self.dock is not None:
    #         dock = self.dock
    #         self.dock = None
    #         dock.stop()

    def open_native_taskbar_style(self):
        from .native_taskbar_style import NativeTaskbarStyle
        style = NativeTaskbarStyle(self.app, self.store.settings)
        self.native_taskbar_style = style
        style.status.connect(self.refresh_taskbar_controls)
        style.failed.connect(self.taskbar_style_failed)
        style.start()

    def close_native_taskbar_style(self):
        style, self.native_taskbar_style = self.native_taskbar_style, None
        if style is not None:
            style.stop()

    def taskbar_style_failed(self, message):
        self.set_taskbar_style(False)
        self.report_error(message)

    def refresh_taskbar_controls(self, *_):
        if self.window is not None:
            self.window.pages.widget(1).refresh_taskbar_options()

    def set_taskbar_style(self, enabled):
        settings = self.store.settings
        if not enabled:
            self.close_native_taskbar_style()
        if enabled != settings.taskbar_style_enabled:
            settings.taskbar_style_enabled = enabled
            try:
                self.store.save()
            except OSError as exc:
                settings.taskbar_style_enabled = False
                self.report_error(str(exc))
            else:
                if enabled:
                    self.open_native_taskbar_style()
        self.refresh_taskbar_controls()
        QTimer.singleShot(0, self.maybe_quit)

    def show_organizer_settings(self):
        self.show_window()
        self.window.pages.setCurrentIndex(1)

    def get_organizer(self):
        if self.organizer is None:
            from .organizer import OrganizerManager
            self.organizer = OrganizerManager(self)
            self.organizer.error.connect(self.report_error)
            self.organizer.changed.connect(self.organizer_changed)
            self.app.aboutToQuit.connect(self.organizer.cleanup)
            self.organizer.initialize()
        return self.organizer

    def organizer_changed(self):
        self.organizer_action.blockSignals(True)
        self.organizer_action.setChecked(self.store.settings.organizer_enabled)
        self.organizer_action.blockSignals(False)
        QTimer.singleShot(0, self.maybe_quit)

    def update_tray(self):
        dynamic = self.engine.surface is not None
        self.pause_action.setEnabled(dynamic)
        self.stop_action.setEnabled(dynamic)
        self.pause_action.setText(tr("继续动态壁纸") if self.engine.manual_pause else tr("暂停动态壁纸"))
        self.tray.setToolTip(tr("轻块桌面 · ")+self.engine.reason)

    def restore_last(self):
        item = self.store.find(self.store.settings.active_id)
        if not item:
            return
        try:
            self.engine.apply(item)
            if item["kind"] == "image" and self.window is None and not self.needs_background():
                self.app.quit()
        except OSError as exc:
            self.restore_attempts += 1
            if self.restore_attempts < 6 and item["kind"] != "image":
                QTimer.singleShot(2500, self.restore_last)
            else:
                self.report_error(str(exc))
        except (ValueError, RuntimeError) as exc:
            self.report_error(str(exc))

    def restore(self):
        try:
            self.engine.restore_original()
            if self.window is None and not self.needs_background():
                self.app.quit()
        except OSError as exc:
            self.report_error(str(exc))

    def stop(self):
        self.engine.stop()
        if self.window is None and not self.needs_background():
            self.app.quit()

    def report_error(self, text):
        logging.error(text)
        if self.window is not None and self.window.isVisible() and not self.window.isMinimized():
            self.window.show_error(text)
        else:
            self.tray.showMessage(tr("轻块桌面"), text, QSystemTrayIcon.MessageIcon.Warning, 8000)


def run():
    parser = argparse.ArgumentParser(description=tr("轻块桌面 桌面工作台 v")+VERSION)
    parser.add_argument("--background", action="store_true", help="Start quietly in the tray")
    parser.add_argument("--quit", action="store_true", help="Quit the running instance for this configuration")
    parser.add_argument("--data-dir", type=Path, help="Override local config directory")
    parser.add_argument("--screenshot", type=Path, help="Capture the settings window, then exit; no wallpaper applied")
    args = parser.parse_args()
    if sys.platform != "win32":
        raise SystemExit(tr("轻块桌面 v")+VERSION+tr(" 目前仅支持 Windows 10 / 11。"))
    # Qt's DirectWrite fallback database can map hundreds of installed fonts.
    # FreeType keeps normal multilingual fallback without that idle mapping cost.
    # Respect an explicit platform override (including offscreen QA).
    os.environ.setdefault("QT_QPA_PLATFORM", "windows:fontengine=freetype")
    app = QApplication(sys.argv[:1])
    app.setApplicationName("StillWallpaper")
    app.setApplicationVersion(VERSION)
    app.setFont(QFont("Microsoft YaHei UI", 9))
    app.setQuitOnLastWindowClosed(False)
    store = Store(args.data_dir)
    handler = RotatingFileHandler(store.directory / "still.log", maxBytes=512000, backupCount=2, encoding="utf-8")
    logging.basicConfig(level=logging.INFO, handlers=[handler], format="%(asctime)s %(levelname)s %(message)s")
    def exception_hook(exc_type, value, tb):
        logging.error("Unhandled exception", exc_info=(exc_type, value, tb))
        # Keep playback exceptions visible, without crashing out of a Qt callback.
        if hasattr(app, "controller"):
            app.controller.report_error(tr("操作未完成：{0}。详细信息已记录在 still.log。").format(value))
    sys.excepthook = exception_hook
    # One instance per user/config directory. A second launch opens settings.
    import hashlib
    server_name = "StillWallpaper-"+hashlib.sha256(str(store.directory.resolve()).casefold().encode()).hexdigest()[:16]
    socket = QLocalSocket()
    socket.connectToServer(server_name)
    if socket.waitForConnected(400):
        socket.write(b"quit" if args.quit else b"show")
        socket.waitForBytesWritten(400)
        return 0
    if args.quit:
        return 0
    server = QLocalServer(app)
    QLocalServer.removeServer(server_name)
    if not server.listen(server_name):
        QMessageBox.critical(None, tr("轻块桌面"), tr("无法创建本地实例通道。"))
        return 1
    if args.screenshot:
        store.settings.restore_on_start = False
    controller = Controller(app, store, background=args.background, check_updates=not args.screenshot)
    app.controller = controller
    def new_connection():
        while server.hasPendingConnections():
            connection = server.nextPendingConnection()
            def receive(c=connection):
                payload = bytes(c.readAll())
                if payload == b"quit":
                    controller.request_quit()
                elif payload:
                    controller.show_window()
                c.disconnectFromServer()
                c.deleteLater()
            if connection.bytesAvailable():
                receive()
            else:
                connection.readyRead.connect(receive)
    server.newConnection.connect(new_connection)
    if args.screenshot:
        def capture():
            args.screenshot.parent.mkdir(parents=True, exist_ok=True)
            controller.window.grab().save(str(args.screenshot))
            app.quit()
        QTimer.singleShot(1000, capture)
    return app.exec()
