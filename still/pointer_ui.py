from PySide6.QtCore import QPointF, QRectF, QSignalBlocker, QSize, Qt, QTimer
from PySide6.QtGui import QColor, QIcon, QPainter, QPen, QPixmap
from PySide6.QtWidgets import QButtonGroup, QColorDialog, QComboBox, QGridLayout, QHBoxLayout, QSizePolicy, QSlider, QToolButton, QVBoxLayout, QWidget

from .i18n import tr
from .cursor_art import preview
from .ui import Toggle, button, label


def system_icon():
    pixmap = QPixmap(84,84)
    pixmap.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.setPen(QPen(QColor("#80a995"),5,Qt.PenStyle.SolidLine,Qt.PenCapStyle.RoundCap,Qt.PenJoinStyle.RoundJoin))
    painter.drawArc(QRectF(20,20,44,44),45*16,285*16)
    painter.drawPolyline([QPointF(68,18),QPointF(68,34),QPointF(52,34)])
    painter.end()
    return QIcon(pixmap)


class PointerSettings(QWidget):
    def __init__(self, window):
        super().__init__()
        self.window = window
        self.manager = None
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(12)
        layout.addWidget(label(tr("三种不同轮廓，覆盖文本、链接、缩放与忙碌动画；退出时恢复系统指针。"), "muted", True))
        grid = QGridLayout()
        self.buttons = {}
        group = QButtonGroup(self)
        for index, (key, name) in enumerate((("system", tr("系统方案")), ("stream", tr("流线 · 圆润实心")),
                                             ("outline", tr("棱镜 · 几何镂空")), ("pixel", tr("像素 · 阶梯复古")))):
            choice = QToolButton()
            choice.setObjectName("cursorTheme")
            choice.setCheckable(True)
            choice.setMinimumHeight(84)
            choice.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextUnderIcon)
            choice.setText(name)
            choice.setAccessibleName(tr("鼠标主题")+" · "+name)
            choice.setCursor(Qt.CursorShape.PointingHandCursor)
            choice.setIcon(QIcon(QPixmap.fromImage(preview(key,window.store.settings.cursor_color))) if key!="system" else
                           system_icon())
            choice.setIconSize(QSize(180,42))
            choice.setSizePolicy(QSizePolicy.Policy.Expanding,QSizePolicy.Policy.Preferred)
            choice.clicked.connect(lambda _checked=False, k=key:self.choose(k))
            group.addButton(choice)
            self.buttons[key]=choice
            grid.addWidget(choice,index//2,index%2)
        grid.setColumnStretch(0,1);grid.setColumnStretch(1,1)
        layout.addLayout(grid)
        self.status=label("","muted",True)
        layout.addWidget(self.status)
        row=QHBoxLayout()
        row.addWidget(label(tr("指针颜色")),1)
        self.cursor_color=button("",lambda:self.choose_color("cursor"))
        self.cursor_color.setMinimumWidth(120)
        row.addWidget(self.cursor_color)
        layout.addLayout(row)
        self.size_save=QTimer(self,singleShot=True,interval=250)
        self.size_save.timeout.connect(self.save_size)
        self.cursor_size_label=label("")
        layout.addWidget(self.cursor_size_label)
        self.cursor_size=QSlider(Qt.Orientation.Horizontal)
        self.cursor_size.setRange(75,200)
        self.cursor_size.setSingleStep(5)
        self.cursor_size.setPageStep(25)
        self.cursor_size.setAccessibleName(tr("指针大小"))
        self.cursor_size.valueChanged.connect(self.size_changed)
        layout.addWidget(self.cursor_size)
        layout.addWidget(label(tr("大小按系统 DPI 缩放；系统方案下先保存，选择美化指针后生效。"),"muted",True))
        row=QHBoxLayout()
        row.addWidget(label(tr("鼠标粒子特效")),1)
        self.particles=Toggle(window.store.settings.cursor_particles)
        self.particles.setAccessibleName(tr("鼠标粒子特效"))
        self.particles.toggled.connect(self.particles_changed)
        row.addWidget(self.particles)
        layout.addLayout(row)
        row=QHBoxLayout()
        row.addWidget(label(tr("粒子样式")),1)
        self.particle_style=QComboBox()
        for key,name in (("dots",tr("点点")),("fall",tr("飘落")),("glow",tr("光芒"))):
            self.particle_style.addItem(name,key)
        self.particle_style.setAccessibleName(tr("粒子样式"))
        self.particle_style.currentIndexChanged.connect(lambda:self.service().set_particle_options(style=self.particle_style.currentData()))
        row.addWidget(self.particle_style)
        self.particle_color=button("",lambda:self.choose_color("particle"))
        self.particle_color.setMinimumWidth(120)
        self.particle_color.setAccessibleName(tr("粒子颜色"))
        row.addWidget(self.particle_color)
        layout.addLayout(row)
        self.effect_hint=label("","muted",True)
        layout.addWidget(self.effect_hint)
        layout.addWidget(label(tr("指针与粒子可自由组合、独立调色。停止移动后自然淡出并停绘，关闭后释放窗口与监听。"),"muted",True))
        self.preview_color=window.store.settings.cursor_color
        if getattr(window.controller,"pointer_style",None) is not None:
            self.service()
        self.refresh()

    def service(self):
        if self.manager is None:
            self.manager=self.window.controller.get_pointer_style()
            self.manager.changed.connect(self.refresh)
        return self.manager

    def choose(self,key):
        self.service().set_theme(key)
        self.refresh()

    def particles_changed(self,enabled):
        self.service().set_particles(enabled)

    def size_changed(self,value):
        self.cursor_size_label.setText(tr("指针大小 · {0}%").format(value))
        self.size_save.start()

    def save_size(self):
        manager=self.service()
        if manager.pending is not None or manager.process in manager.intentional:
            self.size_save.start()
        else:
            manager.set_size(self.cursor_size.value())

    def choose_color(self,kind):
        settings=self.window.store.settings
        value=settings.cursor_color if kind=="cursor" else settings.particle_color
        color=QColorDialog.getColor(QColor(value),self,tr("指针颜色") if kind=="cursor" else tr("粒子颜色"))
        if color.isValid():
            if kind=="cursor": self.service().set_color(color.name())
            else: self.service().set_particle_options(color=color.name())

    def refresh(self):
        from .pointer_style import pointer_host
        pending=self.manager.pending if self.manager else None
        stopping=bool(self.manager and self.manager.process in self.manager.intentional)
        actual=self.manager.actual_theme if self.manager else self.window.store.settings.cursor_theme
        for key,button in self.buttons.items():
            with QSignalBlocker(button):button.setChecked(key==(pending or actual))
            button.setEnabled(pending is None and not stopping and (key=="system" or pointer_host() is not None))
        with QSignalBlocker(self.particles):self.particles.setChecked(self.window.store.settings.cursor_particles)
        settings=self.window.store.settings
        if not self.cursor_size.isSliderDown() and not self.size_save.isActive():
            size=self.manager.pending_scale if pending and self.manager.pending_scale else settings.cursor_scale
            with QSignalBlocker(self.cursor_size):self.cursor_size.setValue(size)
            self.cursor_size_label.setText(tr("指针大小 · {0}%").format(size))
        self.cursor_color.setEnabled(pending is None and not stopping)
        for widget,color in ((self.cursor_color,settings.cursor_color),(self.particle_color,settings.particle_color)):
            widget.setText(color.upper())
            widget.setStyleSheet("QPushButton {border:2px solid "+color+";}")
        with QSignalBlocker(self.particle_style):
            self.particle_style.setCurrentIndex(self.particle_style.findData(settings.particle_style))
        if settings.cursor_color!=self.preview_color:
            for key,widget in self.buttons.items():
                if key!="system":widget.setIcon(QIcon(QPixmap.fromImage(preview(key,settings.cursor_color))))
            self.preview_color=settings.cursor_color
        hints={"dots":tr("点点：短尾迹逐渐变小、淡出。"),"fall":tr("飘落：细微荧光轻柔下落，淡入后逐渐消散。"),"glow":tr("光芒：沿当前指针的外轮廓流动，停下后柔和消散。")}
        self.effect_hint.setText(hints[settings.particle_style])
        self.status.setText(tr("正在应用鼠标主题…") if pending else tr("当前使用 Windows 系统指针。") if actual=="system" else
                            tr("鼠标主题已应用，退出时恢复系统方案。"))
        if pointer_host() is None:self.status.setText(tr("鼠标主题组件不可用，重新安装后可用。"))
