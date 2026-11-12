from PySide6.QtCore import QPointF, QRectF, QSignalBlocker, QSize, Qt
from PySide6.QtGui import QColor, QIcon, QPainter, QPen, QPixmap
from PySide6.QtWidgets import QButtonGroup, QGridLayout, QHBoxLayout, QSizePolicy, QToolButton, QVBoxLayout, QWidget

from .i18n import tr
from .model import ASSETS
from .ui import Toggle, label


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
        layout.addWidget(label(tr("三套简洁指针包含文本、链接、调整大小和忙碌动画。退出软件后恢复 Windows 指针方案。"), "muted", True))
        grid = QGridLayout()
        self.buttons = {}
        group = QButtonGroup(self)
        for index, (key, name) in enumerate((("system", tr("系统方案")), ("frost", tr("霜白")),
                                             ("graphite", tr("曜黑")), ("mint", tr("薄荷")))):
            button = QToolButton()
            button.setObjectName("cursorTheme")
            button.setCheckable(True)
            button.setMinimumHeight(84)
            button.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextUnderIcon)
            button.setText(name)
            button.setAccessibleName(tr("鼠标主题")+" · "+name)
            button.setCursor(Qt.CursorShape.PointingHandCursor)
            button.setIcon(QIcon(str(ASSETS/"cursors"/key/"preview.png")) if key!="system" else
                           system_icon())
            button.setIconSize(QSize(180,42))
            button.setSizePolicy(QSizePolicy.Policy.Expanding,QSizePolicy.Policy.Preferred)
            button.clicked.connect(lambda _checked=False, k=key:self.choose(k))
            group.addButton(button)
            self.buttons[key]=button
            grid.addWidget(button,index//2,index%2)
        grid.setColumnStretch(0,1);grid.setColumnStretch(1,1)
        layout.addLayout(grid)
        self.status=label("","muted",True)
        layout.addWidget(self.status)
        row=QHBoxLayout()
        row.addWidget(label(tr("鼠标粒子特效")),1)
        self.particles=Toggle(window.store.settings.cursor_particles)
        self.particles.setAccessibleName(tr("鼠标粒子特效"))
        self.particles.toggled.connect(self.particles_changed)
        row.addWidget(self.particles)
        layout.addLayout(row)
        layout.addWidget(label(tr("移动时显示轻量尾迹，停止后淡出并停绘。关闭后释放特效窗口与监听。"),"muted",True))
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

    def refresh(self):
        from .pointer_style import pointer_host
        pending=self.manager.pending if self.manager else None
        stopping=bool(self.manager and self.manager.process in self.manager.intentional)
        actual=self.manager.actual_theme if self.manager else self.window.store.settings.cursor_theme
        for key,button in self.buttons.items():
            with QSignalBlocker(button):button.setChecked(key==(pending or actual))
            button.setEnabled(pending is None and not stopping and (key=="system" or pointer_host() is not None))
        with QSignalBlocker(self.particles):self.particles.setChecked(self.window.store.settings.cursor_particles)
        self.status.setText(tr("正在应用鼠标主题…") if pending else tr("当前使用 Windows 系统指针。") if actual=="system" else
                            tr("鼠标主题已应用，退出时恢复系统方案。"))
        if pointer_host() is None:self.status.setText(tr("鼠标主题组件不可用，重新安装后可用。"))
