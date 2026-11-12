"""Desktop preferences against isolated files and widgets; never changes the shell."""
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from PySide6.QtCore import QObject, QPoint, QPointF, Qt
from PySide6.QtGui import QColor, QImage, QPainter, QWheelEvent
from PySide6.QtWidgets import QApplication, QCheckBox

from still.auto_collect import AutoCollector
from still.desktop_windows import paint_desktop_panel
from still.model import Store
from still.notes import NotePin, note_color
from still.organizer_model import Collections
from still.productivity_model import ToolsDB
from still.settings_sections import SettingsSection
from still import system_appearance


class DesktopPreferencesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])
        cls.app.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.temp = TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.store = Store(self.root / 'config')
        self.collections = Collections(self.store)
        self.manager = QObject()
        self.manager.store = self.store
        self.manager.collections = self.collections
        self.manager.enabled = True
        self.manager.loose_icons = {}
        self.now = 100.
        self.collector = AutoCollector(self.manager, lambda: self.now)
        self.manager.refresh = self.collector.sync

    def tearDown(self):
        self.collector.stop()
        self.temp.cleanup()

    def loose(self, name):
        path = self.root / name
        path.write_text('original bytes', encoding='utf-8')
        entry = {'id': name, 'name': name, 'target': str(path)}
        self.collections.loose.append(entry)
        return entry

    def test_one_minute_deadline_preserved_across_refresh_and_one_batch_write(self):
        entries = [self.loose('one.txt'), self.loose('two.txt')]
        self.collector.sync()
        self.now = 159.9
        self.collector.sync()
        self.collector.collect()
        self.assertEqual(self.collections.loose, entries)
        self.assertEqual(set(self.collector.deadlines.values()), {160.})
        self.now = 160.1
        with patch.object(self.collections, 'write_manifest', wraps=self.collections.write_manifest) as write:
            self.collector.collect()
            self.assertEqual(write.call_count, 1)
        self.assertFalse(self.collections.loose)
        self.assertFalse(self.collector.timer.isActive())
        self.assertFalse(self.collector.deadlines)
        self.assertTrue(all(Path(entry['target']).read_text() == 'original bytes' for entry in entries))

    def test_auto_collection_is_default_on_and_disable_has_no_timer(self):
        self.assertTrue(self.store.settings.organizer_auto_collect)
        self.loose('new.txt')
        self.collector.sync()
        self.assertTrue(self.collector.timer.isActive())
        self.store.settings.organizer_auto_collect = False
        self.collector.sync()
        self.now += 600
        self.collector.collect()
        self.assertFalse(self.collector.timer.isActive())
        self.assertEqual(len(self.collections.loose), 1)
        self.store.settings.organizer_auto_collect = True
        self.collector.sync()
        self.assertEqual(next(iter(self.collector.deadlines.values())), self.now + 60)
        self.manager.enabled = False
        self.collector.sync()
        self.assertFalse(self.collector.deadlines)

    def test_drag_hover_popup_defer_without_rescanning_desktop(self):
        entry = self.loose('drag.txt')
        self.collector.sync()
        self.now = 161
        icon = SimpleNamespace(press=(10, 10), underMouse=lambda: False)
        self.manager.loose_icons[entry['id']] = icon
        self.collector.collect()
        self.assertEqual(self.collector.deadlines[entry['id']], 166)
        self.now = 167
        icon.press = None
        with patch('still.auto_collect.QApplication.activePopupWidget', return_value=object()):
            self.collector.collect()
        self.assertEqual(self.collector.deadlines[entry['id']], 172)
        self.now = 173
        self.collector.collect()
        self.assertFalse(self.collections.loose)

    def test_failed_batch_restores_groups_loose_entries_and_defers_retry(self):
        entries = [self.loose('a.txt'), self.loose('b.txt')]
        original_groups = self.collections.groups[:]
        self.collector.sync()
        self.now = 161
        with patch.object(self.collections, 'write_manifest', side_effect=OSError('disk full')):
            self.collector.collect()
        self.assertEqual(self.collections.loose, entries)
        self.assertEqual(self.collections.groups, original_groups)
        self.assertTrue(all(e not in g['entries'] for g in original_groups for e in entries))
        self.assertEqual(set(self.collector.deadlines.values()), {221.})

    def test_missing_file_rolls_back_entire_batch_without_moving_sources(self):
        entries = [self.loose('keep.txt'), self.loose('deleted.txt')]
        Path(entries[1]['target']).unlink()
        with self.assertRaises(FileNotFoundError):
            self.collections.put_away_many(entries)
        self.assertEqual(self.collections.loose, entries)
        self.assertTrue(Path(entries[0]['target']).exists())
        self.assertFalse(any(e in g['entries'] for g in self.collections.groups for e in entries))

    def test_note_long_content_scrolls_without_scrollbars_and_geometry_survives_restart(self):
        db = ToolsDB(self.root)
        body = '\n'.join(f'Line {i}' for i in range(400))
        db.execute('INSERT INTO notes(id,title,body,color) VALUES(?,?,?,?)', ('note','Test',body,'#c74b95'))
        row = db.query('SELECT * FROM notes')[0]
        pin = NotePin(SimpleNamespace(db=db), row)
        try:
            pin.resize(410, 310)
            pin.move(70, 90)
            pin.save_geometry()
            # Avoid the native desktop layer in this isolated widget test.
            with patch.object(pin, 'position_layer'):
                pin.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen)
                pin.show()
                pin.layer_timer.stop()
                self.app.processEvents()
            self.assertEqual(pin.body_view.toPlainText(), body)
            self.assertEqual(pin.body_view.verticalScrollBarPolicy(), Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
            self.assertFalse(pin.body_view.verticalScrollBar().isVisible())
            viewport = pin.body_view.viewport()
            wheel = QWheelEvent(QPointF(40,40), QPointF(viewport.mapToGlobal(QPoint(40,40))),
                                QPoint(), QPoint(0,-120), Qt.MouseButton.NoButton,
                                Qt.KeyboardModifier.NoModifier, Qt.ScrollPhase.NoScrollPhase, False)
            self.app.sendEvent(viewport, wheel)
            self.assertGreater(pin.body_view.verticalScrollBar().value(), 0)
            second = NotePin(None, db.query('SELECT * FROM notes')[0])
            try:
                self.assertEqual((second.width(), second.height()), (410,310))
                self.assertEqual(note_color(second.row['color']).name(), '#c74b95')
            finally:
                second.close(); second.deleteLater()
        finally:
            pin.layer_timer.stop(); pin.hide(); pin.deleteLater()
            db.connection.close()

    def test_note_resize_edges_and_minimum(self):
        pin = NotePin(None, {'id':'n','title':'Title','body':'Body','color':'mint','done':0})
        try:
            self.assertEqual(pin.edge_at(QPoint(1,1)), 5)
            self.assertEqual(pin.edge_at(QPoint(pin.width()-1,pin.height()-1)), 10)
            self.assertEqual(pin.edge_at(QPoint(60,30)), 0)
            self.assertEqual(note_color('invalid'), note_color('mint'))
        finally:
            pin.deleteLater()

    def test_group_tint_changes_actual_panel_pixels(self):
        def render(color):
            image = QImage(200,160,QImage.Format.Format_ARGB32_Premultiplied)
            image.fill(Qt.GlobalColor.transparent)
            painter = QPainter(image)
            paint_desktop_panel(painter, image.rect(), 190, QColor(color))
            painter.end()
            return image.pixelColor(100,80)
        mint, rose = render('#a6e4ca'), render('#ef8ba8')
        self.assertNotEqual(mint, rose)
        self.assertGreater(mint.green(), rose.green())
        self.assertGreater(rose.red(), mint.red())

    def test_group_color_reloads_and_failed_commit_keeps_previous_color(self):
        from still.organizer import OrganizerManager
        group = self.collections.create('Test Group', 'mint')
        manager = SimpleNamespace(collections=self.collections, refresh=Mock(), error=Mock())
        OrganizerManager.set_group(manager, group, 'color', 'rose')
        self.assertEqual(Collections(Store(self.store.directory)).find(group['id'])['color'], 'rose')
        with patch.object(self.collections, 'write_manifest', side_effect=OSError('disk full')):
            OrganizerManager.set_group(manager, group, 'color', 'blue')
        self.assertEqual(group['color'], 'rose')
        manager.error.emit.assert_called_once()

    def test_collapsible_settings_hide_children_and_settle_without_idle_animation(self):
        section = SettingsSection('Group')
        section.body.addWidget(QCheckBox('Option'))
        self.assertTrue(section.content.isHidden())
        section.set_expanded(True, animate=False)
        self.assertFalse(section.content.isHidden())
        section.set_expanded(False, animate=False)
        self.assertTrue(section.content.isHidden())
        self.assertEqual(section.content.maximumHeight(), 0)
        self.assertEqual(section.animation.state(), section.animation.State.Stopped)
        section.deleteLater()

    def test_night_light_uses_supported_settings_uri_and_disables_when_unavailable(self):
        with patch.object(system_appearance, 'night_light_available', return_value=(True,'available')), \
                patch.object(system_appearance.QDesktopServices, 'openUrl', return_value=True) as opened:
            system_appearance.open_night_light()
            self.assertEqual(opened.call_args.args[0].toString(), 'ms-settings:nightlight')
        with patch.object(system_appearance, 'night_light_available', return_value=(False,'unavailable')), \
                patch.object(system_appearance.QDesktopServices, 'openUrl') as opened:
            with self.assertRaises(OSError): system_appearance.open_night_light()
            opened.assert_not_called()
        version = SimpleNamespace(major=10,build=14000,product_type=1)
        with patch.object(system_appearance.sys,'getwindowsversion', return_value=version):
            self.assertFalse(system_appearance.night_light_available()[0])


if __name__ == '__main__':
    unittest.main()
