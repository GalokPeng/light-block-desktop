# Light Block Desktop user guide

[简体中文](usage.zh-CN.md) · [README](../README.en.md)

## Install and get started

Download Setup or the portable ZIP from [GitHub Releases](https://github.com/GalokPeng/light-block-desktop/releases/latest). Extract the complete ZIP before running the portable app. Both distributions require Windows 10 or 11 x64. The installer follows the Windows display language. The application uses Simplified Chinese for Chinese display languages and English otherwise.

Exit the previous version before upgrading. The app copies the legacy profile from `%LOCALAPPDATA%\StillWallpaper` into `data` beside the executable. A read-only app folder keeps using the legacy location. Open the current folder from **About**. If you move from portable to installed, toggle **Start with Windows** in the new copy to update its path. The sun or moon changes the app theme and the Windows frame. The language button beside it offers Follow system, Simplified Chinese, and English. Settings and the tray menu switch immediately. Reopen other tools to use the selected language. When a newer release is available, the sidebar button downloads its Setup.

## Wallpaper

Import a local picture, GIF or video to save a full-quality copy in `data/wallpapers`. Select a fit mode and click **Apply to Desktop**. Static wallpaper is managed by Windows. GIF and video play from the copy using Qt's native video output. Video wallpaper sound is off by default. Use **Light Block Desktop Sound** in **Playback Settings** or the desktop context menu to switch it. Video covers are extracted from video frames and cached in `data/thumbnails`. Each session plays one live file on a selected display. Full-screen pause, battery pause, and restoring the wallpaper at startup are enabled by default. Locking Windows or turning off the display also pauses playback.

Enable **Start with Windows** to start quietly after sign-in. With live wallpaper or other background tools enabled, closing Settings leaves the app in the tray. Choose **Quit Light Block Desktop** to stop it.

## Desktop organizer and notes

Enable **Desktop Organizer** to show folder groups and hide native desktop icons. Items are grouped by type. Use **Sync Categories** to add new shortcuts. Click a folder to expand it. Click outside it or open an item to return to the overview.

Click the organizer or group title to edit it. Enter or click away to save, and Esc to cancel. Empty titles are supported. Right-click to create a group, add shortcuts or manage a group. You can change icon size and opacity in Settings. Scrolling works inside the group grid. Disabling the organizer or quitting normally restores the previous Windows icon visibility.

The panel starts centered. Right-click and choose **Move panel**, then drag it to a new position. The panel locks again when you release it. Choose **Center panel** to reset. Drag one folder tile onto another to change the order.

Right-click the desktop background and choose **Light Block Desktop · New Note** to add a pinned note. On Windows 11, select **Show more options** first. The organizer panel's context menu also offers **New Note** and a sound switch. You can also pin an existing note in **Notes & Tasks**. Drag a desktop note to move it and double-click to edit.

## Screen capture

- **Select**. Press `Ctrl+Alt+A` by default, or set a different global shortcut under **Screen Capture**. Hover to detect a window or drag a region. Use Alt for the whole window. Drag a blue handle to resize, or use the move control to reposition. Right-click to clear the selection. Right-click again to exit.
- **Annotate**. Select a tool to show its options below the icon row. Rectangles and ellipses support fill, solid and dashed outlines, and adjustable width. Rectangles also support rounded corners. Hold Shift for a square or circle. Mosaic has a rounded selection mode and a brush mode. Enter text directly on the canvas, then drag to move it or double-click to edit again. Scroll over a supported annotation to resize it.
- **Save or pin**. Save PNG, copy to the clipboard, or pin the result above windows. Use Ctrl + scroll to zoom a pin. Right-click and choose **Edit pinned image** to annotate in place, then click **Finish editing**. Double-click outside edit mode to close the pin and copy its image.
- **Scrolling capture**. Choose **Scrolling Capture** and scroll within the frame. The preview shows the stitched image. If overlap is lost, scroll back or set it manually. The image limit is 40 megapixels.
- **Recording**. Choose **Start Recording**. The frame stays on screen. Use the stop control or `Ctrl+Alt+R` to finish. The preview starts with the quality and frame rate selected in **Screen Capture** settings. Both offer original resolution, 1080p, 720p, or 480p, and original frame rate or 12, 15, 24, 30, or 60 fps. The original frame rate setting captures at 24 fps. Save or copy MP4, GIF, or MKV. Recording is silent.

Recording and media export require FFmpeg on PATH with `gdigrab`, `libx264`, `ffv1` and palette filters. Wallpaper playback uses the bundled runtime. The media editor can trim or crop clips and export to MP4, GIF or MKV.

## Productivity tools

| Tool | Use |
| --- | --- |
| Launcher | Search Start menu apps, organizer items and built-in tools. Type arithmetic to calculate and copy the result |
| Clipboard history | Enable local text and image history, then search, pin, copy or clear it. History is off by default |
| Notes & Tasks | Write and search notes, mark tasks done, archive, set reminders or pin notes to the desktop |
| File Workbench | Preview images or text, inspect properties, calculate SHA-256 and preview batch renames with recovery and undo |
| Focus Workspaces | Save wallpaper and existing group positions. Apply the workspace and run focus or break timers |
