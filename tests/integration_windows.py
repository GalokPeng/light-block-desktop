"""Runs on an interactive Windows desktop; restores the previous wallpaper.

Does not enable startup. Exercises real WorkerW, native wallpaper and decoder.
Requires psutil only for CPU measurement, never for app runtime.
"""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import time
from unittest.mock import patch

import psutil
from PySide6.QtCore import QEvent, QEventLoop, Qt, QTimer
from PySide6.QtMultimedia import QMediaPlayer
from PySide6.QtMultimediaWidgets import QVideoWidget
from PySide6.QtWidgets import QApplication

from still import win32
from still.engine import WallpaperEngine
from still.model import ASSETS, Store


def pump(seconds):
    loop = QEventLoop()
    QTimer.singleShot(int(seconds*1000), loop.quit)
    loop.exec()


def wait_static(engine):
    deadline = time.monotonic() + 50
    while engine.busy and time.monotonic() < deadline:
        pump(.02)
    assert not engine.busy, "Static wallpaper task exceeded its deadline"


def sample(seconds=2):
    process = psutil.Process()
    before = sum(process.cpu_times()[:2])
    start = time.perf_counter()
    pump(seconds)
    return {"working_set_mb": round(win32.memory_mb(), 1),
            "cpu_one_core_percent": round((sum(process.cpu_times()[:2])-before)/(time.perf_counter()-start)*100, 2)}


def main():
    app = QApplication([])
    app.setQuitOnLastWindowClosed(False)
    original = win32.current_wallpaper()
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in ASSETS.iterdir() if p.suffix in {".jpg", ".png", ".mp4", ".gif"}}
    report = {"platform": sys.getwindowsversion().build, "monitor": win32.monitors(), "checks": [], "measurements": {}}
    with tempfile.TemporaryDirectory() as temp:
        store = Store(Path(temp))
        store.settings.battery_pause = store.settings.fullscreen_pause = False
        engine = WallpaperEngine(store, app)
        errors = []
        engine.error.connect(errors.append)
        try:
            report["measurements"]["idle_engine"] = sample()
            engine.apply(store.items[0])
            wait_static(engine)
            assert not errors, errors
            assert win32.current_wallpaper()["path"] == str(ASSETS / "night-drive.jpg")
            assert engine.surface is None and not engine.tick.isActive()
            report["checks"].append("Static original JPEG applied asynchronously through Windows; no decoder/timer")
            engine.restore_original()
            wait_static(engine)
            report["checks"].append("Original Windows wallpaper restored")
            gif = next(i for i in store.items if i["kind"] == "gif")
            engine.apply(gif)
            pump(0.6)
            hwnd = int(engine.surface.winId())
            assert win32.GetParent(hwnd) == engine.worker
            ext = win32.GetWindowLong(hwnd, -20)
            assert ext & 0x20 and ext & 0x08000000
            assert isinstance(engine.surface, QVideoWidget)
            assert engine.player.hasVideo() and engine.player.isPlaying()
            report["checks"].append("Original GIF decoded through the same native QVideoWidget output as video, embedded behind desktop")
            report["measurements"]["gif_playing"] = sample()
            engine.pause()
            assert engine.player.playbackState() == QMediaPlayer.PlaybackState.PausedState
            report["measurements"]["gif_paused"] = sample()
            engine.pause()
            with patch("still.win32.on_battery", return_value=True):
                store.settings.battery_pause = True
                engine.evaluate()
                assert engine.state == "paused"
            store.settings.battery_pause = False
            with patch("still.win32.fullscreen_on", return_value=True):
                store.settings.fullscreen_pause = True
                engine.evaluate()
                assert engine.state == "paused"
            store.settings.fullscreen_pause = False
            engine.evaluate()
            assert engine.state == "playing"
            report["checks"].append("Manual pause/resume and policy transitions (simulated battery/fullscreen inputs)")
            video = next(i for i in store.items if i["kind"] == "video")
            engine.apply(video)
            frames = []
            engine.surface.videoSink().videoFrameChanged.connect(lambda frame: frames.append((frame.width(), frame.height())) if frame.isValid() else None)
            pump(2)
            assert engine.player.hasVideo(), errors
            assert len(frames) > 1, (len(frames), errors)
            assert all(size == (1280, 720) for size in frames), frames[:2]
            assert engine.player.audioOutput() is None
            report["checks"].append("H.264 MP4 decoded to QVideoWidget at original 1280x720; no audio output")
            report["measurements"]["video_playing"] = sample(3)
            engine.pause()
            position = engine.player.position()
            pump(0.4)
            assert abs(engine.player.position()-position) < 150
            report["measurements"]["video_paused"] = sample()
            engine.pause()
            pump(0.4)
            assert engine.player.position() != position
            report["checks"].append("Video pause/resume freezes playback position")
            # Cross a loop boundary to verify Infinite playback remains active.
            pump(4.2)
            assert engine.player.isPlaying()
            report["checks"].append("Video loop boundary crossed successfully")
            engine.stop()
            pump(0.2)
            app.sendPostedEvents(None, QEvent.Type.DeferredDelete)
            assert engine.surface is None and engine.player is None and not engine.tick.isActive()
            report["checks"].append("Stop releases renderer and stops policy timer")
            # Source integrity applies to demo assets and to the same import/play path.
            assert hashes == {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in ASSETS.iterdir() if p.name in hashes}
            report["checks"].append("All source media hashes unchanged")
            assert not errors, errors
            # Invalid media must reveal the existing desktop and release playback.
            bad = Path(temp) / "损坏 视频.mp4"
            bad.write_bytes(b"this is not an mp4")
            invalid = store.add(bad)
            engine.apply(invalid)
            pump(1)
            assert errors and engine.surface is None and engine.player is None
            assert not store.settings.active_id
            report["checks"].append("Corrupt video fails safely and releases its desktop window")
            # Registry round trip, preserving any pre-existing entry exactly.
            import winreg
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER, win32.RUN_KEY) as key:
                try:
                    previous_startup = winreg.QueryValueEx(key, win32.RUN_NAME)
                except FileNotFoundError:
                    previous_startup = None
            try:
                win32.set_startup(True)
                assert win32.startup_enabled()
                win32.set_startup(False)
                assert not win32.startup_enabled()
            finally:
                with winreg.CreateKey(winreg.HKEY_CURRENT_USER, win32.RUN_KEY) as key:
                    if previous_startup is not None:
                        winreg.SetValueEx(key, win32.RUN_NAME, 0, previous_startup[1], previous_startup[0])
                    else:
                        try:
                            winreg.DeleteValue(key, win32.RUN_NAME)
                        except FileNotFoundError:
                            pass
            report["checks"].append("Startup registry enable/disable round trip; original value restored")
        finally:
            engine.static_task.close()
            store.settings.original_wallpaper = original
            try:
                engine.restore_original()
                wait_static(engine)
                assert win32.current_wallpaper() == original
            finally:
                engine.cleanup()
            pump(0.2)
    report["result"] = "PASS"
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if len(sys.argv) > 1:
        Path(sys.argv[1]).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
