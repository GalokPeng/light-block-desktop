import sys

if __name__ == "__main__":
    if sys.platform != "win32":
        raise SystemExit("Still requires Windows 10 or 11.")
    from still.app import run
    raise SystemExit(run())
