"""Generate original demo artwork. Development only: PySide6, Pillow, ffmpeg."""
import math
from pathlib import Path
from random import Random
import subprocess
import sys
import tempfile

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QGuiApplication, QImage, QLinearGradient, QPainter, QPainterPath, QPen, QRadialGradient

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


def orbit_background():
    image = QImage(640, 360, QImage.Format.Format_RGB32)
    with QPainter(image) as painter:
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        sky = QLinearGradient(0, 10, 625, 355)
        for stop, color in ((0, "#080d1a"), (.51, "#10172e"), (1, "#201a38")):
            sky.setColorAt(stop, QColor(color))
        painter.fillRect(image.rect(), sky)
        haze = QRadialGradient(QPointF(409, 185), 235)
        haze.setColorAt(0, QColor(69, 92, 162, 94))
        haze.setColorAt(.62, QColor(50, 70, 125, 42))
        haze.setColorAt(1, QColor(25, 42, 83, 0))
        painter.fillRect(image.rect(), haze)
        stars = Random(421)
        painter.setPen(Qt.PenStyle.NoPen)
        for _ in range(120):
            x, y = stars.uniform(13, 627), stars.uniform(12, 348)
            radius = stars.choice((.35, .55, .8, 1.15))
            painter.setBrush(QColor(191, 208, 251, stars.randint(22, 109)))
            painter.drawEllipse(QPointF(x, y), radius, radius)
        painter.translate(405, 178)
        painter.rotate(-22)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        for rx, ry, color in ((161, 62, "#58436e"), (125, 43, "#416487"), (82, 73, "#3a4f71")):
            painter.setPen(QPen(QColor(color), .85))
            painter.drawEllipse(QRectF(-rx, -ry, 2*rx, 2*ry))
        painter.setPen(QPen(QColor("#4c638e"), .9))
        planet = QRadialGradient(QPointF(-22, -22), 89)
        for stop, color in ((0, "#5a75b0"), (.34, "#354a7c"), (.68, "#1c294f"), (1, "#10182b")):
            planet.setColorAt(stop, QColor(color))
        painter.setBrush(planet)
        painter.drawEllipse(QPointF(0, 0), 54, 54)
    return image


def orbit_frame(background, phase):
    frame = background.copy()
    with QPainter(frame) as painter:
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.translate(405, 178)
        painter.rotate(-22)
        for rx, ry, direction, offset, tint in (
            (161, 62, 1, 1.8, (255, 169, 198)), (125, 43, -1, .7, (128, 225, 255)),
        ):
            angle = direction * phase + offset
            for step in range(17, 0, -1):
                a = angle - direction * step * .047
                b = a + direction * .05
                painter.setPen(QPen(QColor(*tint, 214 - step * 11), 1 + (17-step)*.052,
                                    Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
                painter.drawLine(QPointF(rx*math.cos(a), ry*math.sin(a)), QPointF(rx*math.cos(b), ry*math.sin(b)))
            position = QPointF(rx*math.cos(angle), ry*math.sin(angle))
            glow = QRadialGradient(position, 9)
            glow.setColorAt(0, QColor(252, 248, 255, 255))
            glow.setColorAt(.29, QColor(*tint, 230))
            glow.setColorAt(1, QColor(*tint, 0))
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(glow)
            painter.drawEllipse(position, 9, 9)
    return frame


def generate_gif():
    background = orbit_background()
    with tempfile.TemporaryDirectory() as folder:
        directory = Path(folder)
        for index in range(100):
            orbit_frame(background, math.tau * index / 100).save(str(directory / f"orbit-{index:03d}.png"))
        source = ["-framerate", "25", "-i", str(directory / "orbit-%03d.png")]
        ffmpeg = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y"]
        palette = str(directory / "palette.png")
        subprocess.run([*ffmpeg, *source, "-vf", "palettegen=stats_mode=diff", "-frames:v", "1", palette], check=True)
        subprocess.run([*ffmpeg, *source, "-i", palette, "-lavfi", "paletteuse=dither=bayer:diff_mode=rectangle",
                        "-loop", "0", str(DEST / "startrail.gif")], check=True)


if __name__ == "__main__":
    app = QGuiApplication([])
    DEST.mkdir(exist_ok=True)
    generate_gif()
    if sys.argv[1:] == ["--gif-only"]:
        raise SystemExit()
    from PIL import Image
    for name in ("dunes", "mountain", "glow"):
        artwork(name).save(str(DEST / (name+".png")))
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        for n in range(96):
            frame = temp / f"frame-{n:03d}.png"
            artwork("glow", 1280, 720, n/96*2*math.pi).save(str(frame))
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-framerate", "24", "-i", str(temp / "frame-%03d.png"),
                        "-c:v", "libx264", "-preset", "slow", "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(DEST / "motion.mp4")], check=True)
    from still.app import make_icon
    # make_icon requires QApplication (QGuiApplication also supports QPixmap).
    pix = make_icon().pixmap(256, 256)
    path = DEST / "icon.png"
    pix.save(str(path))
    with Image.open(path) as icon:
        icon.save(DEST / "still.ico", sizes=[(16,16), (32,32), (48,48), (64,64), (128,128), (256,256)])
    print("Generated demo assets", DEST)
