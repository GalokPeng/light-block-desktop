import sys

if __name__ == "__main__":
    if sys.platform != "win32":
        raise SystemExit("Still requires Windows 10 or 11.")
    if sys.argv[1:] == ["--taskbar-style-helper"]:
        from still.taskbar_style_native import run_helper
        raise SystemExit(run_helper())
    from still.app import run
    raise SystemExit(run())
