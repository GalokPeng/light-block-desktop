from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from PySide6.QtCore import Qt
from PySide6.QtGui import QFont, QIcon, QImage
from PySide6.QtWidgets import QApplication, QStyle, QStyleOptionComboBox

from still.cursor_art import render
from still.cursor_cache import CursorBuild
from still.i18n import set_language
from still.model import Store
from still.notes import NotesWindow
from still.workbench import Workbench


class AppearanceTests(unittest.TestCase):
    def test_custom_note_color_is_readable_and_keeps_only_one_custom_entry(self):
        app=QApplication.instance() or QApplication([])
        app.setQuitOnLastWindowClosed(False)
        for language in ("zh","en"):
            with TemporaryDirectory() as directory:
                set_language(language)
                workbench=Workbench(SimpleNamespace(app=app,store=Store(Path(directory)),icon=QIcon()))
                editor=NotesWindow(workbench)
                try:
                    editor.setFont(QFont("Microsoft YaHei UI",11))
                    editor.create()
                    for color in ("#e185ad","#71c3ea","#e185ad"):
                        editor.select_color(color)
                        editor.dirty=True;editor.save()
                        self.assertEqual(editor.engine.get(editor.loaded_id)["color"],color)
                        self.assertEqual(editor.color.count(),5)
                        self.assertEqual(editor.color.currentData(),color)
                        self.assertFalse(editor.color.itemIcon(editor.color.currentIndex()).isNull())
                        self.assertEqual(editor.color.toolTip(),color.upper())
                    combo=editor.color
                    combo.resize(combo.sizeHint())
                    option=QStyleOptionComboBox();combo.initStyleOption(option)
                    rect=combo.style().subControlRect(QStyle.ComplexControl.CC_ComboBox,option,
                                                    QStyle.SubControl.SC_ComboBoxEditField,combo)
                    self.assertGreater(rect.width(),combo.fontMetrics().horizontalAdvance(combo.currentText())+20)
                finally:
                    editor.close();workbench.cleanup()
        set_language("system")

    def test_custom_cursor_cache_reuses_complete_entries_and_does_not_prune_unrelated_data(self):
        with TemporaryDirectory() as directory:
            cache=Path(directory)
            for n in range(10):
                (cache/f'2-stream-{n:06x}').mkdir()
            unrelated=cache/'personal-folder';unrelated.mkdir()
            marker=unrelated/'keep.txt';marker.write_text('keep')
            job=CursorBuild(cache,'outline','#78a9ed')
            result=[];errors=[];job.ready.connect(result.append);job.failed.connect(errors.append)
            job.run()
            self.assertFalse(errors,errors)
            target=Path(result[0])
            self.assertTrue((target/'busy-128.ani').is_file())
            self.assertEqual(len([p for p in cache.iterdir() if p.name.startswith('2-')]),8)
            self.assertEqual(marker.read_text(),'keep')
            with patch('still.cursor_cache.write_scheme',side_effect=AssertionError('regenerated cached scheme')):
                job.run()
            self.assertFalse(errors,errors)
            self.assertEqual(result,[str(target),str(target)])
            for theme,color in (('../escape','#78a9ed'),('stream','../../oops')):
                with self.assertRaises(ValueError):CursorBuild(cache,theme,color)

    def test_same_color_still_has_distinct_silhouettes_and_custom_color_reaches_artwork(self):
        for role in ('normal','text','hand','move','busy'):
            masks=[]
            for theme in ('stream','outline','pixel'):
                image,_=render(theme,role,48,color='#80c9a5')
                alpha=image.convertToFormat(QImage.Format.Format_Alpha8)
                masks.append(bytes(alpha.constBits()))
            self.assertEqual(len(set(masks)),3,role)
        for theme in ('stream','outline','pixel'):
            first,hotspot=render(theme,'normal',64,color='#80c9a5')
            second,other=render(theme,'normal',64,color='#dc89a8')
            self.assertEqual(hotspot,other)
            self.assertNotEqual(bytes(first.constBits()),bytes(second.constBits()))
