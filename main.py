import sys

if __name__ == "__main__":
    if sys.platform != "win32":
        raise SystemExit("Still requires Windows 10 or 11.")
    # Legacy Dock and tray helper, retained for local comparison.
    # if sys.argv[1:] == ["--taskbar-style-helper"]:
    #     from still.taskbar_style_native import run_helper
    #     raise SystemExit(run_helper())
    if sys.argv[1:] == ["--media-player-helper"]:
        from still.media_worker import run_helper
        raise SystemExit(run_helper())
    if sys.argv[1:] == ["--native-taskbar-style-helper"]:
        from still.native_taskbar import run_helper
        raise SystemExit(run_helper())
    if sys.argv[1:] == ["--static-wallpaper-helper"]:
        from still.static_wallpaper import run_helper
        raise SystemExit(run_helper())
    from still.app import run
    raise SystemExit(run())
