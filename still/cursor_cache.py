"""Generate custom cursor colors away from the GUI; publish complete cache entries."""
import json
import os
from pathlib import Path
import re
import shutil
import tempfile

from PySide6.QtCore import QThread, Signal
from .cursor_art import DEFAULT_COLOR, REVISION, ROLES, SIZES, THEMES, write_scheme
from .model import ASSETS


class CursorBuild(QThread):
    ready = Signal(str)
    failed = Signal(str)

    def __init__(self, cache, theme, color, parent=None):
        super().__init__(parent)
        self.cache, self.theme, self.color = Path(cache), theme, color.lower()
        if theme not in THEMES or not re.fullmatch(r'#[0-9a-f]{6}',self.color):
            raise ValueError("invalid cursor scheme")

    def run(self):
        temporary = None
        try:
            if self.color == DEFAULT_COLOR:
                target = ASSETS/'cursors'/self.theme
            else:
                self.cache.mkdir(parents=True,exist_ok=True)
                target = self.cache/f'{REVISION}-{self.theme}-{self.color[1:]}'
                expected = dict(revision=REVISION,theme=self.theme,color=self.color)
                try:
                    valid = json.loads((target/'scheme.json').read_text(encoding='utf-8')) == expected
                    valid = valid and all((target/(role+'.cur')).is_file() for role in ROLES)
                    valid = valid and all((target/f'{role}-{size}.ani').is_file() for role in ('busy','working') for size in SIZES)
                except (OSError,ValueError):
                    valid = False
                if not valid:
                    temporary = Path(tempfile.mkdtemp(prefix='.building-',dir=self.cache))
                    write_scheme(temporary,self.theme,self.color)
                    if self.isInterruptionRequested():
                        return
                    # Only entries under the private cursor-cache directory are replaced.
                    if target.exists():
                        self.remove_entry(target)
                    os.replace(temporary,target)
                    temporary = None
                # Keep at most eight generated combinations; never prune the one returned.
                previous = sorted((p for p in self.cache.iterdir() if p.is_dir() and re.fullmatch(r'\d+-(stream|outline|pixel)-[0-9a-f]{6}',p.name) and p!=target),
                                  key=lambda p:p.stat().st_mtime,reverse=True)
                for path in previous[7:]:
                    try:
                        self.remove_entry(path)
                    except OSError:
                        pass
            if not self.isInterruptionRequested():
                self.ready.emit(str(target))
        except Exception as exc:
            self.failed.emit(str(exc))
        finally:
            if temporary:
                self.remove_entry(temporary)

    def remove_entry(self, path):
        # Resolve before recursive removal, including Windows reparse points.
        root, resolved = self.cache.resolve(), path.resolve()
        if resolved.parent != root or path.is_symlink():
            raise OSError("cursor cache entry escapes its directory")
        shutil.rmtree(resolved)
