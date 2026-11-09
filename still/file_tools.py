from __future__ import annotations

from datetime import datetime
import hashlib
import json
from pathlib import Path
import os

from PySide6.QtCore import QFileInfo, QThread, Qt, QTimer, Signal
from PySide6.QtGui import QImage, QImageReader, QPixmap
from PySide6.QtWidgets import (QAbstractItemView, QComboBox, QDialog, QFileDialog, QFileIconProvider, QHBoxLayout,
    QLabel, QLineEdit, QListWidget, QListWidgetItem, QPlainTextEdit, QSplitter, QStackedWidget,
    QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget)

from .desktop_windows import open_target, reveal_file
from .productivity_model import RenameJournal, rename_preview


class HashWorker(QThread):
    result = Signal(str, str)

    def __init__(self, path, parent=None):
        super().__init__(parent)
        self.path = path

    def run(self):
        try:
            digest = hashlib.sha256()
            with open(self.path, "rb") as file:
                while True:
                    if self.isInterruptionRequested():
                        return
                    block = file.read(1024*1024)
                    if not block:
                        break
                    digest.update(block)
            if not self.isInterruptionRequested():
                self.result.emit(self.path, digest.hexdigest())
        except OSError as exc:
            self.result.emit(self.path, str(exc))


class DirectoryScanner(QThread):
    listed = Signal(str, list, str)

    def __init__(self, path, parent=None):
        super().__init__(parent)
        self.path = path

    def run(self):
        rows = []
        try:
            with os.scandir(self.path) as entries:
                for entry in entries:
                    if self.isInterruptionRequested():
                        return
                    rows.append({"path":entry.path,"name":entry.name,"directory":entry.is_dir(follow_symlinks=False)})
                    if len(rows) >= 10000:
                        break
            rows.sort(key=lambda row:(not row["directory"],row["name"].casefold()))
            self.listed.emit(self.path, rows, "")
        except OSError as exc:
            self.listed.emit(self.path, [], str(exc))


class RenameDialog(QDialog):
    def __init__(self, workbench, paths, parent=None):
        super().__init__(parent)
        from .ui import STYLE, button, label
        self.workbench, self.paths = workbench, paths
        self.journal = RenameJournal(workbench.controller.store.directory)
        self.pairs = []
        self.setWindowTitle("批量重命名 · 先预览再应用")
        self.setObjectName("root")
        self.setStyleSheet(STYLE)
        self.resize(800, 560)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(23, 20, 23, 20)
        layout.addWidget(label("把命名，也整理得井井有条。", "section"))
        self.pattern = QLineEdit("{n:03}_{name}{ext}")
        self.pattern.setPlaceholderText("{name} 原名称 · {ext} 扩展名 · {n:03} 三位序号")
        self.pattern.textChanged.connect(self.preview)
        layout.addWidget(self.pattern)
        layout.addWidget(label("模板支持 {name}、{ext}、{n:03}。同名冲突会阻止操作；成功后可撤销最近一次。", "muted", True))
        self.table = QTableWidget(0, 2)
        self.table.setHorizontalHeaderLabels(["原名称", "新名称"])
        self.table.horizontalHeader().setStretchLastSection(True)
        self.table.setColumnWidth(0, 330)
        self.table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        layout.addWidget(self.table, 1)
        self.status = label("", "muted", True)
        layout.addWidget(self.status)
        actions = QHBoxLayout()
        actions.addWidget(button("取消", self.reject))
        actions.addStretch()
        self.apply_button = button("应用重命名", self.apply, "primary")
        actions.addWidget(self.apply_button)
        layout.addLayout(actions)
        self.preview()

    def preview(self):
        try:
            self.pairs = rename_preview(self.paths, self.pattern.text())
            self.table.setRowCount(len(self.pairs))
            for index, (source, target) in enumerate(self.pairs):
                self.table.setItem(index, 0, QTableWidgetItem(source.name))
                self.table.setItem(index, 1, QTableWidgetItem(target.name))
            self.apply_button.setEnabled(True)
            self.status.setText(f"{len(self.pairs)} 个项目 · 未应用前原文件保持不变")
        except (ValueError, OSError, IndexError) as exc:
            self.apply_button.setEnabled(False)
            self.status.setText(str(exc))

    def apply(self):
        try:
            # Recheck live files, since preview may have been open for minutes.
            pairs = rename_preview(self.paths, self.pattern.text())
            self.workbench.last_rename = self.journal.apply(pairs)
            self.accept()
        except OSError as exc:
            self.status.setText("未完成，恢复日志保留在 rename_jobs："+str(exc))


class FileWorkbench(QWidget):
    def __init__(self, workbench):
        super().__init__()
        from .ui import STYLE, button, label
        self.workbench = workbench
        self.folder = None
        self.worker = None
        self.scanner = None
        self.cache_folder = None
        self.cached_entries = []
        self.page = 0
        self.current = None
        self.provider = QFileIconProvider()
        self.setWindowTitle("轻块桌面 · 文件工作台")
        self.setObjectName("root")
        self.setWindowIcon(workbench.controller.icon)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setStyleSheet(STYLE+"QListWidget,QPlainTextEdit{background:#172330;border:1px solid #334253;border-radius:9px;padding:8px;} QListWidget::item{padding:8px;} QListWidget::item:selected{background:#35564a;}")
        self.resize(1120, 720)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(23, 22, 23, 20)
        layout.addWidget(label("先看清，再整理。", "title"))
        row = QHBoxLayout()
        self.path_label = QLineEdit()
        self.path_label.setReadOnly(True)
        self.path_label.setPlaceholderText("选择文件夹，预览与整理本地文件")
        row.addWidget(self.path_label, 1)
        row.addWidget(button("选择文件夹…", self.choose))
        row.addWidget(button("上一级", self.up))
        row.addWidget(button("刷新", lambda: self.refresh(force=True)))
        layout.addLayout(row)
        self.search = QLineEdit()
        self.search.setPlaceholderText("按名称筛选当前文件夹…")
        self.search_timer = QTimer(self)
        self.search_timer.setSingleShot(True)
        self.search_timer.setInterval(180)
        self.search_timer.timeout.connect(self.render_list)
        self.search.textChanged.connect(self.search_changed)
        layout.addWidget(self.search)
        split = QSplitter()
        self.list = QListWidget()
        self.list.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        self.list.currentItemChanged.connect(self.select)
        self.list.itemDoubleClicked.connect(self.open_or_enter)
        split.addWidget(self.list)
        preview = QWidget()
        pv = QVBoxLayout(preview)
        pv.setContentsMargins(15, 0, 0, 0)
        self.meta = label("选择一个文件查看预览", "muted", True)
        self.meta.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        pv.addWidget(self.meta)
        self.stack = QStackedWidget()
        self.image_label = QLabel()
        self.image_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.image_label.setMinimumSize(300, 260)
        self.text = QPlainTextEdit()
        self.text.setReadOnly(True)
        self.stack.addWidget(self.image_label)
        self.stack.addWidget(self.text)
        pv.addWidget(self.stack, 1)
        split.addWidget(preview)
        split.setSizes([350, 700])
        layout.addWidget(split, 1)
        pages = QHBoxLayout()
        pages.addWidget(button("上一页", lambda: self.change_page(-1)))
        self.page_label = label("", "muted")
        pages.addWidget(self.page_label)
        pages.addWidget(button("下一页", lambda: self.change_page(1)))
        pages.addStretch()
        layout.addLayout(pages)
        actions = QHBoxLayout()
        actions.addWidget(button("系统应用打开", self.open))
        actions.addWidget(button("在文件夹中显示", self.reveal))
        actions.addWidget(button("计算 SHA-256", self.hash))
        actions.addWidget(button("撤销最近重命名", self.undo))
        actions.addWidget(button("恢复中断任务", self.recover))
        actions.addStretch()
        actions.addWidget(button("批量重命名…", self.rename, "primary"))
        layout.addLayout(actions)
        self.status = label("预览为只读。批量重命名需要先确认预览，并保留恢复日志。", "muted", True)
        self.status.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        layout.addWidget(self.status)
        pending = RenameJournal(workbench.controller.store.directory).pending()
        if pending:
            self.status.setText(f"发现 {len(pending)} 项中断的重命名任务，请点击“恢复中断任务”。文件有身份记录和重名保护。")

    def choose(self):
        folder = QFileDialog.getExistingDirectory(self, "选择文件夹")
        if folder:
            self.folder = Path(folder)
            self.refresh()

    def up(self):
        if self.folder:
            self.folder = self.folder.parent
            self.refresh()

    def refresh(self, *_, force=False):
        if not self.folder:
            return
        self.path_label.setText(str(self.folder))
        if self.cache_folder == self.folder and not force:
            self.render_list()
            return
        if self.scanner is not None and self.scanner.isRunning():
            self.scanner.requestInterruption()
        self.scanner = DirectoryScanner(str(self.folder),self.workbench)
        self.workbench.track_thread(self.scanner)
        self.scanner.listed.connect(self.directory_loaded)
        self.scanner.finished.connect(self.scan_finished)
        self.scanner.start()
        self.status.setText("正在读取目录…搜索使用缓存，图标仅按当前页加载。")

    def directory_loaded(self, path, rows, error):
        if self.sender() is not self.scanner or self.folder != Path(path):
            return
        self.cache_folder = self.folder
        self.cached_entries = rows
        self.page = 0
        self.render_list()
        if error:
            self.status.setText(error)

    def scan_finished(self):
        if self.sender() is self.scanner:
            self.scanner = None

    def search_changed(self):
        self.page = 0
        self.search_timer.start()

    def render_list(self):
        query = self.search.text().casefold()
        rows = [row for row in self.cached_entries if query in row["name"].casefold()]
        self.page = max(0,min(self.page,max(0,(len(rows)-1)//150)))
        self.list.setUpdatesEnabled(False)
        self.list.clear()
        for row in rows[self.page*150:(self.page+1)*150]:
            item = QListWidgetItem(self.provider.icon(QFileInfo(row["path"])),row["name"])
            item.setData(Qt.ItemDataRole.UserRole,row["path"])
            self.list.addItem(item)
        self.list.setUpdatesEnabled(True)
        self.page_label.setText(f"第 {self.page+1} / {max(1,(len(rows)+149)//150)} 页 · {len(rows)} 个结果")
        if len(self.cached_entries) >= 10000:
            self.status.setText("当前目录仅索引前 10000 项，请进入子目录进一步查找。")

    def change_page(self, delta):
        self.page += delta
        self.render_list()

    def select(self, item, previous=None):
        if self.worker and self.worker.isRunning():
            self.worker.requestInterruption()
        self.current = Path(item.data(Qt.ItemDataRole.UserRole)) if item else None
        if not self.current:
            return
        path = self.current
        try:
            stat = path.stat()
            self.meta.setText(f"{path.name}\n{stat.st_size:,} 字节 · 修改于 {datetime.fromtimestamp(stat.st_mtime):%Y-%m-%d %H:%M}\n{path}")
            if path.is_dir():
                self.text.setPlainText("文件夹 · 双击进入\n\n"+"\n".join(p.name for _,p in zip(range(120),path.iterdir())))
                self.stack.setCurrentWidget(self.text)
                return
            image = QImage()
            if path.suffix.lower() in {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".gif", ".tif", ".tiff"}:
                reader = QImageReader(str(path))
                reader.setAutoTransform(True)
                size = reader.size()
                if size.isValid():
                    size.scale(1400, 1000, Qt.AspectRatioMode.KeepAspectRatio)
                    reader.setScaledSize(size)
                image = reader.read()
            if not image.isNull():
                self.image_label.setPixmap(QPixmap.fromImage(image).scaled(640, 470, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation))
                self.stack.setCurrentWidget(self.image_label)
            elif path.suffix.lower() in {".txt", ".md", ".json", ".csv", ".py", ".log", ".yaml", ".yml", ".ini", ".xml", ".html", ".css", ".js", ".ts"}:
                with path.open("rb") as file:
                    content = file.read(512*1024)
                self.text.setPlainText(content.decode("utf-8", "replace")+ ("\n\n…预览仅显示前 512 KB" if stat.st_size > len(content) else ""))
                self.stack.setCurrentWidget(self.text)
            else:
                self.text.setPlainText("当前格式请使用系统应用打开。\n\n你仍可查看属性、计算校验值，或参与批量重命名。")
                self.stack.setCurrentWidget(self.text)
        except OSError as exc:
            self.status.setText(str(exc))

    def open_or_enter(self, item):
        path = Path(item.data(Qt.ItemDataRole.UserRole))
        if path.is_dir():
            self.folder = path
            self.refresh()
        else:
            self.open()

    def open(self):
        if self.current:
            try: open_target(str(self.current))
            except OSError as exc: self.workbench.error(str(exc))

    def reveal(self):
        if self.current:
            reveal_file(str(self.current))

    def hash(self):
        if not self.current or not self.current.is_file():
            return
        if self.worker and self.worker.isRunning():
            self.worker.requestInterruption()
            self.status.setText("上一项计算正在取消，请稍后重试。")
            return
        self.worker = HashWorker(str(self.current), self.workbench)
        self.workbench.track_thread(self.worker)
        self.worker.finished.connect(self.hash_finished)
        self.worker.result.connect(self.hash_result)
        self.worker.start()
        self.status.setText("正在计算 SHA-256，可继续浏览；关闭窗口会取消。")

    def hash_finished(self):
        if self.sender() is self.worker:
            self.worker = None

    def hash_result(self, path, value):
        if self.current is not None and str(self.current) == path and self.sender() is self.worker:
            self.status.setText(Path(path).name+"\nSHA-256: "+value)

    def rename(self):
        paths = [Path(item.data(Qt.ItemDataRole.UserRole)) for item in self.list.selectedItems()]
        if paths and RenameDialog(self.workbench, paths, self).exec():
            self.refresh(force=True)
            self.status.setText("重命名完成，可撤销最近一次。")

    def undo(self):
        if not self.workbench.last_rename:
            self.status.setText("当前没有可撤销的重命名任务。")
            return
        try:
            RenameJournal(self.workbench.controller.store.directory).undo(self.workbench.last_rename)
            self.workbench.last_rename = None
            self.refresh(force=True)
            self.status.setText("已撤销重命名。")
        except (OSError, ValueError) as exc:
            self.workbench.error(str(exc))

    def recover(self):
        journal = RenameJournal(self.workbench.controller.store.directory)
        restored, errors = 0, []
        for file,data in journal.pending():
            try:
                journal.recover(file)
                restored += 1
            except (OSError,ValueError) as exc:
                errors.append(str(exc))
        self.refresh(force=True)
        self.status.setText(f"已恢复 {restored} 项任务。"+("\n"+"\n".join(errors) if errors else "原文件名已还原；冲突文件不会覆盖。"))

    def closeEvent(self, event):
        self.search_timer.stop()
        if self.scanner and self.scanner.isRunning():
            self.scanner.requestInterruption()
        if self.worker and self.worker.isRunning():
            self.worker.requestInterruption()
        event.accept()
