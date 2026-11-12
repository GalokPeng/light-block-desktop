"""Original vector-drawn Windows cursors; no external images or fonts."""
import json
import os
from pathlib import Path
import struct

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QGuiApplication, QImage, QPainter, QPainterPath, QPen

ROOT = Path(__file__).resolve().parents[1]
THEMES = {"frost": ("#fbfdff", "#263746", "#70a6cf"),
          "graphite": ("#293642", "#f3f8fd", "#9ec8e7"),
          "mint": ("#b7efd5", "#214d40", "#4ba587")}
ROLES = ("normal", "help", "working", "busy", "cross", "text", "hand", "unavailable",
         "vertical", "horizontal", "diagonal1", "diagonal2", "move", "alternate", "pin", "person", "pen")
SIZES = (32, 48, 64, 96, 128)


def polygon(points):
    path = QPainterPath(QPointF(*points[0]))
    for point in points[1:]: path.lineTo(QPointF(*point))
    path.closeSubpath()
    return path


def render(theme, role, size, frame=0):
    fill, edge, accent = map(QColor, THEMES[theme])
    image = QImage(size, size, QImage.Format.Format_ARGB32)
    image.fill(Qt.GlobalColor.transparent)
    p = QPainter(image)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    p.scale(size/32, size/32)

    def shape(path, width=1.35):
        p.setPen(QPen(edge, width, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin))
        p.setBrush(fill)
        p.drawPath(path)

    def stroke(path, width=1.8, color=None):
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.setPen(QPen(edge, width+1.6, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin))
        p.drawPath(path)
        p.setPen(QPen(color or fill, width, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin))
        p.drawPath(path)

    hotspot = (16, 16)
    arrow = polygon([(4,2),(4,25),(10,19),(15,29),(19,27),(14,17),(24,17)])
    if role in ("normal", "help", "working", "pin", "person"):
        hotspot=(4,2)
        p.save()
        if role!='normal': p.scale(.78,.78)
        shape(arrow)
        p.restore()
    if role in ('busy','working'):
        cx,cy,radius = (24,24,5.5) if role=='working' else (16,16,10)
        import math
        for n in range(12):
            angle=(n-3)*math.pi/6
            color=QColor(accent)
            color.setAlpha(50+int(((n-frame)%12)/11*205))
            p.setPen(QPen(color,2.5,Qt.PenStyle.SolidLine,Qt.PenCapStyle.RoundCap))
            p.drawLine(QPointF(cx+math.cos(angle)*(radius-3),cy+math.sin(angle)*(radius-3)),
                       QPointF(cx+math.cos(angle)*radius,cy+math.sin(angle)*radius))
    elif role=='help':
        path=QPainterPath(QPointF(21,8)); path.cubicTo(20,2,30,2,28,8); path.cubicTo(27,10,24,9,24,13)
        stroke(path,1.7)
        p.setPen(QPen(edge,3.4,Qt.PenStyle.SolidLine,Qt.PenCapStyle.RoundCap));p.drawPoint(QPointF(24,17))
        p.setPen(QPen(fill,1.8,Qt.PenStyle.SolidLine,Qt.PenCapStyle.RoundCap));p.drawPoint(QPointF(24,17))
    elif role=='cross':
        path=QPainterPath()
        for a,b in [((16,3),(16,12)),((16,20),(16,29)),((3,16),(12,16)),((20,16),(29,16))]:
            path.moveTo(*a);path.lineTo(*b)
        stroke(path,1)
        p.setPen(QPen(edge,1));p.setBrush(fill);p.drawEllipse(QRectF(14.5,14.5,3,3))
    elif role=='text':
        path=QPainterPath();path.moveTo(11,5);path.lineTo(21,5);path.moveTo(16,5);path.lineTo(16,27);path.moveTo(11,27);path.lineTo(21,27)
        stroke(path,1.3)
    elif role in ('vertical','horizontal','diagonal1','diagonal2','move'):
        p.save();p.translate(16,16)
        p.rotate({'vertical':0,'horizontal':90,'diagonal1':-45,'diagonal2':45,'move':0}[role])
        path=polygon([(0,-13),(-5,-7),(-2,-7),(-2,7),(-5,7),(0,13),(5,7),(2,7),(2,-7),(5,-7)])
        shape(path,1.2)
        if role=='move': p.rotate(90);shape(path,1.2)
        p.restore()
    elif role=='unavailable':
        path=QPainterPath();path.addEllipse(QRectF(6,6,20,20));path.moveTo(9,23);path.lineTo(23,9)
        stroke(path,2.2)
    elif role=='alternate':
        hotspot=(16,3);shape(polygon([(16,3),(5,15),(12,15),(12,29),(20,29),(20,15),(27,15)]))
    elif role=='hand':
        hotspot=(14,4)
        path=QPainterPath(QPointF(11,19));path.lineTo(11,6);path.cubicTo(11,2,16,2,16,6);path.lineTo(16,13)
        path.cubicTo(18,10,21,12,21,15);path.cubicTo(23,12,26,14,26,17);path.cubicTo(30,17,28,24,26,29)
        path.lineTo(14,29);path.lineTo(5,21);path.cubicTo(2,17,7,15,11,19);path.closeSubpath();shape(path)
    elif role in ('pin','person'):
        p.setPen(QPen(edge,1.1));p.setBrush(fill)
        if role=='person':
            p.drawEllipse(QRectF(21,18,6,6));p.drawRoundedRect(QRectF(19,25,10,5),2,2)
        else:
            path=polygon([(21,18),(29,18),(27,23),(30,26),(25,26),(23,31),(23,26),(19,26),(22,23)])
            shape(path,1.1)
    elif role=='pen':
        hotspot=(6,27);shape(polygon([(6,27),(9,19),(23,5),(28,10),(14,24)]));p.setPen(QPen(edge,1));p.drawLine(9,19,14,24)
    p.end()
    return image, (round(hotspot[0]*size/32),round(hotspot[1]*size/32))


def cur(theme, role, frame=0, sizes=SIZES):
    images=[]
    for size in sizes:
        image,hotspot=render(theme,role,size,frame)
        raw=bytes(image.constBits())
        rows=[raw[y*image.bytesPerLine():y*image.bytesPerLine()+size*4] for y in range(size-1,-1,-1)]
        pixels=b''.join(rows)
        stride=((size+31)//32)*4
        mask=bytearray(stride*size)
        for y,row in enumerate(rows):
            for x in range(size):
                if row[x*4+3]==0: mask[y*stride+x//8]|=0x80>>(x%8)
        dib=struct.pack('<IiiHHIIiiII',40,size,size*2,1,32,0,len(pixels)+len(mask),0,0,0,0)+pixels+bytes(mask)
        images.append((size,hotspot,dib))
    offset=6+16*len(images); entries=[];data=[]
    for size,hotspot,dib in images:
        entries.append(struct.pack('<BBBBHHII',size,size,0,0,*hotspot,len(dib),offset));data.append(dib);offset+=len(dib)
    return struct.pack('<HHH',0,2,len(images))+b''.join(entries)+b''.join(data)


def chunk(name,data): return name+struct.pack('<I',len(data))+data+(b'\0' if len(data)%2 else b'')


def ani(theme,role,size=64):
    # Windows' ANI reader rejects multi-resolution CUR frames. Keep one size
    # per animation and let the host choose the DPI variant before loading.
    frames=b''.join(chunk(b'icon',cur(theme,role,n,sizes=(size,))) for n in range(12))
    header=struct.pack('<9I',36,12,12,0,0,32,1,5,1)
    data=b'ACON'+chunk(b'anih',header)+chunk(b'LIST',b'fram'+frames)
    return b'RIFF'+struct.pack('<I',len(data))+data


def main():
    app=QGuiApplication([])
    root=ROOT/'assets/cursors'
    for theme in THEMES:
        target=root/theme;target.mkdir(parents=True,exist_ok=True)
        for role in ROLES: (target/(role+'.cur')).write_bytes(cur(theme,role))
        for role in ('busy','working'):
            (target/(role+'.ani')).write_bytes(ani(theme,role))
            for size in SIZES:
                (target/f'{role}-{size}.ani').write_bytes(ani(theme,role,size))
        preview=QImage(300,70,QImage.Format.Format_ARGB32);preview.fill(Qt.GlobalColor.transparent)
        painter=QPainter(preview)
        for index,role in enumerate(('normal','text','hand','move','busy')):
            image,_=render(theme,role,48);painter.drawImage(8+index*58,11,image)
        painter.end();preview.save(str(target/'preview.png'))
    (root/'manifest.json').write_text(json.dumps(dict(license='MIT',source='Original geometric artwork generated by tools/generate_cursors.py',themes=list(THEMES),roles=list(ROLES),sizes=SIZES,animated=['busy','working']),indent=2)+'\n',encoding='utf-8')
    print('Generated 3 original cursor themes, 17 roles, 5 sizes and animated busy states.')


if __name__=='__main__': main()
