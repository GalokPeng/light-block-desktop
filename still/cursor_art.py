"""Original MIT cursor artwork. QImage-only rendering is safe in a worker.

Stream: rounded filled silhouettes. Outline: hollow faceted silhouettes.
Pixel: stepped silhouettes without smoothing. No external artwork or fonts.
"""
import json
import math
from pathlib import Path
import struct

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QImage, QPainter, QPainterPath, QPen

REVISION = 2
DEFAULT_COLOR = "#d9f2ed"
THEMES = ("stream", "outline", "pixel")
ROLES = ("normal", "help", "working", "busy", "cross", "text", "hand", "unavailable",
         "vertical", "horizontal", "diagonal1", "diagonal2", "move", "alternate", "pin", "person", "pen")
SIZES = (32, 48, 64, 96, 128)


def polygon(points):
    path = QPainterPath(QPointF(*points[0]))
    for point in points[1:]:
        path.lineTo(*point)
    path.closeSubpath()
    return path


def render(theme, role, size, frame=0, color=DEFAULT_COLOR):
    if theme not in THEMES or role not in ROLES:
        raise ValueError("unknown cursor")
    ink = QColor(color)
    if not ink.isValid():
        raise ValueError("invalid cursor color")
    pixel, hollow = theme == "pixel", theme == "outline"
    edge = QColor("#15212c") if ink.lightnessF() > .48 else QColor("#f3f9ff")
    fill = QColor(15, 24, 36, 100) if hollow else QColor(ink)
    image = QImage(32 if pixel else size, 32 if pixel else size, QImage.Format.Format_ARGB32)
    image.fill(Qt.GlobalColor.transparent)
    p = QPainter(image)
    p.setRenderHint(QPainter.RenderHint.Antialiasing, not pixel)
    if not pixel:
        p.scale(size / 32, size / 32)
    cap = Qt.PenCapStyle.SquareCap if pixel else Qt.PenCapStyle.RoundCap
    join = Qt.PenJoinStyle.MiterJoin if pixel or hollow else Qt.PenJoinStyle.RoundJoin

    def shape(path, width=1.25):
        p.setBrush(fill)
        p.setPen(QPen(edge, 3.5 if hollow else 2 if pixel else width, Qt.PenStyle.SolidLine, cap, join))
        p.drawPath(path)
        if hollow:
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.setPen(QPen(ink, 1.5, Qt.PenStyle.SolidLine, cap, join))
            p.drawPath(path)

    def stroke(path, width=1.6):
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.setPen(QPen(edge, width+2, Qt.PenStyle.SolidLine, cap, join))
        p.drawPath(path)
        p.setPen(QPen(ink, width, Qt.PenStyle.SolidLine, cap, join))
        p.drawPath(path)

    hotspot = (16, 16)
    if pixel:
        arrow = polygon([(4,2),(6,2),(6,4),(8,4),(8,6),(10,6),(10,8),(12,8),(12,10),
                         (14,10),(14,12),(16,12),(16,14),(18,14),(18,16),(12,16),(12,20),
                         (14,20),(14,24),(16,24),(16,28),(12,28),(12,24),(10,24),(10,20),
                         (8,20),(8,18),(6,18),(6,20),(4,20)])
    elif hollow:
        arrow = polygon([(4,3),(25,19),(16,19),(12,29),(9,20),(4,24)])
    else:
        arrow = QPainterPath(QPointF(4,3))
        arrow.cubicTo(3,1,2,2,2.5,5); arrow.lineTo(4,25); arrow.quadTo(4.2,27,5.7,25.6)
        arrow.lineTo(11,20); arrow.lineTo(15,28); arrow.quadTo(15.7,29.7,17.2,29)
        arrow.lineTo(19,28); arrow.quadTo(20.2,27.5,19.5,26)
        arrow.lineTo(15,17); arrow.lineTo(24,17); arrow.quadTo(26.5,17,24.5,15.2); arrow.closeSubpath()
    if role in ("normal", "help", "working", "pin", "person"):
        hotspot = (4,2) if pixel else (4,3)
        p.save()
        if role != "normal":
            p.scale(.75, .75); hotspot = tuple(round(v*.75) for v in hotspot)
        shape(arrow); p.restore()
    if role in ("busy", "working"):
        cx, cy, r = (24,24,5.5) if role == "working" else (16,16,10)
        for n in range(8 if pixel else 12):
            count = 8 if pixel else 12; angle = n*math.tau/count-math.pi/2
            tint = QColor(ink); tint.setAlpha(55+round(((n-round(frame*count/12)) % count)/(count-1)*200))
            p.setBrush(tint); p.setPen(Qt.PenStyle.NoPen)
            x,y = cx+math.cos(angle)*r, cy+math.sin(angle)*r
            if pixel: p.drawRect(QRectF(round(x/2)*2-1, round(y/2)*2-1, 3,3))
            elif hollow: p.drawPath(polygon([(x,y-1.8),(x+1.4,y),(x,y+1.8),(x-1.4,y)]))
            else: p.drawEllipse(QPointF(x,y),1.5,1.5)
    elif role == "help":
        path = QPainterPath(QPointF(21,7))
        if pixel:
            for point in ((21,4),(28,4),(28,10),(24,10),(24,14)): path.lineTo(*point)
        else:
            path.cubicTo(20,1,30,1,28,8); path.cubicTo(27,10,24,10,24,14)
        stroke(path); p.drawPoint(QPointF(24,18))
    elif role == "cross":
        path = QPainterPath()
        for a,b in (((16,3),(16,11)),((16,21),(16,29)),((3,16),(11,16)),((21,16),(29,16))):
            path.moveTo(*a); path.lineTo(*b)
        stroke(path,2 if pixel else 1)
        shape(polygon([(16,12),(20,16),(16,20),(12,16)]) if hollow else polygon([(14,14),(18,14),(18,18),(14,18)]), .8)
    elif role == "text":
        path = QPainterPath()
        if hollow:
            for x in (14,18): path.moveTo(x,5); path.lineTo(x,27)
            for y in (5,27): path.moveTo(10,y); path.lineTo(22,y)
        elif pixel:
            shape(polygon([(10,4),(22,4),(22,8),(18,8),(18,24),(22,24),(22,28),(10,28),(10,24),(14,24),(14,8),(10,8)]))
        else:
            path.moveTo(11,5); path.quadTo(16,7,21,5); path.moveTo(16,6); path.lineTo(16,26)
            path.moveTo(11,27); path.quadTo(16,25,21,27)
        stroke(path,1.3)
    elif role in ("vertical", "horizontal", "diagonal1", "diagonal2", "move"):
        p.save(); p.translate(16,16)
        p.rotate({"vertical":0,"horizontal":90,"diagonal1":-45,"diagonal2":45,"move":0}[role])
        if pixel:
            path = polygon([(-2,-12),(2,-12),(2,-10),(4,-10),(4,-8),(6,-8),(6,-6),(2,-6),
                            (2,6),(6,6),(6,8),(4,8),(4,10),(2,10),(2,12),(-2,12),(-2,10),
                            (-4,10),(-4,8),(-6,8),(-6,6),(-2,6),(-2,-6),(-6,-6),(-6,-8),(-4,-8),(-4,-10),(-2,-10)])
        elif hollow:
            path = polygon([(0,-12),(-5,-6),(-2,-6),(-2,6),(-5,6),(0,12),(5,6),(2,6),(2,-6),(5,-6)])
        else:
            path = QPainterPath(QPointF(-4,-7)); path.lineTo(0,-12); path.lineTo(4,-7)
            path.moveTo(0,-11); path.lineTo(0,11); path.moveTo(-4,7); path.lineTo(0,12); path.lineTo(4,7)
        for _ in range(2 if role == "move" else 1):
            shape(path) if pixel or hollow else stroke(path,2.5); p.rotate(90)
        p.restore()
    elif role == "unavailable":
        path = polygon([(10,5),(22,5),(28,11),(28,21),(22,27),(10,27),(4,21),(4,11)]) if pixel else QPainterPath()
        if not pixel: path.addEllipse(QRectF(5,5,22,22))
        stroke(path,2); path=QPainterPath(QPointF(9,23)); path.lineTo(23,9); stroke(path,2)
    elif role == "alternate":
        hotspot = (16,3)
        shape(polygon([(16,3),(5,15),(12,15),(12,29),(20,29),(20,15),(27,15)]))
        if hollow:
            path=QPainterPath(QPointF(13,13)); path.lineTo(19,13); stroke(path,1)
    elif role == "hand":
        hotspot = (14,4)
        if pixel:
            path = polygon([(12,4),(16,4),(16,14),(20,14),(20,12),(24,12),(24,16),(28,16),
                            (28,24),(26,24),(26,28),(14,28),(14,26),(10,26),(10,22),(6,22),(6,18),(10,18),(12,20)])
        elif hollow:
            path = polygon([(12,18),(12,4),(15,2),(18,4),(18,13),(22,11),(26,14),(29,19),(26,29),(15,29),(5,20),(7,16)])
        else:
            path=QPainterPath(QPointF(11,19)); path.lineTo(11,6); path.cubicTo(11,2,16,2,16,6); path.lineTo(16,13)
            path.cubicTo(18,10,21,12,21,15); path.cubicTo(23,12,26,14,26,17)
            path.cubicTo(30,17,28,24,26,29); path.lineTo(14,29); path.lineTo(5,21)
            path.cubicTo(2,17,7,15,11,19); path.closeSubpath()
        shape(path)
        if hollow:
            path=QPainterPath(QPointF(17,18)); path.lineTo(23,18); stroke(path,1)
    elif role in ("pin", "person"):
        path=QPainterPath()
        if role == "person":
            if pixel: path.addRect(QRectF(22,18,6,6)); path.addRect(QRectF(20,26,10,4))
            else: path.addEllipse(QRectF(22,18,6,6)); path.addRoundedRect(QRectF(20,25,10,5),1 if hollow else 2,2)
        else: path=polygon([(21,18),(29,18),(27,23),(30,26),(25,26),(23,31),(23,26),(19,26),(22,23)])
        shape(path,1)
    elif role == "pen":
        hotspot=(6,27)
        points = [(6,28),(6,22),(10,22),(10,18),(14,18),(14,14),(18,14),(18,10),(22,10),(22,6),(26,6),
                  (26,10),(28,10),(28,14),(24,14),(24,18),(20,18),(20,22),(16,22),(16,26),(10,26),(10,28)] if pixel else [(6,27),(9,19),(23,5),(28,10),(14,24)]
        shape(polygon(points))
    p.end()
    if pixel and size != 32:
        image = image.scaled(size,size,Qt.AspectRatioMode.IgnoreAspectRatio,Qt.TransformationMode.FastTransformation)
    return image, tuple(round(v*size/32) for v in hotspot)


def cur(theme, role, frame=0, sizes=SIZES, color=DEFAULT_COLOR):
    images=[]
    for size in sizes:
        image,hotspot=render(theme,role,size,frame,color); raw=bytes(image.constBits())
        rows=[raw[y*image.bytesPerLine():y*image.bytesPerLine()+size*4] for y in range(size-1,-1,-1)]
        pixels=b''.join(rows); stride=((size+31)//32)*4; mask=bytearray(stride*size)
        for y,row in enumerate(rows):
            for x in range(size):
                if row[x*4+3]==0: mask[y*stride+x//8]|=0x80>>(x%8)
        dib=struct.pack('<IiiHHIIiiII',40,size,size*2,1,32,0,len(pixels)+len(mask),0,0,0,0)+pixels+bytes(mask)
        images.append((size,hotspot,dib))
    offset=6+16*len(images); entries=[];data=[]
    for size,hotspot,dib in images:
        entries.append(struct.pack('<BBBBHHII',size,size,0,0,*hotspot,len(dib),offset));data.append(dib);offset+=len(dib)
    return struct.pack('<HHH',0,2,len(images))+b''.join(entries)+b''.join(data)


def chunk(name,data):
    return name+struct.pack('<I',len(data))+data+(b'\0' if len(data)%2 else b'')


def ani(theme,role,size=64,color=DEFAULT_COLOR):
    # Windows rejects multi-resolution CURs inside ANI. Host selects the DPI file.
    frames=b''.join(chunk(b'icon',cur(theme,role,n,sizes=(size,),color=color)) for n in range(12))
    data=b'ACON'+chunk(b'anih',struct.pack('<9I',36,12,12,0,0,32,1,5,1))+chunk(b'LIST',b'fram'+frames)
    return b'RIFF'+struct.pack('<I',len(data))+data


def preview(theme, color=DEFAULT_COLOR):
    image=QImage(300,70,QImage.Format.Format_ARGB32);image.fill(Qt.GlobalColor.transparent)
    painter=QPainter(image)
    for index,role in enumerate(('normal','text','hand','move','busy')):
        cursor,_=render(theme,role,48,color=color);painter.drawImage(8+index*58,11,cursor)
    painter.end(); return image


def write_scheme(target, theme, color=DEFAULT_COLOR):
    target=Path(target);target.mkdir(parents=True,exist_ok=True)
    for role in ROLES: (target/(role+'.cur')).write_bytes(cur(theme,role,color=color))
    for role in ('busy','working'):
        (target/(role+'.ani')).write_bytes(ani(theme,role,color=color))
        for size in SIZES: (target/f'{role}-{size}.ani').write_bytes(ani(theme,role,size,color))
    preview(theme,color).save(str(target/'preview.png'))
    (target/'scheme.json').write_text(json.dumps(dict(revision=REVISION,theme=theme,color=color)),encoding='utf-8')
