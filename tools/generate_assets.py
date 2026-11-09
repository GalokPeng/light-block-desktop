"""Generate original demo artwork. Development only: PySide6, Pillow, ffmpeg."""
import math
from pathlib import Path
import subprocess
import tempfile

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QGuiApplication, QImage, QLinearGradient, QPainter, QPainterPath, QRadialGradient

DEST = Path(__file__).resolve().parent.parent / "assets"


def gradient(top, bottom, height=1440):
    g = QLinearGradient(0, 0, 0, height)
    g.setColorAt(0, QColor(top))
    g.setColorAt(1, QColor(bottom))
    return g


def artwork(name, width=2560, height=1440, phase=0.0):
    image = QImage(width, height, QImage.Format.Format_RGB32)
    p = QPainter(image)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    p.scale(width/2560, height/1440)
    p.setPen(Qt.PenStyle.NoPen)
    if name == "dunes":
        p.fillRect(QRectF(0, 0, 2560, 1440), gradient("#253a53", "#bf7c8d"))
        halo = QRadialGradient(QPointF(1675, 590), 760)
        halo.setColorAt(0, QColor(244, 189, 167, 100))
        halo.setColorAt(1, QColor(244, 189, 167, 0))
        p.fillRect(QRectF(0, 0, 2560, 1440), halo)
        p.setBrush(gradient("#f5d7bd", "#cf9e99"))
        p.drawEllipse(QRectF(1610, 350, 246, 246))
        shapes = [
            ("#907d9b", "#47445f", (0, 910, 480, 570, 880, 950, 1450, 760, 2000, 690, 2560, 790)),
            ("#9b7183", "#4c405e", (0, 1130, 600, 1010, 1070, 620, 1670, 1000, 2210, 1040, 2560, 890)),
            ("#4b4e6b", "#182d45", (0, 1270, 510, 870, 1170, 1210, 1740, 1090, 2180, 960, 2560, 1170)),
            ("#213b53", "#101f35", (0, 1400, 680, 1200, 1070, 1330, 1600, 1280, 2170, 1200, 2560, 1390)),
        ]
        for top, bottom, s in shapes:
            path = QPainterPath(QPointF(s[0], s[1]))
            path.cubicTo(s[2], s[3], s[4], s[5], s[6], s[7])
            path.cubicTo(s[8], s[9], s[10], s[11], 2800, 1200)
            path.lineTo(2800, 1500)
            path.lineTo(-100, 1500)
            path.closeSubpath()
            p.fillPath(path, gradient(top, bottom))
    elif name == "mountain":
        p.fillRect(QRectF(0, 0, 2560, 1440), gradient("#9cbdb2", "#1e504e"))
        p.setBrush(QColor("#d3ddbc"))
        p.drawEllipse(QRectF(590, 260, 180, 180))
        for n, color in enumerate(("#719b8f", "#507f78", "#2e6364", "#154449", "#102d37")):
            path = QPainterPath(QPointF(-100, 880+n*130))
            path.cubicTo(160, 800+n*110, 370, 300+n*160, 700, 620+n*150)
            path.cubicTo(1100, 1040+n*70, 1400, 300+n*150, 1830, 620+n*140)
            path.cubicTo(2150, 870+n*120, 2330, 620+n*150, 2700, 670+n*160)
            path.lineTo(2700, 1500)
            path.lineTo(-100, 1500)
            path.closeSubpath()
            p.fillPath(path, QColor(color))
    else:
        p.fillRect(QRectF(0, 0, 2560, 1440), QColor("#07152c"))
        for cx, cy, radius, color in ((1050, 660, 1100, "#123b77"), (1550, 590, 780, "#5b58a2"), (1380, 730, 560, "#3492b0")):
            cx += math.sin(phase)*110
            cy += math.cos(phase)*80
            g = QRadialGradient(QPointF(cx, cy), radius)
            c = QColor(color)
            g.setColorAt(0, c)
            c.setAlpha(0)
            g.setColorAt(1, c)
            p.fillRect(QRectF(0, 0, 2560, 1440), g)
        for n in range(8):
            path = QPainterPath(QPointF(-100, 820+n*80))
            path.cubicTo(650, 370+n*60+math.sin(phase)*60, 1190, 1220-n*50, 2700, 240+n*65)
            p.setPen(QPen(QColor(120, 212, 233, 30-n*3), 2))
            p.drawPath(path)
    p.end()
    return image


if __name__ == "__main__":
    from PySide6.QtGui import QPen
    from PIL import Image
    app = QGuiApplication([])
    DEST.mkdir(exist_ok=True)
    for name in ("dunes", "mountain", "glow"):
        artwork(name).save(str(DEST / (name+".png")))
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        frames = []
        for n in range(96):
            frame = temp / f"frame-{n:03d}.png"
            artwork("glow", 1280, 720, n/96*2*math.pi).save(str(frame))
            if n % 3 == 0:
                with Image.open(frame) as image:
                    frames.append(image.resize((480, 270), Image.Resampling.LANCZOS).convert("P", palette=Image.Palette.ADAPTIVE))
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-framerate", "24", "-i", str(temp / "frame-%03d.png"),
                        "-c:v", "libx264", "-preset", "slow", "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(DEST / "motion.mp4")], check=True)
        frames[0].save(DEST / "ripple.gif", save_all=True, append_images=frames[1:], duration=125, loop=0, optimize=True)
    from still.app import make_icon
    # make_icon requires QApplication (QGuiApplication also supports QPixmap).
    pix = make_icon().pixmap(256, 256)
    path = DEST / "icon.png"
    pix.save(str(path))
    with Image.open(path) as icon:
        icon.save(DEST / "still.ico", sizes=[(16,16), (32,32), (48,48), (64,64), (128,128), (256,256)])
    print("Generated demo assets", DEST)
