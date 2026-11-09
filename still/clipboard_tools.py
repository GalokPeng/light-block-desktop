from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W

from PySide6.QtCore import QBuffer, QByteArray, QIODevice, QObject, Qt, Signal
from PySide6.QtGui import QImage
from PySide6.QtWidgets import QApplication, QCheckBox, QHBoxLayout, QLineEdit, QListWidget, QListWidgetItem, QVBoxLayout, QWidget

from .i18n import tr


def excluded_from_history():
    """Honor Windows clipboard history exclusion formats before reading data."""
    from . import win32
    register = win32.bind(win32.user32, "RegisterClipboardFormatW", W.UINT, W.LPCWSTR)
    available = win32.bind(win32.user32, "IsClipboardFormatAvailable", W.BOOL, W.UINT)
    if available(register("ExcludeClipboardContentFromMonitorProcessing")):
        return True
    flags = [register(name) for name in ("CanIncludeInClipboardHistory", "CanUploadToCloudClipboard")]
    if not any(available(flag) for flag in flags):
        return False
    open_clipboard = win32.bind(win32.user32, "OpenClipboard", W.BOOL, W.HWND)
    close_clipboard = win32.bind(win32.user32, "CloseClipboard", W.BOOL)
    if not open_clipboard(None):
        return True
    try:
        get = win32.bind(win32.user32, "GetClipboardData", W.HANDLE, W.UINT)
        lock = win32.bind(win32.kernel32, "GlobalLock", C.c_void_p, W.HANDLE)
        unlock = win32.bind(win32.kernel32, "GlobalUnlock", W.BOOL, W.HANDLE)
        for flag in flags:
            if available(flag):
                handle = get(flag)
                pointer = lock(handle) if handle else None
                if pointer:
                    value = W.DWORD.from_address(pointer).value
                    unlock(handle)
                    if value == 0:
                        return True
    finally:
        close_clipboard()
    return False


class ClipboardEngine(QObject):
    changed = Signal()

    def __init__(self, workbench):
        super().__init__()
        self.workbench = workbench
        self.enabled = False
        self.last_written = None
        self.set_enabled(workbench.controller.store.settings.clipboard_enabled)

    def set_enabled(self, value):
        clipboard = QApplication.clipboard()
        if value and not self.enabled:
            clipboard.dataChanged.connect(self.capture)
        elif not value and self.enabled:
            clipboard.dataChanged.disconnect(self.capture)
        self.enabled = bool(value)
        self.workbench.controller.store.settings.clipboard_enabled = self.enabled
        self.workbench.controller.store.save()
        self.changed.emit()

    def capture(self):
        if excluded_from_history():
            return
        clipboard = QApplication.clipboard()
        mime = clipboard.mimeData()
        if mime.hasImage():
            image = clipboard.image()
            if image.isNull() or image.width()*image.height() > 20000000:
                return
            buffer = QBuffer()
            buffer.open(QIODevice.OpenModeFlag.WriteOnly)
            image.save(buffer, "PNG")
            payload = bytes(buffer.data())
            self.workbench.db.add_clip("image", tr("图片 {0} × {1}").format(image.width(), image.height()), payload)
        elif mime.hasText() and mime.text().strip():
            self.workbench.db.add_clip("text", mime.text())
        else:
            return
        self.changed.emit()

    def copy(self, ident):
        rows = self.workbench.db.query("SELECT * FROM clipboard WHERE id=?", (ident,))
        if rows:
            row = rows[0]
            if row["kind"] == "image":
                QApplication.clipboard().setImage(QImage.fromData(row["payload"]))
            else:
                QApplication.clipboard().setText(row["text"])


    def cleanup(self):
        if self.enabled:
            QApplication.clipboard().dataChanged.disconnect(self.capture)
            self.enabled = False


class ClipboardWindow(QWidget):
    def __init__(self, workbench):
        super().__init__()
        from .ui import STYLE, button, label
        self.workbench, self.engine = workbench, workbench.clipboard()
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setWindowTitle(tr("轻块桌面 · 剪贴板历史"))
        self.setObjectName("root")
        self.setWindowIcon(workbench.controller.icon)
        self.setStyleSheet(STYLE+"QListWidget{background:#182331;border:0;border-radius:10px;} QListWidget::item{padding:12px;border-bottom:1px solid #2c394a;} QListWidget::item:selected{background:#365447;}")
        self.resize(760, 590)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(23, 21, 23, 21)
        layout.addWidget(label(tr("复制过的灵感，随时找回来。"), "title"))
        self.enabled = QCheckBox(tr("启用本地剪贴板记录（默认关闭）"))
        self.enabled.setChecked(self.engine.enabled)
        self.enabled.toggled.connect(self.engine.set_enabled)
        layout.addWidget(self.enabled)
        layout.addWidget(label(tr("内容仅存本机。应用声明禁止历史记录的内容会跳过；单项上限 5 MB，总内容约 32 MB。"), "muted", True))
        self.search = QLineEdit()
        self.search.setPlaceholderText(tr("搜索历史文本…"))
        self.search.textChanged.connect(self.refresh)
        layout.addWidget(self.search)
        self.history = QListWidget()
        self.history.itemDoubleClicked.connect(lambda _: self.copy())
        layout.addWidget(self.history, 1)
        row = QHBoxLayout()
        row.addWidget(button(tr("记录当前内容"), self.engine.capture))
        row.addWidget(button(tr("固定 / 取消固定"), self.pin))
        row.addWidget(button(tr("清空未固定项"), self.clear))
        row.addStretch()
        row.addWidget(button(tr("复制所选"), self.copy, "primary"))
        layout.addLayout(row)
        self.status = label(tr("双击复制，切回目标应用粘贴。"), "muted")
        layout.addWidget(self.status)
        self.engine.changed.connect(self.refresh)
        self.refresh()

    def refresh(self, *_):
        self.history.clear()
        query = "%"+self.search.text()+"%"
        for row in self.workbench.db.query("SELECT id,kind,substr(text,1,200) summary,created,pinned FROM clipboard WHERE text LIKE ? ORDER BY pinned DESC,created DESC LIMIT 100", (query,)):
            text = row["summary"].replace("\n", "  ")
            item = QListWidgetItem(("★  " if row["pinned"] else "")+text+"\n"+row["created"][:19].replace("T", " ")+"  ·  "+row["kind"])
            item.setData(Qt.ItemDataRole.UserRole, row["id"])
            self.history.addItem(item)

    def selected_id(self):
        return self.history.currentItem().data(Qt.ItemDataRole.UserRole) if self.history.currentItem() else None

    def copy(self):
        if self.selected_id() is not None:
            self.engine.copy(self.selected_id())
            self.status.setText(tr("已复制，切回应用即可粘贴。"))

    def pin(self):
        if self.selected_id() is not None:
            try:
                self.workbench.db.pin_clip(self.selected_id())
                self.refresh()
            except ValueError as exc:
                self.workbench.error(str(exc))

    def clear(self):
        self.workbench.db.execute("DELETE FROM clipboard WHERE pinned=0")
        self.refresh()
