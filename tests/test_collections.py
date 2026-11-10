import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from still.model import Store
from still.organizer_model import Collections, named_entries


class CollectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.desktop = self.root / "Desktop"
        self.desktop.mkdir()
        self.store = Store(self.root / "config")
        self.collections = Collections(self.store)
        self.group = self.collections.create("中文分组")

    def tearDown(self):
        self.temp.cleanup()

    def test_time_order_is_newest_first_and_name_order_still_works(self):
        for name, date in (("Alpha.txt", 1700000000), ("Zulu.txt", 1700000200)):
            path = self.desktop / name
            path.touch()
            os.utime(path, (date, date))
            self.collections.add_paths(self.group, [path])
        self.assertEqual([entry["name"] for entry, _ in named_entries([self.group], order="time")],
                         ["Zulu.txt", "Alpha.txt"])
        self.assertEqual([entry["name"] for entry, _ in named_entries([self.group])],
                         ["Alpha.txt", "Zulu.txt"])

    def test_shelf_lists_system_entries_first_and_sorts_the_remaining_names(self):
        system = self.collections.create("System")
        for key in ("settings", "computer", "recycle"):
            self.collections.add_system(system, key)
        for name in ("Zulu.txt", "alpha.txt"):
            (self.desktop / name).touch()
            self.collections.add_paths(self.group, [self.desktop / name])
        matches = named_entries(self.collections.groups)
        self.assertEqual([entry["system"] for entry, _ in matches[:3]], ["computer", "recycle", "settings"])
        self.assertEqual([entry["name"] for entry, _ in matches[3:]], ["alpha.txt", "Zulu.txt"])
        self.assertEqual(named_entries(self.collections.groups, "ALPHA 中文"), [matches[3]])

    def test_desktop_sync_observes_new_entries_and_preserves_manual_references(self):
        original = self.desktop / "Existing.txt"
        original.touch()
        snapshot = {}
        self.collections.classify_desktop([self.desktop], snapshot=snapshot)
        documents = next(group for group in self.collections.groups if group.get("auto_category") == "documents")
        documents["entries"].clear()  # An entry was removed from the shelf, while the file stayed on the desktop.
        new_file = self.desktop / "New.txt"
        new_file.write_text("file contents", encoding="utf-8")
        manual = self.desktop / "Manual.txt"
        manual.touch()
        self.collections.add_paths(self.group, [manual])
        self.assertEqual(self.collections.classify_desktop([self.desktop], snapshot=snapshot, only_new=True), 1)
        self.assertEqual([entry["target"] for entry in documents["entries"]], [str(new_file)])
        with patch.object(self.collections, "write_manifest") as write:
            self.assertEqual(self.collections.classify_desktop([self.desktop], snapshot=snapshot, only_new=True), 0)
            write.assert_not_called()
        renamed = new_file.rename(self.desktop / "Renamed.txt")
        manual.unlink()
        self.assertEqual(self.collections.classify_desktop([self.desktop], snapshot=snapshot, only_new=True), 1)
        self.assertEqual([entry["target"] for entry in documents["entries"]], [str(renamed)])
        self.assertEqual([entry["target"] for entry in self.group["entries"]], [str(manual)])
        self.assertEqual(renamed.read_text(encoding="utf-8"), "file contents")
        self.assertTrue(original.exists())

    def test_reference_does_not_move_and_cross_group_transfer(self):
        source = self.desktop / "我的文件.txt"
        source.write_text("original", encoding="utf-8")
        entry = self.collections.add_paths(self.group, [source])[0]
        self.collections.add_paths(self.group, [source])
        self.assertEqual(len(self.group["entries"]), 1)
        target = self.collections.create("另一个分组")
        self.collections.move_entries(self.group, target, [entry["id"]])
        self.assertFalse(self.group["entries"])
        self.collections.remove_group(target)
        self.assertEqual(source.read_text(encoding="utf-8"), "original")

    def test_collect_and_restore_never_overwrites_a_collision(self):
        source = self.desktop / "快捷方式.lnk"
        source.write_bytes(b"original-shortcut")
        entries, errors = self.collections.collect(self.group, [source], [self.desktop])
        self.assertFalse(errors)
        self.assertFalse(source.exists())
        source.write_bytes(b"new-file")
        self.collections.restore_entry(self.group, entries[0])
        self.assertEqual(source.read_bytes(), b"new-file")
        self.assertEqual((self.desktop / "快捷方式 (2).lnk").read_bytes(), b"original-shortcut")
        self.assertFalse(self.store.settings.organizer_pending)

    def test_reject_outside_desktop_and_failed_move_preserves_source(self):
        outside = self.root / "private.txt"
        outside.write_text("keep")
        added, errors = self.collections.collect(self.group, [outside], [self.desktop])
        self.assertFalse(added)
        self.assertTrue(errors)
        self.assertTrue(outside.exists())
        source = self.desktop / "protected.txt"
        source.write_text("keep")
        with patch("still.organizer_model.os.rename", side_effect=PermissionError("denied")):
            added, errors = self.collections.collect(self.group, [source], [self.desktop])
        self.assertTrue(source.exists())
        self.assertFalse(added)
        self.assertTrue(errors)

    def test_crash_recovery_preserves_managed_item(self):
        source = self.desktop / "待收纳.txt"
        source.write_text("important")
        target = self.store.directory / "organizer_files" / "stash.txt"
        target.parent.mkdir(exist_ok=True)
        entry = {"id": "pending", "name": source.name, "target": str(target), "original_path": str(source), "managed": True}
        self.store.settings.organizer_pending.append({"group_id": self.group["id"], "entry": entry})
        self.store.save()
        source.rename(target)  # Simulate process termination before commit.
        recovered = Collections(Store(self.store.directory))
        group = recovered.find(self.group["id"])
        self.assertEqual(group["entries"][0]["id"], "pending")
        recovered.restore_entry(group, group["entries"][0])
        self.assertEqual(source.read_text(), "important")

    def test_remove_group_restores_managed_directory(self):
        folder = self.desktop / "项目目录"
        folder.mkdir()
        (folder / "data.txt").write_text("content")
        added, errors = self.collections.collect(self.group, [folder], [self.desktop])
        self.assertTrue(added)
        self.assertFalse(errors)
        self.assertFalse(self.collections.remove_group(self.group))
        self.assertEqual((folder / "data.txt").read_text(), "content")

    def test_independent_manifest_survives_old_version_save_and_broken_settings(self):
        source = self.desktop / "important.txt"
        source.write_text("important")
        self.collections.collect(self.group, [source], [self.desktop])
        # Old applications know only wallpaper settings and discard newer keys.
        self.store.file.write_text('{"fit": "fill", "library": []}', encoding="utf-8")
        reopened = Collections(Store(self.store.directory))
        recovered = reopened.find(self.group["id"])
        self.assertEqual(len(recovered["entries"]), 1)
        self.store.file.write_text('{broken', encoding="utf-8")
        reopened = Collections(Store(self.store.directory))
        recovered = reopened.find(self.group["id"])
        reopened.restore_entry(recovered, recovered["entries"][0])
        self.assertEqual(source.read_text(), "important")

    def test_receipts_recover_when_both_manifests_and_settings_are_broken(self):
        source = self.desktop / "critical.txt"
        source.write_text("keep")
        self.collections.collect(self.group, [source], [self.desktop])
        for file in (self.store.file, self.collections.manifest, self.collections.backup):
            file.write_text("bad json", encoding="utf-8")
        recovered = Collections(Store(self.store.directory))
        group = recovered.find(self.group["id"])
        self.assertIsNotNone(group)
        recovered.restore_entry(group, group["entries"][0])
        self.assertEqual(source.read_text(), "keep")

    def test_same_group_move_keeps_drop_position(self):
        self.group["entries"] = [{"id": name, "name": name, "target": name} for name in "ABCD"]
        self.collections.move_entries(self.group, self.group, ["B"], 3)
        self.assertEqual([e["id"] for e in self.group["entries"]], list("ACBD"))
        self.collections.move_entries(self.group, self.group, ["B", "D"], 0)
        self.assertEqual([e["id"] for e in self.group["entries"]], list("BDAC"))

    def test_batch_invalid_reference_does_not_partially_change_group(self):
        source = self.desktop / "valid.txt"
        source.write_text("keep")
        with self.assertRaises(FileNotFoundError):
            self.collections.add_paths(self.group, [source, self.desktop / "missing.txt"])
        self.assertEqual(self.group["entries"], [])


if __name__ == "__main__":
    unittest.main()
