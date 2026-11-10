from tempfile import TemporaryDirectory
from pathlib import Path
import unittest
from unittest.mock import patch

from still import native_taskbar
from still.model import DEFAULT_TASKBAR_RADIUS, Store
from still.native_taskbar_xaml import stage_runtime_library
from still.taskbar_visibility import TaskbarVisibilityLease, bottom_strip
from still.taskbar_style_native import HIDDEN_SHAPE


class NativeTaskbarTests(unittest.TestCase):
    @patch.object(native_taskbar, "put_region")
    @patch.object(native_taskbar, "region_matches", return_value=True)
    @patch.object(native_taskbar, "window_region", return_value=(2, (0, 0, 3840, 72)))
    @patch.object(native_taskbar, "window_bounds", return_value=(0, 2088, 3840, 2160))
    @patch.object(native_taskbar, "monitor_bounds", return_value=(0, 0, 3840, 2160))
    @patch.object(native_taskbar, "get_dpi", return_value=144)
    @patch.object(native_taskbar, "process_id", return_value=2)
    @patch.object(native_taskbar, "is_window", return_value=True)
    def test_repeated_updates_avoid_repainting_and_restore_original_rectangle(self, _, _pid, _dpi,
                                                                           _monitor, bounds, _region, _owns, put):
        style = native_taskbar.NativeTaskbarRegions()
        self.assertTrue(style.apply_window(1, 2, 18))
        self.assertTrue(style.apply_window(1, 2, 18))
        put.assert_called_once_with(1, (0, 9, 3841, 73), 27)
        bounds.return_value = (0, 1024, 1920, 1080)
        style.restore()
        put.assert_called_with(1, (0, 0, 1920, 56))

    @patch.object(native_taskbar, "put_region")
    @patch.object(native_taskbar, "window_region", return_value=(3, (100, 0, 1200, 54)))
    @patch.object(native_taskbar, "window_bounds", return_value=(0, 1024, 1600, 1080))
    @patch.object(native_taskbar, "get_dpi", return_value=96)
    def test_existing_custom_regions_are_left_alone(self, _dpi, _bounds, _region, put):
        style = native_taskbar.NativeTaskbarRegions()
        self.assertFalse(style.apply_window(1, 2, 18))
        style.restore()
        put.assert_not_called()

    @patch.object(native_taskbar, "auto_hidden", return_value=True)
    @patch.object(native_taskbar, "explorer_taskbars", return_value={1: 2, 3: 2})
    def test_native_auto_hide_restores_unclipped_hit_areas(self, _, auto_hidden):
        style = native_taskbar.NativeTaskbarRegions()
        with (patch.object(style, "restore") as restore,
              patch.object(style, "apply_window", return_value=True) as apply):
            self.assertEqual(style.update(18), {"auto_hide": True})
            restore.assert_called_once_with()
            apply.assert_not_called()
            auto_hidden.return_value = False
            self.assertEqual(style.update(18), {"auto_hide": False})
            self.assertEqual([call.args for call in apply.call_args_list], [(1, 2, 18), (3, 2, 18)])

    def test_original_profiles_keep_the_style_toggle_and_get_a_default_radius(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "settings.json").write_text('{"taskbar_style_enabled": true, "dock_pins": []}', encoding="utf-8")
            store = Store(root)
            self.assertTrue(store.settings.taskbar_style_enabled)
            self.assertEqual(store.settings.taskbar_radius, DEFAULT_TASKBAR_RADIUS)
            self.assertEqual(store.settings.dock_pins, [])
            store.settings.taskbar_radius = 22
            store.save()
            self.assertEqual(Store(root).settings.taskbar_radius, 22)

    def test_coverage_strip_mirrors_the_native_start_position_even_on_a_negative_monitor(self):
        with (patch("still.taskbar_visibility.win32.FindWindowEx", return_value=4),
              patch("still.taskbar_visibility.window_bounds", return_value=(-1400, 1000, -1350, 1080))):
            self.assertEqual(bottom_strip(1, (-1920, 1000, 0, 1080), (-1920, 0, 0, 1080)),
                             (-1400, 1000, -520, 1080))
            self.assertIsNone(bottom_strip(1, (-1920, 0, 0, 80), (-1920, 0, 0, 1080)))

    def test_hidden_lease_restores_only_the_original_taskbar_region(self):
        style = TaskbarVisibilityLease()
        style.bar, style.pid = 1, 2
        bounds = (0, 2088, 3840, 2160)
        with (patch("still.taskbar_visibility.window_region", return_value=(2, (0, 0, 3840, 72))),
              patch("still.taskbar_visibility.window_bounds", return_value=bounds),
              patch("still.taskbar_visibility.put_region") as put,
              patch("still.taskbar_visibility.region_matches", return_value=True),
              patch.object(style, "same_bar", return_value=True)):
            style.hide(bounds)
            put.assert_called_once_with(1, *HIDDEN_SHAPE)
            style.restore_region()
            put.assert_called_with(1, (0, 0, 3840, 72))
            self.assertIsNone(style.rectangular)

    def test_explorer_maps_a_copy_outside_the_installation_directory(self):
        with TemporaryDirectory() as directory, patch.dict("os.environ", {"LOCALAPPDATA": directory}):
            install = Path(directory) / "installed-taskbar.dll"
            install.write_bytes(b"component")
            first = Path(stage_runtime_library(install))
            second = Path(stage_runtime_library(install))
            self.assertNotEqual(install, second)
            self.assertFalse(first.exists())
            self.assertEqual(second.read_bytes(), install.read_bytes())
