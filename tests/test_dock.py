from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import Mock, patch

from PySide6.QtCore import Qt

from still import dock_windows
from still.dock import Dock, group_entries
from still.dock_windows import AppWindow, overlaps_strip
from still.model import APPS_FOLDER, Store, dock_target_key
from still.taskbar_style_native import BottomWorkArea, HIDDEN_SHAPE, Taskbar, TaskbarLease


class DockTests(unittest.TestCase):
    def test_dock_and_frame_change_visibility_only_on_native_commit(self):
        dock = Mock(covered=False, tray_bounds=(900, 944, 1200, 1000), tray_ready=True)
        dock.menu = None
        Dock.update_reveal(dock)
        dock.sync_material.assert_called_once_with()
        dock.setVisible.assert_not_called()
        result = {"bounds": dock.tray_bounds, "hwnd": 10, "tray": 20, "positioned": True}
        for hidden in (True, False):
            dock.isVisible.return_value = hidden
            Dock.tray_layout(dock, dict(result, hidden=hidden))
            dock.setVisible.assert_called_with(not hidden)
            dock.tray_frame.setVisible.assert_called_with(not hidden)

    def test_only_a_window_intersecting_the_visible_strip_covers_the_dock(self):
        strip = (400, 930, 1300, 990)
        self.assertTrue(overlaps_strip((0, 0, 1600, 1000), strip))
        self.assertTrue(overlaps_strip((1150, 500, 1600, 960), strip))
        self.assertFalse(overlaps_strip((0, 0, 1600, 930), strip))
        self.assertFalse(overlaps_strip((1300, 400, 1600, 1000), strip))

    def test_coverage_excludes_click_through_overlays_and_windows_behind_the_desktop(self):
        order = [10, 20, 30]  # Overlay, desktop, application
        def enumerate_windows(callback, data):
            for hwnd in order:
                if not callback(hwnd, data):
                    break

        def bounds(hwnd, attribute, value, size):
            if attribute == 9:
                rect = value._obj
                rect.left, rect.top, rect.right, rect.bottom = 550, 500, 800, 960
            return 0

        with (patch.object(dock_windows.api, "EnumWindows", side_effect=enumerate_windows),
              patch.object(dock_windows.api, "DwmGetWindowAttribute", side_effect=bounds),
              patch.object(dock_windows.api, "GetWindowLong", side_effect=lambda hwnd, _: 0x80020 if hwnd == 10 else 0),
              patch.object(dock_windows, "is_visible", return_value=True),
              patch.object(dock_windows, "is_iconic", return_value=False),
              patch.object(dock_windows, "app_surface", return_value=True),
              patch.object(dock_windows, "window_class", side_effect=lambda hwnd: "Progman" if hwnd == 20 else "App")):
            self.assertIsNone(dock_windows.covering_window((400, 930, 1000, 990)))
            order[:] = [10, 30, 20]
            self.assertEqual(dock_windows.covering_window((400, 930, 1000, 990)), 30)
            self.assertIsNone(dock_windows.covering_window((400, 930, 550, 990), (800, 930, 1000, 990)))

    def test_drop_never_acknowledges_a_shell_move(self):
        event = Mock()
        event.possibleActions.return_value = Qt.DropAction.MoveAction
        self.assertFalse(Dock.accept_pin_drop(event))
        event.accept.assert_not_called()
        event.possibleActions.return_value |= Qt.DropAction.CopyAction
        self.assertTrue(Dock.accept_pin_drop(event))
        event.setDropAction.assert_called_once_with(Qt.DropAction.CopyAction)
        event.accept.assert_called_once()

    def test_pins_persist_in_order_and_drop_invalid_or_duplicate_entries(self):
        first = r"C:\Apps\Writer.lnk"
        second = APPS_FOLDER + "Example_abc!App"
        with TemporaryDirectory() as directory:
            path = Path(directory)
            store = Store(path)
            self.assertFalse(store.settings.taskbar_style_enabled)
            self.assertIsNone(store.settings.dock_pins)
            store.settings.dock_pins = [first, second, first.lower(), "relative.exe", None, r"C:\Apps\photo.jpg"]
            store.settings.taskbar_style_enabled = True
            store.save()
            saved = Store(path)
            self.assertTrue(saved.settings.taskbar_style_enabled)
            self.assertEqual(saved.settings.dock_pins, [first, second])
            saved.settings.dock_pins = []
            saved.save()
            self.assertEqual(Store(path).settings.dock_pins, [])

    def test_shortcut_preserves_launch_target_and_groups_running_windows(self):
        exe = r"C:\Apps\Writer.exe"
        shortcut = r"C:\Links\Writer with arguments.lnk"
        other_shortcut = r"C:\Links\Writer safe mode.lnk"
        first = AppWindow(10, 1, exe, exe, "First")
        second = AppWindow(20, 2, exe.lower(), exe.lower(), "Second")
        entries = group_entries([shortcut, other_shortcut], [first, second],
                                {target: dock_target_key(exe) for target in (shortcut, other_shortcut)})
        self.assertEqual([entry.target for entry in entries], [shortcut, other_shortcut])
        self.assertEqual(entries[0].windows, (first, second))
        self.assertEqual(entries[1].windows, ())

    def test_unpinned_app_order_does_not_change_when_window_focus_changes(self):
        exe = r"C:\Apps\Writer.exe"
        app_id = APPS_FOLDER + "Example_abc!App"
        first = AppWindow(10, 1, exe, exe, "Writer")
        second = AppWindow(20, 2, app_id, r"C:\Apps\Example.exe", "Example")
        self.assertEqual(group_entries([], [first, second], {}), group_entries([], [second, first], {}))
        entries = group_entries([app_id], [first, second], {})
        self.assertEqual(entries[0].windows, (second,))
        self.assertEqual(entries[1].windows, (first,))


class TaskbarLeaseTests(unittest.TestCase):
    bar = Taskbar(10, 20, 30, (0, 944, 1600, 1000), (1300, 944, 1600, 1000))

    def test_hidden_region_is_recognized_as_ours_when_revealing(self):
        lease = TaskbarLease()
        lease.applied = self.bar
        lease.shifted_x = -400
        lease.shape = HIDDEN_SHAPE
        moved = Taskbar(10, 20, 30, (-400, 944, 1200, 1000), (900, 944, 1200, 1000))
        with (patch("still.taskbar_style_native.taskbar", return_value=moved),
              patch("still.taskbar_style_native.region_matches", return_value=True) as owns,
              patch("still.taskbar_style_native.put_region") as put,
              patch("still.taskbar_style_native.suppress_taskbar"),
              patch("still.taskbar_style_native.move_taskbar"),
              patch("still.taskbar_style_native.window_bounds", return_value=moved.bounds),
              patch.object(lease.work_area, "expand")):
            lease.update(500)
        owns.assert_called_once_with(self.bar.hwnd, HIDDEN_SHAPE)
        put.assert_called_once_with(self.bar.hwnd, *moved.tray_shape, redraw=False)

    @patch("still.taskbar_style_native.put_region")
    @patch("still.taskbar_style_native.window_region", return_value=(2, (150, 0, 1600, 56)))
    def test_leaves_another_taskbar_customization_alone(self, region, put):
        with patch("still.taskbar_style_native.taskbar", return_value=self.bar):
            with self.assertRaisesRegex(OSError, "unsupported"):
                TaskbarLease().update(500)
        put.assert_not_called()

    @patch("still.taskbar_style_native.move_taskbar")
    @patch("still.taskbar_style_native.suppress_taskbar")
    @patch("still.taskbar_style_native.put_region")
    def test_centers_tray_and_restores_position_and_full_region_at_current_resolution(self, put, suppress, move):
        lease = TaskbarLease()
        lease.work_area = Mock()
        shifted = Taskbar(10, 20, 30, (-400, 944, 1200, 1000), (900, 944, 1200, 1000))
        with patch("still.taskbar_style_native.taskbar", side_effect=[self.bar, shifted]), \
                patch("still.taskbar_style_native.window_region", return_value=(2, (0, 0, 1600, 56))), \
                patch("still.taskbar_style_native.region_matches", return_value=True) as owns_region, \
                patch("still.taskbar_style_native.win32.FindWindow", return_value=self.bar.hwnd), \
                patch("still.taskbar_style_native.win32.FindWindowEx", return_value=None), \
                patch("still.taskbar_style_native.process_id", return_value=self.bar.pid), \
                patch("still.taskbar_style_native.window_bounds", return_value=(-400, 1020, 1520, 1080)):
            self.assertEqual(lease.update(500), shifted)
            lease.restore()
        owns_region.assert_called_once_with(self.bar.hwnd, self.bar.tray_shape)
        self.assertEqual(put.call_args_list[0].args, (self.bar.hwnd, *self.bar.tray_shape))
        self.assertEqual(put.call_args_list[1].args, (self.bar.hwnd, (0, 0, 1920, 60)))
        self.assertEqual([call.args for call in move.call_args_list], [(10, -400, 944), (10, 0, 1020)])
        self.assertEqual(move.call_args_list[0].kwargs, {"show": True})
        suppress.assert_called_once_with(self.bar.hwnd)
        lease.work_area.expand.assert_called_once_with(shifted)
        lease.work_area.restore.assert_called_once()

    @patch("still.taskbar_style_native.put_region")
    @patch("still.taskbar_style_native.move_taskbar")
    @patch("still.taskbar_style_native.taskbar", return_value=bar)
    def test_initial_layout_probe_does_not_modify_explorer(self, read, move, put):
        self.assertEqual(TaskbarLease().update(None), self.bar)
        move.assert_not_called()
        put.assert_not_called()


class BottomWorkAreaTests(unittest.TestCase):
    @patch("still.taskbar_style_native.resize_maximized_windows")
    @patch("still.taskbar_style_native.set_primary_work_area")
    @patch("still.taskbar_style_native.win32.target_monitor", return_value={"name": "primary"})
    def test_extends_only_the_bottom_and_restores_the_original_area(self, monitor, set_area, resize):
        bar = TaskbarLeaseTests.bar
        original, extended = (15, 20, 1590, 944), (15, 20, 1590, 1000)
        area = BottomWorkArea()
        with patch("still.taskbar_style_native.primary_work_area",
                   side_effect=[original, extended, extended, original]):
            area.expand(bar)
            area.restore()
        self.assertEqual([call.args for call in set_area.call_args_list], [(extended,), (original,)])
        self.assertEqual([call.args for call in resize.call_args_list], [(original, extended), (extended, original)])

    @patch("still.taskbar_style_native.set_primary_work_area")
    @patch("still.taskbar_style_native.primary_work_area", return_value=(0, 0, 1600, 900))
    def test_leaves_an_additional_bottom_reservation_alone(self, read, set_area):
        area = BottomWorkArea()
        area.expand(TaskbarLeaseTests.bar)
        area.restore()
        set_area.assert_not_called()

    @patch("still.taskbar_style_native.resize_maximized_windows")
    @patch("still.taskbar_style_native.set_primary_work_area")
    @patch("still.taskbar_style_native.win32.target_monitor", return_value={"name": "primary"})
    def test_does_not_overwrite_a_new_work_area_on_exit(self, monitor, set_area, resize):
        original, extended = (0, 0, 1600, 944), (0, 0, 1600, 1000)
        area = BottomWorkArea()
        with patch("still.taskbar_style_native.primary_work_area",
                   side_effect=[original, extended, (0, 0, 1600, 900)]):
            area.expand(TaskbarLeaseTests.bar)
            area.restore()
        set_area.assert_called_once_with(extended)
        resize.assert_called_once_with(original, extended)
