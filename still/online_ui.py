"""Virtualized online cards and a single, explicitly started media preview."""
from collections import OrderedDict
from html import escape
from pathlib import Path

from PySide6.QtCore import QAbstractListModel, QModelIndex, QRectF, QSize, Qt, QTimer, QUrl
from PySide6.QtGui import QColor, QDesktopServices, QFont, QImageReader, QMovie, QPainterPath, QPen, QPixmap
from PySide6.QtWidgets import (QAbstractItemView, QButtonGroup, QFrame, QHBoxLayout, QLabel,
    QLineEdit, QListView, QProgressBar, QStackedWidget, QStyledItemDelegate, QStyle,
    QVBoxLayout, QWidget)

from .i18n import tr
from .online_catalog import MAX_PREVIEW_BYTES, checksum_key, title_for
from .ui import button, label, preview_image


def download_text(library, entry):
    state = library.state(entry)
    if state == "downloaded":
        return tr("已下载")
    if state == "update":
        return tr("下载新版")
    if state == "downloading":
        received, total = library.progress.get(entry["id"], (0, entry["bytes"]))
        return tr("下载中 {0}%").format(min(100, received * 100 // total)) if total else tr("正在下载…")
    return tr("下载原文件") if state == "available" else tr("重试下载")


class CatalogModel(QAbstractListModel):
    def __init__(self, parent):
        super().__init__(parent)
        self.entries = []

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.entries)

    def data(self, index, role=Qt.ItemDataRole.DisplayRole):
        if not index.isValid() or not 0 <= index.row() < len(self.entries):
            return None
        entry = self.entries[index.row()]
        if role == Qt.ItemDataRole.ToolTipRole:
            return title_for(entry) + "\n" + entry.get("original_filename", entry["filename"])
        if role == Qt.ItemDataRole.DisplayRole:
            return title_for(entry)
        if role == Qt.ItemDataRole.UserRole:
            return entry
        return None

    def replace(self, entries):
        self.beginResetModel()
        self.entries = entries
        self.endResetModel()


class CatalogDelegate(QStyledItemDelegate):
    def __init__(self, page):
        super().__init__(page)
        self.page = page

    def sizeHint(self, _option, _index):
        return QSize(212, 184)

    def paint(self, painter, option, index):
        entry = index.data(Qt.ItemDataRole.UserRole)
        if not entry:
            return
        light = self.page.light
        selected = bool(option.state & QStyle.StateFlag.State_Selected)
        hovered = bool(option.state & QStyle.StateFlag.State_MouseOver)
        area = QRectF(option.rect).adjusted(4, 4, -4, -4)
        painter.save()
        painter.setRenderHint(painter.RenderHint.Antialiasing)
        painter.setRenderHint(painter.RenderHint.SmoothPixmapTransform)
        background = "#ffffff" if light else "#1c2631"
        border = "#538f77" if selected else ("#8daba0" if hovered else "#d3ded9" if light else "#35414d")
        painter.setPen(QPen(QColor(border), 1.5 if selected else 1))
        painter.setBrush(QColor(background))
        painter.drawRoundedRect(area, 12, 12)
        cover = QRectF(area.x()+7, area.y()+7, area.width()-14, 101)
        clip = QPainterPath()
        clip.addRoundedRect(cover, 7, 7)
        painter.save()
        painter.setClipPath(clip)
        painter.fillRect(cover, QColor("#e2eae6" if light else "#243641"))
        pixmap = self.page.cover(entry)
        if not pixmap.isNull():
            size = QSize(int(cover.width()), int(cover.height()))
            scaled = pixmap.scaled(size, Qt.AspectRatioMode.KeepAspectRatioByExpanding, Qt.TransformationMode.SmoothTransformation)
            painter.drawPixmap(int(cover.center().x()-scaled.width()/2), int(cover.center().y()-scaled.height()/2), scaled)
        else:
            painter.setPen(QColor("#5a7669" if light else "#a6c7b9"))
            painter.setFont(QFont("Segoe UI", 10))
            painter.drawText(cover, Qt.AlignmentFlag.AlignCenter, {"image": tr("静态"), "gif": "GIF", "video": tr("视频")}[entry["kind"]])
        painter.restore()
        painter.setPen(QColor("#243a30" if light else "#edf4ef"))
        painter.setFont(QFont("Microsoft YaHei UI", 10, QFont.Weight.Medium))
        text = painter.fontMetrics().elidedText(title_for(entry), Qt.TextElideMode.ElideRight, int(area.width()-22))
        painter.drawText(QRectF(area.x()+11, area.y()+113, area.width()-22, 23), Qt.AlignmentFlag.AlignVCenter, text)
        painter.setFont(QFont("Microsoft YaHei UI", 8))
        painter.setPen(QColor("#597064" if light else "#a3b5ad"))
        detail = f'{entry["width"]} × {entry["height"]}  ·  {entry["bytes"]/1000000:.1f} MB'
        painter.drawText(QRectF(area.x()+11, area.y()+138, area.width()-22, 16), Qt.AlignmentFlag.AlignVCenter, detail)
        painter.setPen(QColor("#3d8062" if light else "#a6e4ca"))
        painter.drawText(QRectF(area.x()+11, area.y()+158, area.width()-22, 16), Qt.AlignmentFlag.AlignVCenter, download_text(self.page.library, entry))
        painter.restore()


class Poster(QLabel):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.original = QPixmap()
        self.setMinimumSize(0, 160)
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setStyleSheet("background: #15232a; border-radius: 9px;")

    def display(self, pixmap):
        self.original = pixmap
        self.fit()

    def fit(self):
        if not self.original.isNull():
            self.setPixmap(self.original.scaled(self.size(), Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation))
        else:
            self.clear()

    def resizeEvent(self, event):
        self.fit()
        super().resizeEvent(event)


class OnlinePage(QWidget):
    def __init__(self, window, library):
        super().__init__()
        self.window, self.library, self.light = window, library, window.light
        self.selected, self.playing_id = None, ""
        self.preview_error = ""
        self.movie = self.player = self.audio = self.video = None
        self.cover_cache = OrderedDict()
        self.filter_kind, self.catalog_key = "image", None
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(16)
        left = QVBoxLayout()
        left.setSpacing(12)
        tools = QHBoxLayout()
        self.search = QLineEdit()
        self.search.setPlaceholderText(tr("搜索在线壁纸、风景或标签…"))
        self.search.setClearButtonEnabled(True)
        self.search.textChanged.connect(self.filter_entries)
        tools.addWidget(self.search, 1)
        self.refresh_button = button(tr("刷新"), self.library.refresh)
        tools.addWidget(self.refresh_button)
        left.addLayout(tools)
        filters = QHBoxLayout()
        group = QButtonGroup(self)
        self.kind_buttons = {}
        for kind, text in (("image", tr("静态")), ("gif", "GIF"), ("video", tr("视频"))):
            control = button(text, lambda checked=False, k=kind: self.set_filter(k), "pill")
            control.setCheckable(True)
            control.setChecked(kind == self.filter_kind)
            group.addButton(control)
            self.kind_buttons[kind] = control
            filters.addWidget(control)
        filters.addStretch()
        self.result_count = label("", "muted")
        filters.addWidget(self.result_count)
        left.addLayout(filters)
        self.view = QListView()
        self.view.setAccessibleName(tr("在线壁纸"))
        self.view.setViewMode(QListView.ViewMode.IconMode)
        self.view.setResizeMode(QListView.ResizeMode.Adjust)
        self.view.setMovement(QListView.Movement.Static)
        self.view.setUniformItemSizes(True)
        self.view.setGridSize(QSize(216, 188))
        self.view.setMouseTracking(True)
        self.view.setSpacing(0)
        self.view.setFrameShape(QFrame.Shape.NoFrame)
        self.view.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.view.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.model = CatalogModel(self)
        self.view.setModel(self.model)
        self.view.setItemDelegate(CatalogDelegate(self))
        self.view.selectionModel().currentChanged.connect(self.selection_changed)
        self.view.doubleClicked.connect(lambda _index: self.toggle_preview())
        self.browse_stack = QStackedWidget()
        self.browse_stack.addWidget(self.view)
        left.addWidget(self.browse_stack, 1)
        self.empty = label(tr("没有匹配的壁纸，试试其他关键词。"), "muted", True)
        self.empty.hide()
        left.addWidget(self.empty)
        self.status = label("", "muted", True)
        left.addWidget(self.status)
        layout.addLayout(left, 1)
        panel = QFrame()
        panel.setObjectName("panel")
        panel.setFixedWidth(282)
        right = QVBoxLayout(panel)
        right.setContentsMargins(16, 18, 16, 16)
        right.setSpacing(10)
        self.title = label("", "section", True)
        self.title.setTextFormat(Qt.TextFormat.PlainText)
        right.addWidget(self.title)
        self.preview_stack = QStackedWidget()
        self.poster = Poster()
        self.preview_stack.addWidget(self.poster)
        self.browse_stack.addWidget(self.preview_stack)
        self.inspector_cover = Poster()
        self.inspector_cover.setFixedHeight(166)
        right.addWidget(self.inspector_cover)
        self.preview_button = button(tr("开始预览"), self.toggle_preview)
        right.addWidget(self.preview_button)
        self.meta, self.attribution = label("", "muted", True), label("", "muted", True)
        self.attribution.setTextFormat(Qt.TextFormat.PlainText)
        right.addWidget(self.meta)
        right.addWidget(self.attribution)
        self.popularity = label("", "muted", True)
        right.addWidget(self.popularity)
        self.links = QLabel()
        self.links.setWordWrap(True)
        self.links.setOpenExternalLinks(True)
        right.addWidget(self.links)
        self.preview_hint = label(tr("封面用于浏览，下载保留原文件画质。动态预览默认静音。"), "muted", True)
        right.addWidget(self.preview_hint)
        right.addStretch()
        self.message = label("", "muted", True)
        right.addWidget(self.message)
        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.hide()
        right.addWidget(self.progress)
        self.download_button = button(tr("下载原文件"), self.download, "primary")
        right.addWidget(self.download_button)
        self.apply_button = button(tr("应用到桌面  →"), self.apply)
        right.addWidget(self.apply_button)
        layout.addWidget(panel)
        self.library.changed.connect(self.refresh_state)
        self.library.imported.connect(self.imported)
        self.library.media_ready.connect(self.cover_ready)
        self.library.preview_ready.connect(self.media_preview_ready)
        self.library.preview_failed.connect(self.preview_failed)
        self.library.preview_progress.connect(self.preview_progress)
        self.set_theme(self.light)
        self.refresh_state()

    def set_theme(self, light):
        self.light = light
        self.view.setStyleSheet("QListView { background: transparent; border: none; outline: none; }")
        self.view.viewport().update()
        self.update_details()

    def refresh_state(self):
        key = [(e["id"], e["revision"], checksum_key(e), e["title"]) for e in self.library.entries]
        if self.catalog_key != key:
            self.catalog_key = key
            for kind, control in self.kind_buttons.items():
                name = {"image": tr("静态"), "gif": "GIF", "video": tr("视频")}[kind]
                count = sum(e["kind"] == kind for e in self.library.entries)
                control.setText(f"{name}  {count}")
            self.filter_entries()
        self.view.viewport().update()
        messages = {"bundled": tr("精选直链 · 图片与视频由原始来源提供"),
            "cached": tr("正在显示缓存目录，已下载壁纸可离线使用。"),
            "refreshing": tr("正在刷新精选目录…"),
            "live": tr("目录已更新 · 点击卡片查看来源与授权"),
            "offline": tr("目录暂不可达，正在显示已有精选；预览和下载仍取决于素材源连接。")}
        self.status.setText(messages[self.library.catalog_state])
        self.refresh_button.setEnabled(self.library.catalog_state != "refreshing")
        self.update_details()

    def set_filter(self, kind):
        self.filter_kind = kind
        self.kind_buttons[kind].setChecked(True)
        self.filter_entries()

    def filter_entries(self):
        selected_id = self.selected["id"] if self.selected else ""
        query = self.search.text().strip().casefold()
        entries = [e for e in self.library.entries if e["kind"] == self.filter_kind
                and query in " ".join([*e["title"].values(), *e["tags"], e.get("search_text", "")]).casefold()]
        self.result_count.setText(tr("{0} 项").format(len(entries)))
        missing_gifs = self.filter_kind == "gif" and not any(e["kind"] == "gif" for e in self.library.entries)
        self.empty.setText(tr("暂未收录符合授权、原生 1080p、16:9 且不超过 20 MB 的循环 GIF。") if missing_gifs else tr("没有匹配的壁纸，试试其他关键词。"))
        self.close_preview()
        self.model.replace(entries)
        self.empty.setVisible(not entries)
        if entries:
            row = next((i for i, entry in enumerate(entries) if entry["id"] == selected_id), 0)
            self.view.setCurrentIndex(self.model.index(row))
        else:
            self.selected = None
            self.update_details()

    def selection_changed(self, current, _previous):
        self.close_preview()
        self.preview_error = ""
        self.selected = current.data(Qt.ItemDataRole.UserRole) if current.isValid() else None
        if self.selected:
            self.poster.display(self.cover(self.selected))
            self.inspector_cover.display(self.cover(self.selected))
        self.update_details()

    def cover(self, entry):
        path = self.library.thumbnail(entry)
        if path is None:
            return QPixmap()
        key = str(path)
        if key not in self.cover_cache:
            pixmap = preview_image(path, 480)
            if pixmap.isNull():
                try:
                    path.unlink(missing_ok=True)
                except OSError:
                    pass
                self.library.thumb_failed.add(entry["id"])
                return QPixmap()
            self.cover_cache[key] = pixmap
            if len(self.cover_cache) > 32:
                self.cover_cache.popitem(last=False)
        else:
            self.cover_cache.move_to_end(key)
        return self.cover_cache[key]

    def cover_ready(self, ident, _path):
        self.view.viewport().update()
        if self.selected and self.selected["id"] == ident:
            self.inspector_cover.display(self.cover(self.selected))
            if not self.playing_id or self.selected["kind"] == "image":
                self.poster.display(self.preview_cover(self.selected))

    def preview_cover(self, entry):
        path = self.library.thumbnail(entry)
        return preview_image(path, 1600) if path else QPixmap()

    def update_details(self):
        entry = self.selected
        for control in (self.preview_button, self.download_button, self.apply_button):
            control.setEnabled(bool(entry))
        if not entry:
            self.title.setText(tr("选择一张壁纸"))
            self.download_button.setText(tr("下载原文件"))
            self.preview_hint.setText(tr("封面用于浏览，下载保留原文件画质。动态预览默认静音。"))
            self.meta.clear()
            self.attribution.clear()
            self.popularity.clear()
            self.links.clear()
            self.poster.display(QPixmap())
            self.inspector_cover.display(QPixmap())
            self.progress.hide()
            self.message.clear()
            return
        self.title.setText(title_for(entry))
        kind = {"image": tr("静态"), "gif": "GIF", "video": tr("视频")}[entry["kind"]]
        self.meta.setText(f'{entry["width"]} × {entry["height"]}  ·  {entry["bytes"]/1000000:.1f} MB  ·  {kind}')
        self.preview_hint.setText(tr("动态预览使用流量优化版本；下载仍保留原始 1080p 文件。") if entry.get("preview") else tr("封面用于浏览，下载保留原文件画质。动态预览默认静音。"))
        self.attribution.setText(tr("作者：{0}\n许可：{1}").format(entry["author"], entry["license"]))
        metrics = entry.get("popularity", {})
        summary = []
        for key, text in (("votes", "公开评选支持票：{0}"), ("views", "来源浏览量：{0}"), ("favorites", "来源收藏数：{0}"), ("likes", "来源点赞数：{0}")):
            if metrics.get(key) is not None:
                if key == "views" and "views_observed_days" in metrics:
                    text = "已公开日期浏览量：{0}"
                summary.append(tr(text).format(f"{metrics[key]:,}"))
        self.popularity.setText("\n".join(summary) or tr("来源未公开热度数据。"))
        period = (tr("浏览区间：{0} 至 {1}").format(metrics["views_start"], metrics["views_end"])
                  if metrics.get("views_start") and metrics.get("views_end") else "")
        self.popularity.setToolTip(tr("热度核对日期：{0}；不同来源的指标不能直接横向比较。").format(metrics.get("checked_at", "")) + ("\n"+period if period else ""))
        if "views_observed_days" in metrics:
            self.popularity.setToolTip(self.popularity.toolTip() + "\n" + tr("有统计数据的日期：{0} 天；未公开的日期不计入。").format(metrics["views_observed_days"]))
        if entry.get("restrictions"):
            self.attribution.setToolTip(entry["restrictions"])
        else:
            self.attribution.setToolTip("")
        color = "#286b54" if self.light else "#a6e4ca"
        self.links.setText('<a style="color:'+color+'" href="'+escape(entry["source_url"], quote=True)+'">'+tr("作品来源")+'</a>　·　<a style="color:'+color+'" href="'+escape(entry["license_url"], quote=True)+'">'+tr("查看授权")+'</a>' +
            ('　·　<a style="color:'+color+'" href="'+escape(metrics["evidence_url"], quote=True)+'">'+tr("热度依据")+'</a>' if metrics.get("evidence_url") else ''))
        state = self.library.state(entry)
        self.download_button.setText(tr("取消下载") if state == "downloading" else download_text(self.library, entry))
        cached = self.library.has_preview(entry) or state == "downloaded"
        limited = self.library.limited(entry["download_url"])
        self.download_button.setEnabled(state != "downloaded" and (not limited or cached or state == "downloading"))
        self.preview_button.setEnabled(not limited or cached or bool(self.playing_id))
        self.apply_button.setEnabled(state == "downloaded" and not self.window.engine.busy)
        self.progress.setVisible(state == "downloading")
        received, total = self.library.progress.get(entry["id"], (0, entry["bytes"]))
        self.progress.setValue(min(100, received * 100 // total) if total else 0)
        errors = {"network": tr("连接失败，请重试或打开作品来源。"), "integrity": tr("文件校验未通过，未加入本地图库。"),
            "size": tr("来源文件大小已变化，请刷新目录后重试。"), "disk": tr("无法保存壁纸，请检查空间和文件夹权限。"),
            "source": tr("下载跳转到未经核验的来源，已停止。"), "limit": tr("已有两个下载进行中，请稍后重试。"),
            "rate_limit": tr("来源暂时限流，请稍后再试。已缓存或已下载的壁纸仍可使用。")}
        self.message.setText(errors.get(state, tr("下载将在后台继续；退出软件会取消未完成下载。") if state == "downloading" else self.preview_error))

    def download(self):
        if self.selected:
            if self.library.state(self.selected) == "downloading":
                self.library.cancel_download(self.selected)
            else:
                self.library.download(self.selected)

    def imported(self, _item):
        if getattr(self.window, "library_mode", "local") == "online":
            self.window.library_dirty = True
        else:
            self.window.refresh_cards()
        self.update_details()

    def apply(self):
        item = self.library.local_item(self.selected) if self.selected else None
        if item:
            self.window.select(item)
            self.window.apply_selected()

    def toggle_preview(self):
        if self.playing_id:
            self.close_preview()
            return
        entry = self.selected
        if not entry:
            return
        self.playing_id = entry["id"]
        self.preview_error = ""
        self.browse_stack.setCurrentWidget(self.preview_stack)
        self.poster.display(self.preview_cover(entry))
        self.preview_button.setText(tr("停止预览"))
        if entry["kind"] == "image":
            self.poster.display(self.preview_cover(entry))
        else:
            self.preview_button.setText(tr("正在加载预览，点击取消…"))
            self.library.preview_media(entry)

    def preview_progress(self, ident, received, total):
        if ident == self.playing_id and total:
            self.preview_button.setText(tr("预览加载 {0}% · 点击取消").format(min(100, received * 100 // total)))

    def media_preview_ready(self, ident, path):
        if ident != self.playing_id or not self.isVisible():
            return
        self.preview_button.setText(tr("停止预览"))
        if self.selected["kind"] == "video":
            from PySide6.QtMultimedia import QAudioOutput, QMediaPlayer
            from PySide6.QtMultimediaWidgets import QVideoWidget
            if self.player is None:
                self.video = QVideoWidget()
                self.preview_stack.addWidget(self.video)
                self.player = QMediaPlayer(self)
                self.audio = QAudioOutput(self)
                self.audio.setMuted(True)
                self.player.setAudioOutput(self.audio)
                self.player.setVideoOutput(self.video)
                self.player.setLoops(QMediaPlayer.Loops.Infinite)
                self.player.errorOccurred.connect(lambda *_, ident=ident: self.preview_failed(ident))
            self.preview_stack.setCurrentWidget(self.video)
            self.player.setSource(QUrl.fromLocalFile(path))
            self.player.play()
            return
        movie = QMovie(path, parent=self)
        movie.setCacheMode(QMovie.CacheMode.CacheNone)
        size = QImageReader(path).size()
        if size.isValid():
            size.scale(960, 960, Qt.AspectRatioMode.KeepAspectRatio)
            movie.setScaledSize(size)
        self.movie = movie
        movie.frameChanged.connect(lambda _frame: self.poster.display(movie.currentPixmap()))
        movie.error.connect(lambda _error: self.preview_failed(ident))
        if not movie.isValid():
            self.preview_failed(ident)
            return
        self.preview_button.setText(tr("停止预览"))
        movie.start()

    def preview_failed(self, ident):
        if ident and ident == self.playing_id:
            self.close_preview()
            self.preview_error = (tr("来源暂时限流，请稍后再试。已缓存或已下载的壁纸仍可使用。")
                if self.selected and self.library.limited(self.selected["download_url"]) else
                tr("文件较大，请先下载原文件，再开始预览。")
                if self.selected and (self.selected.get("preview") or self.selected)["bytes"] > MAX_PREVIEW_BYTES else
                tr("预览暂不可用，可重试或下载原文件后预览。"))
            self.update_details()

    def close_preview(self):
        self.playing_id = ""
        self.browse_stack.setCurrentWidget(self.view)
        self.library.cancel_preview()
        if self.movie is not None:
            self.movie.blockSignals(True)
            self.movie.stop()
            self.movie.setFileName("")
            self.movie.deleteLater()
            self.movie = None
        if self.player is not None:
            player, self.player = self.player, None
            player.blockSignals(True)
            player.stop()
            player.setVideoOutput(None)
            player.setAudioOutput(None)
            player.setSource(QUrl())
            player.deleteLater()
            self.audio.deleteLater()
            self.audio = None
            self.preview_stack.removeWidget(self.video)
            self.video.deleteLater()
            self.video = None
        self.preview_stack.setCurrentWidget(self.poster)
        self.preview_button.setText(tr("开始预览"))

    def showEvent(self, event):
        super().showEvent(event)
        self.library.activate()
        self.view.viewport().update()
        self.update_details()

    def hideEvent(self, event):
        self.close_preview()
        self.library.pause_browsing()
        self.cover_cache.clear()
        super().hideEvent(event)
