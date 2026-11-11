import unittest

from PySide6.QtCore import QPoint, Qt
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication

from still.recording_timeline import RecordingTimeline


class RecordingTimelineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        self.timeline = RecordingTimeline(30)
        self.timeline.resize(624, 55)
        self.timeline.setDuration(10000)
        self.timeline.show()
        self.addCleanup(self.timeline.close)

    def test_endpoints_drag_and_keyboard_are_frame_aligned_without_accumulated_rounding(self):
        t = self.timeline
        QTest.mousePress(t, Qt.MouseButton.LeftButton, pos=QPoint(round(t.x(0)), 30))
        QTest.mouseMove(t, QPoint(round(t.x(30)), 30))
        QTest.mouseRelease(t, Qt.MouseButton.LeftButton, pos=QPoint(round(t.x(30)), 30))
        self.assertEqual((t.first, t.last), (30, 300))
        for _ in range(30):
            QTest.keyClick(t, Qt.Key.Key_Right)
        self.assertEqual(t.ms(t.first), 2000)
        QTest.keyClick(t, Qt.Key.Key_Left, Qt.KeyboardModifier.ShiftModifier)
        self.assertEqual(t.first, 50)
        QTest.mouseClick(t, Qt.MouseButton.LeftButton, pos=QPoint(round(t.x(300)), 30))
        QTest.keyClick(t, Qt.Key.Key_Left)
        self.assertEqual(t.last, 299)
        self.assertEqual(t.position, t.ms(298))

    def test_handles_cannot_cross_and_reset_restores_full_range(self):
        t = self.timeline
        t.active = "start"
        t.apply_frame(10000)
        self.assertEqual((t.first, t.last), (299, 300))
        t.active = "end"
        t.apply_frame(0)
        self.assertEqual((t.first, t.last), (299, 300))
        selections = []
        t.trimChanged.connect(lambda a, b: selections.append((a, b)))
        t.reset()
        self.assertEqual(selections[-1], (0, 10000))

    def test_playhead_seeks_without_trimming_and_clamps_to_selected_range(self):
        t = self.timeline
        seeks, selections = [], []
        t.seekRequested.connect(seeks.append)
        t.trimChanged.connect(lambda *values: selections.append(values))
        QTest.mouseClick(t, Qt.MouseButton.LeftButton, pos=QPoint(round(t.x(150)), 30))
        self.assertEqual(seeks[-1], 5000)
        self.assertFalse(selections)
        t.first, t.last = 100, 200
        t.apply_frame(250)
        self.assertEqual(seeks[-1], t.ms(199))

    def test_duration_update_keeps_trim_or_extends_an_untouched_full_range(self):
        t = self.timeline
        t.setDuration(11000)
        self.assertEqual((t.first, t.last), (0, 330))
        t.first, t.last = 30, 300
        t.setDuration(12000)
        self.assertEqual((t.first, t.last), (30, 300))
