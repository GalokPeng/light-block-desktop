<p align="center"><img src="assets/icon.png" width="88" alt="Light Block Desktop icon"></p>
<h1 align="center">Light Block Desktop</h1>
<p align="center">Live wallpaper, desktop folders, screen capture and notes for Windows</p>
<p align="center"><a href="https://github.com/GalokPeng/light-block-desktop/releases/latest"><strong>Download the latest version</strong></a> · <a href="https://light-block-desktop.vercel.app/en/">Website</a> · <a href="README.md">简体中文</a> · <a href="CHANGES.md">Changelog</a></p>

![Wallpaper library and display settings in Light Block Desktop](site/public/media/wallpaper-en.webp)

## Download and install

1. Download `Setup-x64.exe` from [GitHub Releases](https://github.com/GalokPeng/light-block-desktop/releases/latest). For the portable edition, extract the full `Windows-x64.zip` and run `轻块桌面.exe`.
2. Requires Windows 10 or 11 x64. Setup uses the Windows display language and includes English, Simplified Chinese and Traditional Chinese. The app selects Simplified Chinese on Chinese Windows and defaults to English otherwise.
3. Quit the previous version before upgrading. Settings live in `%LOCALAPPDATA%\StillWallpaper`. Installation, upgrades and uninstall preserve that folder.

Setup installs for the current user by default. Wallpaper playback and still screenshots work with the bundled runtime. Recording and export require the [FFmpeg command-line tool](https://ffmpeg.org/download.html) with `gdigrab`, `libx264`, `ffv1` and palette filters on PATH.

## Features

| Feature | How it works |
| --- | --- |
| Wallpaper | Import a still image, GIF or video. Live wallpaper plays from the source file. Choose a display, fit mode and automatic pause conditions |
| Desktop organizer | Group files by type, open folders in a grid, and customize names, icon sizes and opacity. Disabling it restores the original Windows icon visibility |
| Capture and annotation | Press `Ctrl+Alt+A` by default. Select a window or area and add text, arrows, numbers, strokes or mosaic. Right-click to reselect, then save PNG, copy or pin |
| Scrolling capture | Scroll inside the selected region to stitch frames. Follow the side preview or adjust the overlap manually |
| Region recording | The selection frame stays visible. Press `Ctrl+Alt+R` to stop. Save or copy MP4 and GIF, or save lossless MKV |
| Productivity | Launcher and calculator, optional clipboard history, pinned notes and tasks, file preview and batch rename, and focus workspaces |

| Desktop organizer and pinned note | Capture toolbar |
| --- | --- |
| ![Folder groups and a desktop note](site/public/media/organizer-en.webp) | ![A selected area with annotations and the capture toolbar](site/public/media/capture-en.webp) |

Screenshots show the app with fictional demo data. Processing happens locally, with no account, ads or telemetry. Desktop organizing and clipboard history are opt-in. Use the sun or moon button to switch the settings theme. [User guide](docs/usage.en.md)

## Development and releases

```powershell
python -m pip install -r requirements.txt
python main.py
python tools/run_tests.py
```

Windows x64, Python 3.11 and PySide6. The [development Skill](skills/light-block-desktop-dev/SKILL.md) can onboard an agent. See the [contribution guidelines](CONTRIBUTING.md), [release workflow](docs/release.md) and [website deployment](docs/website.md).

The application source uses the [MIT license](LICENSE). Bundled dependencies keep their [own licenses](THIRD_PARTY_NOTICES.md).

## Star history

[![GitHub stars](https://img.shields.io/github/stars/GalokPeng/light-block-desktop?style=flat)](https://github.com/GalokPeng/light-block-desktop/stargazers)

[![Star History Chart](https://api.star-history.com/svg?repos=GalokPeng/light-block-desktop&type=Date)](https://www.star-history.com/#GalokPeng/light-block-desktop&Date)
