from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import Mock, patch

from PySide6.QtCore import Qt

from still.dock import Dock, group_entries
from still.dock_windows import AppWindow
from still.model import APPS_FOLDER, Store, dock_target_key
from still.taskbar_style_native import Taskbar, TaskbarLease


class DockTests(unittest.TestCase):
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

    @patch("still.taskbar_style_native.put_region")
    @patch("still.taskbar_style_native.window_region", return_value=(2, (150, 0, 1600, 56)))
    def test_leaves_another_taskbar_customization_alone(self, region, put):
        with patch("still.taskbar_style_native.taskbar", return_value=self.bar):
            with self.assertRaisesRegex(OSError, "unsupported"):
                TaskbarLease().update(500)
        put.assert_not_called()

    @patch("still.taskbar_style_native.move_taskbar")
    @patch("still.taskbar_style_native.put_region")
    def test_centers_tray_and_restores_position_and_full_region_at_current_resolution(self, put, move):
        lease = TaskbarLease()
        shifted = Taskbar(10, 20, 30, (-400, 944, 1200, 1000), (900, 944, 1200, 1000))
        with patch("still.taskbar_style_native.taskbar", side_effect=[self.bar, shifted]), \
                patch("still.taskbar_style_native.window_region", return_value=(2, (0, 0, 1600, 56))), \
                patch("still.taskbar_style_native.region_matches", return_value=True) as owns_region, \
                patch("still.taskbar_style_native.win32.FindWindow", return_value=self.bar.hwnd), \
                patch("still.taskbar_style_native.process_id", return_value=self.bar.pid), \
                patch("still.taskbar_style_native.window_bounds", return_value=(-400, 1020, 1520, 1080)):
            self.assertEqual(lease.update(500), shifted)
            lease.restore()
        owns_region.assert_called_once_with(self.bar.hwnd, self.bar.tray_shape)
        self.assertEqual(put.call_args_list[0].args, (self.bar.hwnd, *self.bar.tray_shape))
        self.assertEqual(put.call_args_list[1].args, (self.bar.hwnd, (0, 0, 1920, 60)))
        self.assertEqual([call.args for call in move.call_args_list], [(10, -400, 944), (10, 0, 1020)])

    @patch("still.taskbar_style_native.put_region")
    @patch("still.taskbar_style_native.move_taskbar")
    @patch("still.taskbar_style_native.taskbar", return_value=bar)
    def test_initial_layout_probe_does_not_modify_explorer(self, read, move, put):
        self.assertEqual(TaskbarLease().update(None), self.bar)
        move.assert_not_called()
        put.assert_not_called()
