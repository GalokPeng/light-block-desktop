import json
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from PySide6.QtCore import QRect, QRectF, Qt
from PySide6.QtGui import QColor, QIcon, QImage, QPainter, QPixmap
from PySide6.QtWidgets import QApplication

from still.desktop_windows import paint_desktop_panel
from still.model import Settings, Store
from still.organizer import DesktopShelf, OrganizerManager
from still.organizer_colors import foreground, icon_background


class OrganizerColorsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app=QApplication.instance() or QApplication([])
        cls.app.setQuitOnLastWindowClosed(False)

    def test_palette_persists_independently_and_invalid_values_keep_defaults(self):
        with TemporaryDirectory() as directory:
            store=Store(Path(directory))
            self.assertIsNone(store.settings.organizer_icon_color)
            self.assertIsNone(store.settings.organizer_background_color)
            store.settings.organizer_icon_color="#8caddd"
            store.settings.organizer_background_color="#e5ede9"
            store.settings.organizer_groups=[dict(id="test",name="Custom",color="rose",entries=[])]
            store.save()
            restored=Store(store.directory)
            self.assertEqual((restored.settings.organizer_icon_color,restored.settings.organizer_background_color),("#8caddd","#e5ede9"))
            self.assertEqual(restored.settings.organizer_groups[0]["color"],"rose")
            (store.directory/"settings.json").write_text(json.dumps(dict(organizer_icon_color=[],organizer_background_color="bad")))
            restored=Store(store.directory)
            self.assertIsNone(restored.settings.organizer_icon_color)
            self.assertIsNone(restored.settings.organizer_background_color)

    def test_explicit_background_reaches_pixels_and_keeps_group_tint_independent(self):
        def sample(background,tint):
            image=QImage(320,140,QImage.Format.Format_ARGB32)
            image.fill(Qt.GlobalColor.transparent)
            painter=QPainter(image)
            paint_desktop_panel(painter,QRectF(0,0,320,140),165,tint,background)
            painter.end()
            return image.pixelColor(160,70)
        self.assertEqual(sample("#496f9d","#ff0000"),sample("#496f9d","#00ff00"))
        self.assertNotEqual(sample("#496f9d",None),sample("#a85f79",None))
        self.assertNotEqual(sample(None,"#ff0000"),sample(None,"#00ff00"))
        settings=Settings(organizer_background_color="#e5ede9",organizer_icon_color="#a85f79")
        self.assertEqual(foreground(settings),QColor("#182936"))
        self.assertEqual(icon_background(settings).name(),"#a85f79")
        settings.organizer_background_color="#25374c"
        self.assertEqual(foreground(settings),QColor("#f4f7fc"))
        self.assertEqual(icon_background(settings).name(),"#a85f79")

    def test_recoloring_top_center_bottom_preserves_loaded_icons_and_geometry(self):
        for alignment in ("top","center","bottom"):
            settings=Settings(organizer_alignment=alignment,organizer_views={alignment:"name"})
            entry=dict(id="app",name="Example",target="C:/example.exe")
            group=dict(id="group",name="Examples",color="rose",entries=[entry])
            manager=SimpleNamespace(store=SimpleNamespace(settings=settings),
                collections=SimpleNamespace(groups=[group]),icon_for=Mock(return_value=QIcon()),changed=Mock())
            with patch('still.organizer.MouseHook'), patch.object(DesktopShelf,'location',return_value=(QRect(0,0,1280,720),QRect(0,0,1280,720).center())):
                shelf=DesktopShelf(manager);manager.shelf=shelf
                try:
                    icon=QPixmap(32,32);icon.fill(QColor("#7799cc"))
                    shelf.grid.items.cache_icon(0,icon)
                    before=shelf.geometry();cache_key=shelf.grid.items.icons[0].cacheKey()
                    for panel,tile in (("#e5ede9","#8caddd"),("#31465c","#c193b0"),(None,None)):
                        settings.organizer_background_color=panel
                        settings.organizer_icon_color=tile
                        OrganizerManager.refresh_appearance(manager)
                        self.assertEqual(shelf.geometry(),before)
                        self.assertEqual(shelf.grid.items.icons[0].cacheKey(),cache_key)
                        manager.icon_for.assert_not_called()
                finally:
                    shelf.stop_interactions();shelf.close();shelf.deleteLater()
