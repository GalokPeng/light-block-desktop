# Build from the project directory: python -m PyInstaller --noconfirm Still.spec
from pathlib import Path
from runpy import run_path
import os

root = Path(SPECPATH)
metadata = run_path(str(root / 'tools' / 'release.py'))
version = metadata['read_version'](root)
resource = metadata['windows_version_file'](root, version)
ffmpeg = os.environ.get('STILL_FFMPEG')
if ffmpeg and not Path(ffmpeg).is_file():
    raise SystemExit('STILL_FFMPEG must point to an existing ffmpeg.exe.')
resources = [(str(root / 'assets'), 'assets'), (str(root / 'licenses'), 'licenses'),
             (str(root / 'LICENSE'), '.'), (str(root / 'THIRD_PARTY_NOTICES.md'), '.')]
if ffmpeg:
    resources.append((ffmpeg, 'runtime'))
analysis = Analysis(
    [str(root / 'main.py')], pathex=[str(root)], binaries=[],
    datas=resources,
    hiddenimports=[], hookspath=[], hooksconfig={}, runtime_hooks=[],
    excludes=['tkinter', 'unittest', 'PIL', 'psutil', 'numpy', 'pytest',
              'PyQt6', 'PySide6.QtQml', 'PySide6.QtQuick', 'PySide6.QtQuickWidgets',
              'PySide6.QtWebEngineWidgets', 'PySide6.QtWebEngineCore', 'PySide6.QtPdf',
              'PySide6.QtDesigner', 'PySide6.QtSql', 'PySide6.QtTest'],
    noarchive=False, optimize=1,
)

# Qt's Gui hook finds all wheel plugins. PDF and Virtual Keyboard are
# unnecessary here and can pull GPL-only Qt modules into the distribution.
def supported_runtime(entry):
    name = Path(entry[0]).name.lower()
    return name != 'qpdf.dll' and not any(
        component in name for component in ('qt6pdf', 'qt6qml', 'qt6quick', 'virtualkeyboard')
    )


analysis.binaries = [entry for entry in analysis.binaries if supported_runtime(entry)]
analysis.datas = [entry for entry in analysis.datas if supported_runtime(entry)]
pyz = PYZ(analysis.pure)
exe = EXE(pyz, analysis.scripts, [], exclude_binaries=True, name='轻块桌面',
          debug=False, bootloader_ignore_signals=False, strip=False, upx=False,
          console=False, disable_windowed_traceback=False,
          icon=str(root / 'assets' / 'still.ico'),
          version=str(resource))
collect = COLLECT(exe, analysis.binaries, analysis.datas, strip=False, upx=False,
                  name=f'Qingkuai-v{version}-Windows-x64')
