from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from PySide6.QtCore import QObject, QPoint, QUrl
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtWidgets import QApplication, QGridLayout, QLabel, QLineEdit, QMainWindow, QMenu, QWidget

from still.i18n import tr
from still.model import ASSETS, Store
from still.ui import MainWindow, Preview
from still.video_thumbnails import VideoThumbnails


class LibraryWindow(QMainWindow):
    """Real cards and preview, with desktop playback and decoders isolated."""
    refresh_cards = MainWindow.refresh_cards
    select = MainWindow.select
    wallpaper_menu = MainWindow.wallpaper_menu
    remove_selected = MainWindow.remove_selected

    def __init__(self, store):
        super().__init__()
        self.store = store
        self.selected = store.items[0]
        self.engine = Mock(active=None)
        self.light, self.filter_kind = False, "all"
        self.cards = []
        self.search, self.card_count = QLineEdit(self), QLabel(self)
        self.preview = Preview()
        self.preview.setParent(self)
        content = QWidget(self)
        self.grid = QGridLayout(content)
        self.setCentralWidget(content)
        self.thumbnails = Mock()
        self.thumbnails.get.return_value = QPixmap()
        self.show_notice, self.show_error, self.applied = Mock(), Mock(), Mock()
        self.refresh_cards()

    def apply_selected(self):
        self.applied()

    def update_selection(self):
        self.preview.set_item(self.selected, self.store.path(self.selected))


class WallpaperLibraryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def open_delete_menu(self, card, *, trigger=False):
        outcome = {}

        def create_menu(parent):
            menu = QMenu(parent)

            def choose(_position):
                action = next(action for action in menu.actions() if action.text() == tr("删除壁纸"))
                outcome["enabled"] = action.isEnabled()
                if trigger:
                    action.trigger()

            # Keep real QAction signals without running unrelated deferred Qt
            # callbacks left by other test fixtures in a nested menu event loop.
            return SimpleNamespace(addAction=menu.addAction, addSeparator=menu.addSeparator,
                                   exec=choose, deleteLater=menu.deleteLater)

        with patch("still.ui.QMenu", side_effect=create_menu):
            card.customContextMenuRequested.emit(QPoint(8, 8))
        return outcome["enabled"]

    def test_right_click_deletes_the_clicked_import_and_keeps_original_and_other_items(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "wallpaper.png"
            source.write_bytes((ASSETS / "dunes.png").read_bytes())
            other = root / "other.png"
            other.write_bytes(source.read_bytes())
            store = Store(root / "profile")
            item, kept = store.add(source), store.add(other)
            managed = store.path(item)
            window = LibraryWindow(store)
            window.select(kept)

            card = next(card for card in window.cards if card.item["id"] == item["id"])
            self.assertTrue(self.open_delete_menu(card, trigger=True))
            self.assertIsNone(store.find(item["id"]))
            self.assertIsNotNone(store.find(kept["id"]))
            self.assertFalse(managed.exists())
            self.assertTrue(source.exists())
            self.assertEqual(window.preview.item["id"], store.items[0]["id"])
            window.engine.restore_original.assert_not_called()
            window.show_error.assert_not_called()
            window.deleteLater()

    def test_deleting_active_images_gifs_and_videos_restores_desktop_before_unlink(self):
        for name in ("picture.png", "animation.gif", "clip.mp4"):
            with self.subTest(name=name), TemporaryDirectory() as directory:
                root = Path(directory)
                source = root / name
                source.write_bytes((ASSETS / ("startrail.gif" if source.suffix == ".gif" else "dunes.png")).read_bytes())
                store = Store(root / "profile")
                item = store.add(source)
                managed = store.path(item)
                window = LibraryWindow(store)
                window.select(item)
                window.engine.active = item
                store.settings.active_id = item["id"]

                def restore():
                    self.assertTrue(managed.exists())
                    window.engine.active = None
                    store.settings.active_id = ""

                window.engine.restore_original.side_effect = restore
                window.remove_selected()
                window.engine.restore_original.assert_called_once_with()
                window.thumbnails.cancel.assert_called_once_with(managed)
                self.assertIsNone(store.find(item["id"]))
                self.assertFalse(managed.exists())
                self.assertTrue(source.exists())
                self.assertEqual(Store(store.directory).settings.active_id, "")
                self.assertIsNone(window.preview.movie)
                window.deleteLater()

    def test_built_in_wallpaper_has_a_disabled_delete_action(self):
        with TemporaryDirectory() as directory:
            store = Store(Path(directory))
            window = LibraryWindow(store)

            self.assertFalse(self.open_delete_menu(window.cards[0]))
            window.remove_selected()
            self.assertEqual(len(store.items), 5)
            window.thumbnails.cancel.assert_not_called()
            window.deleteLater()

    def test_failed_save_keeps_the_import_and_restores_preview_selection(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "picture.png"
            source.write_bytes((ASSETS / "dunes.png").read_bytes())
            store = Store(root / "profile")
            item = store.add(source)
            window = LibraryWindow(store)
            window.select(item)
            with patch.object(store, "save", side_effect=OSError("read-only profile")):
                window.remove_selected()
            self.assertIsNotNone(store.find(item["id"]))
            self.assertTrue(store.path(item).is_file())
            self.assertEqual(window.selected["id"], item["id"])
            window.show_error.assert_called_once()
            window.deleteLater()

    def test_deleting_a_video_cancels_its_cover_without_cancelling_other_work(self):
        with TemporaryDirectory() as directory:
            cache = VideoThumbnails(Path(directory))
            removed, kept = Path(directory) / "removed.mp4", Path(directory) / "kept.mp4"
            cache.pending = {"removed": removed, "kept": kept}
            cache.player = Mock()
            cache.active = "removed", removed, QObject(cache)
            cache.first_frame = QImage()
            cache.cancel(removed)
            self.assertEqual(cache.pending, {"kept": kept})
            self.assertIsNone(cache.active)
            self.assertNotIn("removed", cache.failed)
            cache.player.stop.assert_called_once()
            cache.player.setSource.assert_called_once_with(QUrl())
            cache.close()
            cache.deleteLater()
