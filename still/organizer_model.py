"""Persistent collections and reversible desktop moves, without Qt/UI state."""
from __future__ import annotations

import os
import json
import shutil
import stat
from pathlib import Path
import uuid

from .i18n import tr

COLORS = {"mint": "#a6e4ca", "blue": "#9ac8ff", "violet": "#cbb7f7", "amber": "#f5d18e", "rose": "#efb5c5"}
SYSTEM_ENTRIES = {"computer": ("此电脑", "shell:MyComputerFolder"),
                  "recycle": ("回收站", "shell:RecycleBinFolder"),
                  "settings": ("Windows 设置", "ms-settings:")}
SYSTEM_ORDER = {target.casefold(): index for index, (_, target) in enumerate(SYSTEM_ENTRIES.values())}
DESKTOP_CATEGORIES = (
    ("folders", "文件夹", "mint", ()),
    ("shortcuts", "快捷方式", "blue", (".lnk", ".url", ".appref-ms", ".website", ".exe")),
    ("system", "系统图标", "violet", ()),
    ("documents", "文档与表格", "amber", (".txt", ".md", ".pdf", ".doc", ".docx", ".xls", ".xlsx", ".csv", ".ppt", ".pptx", ".rtf", ".odt")),
    ("images", "图片与设计", "rose", (".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".svg", ".tif", ".tiff", ".ico", ".psd", ".ai", ".heic")),
    ("media", "视频与音频", "blue", (".mp4", ".mkv", ".mov", ".avi", ".webm", ".m4v", ".mp3", ".wav", ".flac", ".m4a", ".ogg")),
    ("archives", "压缩包与安装包", "amber", (".zip", ".7z", ".rar", ".gz", ".tar", ".msi", ".msix", ".iso")),
    ("other", "其他文件", "violet", ()),
)
CATEGORY_NAMES = {key: name for key, name, _, _ in DESKTOP_CATEGORIES}
SUFFIX_CATEGORY = {suffix: key for key, _, _, extensions in DESKTOP_CATEGORIES for suffix in extensions}


def group_name(group):
    name = group["name"]
    return tr(name) if name == CATEGORY_NAMES.get(group.get("auto_category")) else name


def entry_name(entry):
    name = entry["name"]
    default = SYSTEM_ENTRIES.get(entry.get("system"))
    return tr(name) if default and name == default[0] else name


def organizer_view(settings):
    return settings.organizer_views.get(settings.organizer_alignment, "name")


def view_options():
    return (("name", tr("按名称排列")), ("time", tr("按修改时间排列")), ("groups", tr("按分组排列")))


def entry_modified(entry):
    try:
        return Path(entry["target"]).stat().st_mtime_ns
    except OSError:
        return -1


def named_entries(groups, query="", *, order="name"):
    words = query.casefold().split()
    matches = []
    for group in groups:
        category = group_name(group).casefold()
        for entry in group["entries"]:
            name = entry_name(entry).casefold()
            if all(word in name or word in category for word in words):
                priority = SYSTEM_ORDER.get(entry["target"].casefold(), len(SYSTEM_ORDER))
                modified = -entry_modified(entry) if order == "time" and priority == len(SYSTEM_ORDER) else 0
                matches.append((priority, modified, name, entry, group))
    matches.sort(key=lambda match: match[:3])
    return [(entry, group) for _, _, _, entry, group in matches]


def identifier():
    return uuid.uuid4().hex


def unique_path(path: Path) -> Path:
    candidate = path
    index = 2
    while candidate.exists():
        candidate = path.with_name(f"{path.stem} ({index}){path.suffix}")
        index += 1
    return candidate


class Collections:
    def __init__(self, store):
        self.store = store
        self.manifest = store.directory / "collections.json"
        self.backup = store.directory / "collections.backup.json"
        self.receipts = store.directory / "organizer_files" / ".receipts"
        self._load_independent_manifest()
        if not isinstance(store.settings.organizer_groups, list):
            store.settings.organizer_groups = []
        if not isinstance(store.settings.organizer_loose, list):
            store.settings.organizer_loose = []
        self.loose[:] = [e for e in self.loose if isinstance(e, dict)
                        and isinstance(e.get("id"), str) and isinstance(e.get("target"), str)]
        self.groups[:] = [g for g in self.groups if isinstance(g, dict) and isinstance(g.get("id"), str)]
        for group in self.groups:
            if not isinstance(group.get("name"), str):
                group["name"] = tr("未命名分组")
            group.setdefault("entries", [])
            if not isinstance(group["entries"], list):
                group["entries"] = []
            group["entries"][:] = [e for e in group["entries"] if isinstance(e, dict)
                                    and isinstance(e.get("id"), str) and isinstance(e.get("target"), str)]
            group.setdefault("color", "mint")
            group.setdefault("rect", [80, 90, 350, 300])
            group.setdefault("icon_size", 44)
            group.setdefault("collapsed", False)
            group.setdefault("locked", False)
        self._recover_receipts()
        self.store.sidecar_savers.append(self.write_manifest)
        self.recover()

    def _load_independent_manifest(self):
        for file in (self.manifest, self.backup):
            if not file.exists():
                continue
            try:
                data = json.loads(file.read_text(encoding="utf-8"))
                if not isinstance(data.get("groups"), list) or not isinstance(data.get("pending"), list):
                    raise ValueError("Invalid collection manifest")
                self.store.settings.organizer_groups = data["groups"]
                self.store.settings.organizer_pending = data["pending"]
                self.store.settings.organizer_loose = data.get("loose", [])
                if file == self.backup:
                    damaged = self.manifest.with_name("collections.damaged-"+identifier()[:8]+".json")
                    if self.manifest.exists():
                        self.manifest.replace(damaged)
                    self.store.warning = tr("收纳清单已从独立备份恢复。")
                return
            except (OSError, ValueError, TypeError, AttributeError):
                continue

    def write_manifest(self):
        data = {"version": 1, "groups": self.groups, "pending": self.store.settings.organizer_pending, "loose": self.loose}
        self.manifest.parent.mkdir(parents=True, exist_ok=True)
        if self.manifest.exists():
            try:
                previous = json.loads(self.manifest.read_text(encoding="utf-8"))
                if isinstance(previous, dict) and isinstance(previous.get("groups"), list):
                    shutil.copyfile(self.manifest, self.backup)
            except (OSError, ValueError):
                pass
        temp = self.manifest.with_suffix(".tmp")
        with temp.open("w", encoding="utf-8") as file:
            json.dump(data, file, ensure_ascii=False, indent=2)
            file.flush()
            os.fsync(file.fileno())
        os.replace(temp, self.manifest)

    def _receipt(self, group, entry, state):
        self.receipts.mkdir(parents=True, exist_ok=True)
        data = {"entry": entry, "state": state, "group": {k: group[k] for k in ("id", "name", "color", "rect")}}
        file = self.receipts / (entry["id"] + ".json")
        temp = file.with_suffix(".tmp")
        temp.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
        os.replace(temp, file)

    def _valid_managed_target(self, entry):
        try:
            vault = (self.store.directory / "organizer_files").resolve()
            target = Path(entry["target"]).resolve()
            return target.is_relative_to(vault) and target != vault and not Path(entry["target"]).is_symlink()
        except (OSError, ValueError, KeyError):
            return False

    def _recover_receipts(self):
        if not self.receipts.exists():
            return
        known = {e.get("id") for g in self.groups for e in g.get("entries", [])}
        for receipt in self.receipts.glob("*.json"):
            try:
                data = json.loads(receipt.read_text(encoding="utf-8"))
                entry, info = data["entry"], data["group"]
                if entry["id"] in known or not self._valid_managed_target(entry) or not Path(entry["target"]).exists():
                    continue
                if data.get("state") == "pending" and Path(entry["original_path"]).exists():
                    continue
                group = self.find(info["id"])
                if group is None:
                    group = {**info, "entries": [], "icon_size": 44, "collapsed": False, "locked": False}
                    self.groups.append(group)
                group["entries"].append(entry)
                known.add(entry["id"])
                self.store.warning = tr("已从独立文件凭据恢复收纳项目。")
            except (OSError, ValueError, KeyError, TypeError):
                continue

    @property
    def groups(self):
        return self.store.settings.organizer_groups

    @property
    def loose(self):
        return self.store.settings.organizer_loose

    def find(self, ident):
        return next((g for g in self.groups if g["id"] == ident), None)

    def create(self, name=None, color="mint", rect=None, save=True):
        if name is None:
            name = tr("新的分组")
        group = {"id": identifier(), "name": name.strip()[:40], "color": color if color in COLORS else "mint",
                 "rect": rect or [100 + len(self.groups)*32, 100 + len(self.groups)*32, 350, 300],
                 "icon_size": 44, "collapsed": False, "locked": False, "entries": []}
        self.groups.append(group)
        if save:
            self.store.save()
        return group

    def classify_desktop(self, roots, next_rect=None, *, snapshot=None, only_new=False, defer_new=False):
        """Build file references in a single manifest write, leaving Desktop files in place."""
        known = {os.path.normcase(e["target"]) for g in self.groups for e in g["entries"]}
        known.update(os.path.normcase(e["target"]) for e in self.loose)
        new_loose = []
        buckets = {key: [] for key, _, _, _ in DESKTOP_CATEGORIES}
        listed, observed = {}, {}
        for root in roots:
            root_key = os.path.normcase(str(root.absolute()))
            try:
                paths = sorted(root.iterdir(), key=lambda p: (p.suffix.lower(), p.name.casefold()))
            except OSError:
                continue
            present = listed.setdefault(root_key, set())
            seen = observed.setdefault(root_key, set())
            previous = snapshot.get(root_key, ()) if only_new and snapshot is not None else ()
            for path in paths:
                if path.name.startswith(".") or path.name.lower() == "desktop.ini":
                    continue
                target = str(path.absolute())
                normalized = os.path.normcase(target)
                present.add(normalized)
                if normalized in known or normalized in previous:
                    seen.add(normalized)
                    continue
                try:
                    info = path.stat()
                except OSError:
                    continue
                if getattr(info, "st_file_attributes", 0) & 0x02:
                    continue
                seen.add(normalized)
                key = "folders" if stat.S_ISDIR(info.st_mode) else SUFFIX_CATEGORY.get(path.suffix.lower(), "other")
                name = path.stem if key == "shortcuts" else path.name
                entry = {"id": identifier(), "name": name, "target": target, "managed": False, "desktop_auto": True}
                (new_loose if defer_new else buckets[key]).append(entry)
                known.add(normalized)
        for key, (name, target) in SYSTEM_ENTRIES.items():
            if os.path.normcase(target) not in known:
                buckets["system"].append({"id": identifier(), "name": name, "target": target, "managed": False, "system": key})
        original_groups = self.groups[:]
        original_entries = [(g, g["entries"][:]) for g in original_groups]
        original_loose = self.loose[:]
        removed = 0
        for group, entries in original_entries:
            kept = []
            for entry in entries:
                if entry.get("desktop_auto") and not entry.get("managed"):
                    target = Path(entry["target"])
                    present = listed.get(os.path.normcase(str(target.parent)))
                    if present is not None and os.path.normcase(str(target)) not in present:
                        removed += 1
                        continue
                kept.append(entry)
            group["entries"][:] = kept
        initialized = self.store.settings.organizer_auto_classified
        self.loose[:] = [e for e in self.loose if
                        os.path.normcase(str(Path(e["target"]).parent)) not in listed or
                        os.path.normcase(e["target"]) in listed[os.path.normcase(str(Path(e["target"]).parent))]]
        removed += len(original_loose) - len(self.loose)
        self.loose.extend(new_loose)
        count = len(new_loose)
        for key, name, color, _ in DESKTOP_CATEGORIES:
            entries = buckets[key]
            group = next((g for g in self.groups if g.get("auto_category") == key), None)
            if group is None and (entries or not initialized and key in {"folders", "shortcuts", "system", "other"}):
                group = self.create(name, color, next_rect() if next_rect else None, save=False)
                group["auto_category"] = key
            if group is not None:
                group["entries"].extend(entries)
                count += len(entries)
        try:
            if not initialized:
                self.store.settings.organizer_auto_classified = True
                self.store.save()
            elif count or removed:
                self.write_manifest()
        except OSError:
            self.store.settings.organizer_auto_classified = initialized
            self.groups[:] = original_groups
            for group, entries in original_entries:
                group["entries"][:] = entries
            self.loose[:] = original_loose
            raise
        if snapshot is not None:
            snapshot.update(observed)
        return count

    def put_away(self, entry, group=None):
        """Move a loose reference into a group; never move the desktop file."""
        if entry not in self.loose:
            return
        if not Path(entry["target"]).exists():
            raise FileNotFoundError(tr("找不到：{0}").format(entry["name"]))
        previous_groups = self.groups[:]
        previous_loose = self.loose[:]
        if group is None:
            path = Path(entry["target"])
            category = "folders" if path.is_dir() else SUFFIX_CATEGORY.get(path.suffix.lower(), "other")
            group = next((g for g in self.groups if g.get("auto_category") == category), None)
            if group is None:
                _, name, color, _ = next(c for c in DESKTOP_CATEGORIES if c[0] == category)
                group = self.create(name, color, save=False)
                group["auto_category"] = category
        if group not in self.groups:
            raise ValueError("Unknown collection group")
        previous_entries = group["entries"][:]
        if not any(os.path.normcase(e["target"]) == os.path.normcase(entry["target"]) for e in group["entries"]):
            group["entries"].append(entry)
        self.loose.remove(entry)
        try:
            self.write_manifest()
        except OSError:
            self.groups[:] = previous_groups
            self.loose[:] = previous_loose
            group["entries"][:] = previous_entries
            raise

    def add_paths(self, group, paths):
        paths = [Path(path).absolute() for path in paths]
        for path in paths:
            if not path.exists():
                raise FileNotFoundError(tr("找不到：{0}").format(path.name))
        added = []
        existing = {os.path.normcase(e["target"]) for e in group["entries"]}
        for path in paths:
            path = Path(path).absolute()
            if not path.exists():
                raise FileNotFoundError(tr("找不到：{0}").format(path.name))
            if os.path.normcase(str(path)) in existing:
                continue
            entry = {"id": identifier(), "name": path.stem if path.suffix.lower() in {".lnk", ".url", ".exe"} else path.name,
                     "target": str(path), "managed": False}
            group["entries"].append(entry)
            existing.add(os.path.normcase(str(path)))
            added.append(entry)
        self.store.save()
        return added

    def add_system(self, group, key):
        name, target = SYSTEM_ENTRIES[key]
        if not any(e["target"] == target for e in group["entries"]):
            group["entries"].append({"id": identifier(), "name": name, "target": target, "managed": False, "system": key})
            self.store.save()

    def move_entries(self, source, target, identifiers, index=None):
        selected = [e for e in source["entries"] if e["id"] in identifiers]
        if source is target and index is not None:
            index -= sum(1 for n, e in enumerate(source["entries"]) if n < index and e["id"] in identifiers)
        source["entries"] = [e for e in source["entries"] if e["id"] not in identifiers]
        if index is None:
            index = len(target["entries"])
        target["entries"][max(0, min(index, len(target["entries"]))):max(0, min(index, len(target["entries"])))] = selected
        self.store.save()

    def collect(self, group, paths, desktop_roots):
        """Atomic same-volume renames only. Journal before touching any source."""
        allowed = {Path(p).resolve() for p in desktop_roots}
        destination = self.store.directory / "organizer_files" / group["id"]
        destination.mkdir(parents=True, exist_ok=True)
        added = []
        errors = []
        for path in paths:
            source = Path(path).absolute()
            if source.parent.resolve() not in allowed:
                errors.append(tr("{0}：只能收纳桌面目录中的直接项目。").format(source.name))
                continue
            if not source.exists() or source.is_symlink():
                errors.append(tr("{0}：项目不存在或是文件系统链接，请改用添加入口。").format(source.name))
                continue
            target = unique_path(destination / source.name)
            entry = {"id": identifier(), "name": source.stem if source.suffix.lower() in {".lnk", ".url", ".exe"} else source.name,
                     "target": str(target), "original_path": str(source), "managed": True}
            journal = {"group_id": group["id"], "entry": entry}
            self._receipt(group, entry, "pending")
            self.store.settings.organizer_pending.append(journal)
            self.store.save()
            try:
                # os.rename never copies a huge folder on the UI thread and
                # fails across volumes. No replacement of existing destinations.
                os.rename(source, target)
                group["entries"].append(entry)
                added.append(entry)
                try:
                    self._receipt(group, entry, "stored")
                except OSError as exc:
                    errors.append(tr("{0}：文件已收纳；冗余凭据更新失败，独立清单仍会记录：{1}").format(source.name, exc))
            except OSError as exc:
                errors.append(tr("{0}：{1}。原项目保留；可改用添加入口。").format(source.name, exc))
            finally:
                self.store.settings.organizer_pending.remove(journal)
                self.store.save()
        return added, errors

    def restore_entry(self, group, entry, keep_reference=False):
        if entry.get("managed"):
            if not self._valid_managed_target(entry):
                raise OSError(tr("收纳路径不在受管目录中，已取消还原。"))
            source = Path(entry["target"])
            original = Path(entry["original_path"])
            if not source.exists():
                raise FileNotFoundError(tr("收纳文件不存在：{0}").format(source.name))
            if not original.parent.is_dir():
                raise FileNotFoundError(tr("原桌面目录已不可用，文件仍保留在收纳目录。"))
            target = unique_path(original)
            # Restore journal makes a crash between rename and config save recoverable.
            reference = {"id": entry["id"], "name": entry["name"], "target": str(target), "managed": False}
            journal = {"group_id": group["id"], "entry": entry, "restore_to": str(target),
                       "reference": reference if keep_reference else None}
            self.store.settings.organizer_pending.append(journal)
            self.store.save()
            try:
                os.rename(source, target)
            except Exception:
                self.store.settings.organizer_pending.remove(journal)
                self.store.save()
                raise
            index = group["entries"].index(entry)
            group["entries"].remove(entry)
            if keep_reference:
                group["entries"].insert(index, reference)
            self.store.settings.organizer_pending.remove(journal)
            receipt = self.receipts / (entry["id"] + ".json")
            receipt.unlink(missing_ok=True)
        else:
            group["entries"].remove(entry)
        self.store.save()

    def remove_group(self, group):
        errors = []
        for entry in list(group["entries"]):
            try:
                self.restore_entry(group, entry)
            except OSError as exc:
                errors.append(str(exc))
        if not group["entries"]:
            self.groups.remove(group)
            self.store.save()
        return errors

    def recover(self):
        if not isinstance(self.store.settings.organizer_pending, list):
            self.store.settings.organizer_pending = []
        pending = self.store.settings.organizer_pending
        for journal in list(pending):
            if not isinstance(journal, dict) or not isinstance(journal.get("entry"), dict):
                pending.remove(journal)
                self.store.warning = tr("已隔离无法识别的收纳日志；文件凭据仍独立保留。")
                continue
            group = self.find(journal.get("group_id"))
            entry = journal.get("entry", {})
            if not group or not entry.get("target"):
                continue
            ids = {e["id"] for e in group["entries"]}
            if journal.get("restore_to"):
                if Path(journal["restore_to"]).exists() and not Path(entry["target"]).exists():
                    group["entries"] = [e for e in group["entries"] if e["id"] != entry["id"]]
                    if isinstance(journal.get("reference"), dict):
                        group["entries"].append(journal["reference"])
            elif Path(entry["target"]).exists() and entry["id"] not in ids:
                group["entries"].append(entry)
            pending.remove(journal)
        if pending:
            self.store.warning = tr("有收纳日志无法自动恢复，项目仍保留在 organizer_files 目录。")
        self.store.save()
