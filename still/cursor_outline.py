"""Small cached outer-edge sprites from the actual Windows cursor silhouette.

GetIconInfo returns owned bitmaps: every path releases them. No screen capture,
cursor replacement, full-screen blur or idle polling is involved.
"""
from collections import OrderedDict
import ctypes as C
from ctypes import wintypes as W
from dataclasses import dataclass
import math

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QImage, QLinearGradient, QPainter

from . import win32


class CursorInfo(C.Structure):
    _fields_ = [("size", W.DWORD), ("flags", W.DWORD), ("cursor", W.HANDLE), ("position", W.POINT)]


class IconInfo(C.Structure):
    _fields_ = [("icon", W.BOOL), ("x", W.DWORD), ("y", W.DWORD), ("mask", W.HBITMAP), ("color", W.HBITMAP)]


class Bitmap(C.Structure):
    _fields_ = [("kind", W.LONG), ("width", W.LONG), ("height", W.LONG), ("stride", W.LONG),
                ("planes", W.WORD), ("bits", W.WORD), ("data", C.c_void_p)]


class Header(C.Structure):
    _fields_ = [("size", W.DWORD), ("width", W.LONG), ("height", W.LONG), ("planes", W.WORD),
                ("bits", W.WORD), ("compression", W.DWORD), ("image_size", W.DWORD),
                ("x_resolution", W.LONG), ("y_resolution", W.LONG), ("colors", W.DWORD), ("important", W.DWORD)]


gdi = C.WinDLL("gdi32", use_last_error=True)
# ctypes caches function objects on each DLL instance. Keep our structure
# signatures private so another icon consumer cannot overwrite argtypes.
user = C.WinDLL("user32", use_last_error=True)
get_cursor = win32.bind(user, "GetCursorInfo", W.BOOL, C.POINTER(CursorInfo))
get_icon = win32.bind(user, "GetIconInfo", W.BOOL, W.HANDLE, C.POINTER(IconInfo))
get_object = win32.bind(gdi, "GetObjectW", C.c_int, W.HANDLE, C.c_int, C.c_void_p)
get_bits = win32.bind(gdi, "GetDIBits", C.c_int, W.HDC, W.HBITMAP, W.UINT, W.UINT, C.c_void_p, C.c_void_p, W.UINT)
delete = win32.bind(gdi, "DeleteObject", W.BOOL, W.HANDLE)
get_dc = win32.bind(user, "GetDC", W.HDC, W.HWND)
release_dc = win32.bind(user, "ReleaseDC", C.c_int, W.HWND, W.HDC)


def bitmap_bytes(dc, handle):
    bitmap = Bitmap()
    if not handle or not get_object(handle, C.sizeof(bitmap), C.byref(bitmap)):
        return None
    width, height = bitmap.width, abs(bitmap.height)
    if not (0 < width <= 512 and 0 < height <= 1024):
        return None
    header = Header(size=C.sizeof(Header), width=width, height=-height, planes=1, bits=32)
    pixels = C.create_string_buffer(width*height*4)
    if get_bits(dc, handle, 0, height, pixels, C.byref(header), 0) != height:
        return None
    return width, height, pixels.raw


def cursor_mask(handle):
    info = IconInfo()
    if not get_icon(handle, C.byref(info)):
        return None
    dc = get_dc(None)
    try:
        if not dc:
            return None
        color = bitmap_bytes(dc, info.color)
        mask = bitmap_bytes(dc, info.mask)
        if color:
            width, height, pixels = color
            alpha = pixels[3::4]
            if any(alpha):
                coverage = bytes(255 if value >= 64 else 0 for value in alpha)
            elif mask and mask[:2] == (width,height):
                coverage = bytes(255 if value < 128 else 0 for value in mask[2][0::4])
            else:
                return None
        elif mask and mask[1] % 2 == 0:
            width, double, pixels = mask
            height = double//2
            values = pixels[0::4]
            # Monochrome cursors contain AND then XOR planes. Inverting pixels
            # (AND=1, XOR=1) are visible too, especially text/cross cursors.
            coverage = bytes(255 if a < 128 or b >= 128 else 0
                             for a,b in zip(values[:width*height],values[width*height:]))
        else:
            return None
        if height > 512 or not any(coverage) or info.x>=width or info.y>=height:
            return None
        pixels = bytearray(width*height*4)
        for index, value in enumerate(coverage):
            if value:
                pixels[index*4:index*4+4] = b'\xff\xff\xff\xff'
        image = QImage(bytes(pixels), width, height, width*4, QImage.Format.Format_ARGB32).copy()
        return image, QPointF(info.x,info.y)
    finally:
        if dc:
            release_dc(None,dc)
        if info.color:
            delete(info.color)
        if info.mask:
            delete(info.mask)


@dataclass
class Outline:
    glow: QImage
    rim: QImage
    hotspot: QPointF
    scale: float


def make_outline(mask, hotspot, scale, color):
    scale = max(1., min(4.,scale))
    padding = math.ceil(4*scale)+1
    width,height = mask.width()+padding*2, mask.height()+padding*2

    def band(radius):
        image = QImage(width,height,QImage.Format.Format_ARGB32_Premultiplied)
        image.fill(Qt.GlobalColor.transparent)
        painter = QPainter(image)
        for y in range(-radius,radius+1):
            for x in range(-radius,radius+1):
                if x*x+y*y <= radius*radius:
                    painter.drawImage(padding+x,padding+y,mask)
        painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_DestinationOut)
        painter.drawImage(padding,padding,mask)
        painter.end()
        return image

    rim = band(max(1,round(scale)))
    glow = QImage(width,height,QImage.Format.Format_ARGB32_Premultiplied)
    glow.fill(Qt.GlobalColor.transparent)
    painter = QPainter(glow)
    for radius,opacity in ((3.5,.10),(2.4,.16),(1.5,.26)):
        painter.setOpacity(opacity)
        painter.drawImage(0,0,band(max(1,round(radius*scale))))
    painter.end()
    for image in (glow,rim):
        painter = QPainter(image)
        painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceIn)
        painter.fillRect(image.rect(),QColor(color))
        painter.end()
        image.setDevicePixelRatio(scale)
    return Outline(glow,rim,QPointF((hotspot.x()+padding)/scale,(hotspot.y()+padding)/scale),scale)


def paint_outline(painter, outline, pointer, color, now, fade):
    origin = pointer-outline.hotspot
    painter.save()
    painter.setOpacity(fade*(.7+.18*math.sin(now*5)))
    painter.drawImage(origin,outline.glow)
    painter.setOpacity(fade*.65)
    painter.drawImage(origin,outline.rim)
    # A moving highlight is clipped by the outer band; the cursor body is
    # never painted over, and no free-floating ring surrounds the pointer.
    highlight = outline.rim.copy()
    width,height = highlight.width()/outline.scale, highlight.height()/outline.scale
    phase = (now*.65)%1
    gradient = QLinearGradient(-width+phase*width*2,0,phase*width*2,height)
    base = QColor(color); base.setAlpha(0)
    bright = QColor(color).lighter(155); bright.setAlpha(240)
    gradient.setColorAt(0,base);gradient.setColorAt(.35,base)
    gradient.setColorAt(.55,bright);gradient.setColorAt(.75,base);gradient.setColorAt(1,base)
    target = QPainter(highlight)
    target.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceIn)
    target.fillRect(QRectF(0,0,width,height),gradient)
    target.end()
    painter.setOpacity(fade)
    painter.drawImage(origin,highlight)
    painter.restore()


class CursorOutlineCache:
    def __init__(self):
        self.entries = OrderedDict()

    def clear(self):
        self.entries.clear()

    def current(self, scale, color):
        info = CursorInfo(size=C.sizeof(CursorInfo))
        if not get_cursor(C.byref(info)) or not info.flags & 1 or not info.cursor:
            return None
        key = info.cursor, round(scale,2), QColor(color).name()
        if key not in self.entries:
            result = cursor_mask(info.cursor)
            self.entries[key] = make_outline(*result,scale,color) if result else None
            if len(self.entries)>16:
                self.entries.popitem(last=False)
        else:
            self.entries.move_to_end(key)
        return self.entries[key]
