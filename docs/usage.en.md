# Light Block Desktop guide

[简体中文](usage.zh-CN.md) · [README](../README.en.md)

> **v2.0.0** — Current stable release. v1.0.0 remains available as an archived download.

Light Block Desktop supports Windows 10 / 11 x64 and combines wallpapers, screen capture, recording, desktop organization, native taskbar styling and local tools. Settings, notes and downloaded wallpapers stay on your computer; online wallpapers and updates use network access when requested.

## Installation and everyday use

Download the latest stable Setup from [GitHub Releases](https://github.com/GalokPeng/light-block-desktop/releases), or extract its complete portable ZIP and run `轻块桌面.exe`. Keep its DLLs, plugins, assets and runtime folders together. Users do not need to install Python, Qt or FFmpeg separately.

Setup follows the Windows display language and supports Simplified Chinese, Traditional Chinese and English. At first launch, the app selects Simplified Chinese for Chinese systems and English otherwise. The language button switches Chinese and English; the sun/moon button switches the theme. Version information is shown in About.

Changes save and apply automatically; slider updates are briefly combined. Closing Settings leaves enabled desktop services and the tray running. Use the tray Quit command to exit completely. Start with Windows is available in Settings.

The lower-left update button checks for new releases and displays download progress. After checksum verification, the app exits normally and launches Setup. Cancelling or failing a download keeps the current app running.

## Upgrades and data

v2.0.0 uses the original installation identity. It imports v1.0.0 settings, local wallpapers, notes, groups and supported local history into `data/profile-v2`. A read-only installation uses `%LOCALAPPDATA%/StillWallpaper/profile-v2`. About opens the active data directory.

Original data remains untouched. Migration creates a `legacy-backup` and a completion report, then atomically commits the new profile. Failure does not silently start an empty replacement. Subsequent launches reuse the new profile. Missing media is reported; old operation journals are retained in the backup rather than executed in the new version.

v1.0.0 remains available on the website and its source on `python-v1.0.0`. Exit the current app before rollback and use a separate installation directory. New changes are not synchronized back to v1.0.0. Do not run multiple versions with taskbar, organizer or cursor styling enabled simultaneously.

Developers must use a fresh `--data-dir <folder>` for validation. Tests never access real user data or change the original installation.

## Wallpapers

Importing a local image, GIF or video preserves a full-quality copy under the active profile's `media` folder. Select it to preview within the page; click Play for video. Choose a display and fit mode, then Apply to desktop. Preview and desktop playback are separate channels, so leaving a preview does not stop the desktop wallpaper. Applying a wallpaper stops its preview first to avoid duplicate decoding; Play preview starts it again when needed.

Static wallpapers use the Windows wallpaper interface, with preparation and system calls outside the settings thread. GIF and video playback run in supervised child processes, and switching cleans up the previous player. Hardware decoding depends on the format, driver and installed Windows components; not every format is guaranteed to use it.

Video sound is off by default. Change Wallpaper sound in Settings → Playback and system, Toggle audio, or the installed desktop context-menu entry. Enabling requires confirmation; the confirmed preference is saved for desktop playback without changing the independent preview channel. Full-screen pause, battery pause and restoration at startup are enabled by default. Stop, pause and original-wallpaper restoration are available in the inspector.

Right-click an imported item to delete its library copy while keeping the file you originally imported. A file in use must first be released; failures show a message. Hiding a bundled sample only removes its library entry, not the packaged asset.

### Online wallpapers

The online page has Images, GIF and Videos filters. The bundled selection contains 20 images, 10 GIFs and 20 videos, each originally at least 1920 × 1080 and no larger than 20 MB. Search bilingual titles and catalog tags, and inspect authorship, licensing, source links and publicly available popularity evidence. Each asset retains its own license conditions.

Select a card and Start preview to watch within the page; Stop preview returns to the grid. Covers are loaded on demand with bounded concurrency and caches. Motion preview first downloads and verifies the source, so it can depend on source-network availability. A regular download preserves original bytes and shows progress and downloaded status; verified preview data can be reused. Downloaded content can be imported or applied to the desktop.

**Refresh retrieves the official public URL catalog and reloads covers.** The manifest is limited to 2 MB and validated for schema, source, size, dimensions and digest fields. A failed refresh preserves the old catalog; a running download prevents replacement. Downloaded originals work offline; uncached media still need access to their source. Local network policies may block a source: the app reports the failure and does not bypass the block. See [asset credits](wallpaper-sources.md) and [selection evidence](wallpaper-evaluation.md).

## Appearance

Desktop organizer, native taskbar and pointer options are separate collapsible sections and can be enabled independently.

- **Organizer:** bottom by default, with top/center alternatives; sort by name, modification time or group. Search items, rename/recolor groups, and adjust shelf/icon backgrounds, opacity and icon size. New desktop items remain visible initially and are collected after one minute by default; disable this or choose a group from their context menu. Disabling organization restores the original icon visibility state.
- **Taskbar:** on supported Windows 11 builds, adjacent centered sections contain shortcuts/native search, apps and the system tray. Windows search and tray interactions remain available. Adjust corner radius, color and opacity. The bar hides when foreground windows occupy its area and returns at the screen edge. Explorer versions and other customization tools affect compatibility; unsupported layouts are not forcibly taken over.
- **Pointer:** System, Stream, Outline and Pixel styles, with independent size and color. Dots, Fall and Contour glow have separate colors. Disabling stops the effects, and exiting restores the system cursor scheme.

Settings also links to Windows Night light. This opens the system control rather than pretending to control its color-temperature state; the entry is unavailable on unsupported systems.

Enable tool shortcuts separately in Settings → Tool preferences; they are off by default in this version. `Ctrl+Alt+Space` opens the launcher, `Ctrl+Alt+V` opens clipboard history, and `Ctrl+Alt+R` starts/stops recording. Capture has its own shortcut setting; an active recording retains its stop shortcut. Registration conflicts report an error and preserve the previous settings.

## Capture and recording

`Ctrl+Alt+A` opens capture by default; customize it in Capture settings. Drag selection edges to resize. Right-click resets an existing selection; before a selection exists it exits capture.

The compact toolbar exposes the current tool's parameters: rectangle/ellipse, fill, solid/dashed outlines, width, radius, arrows, lines, pen, highlighter, text, numbering and mosaic. Option fields are selection-only. Text is entered on the canvas, can be dragged and re-edited by double-clicking, with font, size and color in the subtoolbar. Wheel adjustments affect annotations, not the capture rectangle.

Copy, save or pin the finished image at its original position. Use `Ctrl+wheel` to zoom a pin, right-click to edit with the same controls, and double-click to close it. Scroll capture samples the selected area and matches overlap, with an accumulated thumbnail beside it. After switching pages, choose **Capture next**: matching content is joined at its overlap, or the full page is appended if there is no overlap. Undo removes the last addition. **Finish** creates a pin even if nothing scrolled; unmatched automatic samples are not forcibly appended, so use Capture next to explicitly include a new page. Long pins use the wheel to scroll, `Ctrl+wheel` to zoom, `Shift+wheel` for opacity, and navigation keys to browse. Copy and save preserve the complete image.

Record from a selected region. Its boundary stays visible while recording; stop through the toolbar or the recording-session `Ctrl+Alt+R` shortcut. The result first appears as a pinned preview for trimming, cropping, annotation and export. Drag timeline endpoints, or use `Tab`/`Shift+Tab` to select either endpoint and the playhead. Arrow keys move one frame and `Shift+arrow` moves ten frames.

Export MP4, GIF or lossless MKV at original/1080p/720p/480p resolution and the original or a fixed frame rate. Fast GIF mode reduces palette-analysis sampling, not the chosen output dimensions. Before recording, enable system audio, microphone, or both in the preparation bar. Both are off by default, and devices open only after Start. After Stop, audio is finalized in the background before preview. Missing permission, disconnection, or a default-device change produces an explicit error rather than silently claiming a successful audio recording. GIF has no audio. Media studio can preserve existing audio for supported export formats.

**Recording and export use the packaged FFmpeg runtime with fixed-hash verification; the app does not search PATH for an alternative.** If files are missing or fail verification, reinstall or extract the matching complete release package. Developers should rebuild/stage the runtime. Do not follow old instructions to install arbitrary FFmpeg binaries.

The preparation bar remembers audio choices for the next recording; no input is captured before Start. Listen/Mute in the recording preview affects playback only; exported audio remains intact. System audio and microphone capture use Windows WASAPI without a virtual audio driver. Combining audio copies the video stream without another video encode. Original resolution, higher frame rates and lossless recording still increase CPU, disk and memory use; hardware encoding is not guaranteed.

An unresponsive audio driver times out the task while preserving recorded files. If the device remains occupied, exit the app normally to release it. Abandoned audio cache is cleaned only on a later launch after the original process exits. Synthetic tests cover mixed audio and stalled-worker lifetimes; actual devices, privacy permissions and startup synchronization still require testing on the target PC.

## Tools

- **Notes:** create from Tools or the installed desktop context menu (Windows 11 may require Show more options). Notes live on the desktop layer. Drag their title to move, or their edges/corners to resize. Long content scrolls without a visible scrollbar. Colors, full-text search, completion, archiving and reminders are available.
- **Clipboard history:** automatic capture is off by default. Enable it for local history, or explicitly use Record current content. Search, pin, copy and delete entries, with inline text/image previews. Automated fixtures use synthetic history and do not read the real clipboard.
- **Quick launch:** searches desktop and Start menu entries and supports limited arithmetic; an item opens only when selected for execution.
- **File workbench:** inline browsing, previews, digests and batch-rename plans. Inspect the plan before applying changes. Undo/recovery records generated by this version use the new format and do not execute legacy journals.
- **Focus workspaces:** focus/rest timers, history and workspace switching. Supported earlier history is imported; active timers remain stopped during upgrade.

## Troubleshooting and development

Open the active profile from About. The current log is `native.log`; migrated records remain separate from v1.0.0 originals. Review file paths before sharing logs. A missing old wallpaper or a blocked network source is separate from losing configuration data.

There is no universal memory-use figure: resolution, frame rate, decoding backend and display count all matter. Old Python measurements do not describe the current runtime; measure a specific workload on the target machine.

The app runtime is C++23, Qt Widgets and Win32. Python is used only by development/build tools. Use an isolated development profile:

```powershell
./build.ps1
& ".\dist\LightBlockDesktop\轻块桌面.exe" --data-dir "$PWD\.local\dev-profile"
./tools/test.ps1 -SkipBuild
```

The complete previous implementation remains on [`python-v1.0.0`](https://github.com/GalokPeng/light-block-desktop/tree/python-v1.0.0) and [`v1.0.0`](https://github.com/GalokPeng/light-block-desktop/tree/v1.0.0). See [contributing](../CONTRIBUTING.en.md) and [releases](release.en.md). Application source is MIT; packaged dependencies and wallpapers retain their [own licenses](../THIRD_PARTY_NOTICES.md).
