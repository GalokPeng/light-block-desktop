"""Region capture, non-destructive annotation and pinned image references."""
from __future__ import annotations

from bisect import bisect_left, bisect_right
import math
from pathlib import Path

from PySide6.QtCore import QPoint, QPointF, QRect, QRectF, QSize, Qt, Signal
from PySide6.QtGui import (QColor, QCursor, QFont, QKeySequence, QPainter, QPainterPath,
                           QPainterPathStroker, QPen, QPolygonF, QShortcut)
from PySide6.QtWidgets import QApplication, QFileDialog, QHBoxLayout, QInputDialog, QMenu, QSizeGrip, QVBoxLayout, QWidget


from .i18n import tr
from .capture_text import TEXT_SIZES, AnnotationHistory, InlineText, annotation_font
from .capture_tools import DOCK_STYLE, NUMBER_SIZES, STROKE_SIZES, DrawingTools, divider, draw_colors, icon_button, strip

SHAPES = {"rect", "ellipse"}
MOSAICS = {"mosaic", "mosaic_brush"}
FREEHAND = {"pen", "highlight", "mosaic_brush"}


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


def operation_rect(operation):
    points = operation["points"]
    return QRectF(QPointF(*points[0]), QPointF(*points[-1])).normalized()


def shape_path(operation):
    kind = operation["kind"]
    rect = QRectF(*operation["rect"]) if "rect" in operation else operation_rect(operation)
    path = QPainterPath()
    if kind == "ellipse":
        path.addEllipse(rect)
    else:
        radius = min(operation.get("radius", 0), rect.width()/2, rect.height()/2)
        path.addRoundedRect(rect, radius, radius)
    return path


def brush_stroker(width):
    stroker = QPainterPathStroker()
    stroker.setWidth(width)
    stroker.setCapStyle(Qt.PenCapStyle.RoundCap)
    stroker.setJoinStyle(Qt.PenJoinStyle.RoundJoin)
    return stroker


def mosaic_path(operation):
    if operation["kind"] != "mosaic_brush":
        return shape_path(operation)
    points = [QPointF(*point) for point in operation["points"]]
    width = operation.get("width", 24)
    path = QPainterPath(points[0])
    if len(points) == 1:
        path.addEllipse(points[0], width/2, width/2)
        return path
    for point in points[1:]:
        path.lineTo(point)
    return brush_stroker(width).createStroke(path)


def mosaic_texture(source, operation, ratio=1):
    path = mosaic_path(operation)
    bounds = path.boundingRect()
    area = QRectF(bounds.topLeft()*ratio, bounds.size()*ratio).toAlignedRect().intersected(source.rect())
    if area.isEmpty():
        return None
    block = max(1, operation.get("block_size", 14)*ratio)
    pixels = source.copy(area).scaled(max(1, int(area.width()/block)), max(1, int(area.height()/block)))
    pixels.setDevicePixelRatio(1)
    target = QRectF(QPointF(area.topLeft())/ratio, QRectF(area).size()/ratio)
    return path, target, pixels


def paint_mosaic(painter, texture):
    if texture is None:
        return
    path, target, pixels = texture
    painter.save()
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, False)
    painter.setClipPath(path, Qt.ClipOperation.IntersectClip)
    painter.drawImage(target, pixels)
    painter.restore()


def extend_drawing(operation, point, constrain=False):
    points = operation["points"]
    kind = operation["kind"]
    if kind in FREEHAND:
        if points[-1] != point:
            points.append(point)
    elif kind not in {"text", "number"}:
        if constrain and kind in SHAPES:
            x, y = points[0]
            dx, dy = point[0]-x, point[1]-y
            size = min(abs(dx), abs(dy))
            point = (x+math.copysign(size, dx), y+math.copysign(size, dy))
        operation["points"] = [points[0], point]


def stroke_hit(operation, point, margin):
    width = operation.get("width", 4)
    points = [QPointF(*point) for point in operation["points"]]
    kind = operation["kind"]
    if kind == "number":
        return number_badge(operation, points[0]).adjusted(-margin, -margin, margin, margin).contains(point)
    radius = max(margin, width * (1.5 if kind == "highlight" else .5) + margin / 2)
    if kind in SHAPES:
        path = shape_path(operation)
        return ((operation.get("fill") and path.contains(point))
                or brush_stroker(radius*2).createStroke(path).contains(point))
    if kind in MOSAICS:
        return mosaic_path(operation).contains(point)
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

    def hit(self, point, *, erase=False):
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
            elif kind in SHAPES or kind in {"pen", "highlight", "line", "arrow", "number"} or erase and kind in MOSAICS:
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
    style = Qt.PenStyle.DashLine if operation.get("dashed") else Qt.PenStyle.SolidLine
    painter.setPen(QPen(color, width, style, Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin))
    painter.setBrush(Qt.BrushStyle.NoBrush)
    points = [QPointF(*point) for point in operation.get("points", [])]
    if kind in ("pen", "highlight") and points:
        if len(points) == 1 or all(point == points[0] for point in points):
            painter.drawPoint(points[0])
        else:
            painter.drawPolyline(QPolygonF(points))
    elif kind in SHAPES and len(points) >= 2:
        if operation.get("fill"):
            fill = QColor(color)
            fill.setAlpha(80)
            painter.setBrush(fill)
        painter.drawPath(shape_path(operation))
    elif kind in ("arrow", "line") and len(points) >= 2:
        a, b = points[0], points[-1]
        painter.drawLine(a, b)
        if kind == "arrow" and a != b:
            painter.setPen(QPen(color, width, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
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
        elif operation["kind"] in MOSAICS:
            texture = mosaic_texture(image, operation)
            painter = QPainter(image)
            paint_mosaic(painter, texture)
            painter.end()
        else:
            painter = QPainter(image)
            paint_operation(painter, operation)
            painter.end()
    return image


class AnnotationCanvas(QWidget):
    changed = Signal()

    def __init__(self, image, attach_text_bar=lambda bar: None, wheel_sizes=True, *, pin=None):
        super().__init__(pin)
        self.pin = pin
        self.source = image.copy()
        self.source.setDevicePixelRatio(1)
        self.image = self.source
        self.history = AnnotationHistory()
        self.operations = self.history.operations
        self.inline = InlineText(self, lambda: self.image_area, self.text_transform, self.publish,
                                 attach_text_bar, draw_colors())
        self.sizing = AnnotationSizer(self.inline, wheel_sizes)
        self.inline.resize_on_wheel = self.adjust_wheel
        self.hidden_image = None
        self.hidden_index = None
        self.tool = "pen"
        self.drawing = None
        self.current = None
        self.image_area = QRectF()
        if pin is None:
            self.setMinimumSize(420, 300)
        self.setCursor(Qt.CursorShape.CrossCursor)

    def paintEvent(self, event):
        painter = QPainter(self)
        if self.pin is None:
            painter.fillRect(self.rect(), QColor("#101721"))
        padding = 30 if self.pin is None else 0
        scale = min((self.width()-padding)/self.image.width(), (self.height()-padding)/self.image.height())
        if self.pin is None:
            scale = min(scale, 1.5)
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
            painter.setClipRect(self.image_area)
            painter.translate(self.image_area.topLeft())
            painter.scale(scale, scale)
            if self.current["kind"] == "crop":
                painter.setPen(QPen(QColor("#a6e4ca"), 2/scale, Qt.PenStyle.DashLine))
                painter.drawRect(operation_rect(self.current))
            elif self.current["kind"] in MOSAICS:
                paint_mosaic(painter, mosaic_texture(self.image, self.current))
            else:
                paint_operation(painter, self.current)
            painter.restore()

    def set_tool(self, tool):
        self.tool = tool
        self.inline.activate(tool == "text")
        if self.drawing is not None:
            self.drawing.activate(tool)
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
            if self.tool == "erase":
                target = self.sizing.hit(event.position(), erase=True)
                if target is not None:
                    self.publish(None, target)
                return
            point = self.coordinate(event.position())
            self.current = (self.drawing.operation(self.tool, point) if self.drawing else
                            {"kind": self.tool, "points": [point]})
            if self.tool == "number":
                self.current["number"] = next_number(self.operations)
            self.update()

    def mouseMoveEvent(self, event):
        if self.inline.move(event.position()):
            return
        if self.tool == "text":
            self.setCursor(Qt.CursorShape.SizeAllCursor if self.inline.hit(event.position()) is not None
                           else Qt.CursorShape.IBeamCursor)
        if self.current:
            extend_drawing(self.current, self.coordinate(event.position()), event.modifiers() & Qt.KeyboardModifier.ShiftModifier)
            self.update()

    def mouseDoubleClickEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton and self.tool == "text":
            self.inline.double_click(event.position())

    def wheelEvent(self, event):
        if not self.adjust_wheel(event, event.position()):
            event.ignore()

    def adjust_wheel(self, event, point):
        if self.current:
            return False
        if self.pin is not None and self.pin.zoom_wheel(event):
            return True
        return self.sizing.adjust(event, point)

    def mouseReleaseEvent(self, event):
        if event.button() != Qt.MouseButton.LeftButton or self.inline.release() or not self.current:
            return
        operation = self.current
        self.current = None
        extend_drawing(operation, self.coordinate(event.position()), event.modifiers() & Qt.KeyboardModifier.ShiftModifier)
        if operation["kind"] in {"crop", "mosaic"}:
            rect = operation_rect(operation).toRect().intersected(self.image.rect())
            if rect.width() < 3 or rect.height() < 3:
                self.update()
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

    def reset_number(self):
        self.inline.finish()
        value = choose_number(self, next_number(self.operations))
        if value is not None:
            self.publish({"kind": "sequence", "number": value})


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
        dock = QWidget()
        dock.setStyleSheet(DOCK_STYLE)
        bars = QVBoxLayout(dock)
        bars.setContentsMargins(0, 0, 0, 0)
        bars.setSpacing(5)
        toolbar, row = strip()
        bars.addWidget(toolbar)
        outer.addWidget(dock)
        self.canvas = AnnotationCanvas(image, bars.addWidget, wheel_sizes)
        self.canvas.drawing = DrawingTools(row, bars.addWidget, self.canvas.set_tool, self.canvas.reset_number, crop=True)
        divider(row)
        row.addWidget(icon_button("undo", tr("撤销"), self.canvas.undo))
        row.addWidget(icon_button("redo", tr("重做"), self.canvas.redo))
        row.addStretch()
        outer.addWidget(self.canvas, 1)
        self.canvas.set_tool("pen")
        bottom = QHBoxLayout()
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
        self.image.setDevicePixelRatio(1)
        self.drag_offset = None
        self.border_visible = True
        self.editing = False
        self.canvas = self.edit_dock = None
        self.dock_height = 0
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setWindowTitle(tr("轻块桌面 · 贴图"))
        self.setToolTip(tr("Ctrl + 滚轮缩放 · 右键编辑"))
        self.setMinimumSize(100, 70)
        screen = QApplication.screenAt(QCursor.pos()) or QApplication.primaryScreen()
        area = screen.availableGeometry()
        factor = min(1, 620/image.width(), (area.width()-40)/image.width(), (area.height()-70)/image.height())
        self.resize(max(100,round(image.width()*factor))+12, max(70,round(image.height()*factor))+12)
        self.move(area.center()-self.rect().center())
        self.grip = QSizeGrip(self)
        self.grip.setFixedSize(17, 17)
        QShortcut(QKeySequence("Escape"), self, lambda: self.stop_editing() if self.editing else self.close())
        QShortcut(QKeySequence.StandardKey.Copy, self, self.copy_image)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        if self.border_visible:
            painter.setPen(QPen(QColor(220, 235, 255, 180), 1))
            painter.setBrush(QColor("#202c3c"))
            painter.drawRoundedRect(QRectF(self.rect()).adjusted(1, 1, -1, -1), 8, 8)
        if not self.editing:
            image = self.canvas.image if self.canvas is not None else self.image
            area = QRectF(self.photo_area())
            factor = min(area.width()/image.width(), area.height()/image.height())
            width, height = image.width()*factor, image.height()*factor
            target = QRectF(0, 0, width, height)
            target.moveCenter(area.center())
            painter.drawImage(target, image)

    def resizeEvent(self, event):
        self.grip.move(self.width()-19, self.height()-19)
        self.place_editor()
        super().resizeEvent(event)

    def photo_area(self):
        margin = 6 if self.border_visible else 0
        return self.rect().adjusted(margin, margin, -margin, -margin-self.dock_height)

    def place_editor(self):
        if self.editing:
            self.canvas.setGeometry(self.photo_area())
            self.edit_dock.setGeometry(4, self.height()-self.dock_height, self.width()-8, self.dock_height-4)

    def current_image(self):
        return self.canvas.result() if self.canvas is not None else self.image

    def copy_image(self):
        QApplication.clipboard().setImage(self.current_image())

    def start_editing(self):
        if self.editing:
            self.canvas.setFocus()
            return
        if self.canvas is None:
            self.edit_dock = QWidget(self)
            self.edit_dock.setStyleSheet(DOCK_STYLE)
            bars = QVBoxLayout(self.edit_dock)
            bars.setContentsMargins(0, 0, 0, 0)
            bars.setSpacing(4)
            toolbar, row = strip()
            bars.addWidget(toolbar)
            self.canvas = AnnotationCanvas(self.image, bars.addWidget, pin=self)
            self.canvas.drawing = DrawingTools(row, bars.addWidget, self.edit_tool, self.canvas.reset_number)
            divider(row)
            row.addWidget(icon_button("undo", tr("撤销"), self.canvas.undo))
            row.addWidget(icon_button("redo", tr("重做"), self.canvas.redo))
            row.addWidget(icon_button("copy", tr("复制图片"), self.copy_image))
            if self.workbench is not None:
                row.addWidget(icon_button("save", tr("保存 PNG"), lambda: self.workbench.save_capture(self.current_image())))
            row.addWidget(icon_button("done", tr("完成编辑"), self.stop_editing))
            QShortcut(QKeySequence.StandardKey.Undo, self.canvas, self.canvas.undo)
            QShortcut(QKeySequence.StandardKey.Redo, self.canvas, self.canvas.redo)
        self.editing = True
        self.grip.hide()
        self.canvas.show()
        self.edit_dock.show()
        self.edit_tool(self.canvas.tool)
        self.canvas.setFocus()
        self.update()

    def edit_tool(self, tool):
        self.canvas.set_tool(tool)
        previous = self.dock_height
        hint = self.edit_dock.sizeHint()
        self.dock_height = hint.height()+4
        self.fit_size(QSize(max(self.width(), hint.width()+8), self.height()+self.dock_height-previous))
        self.place_editor()

    def stop_editing(self):
        self.canvas.inline.activate(False)
        self.editing = False
        self.canvas.hide()
        self.edit_dock.hide()
        height, self.dock_height = self.height()-self.dock_height, 0
        self.grip.setVisible(self.border_visible)
        self.resize(self.width(), height)
        self.update()

    def fit_size(self, size, anchor=None):
        screen = self.screen() or QApplication.primaryScreen()
        bounds = screen.availableGeometry()
        minimum = QSize(100, 70+self.dock_height)
        if self.editing:
            minimum.setWidth(max(minimum.width(), self.edit_dock.sizeHint().width()+8))
        size = size.expandedTo(minimum).boundedTo(bounds.size())
        origin = self.pos()
        if anchor is not None:
            origin = anchor - QPoint(round((anchor.x()-self.x())*size.width()/self.width()),
                                     round((anchor.y()-self.y())*size.height()/self.height()))
        self.resize(size)
        self.move(max(bounds.left(), min(origin.x(), bounds.right()-self.width()+1)),
                  max(bounds.top(), min(origin.y(), bounds.bottom()-self.height()+1)))

    def zoom_wheel(self, event):
        if not event.modifiers() & Qt.KeyboardModifier.ControlModifier:
            return False
        delta = event.angleDelta().y() or event.pixelDelta().y()*4
        if delta:
            factor = 1.16 ** max(-5, min(5, delta/120))
            width = round(self.width()*factor)
            height = round((self.height()-self.dock_height)*factor)+self.dock_height
            self.fit_size(QSize(width, height), event.globalPosition().toPoint())
        event.accept()
        return True

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.drag_offset = event.globalPosition().toPoint()-self.pos()

    def mouseMoveEvent(self, event):
        if self.drag_offset is not None:
            self.move(event.globalPosition().toPoint()-self.drag_offset)

    def mouseReleaseEvent(self, event):
        self.drag_offset = None

    def mouseDoubleClickEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton and not self.editing:
            self.copy_image()
            self.close()

    def toggle_border(self):
        self.border_visible = not self.border_visible
        self.grip.setVisible(self.border_visible and not self.editing)
        self.place_editor()
        self.update()

    def wheelEvent(self, event):
        if self.zoom_wheel(event):
            return
        delta = event.angleDelta().y()
        if delta:
            self.setWindowOpacity(max(.25, min(1.0, self.windowOpacity() + (.05 if delta > 0 else -.05))))

    def contextMenuEvent(self, event):
        menu = QMenu(self)
        menu.addAction(tr("完成编辑") if self.editing else tr("编辑贴图"),
                       self.stop_editing if self.editing else self.start_editing)
        menu.addAction(tr("复制图片"), self.copy_image)
        menu.addAction(tr("隐藏边框") if self.border_visible else tr("显示边框"), self.toggle_border)
        menu.addAction(tr("恢复不透明"), lambda: self.setWindowOpacity(1.0))
        menu.addAction(tr("关闭贴图"), self.close)
        menu.exec(event.globalPos())
