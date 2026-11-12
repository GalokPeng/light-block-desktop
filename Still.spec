# Build from the project directory with build.ps1.
from pathlib import Path
from runpy import run_path
from importlib.util import find_spec
from shutil import copyfile
import os

root = Path(SPECPATH)
metadata = run_path(str(root / 'tools' / 'release.py'))
version = metadata['read_version'](root)
resource = metadata['windows_version_file'](root, version)
ffmpeg = os.environ.get('STILL_FFMPEG')
if not ffmpeg or not Path(ffmpeg).is_file() or Path(ffmpeg).name.lower() != 'ffmpeg.exe':
    raise SystemExit('Build with build.ps1 so the screen-recording runtime is included.')
qt_translations = Path(find_spec('PySide6').origin).parent / 'translations'
asset_resources = [(str(path), 'assets') for path in (root / 'assets').iterdir() if path.name != 'cursors']
# Keep previous source artwork available, but ship only the active schemes.
asset_resources += [(str(root / 'assets/cursors' / name), 'assets/cursors/' + name)
                    for name in ('stream', 'outline', 'pixel')]
asset_resources.append((str(root / 'assets/cursors/manifest.json'), 'assets/cursors'))
resources = asset_resources + [(str(root / 'licenses'), 'licenses'),
             (str(root / 'locales'), 'locales'),
             (str(qt_translations / 'qtbase_zh_CN.qm'), 'PySide6/translations'),
             (str(root / 'LICENSE'), '.'), (str(root / 'THIRD_PARTY_NOTICES.md'), '.')]
resources.append((ffmpeg, 'runtime'))
updater = os.environ.get('STILL_UPDATE_LAUNCHER')
if not updater or not Path(updater).is_file():
    raise SystemExit('Build with build.ps1 so the update launcher is included.')
bundled_updater = root / 'build' / 'update-launcher' / 'QingkuaiUpdate.exe'
copyfile(updater, bundled_updater)
resources.append((str(bundled_updater), 'runtime'))
pointer = os.environ.get('STILL_POINTER_HOST')
if not pointer or not Path(pointer).is_file():
    raise SystemExit('Build with build.ps1 so the native cursor host is included.')
bundled_pointer = root / 'build' / 'pointer-host' / 'QingkuaiPointer.exe'
copyfile(pointer, bundled_pointer)
resources.append((str(bundled_pointer), 'runtime'))
taskbar = os.environ.get('STILL_TASKBAR_DLL')
native_binaries = []
if taskbar:
    taskbar_dir = root / 'build' / 'native-taskbar'
    # PyInstaller preserves the source basename. Give the packaged DLL a stable name.
    bundled_taskbar = taskbar_dir / 'QingkuaiTaskbarLayout.dll'
    copyfile(taskbar, bundled_taskbar)
    native_binaries.append((str(bundled_taskbar), 'runtime'))
analysis = Analysis(
    [str(root / 'main.py')], pathex=[str(root)], binaries=native_binaries,
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
