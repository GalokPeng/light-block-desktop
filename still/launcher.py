from __future__ import annotations

import os
from pathlib import Path

from PySide6.QtCore import QFileInfo, QThread, Qt, Signal
from PySide6.QtGui import QCursor, QKeySequence, QShortcut
from PySide6.QtWidgets import QApplication, QFileIconProvider, QLineEdit, QListWidget, QListWidgetItem, QVBoxLayout, QWidget

from .desktop_windows import open_target, start_menu_folders
from .productivity_model import calculate


class AppIndexer(QThread):
    indexed = Signal(list)

    def run(self):
        entries, known = [], set()
        for root in start_menu_folders():
            for folder, _, files in os.walk(root):
                if self.isInterruptionRequested():
                    return
                for name in files:
                    path = Path(folder) / name
                    if path.suffix.lower() not in {".lnk", ".url", ".appref-ms"} or str(path).casefold() in known:
                        continue
                    known.add(str(path).casefold())
                    entries.append({"name": path.stem, "target": str(path), "kind": "应用"})
                    if len(entries) >= 10000:
                        self.indexed.emit(entries)
                        return
        self.indexed.emit(entries)


class Launcher(QWidget):
    def __init__(self, workbench):
        super().__init__(None, Qt.WindowType.Window | Qt.WindowType.FramelessWindowHint | Qt.WindowType.WindowStaysOnTopHint)
        from .ui import STYLE, label
        self.workbench = workbench
        self.setWindowTitle("轻块桌面 · 快速启动")
        self.setObjectName("root")
        self.setWindowIcon(workbench.controller.icon)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setStyleSheet(STYLE+"QLineEdit{font-size:20px;padding:17px;} QListWidget{background:#182330;border:0;border-radius:10px;outline:0;} QListWidget::item{padding:12px;border-radius:7px;} QListWidget::item:selected{background:#365548;}")
        self.resize(680, 480)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 18)
        self.search = QLineEdit()
        self.search.setPlaceholderText("搜索应用、分组文件，或直接计算 24 × 60")
        self.search.setClearButtonEnabled(True)
        layout.addWidget(self.search)
        self.results = QListWidget()
        self.results.setSpacing(3)
        layout.addWidget(self.results, 1)
        self.status = label("输入关键词搜索 · ↑↓ 选择 · Enter 打开 · Esc 关闭", "muted")
        layout.addWidget(self.status)
        self.provider = QFileIconProvider()
        self.search.textChanged.connect(self.filter)
        self.search.returnPressed.connect(self.open_selected)
        self.results.itemActivated.connect(lambda _: self.open_selected())
        QShortcut(QKeySequence("Escape"), self, self.close)
        QShortcut(QKeySequence("Down"), self, lambda: self.navigate(1))
        QShortcut(QKeySequence("Up"), self, lambda: self.navigate(-1))
        self.commands = [
            {"name": "截图工作室", "kind": "轻块桌面工具", "action": "capture"},
            {"name": "录制屏幕 / 停止录屏", "kind": "轻块桌面工具", "action": "record"},
            {"name": "视频裁剪与 GIF 导出", "kind": "轻块桌面工具", "action": "media"},
            {"name": "剪贴板历史", "kind": "轻块桌面工具", "action": "clipboard"},
            {"name": "便签与任务", "kind": "轻块桌面工具", "action": "notes"},
            {"name": "文件工作台", "kind": "轻块桌面工具", "action": "files"},
            {"name": "专注工作区", "kind": "轻块桌面工具", "action": "focus"},
        ]
        self.entries = list(workbench.app_index)
        if not workbench.app_index:
            self.status.setText("正在建立开始菜单索引…")
            workbench.ensure_app_index()
        workbench.apps_indexed.connect(self.index_ready)
        self.filter()

    def index_ready(self, entries):
        self.entries = entries
        self.filter()

    def showEvent(self, event):
        screen = QApplication.screenAt(QCursor.pos()) or QApplication.primaryScreen()
        area = screen.availableGeometry()
        self.move(area.center().x()-self.width()//2, area.y()+max(50, area.height()//5))
        super().showEvent(event)
        self.search.setFocus()

    def filter(self, *_):
        query = self.search.text().strip()
        self.results.clear()
        if query and any(c.isdigit() for c in query):
            try:
                expression = query.replace("×", "*").replace("÷", "/")
                result = calculate(expression)
                item = QListWidgetItem(f"=  {result}    ·    Enter 复制结果")
                item.setData(Qt.ItemDataRole.UserRole, {"result": str(result)})
                self.results.addItem(item)
            except (ValueError, SyntaxError, ZeroDivisionError, OverflowError):
                pass
        collections = self.workbench.controller.get_organizer().collections
        files = [{"name": e["name"], "target": e["target"], "kind": g["name"]} for g in collections.groups for e in g["entries"]]
        candidates = self.commands + files + self.entries
        tokens = query.casefold().split()
        matched = [entry for entry in candidates if all(token in entry["name"].casefold() for token in tokens)]
        matched.sort(key=lambda entry: (0 if entry["name"].casefold().startswith(query.casefold()) else 1, entry["name"].casefold()))
        for entry in matched[:16]:
            item = QListWidgetItem(entry["name"]+"\n"+entry["kind"])
            if "target" in entry and Path(entry["target"]).exists():
                item.setIcon(self.provider.icon(QFileInfo(entry["target"])))
            item.setData(Qt.ItemDataRole.UserRole, entry)
            self.results.addItem(item)
        if self.results.count():
            self.results.setCurrentRow(0)
        self.status.setText(f"{len(self.entries)} 个开始菜单应用 · ↑↓ 选择 · Enter 打开 · Esc 关闭")

    def navigate(self, delta):
        if self.results.count():
            self.results.setCurrentRow((self.results.currentRow()+delta) % self.results.count())

    def open_selected(self):
        item = self.results.currentItem()
        if item is None:
            return
        entry = item.data(Qt.ItemDataRole.UserRole)
        try:
            if "result" in entry:
                QApplication.clipboard().setText(entry["result"])
            elif "action" in entry:
                action = entry["action"]
                self.close()
                self.workbench.invoke(action)
                return
            else:
                open_target(entry["target"])
            self.close()
        except OSError as exc:
            self.workbench.error(str(exc))
