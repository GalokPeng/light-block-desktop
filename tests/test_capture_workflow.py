import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from PySide6.QtCore import QPoint, QPointF, QRect, QSize, Qt
from PySide6.QtGui import QColor, QFont, QPixmap, QWheelEvent
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QToolButton

from still.capture import AnnotationCanvas, PinWindow, next_number
from still.capture_overlay import CaptureOverlay, beside_region, pixel_rect
from still.desktop_icons import DesktopIconLease
from still.model import Store
from still.organizer_model import Collections


class FrozenScreen:
    def geometry(self):
        return QRect(0, 0, 1000, 700)

    def name(self):
        return "fixture"

    def grabWindow(self, _):
        snapshot = QPixmap(1500, 1050)
        snapshot.fill(QColor("#304253"))
        snapshot.setDevicePixelRatio(1.5)
        return snapshot


class CaptureWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])
        cls.app.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.frame = QRect(50, 110, 670, 450)
        self.control = QRect(160, 185, 270, 200)
        with patch("still.capture_overlay.win32.target_monitor", return_value={"rect": (-1500, 0, 1500, 1050)}), \
             patch("still.capture_overlay.native_targets", return_value=[(self.frame, [self.control])]):
            self.overlay = CaptureOverlay(FrozenScreen())
        self.overlay.show()
        self.app.processEvents()

    def tearDown(self):
        if not self.overlay.completed:
            self.overlay.close()

    def drag(self, start, end):
        QTest.mousePress(self.overlay, Qt.MouseButton.LeftButton, pos=start)
        QTest.mouseMove(self.overlay, end)
        QTest.mouseRelease(self.overlay, Qt.MouseButton.LeftButton, pos=end)

    def wheel(self, widget, point, delta):
        event = QWheelEvent(QPointF(point), QPointF(widget.mapToGlobal(point)), QPoint(), QPoint(0, delta),
                            Qt.MouseButton.NoButton, Qt.KeyboardModifier.NoModifier,
                            Qt.ScrollPhase.NoScrollPhase, False)
        self.app.sendEvent(widget, event)

    def test_wheel_resizes_only_the_hovered_annotation(self):
        overlay = self.overlay
        region = QRect(90, 105, 550, 400)
        overlay.selection = region
        overlay.confirm_selection()
        overlay.set_tool("text")
        QTest.mouseClick(overlay, Qt.MouseButton.LeftButton, pos=QPoint(140, 145))
        overlay.inline.editor.setPlainText("Wheel")
        self.wheel(overlay.inline.editor.viewport(), QPoint(7, 7), 120)
        self.assertEqual(overlay.inline.editor.font().pixelSize(), 22)
        self.assertEqual(overlay.inline.bar.size.currentData(), 22)
        overlay.inline.finish()
        self.wheel(overlay, QPoint(146, 151), -120)
        self.assertEqual(overlay.operations[0]["font_size"], 20)
        overlay.publish({"kind": "arrow", "points": [(140, 240), (220, 240)], "width": 4})
        overlay.publish({"kind": "line", "points": [(140, 300), (240, 300)], "width": 4})
        overlay.publish({"kind": "number", "points": [(310, 230)], "width": 4, "number": 1})
        self.wheel(overlay, QPoint(210, 244), 120)
        self.wheel(overlay, QPoint(190, 300), -120)
        self.wheel(overlay, QPoint(310, 230), -120)
        self.assertEqual(overlay.operations[1]["width"], 6)
        self.assertEqual(overlay.operations[2]["width"], 3)
        self.assertEqual(overlay.operations[3]["badge_size"], 20)
        self.wheel(overlay, QPoint(535, 330), 120)
        self.assertEqual(overlay.selection, region)
        overlay.undo()
        self.assertNotIn("badge_size", overlay.operations[3])

    def test_longshot_ignores_annotation_wheel(self):
        overlay = self.overlay
        overlay.selection = QRect(90, 105, 550, 400)
        overlay.sizing.enabled = False
        overlay.confirm_selection()
        overlay.publish({"kind": "line", "points": [(140, 300), (240, 300)], "width": 4})
        self.wheel(overlay, QPoint(190, 300), 120)
        self.assertEqual(overlay.operations[0]["width"], 4)

    def test_smart_selection_move_resize_and_pin_use_physical_pixels(self):
        QTest.mouseClick(self.overlay, Qt.MouseButton.LeftButton, pos=self.control.center())
        self.assertEqual(self.overlay.selection, self.control)
        offset = QPoint(25, 20)
        self.drag(self.control.center(), self.control.center()+offset)
        self.assertEqual(self.overlay.selection, self.control.translated(offset))
        corner = self.overlay.selection.bottomRight()
        self.drag(corner, corner+QPoint(28, 19))
        expected = self.control.translated(offset).adjusted(0, 0, 28, 19)
        self.assertEqual(self.overlay.selection, expected)
        results = []
        self.overlay.finished.connect(lambda *value: results.append(value))
        pin = next(button for button in self.overlay.findChildren(QToolButton) if button.accessibleName() == "贴图")
        QTest.mouseClick(pin, Qt.MouseButton.LeftButton)
        action, image, region = results[0]
        expected_physical = pixel_rect(expected, 1.5).translated(-1500, 0)
        self.assertEqual(action, "pin")
        self.assertEqual(region, expected_physical)
        self.assertEqual(image.size(), region.size())
        self.assertEqual(image.devicePixelRatio(), 1)

    def test_initial_drag_keeps_background_pixels_and_clears_old_outline(self):
        overlay = self.overlay
        overlay.set_smart(False)
        untouched = QPoint(600, 390)
        def pixel(point):
            image = overlay.grab(QRect(point, QSize(1, 1))).toImage()
            return image.pixelColor(0, 0)
        original = QColor("#304253")
        self.assertEqual(pixel(untouched), original)
        QTest.mousePress(overlay, Qt.MouseButton.LeftButton, pos=QPoint(230, 200))
        QTest.mouseMove(overlay, QPoint(460, 320))
        self.assertEqual(pixel(untouched), original)
        QTest.mouseMove(overlay, QPoint(360, 280))
        self.assertEqual(pixel(QPoint(460, 270)), original)
        QTest.mouseRelease(overlay, Qt.MouseButton.LeftButton, pos=QPoint(360, 280))
        self.assertEqual(overlay.selection, QRect(QPoint(230, 200), QPoint(360, 280)))
        self.assertEqual(pixel(QPoint(300, 260)), original)

    def test_right_click_clears_selection_and_draft_then_exits(self):
        overlay = self.overlay
        cancelled = []
        overlay.cancelled.connect(lambda: cancelled.append(True))
        QTest.mouseClick(overlay, Qt.MouseButton.LeftButton, pos=self.control.center())
        overlay.set_tool("text")
        point = self.control.center()
        QTest.mouseClick(overlay, Qt.MouseButton.LeftButton, pos=point)
        overlay.inline.editor.setPlainText("discard this")
        QTest.mouseClick(overlay.inline.editor.viewport(), Qt.MouseButton.RightButton, pos=QPoint(7, 7))
        self.assertTrue(overlay.selection.isEmpty())
        self.assertEqual(overlay.operations, [])
        self.assertTrue(overlay.picker.isVisible())
        self.assertFalse(overlay.inline.editor.isVisible())
        self.assertEqual(cancelled, [])
        QTest.mouseClick(overlay, Qt.MouseButton.LeftButton, pos=self.control.center())
        self.assertEqual(overlay.selection, self.control)
        QTest.mouseClick(overlay, Qt.MouseButton.RightButton, pos=point)
        QTest.mouseClick(overlay, Qt.MouseButton.RightButton, pos=point)
        self.assertEqual(cancelled, [True])

    def test_right_click_can_interrupt_the_first_drag(self):
        overlay = self.overlay
        overlay.set_smart(False)
        start, end = QPoint(180, 210), QPoint(420, 360)
        QTest.mousePress(overlay, Qt.MouseButton.LeftButton, pos=start)
        QTest.mouseMove(overlay, end)
        QTest.mouseClick(overlay, Qt.MouseButton.RightButton, pos=end)
        QTest.mouseRelease(overlay, Qt.MouseButton.LeftButton, pos=end)
        self.assertTrue(overlay.selection.isEmpty())
        self.assertTrue(overlay.picker.isVisible())
        self.assertFalse(overlay.completed)
        self.drag(start, end)
        self.assertEqual(overlay.selection, QRect(start, end))

    def test_markup_is_burned_into_export_and_counter_can_restart(self):
        self.overlay.selection = QRect(90, 110, 500, 400)
        self.overlay.confirm_selection()
        self.overlay.set_tool("highlight")
        self.drag(QPoint(120, 160), QPoint(230, 160))
        self.overlay.set_tool("number")
        QTest.mouseClick(self.overlay, Qt.MouseButton.LeftButton, pos=QPoint(150, 220))
        self.assertEqual(next_number(self.overlay.operations), 2)
        with patch("still.capture.QInputDialog.getItem", return_value=("7", True)):
            self.overlay.reset_number()
        QTest.mouseClick(self.overlay, Qt.MouseButton.LeftButton, pos=QPoint(220, 220))
        self.assertEqual(next_number(self.overlay.operations), 8)
        self.overlay.undo()
        self.assertEqual(next_number(self.overlay.operations), 7)
        self.overlay.redo()
        self.overlay.set_tool("text")
        QTest.mouseClick(self.overlay, Qt.MouseButton.LeftButton, pos=QPoint(150, 300))
        QTest.keyClicks(self.overlay.inline.editor, "Text")
        clean, _ = self.overlay.crop(False)
        marked, _ = self.overlay.crop(True)
        self.assertNotEqual(clean, marked)
        self.assertEqual(clean.size(), marked.size())
        highlight = marked.pixelColor(70, 75)
        self.assertNotEqual(highlight, clean.pixelColor(70, 75))
        self.assertNotEqual(highlight, QColor("#ff6868"))

    def test_pin_double_click_copies_and_border_toggle_hides_grip(self):
        image = self.overlay.snapshot.toImage()
        pin = PinWindow(None, image)
        pin.show()
        pin.toggle_border()
        self.assertFalse(pin.border_visible)
        self.assertTrue(pin.grip.isHidden())
        clipboard = Mock()
        with patch.object(QApplication, "clipboard", return_value=clipboard):
            QTest.mouseDClick(pin, Qt.MouseButton.LeftButton, pos=pin.rect().center())
        clipboard.setImage.assert_called_once()
        self.assertFalse(pin.isVisible())

    def test_preview_placement_leaves_the_capture_region_free(self):
        region = QRect(250, 130, 300, 410)
        area = QRect(0, 0, 1000, 720)
        size = QSize(310, 370)
        location = beside_region(region, area, size)
        self.assertTrue(area.contains(QRect(location, size)))
        self.assertFalse(region.intersects(QRect(location, size)))

    def test_capture_options_are_selection_only(self):
        self.assertFalse(self.overlay.color.isEditable())
        self.assertFalse(self.overlay.stroke.isEditable())
        self.overlay.set_tool("text")
        bar = self.overlay.inline.bar
        self.assertFalse(bar.family.isEditable())
        self.assertFalse(bar.size.isEditable())
        self.assertFalse(bar.color.isEditable())
        bar.family.setCurrentIndex(0)
        self.assertTrue(bar.family.currentFont().family())

    def test_inline_text_can_be_styled_moved_reopened_and_undone(self):
        self.overlay.selection = QRect(90, 110, 500, 400)
        self.overlay.confirm_selection()
        self.overlay.set_tool("text")
        inline = self.overlay.inline
        anchor = QPoint(150, 165)
        QTest.mouseClick(self.overlay, Qt.MouseButton.LeftButton, pos=anchor)
        self.assertTrue(inline.editor.isVisible())
        QTest.keyClicks(inline.editor, "Still")
        QTest.keyClick(inline.editor, Qt.Key.Key_Return)
        QTest.keyClicks(inline.editor, "Notes")
        inline.bar.family.setCurrentFont(QFont("Segoe UI"))
        inline.bar.size.setCurrentIndex(inline.bar.size.findData(32))
        inline.bar.color.setCurrentIndex(inline.bar.color.findData("#68adff"))
        inline.bar.effects["bold"].setChecked(True)
        self.assertEqual(inline.editor.font().pixelSize(), 32)
        QTest.mouseClick(self.overlay, Qt.MouseButton.LeftButton, pos=QPoint(400, 350))
        self.assertFalse(inline.editor.isVisible())
        operation = self.overlay.operations[0]
        self.assertEqual(operation["text"], "Still\nNotes")
        self.assertTrue(operation["bold"])
        self.assertEqual(operation["color"], "#68adff")
        self.assertEqual(operation["font_size"], 32)
        delta = QPoint(40, 55)
        self.drag(anchor+QPoint(8, 8), anchor+QPoint(8, 8)+delta)
        self.assertEqual(self.overlay.operations[0]["points"], [(190, 220)])
        QTest.mouseDClick(self.overlay, Qt.MouseButton.LeftButton, pos=anchor+delta+QPoint(8, 8))
        self.assertTrue(inline.editor.isVisible())
        inline.editor.setPlainText("截图文字")
        marked, _ = self.overlay.crop()
        self.assertEqual(self.overlay.operations[0]["text"], "截图文字")
        clean, _ = self.overlay.crop(False)
        self.assertNotEqual(marked, clean)
        self.overlay.undo()
        self.assertEqual(self.overlay.operations[0]["text"], operation["text"])
        self.overlay.undo()
        self.assertEqual(self.overlay.operations[0], operation)
        self.overlay.redo()
        self.assertEqual(self.overlay.operations[0]["points"], [(190, 220)])

    def test_editor_text_can_be_moved_after_crop_and_burned_into_result(self):
        bar_widgets = []
        canvas = AnnotationCanvas(self.overlay.snapshot.toImage(), bar_widgets.append)
        canvas.resize(640, 520)
        canvas.show()
        self.app.processEvents()
        try:
            canvas.set_tool("text")
            anchor = canvas.image_area.center().toPoint()
            QTest.mouseClick(canvas, Qt.MouseButton.LeftButton, pos=anchor)
            canvas.inline.editor.setPlainText("Inline")
            canvas.result()
            canvas.publish({"kind": "crop", "rect": [150, 150, 1100, 750]})
            self.app.processEvents()
            hit = canvas.inline.rect_for(canvas.operations[0], 0).center().toPoint()
            QTest.mouseDClick(canvas, Qt.MouseButton.LeftButton, pos=hit)
            self.assertTrue(canvas.inline.editor.isVisible())
            canvas.inline.editor.setPlainText("Edited")
            before = canvas.result().copy()
            QTest.mousePress(canvas, Qt.MouseButton.LeftButton, pos=hit)
            QTest.mouseMove(canvas, hit + QPoint(46, 36))
            QTest.mouseRelease(canvas, Qt.MouseButton.LeftButton, pos=hit + QPoint(46, 36))
            self.assertNotEqual(canvas.result(), before)
            self.assertEqual(canvas.result().size(), QSize(1100, 750))
        finally:
            canvas.close()


class AutoClassifyTests(unittest.TestCase):
    def test_classification_keeps_original_paths_and_existing_custom_group(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            desktop = root / "desktop"
            desktop.mkdir()
            (desktop / "folder").mkdir()
            for name in ("Word.DOCX", "Link.lnk", "picture.PNG", "data.xyz", "desktop.ini"):
                (desktop / name).write_text(name, encoding="utf8")
            store = Store(root / "config")
            self.assertTrue(store.settings.capture_hotkey)
            collection = Collections(store)
            custom = collection.create("我的收藏")
            collection.add_paths(custom, [desktop / "picture.PNG"])
            rect = lambda: [10+len(collection.groups)*300, 30, 280, 220]
            self.assertEqual(collection.classify_desktop([desktop], rect), 7)
            groups = {g["auto_category"]: g for g in collection.groups if "auto_category" in g}
            self.assertEqual(groups["documents"]["entries"][0]["name"], "Word.DOCX")
            self.assertFalse(groups["shortcuts"]["collapsed"])
            self.assertEqual(groups["folders"]["icon_size"], 44)
            self.assertEqual(custom["entries"][0]["name"], "picture.PNG")
            self.assertEqual(len(list(desktop.iterdir())), 6)
            reloaded = Collections(Store(root / "config"))
            self.assertEqual(reloaded.classify_desktop([desktop], rect), 0)

    def test_legacy_managed_entry_returns_to_desktop_and_remains_a_reference(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            desktop = root / "desktop"
            desktop.mkdir()
            item = desktop / "legacy.txt"
            item.write_bytes(b"legacy")
            collection = Collections(Store(root / "config"))
            group = collection.create("旧分组")
            moved, errors = collection.collect(group, [item], [desktop])
            self.assertFalse(errors)
            self.assertFalse(item.exists())
            collection.restore_entry(group, moved[0], keep_reference=True)
            self.assertEqual(item.read_bytes(), b"legacy")
            self.assertEqual(group["entries"][0]["target"], str(item))
            self.assertFalse(group["entries"][0]["managed"])

    def test_visibility_lease_keeps_original_state_after_restart(self):
        for initial in (False, True):
            with self.subTest(originally_hidden=initial), tempfile.TemporaryDirectory() as folder:
                hidden = initial

                def flags(new=None):
                    nonlocal hidden
                    previous = hidden
                    if new is not None:
                        hidden = new
                    return previous

                with patch("still.desktop_icons.icons_hidden", side_effect=flags), \
                     patch.object(DesktopIconLease, "claim"):
                    lease = DesktopIconLease(Path(folder))
                    try:
                        lease.hide()
                        self.assertTrue(hidden)
                        self.assertTrue(lease.path.exists())
                    finally:
                        lease.release_handle()
                    resumed = DesktopIconLease(Path(folder))
                    try:
                        resumed.hide()
                        resumed.restore()
                        self.assertEqual(hidden, initial)
                        self.assertFalse(resumed.path.exists())
                    finally:
                        resumed.release_handle()


if __name__ == "__main__":
    unittest.main()
