"""One frozen screen, an adjustable selection and the capture tools beside it."""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W

from PySide6.QtCore import QEvent, QPoint, QPointF, QRect, QRectF, QSize, Qt, Signal
from PySide6.QtGui import QColor, QCursor, QFontMetrics, QIcon, QKeySequence, QPainter, QPen, QPixmap, QShortcut
from PySide6.QtWidgets import (QApplication, QComboBox, QFormLayout, QFrame, QHBoxLayout,
                             QMenu, QToolButton, QVBoxLayout, QWidget, QWidgetAction)

from .i18n import tr
from . import win32
from .capture import (DRAW_COLORS, DRAW_TOOLS, STROKE_SIZES, AnnotationSizer, choose_number,
                      next_number, paint_operation, render_operations)
from .capture_text import AnnotationHistory, InlineText
from .capture_icons import capture_icon


def pixel_rect(rect, ratio):
    left, top = round(rect.x()*ratio), round(rect.y()*ratio)
    right = round((rect.x()+rect.width())*ratio)
    bottom = round((rect.y()+rect.height())*ratio)
    return QRect(left, top, right-left, bottom-top)


def logical_region(physical, screen):
    x, y = win32.target_monitor(screen.name())["rect"][:2]
    ratio = screen.devicePixelRatio()
    return pixel_rect(physical.translated(-x, -y), 1/ratio).translated(screen.geometry().topLeft())


def beside_region(region, area, size, gap=10):
    """Find an on-screen position that leaves the selected area uncovered."""
    width, height = size.width(), size.height()
    if width > area.width() or height > area.height():
        return None
    top = max(area.top(), min(region.top(), area.bottom()-height+1))
    left = max(area.left(), min(region.left(), area.right()-width+1))
    candidates = (QPoint(region.right()+gap+1, top), QPoint(region.left()-width-gap, top),
                  QPoint(left, region.bottom()+gap+1), QPoint(left, region.top()-height-gap))
    for point in candidates:
        if area.contains(QRect(point, size)):
            return point
    return None


def native_targets(monitor, ratio, bounds):
    """Snapshot actual frame/control rectangles in top-level Z order."""
    visible = win32.bind(win32.user32, "IsWindowVisible", W.BOOL, W.HWND)
    iconic = win32.bind(win32.user32, "IsIconic", W.BOOL, W.HWND)
    children = win32.bind(win32.user32, "EnumChildWindows", W.BOOL, W.HWND, win32.ENUMPROC, W.LPARAM)
    ox, oy = monitor["rect"][:2]
    targets = []
    control_count = 0

    def logical(rect):
        left, top = round((rect.left-ox)/ratio), round((rect.top-oy)/ratio)
        right, bottom = round((rect.right-ox)/ratio), round((rect.bottom-oy)/ratio)
        return QRect(left, top, right-left, bottom-top).intersected(bounds)

    @win32.ENUMPROC
    def visit(hwnd, _):
        nonlocal control_count
        if not visible(hwnd) or iconic(hwnd):
            return True
        classname = C.create_unicode_buffer(128)
        win32.GetClassName(hwnd, classname, len(classname))
        if classname.value in {"Progman", "WorkerW", "Shell_TrayWnd", "Shell_SecondaryTrayWnd"}:
            return True
        cloaked = W.DWORD()
        win32.DwmGetWindowAttribute(hwnd, 14, C.byref(cloaked), C.sizeof(cloaked))
        if cloaked.value:
            return True
        native = W.RECT()
        if win32.DwmGetWindowAttribute(hwnd, 9, C.byref(native), C.sizeof(native)):
            if not win32.GetWindowRect(hwnd, C.byref(native)):
                return True
        frame = logical(native)
        if frame.width() < 32 or frame.height() < 24:
            return True
        controls = []

        @win32.ENUMPROC
        def visit_child(child, _):
            nonlocal control_count
            if len(controls) >= 50 or control_count >= 400:
                return False
            child_rect = W.RECT()
            if visible(child) and win32.GetWindowRect(child, C.byref(child_rect)):
                box = logical(child_rect).intersected(frame)
                if box != frame and box.width() >= 32 and box.height() >= 24 and box not in controls:
                    controls.append(box)
                    control_count += 1
            return True

        if control_count < 400:
            children(hwnd, visit_child, 0)
        controls.sort(key=lambda r: r.width()*r.height())
        targets.append((frame, controls))
        return len(targets) < 96

    win32.EnumWindows(visit, 0)
    return targets


class CaptureOverlay(QWidget):
    finished = Signal(str, object, object)
    cancelled = Signal()

    def __init__(self, screen, mode="capture"):
        super().__init__(None, Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint |
                         Qt.WindowType.WindowStaysOnTopHint | Qt.WindowType.NoDropShadowWindowHint)
        from .ui import STYLE
        self.screen_source = screen
        self.mode = mode
        self.snapshot = screen.grabWindow(0)
        if self.snapshot.isNull():
            raise OSError(tr("无法读取当前屏幕，请检查显示器后重试。"))
        self.ratio = self.snapshot.devicePixelRatio()
        self.monitor = win32.target_monitor(screen.name())
        self.setWindowTitle(tr("轻块桌面 · 截图选区"))
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setAttribute(Qt.WidgetAttribute.WA_OpaquePaintEvent)
        self.setGeometry(screen.geometry())
        self.setMouseTracking(True)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.targets = native_targets(self.monitor, self.ratio, self.rect())
        self.selection = QRect()
        self.hover = QRect()
        self.original = QRect()
        self.mode_smart = True
        self.completed = False
        self.gesture = None
        self.handle = ""
        self.anchor = QPoint()
        self.draft = None
        self.history = AnnotationHistory()
        self.operations = self.history.operations
        self.tool = "move"
        self.setCursor(Qt.CursorShape.CrossCursor)

        self.toolbar = QFrame(self)
        self.toolbar.setObjectName("captureDock")
        self.toolbar.setStyleSheet(STYLE + """
            QFrame#captureDock { background:#171a21;border:1px solid #444b58;border-radius:10px; }
            QToolButton { background:transparent;border:0;border-radius:6px;padding:2px;color:white; }
            QToolButton:hover { background:#323944; }
            QToolButton:checked { background:#274d7c; }
            QToolButton#confirmCapture { background:#2d6bd2; }
            QToolButton#confirmCapture:hover { background:#3b80eb; }
            QToolButton::menu-button { width:13px;border:0; }
            QToolButton::menu-indicator { width:8px;height:8px; }
            QFrame#toolDivider { background:#414956; }
        """)
        bar = QVBoxLayout(self.toolbar)
        self.inline = InlineText(self, lambda: QRectF(self.selection), lambda index: (QPointF(), 1),
                                 self.publish, bar.addWidget, DRAW_COLORS)
        self.sizing = AnnotationSizer(self.inline, mode != "longshot")
        self.inline.resize_on_wheel = self.sizing.adjust
        bar.setContentsMargins(6, 5, 6, 5)
        bar.setSpacing(3)
        self.picker = QWidget()
        picker = QHBoxLayout(self.picker)
        picker.setContentsMargins(0, 0, 0, 0)
        picker.setSpacing(3)
        self.pick_controls = {}
        for key, title, smart in (("smart", tr("智能窗口"), True), ("rect", tr("手动选区"), False)):
            item = self.icon_button(key, title, lambda checked=False, value=smart: self.set_smart(value))
            item.setCheckable(True)
            item.setChecked(smart)
            self.pick_controls[smart] = item
            picker.addWidget(item)
        picker.addWidget(self.icon_button("full", tr("当前屏幕"), self.select_full))
        picker.addWidget(self.icon_button("cancel", tr("取消 · Esc"), self.close))
        bar.addWidget(self.picker)

        self.actions = QWidget()
        row = QHBoxLayout(self.actions)
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(3)
        self.drawing_controls = {}
        self.add_drawing_group(row, ("rect", "line", "arrow"))
        self.add_drawing_group(row, ("pen", "highlight"))
        self.add_drawing_group(row, ("text",))
        self.add_drawing_group(row, ("number",))
        self.add_drawing_group(row, ("mosaic",))
        options = QMenu(self)
        form = QWidget()
        form_layout = QFormLayout(form)
        form_layout.setContentsMargins(12, 8, 12, 8)
        self.color = QComboBox()
        self.color.setEditable(False)
        self.color.setAccessibleName(tr("标注颜色"))
        for value, name in DRAW_COLORS:
            swatch = QPixmap(14, 14)
            swatch.fill(QColor(value))
            self.color.addItem(QIcon(swatch), name, value)
        form_layout.addRow(tr("颜色"), self.color)
        self.stroke = QComboBox()
        self.stroke.setEditable(False)
        self.stroke.setAccessibleName(tr("画笔大小"))
        for value in STROKE_SIZES:
            self.stroke.addItem(f"{value} px", value)
        self.stroke.setCurrentIndex(self.stroke.findData(4))
        form_layout.addRow(tr("粗细"), self.stroke)
        style_action = QWidgetAction(options)
        style_action.setDefaultWidget(form)
        options.addAction(style_action)
        row.addWidget(self.icon_button("style", tr("颜色与粗细"), menu=options))
        self.divider(row)
        row.addWidget(self.icon_button("undo", tr("撤销 · Ctrl+Z"), self.undo))
        row.addWidget(self.icon_button("redo", tr("重做"), self.redo))
        self.divider(row)
        for action, title in (("longshot", tr("滚轮长截图")), ("record", tr("开始录屏")), ("pin", tr("贴图")),
                              ("save", tr("保存 PNG")), ("cancel", tr("取消 · Esc")), ("copy", tr("复制 · Ctrl+C"))):
            callback = self.close if action == "cancel" else lambda checked=False, value=action: self.commit(value)
            item = self.icon_button(action, title, callback)
            if mode == action or mode == "capture" and action == "copy":
                item.setObjectName("confirmCapture")
            row.addWidget(item)
        bar.addWidget(self.actions)

        self.side_tools = QWidget(self)
        self.side_tools.setStyleSheet("""
            QToolButton { background:#1d2939;border:1px solid #465365;border-radius:17px; }
            QToolButton:hover { background:#34465c; }
            QToolButton:checked { background:#2a65b8;border-color:#9bc8ff; }
        """)
        side = QVBoxLayout(self.side_tools)
        side.setContentsMargins(0, 0, 0, 0)
        side.setSpacing(5)
        self.move_tool = self.icon_button("move", tr("拖动选区"), lambda: self.set_tool("move"))
        self.move_tool.setCheckable(True)
        self.move_tool.setChecked(True)
        side.addWidget(self.move_tool)
        side.addWidget(self.icon_button("full", tr("选中当前屏幕"), self.select_full))
        side.addWidget(self.icon_button("reset", tr("重新选区"), self.reset_selection))
        self.side_tools.adjustSize()
        self.side_tools.hide()
        self.actions.hide()
        self.place_bar()
        QShortcut(QKeySequence("Escape"), self, self.close)
        QShortcut(QKeySequence.StandardKey.Copy, self, lambda: self.commit("copy"))
        QShortcut(QKeySequence.StandardKey.Save, self, lambda: self.commit("save"))
        QShortcut(QKeySequence.StandardKey.Undo, self, self.undo)
        QShortcut(QKeySequence.StandardKey.Redo, self, self.redo)
        QShortcut(QKeySequence("Return"), self, lambda: self.commit(mode if mode in {"record", "longshot"} else "copy"))
        self.update_hover(self.mapFromGlobal(QCursor.pos()))
        QApplication.instance().installEventFilter(self)

    def eventFilter(self, watched, event):
        if (event.type() == QEvent.Type.MouseButtonRelease and event.button() == Qt.MouseButton.RightButton
                and isinstance(watched, QWidget) and watched.window() is self):
            if self.selection.isEmpty() and self.gesture is None:
                self.close()
            else:
                self.reset_selection()
            return True
        return super().eventFilter(watched, event)

    def place_bar(self):
        self.toolbar.adjustSize()
        width, height = self.toolbar.width(), self.toolbar.height()
        if self.selection.isEmpty():
            x, y = (self.width()-width)//2, 15
        else:
            x = self.selection.right()-width+1
            y = self.selection.bottom()+12
            if y+height > self.height()-6:
                y = self.selection.top()-height-11
            if y < 6:
                y = self.selection.bottom()-height-12
        self.toolbar.move(max(6, min(x, self.width()-width-6)), max(6, min(y, self.height()-height-6)))
        if not self.selection.isEmpty():
            x = self.selection.right()+10
            if x+self.side_tools.width() > self.width()-5:
                x = self.selection.left()-self.side_tools.width()-9
            if x < 5:
                x = self.selection.right()-self.side_tools.width()-9
            y = max(6, min(self.selection.top(), self.height()-self.side_tools.height()-6))
            self.side_tools.move(max(5, x), y)

    def icon_button(self, key, title, callback=None, menu=None):
        button = QToolButton()
        button.setIcon(capture_icon(key))
        button.setIconSize(QSize(21, 21))
        button.setFixedSize(46 if menu and callback else 34, 34)
        button.setToolTip(title)
        button.setAccessibleName(title)
        button.setCursor(Qt.CursorShape.PointingHandCursor)
        button.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        if callback:
            button.clicked.connect(callback)
        if menu:
            button.setMenu(menu)
            button.setPopupMode(QToolButton.ToolButtonPopupMode.MenuButtonPopup if callback else
                                QToolButton.ToolButtonPopupMode.InstantPopup)
        return button

    def divider(self, layout):
        line = QFrame()
        line.setObjectName("toolDivider")
        line.setFixedSize(1, 20)
        layout.addWidget(line)

    def add_drawing_group(self, layout, tools):
        labels = dict(DRAW_TOOLS)
        first = tools[0]
        menu = QMenu(self) if len(tools) > 1 or first == "number" else None
        control = self.icon_button(first, labels[first], lambda: self.set_tool(control.property("tool")), menu)
        control.setCheckable(True)
        control.setProperty("tool", first)
        for key in tools:
            self.drawing_controls[key] = control
            if menu and len(tools) > 1:
                menu.addAction(capture_icon(key), labels[key], lambda checked=False, tool=key: self.set_tool(tool))
        if first == "number":
            menu.addAction(tr("重计序号…"), self.reset_number)
        layout.addWidget(control)

    def set_smart(self, enabled):
        self.mode_smart = enabled
        for value, control in self.pick_controls.items():
            control.setChecked(value == enabled)
        self.update_hover(self.mapFromGlobal(QCursor.pos()))
        self.update()

    def set_tool(self, tool):
        self.tool = tool
        self.inline.activate(tool == "text")
        self.place_bar()
        self.update()
        selected = self.drawing_controls.get(tool)
        for control in set(self.drawing_controls.values()):
            control.setChecked(control is selected)
        self.move_tool.setChecked(tool == "move")
        if selected:
            selected.setIcon(capture_icon(tool))
            selected.setProperty("tool", tool)
            selected.setToolTip(dict(DRAW_TOOLS)[tool])
            selected.setAccessibleName(dict(DRAW_TOOLS)[tool])

    def update_hover(self, point, outer=False):
        previous = self.hover
        self.hover = QRect()
        if self.mode_smart and self.rect().contains(point):
            for frame, controls in self.targets:
                if frame.contains(point):
                    self.hover = frame if outer else next((r for r in controls if r.contains(point)), frame)
                    break
        if previous != self.hover:
            self.update(previous.united(self.hover).adjusted(-2, -2, 2, 2))

    def select_full(self):
        self.selection = self.rect()
        self.confirm_selection()

    def reset_selection(self):
        self.selection = QRect()
        self.hover = QRect()
        self.original = QRect()
        self.gesture = self.draft = None
        self.handle = ""
        self.set_tool("move")
        self.history.clear()
        self.actions.hide()
        self.side_tools.hide()
        self.picker.show()
        self.toolbar.show()
        self.setCursor(Qt.CursorShape.CrossCursor)
        self.place_bar()
        self.setFocus()
        self.update()

    def confirm_selection(self):
        self.picker.hide()
        self.actions.show()
        self.side_tools.show()
        self.toolbar.show()
        self.place_bar()
        self.setFocus()
        self.update()

    def handles(self):
        r = self.selection
        return {"lt": r.topLeft(), "t": QPoint(r.center().x(), r.top()), "rt": r.topRight(),
                "l": QPoint(r.left(), r.center().y()), "r": QPoint(r.right(), r.center().y()),
                "lb": r.bottomLeft(), "b": QPoint(r.center().x(), r.bottom()), "rb": r.bottomRight()}

    def hit_handle(self, point):
        if self.selection.isEmpty():
            return ""
        return next((name for name, corner in self.handles().items() if (corner-point).manhattanLength() <= 10), "")

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.drawPixmap(0, 0, self.snapshot)
        shaded = self.gesture != "new" and not self.selection.isEmpty()
        if shaded:
            painter.fillRect(self.rect(), QColor(4, 10, 19, 80))
        chosen = self.hover if self.selection.isEmpty() and self.gesture != "new" else self.selection
        if not chosen.isEmpty():
            painter.save()
            painter.setClipRect(chosen)
            if shaded:
                painter.drawPixmap(0, 0, self.snapshot)
            for index, operation in enumerate(self.operations):
                if index == self.inline.hidden:
                    continue
                if operation["kind"] == "mosaic":
                    painter.drawImage(QRectF(*operation["rect"]), operation["preview"])
                elif operation["kind"] != "sequence":
                    paint_operation(painter, operation)
            if self.draft:
                if self.draft["kind"] == "mosaic":
                    painter.setPen(QPen(QColor("#a6e4ca"), 1, Qt.PenStyle.DashLine))
                    painter.setBrush(Qt.BrushStyle.NoBrush)
                    painter.drawRect(QRectF(QPointF(*self.draft["points"][0]), QPointF(*self.draft["points"][-1])).normalized())
                elif self.draft["kind"] != "text":
                    paint_operation(painter, self.draft)
            self.inline.paint(painter, paint_operation)
            painter.restore()
            painter.setPen(QPen(QColor("#398bff"), 2))
            painter.setBrush(Qt.BrushStyle.NoBrush)
            painter.drawRect(chosen.adjusted(0, 0, -1, -1))
            if not self.selection.isEmpty() and self.gesture != "new":
                painter.setPen(QPen(QColor("#f8fbff"), 1.4))
                painter.setBrush(QColor("#398bff"))
                for point in self.handles().values():
                    painter.drawEllipse(QPointF(point), 5, 5)
            if not self.selection.isEmpty():
                box = pixel_rect(chosen, self.ratio)
                ox, oy = self.monitor["rect"][:2]
                text = f"{ox+box.x()},{oy+box.y()}   {box.width()} × {box.height()} px"
                width = QFontMetrics(painter.font()).horizontalAdvance(text)+22
                badge = QRectF(max(6, min(chosen.x(), self.width()-width-6)), max(6, chosen.top()-34), width, 25)
                painter.setPen(Qt.PenStyle.NoPen)
                painter.setBrush(QColor(14, 19, 28, 218))
                painter.drawRoundedRect(badge, 12, 12)
                painter.setPen(QColor("#ffffff"))
                painter.drawText(badge, Qt.AlignmentFlag.AlignCenter, text)
        if self.selection.isEmpty() and self.gesture != "new":
            hint = (tr("悬停识别窗口 · Alt 选整个窗口 · 拖动自由选区 · 右键退出") if self.mode_smart
                    else tr("拖动选取区域 · 右键退出"))
            width = min(self.width() - 16, QFontMetrics(painter.font()).horizontalAdvance(hint) + 20)
            badge = QRect((self.width() - width) // 2, self.toolbar.geometry().bottom() + 7, width, 24)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(QColor(14, 19, 28, 218))
            painter.drawRoundedRect(badge, 10, 10)
            painter.setPen(QColor("#eff7f9"))
            painter.drawText(badge, Qt.AlignmentFlag.AlignCenter, hint)

    def mousePressEvent(self, event):
        if event.button() != Qt.MouseButton.LeftButton:
            return
        point = event.position().toPoint()
        if self.tool in {"text", "move"} and not self.hit_handle(point):
            if self.inline.press(event.position(), self.tool == "text"):
                self.toolbar.raise_()
                self.side_tools.raise_()
                return
        self.inline.finish()
        self.inline.selected = None
        self.anchor = point
        self.original = QRect(self.selection)
        self.handle = self.hit_handle(point)
        if self.handle:
            self.gesture = "resize"
        elif self.selection.contains(point):
            self.gesture = "move" if self.tool == "move" else "draw"
            if self.gesture == "draw":
                self.draft = {"kind": self.tool, "color": self.color.currentData(),
                              "width": self.stroke.currentData(), "points": [(point.x(), point.y())]}
                if self.tool == "number":
                    self.draft["number"] = next_number(self.operations)
        else:
            self.update_hover(point, bool(event.modifiers() & Qt.KeyboardModifier.AltModifier))
            self.gesture = "new"
            self.selection = QRect()
            self.update()
        self.toolbar.hide()
        self.side_tools.hide()

    def update_gesture(self, point):
        if self.gesture == "new":
            self.selection = QRect(self.anchor, point).normalized().intersected(self.rect())
        elif self.gesture == "move":
            r = self.original.translated(point-self.anchor)
            r.moveLeft(max(0, min(r.x(), self.width()-r.width())))
            r.moveTop(max(0, min(r.y(), self.height()-r.height())))
            self.selection = r
        elif self.gesture == "resize":
            r = QRect(self.original)
            if "l" in self.handle:
                r.setLeft(max(0, min(point.x(), r.right()-7)))
            if "r" in self.handle:
                r.setRight(min(self.width()-1, max(point.x(), r.left()+7)))
            if "t" in self.handle:
                r.setTop(max(0, min(point.y(), r.bottom()-7)))
            if "b" in self.handle:
                r.setBottom(min(self.height()-1, max(point.y(), r.top()+7)))
            self.selection = r
        elif self.draft:
            position = (max(self.selection.left(), min(point.x(), self.selection.right())),
                        max(self.selection.top(), min(point.y(), self.selection.bottom())))
            points = self.draft["points"]
            if self.tool in {"pen", "highlight"}:
                if points[-1] != position:
                    points.append(position)
            elif self.tool not in {"text", "number"}:
                self.draft["points"] = [points[0], position]

    def mouseMoveEvent(self, event):
        point = event.position().toPoint()
        if self.inline.move(event.position()):
            self.setCursor(Qt.CursorShape.SizeAllCursor)
            return
        if self.gesture:
            previous = self.selection
            self.update_gesture(point)
            self.update(previous.united(self.selection).adjusted(-12, -38, 200, 12))
            return
        handle = self.hit_handle(point)
        cursors = {"lt": Qt.CursorShape.SizeFDiagCursor, "rb": Qt.CursorShape.SizeFDiagCursor,
                   "rt": Qt.CursorShape.SizeBDiagCursor, "lb": Qt.CursorShape.SizeBDiagCursor,
                   "t": Qt.CursorShape.SizeVerCursor, "b": Qt.CursorShape.SizeVerCursor,
                   "l": Qt.CursorShape.SizeHorCursor, "r": Qt.CursorShape.SizeHorCursor}
        cursor = Qt.CursorShape.CrossCursor
        if self.tool in {"text", "move"} and self.inline.hit(event.position()) is not None:
            cursor = Qt.CursorShape.SizeAllCursor
        elif self.selection.contains(point):
            cursor = {"text": Qt.CursorShape.IBeamCursor, "move": Qt.CursorShape.SizeAllCursor}.get(self.tool, cursor)
        self.setCursor(cursors.get(handle, cursor))
        if self.selection.isEmpty():
            self.update_hover(point, bool(event.modifiers() & Qt.KeyboardModifier.AltModifier))

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_Alt and self.selection.isEmpty() and self.gesture is None:
            self.update_hover(self.mapFromGlobal(QCursor.pos()), outer=True)
        super().keyPressEvent(event)

    def wheelEvent(self, event):
        if self.gesture or not self.sizing.adjust(event, event.position()):
            event.ignore()

    def keyReleaseEvent(self, event):
        if event.key() == Qt.Key.Key_Alt and self.selection.isEmpty() and self.gesture is None:
            self.update_hover(self.mapFromGlobal(QCursor.pos()))
        super().keyReleaseEvent(event)

    def mouseDoubleClickEvent(self, event):
        if (event.button() == Qt.MouseButton.LeftButton and self.tool in {"text", "move"}
                and self.inline.hit(event.position()) is not None):
            self.set_tool("text")
            self.inline.double_click(event.position())
            self.toolbar.raise_()
            self.side_tools.raise_()
        else:
            super().mouseDoubleClickEvent(event)

    def mouseReleaseEvent(self, event):
        if event.button() != Qt.MouseButton.LeftButton or self.inline.release() or not self.gesture:
            return
        self.update_gesture(event.position().toPoint())
        gesture, self.gesture = self.gesture, None
        if gesture == "new" and (self.selection.width() < 5 or self.selection.height() < 5):
            self.selection = QRect(self.hover if self.original.isEmpty() else self.original)
        if self.draft:
            operation, self.draft = self.draft, None
            self.add_drawing(operation)
        if self.selection.isEmpty():
            self.toolbar.show()
            self.place_bar()
            self.update()
        else:
            self.confirm_selection()

    def add_drawing(self, operation):
        kind = operation["kind"]
        if kind == "mosaic":
            first, last = operation["points"][0], operation["points"][-1]
            area = QRect(QPoint(*first), QPoint(*last)).normalized()
            if area.width() < 3 or area.height() < 3:
                return
            box = pixel_rect(area, self.ratio)
            operation["rect"] = list(area.getRect())
            operation["preview"] = self.snapshot.copy(box).toImage().scaled(max(1, box.width()//14), max(1, box.height()//14))
            operation["preview"].setDevicePixelRatio(1)
        self.publish(operation)

    def publish(self, operation, index=None):
        if self.history.change(operation, index):
            self.update()
        return len(self.operations) - 1 if index is None else index

    def undo(self):
        self.inline.finish()
        self.inline.release()
        self.inline.selected = None
        self.history.undo()
        self.update()

    def redo(self):
        self.inline.finish()
        self.inline.release()
        self.inline.selected = None
        self.history.redo()
        self.update()

    def reset_number(self):
        self.inline.finish()
        value = choose_number(self, next_number(self.operations))
        if value is not None:
            self.publish({"kind": "sequence", "number": value})

    def crop(self, marked=True):
        if marked:
            self.inline.finish()
        box = pixel_rect(self.selection, self.ratio).intersected(self.snapshot.rect())
        image = self.snapshot.copy(box).toImage()
        image.setDevicePixelRatio(1)
        if marked and self.operations:
            transformed = []
            for source in self.operations:
                op = {key: value for key, value in source.items() if key != "preview"}
                if "points" in op:
                    op["points"] = [(x*self.ratio-box.x(), y*self.ratio-box.y()) for x, y in op["points"]]
                if "rect" in op:
                    op["rect"] = list(pixel_rect(QRect(*op["rect"]), self.ratio).translated(-box.topLeft()).getRect())
                if "width" in op:
                    op["width"] *= self.ratio
                if "font_size" in op:
                    op["font_size"] *= self.ratio
                if "badge_size" in op:
                    op["badge_size"] *= self.ratio
                op["scale"] = self.ratio
                transformed.append(op)
            image = render_operations(image, transformed)
        x, y = self.monitor["rect"][:2]
        return image, box.translated(x, y)

    def commit(self, action):
        if self.selection.isEmpty() or self.completed or self.gesture:
            return
        image, physical = self.crop(action not in {"record", "longshot"})
        if image.isNull():
            return
        self.completed = True
        self.close()
        self.finished.emit(action, image, physical)

    def closeEvent(self, event):
        QApplication.instance().removeEventFilter(self)
        if not self.completed:
            self.completed = True
            self.cancelled.emit()
        event.accept()
