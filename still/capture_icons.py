"""Small line icons drawn at twice their logical size."""
from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QIcon, QPainter, QPen, QPixmap, QPolygonF


LINES = {
    "move": [[(12,3),(12,21)],[(3,12),(21,12)],[(8,7),(12,3),(16,7)],[(8,17),(12,21),(16,17)],
             [(7,8),(3,12),(7,16)],[(17,8),(21,12),(17,16)]],
    "smart": [[(3,8),(3,3),(8,3)],[(16,3),(21,3),(21,8)],[(21,16),(21,21),(16,21)],[(8,21),(3,21),(3,16)],
              [(8,13),(11,16),(16,9)]],
    "full": [[(10,4),(4,4),(4,10)],[(14,4),(20,4),(20,10)],[(20,14),(20,20),(14,20)],[(10,20),(4,20),(4,14)]],
    "pen": [[(4,16),(8,9),(12,15),(19,5)],[(15,5),(19,5),(20,9)]],
    "highlight": [[(5,15),(15,5),(20,10),(10,20),(5,20),(5,15)],[(8,12),(13,17)],[(3,22),(15,22)]],
    "line": [[(4,19),(20,5)]],
    "arrow": [[(4,19),(20,5)],[(13,5),(20,5),(20,12)]],
    "text": [[(5,5),(19,5)],[(12,5),(12,20)],[(8,20),(16,20)]],
    "number": [[(10,9),(12,7),(12,17)]],
    "style": [[(4,7),(10,7)],[(14,7),(20,7)],[(4,17),(14,17)],[(18,17),(20,17)],[(10,4),(10,10)],[(17,14),(17,20)]],
    "record": [[(16,9),(21,6),(21,18),(16,15)]],
    "longshot": [[(8,4),(4,4),(4,8)],[(16,4),(20,4),(20,8)],[(20,16),(20,20),(16,20)],[(8,20),(4,20),(4,16)],
                 [(12,7),(12,17)],[(9,14),(12,17),(15,14)]],
    "pin": [[(13,3),(21,11),(17,11),(12,16),(12,19),(5,12),(8,12),(13,7),(13,3)],[(8,15),(3,21)]],
    "save": [[(12,3),(12,15)],[(7,10),(12,15),(17,10)],[(4,16),(4,21),(20,21),(20,16)]],
    "cancel": [[(5,5),(19,19)],[(5,19),(19,5)]],
    "copy": [[(8,4),(20,4),(20,16)]],
    "undo": [[(9,4),(4,9),(9,14)],[(4,9),(15,9),(19,11),(20,15),(18,19),(14,20),(8,20)]],
    "redo": [[(15,4),(20,9),(15,14)],[(20,9),(9,9),(5,11),(4,15),(6,19),(10,20),(16,20)]],
    "reset": [[(19,5),(19,10),(14,10)],[(19,10),(17,6),(13,4),(8,5),(5,8),(4,13),(6,17),(10,20),(15,19),(19,15)]],
}


def capture_icon(key):
    pixmap = QPixmap(48, 48)
    pixmap.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pixmap)
    painter.scale(2, 2)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    color = QColor("#f3f6ff")
    painter.setPen(QPen(color, 1.75, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin))
    for line in LINES.get(key, []):
        painter.drawPolyline(QPolygonF([QPointF(x, y) for x, y in line]))
    if key == "rect":
        painter.drawRoundedRect(QRectF(4, 5, 16, 14), 1.6, 1.6)
    elif key == "number":
        painter.drawEllipse(QRectF(3, 3, 18, 18))
    elif key == "mosaic":
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(color)
        for x, y in ((4,4),(12,4),(8,8),(16,8),(4,12),(12,12),(8,16),(16,16)):
            painter.drawRect(QRectF(x, y, 4, 4))
    elif key == "record":
        painter.drawRoundedRect(QRectF(3, 6, 13, 12), 2, 2)
    elif key == "copy":
        painter.drawRoundedRect(QRectF(4, 8, 12, 12), 1, 1)
    painter.end()
    pixmap.setDevicePixelRatio(2)
    return QIcon(pixmap)
