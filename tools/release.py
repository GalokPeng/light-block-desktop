"""Derive version metadata and prepare release assets from a Git commit."""
import argparse
import ast
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION_PATTERN = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)")
RELEASE_DOCS = ("LICENSE", "README.md", "CHANGES.md", "THIRD_PARTY_NOTICES.md", "TEST_REPORT.md")


def components(version):
    if not VERSION_PATTERN.fullmatch(version):
        raise ValueError(f"Expected X.Y.Z, got {version!r}")
    parts = tuple(int(part) for part in version.split("."))
    if any(part > 65535 for part in parts):
        raise ValueError("Windows version components cannot exceed 65535.")
    return parts


def read_version(root=ROOT):
    tree = ast.parse((root / "still" / "model.py").read_text(encoding="utf-8"))
    version, = (ast.literal_eval(node.value) for node in tree.body if isinstance(node, ast.Assign)
                and any(isinstance(target, ast.Name) and target.id == "VERSION" for target in node.targets))
    components(version)
    return version


def windows_version_file(root, version):
    template = (root / "version_info.txt").read_text(encoding="utf-8")
    target = root / "build" / "version_info.generated.txt"
    target.parent.mkdir(exist_ok=True)
    target.write_text(template.replace("@VERSION@", version).replace("@VERSION_TUPLE@", str((*components(version), 0))),
                      encoding="utf-8")
    return target


def prefix(version):
    return f"Qingkuai-v{version}"


def should_publish(version, tags):
    tag = f"v{version}"
    if tag in tags:
        return False
    versions = [tuple(int(part) for part in candidate[1:].split(".")) for candidate in tags
                if candidate.startswith("v") and VERSION_PATTERN.fullmatch(candidate[1:])]
    if versions and components(version) <= max(versions):
        raise ValueError(f"{tag} is older than a version that has already been tagged.")
    return True


def git(*args):
    return subprocess.check_output(("git", *args), cwd=ROOT, encoding="utf-8").strip()


def bundle(version, dist, output):
    name = prefix(version)
    portable = dist / f"{name}-Windows-x64"
    exe = portable / "轻块桌面.exe"
    setup = output / f"{name}-Setup-x64.exe"
    ffmpeg_sources = output / f"{name}-ffmpeg-source.zip"
    for file in (exe, setup, ffmpeg_sources):
        if not file.is_file():
            raise FileNotFoundError(file)
    for doc in RELEASE_DOCS:
        shutil.copy2(ROOT / doc, portable / doc)
    portable_zip = output / f"{portable.name}.zip"
    with zipfile.ZipFile(portable_zip, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for file in sorted(portable.rglob("*")):
            if file.is_file() and not file.is_relative_to(portable / "data"):
                archive.write(file, file.relative_to(dist))
    source_zip = output / f"{name}-source.zip"
    subprocess.run(("git", "archive", "--format=zip", f"--prefix={name}-source/", "-o", str(source_zip), "HEAD"),
                   cwd=ROOT, check=True)
    for file in (portable_zip, source_zip, ffmpeg_sources):
        with zipfile.ZipFile(file) as archive:
            damaged = archive.testzip()
            if damaged:
                raise ValueError(f"Corrupt archive entry: {damaged}")
    checksum = []
    for file in (setup, portable_zip, source_zip, ffmpeg_sources):
        with file.open("rb") as stream:
            checksum.append(f"{hashlib.file_digest(stream, 'sha256').hexdigest()}  {file.name}\n")
    (output / f"{name}-SHA256SUMS.txt").write_text("".join(checksum), encoding="ascii")


def check_cursor_assets(cursors):
    manifest = json.loads((cursors / "manifest.json").read_text(encoding="utf-8"))
    themes = manifest.get("themes")
    if (not isinstance(themes,list) or not 1 <= len(themes) <= 8
            or any(not isinstance(name,str) or not re.fullmatch(r"[a-z]{1,20}",name) for name in themes)
            or len(set(themes)) != len(themes)):
        raise ValueError("Invalid packaged cursor manifest")
    for theme in themes:
        if not (cursors / theme / "normal.cur").is_file() or not (cursors / theme / "busy-48.ani").is_file():
            raise FileNotFoundError(f"Packaged cursor assets are missing: {theme}")
    return tuple(themes)


def smoke_at(app, data):
    taskbar = app.parent / "_internal" / "runtime" / "QingkuaiTaskbarLayout.dll"
    if not taskbar.is_file():
        raise FileNotFoundError(f"Packaged native taskbar component is missing: {taskbar}")
    library = ctypes.WinDLL(str(taskbar.resolve()))
    free = ctypes.windll.kernel32.FreeLibrary
    free.argtypes = (ctypes.c_void_p,)
    try:
        getattr(library, "DllGetClassObject")
    finally:
        free(library._handle)
    data.mkdir(parents=True, exist_ok=True)
    (data / "settings.json").write_text(
        '{"capture_hotkey":false,"restore_on_start":false,"settings_theme":"light"}',
        encoding="utf-8")
    screenshot = data / "settings.png"
    windows = Path(os.environ["SystemRoot"])
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", PATH=f"{windows / 'System32'};{windows}")
    updater = app.parent / "_internal" / "runtime" / "QingkuaiUpdate.exe"
    if not updater.is_file():
        raise FileNotFoundError(f"Packaged update launcher is missing: {updater}")
    # Without required arguments it must reject the request without starting Setup.
    if subprocess.run((str(updater.resolve()),), env=env, timeout=10,
                      creationflags=subprocess.CREATE_NO_WINDOW).returncode != 1:
        raise RuntimeError("The packaged update launcher could not start safely.")
    pointer = app.parent / "_internal" / "runtime" / "QingkuaiPointer.exe"
    probe = subprocess.run((str(pointer.resolve()), "--probe"), env=env, timeout=10,
                           capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW, check=True)
    if probe.stdout != b"READY\n":
        raise RuntimeError("The packaged cursor host could not start safely.")
    cursors = app.parent / "_internal" / "assets" / "cursors"
    check_cursor_assets(cursors)
    ffmpeg = app.parent / "_internal" / "runtime" / "ffmpeg.exe"
    devices = subprocess.check_output((str(ffmpeg.resolve()), "-hide_banner", "-devices"),
                                      env=env, stderr=subprocess.STDOUT, text=True, timeout=15)
    if not re.search(r"(?m)^\s*D\s+gdigrab\s", devices):
        raise RuntimeError(f"The bundled FFmpeg is missing Windows desktop capture.\n{devices}")
    subprocess.run((str(ffmpeg.resolve()), "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                    "-i", "testsrc2=size=64x64:rate=5", "-frames:v", "2", "-c:v", "libx264",
                    str(data / "recording-smoke.mp4")), env=env, check=True, timeout=20)
    subprocess.run((str(app.resolve()), "--data-dir", str(data), "--screenshot", str(screenshot)),
                   env=env, check=True, timeout=40)
    if screenshot.stat().st_size < 10000 or "ERROR" in (data / "still.log").read_text(encoding="utf-8"):
        raise RuntimeError("The packaged settings window failed the startup smoke test.")


def smoke(app):
    with tempfile.TemporaryDirectory(prefix="qingkuai-release-") as folder:
        smoke_at(app, Path(folder))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("version", "plan", "bundle", "smoke"))
    parser.add_argument("--dist", type=Path, default=ROOT / "dist")
    parser.add_argument("--output", type=Path, default=ROOT / "release")
    parser.add_argument("--app", type=Path)
    args = parser.parse_args()
    version = read_version()
    if args.command == "version":
        print(version)
    elif args.command == "plan":
        publish = should_publish(version, git("tag", "--list", "v*").splitlines())
        print("true" if publish else "false")
    elif args.command == "bundle":
        bundle(version, args.dist.resolve(), args.output.resolve())
    else:
        smoke(args.app or args.dist / f"{prefix(version)}-Windows-x64" / "轻块桌面.exe")


if __name__ == "__main__":
    main()
