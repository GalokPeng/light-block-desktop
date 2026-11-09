from __future__ import annotations

from datetime import datetime, timezone
import uuid

from PySide6.QtCore import QDateTime, QObject, QPoint, QRect, QRectF, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QCursor, QFont, QPainter
from PySide6.QtWidgets import (QApplication, QCheckBox, QComboBox, QDateTimeEdit, QHBoxLayout, QLineEdit,
    QListWidget, QListWidgetItem, QMenu, QPlainTextEdit, QVBoxLayout, QWidget)

from .desktop_windows import desktop_layer, desktop_owner, DesktopWatcher, paint_desktop_panel
from .productivity_model import utc_now


class NotePin(QWidget):
    def __init__(self, engine, row):
        super().__init__(None, Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint | Qt.WindowType.WindowDoesNotAcceptFocus)
        self.engine, self.ident = engine, row["id"]
        self.row = row
        self.drag = None
        self.setWindowTitle("轻块桌面 · 桌面便签")
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.resize(290, 240)
        self.layer_timer = QTimer(self)
        self.layer_timer.setSingleShot(True)
        self.layer_timer.timeout.connect(self.position_layer)
        self.setToolTip("拖动位置 · 双击编辑 · 右键管理")

    def position_layer(self):
        desktop_layer(int(self.winId()))

    def showEvent(self, event):
        self.setWindowOpacity(1 if self.geometry().contains(QCursor.pos()) else .72)
        self.layer_timer.start(0)
        super().showEvent(event)

    def enterEvent(self, event):
        self.setWindowOpacity(1)
        super().enterEvent(event)

    def leaveEvent(self, event):
        self.setWindowOpacity(.72)
        super().leaveEvent(event)

    def set_note(self, row):
        self.row = row
        self.update()

    def paintEvent(self, event):
        row = self.row
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        paint_desktop_panel(painter, QRectF(self.rect()), 190)
        accent = {"mint": "#a6e4ca", "blue": "#9ac8ff", "amber": "#f5d18e", "rose": "#efb5c5"}.get(row["color"], "#a6e4ca")
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(accent))
        painter.drawEllipse(QPoint(23, 29), 4, 4)
        painter.setPen(QColor("#f4f7fc"))
        painter.setFont(QFont("Microsoft YaHei UI", 12, QFont.Weight.DemiBold))
        title = ("已完成 · " if row["done"] else "") + row["title"]
        title = painter.fontMetrics().elidedText(title, Qt.TextElideMode.ElideRight, self.width() - 53)
        painter.drawText(34, 12, self.width()-53, 34, Qt.AlignmentFlag.AlignVCenter, title)
        painter.setPen(QColor(210, 226, 244, 48))
        painter.drawLine(18, 52, self.width()-18, 52)
        painter.setPen(QColor("#d8e4ef"))
        painter.setFont(QFont("Microsoft YaHei UI", 10))
        painter.drawText(19, 65, self.width()-38, self.height()-84, Qt.TextFlag.TextWordWrap, row["body"][:1800])

    def place(self, position):
        desired = QRect(position, self.size())
        screen = QApplication.screenAt(desired.center()) or next(
            (screen for screen in QApplication.screens() if screen.availableGeometry().intersects(desired)),
            QApplication.primaryScreen())
        if screen is not None:
            area = screen.availableGeometry()
            position = QPoint(max(area.left(), min(position.x(), area.right()-self.width()+1)),
                              max(area.top(), min(position.y(), area.bottom()-self.height()+1)))
        self.move(position)

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.drag = (event.globalPosition().toPoint()-self.pos(), self.pos())

    def mouseMoveEvent(self, event):
        if self.drag is not None:
            self.move(event.globalPosition().toPoint()-self.drag[0])

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton and self.drag is not None:
            _, start = self.drag
            self.drag = None
            if self.pos() != start:
                self.place(self.pos())
                self.engine.db.execute("UPDATE notes SET pin_x=?,pin_y=? WHERE id=?", (self.x(), self.y(), self.ident))

    def mouseDoubleClickEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.engine.edit(self.ident)

    def contextMenuEvent(self, event):
        menu = QMenu(self)
        menu.addAction("编辑便签", lambda: self.engine.edit(self.ident))
        menu.addAction("标记完成 / 待办", lambda: self.engine.toggle_done(self.ident))
        menu.addAction("取消贴到桌面", lambda: self.engine.pin(self.ident, False))
        menu.exec(event.globalPos())


class NotesEngine(QObject):
    changed = Signal()
    before_change = Signal()

    def __init__(self, workbench):
        super().__init__()
        self.workbench, self.db = workbench, workbench.db
        self.pins = {}
        self.watching = False
        self.closing = False
        self.watcher = DesktopWatcher(self)
        self.watcher.changed.connect(self.rebuild)
        self.repair_timer = QTimer(self)
        self.repair_timer.setSingleShot(True)
        self.repair_timer.setInterval(900)
        self.repair_timer.timeout.connect(self.repair)
        self.repair_attempts = 0
        workbench.controller.app.screenAdded.connect(self.screen_changed)
        workbench.controller.app.screenRemoved.connect(self.screen_changed)
        workbench.controller.app.primaryScreenChanged.connect(self.screen_changed)
        self.reminder = QTimer(self)
        self.reminder.setSingleShot(True)
        self.reminder.timeout.connect(self.notify_due)
        self.schedule()
        self.refresh_pins()

    def get(self, ident):
        rows = self.db.query("SELECT * FROM notes WHERE id=?", (ident,))
        return rows[0] if rows else None

    def create(self):
        ident = uuid.uuid4().hex
        self.db.execute("INSERT INTO notes(id,title,body,updated) VALUES(?,?,?,?)", (ident, "新的便签", "", utc_now()))
        self.changed.emit()
        return ident

    def save(self, ident, title, body, color, done, due):
        previous = self.get(ident)
        notified = 0 if previous is None or previous["due"] != due else previous["notified"]
        self.db.execute("UPDATE notes SET title=?,body=?,color=?,done=?,due=?,notified=?,updated=? WHERE id=?",
                        (title.strip()[:100] or "未命名便签", body, color, int(done), due, notified, utc_now(), ident))
        self.schedule()
        if ident in self.pins:
            self.pins[ident].set_note(self.get(ident))

    def toggle_done(self, ident):
        self.before_change.emit()
        self.db.execute("UPDATE notes SET done=1-done,updated=? WHERE id=?", (utc_now(), ident))
        self.schedule()
        self.refresh_pins()
        self.changed.emit()

    def pin(self, ident, value):
        self.before_change.emit()
        self.db.execute("UPDATE notes SET pinned=? WHERE id=?", (int(value), ident))
        self.refresh_pins()
        self.schedule()
        self.changed.emit()

    def archive(self, ident, archived=True):
        self.before_change.emit()
        self.db.execute("UPDATE notes SET archived=?,pinned=0,due=NULL,updated=? WHERE id=?", (int(archived), utc_now(), ident))
        self.refresh_pins()
        self.schedule()
        self.changed.emit()

    def edit(self, ident):
        self.workbench.invoke("notes")
        window = self.workbench.windows.get("notes")
        if window:
            window.search.clear()
            window.filter.setCurrentIndex(0)
            window.selected = ident
            window.refresh()

    def refresh_pins(self):
        if self.closing:
            return
        desired = {row["id"]: row for row in self.db.query("SELECT * FROM notes WHERE pinned=1 AND archived=0 ORDER BY updated,id")}
        for ident in list(self.pins):
            if ident not in desired:
                pin = self.pins.pop(ident)
                pin.layer_timer.stop()
                pin.hide()
                pin.deleteLater()
        for ident, row in desired.items():
            if ident not in self.pins:
                pin = NotePin(self, row)
                self.pins[ident] = pin
                area = QApplication.primaryScreen().availableGeometry()
                x = row["pin_x"] if row["pin_x"] is not None else area.x()+140+(len(self.pins)%6)*32
                y = row["pin_y"] if row["pin_y"] is not None else area.y()+min(400,area.height()//3)+(len(self.pins)%6)*20
                pin.place(QPoint(x, y))
                pin.show()
            else:
                self.pins[ident].set_note(row)
        if desired and not self.watching:
            self.watcher.start()
            self.watching = True
            if not self.watcher.owner:
                self.rebuild()
        elif not desired and self.watching:
            self.watcher.stop()
            self.watching = False
            self.repair_timer.stop()

    def screen_changed(self, *_):
        if self.closing:
            return
        for pin in self.pins.values():
            pin.place(pin.pos())

    def rebuild(self):
        if self.pins and not self.closing:
            self.repair_attempts = 0
            self.repair_timer.start()

    def repair(self):
        if not self.pins or self.closing:
            return
        if not desktop_owner():
            self.repair_attempts += 1
            if self.repair_attempts < 8:
                self.repair_timer.start()
            return
        self.watcher.start()
        for pin in self.pins.values():
            pin.layer_timer.stop()
            pin.hide()
            pin.deleteLater()
        self.pins.clear()
        self.refresh_pins()

    def schedule(self):
        self.reminder.stop()
        rows = self.db.query("SELECT due FROM notes WHERE due IS NOT NULL AND done=0 AND notified=0 AND archived=0 ORDER BY due LIMIT 1")
        if rows:
            try:
                remaining = (datetime.fromisoformat(rows[0]["due"])-datetime.now(timezone.utc)).total_seconds()
                self.reminder.start(max(1000, min(24*3600*1000, int(remaining*1000))))
            except ValueError:
                pass
        background = bool(rows or self.pins or self.db.query("SELECT id FROM notes WHERE pinned=1 LIMIT 1"))
        store = self.workbench.controller.store
        if store.settings.notes_background != background:
            store.settings.notes_background = background
            store.save()

    def notify_due(self):
        now = utc_now()
        rows = self.db.query("SELECT * FROM notes WHERE due<=? AND done=0 AND notified=0 AND archived=0", (now,))
        for row in rows:
            self.workbench.controller.tray.showMessage("轻块桌面 · 任务提醒", row["title"]+"\n"+row["body"][:100])
            self.db.execute("UPDATE notes SET notified=1 WHERE id=?", (row["id"],))
        self.schedule()
        self.changed.emit()

    def cleanup(self):
        self.closing = True
        self.watcher.stop()
        self.repair_timer.stop()
        self.reminder.stop()
        for pin in self.pins.values():
            pin.close()
            pin.deleteLater()
        self.pins.clear()


class NotesWindow(QWidget):
    def __init__(self, workbench):
        super().__init__()
        from .ui import STYLE, button, label
        self.workbench, self.engine = workbench, workbench.notes()
        self.selected = None
        self.loaded_id = None
        self.dirty = False
        self.updating = False
        self.setWindowTitle("轻块桌面 · 便签与任务")
        self.setObjectName("root")
        self.setWindowIcon(workbench.controller.icon)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setStyleSheet(STYLE+"QListWidget{background:#192330;border:0;border-radius:10px;} QListWidget::item{padding:12px;} QListWidget::item:selected{background:#345347;} QPlainTextEdit{background:#18222f;border:1px solid #344253;border-radius:10px;padding:10px;}")
        self.resize(980, 650)
        outer = QVBoxLayout(self)
        outer.setContentsMargins(24, 22, 24, 22)
        outer.addWidget(label("留住想法，也完成它。", "title"))
        row = QHBoxLayout()
        self.search = QLineEdit()
        self.search.setPlaceholderText("搜索标题或正文…")
        self.filter = QComboBox()
        for text, value in (("全部", -1), ("待办", 0), ("已完成", 1), ("归档", -2)):
            self.filter.addItem(text, value)
        row.addWidget(self.search, 1)
        row.addWidget(self.filter)
        row.addWidget(button("＋ 新建便签", self.create, "primary"))
        row.addWidget(button("归档 / 还原", self.archive))
        outer.addLayout(row)
        body = QHBoxLayout()
        self.list = QListWidget()
        self.list.setFixedWidth(260)
        self.list.currentItemChanged.connect(self.select)
        body.addWidget(self.list)
        editor = QVBoxLayout()
        self.title = QLineEdit()
        self.title.setPlaceholderText("便签标题")
        self.title.setMaxLength(100)
        self.body = QPlainTextEdit()
        self.body.setPlaceholderText("写下想法、清单或下一步行动…")
        editor.addWidget(self.title)
        editor.addWidget(self.body, 1)
        options = QHBoxLayout()
        self.done = QCheckBox("已完成")
        self.pinned = QCheckBox("贴到桌面")
        self.color = QComboBox()
        for name, value in (("薄荷", "mint"), ("海蓝", "blue"), ("暖砂", "amber"), ("蔷薇", "rose")):
            self.color.addItem(name, value)
        options.addWidget(self.done)
        options.addWidget(self.pinned)
        options.addStretch()
        options.addWidget(self.color)
        editor.addLayout(options)
        reminder = QHBoxLayout()
        self.has_due = QCheckBox("到期提醒")
        self.due = QDateTimeEdit(QDateTime.currentDateTime().addSecs(3600))
        self.due.setCalendarPopup(True)
        self.due.setDisplayFormat("yyyy-MM-dd HH:mm")
        reminder.addWidget(self.has_due)
        reminder.addWidget(self.due)
        editor.addLayout(reminder)
        editor.addWidget(label("自动保存到本机。设置到期提醒或贴到桌面后，关闭设置窗口仍会保留后台。", "muted", True))
        body.addLayout(editor, 1)
        outer.addLayout(body, 1)
        self.save_timer = QTimer(self)
        self.save_timer.setSingleShot(True)
        self.save_timer.setInterval(350)
        self.save_timer.timeout.connect(self.save)
        for signal in (self.title.textChanged, self.body.textChanged, self.done.toggled, self.has_due.toggled, self.due.dateTimeChanged, self.color.currentIndexChanged):
            signal.connect(lambda *_: self.schedule_save())
        self.pinned.toggled.connect(self.pin)
        self.search.textChanged.connect(self.refresh)
        self.filter.currentIndexChanged.connect(self.refresh)
        self.engine.changed.connect(self.refresh)
        self.engine.before_change.connect(self.save)
        self.refresh()

    def create(self):
        self.save()
        self.selected = self.engine.create()
        self.refresh()

    def refresh(self, *_):
        self.save()
        self.updating = True
        self.list.clear()
        query = "%"+self.search.text()+"%"
        mode = self.filter.currentData()
        rows = self.engine.db.query("SELECT id,title,substr(body,1,80) summary,done FROM notes WHERE (title LIKE ? OR body LIKE ?) AND archived=? AND (?<0 OR done=?) ORDER BY updated DESC LIMIT 500",
                                   (query, query, int(mode == -2), mode, mode))
        for row in rows:
            item = QListWidgetItem(("已完成 · " if row["done"] else "")+row["title"]+"\n"+row["summary"].replace("\n", " ")[:55])
            item.setData(Qt.ItemDataRole.UserRole, row["id"])
            self.list.addItem(item)
            if row["id"] == self.selected:
                self.list.setCurrentItem(item)
        if not self.list.currentItem() and self.list.count():
            self.list.setCurrentRow(0)
        self.updating = False
        self.select(self.list.currentItem())

    def select(self, item, previous=None):
        if self.updating:
            return
        self.save()
        self.selected = item.data(Qt.ItemDataRole.UserRole) if item else None
        self.loaded_id = self.selected
        self.dirty = False
        row = self.engine.get(self.selected) if self.selected else None
        self.updating = True
        for widget in (self.title, self.body, self.done, self.pinned, self.has_due, self.due, self.color):
            widget.setEnabled(row is not None)
        self.title.setText(row["title"] if row else "")
        self.body.setPlainText(row["body"] if row else "")
        if row:
            self.done.setChecked(bool(row["done"]))
            self.pinned.setChecked(bool(row["pinned"]))
            self.has_due.setChecked(bool(row["due"]))
            if row["due"]:
                self.due.setDateTime(QDateTime.fromString(row["due"], Qt.DateFormat.ISODateWithMs).toLocalTime())
            self.color.setCurrentIndex(max(0, self.color.findData(row["color"])))
        self.updating = False

    def schedule_save(self):
        if not self.updating:
            self.dirty = True
            self.save_timer.start()

    def save(self):
        if self.updating or not self.loaded_id or not self.dirty:
            return
        self.save_timer.stop()
        due = self.due.dateTime().toUTC().toString(Qt.DateFormat.ISODateWithMs) if self.has_due.isChecked() else None
        self.engine.save(self.loaded_id, self.title.text(), self.body.toPlainText(), self.color.currentData(), self.done.isChecked(), due)
        self.dirty = False
        item = self.list.currentItem()
        if item and item.data(Qt.ItemDataRole.UserRole) == self.selected:
            item.setText(("已完成 · " if self.done.isChecked() else "")+self.title.text()+"\n"+self.body.toPlainText().replace("\n", " ")[:55])

    def pin(self, value):
        if self.selected and not self.updating:
            self.save()
            self.engine.pin(self.selected, value)

    def archive(self):
        if self.selected:
            self.engine.archive(self.selected, self.filter.currentData() != -2)

    def closeEvent(self, event):
        self.save()
        event.accept()
