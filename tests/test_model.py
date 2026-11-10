import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import call, patch

from still.model import Store, media_kind
from still.profile import choose_directory
from still.organizer_model import Collections
from still import win32
from still.hotkeys import MOD_CTRL, MOD_SHIFT, capture_binding


class StoreTests(unittest.TestCase):
    def test_dock_color_persists_independently_of_the_settings_theme(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            store = Store(root)
            self.assertEqual(store.settings.dock_color, "#263038")
            store.settings.dock_color = "#7b48b6"
            store.settings.settings_theme = "light"
            store.save()
            self.assertEqual(Store(root).settings.dock_color, "#7b48b6")
            store.settings.dock_color = "#12"
            store.save()
            self.assertEqual(Store(root).settings.dock_color, "#263038")

    def test_explicit_tray_activation_detaches_the_foreground_input_queue(self):
        with (patch.object(win32, "GetForegroundWindow", return_value=2),
              patch.object(win32, "SetForegroundWindow", return_value=False),
              patch.object(win32, "GetCurrentThreadId", return_value=10),
              patch.object(win32, "GetWindowThreadProcessId", return_value=20),
              patch.object(win32, "SendMessageTimeout", return_value=1),
              patch.object(win32, "AttachThreadInput", return_value=True) as attach,
              patch.object(win32, "BringWindowToTop") as raise_window,
              patch.object(win32, "SetActiveWindow") as activate):
            win32.activate_from_tray(1)
        self.assertEqual(attach.call_args_list, [call(10, 20, True), call(10, 20, False)])
        raise_window.assert_called_once_with(1)
        activate.assert_called_once_with(1)

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
            saved = loaded.find(item["id"])
            copy = loaded.path(saved)
            self.assertTrue(copy.is_relative_to((root / "config" / "wallpapers").resolve()))
            self.assertFalse(Path(saved["path"]).is_absolute())
            moved = media.with_name("moved.png")
            media.rename(moved)
            self.assertEqual(copy.read_bytes(), b"original-file")
            loaded.remove(item["id"])
            self.assertFalse(copy.exists())
            self.assertEqual(moved.read_bytes(), b"original-file")

    def test_legacy_profile_migrates_with_old_wallpapers_and_managed_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            legacy = root / "StillWallpaper"
            destination = root / "install" / "data"
            destination.parent.mkdir()
            vault = legacy / "organizer_files"
            vault.mkdir(parents=True)
            document = vault / "document.txt"
            document.write_text("owned file", encoding="utf-8")
            wallpaper = root / "source.gif"
            wallpaper.write_bytes(b"original animation")
            config = {"settings_theme": "light", "active_id": "legacy-id", "library": [
                {"id": "legacy-id", "path": str(wallpaper), "kind": "gif", "name": "Legacy"}],
                "organizer_groups": [{"id": "g", "entries": [{"target": str(document)}]}]}
            (legacy / "settings.json").write_text(json.dumps(config), encoding="utf-8")
            self.assertEqual(choose_directory(destination, legacy), destination)
            store = Store(destination)
            store.migrate_wallpapers()
            self.assertEqual(store.settings.settings_theme, "light")
            self.assertEqual(store.settings.active_id, "legacy-id")
            self.assertEqual(store.settings.organizer_groups[0]["entries"][0]["target"],
                             str(destination / "organizer_files" / "document.txt"))
            item = Store(destination).find("legacy-id")
            self.assertEqual(store.path(item).read_bytes(), wallpaper.read_bytes())
            self.assertEqual(document.read_text(encoding="utf-8"), "owned file")
            self.assertEqual(json.loads((legacy / "settings.json").read_text(encoding="utf-8")), config)
            with patch("still.profile.writable", return_value=False):
                self.assertEqual(choose_directory(root / "unwritable" / "data", legacy), legacy)

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
