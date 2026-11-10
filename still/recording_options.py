"""The recording defaults and preview use the same export choices."""
from .i18n import tr

RESOLUTIONS = (1080, 720, 480)
FRAME_RATES = (12, 15, 24, 30, 60)
DEFAULT_CAPTURE_FPS = 24


def choice(name, items, selected=None):
    from PySide6.QtWidgets import QComboBox

    combo = QComboBox()
    combo.setAccessibleName(name)
    combo.setToolTip(name)
    for text, value in items:
        combo.addItem(text, value)
    combo.setCurrentIndex(max(0, combo.findData(selected)))
    return combo


def export_choices(settings):
    quality = choice(tr("导出画质"), [(tr("原始分辨率"), 0)] + [(f"{value}p", value) for value in RESOLUTIONS],
                     settings.record_resolution)
    fps = choice(tr("导出帧率"), [(tr("原始帧率"), None)] + [(f"{value} fps", value) for value in FRAME_RATES],
                 settings.record_fps)
    return quality, fps
