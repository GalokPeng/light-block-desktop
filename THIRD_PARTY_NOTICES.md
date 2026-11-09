# Third-party notices

Light Block Desktop application code and original demo artwork are distributed under the MIT license in [LICENSE](LICENSE).

The Windows distribution includes separate, unmodified Python and Qt libraries. The licenses for those libraries remain in effect.

| Component | Version and license | Corresponding source |
| --- | --- | --- |
| CPython | 3.11.9, PSF License | [Python 3.11.9](https://www.python.org/downloads/release/python-3119/) |
| PySide6 and Shiboken6 | 6.11.2, used under LGPL-3.0-only | [Qt for Python 6.11.2 source](https://download.qt.io/official_releases/QtForPython/pyside6/PySide6-6.11.2-src/) |
| Qt runtime | 6.11.2, LGPL-3.0 for the included Qt modules | [Qt 6.11.2 source](https://download.qt.io/official_releases/qt/6.11/6.11.2/single/) |
| Qt Multimedia's shared FFmpeg libraries | LGPL, see Qt Multimedia attributions and the wheel's license notices | [Qt Multimedia 6.11.2 source](https://download.qt.io/official_releases/qt/6.11/6.11.2/submodules/) and [FFmpeg releases](https://ffmpeg.org/releases/) |
| PyInstaller bootloader | 6.22.3, GPL-2.0-or-later with the bootloader exception | [PyInstaller v6.22.3 source](https://github.com/pyinstaller/pyinstaller/tree/v6.22.3) |

The replaceable Qt and Shiboken libraries are under `_internal` and its subdirectories. Installation into the default per-user directory gives the user access to those files. Recipients may replace them with compatible modified versions and reverse engineer the application as needed to debug those modifications under the LGPL. The application source, dependency versions and build instructions accompany the release. License texts are in `licenses/` and in the runtime license files.

See [Qt attributions](https://doc.qt.io/qt-6.11/licenses-used-in-qt.html), [Qt Multimedia attributions](https://doc.qt.io/qt-6.11/qtmultimedia-index.html#licenses-and-attributions) and [Qt for Python licensing](https://doc.qt.io/qtforpython-6/licenses.html).

Screen recording and export use an external FFmpeg command-line executable installed by the user. The standard release includes Qt's shared multimedia libraries separately from that executable. The optional local build argument `-FFmpegPath` accepts an unmodified CLI executable. Distributors who use that argument must provide the licensing and corresponding source for their chosen build. The previous portable package's Gyan 8.1.1 build configuration is retained under `licenses/` for reference.

The installer is built with [Inno Setup](https://jrsoftware.org/isinfo.php). Its bundled Simplified Chinese and Traditional Chinese message files come from [upstream commit 31ee6e3](https://github.com/jrsoftware/issrc/tree/31ee6e35383c530cca8f047216045d3f61ca8be1/Files/Languages). The translation headers and [Inno Setup License](licenses/Inno-Setup.txt) are retained.
