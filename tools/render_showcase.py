"""Render the real app widgets against fixtures. Never reads the host desktop."""
from pathlib import Path
from types import SimpleNamespace
import os
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
os.environ["QT_QPA_PLATFORM"] = "windows:fontengine=freetype"
from PySide6.QtCore import QDateTime, QFileInfo, QPointF, QRect, QRectF, QSize, Qt
from PySide6.QtGui import QColor, QFont, QPainter, QPixmap
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QFileIconProvider

from still.app import Controller
from still.capture_overlay import CaptureOverlay
from still.i18n import install_qt_translation, set_language
from still.model import ASSETS, Store
from still.notes import NotePin, NotesWindow
from still.organizer import DesktopShelf
from still.organizer_model import Collections
from still.recording_ui import RecordingResult

MEDIA = ROOT / "site" / "public" / "media"
MEDIA.mkdir(parents=True, exist_ok=True)
lang = sys.argv[1]
theme = sys.argv[2] if len(sys.argv)>2 else "light"
assert lang in {"zh", "en"} and theme in {"light", "dark"}
zh = lang == "zh"
set_language(lang)
app = QApplication([])
app.setFont(QFont("Microsoft YaHei UI", 9))
app.setQuitOnLastWindowClosed(False)

def save(pixmap, name):
    assert pixmap.save(str(MEDIA / f"{name}-{lang}-{theme}.webp"), "WEBP", 92)

def capture_settings(root):
    store = Store(root / "settings")
    store.settings.settings_language = lang
    store.settings.settings_theme = theme
    for key in ("capture_hotkey", "hotkeys_enabled", "organizer_enabled", "taskbar_style_enabled", "restore_on_start", "notes_background", "clipboard_enabled", "focus_active"):
        setattr(store.settings, key, False)
    store.settings.capture_hotkey = False
    store.settings.restore_on_start = False
    with patch("still.app.Controller.show_window"):
        controller = Controller(app, store, check_updates=False)
    from still.ui import MainWindow
    controller.window = MainWindow(controller)
    controller.window.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen)
    controller.window.show()
    app.processEvents()
    window = controller.window
    window.resize(1280, 860)
    app.processEvents()
    assert window.nav_buttons[0].text() == ("壁纸" if zh else "Wallpaper")
    assert controller.capture_action.text() == ("截屏" if zh else "Screen Capture")
    video_card = next(card for card in window.cards if card.item["kind"] == "video")
    for _ in range(100):
        if not video_card.pixmap.isNull():
            break
        QTest.qWait(35)
    assert not video_card.pixmap.isNull()
    window.select(store.find("builtin:nightdrive"))
    app.processEvents()
    save(window.grab(), "wallpaper")
    library = controller.online_wallpapers()
    library.activated = True
    from urllib.parse import urlsplit
    import shutil
    research = ROOT / ".local/wallpaper-recuration"
    for entry in library.entries:
        ident = entry["id"].removeprefix("commons-")
        if entry["kind"] == "gif": cover = research / "gif-covers" / (ident + ".gif")
        elif entry["id"].startswith("commons-"): cover = research / "covers" / (("image-" if entry["kind"] == "image" else "small-video-") + ident + ".jpg")
        else: cover = research / "covers" / ("video-" + urlsplit(entry["source_url"]).path.split("/")[-1] + ".jpg")
        if cover.exists():
            target = library.thumbnail_path(entry)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(cover, target)
    window.set_library_mode("online")
    app.processEvents()
    save(window.grab(), "online-wallpaper")
    window.set_library_mode("local")
    window.close()
    controller.cleanup()

def background():
    image = QPixmap(1200, 736)
    image.fill(QColor("#1f3343"))
    art = QPixmap(str(ASSETS / ("mountain-bay.jpg" if theme == "light" else "milky-way.jpg"))).scaled(
        image.size(), Qt.AspectRatioMode.KeepAspectRatioByExpanding, Qt.TransformationMode.SmoothTransformation)
    painter = QPainter(image)
    painter.drawPixmap(0, 0, art)
    painter.fillRect(image.rect(), QColor(17, 30, 47, 55))
    painter.end()
    return image

def capture_organizer(root):
    store = Store(root / "organizer")
    store.settings.organizer_alignment = "center"
    store.settings.organizer_views = {"center": "groups"}
    collections = Collections(store)
    demo_files = root / "fixture-files"
    demo_files.mkdir()
    categories = [
        ("folders", "文件夹", "mint", [("素材", "Assets"), ("草稿", "Drafts")]),
        ("documents", "文档与表格", "amber", [("项目计划.txt", "Project plan.txt"), ("版本记录.md", "Release notes.md")]),
        ("images", "图片与设计", "rose", [("山海.jpg", "Mountain bay.jpg")]),
        ("system", "系统图标", "blue", [("此电脑", "This PC")]),
        ("projects", "项目", "blue", [("演示站点", "Demo website"), ("发布素材", "Release assets")]),
        ("captures", "截屏素材", "mint", [("布局.png", "Layout.png"), ("选区.png", "Selection.png")]),
        ("writing", "文稿", "amber", [("文案.txt", "Copy.txt"), ("简报.txt", "Brief.txt")]),
        ("archive", "归档", "rose", [("历次演示", "Past demos")]),
    ]
    for key, title, color, entries in categories:
        group = collections.create(title, color, save=False)
        group["auto_category"] = key
        for index, pair in enumerate(entries):
            name = pair[0 if zh else 1]
            path = demo_files / name
            if key in {"folders", "projects", "archive"}:
                path.mkdir()
            else:
                path.touch()
            group["entries"].append({"id": f"{key}-{index}", "name": name, "target": str(path), "managed": False})
    provider = QFileIconProvider()
    def icon(entry):
        file = Path(entry["target"])
        if entry["id"].startswith("system-"):
            return provider.icon(QFileIconProvider.IconType.Computer)
        kind = QFileIconProvider.IconType.Folder if file.is_dir() else QFileIconProvider.IconType.File
        return provider.icon(kind)
    with patch("still.organizer.MouseHook.start"), patch.object(DesktopShelf, "attach_to_desktop"):
        panel = DesktopShelf(SimpleNamespace(store=store, collections=collections, icon_for=icon))
        panel.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen)
        panel.expanded = True
        panel.refresh()
        panel.show()
        app.processEvents()
    panel.body.layout().activate()
    shelf = panel.grab(panel.panel_rect)
    pixmap = background()
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
    width = 810
    height = round(width * shelf.height() / shelf.width())
    painter.drawPixmap(QRect((1200 - width)//2, (736 - height)//2, width, height), shelf)
    painter.end()
    save(pixmap, "organizer")
    panel.close()

class DemoScreen:
    def geometry(self):
        return QRect(0, 0, 1200, 736)

    def name(self):
        return "demo"

    def grabWindow(self, _):
        pixmap = background()
        painter = QPainter(pixmap)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor("#142130"))
        painter.drawRoundedRect(QRectF(143, 117, 896, 476), 22, 22)
        painter.setPen(QColor("#9be1c9"))
        painter.setFont(QFont("Microsoft YaHei UI", 12, QFont.Weight.DemiBold))
        painter.drawText(192, 173, "演示项目  /  发布清单" if zh else "SAMPLE PROJECT  /  RELEASE BOARD")
        painter.setPen(QColor("#f0f6fc"))
        painter.setFont(QFont("Microsoft YaHei UI", 32, QFont.Weight.DemiBold))
        painter.drawText(192, 239, "准备下一个版本" if zh else "Prepare the next release")
        for index, (cn, en, status_cn, status_en) in enumerate([
            ("整理素材", "Organize assets", "已完成", "Done"),
            ("验证安装包", "Verify the installer", "进行中", "In progress"),
            ("更新说明文档", "Update the guide", "待处理", "To do"),
        ]):
            y = 284 + index * 77
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(QColor("#223547"))
            painter.drawRoundedRect(QRectF(190, y, 799, 62), 10, 10)
            painter.setFont(QFont("Microsoft YaHei UI", 15))
            painter.setPen(QColor("#e2edf5"))
            painter.drawText(221, y + 39, cn if zh else en)
            painter.setFont(QFont("Microsoft YaHei UI", 11))
            painter.setPen(QColor("#a9c9dc"))
            painter.drawText(821, y + 38, status_cn if zh else status_en)
        painter.end()
        return pixmap

def capture_overlay():
    with patch("still.capture_overlay.win32.target_monitor", return_value={"rect": (0, 0, 1200, 736)}), \
         patch("still.capture_overlay.native_targets", return_value=[]):
        overlay = CaptureOverlay(DemoScreen())
    overlay.selection = QRect(123, 98, 941, 504)
    overlay.confirm_selection()
    overlay.operations.extend([
        {"kind": "rect", "points": [(200, 369), (466, 417)], "color": "#a6e4ca", "width": 3},
        {"kind": "arrow", "points": [(769, 412), (811, 399)], "color": "#ffd382", "width": 4},
        {"kind": "number", "points": [(742, 417)], "color": "#ffd382", "number": 1, "width": 5},
    ])
    overlay.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen)
    overlay.show()
    app.processEvents()
    save(overlay.grab(), "capture")
    overlay.close()


def capture_recording(root):
    store = Store(root / "recording")
    store.settings.record_resolution = 720
    store.settings.record_fps = 24
    image = DemoScreen().grabWindow(0).copy(143, 90, 896, 504)
    still = root / "scene.png"
    assert image.save(str(still))
    video = root / "demo.mp4"
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-loop", "1", "-framerate", "24", "-i", str(still),
                    "-t", "6", "-c:v", "libx264", "-pix_fmt", "yuv420p", str(video)], check=True)
    workbench = SimpleNamespace(controller=SimpleNamespace(store=store))
    preview = RecordingResult(workbench, video, 6, QRect(152, 40, 896, 504), QSize(896, 504), 24)
    for widget in (preview, preview.toolbar):
        widget.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen)
    preview.show()
    for _ in range(75):
        if preview.canvas.frame.isValid() and preview.player.duration():
            break
        QTest.qWait(40)
    assert preview.canvas.frame.isValid()
    preview.player.pause()
    preview.format.setCurrentIndex(preview.format.findData("gif"))
    preview.timeline.active = "start"
    preview.timeline.apply_frame(24)
    preview.timeline.active = "end"
    preview.timeline.apply_frame(120)
    preview.update_timeline(2300)
    preview.canvas.publish({"kind": "arrow", "points": [(665, 295), (655, 262)], "color": "#ffc775", "width": 4})
    app.processEvents()
    player = preview.grab()
    toolbar = preview.toolbar.grab()
    pixmap = background()
    painter = QPainter(pixmap)
    player_size = player.deviceIndependentSize()
    toolbar_size = toolbar.deviceIndependentSize()
    top = round((pixmap.height() - player_size.height() - toolbar_size.height() - 12) / 2)
    painter.drawPixmap(round((pixmap.width() - player_size.width()) / 2), top, player)
    painter.drawPixmap(round((pixmap.width() - toolbar_size.width()) / 2), round(top + player_size.height() + 12), toolbar)
    painter.end()
    save(pixmap, "recording")
    preview.close()


def capture_notes(root):
    store = Store(root / "notes")
    store.settings.settings_language = lang
    store.settings.settings_theme = theme
    for key in ("capture_hotkey", "hotkeys_enabled", "organizer_enabled", "taskbar_style_enabled", "restore_on_start", "notes_background", "clipboard_enabled", "focus_active"):
        setattr(store.settings, key, False)
    store.settings.capture_hotkey = False
    store.settings.restore_on_start = False
    with patch("still.app.Controller.show_window"):
        controller = Controller(app, store, check_updates=False)
    workbench = controller.get_workbench()
    engine = workbench.notes()
    samples = [
        ("参考素材", "Reference materials", "整理演示素材。", "Review demo assets.", "blue", True),
        ("发布备忘", "Release checklist", "测试下载。", "Check downloads.", "amber", False),
        ("本周计划", "This week", "• 整理素材\n• 检查安装包\n• 更新指南",
         "• Demo assets\n• Test Setup\n• Update guide", "mint", False),
    ]
    for title_zh, title_en, body_zh, body_en, color, done in samples:
        ident = engine.create()
        engine.save(ident, title_zh if zh else title_en, body_zh if zh else body_en, color, done, None)
    workbench.db.execute("UPDATE notes SET pinned=1 WHERE id=?", (ident,))
    window = NotesWindow(workbench)
    window.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen)
    window.selected = ident
    window.refresh()
    window.due.blockSignals(True)
    window.due.setDateTime(QDateTime.fromString("2027-04-09 14:00", "yyyy-MM-dd HH:mm"))
    window.due.blockSignals(False)
    window.resize(780, 508)
    window.show()
    app.processEvents()
    note = NotePin(None, engine.get(ident))
    note.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen)
    pixmap = background()
    painter = QPainter(pixmap)
    painter.drawPixmap(55, 114, window.grab())
    painter.drawPixmap(867, 140, note.grab())
    painter.end()
    save(pixmap, "notes")
    window.close()
    note.close()
    controller.cleanup()


with tempfile.TemporaryDirectory(prefix="lbd-public-demo-") as tmp, \
     patch("still.i18n.display_language", return_value=lang), \
     patch("PySide6.QtWidgets.QSystemTrayIcon.show"), \
     patch("PySide6.QtGui.QScreen.grabWindow", side_effect=AssertionError("Marketing captures must never read the host desktop")), \
     patch("still.notes.desktop_layer"), \
     patch("still.notes.desktop_owner", return_value=0):
    install_qt_translation(app)
    capture_settings(Path(tmp))
    capture_organizer(Path(tmp))
    capture_overlay()
    capture_recording(Path(tmp))
    capture_notes(Path(tmp))
    app.processEvents()


print("Rendered isolated public screenshots", lang, theme)
