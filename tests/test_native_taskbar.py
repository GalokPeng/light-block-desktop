from tempfile import TemporaryDirectory
from pathlib import Path
import ctypes as C
import unittest
from unittest.mock import Mock, patch

from still import native_taskbar
from still.model import DEFAULT_TASKBAR_RADIUS, Store
from still.native_taskbar_xaml import CHANNEL, LayoutStatus, NativeTaskbarLayout, component_channel, stage_runtime_library, stop_resident
from still.taskbar_visibility import TaskbarVisibilityLease, bottom_strip, measured_strip
from still.taskbar_style_native import HIDDEN_SHAPE


class NativeTaskbarTests(unittest.TestCase):
    def test_appearance_radius_reaches_shared_protocol_without_idle_resubmission(self):
        layout=NativeTaskbarLayout(Path("not-loaded.dll"))
        status=LayoutStatus(version=4)
        layout.view=C.addressof(status)
        layout.events={"Start":123}
        with patch("still.native_taskbar_xaml.set_event") as signal:
            for radius in (6,26,12):
                layout.set_appearance("#234567",79,radius)
                self.assertEqual(status.appearance_radius,radius)
            self.assertEqual(signal.call_count,3)
            layout.set_appearance("#234567",79,12)
            self.assertEqual(signal.call_count,3)
            for invalid in (True,0,27):
                with self.assertRaises(ValueError):layout.set_appearance("#234567",79,invalid)

    def test_native_builds_get_distinct_resident_channels(self):
        module = Mock()
        module._handle = 123
        free = Mock()
        with patch("still.native_taskbar_xaml.C.WinDLL", return_value=module), patch("still.native_taskbar_xaml.win32.bind", return_value=free):
            module.QingkuaiLayoutBuild.return_value = 123456
            first = component_channel(Path("first.dll"))
            module.QingkuaiLayoutBuild.return_value = 654321
            second = component_channel(Path("second.dll"))
        self.assertEqual(first, CHANNEL+"123456.")
        self.assertNotEqual(first, second)
        self.assertEqual(free.call_count, 2)

    def test_old_resident_restores_before_replacement_and_events_are_closed(self):
        with patch("still.native_taskbar_xaml.open_event", side_effect=[1,2,3,4]) as opened, \
             patch("still.native_taskbar_xaml.wait_for", return_value=0) as wait, \
             patch("still.native_taskbar_xaml.reset_event") as reset, \
             patch("still.native_taskbar_xaml.set_event") as signal, \
             patch("still.native_taskbar_xaml.close_handle") as close:
            stop_resident(CHANNEL, 42)
        self.assertTrue(all(CHANNEL+"42." in c.args[2] for c in opened.call_args_list))
        self.assertEqual([c.args[0] for c in reset.call_args_list], [4,3])
        signal.assert_called_once_with(2)
        self.assertEqual(wait.call_args_list[-1].args, (3,2300))
        self.assertEqual([c.args[0] for c in close.call_args_list], [1,2,3,4])

    def test_unresponsive_old_resident_prevents_new_layout_and_closes_handles(self):
        with patch("still.native_taskbar_xaml.open_event", side_effect=[1,2,3,4]), \
             patch("still.native_taskbar_xaml.wait_for", side_effect=[0,258]), \
             patch("still.native_taskbar_xaml.reset_event"),patch("still.native_taskbar_xaml.set_event"), \
             patch("still.native_taskbar_xaml.close_handle") as close:
            with self.assertRaises(OSError):
                stop_resident(CHANNEL, 42)
        self.assertEqual(close.call_count, 4)

    def test_component_initialization_failure_is_retryable(self):
        layout = NativeTaskbarLayout(Path("not-loaded.dll"))
        layout.visibility = Mock()
        with patch("still.native_taskbar_xaml.win32.FindWindow", return_value=1), \
             patch("still.native_taskbar_xaml.win32.FindWindowEx", return_value=2), \
             patch("still.native_taskbar_xaml.process_id", return_value=42), \
             patch.object(layout, "make_events", side_effect=OSError("old component busy")):
            for _ in range(2):
                with self.assertRaises(OSError):
                    layout.update()
                self.assertIsNone(layout.pid)

    def test_new_explorer_process_reconnects_instead_of_disabling_styling(self):
        layout = NativeTaskbarLayout(Path("not-loaded.dll"))
        layout.pid, layout.visibility = 10, Mock()
        layout.visibility.update.return_value = (1200, 2088, 2640, 2160)

        def restore(**_):
            layout.pid, layout.events = None, {}

        def events(_):
            layout.events = {"Resident": 1, "Stop": 2, "Ready": 3, "Restored": 4, "Start": 5}

        with (patch("still.native_taskbar_xaml.win32.FindWindow", return_value=1),
              patch("still.native_taskbar_xaml.win32.FindWindowEx", return_value=4),
              patch("still.native_taskbar_xaml.process_id", return_value=20),
              patch("still.native_taskbar_xaml.wait_for", return_value=0),
              patch("still.native_taskbar_xaml.reset_event"), patch("still.native_taskbar_xaml.set_event"),
              patch("still.native_taskbar_xaml.auto_hidden", return_value=False),
              patch("still.native_taskbar_xaml.monitor_bounds", return_value=(0, 0, 3840, 2160)),
              patch.object(LayoutStatus, "read", return_value=None),
              patch.object(layout, "restore", side_effect=restore) as release,
              patch.object(layout, "make_events", side_effect=events) as create):
            result = layout.update()
            release.assert_called_once_with(wait=False)
            create.assert_called_once_with(20)
            self.assertTrue(result["centered"])
            self.assertEqual(layout.pid, 20)

    def test_fullscreen_shell_demotion_is_repaired_without_repainting_a_stable_bar(self):
        style = TaskbarVisibilityLease()
        style.bar, style.pid, style.work_area = 1, 2, Mock()
        bounds, monitor = (0, 2088, 3840, 2160), (0, 0, 3840, 2160)
        with (patch("still.taskbar_visibility.process_id", return_value=2),
              patch("still.taskbar_visibility.window_bounds", return_value=bounds),
              patch("still.taskbar_visibility.bottom_strip", return_value=(1200, 2088, 2640, 2160)),
              patch.object(style, "restore_region"),
              patch("still.taskbar_visibility.win32.GetWindowLong", side_effect=[8, 0, 8]),
              patch("still.taskbar_visibility.win32.SetWindowPos") as move):
            for _ in range(3):
                style.update(1, monitor, False, True, False)
            self.assertEqual(move.call_count, 2)
            self.assertTrue(style.was_topmost)

    def test_measured_group_uses_xaml_pixels_on_scaled_and_negative_monitors(self):
        geometry = {"host_width": 2560., "group_left": 808.67, "group_right": 1751.33}
        self.assertEqual(measured_strip((0, 2088, 3840, 2160), (0, 0, 3840, 2160), geometry),
                         (1213, 2088, 2627, 2160))
        self.assertEqual(measured_strip((-3840, 2088, 0, 2160), (-3840, 0, 0, 2160), geometry),
                         (-2627, 2088, -1213, 2160))
        for invalid in (float("nan"), float("inf"), 3000., -1.):
            self.assertIsNone(measured_strip((0, 2088, 3840, 2160), (0, 0, 3840, 2160),
                                             dict(geometry, group_right=invalid)))
        self.assertIsNone(measured_strip((0, 0, 3840, 72), (0, 0, 3840, 2160), geometry))

    def test_layout_status_ignores_incomplete_or_unknown_protocol_writes(self):
        status = LayoutStatus(version=4, sequence=2, state=1, repairs=3, host_width=1706.6666, scale=1.5)
        self.assertEqual(C.sizeof(LayoutStatus), 104)
        snapshot = LayoutStatus.read(C.addressof(status))
        self.assertEqual(snapshot["repairs"], 3)
        self.assertEqual(snapshot["host_width"], 1706.67)
        self.assertNotIn("sequence", snapshot)
        status.reserved = (1950 << 16) | 2533
        gutters = LayoutStatus.read(C.addressof(status))
        self.assertEqual((gutters["controls_gutter_left"], gutters["controls_gutter_right"]), (25.33,19.5))
        status.sequence = 3
        self.assertIsNone(LayoutStatus.read(C.addressof(status)))
        status.sequence, status.version = 4, 1
        self.assertIsNone(LayoutStatus.read(C.addressof(status)))

    def test_native_layout_conflict_is_reported_even_if_old_ready_event_is_set(self):
        layout = NativeTaskbarLayout(Path("not-loaded.dll"))
        layout.pid, layout.events = 2, {"Ready": 3}
        status = LayoutStatus(version=4, sequence=2, state=4, repairs=12)
        layout.view = C.addressof(status)
        layout.visibility = Mock()
        with (patch("still.native_taskbar_xaml.win32.FindWindow", return_value=1),
              patch("still.native_taskbar_xaml.win32.FindWindowEx", return_value=4),
              patch("still.native_taskbar_xaml.process_id", return_value=2),
              patch("still.native_taskbar_xaml.wait_for", return_value=0)):
            reply = layout.update()
            self.assertEqual(reply["error"], "xaml_conflict")
            self.assertEqual(reply["diagnostic"]["repairs"], 12)
            layout.visibility.update.assert_not_called()
            status.state = 3
            self.assertEqual(layout.update()["error"], "xaml_layout")

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
