# Third-party notices

Light Block Desktop application code and original demo artwork are distributed under the MIT license in [LICENSE](LICENSE).

The Windows distribution contains the C++ application, replaceable Qt DLLs and
separate media helpers. It contains no Python, PySide or PyInstaller runtime.

| Component | Version and license | Source and notices |
| --- | --- | --- |
| Qt Base, Image Formats and Multimedia | 6.8.3; included modules used under LGPL-3.0 | [Qt licensing](https://doc.qt.io/qt-6.8/licensing.html), [runtime/rebuild details](licenses/Qt-Native-runtime.txt), matching `qt-source.zip` release asset |
| Qt Multimedia FFmpeg shared libraries | 7.1, LGPL-2.1-or-later; built without GPL components | Corresponding source and Qt-compatible build recipe in the matching `qt-source.zip` asset |
| GCC / MinGW runtime DLLs | GCC 13.1.0; GCC Runtime Library Exception and MinGW-w64 licenses | Bundled `licenses/qt/mingw/` notices and SDK source manifest |
| LLVM-MinGW linked runtime | LLVM 20.1.3, Apache-2.0 with LLVM Exceptions; MinGW-w64 13.0.0 | [LLVM](https://github.com/llvm/llvm-project/tree/llvmorg-20.1.3), [MinGW-w64](https://github.com/mingw-w64/mingw-w64/tree/v13.0.0), `licenses/Native-*.txt` |
| C++/WinRT headers | 2.0.240405.15, MIT | [C++/WinRT](https://github.com/microsoft/cppwinrt/tree/2.0.240405.15), [notice](licenses/Native-CppWinRT.txt) |
| GitHub mark from Octicons | 19.23.0, MIT | [source](https://github.com/primer/octicons/blob/v19.23.0/icons/mark-github-16.svg), [license](licenses/Octicons-MIT.txt) |

Qt libraries remain dynamically linked and replaceable in the installation
folder. Recipients may use compatible modified versions and reverse engineer
the application to debug modifications to LGPL components. Build instructions,
exact source archives, toolchain provenance and license texts accompany each
release. The MIT application license does not replace dependency licenses.

The three original geometric cursor schemes and particle artwork use the
project's MIT license. Their C++ generation and rendering code is in `src/`.
Windhawk itself is not included; the native taskbar module uses the documented
build toolchain with its retained notices. SQLite is supplied by Windows
`winsqlite3.dll`; it is not redistributed in the application package.

Screen recording and export launch the bundled, standalone FFmpeg CLI as a separate process. The executable is built from [FFmpeg 8.1.3](https://www.ffmpeg.org/releases/ffmpeg-8.1.3.tar.xz), [x264](https://code.videolan.org/videolan/x264/-/commit/b35605ace3ddf7c1a5d67a2eb553f034aef41d55) and [zlib 1.3.2](https://github.com/madler/zlib/releases/tag/v1.3.2). It is distributed under [GPL-3.0-or-later](licenses/FFmpeg-CLI-GPL-3.0.txt). See the [zlib notice](licenses/FFmpeg-CLI-zlib.txt) and [GCC Runtime Library Exception](licenses/FFmpeg-CLI-GCC-exception.txt) for the linked components. The MinGW-w64 runtime notices are in `licenses/Native-MinGW-w64-runtime.txt`. Qt's shared multimedia libraries and the MIT application are separate works.

The matching [release page](https://github.com/GalokPeng/light-block-desktop/releases) offers `Qingkuai-Preview-v<VERSION>-ffmpeg-source.zip`, which contains the corresponding sources and build scripts. The scripts pin the compiler and assembler packages, including [w64devkit 2.6.0](https://github.com/skeeto/w64devkit/releases/tag/v2.6.0), whose release provides the toolchain source archive. A replacement recording runtime must supply a matching build manifest, license notices and complete corresponding source. Unverified executables are rejected.

The installer is built with [Inno Setup](https://jrsoftware.org/isinfo.php). Its bundled Simplified Chinese and Traditional Chinese message files come from [upstream commit 31ee6e3](https://github.com/jrsoftware/issrc/tree/31ee6e35383c530cca8f047216045d3f61ca8be1/Files/Languages). The translation headers and [Inno Setup License](licenses/Inno-Setup.txt) are retained.

The four bundled static JPEG wallpapers were supplied by the project maintainer as AI-generated project assets and are distributed under the project MIT license. Their original bytes, source basenames and SHA-256 records are in `assets/wallpaper-origins.json`. Existing original motion demos are unchanged. Website previews are separate resized derivatives.

The optional online wallpaper gallery bundles a URL-only catalog, not the remote photographs, GIFs or videos. Some GIFs use NASA scientific imagery; each work’s specific license or public-domain statement is retained in the credits. Wikimedia Commons works retain their individual CC0, public-domain, CC BY or CC BY-SA terms. Jeffrey Beach / Beachfront Productions' original HD videos on Internet Archive use CC BY 3.0. See [individual credits](docs/wallpaper-sources.md) and the [evaluation record](docs/wallpaper-evaluation.md). The app displays attribution, licenses and available popularity evidence, and saves them beside original downloads. The application's MIT license does not relicense these works.
