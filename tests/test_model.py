import json
from pathlib import Path
import tempfile
import unittest

from still.model import Store, media_kind
from still.organizer_model import Collections
from still import win32
from still.hotkeys import MOD_CTRL, MOD_SHIFT, capture_binding


class StoreTests(unittest.TestCase):
    def test_capture_shortcut_validation_and_persistence(self):
        self.assertEqual(capture_binding("Ctrl+Shift+J"), (MOD_CTRL | MOD_SHIFT, ord("J")))
        self.assertEqual(capture_binding("F8"), (0, 0x77))
        for value in ("", "A", "F12", "Ctrl+Alt+R", "Ctrl+Alt+Space", "Ctrl+A, Ctrl+B"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                capture_binding(value)
        with tempfile.TemporaryDirectory() as temp:
            store = Store(Path(temp))
            self.assertEqual(store.settings.capture_shortcut, "Ctrl+Alt+A")
            store.settings.capture_shortcut = "Ctrl+Shift+J"
            store.save()
            self.assertEqual(Store(Path(temp)).settings.capture_shortcut, "Ctrl+Shift+J")

    def test_appearance_and_blank_collection_titles_survive_upgrade(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "settings.json").write_text('{"version":"0.4.2"}', encoding="utf-8")
            store = Store(root)
            self.assertEqual(store.settings.settings_theme, "dark")
            self.assertEqual(store.settings.organizer_title, "桌面收纳")
            store.settings.settings_theme = "light"
            store.settings.organizer_title = ""
            group = Collections(store).create("")
            reloaded = Store(root)
            self.assertEqual(reloaded.settings.settings_theme, "light")
            self.assertEqual(reloaded.settings.organizer_title, "")
            self.assertEqual(Collections(reloaded).find(group["id"])["name"], "")

    def test_unicode_path_roundtrip_and_duplicate(self):
        with tempfile.TemporaryDirectory(prefix="still-测试 空格-") as temp:
            root = Path(temp)
            media = root / "我的 壁纸.PNG"
            media.write_bytes(b"original-file")
            store = Store(root / "config")
            item = store.add(media)
            self.assertEqual(store.add(media)["id"], item["id"])
            self.assertEqual(len(store.settings.library), 1)
            store.settings.active_id = item["id"]
            store.save()
            loaded = Store(root / "config")
            self.assertEqual(loaded.find(item["id"])["path"], str(media.resolve()))
            loaded.remove(item["id"])
            self.assertEqual(media.read_bytes(), b"original-file")

    def test_corrupt_config_is_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "settings.json").write_text("{broken", encoding="utf-8")
            store = Store(root)
            self.assertTrue(store.warning)
            backups = list(root.glob("settings.damaged-*.json"))
            self.assertEqual(len(backups), 1)
            self.assertEqual(backups[0].read_text(), "{broken")
            store.save()
            self.assertIsInstance(json.loads(store.file.read_text()), dict)

    def test_extension_routing_and_missing_file(self):
        self.assertEqual(media_kind(Path("X.MP4")), "video")
        self.assertEqual(media_kind(Path("X.GIF")), "gif")
        self.assertEqual(media_kind(Path("X.webp")), "image")
        with self.assertRaises(ValueError):
            media_kind(Path("run.exe"))
        with tempfile.TemporaryDirectory() as temp:
            store = Store(Path(temp))
            with self.assertRaises(FileNotFoundError):
                store.add(Path(temp) / "missing.png")
            self.assertEqual(store.settings.library, [])

    def test_native_basics(self):
        self.assertGreater(win32.memory_mb(), 1)
        self.assertTrue(win32.monitors())
        self.assertIn("--background", win32.startup_command())
        self.assertNotIn("powershell", win32.startup_command())


if __name__ == "__main__":
    unittest.main()
