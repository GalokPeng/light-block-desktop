from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QPointF, QRectF, QSignalBlocker, QSize, Qt, QTimer, QUrl, Signal
from PySide6.QtGui import QColor, QDesktopServices, QFont, QIcon, QImageReader, QKeySequence, QLinearGradient, QMovie, QPainter, QPainterPath, QPen, QPixmap, QShortcut
from PySide6.QtWidgets import (QAbstractButton, QApplication, QButtonGroup, QCheckBox, QComboBox, QFileDialog,
    QFrame, QGridLayout, QHBoxLayout, QLabel, QLineEdit, QMainWindow, QMenu, QMessageBox, QPushButton,
    QScrollArea, QSizePolicy, QStackedWidget, QToolButton, QVBoxLayout, QWidget)

from .i18n import display_language, tr
from . import win32
from .model import APP_NAME, ASSETS, FITS, VERSION, item_name, item_path
from .updates import REPO_URL
from .video_thumbnails import VideoThumbnails

ACCENT = "#a6e4ca"
KIND_LABELS = {"image": "静态", "gif": "GIF", "video": "视频"}


def navigation_icon(index, light=False):
    """Small native vectors avoid expensive system-wide symbol font fallback."""
    pixmap = QPixmap(40, 40)
    pixmap.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.scale(2, 2)
    painter.setPen(QPen(QColor("#486e62" if light else "#a2b8b0"), 1.35, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin))
    if index == 0:
        painter.drawRoundedRect(QRectF(2, 3, 16, 14), 2, 2)
        painter.drawEllipse(QRectF(11, 6, 3, 3))
        painter.drawLine(3, 14, 7, 10)
        painter.drawLine(7, 10, 11, 14)
        painter.drawLine(11, 14, 14, 11)
        painter.drawLine(14, 11, 17, 14)
    elif index == 1:
        for x in (3, 11):
            for y in (3, 11):
                painter.drawRoundedRect(QRectF(x, y, 6, 6), 1.3, 1.3)
    elif index == 2:
        for x, y, direction_x, direction_y in ((3,3,1,1),(17,3,-1,1),(3,17,1,-1),(17,17,-1,-1)):
            painter.drawLine(x, y, x+direction_x*5, y)
            painter.drawLine(x, y, x, y+direction_y*5)
        painter.drawEllipse(QRectF(7, 7, 6, 6))
    elif index == 3:
        for x in (4, 10, 16):
            for y in (4, 10, 16):
                painter.drawEllipse(QRectF(x-1, y-1, 2, 2))
    elif index == 4:
        for y, x in ((5,7),(10,13),(15,8)):
            painter.drawLine(3, y, 17, y)
            painter.setBrush(QColor("#edf3f0" if light else "#151b24"))
            painter.drawEllipse(QRectF(x-2, y-2, 4, 4))
    else:
        painter.drawEllipse(QRectF(2.5, 2.5, 15, 15))
        painter.drawLine(10, 9, 10, 14)
        painter.drawPoint(10, 6)
    painter.end()
    pixmap.setDevicePixelRatio(2)
    return QIcon(pixmap)

def theme_icon(light):
    pixmap = QPixmap(40, 40)
    pixmap.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.scale(2, 2)
    color = QColor("#4f6a52" if light else "#c5d9e8")
    if light:
        painter.setPen(QPen(color, 1.35, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        center = QPointF(10, 10)
        painter.drawEllipse(center, 3.7, 3.7)
        for x, y in ((1, 0), (0, 1), (1, 1), (1, -1), (-1, 0), (0, -1), (-1, 1), (-1, -1)):
            scale = 0.707 if x and y else 1
            direction = QPointF(x * scale, y * scale)
            painter.drawLine(center + direction * 6.5, center + direction * 8.3)
    else:
        moon, cut = QPainterPath(), QPainterPath()
        moon.addEllipse(QRectF(3, 3, 14, 14))
        cut.addEllipse(QRectF(8.4, 1, 11, 11))
        painter.fillPath(moon.subtracted(cut), color)
    painter.end()
    pixmap.setDevicePixelRatio(2)
    return QIcon(pixmap)


STYLE = """
QWidget { color: #e9edf0; font-family: 'Microsoft YaHei UI', 'Segoe UI'; font-size: 12px; }
QMainWindow, QDialog, QWidget#root { background: #11151c; }
QFrame#sidebar { background: #151b24; border-right: 1px solid #272e38; }
QFrame#panel, QFrame#surface { background: #1a212c; border: 1px solid #2a3340; border-radius: 14px; }
QLabel { background: transparent; border: none; }
QLabel#muted { color: #8e9bad; }
QLabel#eyebrow { color: #86b8a5; font-size: 10px; letter-spacing: 2px; }
QLabel#title { font-size: 26px; font-weight: 600; }
QLabel#section { font-size: 15px; font-weight: 600; }
QLabel#notice { background: #1e3530; color: #b8ecd8; border: 1px solid #34594b; border-radius: 8px; padding: 9px 12px; }
QPushButton { background: #242d39; border: 1px solid #354151; border-radius: 8px; padding: 9px 13px; font-weight: 500; }
QPushButton:hover { background: #303b4a; border-color: #536276; }
QPushButton:pressed { background: #1b2430; }
QPushButton:disabled { background: #202732; color: #697688; border-color: #303947; }
QPushButton#primary { background: #a6e4ca; color: #132b24; border: 1px solid #a6e4ca; font-weight: 700; }
QPushButton#primary:hover { background: #bcf1db; }
QPushButton#primary:pressed { background: #88c7ad; }
QPushButton#primary:disabled { background: #344a43; color: #73877e; border-color: #344a43; }
QPushButton#nav { text-align: left; border: none; padding: 12px 16px; background: transparent; color: #9caabd; }
QPushButton#nav:hover { background: #1e2835; color: #e9edf0; }
QPushButton#nav:checked { background: #273b37; color: #b8eed8; }
QPushButton#pill { padding: 6px 12px; background: transparent; border: none; color: #8e9bad; border-radius: 6px; }
QPushButton#pill:checked { background: #2b3a37; color: #b6ead6; }
QPushButton#text { background: transparent; border: none; color: #a4b5c8; padding: 5px 0; text-align: left; }
QPushButton#text:hover { color: #c7f0df; }
QToolButton#themeToggle { background:#242d39; border:1px solid #354151; border-radius:8px; }
QToolButton#themeToggle:hover, QToolButton#themeToggle:focus { border-color:#87b7a2; }
QComboBox { background: #121a24; border: 1px solid #354151; border-radius: 7px; min-height: 20px; padding: 7px 10px; }
QComboBox:hover, QComboBox:focus { border-color: #7fae9b; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: #202a36; selection-background-color: #365247; padding: 5px; outline: none; }
QLineEdit { background: #171f2a; border: 1px solid #303b49; border-radius: 7px; padding: 7px 10px; selection-background-color: #426855; }
QLineEdit:focus { border-color: #87b7a2; }
QSpinBox, QDoubleSpinBox, QDateTimeEdit { background:#121a24; color:#e9edf0; border:1px solid #354151; border-radius:7px; padding:7px 9px; min-height:20px; }
QProgressBar { background:#182432; color:#e5eee8; border:1px solid #344253; border-radius:7px; min-height:18px; text-align:center; }
QProgressBar::chunk { background:#416f5c; border-radius:6px; }
QTableWidget, QTreeWidget { background:#172330; color:#e7eef6; gridline-color:#344253; border:1px solid #344253; border-radius:8px; }
QHeaderView::section { background:#273544; color:#d0dce8; border:0; padding:8px; }
QMenu { background:#1b2836; color:#e8eff6; border:1px solid #38495b; padding:6px; }
QMenu::item { padding:8px 20px; border-radius:5px; } QMenu::item:selected { background:#35564a; }
QCalendarWidget QWidget { background:#202d3d; color:#e7eef6; }
QCheckBox { spacing: 10px; }
QCheckBox::indicator { width: 17px; height: 17px; background: #121a24; border: 1px solid #536275; border-radius: 5px; }
QCheckBox::indicator:checked { background: #a6e4ca; border: 4px solid #4e8a73; }
QCheckBox::indicator:hover { border-color: #a6e4ca; }
QScrollArea { border: none; background: transparent; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 6px; margin: 2px; }
QScrollBar::handle:vertical { background: #394657; border-radius: 3px; min-height: 32px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
QToolTip { background: #253442; color: #eaf2ef; border: 1px solid #567465; padding: 6px; }
QListWidget#collectionList { background:transparent; border:0; outline:0; }
QListWidget#collectionList::item { background:#202c3a; border:1px solid #334355; border-radius:10px; padding:16px; }
QListWidget#collectionList::item:selected { background:#2c423e; border-color:#8ac1aa; }
"""

LIGHT_STYLE = STYLE + """
QWidget { color:#26343b; }
QMainWindow, QDialog, QWidget#root { background:#f3f6f4; }
QFrame#sidebar { background:#edf3f0; border-right-color:#d5e0db; }
QFrame#panel, QFrame#surface { background:#ffffff; border-color:#d8e2dd; }
QLabel#muted, QPushButton#pill { color:#53666c; }
QLabel#eyebrow { color:#376c5b; }
QLabel#notice { background:#e2f1e9; color:#254b3e; border-color:#b5d5c7; }
QPushButton { background:#f6f8f7; border-color:#cbd9d2; }
QPushButton:hover { background:#e9f0ec; border-color:#93afa3; }
QPushButton:pressed { background:#dce9e3; }
QPushButton:disabled { background:#edf2ef; color:#72827e; border-color:#dce6e1; }
QPushButton#primary:disabled { background:#dce8e1; color:#64766c; border-color:#dce8e1; }
QPushButton#nav { color:#4a6266; }
QPushButton#nav:hover { background:#e3ede7; color:#26343b; }
QPushButton#nav:checked, QPushButton#pill:checked { background:#d9ece2; color:#214e3f; }
QPushButton#text { color:#496960; }
QPushButton#text:hover { color:#1d5643; }
QToolButton#themeToggle { background:#f6f8f7; border-color:#cbd9d2; }
QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox, QDateTimeEdit {
    background:#ffffff; color:#26343b; border-color:#c5d5cd;
    selection-background-color:#d3e9de; selection-color:#203f35;
}
QComboBox QAbstractItemView { background:#ffffff; selection-background-color:#d3e9de; selection-color:#203f35; }
QProgressBar, QTableWidget, QTreeWidget { background:#f5f8f6; color:#26343b; border-color:#cfddd6; gridline-color:#dce7e1; }
QHeaderView::section { background:#e8f0ec; color:#3d5550; }
QMenu, QToolTip { background:#ffffff; color:#26343b; border-color:#cadbd2; }
QMenu::item:selected { background:#d9ece2; }
QCalendarWidget QWidget { background:#f5f8f6; color:#26343b; }
QCheckBox::indicator { background:#ffffff; border-color:#98afa4; }
QCheckBox::indicator:checked { background:#d9ece2; border-color:#4e8a73; }
QScrollBar::handle:vertical { background:#b1c5bb; }
QListWidget#collectionList::item { background:#f2f6f4; border-color:#d9e5df; }
QListWidget#collectionList::item:selected { background:#d9ece2; border-color:#77a994; }
"""


def label(text: str, name: str = "", wrap=False) -> QLabel:
    widget = QLabel(text)
    if name:
        widget.setObjectName(name)
    widget.setWordWrap(wrap)
    return widget


def button(text: str, callback, name="") -> QPushButton:
    widget = QPushButton(text)
    widget.setCursor(Qt.CursorShape.PointingHandCursor)
    if name:
        widget.setObjectName(name)
    widget.clicked.connect(callback)
    return widget


def preview_image(path, width=960) -> QPixmap:
    reader = QImageReader(str(path))
    reader.setAutoTransform(True)
    size = reader.size()
    if size.isValid() and size.width() > width:
        size.scale(width, width, Qt.AspectRatioMode.KeepAspectRatio)
        reader.setScaledSize(size)
    return QPixmap.fromImage(reader.read())


class Logo(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(36, 36)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor(ACCENT))
        p.drawRoundedRect(QRectF(0, 0, 36, 36), 11, 11)
        p.setPen(QPen(QColor("#1d3b31"), 2.2, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        p.drawArc(QRectF(8, 7, 20, 20), 20*16, 140*16)
        p.drawLine(8, 24, 15, 17)
        p.drawLine(15, 17, 22, 25)
        p.drawLine(22, 25, 29, 20)


class Toggle(QAbstractButton):
    def __init__(self, checked=False):
        super().__init__()
        self.setCheckable(True)
        self.setChecked(checked)
        self.setFixedSize(38, 23)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.toggled.connect(self.update)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(QPen(QColor("#ceeedd"), 1) if self.hasFocus() else Qt.PenStyle.NoPen)
        p.setBrush(QColor(ACCENT if self.isChecked() else "#3b4756"))
        p.drawRoundedRect(QRectF(1, 2, 36, 20), 10, 10)
        p.setBrush(QColor("#18392b" if self.isChecked() else "#b3beca"))
        p.setPen(Qt.PenStyle.NoPen)
        p.drawEllipse(QRectF(20 if self.isChecked() else 4, 5, 14, 14))


class Preview(QWidget):
    def __init__(self):
        super().__init__()
        self.item = None
        self.pixmap = QPixmap()
        self.movie = None
        self.setMinimumHeight(258)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

    def set_item(self, item, path=None, cover=None):
        if self.movie is not None:
            self.movie.stop()
            self.movie.setFileName("")
            self.movie.deleteLater()
            self.movie = None
        self.item = item
        path = path or item_path(item)
        self.pixmap = cover if cover is not None else preview_image(path)
        if item["kind"] == "gif":
            self.movie = QMovie(str(path), parent=self)
            self.movie.setCacheMode(QMovie.CacheMode.CacheNone)
            source_size = QImageReader(str(path)).size()
            if source_size.isValid() and max(source_size.width(), source_size.height()) > 960:
                source_size.scale(960, 960, Qt.AspectRatioMode.KeepAspectRatio)
                self.movie.setScaledSize(source_size)
            self.movie.frameChanged.connect(self._preview_frame)
            self.movie.finished.connect(self.movie.start)
            if self.movie.isValid() and self.isVisible():
                self.movie.start()
        self.update()

    def _preview_frame(self, _):
        if self.movie is not None:
            self.pixmap = self.movie.currentPixmap()
            self.update()

    def showEvent(self, event):
        if self.movie is not None and self.movie.isValid():
            if self.movie.state() == QMovie.MovieState.NotRunning:
                self.movie.start()
            else:
                self.movie.setPaused(False)
        super().showEvent(event)

    def hideEvent(self, event):
        if self.movie is not None:
            self.movie.setPaused(True)
        super().hideEvent(event)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        area = QRectF(self.rect()).adjusted(1, 1, -1, -1)
        path = QPainterPath()
        path.addRoundedRect(area, 13, 13)
        p.setClipPath(path)
        p.fillRect(self.rect(), QColor("#1b2633"))
        if not self.pixmap.isNull():
            scaled = self.pixmap.scaled(self.size(), Qt.AspectRatioMode.KeepAspectRatioByExpanding, Qt.TransformationMode.SmoothTransformation)
            p.drawPixmap((self.width()-scaled.width())//2, (self.height()-scaled.height())//2, scaled)
        gradient = QLinearGradient(0, self.height()*0.4, 0, self.height())
        gradient.setColorAt(0, QColor(0, 0, 0, 0))
        gradient.setColorAt(1, QColor(5, 12, 22, 210))
        p.fillRect(self.rect(), gradient)
        if not self.item:
            return
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor(15, 24, 33, 160))
        p.drawRoundedRect(QRectF(18, 18, 152 if display_language() == "en" else 116, 27), 13, 13)
        p.setPen(QColor("#f0f7f4"))
        p.setFont(QFont("Microsoft YaHei UI", 9))
        p.drawText(QRectF(18, 18, 152 if display_language() == "en" else 116, 27), Qt.AlignmentFlag.AlignCenter, tr(KIND_LABELS[self.item["kind"]])+tr("  /  原文件"))
        p.setFont(QFont("Microsoft YaHei UI", 20, QFont.Weight.Medium))
        p.drawText(QRectF(24, self.height()-83, self.width()-48, 38), Qt.AlignmentFlag.AlignVCenter, p.fontMetrics().elidedText(item_name(self.item), Qt.TextElideMode.ElideRight, self.width() - 48))
        p.setFont(QFont("Microsoft YaHei UI", 9))
        p.setPen(QColor("#c7d4dd"))
        subtitle = tr("让每一次回到桌面，都有一点不同。")
        if self.item["kind"] == "gif":
            subtitle = tr("GIF 动态预览 · 应用后在桌面循环播放")
        elif self.item["kind"] == "video":
            subtitle = tr("封面预览 · 应用后在桌面播放")
        p.drawText(QRectF(24, self.height()-43, self.width()-48, 23), Qt.AlignmentFlag.AlignVCenter, subtitle)


class WallpaperCard(QAbstractButton):
    def __init__(self, item, path, pixmap, light=False):
        super().__init__()
        self.item, self.pixmap = item, pixmap
        self.light = light
        self.setMinimumSize(145, 134)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        self.setFixedHeight(139)
        self.setCheckable(True)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setToolTip(str(path))
        self.setAccessibleName(item_name(item)+", "+tr(KIND_LABELS[item["kind"]]))

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        r = QRectF(self.rect()).adjusted(1, 1, -1, -1)
        hovered = self.underMouse()
        if self.light:
            background = "#edf5f0" if hovered else "#ffffff"
            border = "#508f77" if self.isChecked() else "#a8bfb3" if hovered else "#d8e3de"
        else:
            background = "#1d2732" if hovered else "#19212c"
            border = ACCENT if self.isChecked() else "#33404d" if hovered else "#28323e"
        p.setBrush(QColor(background))
        p.setPen(QPen(QColor(border), 1.5 if self.isChecked() else 1))
        p.drawRoundedRect(r, 9, 9)
        p.save()
        clip = QPainterPath()
        clip.addRoundedRect(QRectF(5, 5, self.width()-10, 87), 6, 6)
        p.setClipPath(clip)
        if not self.pixmap.isNull():
            scaled = self.pixmap.scaled(self.width()-10, 87, Qt.AspectRatioMode.KeepAspectRatioByExpanding, Qt.TransformationMode.SmoothTransformation)
            p.drawPixmap(5+(self.width()-10-scaled.width())//2, 5+(87-scaled.height())//2, scaled)
        p.restore()
        p.setFont(QFont("Microsoft YaHei UI", 9))
        p.setPen(QColor("#26343b" if self.light else "#e9edf0"))
        p.drawText(QRectF(12, 97, self.width()-58, 22), Qt.AlignmentFlag.AlignVCenter, self.fontMetrics().elidedText(item_name(self.item), Qt.TextElideMode.ElideRight, self.width()-65))
        p.setPen(QColor("#53666c" if self.light else "#9baabb"))
        p.setFont(QFont("Microsoft YaHei UI", 8))
        p.drawText(QRectF(self.width()-46, 97, 35, 22), Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter, tr(KIND_LABELS[self.item["kind"]]))
        if self.hasFocus():
            p.setPen(QPen(QColor("#508f77" if self.light else "#d0f3e3"), 1, Qt.PenStyle.DotLine))
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.drawRoundedRect(r.adjusted(3, 3, -3, -3), 6, 6)


class MainWindow(QMainWindow):
    closed = Signal()

    def __init__(self, controller):
        super().__init__()
        self.controller, self.store, self.engine = controller, controller.store, controller.engine
        self.thumbnails = VideoThumbnails(self.store.directory, self)
        self.thumbnails.ready.connect(self.cover_ready)
        self.updater = controller.updater()
        self.selected = self.store.find(self.store.settings.active_id) or self.store.items[0]
        self.filter_kind = "all"
        self.cards = []
        self.library_mode, self.online_page = "local", None
        self.library_dirty = False
        self._pending_remove = None
        self.light = self.store.settings.settings_theme == "light"
        self.setWindowTitle(tr(APP_NAME))
        self.setWindowIcon(controller.icon)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setAcceptDrops(True)
        self.resize(1250, 850)
        self.setMinimumSize(1060, 740)
        self.setStyleSheet(LIGHT_STYLE if self.light else STYLE)
        root = QWidget()
        root.setObjectName("root")
        self.setCentralWidget(root)
        main = QHBoxLayout(root)
        main.setContentsMargins(0, 0, 0, 0)
        main.setSpacing(0)
        main.addWidget(self._sidebar())
        content = QWidget()
        outer = QVBoxLayout(content)
        outer.setContentsMargins(28, 24, 28, 16)
        outer.setSpacing(18)
        top = QHBoxLayout()
        titles = QVBoxLayout()
        titles.setSpacing(6)
        titles.addWidget(label(tr("给桌面留一点安静"), "eyebrow"))
        titles.addWidget(label(tr("让桌面，安静地流动。"), "title"))
        top.addLayout(titles)
        top.addStretch()
        self.theme_toggle = QToolButton()
        self.theme_toggle.setObjectName("themeToggle")
        self.theme_toggle.setFixedSize(36, 36)
        self.theme_toggle.setIconSize(QSize(20, 20))
        self.theme_toggle.setCursor(Qt.CursorShape.PointingHandCursor)
        self.theme_toggle.clicked.connect(self.change_theme)
        self.update_theme_toggle()
        top.addWidget(self.theme_toggle)
        self.language_button = self.language_control()
        top.addWidget(self.language_button)
        add = button(tr("＋  导入壁纸"), self.primary_action, "primary")
        self.header_action = add
        add.setToolTip(tr("导入图片、GIF 或视频  Ctrl+O"))
        add.setMinimumWidth(124)
        top.addWidget(add)
        outer.addLayout(top)
        self.pages = QStackedWidget()
        self.pages.addWidget(self._library())
        from .organizer_ui import OrganizerPage
        self.pages.addWidget(OrganizerPage(self))
        from .tools_ui import CapturePage, ProductivityPage
        self.pages.addWidget(CapturePage(self))
        self.pages.addWidget(ProductivityPage(self))
        self.pages.addWidget(self._performance())
        self.pages.addWidget(self._about())
        self.pages.currentChanged.connect(self.page_changed)
        outer.addWidget(self.pages, 1)
        self.notice = label("", "notice", True)
        self.notice.hide()
        outer.addWidget(self.notice)
        bottom = QHBoxLayout()
        self.status_label = label("", "muted")
        self.status_label.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        bottom.addWidget(self.status_label, 1)
        self.memory = label("", "muted")
        bottom.addWidget(self.memory)
        outer.addLayout(bottom)
        main.addWidget(content, 1)
        self.message_timer = QTimer(self)
        self.message_timer.setSingleShot(True)
        self.message_timer.timeout.connect(self.notice.hide)
        self.resource_timer = QTimer(self)
        self.resource_timer.setInterval(3000)
        self.resource_timer.timeout.connect(self.refresh_status)
        self.resource_timer.start()
        self.update_timer = QTimer(self)
        self.update_timer.setInterval(3600000)
        self.update_timer.timeout.connect(self.updater.check)
        self.updater.changed.connect(self.refresh_update)
        self.refresh_update()
        self.engine.changed.connect(self.refresh_status)
        self.engine.applied.connect(self.wallpaper_applied)
        self.engine.restored.connect(self.restore_finished)
        self.engine.error.connect(self.clear_pending_remove)
        self.refresh_cards()
        self.update_selection()
        self.refresh_status()
        QShortcut(QKeySequence.StandardKey.Open, self, self.import_dialog)

    def _sidebar(self):
        side = QFrame()
        side.setObjectName("sidebar")
        side.setFixedWidth(224 if display_language() == "en" else 184)
        layout = QVBoxLayout(side)
        layout.setContentsMargins(16, 28, 16, 20)
        layout.setSpacing(7)
        brand = QHBoxLayout()
        brand.setSpacing(10)
        brand.addWidget(Logo())
        text = QVBoxLayout()
        text.setSpacing(1)
        name = label(tr(APP_NAME), "section", True)
        text.addWidget(name)
        text.addWidget(label(tr("桌面工作台"), "muted"))
        brand.addLayout(text)
        brand.addStretch()
        layout.addLayout(brand)
        layout.addSpacing(33)
        layout.addWidget(label(tr("  工作空间"), "muted"))
        layout.addSpacing(5)
        group = QButtonGroup(self)
        self.nav_buttons = []
        for index, title in enumerate((tr("壁纸库"), tr("桌面收纳"), tr("截屏"), tr("效率工具"), tr("播放与性能"), tr("关于"))):
            b = button(title, lambda checked=False, n=index: self.pages.setCurrentIndex(n), "nav")
            b.setIcon(navigation_icon(index, self.light))
            b.setIconSize(QSize(18, 18))
            b.setCheckable(True)
            b.setChecked(index == 0)
            group.addButton(b)
            self.nav_buttons.append(b)
            layout.addWidget(b)
        layout.addStretch()
        self.update_button = button(tr("检查更新"), self.open_update)
        layout.addWidget(self.update_button)
        return side

    def _library(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(12)
        tabs = QHBoxLayout()
        group = QButtonGroup(page)
        self.library_tabs = {}
        for mode, text in (("local", tr("本地壁纸")), ("online", tr("在线壁纸"))):
            tab = button(text, lambda checked=False, m=mode: self.set_library_mode(m), "pill")
            tab.setCheckable(True)
            tab.setChecked(mode == "local")
            group.addButton(tab)
            self.library_tabs[mode] = tab
            tabs.addWidget(tab)
        tabs.addStretch()
        layout.addLayout(tabs)
        self.library_stack = QStackedWidget()
        self.library_stack.addWidget(self._local_library())
        self.library_stack.addWidget(QWidget())
        layout.addWidget(self.library_stack, 1)
        return page

    def set_library_mode(self, mode):
        if mode == "online" and self.online_page is None:
            from .online_ui import OnlinePage
            self.online_page = OnlinePage(self, self.controller.online_wallpapers())
            placeholder = self.library_stack.widget(1)
            self.library_stack.removeWidget(placeholder)
            placeholder.deleteLater()
            self.library_stack.addWidget(self.online_page)
        self.library_mode = mode
        if mode == "local" and self.library_dirty:
            self.refresh_cards()
        self.library_tabs[mode].setChecked(True)
        self.library_stack.setCurrentIndex(1 if mode == "online" else 0)

    def _local_library(self):
        page = QWidget()
        row = QHBoxLayout(page)
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(20)
        left = QVBoxLayout()
        left.setSpacing(16)
        self.preview = Preview()
        left.addWidget(self.preview, 4)
        toolbar = QHBoxLayout()
        self.card_count = label(tr("我的壁纸"), "section")
        toolbar.addWidget(self.card_count)
        toolbar.addStretch()
        group = QButtonGroup(self)
        self.filter_buttons = {}
        for kind, text in (("all", tr("全部")), ("image", tr("静态")), ("gif", "GIF"), ("video", tr("视频"))):
            b = button(text, lambda checked=False, k=kind: self.set_filter(k), "pill")
            b.setCheckable(True)
            b.setChecked(kind == "all")
            group.addButton(b)
            self.filter_buttons[kind] = b
            toolbar.addWidget(b)
        left.addLayout(toolbar)
        self.search = QLineEdit()
        self.search.setPlaceholderText(tr("搜索壁纸名称，或拖入本地文件…"))
        self.search.setClearButtonEnabled(True)
        self.search.textChanged.connect(self.refresh_cards)
        left.addWidget(self.search)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.grid_content = QWidget()
        self.grid = QGridLayout(self.grid_content)
        self.grid.setContentsMargins(1, 1, 7, 1)
        self.grid.setSpacing(10)
        self.grid.setAlignment(Qt.AlignmentFlag.AlignTop)
        scroll.setWidget(self.grid_content)
        scroll.setMinimumHeight(160)
        left.addWidget(scroll, 5)
        row.addLayout(left, 1)
        row.addWidget(self._inspector())
        return page

    def _inspector(self):
        panel = QFrame()
        panel.setObjectName("panel")
        panel.setFixedWidth(248)
        v = QVBoxLayout(panel)
        v.setContentsMargins(19, 21, 19, 19)
        v.setSpacing(11)
        v.addWidget(label(tr("壁纸设置"), "section"))
        self.meta = label("", "muted", True)
        v.addWidget(self.meta)
        v.addSpacing(8)
        v.addWidget(label(tr("显示方式"), "muted"))
        self.fit = QComboBox()
        self.fit.setAccessibleName(tr("壁纸显示方式"))
        for key, text in FITS.items():
            self.fit.addItem(tr(text), key)
        self.fit.setCurrentIndex(list(FITS).index(self.store.settings.fit))
        self.fit.currentIndexChanged.connect(self.fit_changed)
        v.addWidget(self.fit)
        v.addWidget(label(tr("动态壁纸的显示器"), "muted"))
        self.screen = QComboBox()
        self.screen.setAccessibleName(tr("动态壁纸显示器"))
        self.screen.addItem(tr("主显示器"), "")
        for index, s in enumerate(win32.monitors(), 1):
            self.screen.addItem(tr("显示器 {0}  ·  {1} × {2}").format(index, s['rect'][2], s['rect'][3]), s["name"])
        self.screen.setCurrentIndex(max(0, self.screen.findData(self.store.settings.screen)))
        self.screen.currentIndexChanged.connect(self.screen_changed)
        v.addWidget(self.screen)
        v.addWidget(label(tr("静态壁纸跟随 Windows，\n动态壁纸在所选单屏播放。"), "muted", True))
        v.addSpacing(12)
        startup_row = QHBoxLayout()
        startup_row.addWidget(label(tr("开机自启动")))
        startup_row.addStretch()
        self.startup = Toggle(win32.startup_enabled())
        self.startup.setAccessibleName(tr("开机自启动"))
        self.startup.setToolTip(tr("登录 Windows 后静默启动并恢复上次的壁纸"))
        self.startup.toggled.connect(self.startup_changed)
        startup_row.addWidget(self.startup)
        v.addLayout(startup_row)
        v.addWidget(label(tr("登录后恢复上次的壁纸"), "muted"))
        v.addSpacing(12)
        quality = QFrame()
        quality.setObjectName("surface")
        box = QVBoxLayout(quality)
        box.setContentsMargins(12, 12, 12, 12)
        box.addWidget(label(tr("原文件 · 不重新压缩")))
        box.addWidget(label(tr("预览缩略图仅用于浏览。\n壁纸播放始终读取原文件。"), "muted", True))
        v.addWidget(quality)
        v.addStretch()
        self.remove_button = button(tr("删除壁纸"), self.remove_selected, "text")
        self.remove_button.setToolTip(tr("删除壁纸库中的副本，保留导入时的原文件。"))
        v.addWidget(self.remove_button)
        self.apply_button = button(tr("应用到桌面  →"), self.apply_selected, "primary")
        self.apply_button.setMinimumHeight(42)
        v.addWidget(self.apply_button)
        self.pause_button = button(tr("暂停动态壁纸"), self.engine.pause)
        v.addWidget(self.pause_button)
        self.restore_button = button(tr("恢复 Windows 壁纸"), self.restore, "text")
        v.addWidget(self.restore_button)
        return panel

    def _performance(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(18)
        card = QFrame()
        card.setObjectName("panel")
        v = QVBoxLayout(card)
        v.setContentsMargins(26, 24, 26, 24)
        v.setSpacing(20)
        v.addWidget(label(tr("把资源留给正在做的事"), "section"))
        v.addWidget(label(tr("暂停时保留当前画面，条件解除后自动继续。策略每 2 秒检查一次。"), "muted", True))
        for key, title, description in (
            ("fullscreen_pause", tr("全屏时暂停"), tr("游戏、演示或全屏视频占据壁纸所在屏幕时，暂停动态壁纸。")),
            ("battery_pause", tr("电池供电时暂停"), tr("笔记本断开电源后暂停动态壁纸，接通电源后恢复。")),
            ("restore_on_start", tr("启动后恢复壁纸"), tr("恢复上次应用的壁纸；开机自启时不会弹出设置窗口。")),
        ):
            item = QVBoxLayout()
            item.setSpacing(6)
            check = QCheckBox(title)
            check.setChecked(bool(getattr(self.store.settings, key)))
            check.toggled.connect(lambda value, k=key: self.option_changed(k, value))
            item.addWidget(check)
            item.addWidget(label(description, "muted", True))
            v.addLayout(item)
        self.wallpaper_sound = QCheckBox(tr("轻块桌面声音"))
        self.refresh_wallpaper_sound()
        self.wallpaper_sound.toggled.connect(self.controller.set_wallpaper_sound)
        v.addWidget(self.wallpaper_sound)
        v.addWidget(label(tr("开启后，含音轨的视频壁纸会播放声音。"), "muted", True))
        v.addWidget(label(tr("始终启用：不抢焦点 · 锁屏与熄屏暂停"), "eyebrow", True))
        layout.addWidget(card)
        note = QFrame()
        note.setObjectName("panel")
        n = QVBoxLayout(note)
        n.setContentsMargins(26, 22, 26, 22)
        n.addWidget(label(tr("关于画质和内存"), "section"))
        n.addSpacing(8)
        n.addWidget(label(tr("GIF 与视频均由 Qt / FFmpeg 直接读取原文件，并使用相同的原生画面输出；硬件加速由后端根据设备选择。静态图交给 Windows，设置窗口关闭后释放预览控件。\n\n不修改你的原文件、不转码视频、不降低源分辨率。“填充屏幕”可能裁剪边缘；“完整显示”保留整张画面。内存仍受分辨率、编码、驱动和 Python / Qt 基础开销影响。此版以普通 SDR 内容为目标，HDR 与色彩管理未验证。"), "muted", True))
        layout.addWidget(note)
        layout.addStretch()
        return page

    def _about(self):
        page = QWidget()
        v = QVBoxLayout(page)
        v.setContentsMargins(0, 0, 0, 0)
        card = QFrame()
        card.setObjectName("panel")
        c = QVBoxLayout(card)
        c.setContentsMargins(30, 30, 30, 30)
        c.setSpacing(18)
        c.addWidget(Logo())
        c.addWidget(label(f"{tr(APP_NAME)}  /  v{VERSION}", "title"))
        c.addWidget(label(tr("给桌面一点风景，给工作一点安静。"), "section"))
        c.addWidget(label(tr("纯属个人娱乐，有BUG随便提issues，自己用得不顺就改改"), "muted", True))
        links = QHBoxLayout()
        self.github_button = button(tr("GitHub 项目  ↗"), lambda: QDesktopServices.openUrl(QUrl(REPO_URL)))
        self.github_button.setIconSize(QSize(19, 19))
        self.update_github_icon()
        links.addWidget(self.github_button)
        links.addWidget(button(tr("打开配置与日志文件夹  ↗"), self.open_data_folder))
        links.addStretch()
        c.addLayout(links)
        c.addWidget(label(tr("设置保存在本机。在线图库按需访问素材来源，无账户、无广告、无遥测。"), "eyebrow", True))
        v.addWidget(card)
        v.addStretch()
        return page

    def update_github_icon(self):
        mark = QIcon(str(ASSETS / "github-mark.svg")).pixmap(38, 38)
        if not self.light and not mark.isNull():
            with QPainter(mark) as painter:
                painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceIn)
                painter.fillRect(mark.rect(), QColor("#f4f7fc"))
        self.github_button.setIcon(QIcon(mark))

    def language_control(self):
        control = QToolButton()
        control.setObjectName("themeToggle")
        control.setText("EN" if display_language() == "en" else "中")
        control.setFixedSize(47, 36)
        control.setToolTip(tr("界面语言"))
        control.setAccessibleName(tr("界面语言"))
        control.setCursor(Qt.CursorShape.PointingHandCursor)
        menu = QMenu(control)
        for key, name in (("system", tr("跟随系统")), ("zh", "简体中文"), ("en", "English")):
            item = menu.addAction(name)
            item.setCheckable(True)
            item.setChecked(self.store.settings.settings_language == key)
            item.triggered.connect(lambda checked=False, choice=key: QTimer.singleShot(
                0, lambda: self.controller.change_language(choice)))
        control.setMenu(menu)
        control.setPopupMode(QToolButton.ToolButtonPopupMode.InstantPopup)
        return control

    def showEvent(self, event):
        super().showEvent(event)
        self.apply_frame_theme()
        if self.controller.check_updates:
            self.update_timer.start()
            self.updater.check()

    def apply_frame_theme(self):
        if QApplication.platformName() == "windows":
            win32.theme_frame(int(self.winId()), self.light)

    def refresh_update(self):
        state = self.updater.state
        messages = {"idle": tr("检查更新"), "checking": tr("正在检查更新…"), "update": tr("立即更新"),
                    "latest": tr("已是最新版本"), "error": tr("重试检查更新"),
                    "verifying": tr("正在校验安装包…"), "preparing": tr("正在准备安装…"),
                    "installing": tr("正在退出并更新…"), "download_error": tr("重试更新")}
        if state == "downloading":
            total, received = self.updater.total, self.updater.received
            text = tr("下载中 {0}%").format(min(100, received * 100 // total)) if total else \
                tr("已下载 {0} MB").format(f"{received / 1048576:.1f}")
        else:
            text = messages[state]
        self.update_button.setText(text)
        self.update_button.setEnabled(state in {"idle", "error", "update", "download_error", "downloading"})
        tooltip = tr("下载并校验安装包，随后退出并启动更新。") if state == "update" else \
            tr("无法检查更新，请稍后重试。") if state == "error" else self.updater.error if state == "download_error" else ""
        if state == "downloading":
            tooltip = tr("点击取消下载") + "\n" + tr("当前线路：{0}").format(self.updater.source)
        self.update_button.setToolTip(tooltip)

    def open_update(self):
        self.updater.activate()

    def update_theme_toggle(self):
        target = tr("深色") if self.light else tr("浅色")
        self.theme_toggle.setIcon(theme_icon(self.light))
        self.theme_toggle.setToolTip(tr("切换为{0}主题").format(target))
        self.theme_toggle.setAccessibleName(tr("切换为{0}主题").format(target))

    def change_theme(self):
        settings = self.store.settings
        previous = settings.settings_theme
        selected = "dark" if self.light else "light"
        settings.settings_theme = selected
        try:
            self.store.save()
        except OSError as exc:
            settings.settings_theme = previous
            self.show_error(str(exc))
            return
        self.light = selected == "light"
        self.setStyleSheet(LIGHT_STYLE if self.light else STYLE)
        self.apply_frame_theme()
        self.update_theme_toggle()
        self.update_github_icon()
        for index, button in enumerate(self.nav_buttons):
            button.setIcon(navigation_icon(index, self.light))
        for card in self.cards:
            card.light = self.light
            card.update()
        if self.online_page is not None:
            self.online_page.set_theme(self.light)

    def set_filter(self, kind):
        self.filter_kind = kind
        self.refresh_cards()

    def page_changed(self, index):
        if 0 <= index < len(self.nav_buttons):
            self.nav_buttons[index].setChecked(True)
        self.header_action.setVisible(index < 4)
        self.header_action.setText((tr("＋  导入壁纸"), tr("＋  新建分组"), tr("区域截图"), tr("快速启动"))[min(index, 3)])

    def primary_action(self):
        index = self.pages.currentIndex()
        if index == 0:
            self.import_dialog()
        elif index == 1:
            self.pages.widget(1).create()
        else:
            self.controller.get_workbench().invoke("capture" if index == 2 else "launcher")

    def refresh_cards(self):
        self.library_dirty = False
        while self.grid.count():
            child = self.grid.takeAt(0)
            if child.widget():
                child.widget().deleteLater()
        self.cards = []
        query = self.search.text().strip().casefold()
        items = [i for i in self.store.items if (self.filter_kind == "all" or i["kind"] == self.filter_kind)
                 and query in item_name(i).casefold()]
        self.card_count.setText(tr("我的壁纸  {0:02d}").format(len(self.store.items)))
        for index, item in enumerate(items):
            path = self.store.path(item)
            pixmap = self.thumbnails.get(path) if item["kind"] == "video" else preview_image(path, 420)
            card = WallpaperCard(item, path, pixmap, self.light)
            card.setChecked(item["id"] == self.selected["id"])
            card.clicked.connect(lambda checked=False, i=item: self.select(i))
            card.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
            card.customContextMenuRequested.connect(
                lambda point, i=item, c=card: self.wallpaper_menu(i, c.mapToGlobal(point)))
            self.grid.addWidget(card, index//3, index%3)
            self.cards.append(card)
        for col in range(3):
            self.grid.setColumnStretch(col, 1)
        if not items:
            self.grid.addWidget(label(tr("这里还没有壁纸。试试导入文件或调整筛选条件。"), "muted", True), 0, 0, 1, 3)

    def wallpaper_menu(self, item, position):
        if self.store.find(item["id"]) is None:
            return
        self.select(item)
        menu = QMenu(self)
        busy = getattr(self.engine, "busy", False) is True
        menu.addAction(tr("应用到桌面  →"), self.apply_selected).setEnabled(not busy and self.store.path(item).is_file())
        menu.addSeparator()
        remove = menu.addAction(tr("删除壁纸"), self.remove_selected)
        remove.setEnabled(not busy and not item.get("builtin"))
        remove.setToolTip(tr("内置壁纸不能删除。") if item.get("builtin") else
                          tr("删除壁纸库中的副本，保留导入时的原文件。"))
        try:
            menu.exec(position)
        finally:
            menu.deleteLater()

    def select(self, item):
        self.selected = item
        for card in self.cards:
            card.setChecked(card.item["id"] == item["id"])
        self.update_selection()

    def update_selection(self):
        path = self.store.path(self.selected)
        cover = self.thumbnails.get(path) if self.selected["kind"] == "video" else None
        self.preview.set_item(self.selected, path, cover)
        details = tr(KIND_LABELS[self.selected["kind"]]) + tr("壁纸")
        if self.selected["kind"] != "video":
            size = QImageReader(str(path)).size()
            if size.isValid():
                details += f"  ·  {size.width()} × {size.height()}"
        if path.exists():
            details += f"\n{path.stat().st_size / 1048576:.1f} MB  ·  {path.suffix[1:].upper()}"
        else:
            details += tr("\n原文件不存在，请重新导入")
        self.meta.setText(details)
        self.remove_button.setVisible(not self.selected.get("builtin"))
        # Dynamic media share QVideoWidget, which has no 1:1 mode.
        center_index = self.fit.findData("center")
        self.fit.model().item(center_index).setEnabled(self.selected["kind"] == "image")
        if self.selected["kind"] != "image" and self.fit.currentData() == "center":
            self.fit.setCurrentIndex(self.fit.findData("fit"))
        self._selected_file_exists = path.is_file()
        self.apply_button.setEnabled(self._selected_file_exists and not self.engine.busy)

    def cover_ready(self, path, pixmap):
        if self.selected["kind"] == "video" and str(self.store.path(self.selected)) == path:
            self.preview.pixmap = pixmap
            self.preview.update()
        for card in self.cards:
            if card.item["kind"] == "video" and str(self.store.path(card.item)) == path:
                card.pixmap = pixmap
                card.update()

    def fit_changed(self):
        self.store.settings.fit = self.fit.currentData()
        self.store.save()
        self.show_notice(tr("显示方式已保存，点击“应用到桌面”生效。"))

    def screen_changed(self):
        self.store.settings.screen = self.screen.currentData()
        self.store.save()
        self.show_notice(tr("显示器已保存，点击“应用到桌面”生效。"))

    def refresh_wallpaper_sound(self):
        with QSignalBlocker(self.wallpaper_sound):
            self.wallpaper_sound.setChecked(self.store.settings.wallpaper_sound)

    def option_changed(self, key, value):
        setattr(self.store.settings, key, value)
        self.store.save()
        self.engine.evaluate()

    def startup_changed(self, value):
        try:
            win32.set_startup(value)
            self.show_notice(tr("已启用：登录 Windows 后静默启动。") if value else tr("已关闭开机自启动。"))
        except OSError as exc:
            self.startup.blockSignals(True)
            self.startup.setChecked(not value)
            self.startup.blockSignals(False)
            self.show_error(str(exc))

    def apply_selected(self):
        try:
            self.engine.apply(self.selected)
            if self.engine.busy:
                self.show_notice(tr("正在切换壁纸…"))
        except (OSError, ValueError, RuntimeError) as exc:
            self.show_error(str(exc))

    def wallpaper_applied(self, item):
        self.show_notice(tr("已应用「{0}」 · 可关闭窗口，继续使用电脑。").format(item_name(item)))

    def clear_pending_remove(self, _error):
        self._pending_remove = None

    def restore_finished(self):
        pending, self._pending_remove = self._pending_remove, None
        if pending is None:
            self.show_notice(tr("已恢复 Windows 壁纸。"))
            return
        selected = self.selected
        if self.store.find(pending["id"]) is not None:
            self.selected = pending
            self.remove_selected()
            if selected["id"] != pending["id"] and self.store.find(selected["id"]) is not None:
                self.select(selected)

    def restore(self):
        try:
            self.engine.restore_original()
            if self.engine.busy:
                self.show_notice(tr("正在切换壁纸…"))
        except (OSError, ValueError, RuntimeError) as exc:
            self.show_error(str(exc))

    def remove_selected(self):
        if self.selected.get("builtin"):
            return
        previous = self.selected
        try:
            if self.engine.active and self.engine.active["id"] == previous["id"]:
                self.engine.restore_original()
                if getattr(self.engine, "busy", False) is True:
                    self._pending_remove = previous
                    return
            # Release GIF preview and any queued/in-flight video cover before
            # removing the managed file on Windows.
            self.thumbnails.cancel(self.store.path(previous))
            self.select(self.store.items[0])
            self.store.remove(previous["id"])
        except (OSError, ValueError, RuntimeError) as exc:
            self.select(previous)
            self.show_error(str(exc))
            return
        self.refresh_cards()
        self.show_notice(tr("已从壁纸库移除，原文件保留。"))

    def import_dialog(self):
        paths, _ = QFileDialog.getOpenFileNames(self, tr("导入壁纸 · 原文件不会被移动或修改"), "",
            tr("壁纸文件 (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff *.gif *.mp4 *.mkv *.mov *.webm *.avi *.m4v *.ogv *.wmv);;所有文件 (*)"))
        self.import_files(paths)

    def import_files(self, paths):
        imported, errors = [], []
        for path in paths:
            try:
                p = Path(path)
                from .model import media_kind
                kind = media_kind(p)
                if kind != "video" and not QImageReader(str(p)).canRead():
                    raise ValueError(tr("无法解码图片"))
                imported.append(self.store.add(p))
            except (OSError, ValueError) as exc:
                errors.append(f"{Path(path).name}: {exc}")
        if imported:
            self.selected = imported[-1]
            self.filter_kind = "all"
            self.filter_buttons["all"].setChecked(True)
            self.search.clear()
            self.refresh_cards()
            self.update_selection()
            self.pages.setCurrentIndex(0)
            self.show_notice(tr("已导入 {0} 个文件，原画质副本已保存到壁纸库。").format(len(imported)))
        if errors:
            self.show_error(tr("部分文件未导入：\n")+"\n".join(errors[:8]))

    def dragEnterEvent(self, event):
        if event.mimeData().hasUrls() and any(u.isLocalFile() for u in event.mimeData().urls()):
            event.acceptProposedAction()

    def dropEvent(self, event):
        self.import_files([u.toLocalFile() for u in event.mimeData().urls() if u.isLocalFile()])
        event.acceptProposedAction()

    def show_notice(self, text):
        self.notice.setText(text)
        self.notice.show()
        self.message_timer.start(6500)

    def show_error(self, text):
        QMessageBox.warning(self, tr("轻块桌面"), text)

    def refresh_status(self):
        self.status_label.setText("●  "+self.engine.reason)
        self.memory.setText(tr("本进程内存  {0:.0f} MB").format(win32.memory_mb()))
        self.pause_button.setEnabled(self.engine.surface is not None)
        self.pause_button.setText(tr("继续动态壁纸") if self.engine.manual_pause else tr("暂停动态壁纸"))
        busy = self.engine.busy
        self.apply_button.setEnabled(not busy and getattr(self, "_selected_file_exists", False))
        self.apply_button.setText(tr("正在切换壁纸…") if busy else tr("应用到桌面  →"))
        self.restore_button.setEnabled(not busy)
        self.remove_button.setEnabled(not busy)
        if self.online_page is not None and self.online_page.isVisible():
            self.online_page.update_details()
            self.online_page.view.viewport().update()

    def open_data_folder(self):
        from PySide6.QtCore import QUrl
        from PySide6.QtGui import QDesktopServices
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(self.store.directory)))

    def closeEvent(self, event):
        self.thumbnails.close()
        if self.online_page is not None:
            self.online_page.close_preview()
        self.closed.emit()
        event.accept()
