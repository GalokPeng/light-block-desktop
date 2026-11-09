from __future__ import annotations

import json
import os
import sys
import uuid
from dataclasses import asdict, dataclass, field, fields
from pathlib import Path

from .i18n import tr

VERSION = "0.5.1"
APP_NAME = "轻块桌面"
DEFAULT_ORGANIZER_TITLE = "桌面收纳"
DEFAULT_CAPTURE_SHORTCUT = "Ctrl+Alt+A"
ROOT = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent.parent))
ASSETS = ROOT / "assets"
IMAGE_EXT = {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"}
VIDEO_EXT = {".mp4", ".mkv", ".mov", ".webm", ".avi", ".m4v"}
FITS = {"fill": "填充屏幕", "fit": "完整显示", "stretch": "拉伸铺满", "center": "原始尺寸"}


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
        {"id": "builtin:dunes", "path": "dunes.png", "name": "暮色沙丘", "kind": "image", "builtin": True},
        {"id": "builtin:mountain", "path": "mountain.png", "name": "雾隐青山", "kind": "image", "builtin": True},
        {"id": "builtin:glow", "path": "glow.png", "name": "深蓝引力", "kind": "image", "builtin": True},
        {"id": "builtin:motion", "path": "motion.mp4", "name": "光的呼吸", "kind": "video", "builtin": True},
        {"id": "builtin:ripple", "path": "ripple.gif", "name": "微光涟漪", "kind": "gif", "builtin": True},
    ]


def item_name(item: dict) -> str:
    return tr(item["name"]) if item.get("builtin") else item["name"]


def item_path(item: dict) -> Path:
    return ASSETS / item["path"] if item.get("builtin") else Path(item["path"])


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
    original_wallpaper: dict | None = None
    settings_theme: str = "dark"
    settings_language: str = "system"
    organizer_enabled: bool = False
    organizer_position: list[int] | None = None
    organizer_title: str = DEFAULT_ORGANIZER_TITLE
    organizer_groups: list = field(default_factory=list)
    organizer_pending: list = field(default_factory=list)
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
    record_fps: int = 24


class Store:
    def __init__(self, directory: Path | None = None):
        self.directory = directory or Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "StillWallpaper"
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
                if not isinstance(self.settings.library, list):
                    raise ValueError(tr("壁纸库格式不正确"))
                self.settings.library = [i for i in self.settings.library if isinstance(i, dict)
                                         and isinstance(i.get("path"), str) and isinstance(i.get("id"), str)
                                         and i.get("kind") in {"image", "gif", "video"}]
                if self.settings.fit not in FITS:
                    self.settings.fit = "fill"
                if self.settings.settings_theme not in ("dark", "light"):
                    self.settings.settings_theme = "dark"
                if self.settings.settings_language not in ("system", "zh", "en"):
                    self.settings.settings_language = "system"
                if not isinstance(self.settings.organizer_title, str):
                    self.settings.organizer_title = DEFAULT_ORGANIZER_TITLE
                position = self.settings.organizer_position
                if not (isinstance(position, list) and len(position) == 2
                        and all(type(axis) is int and abs(axis) < 100_000 for axis in position)):
                    self.settings.organizer_position = None
                if not isinstance(self.settings.capture_shortcut, str) or not self.settings.capture_shortcut:
                    self.settings.capture_shortcut = DEFAULT_CAPTURE_SHORTCUT
            except (ValueError, TypeError, OSError) as exc:
                backup = self.directory / f"settings.damaged-{uuid.uuid4().hex[:8]}.json"
                self.file.replace(backup)
                self.warning = tr("设置文件已备份并重建：{0}").format(exc)
                self.settings = Settings()

    @property
    def items(self) -> list[dict]:
        return bundled_items() + self.settings.library

    def find(self, ident: str) -> dict | None:
        return next((i for i in self.items if i["id"] == ident), None)

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
        existing = next((i for i in self.items if item_path(i).resolve() == path), None)
        if existing:
            return existing
        item = {"id": uuid.uuid4().hex, "path": str(path), "name": path.stem, "kind": kind, "builtin": False}
        self.settings.library.append(item)
        self.save()
        return item

    def remove(self, ident: str):
        self.settings.library = [i for i in self.settings.library if i["id"] != ident]
        self.save()
