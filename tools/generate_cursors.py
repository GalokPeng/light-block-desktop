"""Build original schemes using the same artwork as runtime custom colors."""
import json
import os
from pathlib import Path
import sys

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from PySide6.QtGui import QGuiApplication
from still.cursor_art import THEMES, ROLES, SIZES, REVISION, write_scheme


def main():
    app = QGuiApplication([])
    root = ROOT / 'assets/cursors'
    for theme in THEMES:
        write_scheme(root / theme, theme)
    (root / 'manifest.json').write_text(json.dumps(dict(license='MIT', revision=REVISION,
        source='Original geometric artwork in still/cursor_art.py', themes=list(THEMES), roles=list(ROLES),
        sizes=SIZES, animated=['busy', 'working']), indent=2)+'\n', encoding='utf-8')
    print('Generated 3 structurally different themes, 17 roles, 5 sizes and animations.')


if __name__ == '__main__': main()
