"""Region capture, non-destructive annotation and pinned image references."""
from __future__ import annotations

from bisect import bisect_left, bisect_right
import math
from pathlib import Path

from PySide6.QtCore import QPointF, QRect, QRectF, Qt, Signal
from PySide6.QtGui import QColor, QCursor, QFont, QImage, QKeySequence, QPainter, QPen, QPolygonF, QShortcut
from PySide6.QtWidgets import (QApplication, QComboBox, QFileDialog, QHBoxLayout, QInputDialog, QMenu,
    QSizeGrip, QVBoxLayout, QWidget)


from .i18n import tr
from .capture_text import TEXT_SIZES, AnnotationHistory, InlineText, annotation_font


DRAW_TOOLS = (("pen", tr("画笔")), ("highlight", tr("高亮")), ("arrow", tr("箭头")), ("line", tr("直线")),
              ("rect", tr("矩形")), ("text", tr("文字")), ("number", tr("序号")), ("mosaic", tr("马赛克")))
DRAW_COLORS = (("#ff6868", tr("珊瑚红")), ("#a6e4ca", tr("薄荷绿")), ("#ffd382", tr("暖金")),
               ("#68adff", tr("海盐蓝")), ("#ffffff", tr("白色")), ("#151515", tr("黑色")))
STROKE_SIZES = (1, 2, 3, 4, 6, 8, 10, 12, 16, 20, 24, 32, 36)
NUMBER_SIZES = (12, 16, 20, 22, 26, 32, 40, 48, 60, 72, 90, 120, 160)


def next_number(operations):
    for operation in reversed(operations):
        if operation["kind"] == "number":
            return operation["number"] + 1
        if operation["kind"] == "sequence":
            return operation["number"]
    return 1


def choose_number(parent, current):
    choices = [str(number) for number in range(1, 10000)]
    value, accepted = QInputDialog.getItem(parent, tr("重计序号"), tr("下一次标记从几开始"), choices,
                                           min(current-1, len(choices)-1), False)
    return int(value) if accepted else None


def arrow_tips(a, b, width):
    angle = math.atan2(b.y() - a.y(), b.x() - a.x())
    distance = min(math.hypot(b.x() - a.x(), b.y() - a.y()), width * 4 + 8)
    return [b - QPointF(distance * math.cos(angle + delta), distance * math.sin(angle + delta))
            for delta in (-.55, .55)]


def number_badge(operation, center):
    scale = operation.get("scale", 1)
    height = operation.get("badge_size", max(22 * scale, operation.get("width", 4) * 5))
    width = max(height, len(str(operation["number"])) * height * .37 + 8 * scale)
    return QRectF(center.x() - width / 2, center.y() - height / 2, width, height)


def near_segment(point, start, end, radius):
    delta = end - start
    offset = point - start
    length = QPointF.dotProduct(delta, delta)
    if length:
        progress = max(0., min(1., QPointF.dotProduct(delta, offset) / length))
        offset -= delta * progress
    return QPointF.dotProduct(offset, offset) <= radius * radius


def stroke_hit(operation, point, margin):
    width = operation.get("width", 4)
    points = [QPointF(*point) for point in operation["points"]]
    kind = operation["kind"]
    if kind == "number":
        return number_badge(operation, points[0]).adjusted(-margin, -margin, margin, margin).contains(point)
    radius = max(margin, width * (1.5 if kind == "highlight" else .5) + margin / 2)
    if kind == "rect":
        rect = QRectF(points[0], points[-1]).normalized()
        outer = rect.adjusted(-radius, -radius, radius, radius)
        inner = rect.adjusted(radius, radius, -radius, -radius)
        return outer.contains(point) and (inner.isEmpty() or not inner.contains(point))
    if kind == "arrow" and len(points) > 1:
        if any(near_segment(point, points[-1], tip, radius) for tip in arrow_tips(points[0], points[-1], width)):
            return True
    if len(points) == 1:
        return near_segment(point, points[0], points[0], radius)
    return any(near_segment(point, start, end, radius) for start, end in zip(points, points[1:]))


class AnnotationSizer:
    def __init__(self, inline, enabled=True):
        self.inline = inline
        self.enabled = enabled
        self.target = None
        self.delta = 0

    def hit(self, point):
        inline = self.inline
        if inline.edit is not None and QRectF(inline.editor.geometry()).contains(point):
            return "typing"
        for index in range(len(inline.host.operations) - 1, -1, -1):
            op = inline.host.operations[index]
            kind = op["kind"]
            if index == inline.hidden or not op.get("points"):
                continue
            if kind == "text":
                if inline.rect_for(op, index).adjusted(-4, -4, 4, 4).contains(point):
                    return index
            elif kind in {"pen", "highlight", "rect", "line", "arrow", "number"}:
                origin, scale = inline.transform(index)
                if scale > 0 and stroke_hit(op, (point - origin) / scale, 6 / scale):
                    return index
        return None

    def adjust(self, event, point):
        inline = self.inline
        delta = event.angleDelta().y() or event.pixelDelta().y() * 4
        if not self.enabled or not delta or inline.drag is not None or not inline.bounds().contains(point):
            self.target, self.delta = None, 0
            return False
        target = self.hit(point)
        if target != self.target:
            self.target, self.delta = target, 0
        if target is None:
            return False
        event.accept()
        self.delta += delta
        steps = math.trunc(self.delta / 120)
        self.delta -= steps * 120
        if not steps:
            return True
        if target == "typing":
            op = inline.edit
        else:
            op = inline.host.operations[target]
        if op["kind"] == "text":
            name, value, sizes = "font_size", op["font_size"], TEXT_SIZES
        elif op["kind"] == "number":
            name, value, sizes = "badge_size", number_badge(op, QPointF()).height(), NUMBER_SIZES
        else:
            name, value, sizes = "width", op.get("width", 4), STROKE_SIZES
        index = bisect_right(sizes, value) + steps - 1 if steps > 0 else bisect_left(sizes, value) + steps
        size = sizes[max(0, min(index, len(sizes) - 1))]
        if size == value:
            return True
        if target == "typing":
            inline.bar.size.setCurrentIndex(inline.bar.size.findData(size))
        else:
            updated = dict(op, **{name: size})
            inline.publish(updated, target)
            if inline.selected == target and name == "font_size" and inline.bar:
                inline.bar.load(updated)
        return True


def paint_operation(painter, operation):
    kind = operation["kind"]
    color = QColor(operation.get("color", "#ff6868"))
    width = operation.get("width", 4)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    if kind == "highlight":
        color.setAlpha(100)
        width *= 3
    painter.setPen(QPen(color, width, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin))
    painter.setBrush(Qt.BrushStyle.NoBrush)
    points = [QPointF(*point) for point in operation.get("points", [])]
    if kind in ("pen", "highlight") and points:
        if len(points) == 1 or all(point == points[0] for point in points):
            painter.drawPoint(points[0])
        else:
            painter.drawPolyline(QPolygonF(points))
    elif kind in ("rect", "arrow", "line") and len(points) >= 2:
        a, b = points[0], points[-1]
        if kind == "rect":
            painter.drawRect(QRectF(a, b).normalized())
        else:
            painter.drawLine(a, b)
            if kind == "arrow" and a != b:
                for tip in arrow_tips(a, b, width):
                    painter.drawLine(b, tip)
    elif kind == "text" and points:
        painter.setFont(annotation_font(operation))
        painter.drawText(QRectF(points[0].x(), points[0].y(), 16384, 16384),
                         Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop, operation.get("text", ""))
    elif kind == "number" and points:
        number = str(operation["number"])
        badge = number_badge(operation, points[0])
        height = badge.height()
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(color)
        painter.drawRoundedRect(badge, height/2, height/2)
        font = QFont("Segoe UI")
        font.setBold(True)
        font.setPixelSize(max(1, round(height*.56)))
        painter.setFont(font)
        luminance = color.red()*.299 + color.green()*.587 + color.blue()*.114
        painter.setPen(QColor("#152028" if luminance > 150 else "#ffffff"))
        painter.drawText(badge, Qt.AlignmentFlag.AlignCenter, number)


def render_operations(source, operations):
    image = source.copy()
    for operation in operations:
        if operation["kind"] == "sequence":
            continue
        if operation["kind"] == "crop":
            rect = QRect(*operation["rect"]).intersected(image.rect())
            if rect.width() >= 2 and rect.height() >= 2:
                image = image.copy(rect)
        elif operation["kind"] == "mosaic":
            rect = QRect(*operation["rect"]).intersected(image.rect())
            if rect.isEmpty():
                continue
            block = image.copy(rect).scaled(max(1, rect.width()//14), max(1, rect.height()//14),
                    Qt.AspectRatioMode.IgnoreAspectRatio, Qt.TransformationMode.FastTransformation)
            painter = QPainter(image)
            painter.drawImage(rect, block)
            painter.end()
        else:
            painter = QPainter(image)
            paint_operation(painter, operation)
            painter.end()
    return image


class AnnotationCanvas(QWidget):
    changed = Signal()

    def __init__(self, image, attach_text_bar=lambda bar: None, wheel_sizes=True):
        super().__init__()
        self.source = image.copy()
        self.image = image.copy()
        self.history = AnnotationHistory()
        self.operations = self.history.operations
        self.inline = InlineText(self, lambda: self.image_area, self.text_transform, self.publish,
                                 attach_text_bar, DRAW_COLORS)
        self.sizing = AnnotationSizer(self.inline, wheel_sizes)
        self.inline.resize_on_wheel = self.sizing.adjust
        self.hidden_image = None
        self.hidden_index = None
        self.tool = "pen"
        self.color = "#ff6868"
        self.stroke_width = 4
        self.current = None
        self.image_area = QRectF()
        self.setMinimumSize(420, 300)
        self.setCursor(Qt.CursorShape.CrossCursor)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#101721"))
        scale = min((self.width()-30)/self.image.width(), (self.height()-30)/self.image.height(), 1.5)
        width, height = self.image.width()*scale, self.image.height()*scale
        self.image_area = QRectF((self.width()-width)/2, (self.height()-height)/2, width, height)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        hidden = self.inline.hidden
        if hidden is None:
            self.hidden_image = self.hidden_index = None
            background = self.image
        else:
            if self.hidden_index != hidden:
                self.hidden_image = render_operations(self.source, self.operations[:hidden] + self.operations[hidden+1:])
                self.hidden_index = hidden
            background = self.hidden_image
        painter.drawImage(self.image_area, background)
        painter.save()
        painter.setClipRect(self.image_area)
        self.inline.paint(painter, paint_operation)
        painter.restore()
        self.inline.layout_editor()
        if self.current:
            painter.save()
            painter.translate(self.image_area.topLeft())
            painter.scale(scale, scale)
            if self.current["kind"] in ("crop", "mosaic"):
                points = self.current["points"]
                painter.setPen(QPen(QColor("#a6e4ca"), 2/scale, Qt.PenStyle.DashLine))
                painter.drawRect(QRectF(QPointF(*points[0]), QPointF(*points[-1])).normalized())
            else:
                paint_operation(painter, self.current)
            painter.restore()

    def set_tool(self, tool):
        self.tool = tool
        self.inline.activate(tool == "text")
        self.setMouseTracking(tool == "text")
        self.setCursor(Qt.CursorShape.IBeamCursor if tool == "text" else Qt.CursorShape.CrossCursor)
        self.update()

    def text_transform(self, index):
        scale = self.image_area.width() / self.image.width()
        origin = self.image_area.topLeft()
        if index is not None:
            for operation in self.operations[index+1:]:
                if operation["kind"] == "crop":
                    x, y, _, _ = operation["rect"]
                    origin -= QPointF(x, y) * scale
        return origin, scale

    def coordinate(self, point):
        scale = self.image_area.width()/self.image.width()
        if scale <= 0:
            return (0.0, 0.0)
        return (max(0, min(self.image.width()-1, (point.x()-self.image_area.x())/scale)),
                max(0, min(self.image.height()-1, (point.y()-self.image_area.y())/scale)))

    def mousePressEvent(self, event):
        if event.button() != Qt.MouseButton.LeftButton:
            return
        if self.tool == "text" and self.inline.press(event.position(), True):
            return
        if self.image_area.contains(event.position()):
            self.current = {"kind": self.tool, "color": self.color, "width": self.stroke_width,
                            "points": [self.coordinate(event.position())]}
            if self.tool == "number":
                self.current["number"] = next_number(self.operations)

    def mouseMoveEvent(self, event):
        if self.inline.move(event.position()):
            return
        if self.tool == "text":
            self.setCursor(Qt.CursorShape.SizeAllCursor if self.inline.hit(event.position()) is not None
                           else Qt.CursorShape.IBeamCursor)
        if self.current:
            point = self.coordinate(event.position())
            if self.tool in ("pen", "highlight"):
                self.current["points"].append(point)
            else:
                self.current["points"] = [self.current["points"][0], point]
            self.update()

    def mouseDoubleClickEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton and self.tool == "text":
            self.inline.double_click(event.position())

    def wheelEvent(self, event):
        if self.current or not self.sizing.adjust(event, event.position()):
            event.ignore()

    def mouseReleaseEvent(self, event):
        if self.inline.release() or not self.current:
            return
        operation = self.current
        self.current = None
        if operation["kind"] in ("crop", "mosaic"):
            points = operation["points"]
            if len(points) < 2:
                return
            rect = QRectF(QPointF(*points[0]), QPointF(*points[-1])).normalized().toRect().intersected(self.image.rect())
            if rect.width() < 3 or rect.height() < 3:
                return
            operation["rect"] = list(rect.getRect())
        self.publish(operation)

    def publish(self, operation, index=None):
        if self.history.change(operation, index):
            self.rebuild()
        return len(self.operations) - 1 if index is None else index

    def rebuild(self):
        self.hidden_image = self.hidden_index = None
        self.image = render_operations(self.source, self.operations)
        self.update()
        self.changed.emit()

    def undo(self):
        self.inline.finish()
        self.inline.release()
        self.inline.selected = None
        if self.history.undo():
            self.rebuild()

    def redo(self):
        self.inline.finish()
        self.inline.release()
        self.inline.selected = None
        if self.history.redo():
            self.rebuild()

    def result(self):
        self.inline.finish()
        return self.image


class CaptureEditor(QWidget):
    def __init__(self, workbench, image, wheel_sizes=True):
        super().__init__()
        from .ui import STYLE, button, label
        self.workbench = workbench
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setWindowTitle(tr("轻块桌面 · 截图工作室"))
        self.setObjectName("root")
        self.setWindowIcon(workbench.controller.icon)
        self.setStyleSheet(STYLE)
        self.resize(1080, 750)
        outer = QVBoxLayout(self)
        outer.setContentsMargins(20, 18, 20, 18)
        toolbar = QHBoxLayout()
        toolbar.addWidget(label(tr("截图工作室"), "section"))
        self.mode = QComboBox()
        for key, name in (*DRAW_TOOLS, ("crop", tr("裁剪"))):
            self.mode.addItem(name, key)
        toolbar.addWidget(self.mode)
        color = QComboBox()
        for value, name in DRAW_COLORS:
            color.addItem(name, value)
        toolbar.addWidget(color)
        self.stroke = QComboBox()
        for value, name in ((3, tr("细线")), (6, tr("中线")), (12, tr("粗线"))):
            self.stroke.addItem(name, value)
        toolbar.addWidget(self.stroke)
        toolbar.addStretch()
        outer.addLayout(toolbar)
        self.canvas = AnnotationCanvas(image, lambda bar: outer.insertWidget(1, bar), wheel_sizes)
        outer.addWidget(self.canvas, 1)
        self.mode.currentIndexChanged.connect(lambda: self.canvas.set_tool(self.mode.currentData()))
        color.currentIndexChanged.connect(lambda: setattr(self.canvas, "color", color.currentData()))
        self.stroke.currentIndexChanged.connect(lambda: setattr(self.canvas, "stroke_width", self.stroke.currentData()))
        bottom = QHBoxLayout()
        bottom.addWidget(button(tr("撤销"), self.canvas.undo))
        bottom.addWidget(button(tr("重做"), self.canvas.redo))
        bottom.addWidget(button(tr("序号重计"), self.reset_number))
        self.size_label = label("", "muted")
        bottom.addWidget(self.size_label)
        bottom.addStretch()
        bottom.addWidget(button(tr("复制"), self.copy))
        bottom.addWidget(button(tr("贴到屏幕"), lambda: workbench.pin(self.canvas.result())))
        bottom.addWidget(button(tr("保存 PNG…"), self.save, "primary"))
        outer.addLayout(bottom)
        self.canvas.changed.connect(self.update_size)
        self.update_size()
        QShortcut(QKeySequence.StandardKey.Undo, self, self.canvas.undo)
        QShortcut(QKeySequence.StandardKey.Redo, self, self.canvas.redo)
        QShortcut(QKeySequence.StandardKey.Copy, self, self.copy)
        QShortcut(QKeySequence.StandardKey.Save, self, self.save)

    def update_size(self):
        self.size_label.setText(f"{self.canvas.image.width()} × {self.canvas.image.height()} px")

    def reset_number(self):
        self.canvas.inline.finish()
        value = choose_number(self, next_number(self.canvas.operations))
        if value is not None:
            self.canvas.publish({"kind": "sequence", "number": value})

    def copy(self):
        QApplication.clipboard().setImage(self.canvas.result())
        self.size_label.setText(tr("已复制到剪贴板"))

    def save(self):
        from datetime import datetime
        image = self.canvas.result()
        filename = "Still_"+datetime.now().strftime("%Y%m%d_%H%M%S")+".png"
        folder = self.workbench.controller.store.settings.capture_directory
        path, _ = QFileDialog.getSaveFileName(self, tr("保存截图"), str(Path(folder or self.workbench.controller.store.directory) / filename), tr("PNG 图片 (*.png)"))
        if path:
            if not image.save(path, "PNG"):
                self.workbench.error(tr("截图保存失败，请检查路径和剩余空间。"))
            else:
                self.workbench.controller.store.settings.capture_directory = str(Path(path).parent)
                self.workbench.controller.store.save()
                self.size_label.setText(tr("已保存 PNG"))


class PinWindow(QWidget):
    def __init__(self, workbench, image):
        super().__init__(None, Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint | Qt.WindowType.WindowStaysOnTopHint)
        self.workbench, self.image = workbench, image.copy()
        self.drag_offset = None
        self.border_visible = True
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setWindowTitle(tr("轻块桌面 · 贴图"))
        self.setMinimumSize(100, 70)
        screen = QApplication.screenAt(QCursor.pos()) or QApplication.primaryScreen()
        area = screen.availableGeometry()
        factor = min(1, 620/image.width(), (area.width()-40)/image.width(), (area.height()-70)/image.height())
        self.resize(max(100,round(image.width()*factor))+12, max(70,round(image.height()*factor))+12)
        self.move(area.center()-self.rect().center())
        self.grip = QSizeGrip(self)
        self.grip.setFixedSize(17, 17)
        QShortcut(QKeySequence("Escape"), self, self.close)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        margin = 6 if self.border_visible else 0
        if self.border_visible:
            painter.setPen(QPen(QColor(220, 235, 255, 180), 1))
            painter.setBrush(QColor("#202c3c"))
            painter.drawRoundedRect(QRectF(self.rect()).adjusted(1, 1, -1, -1), 8, 8)
        area = QRectF(self.rect()).adjusted(margin, margin, -margin, -margin)
        factor = min(area.width()/self.image.width(), area.height()/self.image.height())
        width, height = self.image.width()*factor, self.image.height()*factor
        painter.drawImage(QRectF((self.width()-width)/2, (self.height()-height)/2, width, height), self.image)

    def resizeEvent(self, event):
        self.grip.move(self.width()-19, self.height()-19)
        super().resizeEvent(event)

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.drag_offset = event.globalPosition().toPoint()-self.pos()

    def mouseMoveEvent(self, event):
        if self.drag_offset is not None:
            self.move(event.globalPosition().toPoint()-self.drag_offset)

    def mouseReleaseEvent(self, event):
        self.drag_offset = None

    def mouseDoubleClickEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            QApplication.clipboard().setImage(self.image)
            self.close()

    def toggle_border(self):
        self.border_visible = not self.border_visible
        self.grip.setVisible(self.border_visible)
        self.update()

    def wheelEvent(self, event):
        self.setWindowOpacity(max(0.25, min(1.0, self.windowOpacity() + (0.05 if event.angleDelta().y() > 0 else -0.05))))

    def contextMenuEvent(self, event):
        menu = QMenu(self)
        menu.addAction(tr("复制图片"), lambda: QApplication.clipboard().setImage(self.image))
        menu.addAction(tr("隐藏边框") if self.border_visible else tr("显示边框"), self.toggle_border)
        menu.addAction(tr("打开编辑器"), lambda: self.workbench.edit_image(self.image))
        menu.addAction(tr("恢复不透明"), lambda: self.setWindowOpacity(1.0))
        menu.addAction(tr("关闭贴图"), self.close)
        menu.exec(event.globalPos())
