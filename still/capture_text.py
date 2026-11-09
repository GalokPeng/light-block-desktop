"""Inline text input and formatting shared by the two capture surfaces."""

from PySide6.QtCore import QEvent, QPointF, QRectF, Qt, Signal
from PySide6.QtGui import QColor, QFont, QFontMetricsF, QIcon, QKeySequence, QPen, QPixmap
from PySide6.QtWidgets import (QApplication, QComboBox, QFontComboBox, QFrame, QHBoxLayout,
                             QPlainTextEdit, QToolButton, QWidget)

from .i18n import tr

TEXT_SIZES = (8, 10, 12, 14, 16, 18, 20, 22, 24, 28, 32, 36, 40, 48, 56, 64, 80, 96, 120, 160)


def annotation_font(operation, scale=1):
    font = QFont(operation.get("font_family", "Microsoft YaHei UI"))
    font.setPixelSize(max(1, round(operation.get("font_size", 20) * scale)))
    font.setBold(operation.get("bold", False))
    font.setItalic(operation.get("italic", False))
    font.setUnderline(operation.get("underline", False))
    return font


class AnnotationHistory:
    def __init__(self):
        self.operations = []
        self.past = []
        self.future = []

    def change(self, operation, index=None):
        index = len(self.operations) if index is None else index
        previous = self.operations[index] if index < len(self.operations) else None
        if previous == operation:
            return False
        self.past.append((index, previous, operation))
        self.future.clear()
        self.apply(index, previous, operation)
        return True

    def apply(self, index, before, after):
        self.operations[index:index + (before is not None)] = [] if after is None else [after]

    def undo(self):
        if self.past:
            change = self.past.pop()
            self.future.append(change)
            index, before, after = change
            self.apply(index, after, before)
            return True
        return False

    def redo(self):
        if self.future:
            change = self.future.pop()
            self.past.append(change)
            self.apply(*change)
            return True
        return False

    def clear(self):
        self.operations.clear()
        self.past.clear()
        self.future.clear()


class TextStyleBar(QFrame):
    changed = Signal()

    def __init__(self, colors):
        super().__init__()
        self.setObjectName("captureStrip")
        self.loading = False
        row = QHBoxLayout(self)
        row.setContentsMargins(8, 4, 8, 4)
        row.setSpacing(6)
        self.family = QFontComboBox()
        self.family.setEditable(False)
        self.family.setCurrentFont(QApplication.font())
        self.family.setFixedWidth(156)
        self.size = QComboBox()
        self.size.setEditable(False)
        for size in TEXT_SIZES:
            self.size.addItem(f"{size} px", size)
        self.size.setCurrentIndex(self.size.findData(20))
        self.color = QComboBox()
        self.color.setEditable(False)
        for value, name in colors:
            swatch = QPixmap(13, 13)
            swatch.fill(QColor(value))
            self.color.addItem(QIcon(swatch), name, value)
        for widget, name in ((self.family, tr("文字字体")), (self.size, tr("文字大小")), (self.color, tr("文字颜色"))):
            widget.setAccessibleName(name)
            widget.setToolTip(name)
            row.addWidget(widget)
        self.effects = {}
        for name, mark, description in (("bold", "B", tr("加粗")), ("italic", "I", tr("斜体")),
                                        ("underline", "U", tr("下划线"))):
            button = QToolButton()
            button.setText(mark)
            button.setToolTip(description)
            button.setAccessibleName(description)
            button.setCheckable(True)
            button.setFixedSize(25, 25)
            button.setFocusPolicy(Qt.FocusPolicy.NoFocus)
            button.setStyleSheet("QToolButton { color:#eff7f9;background:#273241;border:0;border-radius:5px; }"
                                 "QToolButton:checked { background:#356baf; }")
            font = annotation_font({name: True, "font_size": 15, "font_family": "Segoe UI"})
            button.setFont(font)
            button.toggled.connect(self.notify)
            self.effects[name] = button
            row.addWidget(button)
        row.addStretch()
        self.family.currentFontChanged.connect(self.notify)
        self.size.currentIndexChanged.connect(self.notify)
        self.color.currentIndexChanged.connect(self.notify)

    def notify(self):
        if not self.loading:
            self.changed.emit()

    def values(self):
        return {"font_family": self.family.currentFont().family(), "font_size": self.size.currentData(),
                "color": self.color.currentData(),
                **{name: widget.isChecked() for name, widget in self.effects.items()}}

    def load(self, operation):
        self.loading = True
        self.family.setCurrentFont(annotation_font(operation))
        self.size.setCurrentIndex(self.size.findData(operation["font_size"]))
        self.color.setCurrentIndex(self.color.findData(operation["color"]))
        for name, widget in self.effects.items():
            widget.setChecked(operation.get(name, False))
        self.loading = False


class TextInput(QPlainTextEdit):
    def __init__(self, host, finish, resize_on_wheel):
        super().__init__(host)
        self.finish = finish
        self.resize_on_wheel = resize_on_wheel
        self.setAccessibleName(tr("截图文字输入"))
        self.setPlaceholderText(tr("输入文字"))
        self.document().setDocumentMargin(0)
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.hide()

    def event(self, event):
        if event.type() == QEvent.Type.ShortcutOverride and (
            event.key() in (Qt.Key.Key_Escape, Qt.Key.Key_Return, Qt.Key.Key_Enter)
            or any(event.matches(key) for key in (QKeySequence.StandardKey.Copy, QKeySequence.StandardKey.Undo,
                                                QKeySequence.StandardKey.Redo))
        ):
            event.accept()
            return True
        return super().event(event)

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_Escape:
            self.finish()
        else:
            super().keyPressEvent(event)

    def wheelEvent(self, event):
        point = self.parentWidget().mapFromGlobal(event.globalPosition())
        if not self.resize_on_wheel(event, point):
            super().wheelEvent(event)


class InlineText:
    def __init__(self, host, bounds, transform, publish, attach_bar, colors):
        self.host = host
        self.bounds = bounds
        self.transform = transform
        self.publish = publish
        self.attach_bar = attach_bar
        self.colors = colors
        self.bar = None
        self.editor = None
        self.selected = self.hidden = None
        self.edit = self.drag = None
        self.resize_on_wheel = None

    def activate(self, enabled):
        if enabled and self.bar is None:
            self.bar = TextStyleBar(self.colors)
            self.attach_bar(self.bar)
            self.bar.changed.connect(self.apply_style)
            for combo in (self.bar.family, self.bar.size, self.bar.color):
                combo.activated.connect(self.focus_input)
        if self.bar:
            self.bar.setVisible(enabled)
        if not enabled:
            self.finish()
            self.release()
            self.selected = None

    def rect_for(self, operation, index):
        origin, scale = self.transform(index)
        start = origin + QPointF(*operation["points"][0]) * scale
        font = QFontMetricsF(annotation_font(operation, scale))
        lines = operation["text"].split("\n")
        width = max(font.horizontalAdvance(line) for line in lines)
        return QRectF(start.x(), start.y(), width + 4, font.lineSpacing() * len(lines) + 2)

    def hit(self, point):
        if not self.bounds().contains(point):
            return None
        for index in range(len(self.host.operations) - 1, -1, -1):
            operation = self.host.operations[index]
            if operation["kind"] == "text" and self.rect_for(operation, index).adjusted(-4, -4, 4, 4).contains(point):
                return index
        return None

    def begin(self, point, index=None):
        self.activate(True)
        if self.editor is None:
            self.editor = TextInput(self.host, self.finish, self.resize_on_wheel)
            self.editor.textChanged.connect(self.layout_editor)
        self.selected = self.hidden = index
        if index is None:
            origin, scale = self.transform(None)
            area = self.bounds()
            point = QPointF(max(area.left(), min(point.x(), area.right() - 108)),
                            max(area.top(), min(point.y(), area.bottom() - 24)))
            start = (point - origin) / scale
            self.edit = {"kind": "text", "points": [(start.x(), start.y())], **self.bar.values()}
        else:
            self.edit = dict(self.host.operations[index])
            self.bar.load(self.edit)
        self.editor.setPlainText(self.edit.get("text", ""))
        cursor = self.editor.textCursor()
        cursor.movePosition(cursor.MoveOperation.End)
        self.editor.setTextCursor(cursor)
        self.update_editor()
        self.editor.show()
        self.editor.raise_()
        self.editor.setFocus(Qt.FocusReason.MouseFocusReason)
        self.host.update()

    def update_editor(self):
        _, scale = self.transform(self.hidden)
        self.editor.setFont(annotation_font(self.edit, scale))
        self.editor.setStyleSheet("QPlainTextEdit { background:rgba(16,23,33,200);"
                                  f"color:{self.edit['color']};border:1px solid #4d9bff;border-radius:2px; }}")
        self.layout_editor()

    def focus_input(self):
        if self.edit is not None:
            self.editor.setFocus(Qt.FocusReason.OtherFocusReason)

    def layout_editor(self):
        if self.edit is None:
            return
        _, scale = self.transform(self.hidden)
        pixels = max(1, round(self.edit["font_size"]*scale))
        if self.editor.font().pixelSize() != pixels:
            self.editor.setFont(annotation_font(self.edit, scale))
        area = self.bounds().toRect()
        rect = self.rect_for(dict(self.edit, text=self.editor.toPlainText()), self.hidden).toRect()
        x = max(area.left(), rect.x() - 1)
        y = max(area.top(), rect.y() - 1)
        width = min(area.right() - x + 1, max(108, rect.width() + 8))
        height = min(area.bottom() - y + 1, max(24, rect.height() + 4))
        self.editor.setGeometry(x, y, width, height)

    def finish(self):
        if self.edit is None:
            return False
        index = self.hidden
        operation = dict(self.edit, text=self.editor.toPlainText())
        self.edit = self.hidden = None
        self.editor.hide()
        if operation["text"].strip():
            self.selected = self.publish(operation, index)
        elif index is not None:
            self.publish(None, index)
            self.selected = None
        self.host.setFocus()
        self.host.update()
        return True

    def apply_style(self):
        values = self.bar.values()
        if self.edit is not None:
            self.edit.update(values)
            self.update_editor()
        elif self.selected is not None:
            index = self.selected
            self.publish(dict(self.host.operations[index], **values), index)

    def press(self, point, can_create):
        finished = self.finish()
        index = self.hit(point)
        if index is not None:
            self.selected = index
            self.drag = (index, point, self.host.operations[index])
            if can_create:
                self.bar.load(self.host.operations[index])
            self.host.update()
            return True
        if finished:
            return True
        self.selected = None
        self.host.update()
        if can_create and self.bounds().contains(point):
            self.begin(point)
            return True
        return False

    def move(self, point):
        if self.drag is None:
            return False
        index, anchor, operation = self.drag
        delta = point - anchor
        if self.hidden is None and delta.manhattanLength() < QApplication.startDragDistance():
            return True
        origin, scale = self.transform(index)
        start = origin + QPointF(*operation["points"][0]) * scale + delta
        area = self.bounds()
        start.setX(max(area.left(), min(start.x(), area.right() - 8)))
        start.setY(max(area.top(), min(start.y(), area.bottom() - 8)))
        position = (start - origin) / scale
        self.hidden = index
        self.moving = dict(operation, points=[(position.x(), position.y())])
        self.host.update()
        return True

    def release(self):
        if self.drag is None:
            return False
        self.drag = None
        index, self.hidden = self.hidden, None
        if index is not None:
            self.publish(self.moving, index)
        self.host.update()
        return True

    def double_click(self, point):
        index = self.hit(point)
        if index is None:
            return False
        self.finish()
        self.drag = None
        self.begin(point, index)
        return True

    def paint(self, painter, paint_operation):
        if self.drag is not None and self.hidden is not None:
            operation = self.moving
            origin, scale = self.transform(self.hidden)
            painter.save()
            painter.translate(origin)
            painter.scale(scale, scale)
            paint_operation(painter, operation)
            painter.restore()
            rect = self.rect_for(operation, self.hidden)
        elif self.selected is not None and self.edit is None:
            rect = self.rect_for(self.host.operations[self.selected], self.selected)
        else:
            return
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.setPen(QPen(QColor("#4d9bff"), 1, Qt.PenStyle.DashLine))
        painter.drawRect(rect.adjusted(-2, -2, 2, 2))
