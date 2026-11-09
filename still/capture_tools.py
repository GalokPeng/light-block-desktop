"""Icon strip and per-tool settings for screen capture and image editing."""
from PySide6.QtCore import QSize, Qt
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (QCheckBox, QColorDialog, QComboBox, QFrame, QHBoxLayout, QLabel,
                               QSpinBox, QToolButton)

from .capture_icons import capture_icon
from .i18n import tr

TOOL_NAMES = {
    "rect": "矩形", "ellipse": "圆形", "pen": "画笔", "highlight": "高亮",
    "arrow": "箭头", "line": "直线", "text": "文字", "number": "序号",
    "mosaic": "框选马赛克", "mosaic_brush": "涂抹马赛克", "erase": "橡皮擦", "crop": "裁剪",
}
GROUPS = (("rect", "ellipse"), ("pen", "highlight"), ("line", "arrow"),
          ("text",), ("number",), ("mosaic", "mosaic_brush"), ("erase",))
COLORS = (("#ff6868", "珊瑚红"), ("#ffd382", "暖金"), ("#a6e4ca", "薄荷绿"),
          ("#68adff", "海盐蓝"), ("#151515", "黑色"), ("#ffffff", "白色"))
STROKE_SIZES = (1, 2, 3, 4, 6, 8, 10, 12, 16, 20, 24, 32, 36, 48, 64, 80)
NUMBER_SIZES = (12, 16, 20, 22, 26, 32, 40, 48, 60, 72, 90, 120, 160)
DOCK_STYLE = """
QFrame#captureStrip { background:#191c22; border:1px solid #3a414d; border-radius:9px; }
QFrame#toolDivider { background:#39414c; }
QToolButton { background:transparent; border:0; border-radius:6px; padding:2px; color:#f3f6ff; }
QToolButton:hover { background:#343c49; }
QToolButton:checked { background:#274d7c; }
QToolButton#confirmCapture { background:#2d6bd2; }
QToolButton#confirmCapture:hover { background:#3b80eb; }
QLabel, QCheckBox { color:#f3f6ff; }
QComboBox, QSpinBox { color:#f3f6ff; background:#101217; border:1px solid #424a57;
                      border-radius:5px; padding:3px 5px; }
QComboBox QAbstractItemView { color:#f3f6ff; background:#232833; selection-background-color:#305784; }
"""


def draw_colors():
    return [(value, tr(name)) for value, name in COLORS]


def icon_button(key, title, callback, grouped=False):
    item = QToolButton()
    item.setIcon(capture_icon(key, grouped))
    item.setIconSize(QSize(29 if grouped else 21, 21))
    item.setFixedSize(37 if grouped else 32, 32)
    item.setToolTip(title)
    item.setAccessibleName(title)
    item.setCursor(Qt.CursorShape.PointingHandCursor)
    item.setFocusPolicy(Qt.FocusPolicy.NoFocus)
    item.clicked.connect(callback)
    return item


def strip():
    frame = QFrame()
    frame.setObjectName("captureStrip")
    row = QHBoxLayout(frame)
    row.setContentsMargins(5, 4, 5, 4)
    row.setSpacing(3)
    return frame, row


def divider(row):
    item = QFrame()
    item.setObjectName("toolDivider")
    item.setFixedSize(1, 20)
    row.addWidget(item)


def number_control(title, minimum, maximum, value):
    field = QSpinBox()
    field.setRange(minimum, maximum)
    field.setValue(value)
    field.setFixedWidth(46)
    field.setButtonSymbols(QSpinBox.ButtonSymbols.NoButtons)
    field.setAccessibleName(title)
    field.setToolTip(title)
    return field


def labeled(row, title, field):
    label = QLabel(title)
    label.setBuddy(field)
    row.addWidget(label)
    row.addWidget(field)
    return field


class ToolPalette:
    def __init__(self, row):
        self.color = COLORS[0][0]
        self.custom = QToolButton()
        self.custom.setFixedSize(28, 26)
        self.custom.setToolTip(tr("自定义颜色"))
        self.custom.setAccessibleName(tr("自定义颜色"))
        self.custom.clicked.connect(self.choose)
        row.addWidget(self.custom)
        for color, name in draw_colors():
            swatch = QToolButton()
            swatch.setFixedSize(21, 21)
            swatch.setToolTip(name)
            swatch.setAccessibleName(name)
            swatch.setStyleSheet(f"background:{color};border:1px solid #596472;border-radius:5px")
            swatch.clicked.connect(lambda checked=False, value=color: self.select(value))
            row.addWidget(swatch)
        self.select(self.color)

    def select(self, color):
        self.color = color
        self.custom.setStyleSheet(f"background:{color};border:2px solid #81acff;border-radius:6px")

    def choose(self):
        color = QColorDialog.getColor(QColor(self.color), self.custom.window(), tr("标注颜色"))
        if color.isValid():
            self.select(color.name())


class DrawingTools:
    def __init__(self, row, attach, select, reset_number, *, crop=False):
        self.attach = attach
        self.select = select
        self.reset_number = reset_number
        self.groups = GROUPS + ((("crop",),) if crop else ())
        self.buttons = {}
        self.choices = {}
        self.panels = {}
        self.settings = {}
        self.active = None
        for group in self.groups:
            first = group[0]
            grouped = len(group) > 1
            item = icon_button(first, tr(TOOL_NAMES[first]), lambda checked=False, key=first: self.select(self.choices[key]), grouped)
            item.setCheckable(True)
            row.addWidget(item)
            self.buttons[first] = item
            self.choices[first] = first

    def build_panel(self, group):
        key = group[0]
        panel, row = strip()
        options = {}
        if len(group) > 1:
            for tool in group:
                choice = icon_button(tool, tr(TOOL_NAMES[tool]), lambda checked=False, value=tool: self.select(value))
                choice.setCheckable(True)
                row.addWidget(choice)
                options[tool] = choice
            divider(row)
        if key == "rect":
            fill = QCheckBox(tr("填充"))
            fill.setToolTip(tr("填充形状"))
            row.addWidget(fill)
            options["fill"] = fill
        if key in {"rect", "line"}:
            dash = QComboBox()
            dash.setAccessibleName(tr("线型"))
            dash.addItem(tr("实线"), False)
            dash.addItem(tr("虚线"), True)
            row.addWidget(dash)
            options["dashed"] = dash
        if key in {"rect", "line", "pen", "mosaic"}:
            size = tr("画笔宽度") if key == "mosaic" else tr("粗细")
            options["width_label"] = QLabel(size)
            options["width"] = number_control(size, 1, 80, 24 if key == "mosaic" else 4)
            options["width_label"].setBuddy(options["width"])
            row.addWidget(options["width_label"])
            row.addWidget(options["width"])
        if key in {"rect", "mosaic"}:
            title = tr("圆角")
            radius = number_control(title, 0, 80, 0)
            options["radius"] = radius
            options["radius_label"] = QLabel(title)
            options["radius_label"].setBuddy(radius)
            row.addWidget(options["radius_label"])
            row.addWidget(radius)
        if key == "mosaic":
            title = tr("像素块")
            options["block_size"] = labeled(row, title, number_control(title, 6, 64, 14))
        else:
            if key == "number":
                title = tr("标记大小")
                options["badge_size"] = labeled(row, title, number_control(title, 12, 160, 22))
                row.addWidget(icon_button("reset", tr("重计序号…"), self.reset_number))
            divider(row)
            options["palette"] = ToolPalette(row)
        row.addStretch()
        self.attach(panel)
        self.panels[key], self.settings[key] = panel, options

    def activate(self, tool):
        self.active = next((group[0] for group in self.groups if tool in group), None)
        for group in self.groups:
            key = group[0]
            self.buttons[key].setChecked(key == self.active)
            if key != self.active:
                if key in self.panels:
                    self.panels[key].hide()
                continue
            self.choices[key] = tool
            title = tr(TOOL_NAMES[tool])
            button = self.buttons[key]
            button.setIcon(capture_icon(tool, len(group) > 1))
            button.setToolTip(title)
            button.setAccessibleName(title)
            if key in {"text", "erase", "crop"}:
                continue
            if key not in self.panels:
                self.build_panel(group)
            settings = self.settings[key]
            for option in group:
                if option in settings:
                    settings[option].setChecked(tool == option)
            if key == "rect":
                for option in ("radius", "radius_label"):
                    settings[option].setVisible(tool == "rect")
            if key == "mosaic":
                for option in ("width", "width_label"):
                    settings[option].setVisible(tool == "mosaic_brush")
                for option in ("radius", "radius_label"):
                    settings[option].setVisible(tool == "mosaic")
            self.panels[key].show()

    def operation(self, tool, point):
        settings = self.settings.get(self.active, {})
        operation = {"kind": tool, "points": [point]}
        if "palette" in settings:
            operation["color"] = settings["palette"].color
        for key in ("width", "radius", "block_size", "badge_size"):
            if key in settings:
                operation[key] = settings[key].value()
        if "fill" in settings:
            operation["fill"] = settings["fill"].isChecked()
        if "dashed" in settings:
            operation["dashed"] = settings["dashed"].currentData()
        return operation
