from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import sys
import uuid
from dataclasses import asdict, dataclass, field, fields
from pathlib import Path

from .i18n import tr
from .profile import choose_directory, default_directory, legacy_directory
from .recording_options import FRAME_RATES, RESOLUTIONS

VERSION = "0.9.0"
APP_NAME = "轻块桌面"
DEFAULT_ORGANIZER_TITLE = "桌面收纳"
DEFAULT_CAPTURE_SHORTCUT = "Ctrl+Alt+A"
TASKBAR_RADIUS_LIMITS = (6, 26)
DEFAULT_TASKBAR_RADIUS = 22
ROOT = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent.parent))
ASSETS = ROOT / "assets"
LEGACY_BUILTIN_IDS = {"builtin:dunes": "builtin:nightdrive", "builtin:mountain": "builtin:mountainbay",
                      "builtin:glow": "builtin:milkyway"}
IMAGE_EXT = {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"}
VIDEO_EXT = {".mp4", ".mkv", ".mov", ".webm", ".avi", ".m4v", ".ogv", ".wmv"}
FITS = {"fill": "填充屏幕", "fit": "完整显示", "stretch": "拉伸铺满", "center": "原始尺寸"}
APPS_FOLDER = "shell:AppsFolder\\"


def dock_target_key(target: str) -> str:
    return os.path.normcase(os.path.normpath(target))


def normalize_dock_pins(pins: list) -> list[str]:
    unique = {}
    for target in pins:
        if not isinstance(target, str) or "\0" in target:
            continue
        if target.startswith(APPS_FOLDER):
            ident = target[len(APPS_FOLDER):]
            valid = "!" in ident and not any(char in ident for char in "\\/")
        else:
            path = Path(target)
            valid = path.is_absolute() and path.suffix.lower() in {".exe", ".lnk"}
        if valid:
            unique.setdefault(dock_target_key(target), target)
    return list(unique.values())


def media_kind(path: Path) -> str:
    ext = path.suffix.lower()
    if ext == ".gif":
        return "gif"
    if ext in IMAGE_EXT:
        return "image"
    if ext in VIDEO_EXT:
        return "video"
    raise ValueError(tr("不支持 {0} 文件，请导入图片、GIF 或视频。").format(ext or tr("无扩展名")))


def bundled_items() -> list[dict]:
    return [
        {"id": "builtin:nightdrive", "path": "night-drive.jpg", "name": "夜驰 · 城郊高速跑车", "kind": "image", "builtin": True},
        {"id": "builtin:milkyway", "path": "milky-way.jpg", "name": "银河 · 高山湖泊星空", "kind": "image", "builtin": True},
        {"id": "builtin:mountainbay", "path": "mountain-bay.jpg", "name": "山海 · 晨光与海湾", "kind": "image", "builtin": True},
        {"id": "builtin:raincoast", "path": "rain-coast.jpg", "name": "海岸 · 雨夜弯道", "kind": "image", "builtin": True},
        {"id": "builtin:motion", "path": "motion.mp4", "name": "光的呼吸", "kind": "video", "builtin": True},
        {"id": "builtin:ripple", "path": "startrail.gif", "name": "星轨弧光", "kind": "gif", "builtin": True},
    ]


def item_name(item: dict) -> str:
    if isinstance(item.get("online_titles"), dict):
        from .i18n import display_language
        name = item["online_titles"].get(display_language())
        if isinstance(name, str) and name:
            return name
    return tr(item["name"]) if item.get("builtin") else item["name"]


def item_path(item: dict) -> Path:
    return ASSETS / item["path"] if item.get("builtin") else Path(item["path"])


def wallpaper_folder(ident):
    return ident if re.fullmatch(r"[0-9a-f]{32}", ident) else hashlib.sha256(ident.encode()).hexdigest()[:32]


def managed_wallpaper(path, ident):
    parts = Path(path).parts
    return (len(parts) == 3 and parts[:2] == ("wallpapers", wallpaper_folder(ident))
            and parts[-1] != ".." and ":" not in parts[-1])


@dataclass
class Settings:
    version: str = VERSION
    library: list = field(default_factory=list)
    active_id: str = ""
    fit: str = "fill"
    screen: str = ""  # Qt screen name, primary screen if empty or disconnected
    fullscreen_pause: bool = True
    battery_pause: bool = True
    restore_on_start: bool = True
    wallpaper_sound: bool = False
    original_wallpaper: dict | None = None
    settings_theme: str = "dark"
    settings_language: str = "system"
    taskbar_style_enabled: bool = False
    taskbar_radius: int = DEFAULT_TASKBAR_RADIUS
    dock_glass: bool = True
    dock_opacity: int = 70
    dock_color: str = "#263038"
    dock_pins: list[str] | None = None
    organizer_enabled: bool = False
    organizer_alignment: str = "center"
    organizer_views: dict[str, str] = field(default_factory=dict)
    organizer_icon_size: int | None = None
    organizer_position: list[int] | None = None
    organizer_title: str = DEFAULT_ORGANIZER_TITLE
    organizer_groups: list = field(default_factory=list)
    organizer_pending: list = field(default_factory=list)
    organizer_loose: list = field(default_factory=list)
    organizer_opacity: int = 165
    organizer_glass: bool = True
    organizer_mesh: bool = True
    organizer_auto_classified: bool = False
    organizer_single_click: bool = False
    clipboard_enabled: bool = False
    focus_profiles: list = field(default_factory=list)
    hotkeys_enabled: bool = False
    capture_hotkey: bool = True
    capture_shortcut: str = DEFAULT_CAPTURE_SHORTCUT
    capture_directory: str = ""
    notes_background: bool = False
    focus_active: bool = False
    record_lossless: bool = False
    record_fps: int | None = None
    record_resolution: int = 0


class Store:
    def __init__(self, directory: Path | None = None):
        self.directory = (directory or choose_directory(default_directory(), legacy_directory())).resolve()
        self.directory.mkdir(parents=True, exist_ok=True)
        self.file = self.directory / "settings.json"
        self.warning = ""
        self.sidecar_savers = []
        self.settings = Settings()
        if self.file.exists():
            try:
                data = json.loads(self.file.read_text(encoding="utf-8"))
                if not isinstance(data, dict):
                    raise ValueError(tr("设置不是 JSON 对象"))
                allowed = {f.name for f in fields(Settings)}
                self.settings = Settings(**{k: v for k, v in data.items() if k in allowed})
                if isinstance(self.settings.active_id, str):
                    self.settings.active_id = LEGACY_BUILTIN_IDS.get(self.settings.active_id, self.settings.active_id)
                if not isinstance(self.settings.library, list):
                    raise ValueError(tr("壁纸库格式不正确"))
                self.settings.library = [i for i in self.settings.library if isinstance(i, dict)
                                         and isinstance(i.get("path"), str) and isinstance(i.get("id"), str)
                                         and isinstance(i.get("name"), str) and i.get("kind") in {"image", "gif", "video"}
                                         and not i.get("builtin") and (Path(i["path"]).is_absolute()
                                                                      or managed_wallpaper(i["path"], i["id"]))]
                if self.settings.fit not in FITS:
                    self.settings.fit = "fill"
                if self.settings.settings_theme not in ("dark", "light"):
                    self.settings.settings_theme = "dark"
                if self.settings.settings_language not in ("system", "zh", "en"):
                    self.settings.settings_language = "system"
                self.settings.taskbar_style_enabled = self.settings.taskbar_style_enabled is True
                radius = self.settings.taskbar_radius
                if type(radius) is not int or not TASKBAR_RADIUS_LIMITS[0] <= radius <= TASKBAR_RADIUS_LIMITS[1]:
                    self.settings.taskbar_radius = DEFAULT_TASKBAR_RADIUS
                self.settings.dock_glass = self.settings.dock_glass is True
                color = self.settings.dock_color
                if not isinstance(color, str) or not re.fullmatch(r"#[0-9a-fA-F]{6}", color):
                    self.settings.dock_color = "#263038"
                opacity = self.settings.dock_opacity
                self.settings.dock_opacity = max(20, min(100, opacity)) if type(opacity) is int else 70
                pins = self.settings.dock_pins
                self.settings.dock_pins = normalize_dock_pins(pins) if isinstance(pins, list) else None
                if self.settings.organizer_alignment not in ("top", "center", "bottom"):
                    self.settings.organizer_alignment = "center"
                views = self.settings.organizer_views
                self.settings.organizer_views = {key: value for key, value in views.items()
                    if key in ("top", "center", "bottom") and value in ("groups", "name", "time")} if isinstance(views, dict) else {}
                if self.settings.organizer_icon_size not in (32, 44, 60):
                    self.settings.organizer_icon_size = None
                if not isinstance(self.settings.organizer_title, str):
                    self.settings.organizer_title = DEFAULT_ORGANIZER_TITLE
                position = self.settings.organizer_position
                if not (isinstance(position, list) and len(position) == 2
                        and all(type(axis) is int and abs(axis) < 100_000 for axis in position)):
                    self.settings.organizer_position = None
                if not isinstance(self.settings.capture_shortcut, str) or not self.settings.capture_shortcut:
                    self.settings.capture_shortcut = DEFAULT_CAPTURE_SHORTCUT
                self.settings.wallpaper_sound = self.settings.wallpaper_sound is True
                self.settings.record_lossless = self.settings.record_lossless is True
                if type(self.settings.record_fps) is not int or self.settings.record_fps not in FRAME_RATES:
                    self.settings.record_fps = None
                if type(self.settings.record_resolution) is not int or self.settings.record_resolution not in RESOLUTIONS:
                    self.settings.record_resolution = 0
            except (ValueError, TypeError, OSError) as exc:
                backup = self.directory / f"settings.damaged-{uuid.uuid4().hex[:8]}.json"
                self.file.replace(backup)
                self.warning = tr("设置文件已备份并重建：{0}").format(exc)
                self.settings = Settings()

    @property
    def items(self) -> list[dict]:
        return bundled_items() + self.settings.library

    def find(self, ident: str) -> dict | None:
        if isinstance(ident, str):
            ident = LEGACY_BUILTIN_IDS.get(ident, ident)
        return next((i for i in self.items if i["id"] == ident), None)

    def path(self, item):
        return self.directory / item_path(item)

    def save(self):
        for saver in self.sidecar_savers:
            saver()
        temp = self.file.with_suffix(".tmp")
        temp.write_text(json.dumps(asdict(self.settings), ensure_ascii=False, indent=2), encoding="utf-8")
        os.replace(temp, self.file)

    def add(self, path: Path) -> dict:
        path = path.resolve(strict=True)
        if not path.is_file():
            raise ValueError(tr("请选择一个文件。"))
        kind = media_kind(path)
        existing = next((i for i in self.items if self.path(i).resolve() == path or i.get("source") == str(path)), None)
        if existing:
            if not self.path(existing).is_file() and managed_wallpaper(existing["path"], existing["id"]):
                self.copy_wallpaper(existing, path)
            return existing
        item = {"id": uuid.uuid4().hex, "name": path.stem, "kind": kind, "builtin": False}
        self.copy_wallpaper(item, path)
        self.settings.library.append(item)
        try:
            self.save()
        except OSError:
            self.settings.library.pop()
            self.delete_copy(item)
            raise
        return item

    def copy_wallpaper(self, item, source):
        relative = Path("wallpapers") / wallpaper_folder(item["id"]) / source.name
        destination = self.directory / relative
        if not destination.resolve().is_relative_to(self.directory / "wallpapers"):
            raise OSError(tr("无法写入壁纸库。"))
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.parent / (".import-" + uuid.uuid4().hex + ".tmp")
        try:
            shutil.copyfile(source, temporary)
            os.replace(temporary, destination)
        finally:
            temporary.unlink(missing_ok=True)
        item.update(path=str(relative), source=str(source))

    def migrate_wallpapers(self):
        changed = []
        for item in self.settings.library:
            source = Path(item["path"])
            if not source.is_absolute() or not source.is_file():
                continue
            original = item.copy()
            try:
                self.copy_wallpaper(item, source)
                changed.append((item, original))
            except OSError:
                self.warning = tr("部分旧壁纸未能复制，仍从原位置读取。")
        if changed:
            try:
                self.save()
            except OSError:
                for item, original in changed:
                    self.delete_copy(item)
                    item.clear()
                    item.update(original)
                self.warning = tr("部分旧壁纸未能复制，仍从原位置读取。")

    def delete_copy(self, item):
        if not managed_wallpaper(item["path"], item["id"]):
            return
        try:
            path = self.path(item)
            if path.resolve().is_relative_to(self.directory / "wallpapers"):
                path.unlink(missing_ok=True)
                if item.get("online_id"):
                    (path.parent / "origin.json").unlink(missing_ok=True)
                path.parent.rmdir()
        except OSError:
            pass

    def remove(self, ident: str):
        library = self.settings.library
        self.settings.library = [i for i in library if i["id"] != ident]
        try:
            self.save()
        except OSError:
            self.settings.library = library
            raise
        for item in library:
            if item["id"] == ident:
                self.delete_copy(item)
