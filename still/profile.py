"""Locate the writable profile and copy the former per-user profile once."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import stat
import sys
from tempfile import NamedTemporaryFile, TemporaryDirectory


def legacy_directory():
    return Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "StillWallpaper"


def default_directory():
    base = Path(sys.executable).resolve().parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parents[1]
    return base / "data"


def writable(directory):
    try:
        with NamedTemporaryFile(dir=directory):
            return True
    except OSError:
        return False


def migrate_profile(source, destination):
    if destination.exists() or not source.is_dir():
        return
    source = source.resolve()

    def ignore(folder, names):
        excluded = {"native-runtime", "thumbnails", "settings.tmp", "collections.tmp"} if Path(folder) == source else set()
        return [name for name in names if name in excluded or
                getattr((Path(folder) / name).lstat(), "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT]

    with TemporaryDirectory(dir=destination.parent, prefix=".qingkuai-profile-") as temporary:
        staging = Path(temporary) / "data"
        shutil.copytree(source, staging, ignore=ignore)

        def rebase(value, key=None):
            if isinstance(value, dict):
                return {field: rebase(item, field) for field, item in value.items()}
            if isinstance(value, list):
                return [rebase(item, key) for item in value]
            if key in {"path", "target", "capture_directory"} and isinstance(value, str):
                path = Path(value)
                if path.is_absolute():
                    path = path.resolve()
                if path.is_absolute() and path.is_relative_to(source):
                    relative = path.relative_to(source)
                    if (staging / relative).exists():
                        return str(destination / relative)
            return value

        documents = [staging / name for name in ("settings.json", "collections.json", "collections.backup.json")]
        documents.extend((staging / "organizer_files" / ".receipts").glob("*.json"))
        for file in documents:
            if file.is_file():
                try:
                    data = json.loads(file.read_text(encoding="utf-8"))
                except ValueError:
                    continue  # Store and Collections keep their existing recovery paths.
                file.write_text(json.dumps(rebase(data), ensure_ascii=False, indent=2), encoding="utf-8")
        try:
            staging.rename(destination)
        except FileExistsError:
            if not destination.is_dir():
                raise


def choose_directory(destination, legacy):
    if destination.resolve().is_relative_to(legacy.resolve()):
        return legacy
    if not writable(destination if destination.exists() else destination.parent):
        return legacy
    try:
        migrate_profile(legacy, destination)
        return destination
    except OSError:
        return legacy
