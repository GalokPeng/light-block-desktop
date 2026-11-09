# Light Block Desktop user guide

[简体中文](usage.zh-CN.md) · [README](../README.en.md)

## Install and get started

Download Setup or the portable ZIP from [GitHub Releases](https://github.com/GalokPeng/light-block-desktop/releases/latest). Extract the complete ZIP before running the portable app. Both distributions require Windows 10 or 11 x64. The installer follows the Windows display language. The application uses Simplified Chinese for Chinese display languages and English otherwise.

Exit the previous version before upgrading. The app keeps settings in `%LOCALAPPDATA%\StillWallpaper`. If you move from portable to installed, toggle **Start with Windows** in the new copy to update its path. Click the sun or moon to change the settings theme.

## Wallpaper

Import a local picture, GIF or video. Select a fit mode and click **Apply to Desktop**. Static wallpaper is managed by Windows. GIF and video are played silently from their source file using Qt's native video output, without re-encoding. Each session plays one live file on a selected display. Locking Windows or turning off the display pauses it. Full-screen and battery pause are configurable.

Enable **Start with Windows** to start quietly after sign-in. With live wallpaper or other background tools enabled, closing Settings leaves the app in the tray. Choose **Quit Light Block Desktop** to stop it.

## Desktop organizer and notes

Enable **Desktop Organizer** to show folder groups and hide native desktop icons. Items are grouped by type. Use **Sync Categories** to add new shortcuts. Click a folder to expand it. Click outside it or open an item to return to the overview.

Click the organizer or group title to edit it. Enter or click away to save, and Esc to cancel. Empty titles are supported. Right-click to create a group, add shortcuts or manage a group. You can change icon size and opacity in Settings. Scrolling works inside the group grid. Disabling the organizer or quitting normally restores the previous Windows icon visibility.

In **Notes & Tasks**, choose **Pin to Desktop** to show a note on the desktop. Drag to move it and double-click to edit. Notes, reminders and completion state are stored locally.

## Screen capture

- **Select**. Press `Ctrl+Alt+A` by default, or set a different global shortcut under **Screen Capture**. Hover to detect a window or drag a region. Use Alt for the whole window. Drag a blue handle to resize, or use the move control to reposition. Right-click to clear the selection. Right-click again to exit.
- **Annotate**. The toolbar offers text, arrows, lines, rectangles, pen, highlighter, mosaic and numbered markers. Enter text on the canvas. Click away to commit, drag to move, and double-click to edit again. Hover over a supported annotation and scroll to resize it.
- **Save or pin**. Save PNG, copy to the clipboard, or pin the result above windows. Right-click a pin for its border options. Double-click to close the pin and copy its image.
- **Scrolling capture**. Choose **Scrolling Capture** and scroll within the frame. The preview shows the stitched image. If overlap is lost, scroll back or set it manually. The image limit is 40 megapixels.
- **Recording**. Choose **Start Recording**. The frame stays on screen. Use the stop control or `Ctrl+Alt+R` to finish. Save or copy MP4 and GIF. With lossless recording enabled, save the original MKV. Recording is silent.

Recording and media export require FFmpeg on PATH with `gdigrab`, `libx264`, `ffv1` and palette filters. Wallpaper playback uses the bundled runtime. The media editor can trim or crop clips and export to MP4, GIF or MKV.

## Productivity tools

| Tool | Use |
| --- | --- |
| Launcher | Search Start menu apps, organizer items and built-in tools. Type arithmetic to calculate and copy the result |
| Clipboard history | Enable local text and image history, then search, pin, copy or clear it. History is off by default |
| Notes & Tasks | Write and search notes, mark tasks done, archive, set reminders or pin notes to the desktop |
| File Workbench | Preview images or text, inspect properties, calculate SHA-256 and preview batch renames with recovery and undo |
| Focus Workspaces | Save wallpaper and existing group positions. Apply the workspace and run focus or break timers |
