"""Independent organizer surfaces; defaults retain the existing glass palette."""
from PySide6.QtGui import QColor


def foreground(settings):
    color = getattr(settings, "organizer_background_color", None)
    if color:
        base = QColor(color)
        if base.red()*.299 + base.green()*.587 + base.blue()*.114 > 195:
            return QColor("#182936")
    return QColor("#f4f7fc")


def icon_background(settings, selected=False, hovered=False):
    custom = getattr(settings, "organizer_icon_color", None)
    if custom:
        color = QColor(custom)
        color.setAlpha(235 if selected else 220 if hovered else 185)
        return color
    return QColor(162, 205, 255, 68) if selected else QColor(238, 246, 255, 20)
