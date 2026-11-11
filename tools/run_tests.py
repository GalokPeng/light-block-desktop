"""Run the Windows tests with or without an interactive Explorer desktop."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
DESKTOP_TESTS = {
    "test_gif_rendering.GifRenderingTests.test_gif_uses_native_video_output_with_changing_pixels",
    "test_model.StoreTests.test_native_basics",
}


def cases(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from cases(item)
        else:
            yield item


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--desktop", action="store_true", help="Include tests that require Explorer and monitors.")
    args = parser.parse_args()
    sys.path.insert(0, str(ROOT))
    os.environ["QT_QPA_PLATFORM"] = "windows:fontengine=freetype" if args.desktop else "offscreen"
    suite = unittest.defaultTestLoader.discover(str(ROOT / "tests"))
    if not args.desktop:
        print("Interactive Explorer and monitor tests require --desktop.", flush=True)
        suite = unittest.TestSuite(test for test in cases(suite) if test.id() not in DESKTOP_TESTS)
    # Network integration pumps a real event loop for much longer than the
    # synchronous Shell tests, which deliberately use mocked native handles.
    # Keep its QApplication and queued native events in a separate process.
    suite = unittest.TestSuite(test for test in cases(suite) if not test.id().startswith(("test_online_wallpapers.", "test_static_wallpaper.")))
    passed = unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful()
    print("Running online network/UI integration in an isolated QApplication process.", flush=True)
    online = subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", str(ROOT / "tests"),
                             "-p", "test_online_wallpapers.py", "-v"], cwd=ROOT)
    static = subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", str(ROOT / "tests"),
                             "-p", "test_static_wallpaper.py", "-v"], cwd=ROOT)
    return 0 if passed and online.returncode == 0 and static.returncode == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
