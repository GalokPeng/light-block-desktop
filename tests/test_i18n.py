import ast
from pathlib import Path
from string import Formatter
import unittest
from unittest.mock import patch

from still.i18n import english_messages, language_for_id, tr
from still.model import bundled_items, item_name
from still.organizer_model import entry_name, group_name


class LocalizationTests(unittest.TestCase):
    def test_selects_windows_display_language_with_english_fallback(self):
        for langid in (0x0804, 0x0404, 0x0C04, 0x1004):
            self.assertEqual(language_for_id(langid), "zh")
        for langid in (0x0409, 0x0809, 0x0411):
            self.assertEqual(language_for_id(langid), "en")

    def test_all_literal_messages_have_matching_english_entries(self):
        catalog = english_messages()
        formatter = Formatter()
        for path in (Path(__file__).resolve().parents[1] / "still").glob("*.py"):
            for node in ast.walk(ast.parse(path.read_text(encoding="utf-8"))):
                if not (isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
                        and node.func.id == "tr" and isinstance(node.args[0], ast.Constant)):
                    continue
                source = node.args[0].value
                self.assertIn(source, catalog, f"{path.name}:{node.lineno}")
                self.assertTrue(catalog[source])
                self.assertCountEqual(
                    [field for _, field, _, _ in formatter.parse(source) if field is not None],
                    [field for _, field, _, _ in formatter.parse(catalog[source]) if field is not None],
                    f"{path.name}:{node.lineno}")

    def test_translates_default_labels_without_rewriting_user_data(self):
        item = bundled_items()[0]
        group = {"name": "文件夹", "auto_category": "folders"}
        entry = {"name": "此电脑", "system": "computer"}
        with patch("still.i18n.display_language", return_value="en"):
            self.assertEqual(tr("打开"), "Open")
            self.assertEqual(item_name(item), "Dusk Dunes")
            self.assertEqual(group_name(group), "Folders")
            self.assertEqual(entry_name(entry), "This PC")
            self.assertEqual(group_name(dict(group, name="我的文件")), "我的文件")
            self.assertEqual(item_name(dict(item, builtin=False)), "暮色沙丘")
            self.assertEqual(group_name(dict(group, name="")), "")
        with patch("still.i18n.display_language", return_value="zh"):
            self.assertEqual(tr("打开"), "打开")
            self.assertEqual(item_name(item), "暮色沙丘")
            self.assertEqual(group_name(group), "文件夹")
            self.assertEqual(entry_name(entry), "此电脑")
