from __future__ import annotations

from datetime import datetime, timezone
import uuid

from PySide6.QtCore import QDateTime, QObject, QPoint, QRect, QRectF, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QCursor, QFont, QPainter
from PySide6.QtWidgets import (QApplication, QCheckBox, QColorDialog, QComboBox, QDateTimeEdit, QHBoxLayout, QLineEdit,
    QListWidget, QListWidgetItem, QMenu, QPlainTextEdit, QVBoxLayout, QWidget)

from .i18n import tr
from .desktop_windows import current_screen, desktop_layer, desktop_owner, DesktopWatcher, live_screen, paint_desktop_panel
from .productivity_model import utc_now

NOTE_COLORS = {"mint": "#a6e4ca", "blue": "#9ac8ff", "amber": "#f5d18e", "rose": "#efb5c5"}


def note_color(value):
    color = QColor(NOTE_COLORS.get(value, value or ""))
    return color if color.isValid() else QColor(NOTE_COLORS["mint"])


class NotePin(QWidget):
    def __init__(self, engine, row):
        super().__init__(None, Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint | Qt.WindowType.WindowDoesNotAcceptFocus)
        self.engine, self.ident = engine, row["id"]
        self.row = row
        self.drag = None
        self.resizing = None
        self.setWindowTitle(tr("轻块桌面 · 桌面便签"))
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setMinimumSize(220, 150)
        self.setMouseTracking(True)
        self.body_view = QPlainTextEdit(self)
        self.body_view.setReadOnly(True)
        self.body_view.viewport().setCursor(Qt.CursorShape.ArrowCursor)
        self.body_view.setFrameShape(QPlainTextEdit.Shape.NoFrame)
        self.body_view.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.body_view.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.body_view.setStyleSheet("QPlainTextEdit { background:transparent; border:0; color:#edf3fa; font:10pt 'Microsoft YaHei UI'; selection-background-color:#476c7b; }")
        self.body_view.document().setDocumentMargin(0)
        self.body_view.viewport().setAutoFillBackground(False)
        self.body_view.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.body_view.customContextMenuRequested.connect(lambda point: self.show_menu(self.body_view.mapToGlobal(point)))
        width = row["pin_w"] if "pin_w" in row.keys() and type(row["pin_w"]) is int else 290
        height = row["pin_h"] if "pin_h" in row.keys() and type(row["pin_h"]) is int else 240
        self.resize(max(220, min(1400, width)), max(150, min(1400, height)))
        self.set_note(row)
        self.layer_timer = QTimer(self)
        self.layer_timer.setSingleShot(True)
        self.layer_timer.timeout.connect(self.position_layer)
        self.setToolTip(tr("拖动标题移动 · 拖动边缘调整大小 · 滚轮浏览内容 · 右键管理"))

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
        if self.resizing is None and self.drag is None:
            self.unsetCursor()
        super().leaveEvent(event)

    def set_note(self, row):
        self.row = row
        if self.body_view.toPlainText() != row["body"]:
            scroll = self.body_view.verticalScrollBar().value()
            self.body_view.setPlainText(row["body"])
            self.body_view.verticalScrollBar().setValue(scroll)
        self.update()

    def resizeEvent(self, event):
        self.body_view.setGeometry(18, 64, max(1, self.width()-36), max(1, self.height()-83))
        super().resizeEvent(event)

    def paintEvent(self, event):
        row = self.row
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        accent = note_color(row["color"])
        paint_desktop_panel(painter, QRectF(self.rect()), 190, accent)
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(accent))
        painter.drawEllipse(QPoint(23, 29), 4, 4)
        painter.setPen(QColor("#f4f7fc"))
        painter.setFont(QFont("Microsoft YaHei UI", 12, QFont.Weight.DemiBold))
        title = (tr("已完成 · ") if row["done"] else "") + row["title"]
        title = painter.fontMetrics().elidedText(title, Qt.TextElideMode.ElideRight, self.width() - 53)
        painter.drawText(34, 12, self.width()-53, 34, Qt.AlignmentFlag.AlignVCenter, title)
        painter.setPen(QColor(210, 226, 244, 48))
        painter.drawLine(18, 52, self.width()-18, 52)
        painter.setPen(QColor(232, 241, 250, 100))
        for offset in (0, 5):
            painter.drawLine(self.width()-20+offset, self.height()-8, self.width()-8, self.height()-20+offset)

    def place(self, position):
        desired = QRect(position, self.size())
        screen = QApplication.screenAt(desired.center())
        if not live_screen(screen):
            screen = current_screen(next((screen for screen in QApplication.screens()
                if live_screen(screen) and screen.availableGeometry().intersects(desired)), None))
        if screen is not None:
            area = screen.availableGeometry()
            self.resize(min(self.width(), max(self.minimumWidth(), area.width())),
                        min(self.height(), max(self.minimumHeight(), area.height())))
            position = QPoint(max(area.left(), min(position.x(), area.right()-self.width()+1)),
                              max(area.top(), min(position.y(), area.bottom()-self.height()+1)))
        self.move(position)

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            edge = self.edge_at(event.position().toPoint())
            if edge:
                self.resizing = (edge, event.globalPosition().toPoint(), self.geometry())
            else:
                self.drag = (event.globalPosition().toPoint()-self.pos(), self.pos())

    def edge_at(self, point):
        return ((1 if point.x() < 8 else 2 if point.x() >= self.width()-8 else 0) |
                (4 if point.y() < 8 else 8 if point.y() >= self.height()-8 else 0))

    def mouseMoveEvent(self, event):
        if self.resizing is not None:
            edge, start, bounds = self.resizing
            delta = event.globalPosition().toPoint()-start
            rect = QRect(bounds)
            if edge & 1: rect.setLeft(min(bounds.left()+delta.x(), bounds.right()-self.minimumWidth()+1))
            if edge & 2: rect.setRight(max(bounds.right()+delta.x(), bounds.left()+self.minimumWidth()-1))
            if edge & 4: rect.setTop(min(bounds.top()+delta.y(), bounds.bottom()-self.minimumHeight()+1))
            if edge & 8: rect.setBottom(max(bounds.bottom()+delta.y(), bounds.top()+self.minimumHeight()-1))
            self.setGeometry(rect)
        elif self.drag is not None:
            self.move(event.globalPosition().toPoint()-self.drag[0])
        else:
            edge = self.edge_at(event.position().toPoint())
            shape = (Qt.CursorShape.SizeFDiagCursor if edge in (5, 10) else
                     Qt.CursorShape.SizeBDiagCursor if edge in (6, 9) else
                     Qt.CursorShape.SizeHorCursor if edge & 3 else
                     Qt.CursorShape.SizeVerCursor if edge else Qt.CursorShape.ArrowCursor)
            self.setCursor(shape)

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton and self.resizing is not None:
            self.resizing = None
            self.place(self.pos())
            self.save_geometry()
        if event.button() == Qt.MouseButton.LeftButton and self.drag is not None:
            _, start = self.drag
            self.drag = None
            if self.pos() != start:
                self.place(self.pos())
                self.save_geometry()

    def save_geometry(self):
        if self.engine is not None:
            self.engine.db.execute("UPDATE notes SET pin_x=?,pin_y=?,pin_w=?,pin_h=? WHERE id=?",
                                   (self.x(), self.y(), self.width(), self.height(), self.ident))

    def mouseDoubleClickEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.engine.edit(self.ident)

    def contextMenuEvent(self, event):
        self.show_menu(event.globalPos())

    def show_menu(self, position):
        menu = QMenu(self)
        menu.addAction(tr("编辑便签"), lambda: self.engine.edit(self.ident))
        menu.addAction(tr("复制内容"), lambda: QApplication.clipboard().setText(self.row["body"]))
        menu.addAction(tr("便签颜色…"), self.choose_color)
        menu.addAction(tr("标记完成 / 待办"), lambda: self.engine.toggle_done(self.ident))
        menu.addAction(tr("取消贴到桌面"), lambda: self.engine.pin(self.ident, False))
        menu.exec(position)

    def choose_color(self):
        color = QColorDialog.getColor(note_color(self.row["color"]), self, tr("便签颜色"))
        if color.isValid():
            self.engine.set_color(self.ident, color.name())


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

    def create(self, *, pinned=False):
        self.before_change.emit()
        ident = uuid.uuid4().hex
        self.db.execute("INSERT INTO notes(id,title,body,updated,pinned) VALUES(?,?,?,?,?)",
                        (ident, tr("新的便签"), "", utc_now(), int(pinned)))
        if pinned:
            self.refresh_pins()
            self.schedule()
        self.changed.emit()
        return ident

    def save(self, ident, title, body, color, done, due):
        previous = self.get(ident)
        notified = 0 if previous is None or previous["due"] != due else previous["notified"]
        self.db.execute("UPDATE notes SET title=?,body=?,color=?,done=?,due=?,notified=?,updated=? WHERE id=?",
                        (title.strip()[:100] or tr("未命名便签"), body, color, int(done), due, notified, utc_now(), ident))
        self.schedule()
        if ident in self.pins:
            self.pins[ident].set_note(self.get(ident))

    def toggle_done(self, ident):
        self.before_change.emit()
        self.db.execute("UPDATE notes SET done=1-done,updated=? WHERE id=?", (utc_now(), ident))
        self.schedule()
        self.refresh_pins()
        self.changed.emit()

    def set_color(self, ident, value):
        self.before_change.emit()
        self.db.execute("UPDATE notes SET color=?,updated=? WHERE id=?", (note_color(value).name(), utc_now(), ident))
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
                screen = current_screen()
                area = screen.availableGeometry() if screen is not None else pin.geometry()
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
            self.workbench.controller.tray.showMessage(tr("轻块桌面 · 任务提醒"), row["title"]+"\n"+row["body"][:100])
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
        self.setWindowTitle(tr("轻块桌面 · 便签与任务"))
        self.setObjectName("root")
        self.setWindowIcon(workbench.controller.icon)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setStyleSheet(STYLE+"QListWidget{background:#192330;border:0;border-radius:10px;} QListWidget::item{padding:12px;} QListWidget::item:selected{background:#345347;} QPlainTextEdit{background:#18222f;border:1px solid #344253;border-radius:10px;padding:10px;}")
        self.resize(980, 650)
        outer = QVBoxLayout(self)
        outer.setContentsMargins(24, 22, 24, 22)
        outer.addWidget(label(tr("留住想法，也完成它。"), "title"))
        row = QHBoxLayout()
        self.search = QLineEdit()
        self.search.setPlaceholderText(tr("搜索标题或正文…"))
        self.filter = QComboBox()
        for text, value in ((tr("全部"), -1), (tr("待办"), 0), (tr("已完成"), 1), (tr("归档"), -2)):
            self.filter.addItem(text, value)
        row.addWidget(self.search, 1)
        row.addWidget(self.filter)
        row.addWidget(button(tr("＋ 新建便签"), self.create, "primary"))
        row.addWidget(button(tr("归档 / 还原"), self.archive))
        outer.addLayout(row)
        body = QHBoxLayout()
        self.list = QListWidget()
        self.list.setFixedWidth(260)
        self.list.currentItemChanged.connect(self.select)
        body.addWidget(self.list)
        editor = QVBoxLayout()
        self.title = QLineEdit()
        self.title.setPlaceholderText(tr("便签标题"))
        self.title.setMaxLength(100)
        self.body = QPlainTextEdit()
        self.body.setPlaceholderText(tr("写下想法、清单或下一步行动…"))
        editor.addWidget(self.title)
        editor.addWidget(self.body, 1)
        options = QHBoxLayout()
        self.done = QCheckBox(tr("已完成"))
        self.pinned = QCheckBox(tr("贴到桌面"))
        self.color = QComboBox()
        for name, value in ((tr("薄荷"), "mint"), (tr("海蓝"), "blue"), (tr("暖砂"), "amber"), (tr("蔷薇"), "rose")):
            self.color.addItem(name, value)
        options.addWidget(self.done)
        options.addWidget(self.pinned)
        options.addStretch()
        options.addWidget(self.color)
        options.addWidget(button(tr("调色板"), self.choose_color))
        editor.addLayout(options)
        reminder = QHBoxLayout()
        self.has_due = QCheckBox(tr("到期提醒"))
        self.due = QDateTimeEdit(QDateTime.currentDateTime().addSecs(3600))
        self.due.setCalendarPopup(True)
        self.due.setDisplayFormat("yyyy-MM-dd HH:mm")
        reminder.addWidget(self.has_due)
        reminder.addWidget(self.due)
        editor.addLayout(reminder)
        editor.addWidget(label(tr("自动保存到本机。设置到期提醒或贴到桌面后，关闭设置窗口仍会保留后台。"), "muted", True))
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
            item = QListWidgetItem((tr("已完成 · ") if row["done"] else "")+row["title"]+"\n"+row["summary"].replace("\n", " ")[:55])
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
            self.select_color(row["color"])
        self.updating = False

    def select_color(self, value):
        index = self.color.findData(value)
        if index < 0:
            index = 4
            if self.color.count() == index:
                self.color.addItem("", value)
            self.color.setItemText(index, tr("自定义颜色")+" "+note_color(value).name())
            self.color.setItemData(index, value)
        self.color.setCurrentIndex(index)

    def choose_color(self):
        if self.loaded_id:
            color = QColorDialog.getColor(note_color(self.color.currentData()), self, tr("便签颜色"))
            if color.isValid():
                self.select_color(color.name())
                self.schedule_save()

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
            item.setText((tr("已完成 · ") if self.done.isChecked() else "")+self.title.text()+"\n"+self.body.toPlainText().replace("\n", " ")[:55])

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
