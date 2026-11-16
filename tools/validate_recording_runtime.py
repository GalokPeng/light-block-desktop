"""Verify capabilities actually compiled into FFmpeg, not just configure arguments."""
import os
from pathlib import Path
import subprocess
import sys


CAPABILITIES = {
    "filters": {"amix", "apad", "aresample", "asetpts", "overlay", "palettegen", "paletteuse", "crop", "scale"},
    "muxers": {"mp4", "matroska", "gif", "image2", "rawvideo", "h264", "s16le"},
    "encoders": {"libx264", "ffv1", "aac", "flac", "pcm_s16le", "gif", "png"},
    "devices": {"gdigrab", "lavfi"},
}


def validate_output(category, output):
    actual = {parts[1] for line in output.splitlines() if len(parts := line.split()) >= 2}
    missing = CAPABILITIES[category] - actual
    if missing:
        raise ValueError(f"Recording runtime lacks {category}: {', '.join(sorted(missing))}")


def validate(binary):
    env = dict(os.environ, AV_LOG_FORCE_NOCOLOR="1")
    for category in CAPABILITIES:
        output = subprocess.check_output((str(Path(binary).resolve()), "-hide_banner", "-" + category),
                                         stderr=subprocess.STDOUT, text=True, env=env, timeout=20,
                                         creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        validate_output(category, output)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: validate_recording_runtime.py PATH_TO_FFMPEG")
    validate(sys.argv[1])
