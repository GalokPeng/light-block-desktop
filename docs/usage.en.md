# Light Block Desktop user guide

[简体中文](usage.zh-CN.md) · [README](../README.en.md)

## Install and get started

Download Setup or the portable ZIP from [GitHub Releases](https://github.com/GalokPeng/light-block-desktop/releases/latest). Extract the complete ZIP before running the portable app. Both distributions require Windows 10 or 11 x64. The installer follows the Windows display language. The application uses Simplified Chinese for Chinese display languages and English otherwise.

Exit the previous version before upgrading. The app copies the legacy profile from `%LOCALAPPDATA%\StillWallpaper` into `data` beside the executable. A read-only app folder keeps using the legacy location. Open the current folder from **About**. If you move from portable to installed, toggle **Start with Windows** in the new copy to update its path. The sun or moon changes the app theme and the Windows frame. The language button beside it offers Follow system, Simplified Chinese, and English. Settings and the tray menu switch immediately. Reopen other tools to use the selected language.

When a release is available, click **Update now**. The button shows download progress; click again to cancel. After verifying the complete installer against the official SHA-256, the app exits normally and starts Setup. Installed copies update in the existing directory and restart with the same configuration. Portable and source copies open the installation wizard. Downloads start with GitHub for English and `gh-proxy.com` for Chinese, with `ghfast.top` and official-source fallback. Failed, stalled or persistently slow initial downloads switch sources. Closing Settings does not interrupt an active download.

## Wallpaper

Import a local picture, GIF or video to save a full-quality copy in `data/wallpapers`. Select a fit mode and click **Apply to Desktop**. Static wallpaper is managed by Windows. GIF and video play from the copy using Qt's native video output. Video wallpaper sound is off by default. Use **Light Block Desktop Sound** in **Playback Settings** or the desktop context menu to switch it. Video covers are extracted from video frames and cached in `data/thumbnails`. Each session plays one live file on a selected display. Full-screen pause, battery pause, and restoring the wallpaper at startup are enabled by default. Locking Windows or turning off the display also pauses playback.

Enable **Start with Windows** to start quietly after sign-in. With live wallpaper or other background tools enabled, closing Settings leaves the app in the tray. Choose **Quit Light Block Desktop** to stop it.

Right-click an imported wallpaper and choose **Delete wallpaper**, or use the matching button in the inspector. This removes the library copy and keeps the original imported file. Deleting the active wallpaper first restores the original Windows wallpaper. Built-in samples cannot be deleted.

### Online wallpapers

Choose Online wallpapers in the library: 20 images, 10 GIFs and 20 videos, with Static selected by default and no content categories. Search bilingual titles, original filenames or tags within the current type. Every source file is at least 1920 × 1080 and no larger than 20 MB. The maintainer approved retaining the ten GIFs from the local review build; native 1080p, 16:9, 20 MB and explicit licensing remain required, without a likes/favorites threshold. Select a card to see its author, license and available dated support votes, views or favorites. Popularity evidence opens the source statistics; GIF views sum reported dates only, with the period and available-day count in the tooltip. Unpublished metrics are not inferred. Start preview opens an image or muted animation inside the page; Stop preview returns to the cards. Downloads preserve the publisher-provided bytes. GIF, WMV, WebM and OGV files play without additional components.

Download original shows progress and supports cancellation. After verification, Apply to Desktop uses the downloaded original without recompression. Downloads continue when settings close; quitting cancels them. Deleting a local download makes it available again. A cached or bundled catalog remains visible when updates fail; uncached media still requires a connection to its source. [Source credits](wallpaper-sources.md)

## Desktop organizer and notes

Enable **Desktop Organizer** to show folder groups and hide native desktop icons. Items are grouped by type. Use **Sync Categories** to add new shortcuts. Click a folder to expand it. Click outside it or open an item to return to the overview.

Click the organizer or group title to edit it. Enter or click away to save, and Esc to cancel. Empty titles are supported. Right-click to create a group, add shortcuts or manage a group. You can change icon size and opacity in Settings. Scrolling works inside the group grid. Disabling the organizer or quitting normally restores the previous Windows icon visibility.

The panel starts centered. Right-click and choose **Move panel**, then drag it to a new position. The panel locks again when you release it. Choose **Center panel** to reset. Drag one folder tile onto another to change the order.

New files created or pasted onto the desktop while the organizer is enabled appear as separate, draggable desktop icons first. Right-click and choose **Put into collection**; grouped mode also offers each destination group. The original file stays on the desktop, and pending items survive restarts and manual synchronization. The taskbar's left section contains Power, Taskbar settings, Task Manager, and native Windows Search in that order. The three command icons use equal compact dimensions and hover labels, with no large blank gutters. Application icon spacing stays native.

Right-click the desktop background and choose **Light Block Desktop · New Note** to add a pinned note. On Windows 11, select **Show more options** first. The organizer panel's context menu also offers **New Note** and a sound switch. You can also pin an existing note in **Notes & Tasks**. Drag a desktop note to move it and double-click to edit.

## Screen capture

- **Select**. Press `Ctrl+Alt+A` by default, or set a different global shortcut under **Screen Capture**. Hover to detect a window or drag a region. Use Alt for the whole window. Drag a blue handle to resize, or use the move control to reposition. Right-click to clear the selection. Right-click again to exit.
- **Annotate**. Select a tool to show its options below the icon row. Rectangles and ellipses support fill, solid and dashed outlines, and adjustable width. Rectangles also support rounded corners. Hold Shift for a square or circle. Mosaic has a rounded selection mode and a brush mode. Enter text directly on the canvas, then drag to move it or double-click to edit again. Scroll over a supported annotation to resize it.
- **Save or pin**. Save PNG, copy to the clipboard, or pin the result above windows. Use Ctrl + scroll to zoom a pin. Right-click and choose **Edit pinned image** to annotate in place, then click **Finish editing**. Double-click outside edit mode to close the pin and copy its image.
- **Scrolling capture**. Choose **Scrolling Capture** and scroll within the frame. The preview shows the stitched image. If overlap is lost, scroll back or set it manually. The image limit is 40 megapixels.
- **Recording**. Choose **Start Recording**. The frame stays on screen. Use the stop control or `Ctrl+Alt+R` to finish. The preview starts with the quality and frame rate selected in **Screen Capture** settings. Both offer original resolution, 1080p, 720p, or 480p, and original frame rate or 12, 15, 24, 30, or 60 fps. The original frame rate setting captures at 24 fps. Save or copy MP4, GIF, or MKV. Recording is silent.

The installer and portable package include FFmpeg for recording and media export. When running from source, put a compatible FFmpeg executable in `runtime` or on PATH. Wallpaper playback uses the bundled Qt runtime. The media editor can trim or crop clips and export to MP4, GIF or MKV.

Drag the two recording timeline handles to trim. Click either handle, then use Left / Right to move one source frame, or Shift + Left / Right for ten frames. Times show milliseconds; at 24 fps one frame is approximately 41.667 ms. Click the track to seek and use Reset to restore the full recording. GIF export defaults to **Fast palette**; **Fine palette** suits rich gradients. Both retain the chosen resolution and frame rate, with different color analysis and dithering.

## Productivity tools

| Tool | Use |
| --- | --- |
| Launcher | Search Start menu apps, organizer items and built-in tools. Type arithmetic to calculate and copy the result |
| Clipboard history | Enable local text and image history, then search, pin, copy or clear it. History is off by default |
| Notes & Tasks | Write and search notes, mark tasks done, archive, set reminders or pin notes to the desktop |
| File Workbench | Preview images or text, inspect properties, calculate SHA-256 and preview batch renames with recovery and undo |
| Focus Workspaces | Save wallpaper and existing group positions. Apply the workspace and run focus or break timers |
