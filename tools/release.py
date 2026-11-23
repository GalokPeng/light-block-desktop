"""Reproducible stable builds with upgrade-compatible identity and exact source checks."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION_PATTERN = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")
EXE_NAME = "轻块桌面.exe"
SETUP_ALIAS = "Qingkuai-Setup-x64.exe"
TAG_PREFIX = "v"
APP_ID = "{3CD16B31-077E-4CE9-BB01-578642D94876}"
MAX_SETUP_BYTES = 128 * 1024 * 1024


def components(version):
    match = VERSION_PATTERN.fullmatch(version)
    if not match:
        raise ValueError(f"Stable version must be X.Y.Z, got {version!r}")
    parts = tuple(map(int, match.groups()[:3]))
    if any(value > 65535 for value in parts):
        raise ValueError("Windows version components cannot exceed 65535")
    return parts


def windows_version(version):
    return ".".join(map(str, (*components(version)[:3], 0)))


def read_version(root=ROOT):
    text = (root / "src/version.hpp").read_text(encoding="utf-8-sig")
    found = re.findall(r'^#define LB_VERSION_UTF8 "([^"]+)"$', text, re.M)
    if len(found) != 1:
        raise ValueError("Exactly one LB_VERSION_UTF8 is required")
    version = found[0]
    parts = components(version)[:3]
    if not re.search(r'^#define LB_VERSION_COMMA\s+' + ','.join(map(str, (*parts, 0))) + r'\s*$', text, re.M):
        raise ValueError("Windows resource version differs from application version")
    if f'#define LB_VERSION_NUMERIC_UTF8 "{windows_version(version)}"' not in text:
        raise ValueError("Numeric installer version differs from Windows resources")
    if '#define LB_PREVIEW_BUILD 0' not in text or f'#define LB_INSTALLER_APP_ID "{APP_ID}"' not in text:
        raise ValueError("Stable product identity is required")
    return version


def prefix(version):
    return f"Qingkuai-v{version}"


def digest(file):
    with Path(file).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def git(*args):
    return subprocess.check_output(("git", *args), cwd=ROOT, encoding="utf-8").strip()


def should_publish(version, tags):
    if TAG_PREFIX + version in tags:
        return False
    previous = [components(tag[len(TAG_PREFIX):]) for tag in tags if tag.startswith(TAG_PREFIX) and VERSION_PATTERN.fullmatch(tag[len(TAG_PREFIX):])]
    if previous and components(version) <= max(previous):
        raise ValueError("Preview version must be newer than every published stable tag")
    return True


def verify_source_ref(version):
    head = git("rev-parse", "HEAD")
    branch = git("branch", "--show-current")
    if os.environ.get("GITHUB_ACTIONS") == "true":
        if os.environ.get("GITHUB_REF") not in ("refs/heads/main", "refs/heads/cpp-preview", "refs/tags/" + TAG_PREFIX + version) or os.environ.get("GITHUB_SHA") != head:
            raise ValueError("CI must build the exact triggering SHA on the preview branch/tag")
    elif branch not in ("main", "cpp-preview") and (branch or git("describe", "--tags", "--exact-match") != TAG_PREFIX + version):
        raise ValueError("Packaging is restricted to main, staging branch or the exact release tag")
    return head


def safe_path(base, relative):
    root = Path(base).resolve()
    target = (root / relative).resolve()
    if not target.is_relative_to(root) or target == root:
        raise ValueError(f"Unsafe artifact path: {relative}")
    return target


def copy_file(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def verify_ui(app):
    manifest = json.loads((app / "ui-runtime.json").read_text(encoding="utf-8-sig"))
    name = (app / "ui-current.txt").read_text(encoding="ascii").strip()
    if manifest.get("schema") != 1 or manifest.get("ui_dll") != name or not re.fullmatch(r"LightBlockUi-[a-f0-9]{12}\.dll", name):
        raise ValueError("Invalid Qt runtime identity")
    for record in manifest["files"]:
        if digest(safe_path(app, record["path"])) != record["sha256"]:
            raise ValueError(f"Qt runtime integrity mismatch: {record['path']}")
    return manifest


def verify_identity(app, version):
    """Reject stale/renamed stable binaries before they enter a preview package."""
    api = ctypes.WinDLL("version", use_last_error=True)
    api.GetFileVersionInfoSizeW.argtypes = (ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_uint))
    api.GetFileVersionInfoW.argtypes = (ctypes.c_wchar_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p)
    api.VerQueryValueW.argtypes = (ctypes.c_void_p, ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint))
    path = str((app / EXE_NAME).resolve())
    unused = ctypes.c_uint()
    size = api.GetFileVersionInfoSizeW(path, ctypes.byref(unused))
    if not size:
        raise ValueError("Preview executable has no Windows version resource")
    buffer = ctypes.create_string_buffer(size)
    if not api.GetFileVersionInfoW(path, 0, size, buffer):
        raise ctypes.WinError(ctypes.get_last_error())
    for key, expected in (("ProductVersion", version), ("FileVersion", windows_version(version)),
                          ("OriginalFilename", EXE_NAME), ("ProductName", "Light Block Desktop")):
        address, length = ctypes.c_void_p(), ctypes.c_uint()
        if not api.VerQueryValueW(buffer, "\\StringFileInfo\\040904b0\\" + key, ctypes.byref(address), ctypes.byref(length)) or ctypes.wstring_at(address.value) != expected:
            raise ValueError(f"Preview executable identity mismatch: {key}")
    if (app / "轻块桌面测试版.exe").exists():
        raise ValueError("Preview executable must not enter stable staging")


def stage(version, dist):
    source = dist / "LightBlockDesktop"
    verify_ui(source)
    verify_identity(source, version)
    target = safe_path(dist, f"{prefix(version)}-Windows-x64")
    if target.exists():
        if target.parent != dist.resolve() or not target.name.startswith("Qingkuai-v"):
            raise ValueError("Refusing to replace an unexpected staging directory")
        shutil.rmtree(target)
    target.mkdir(parents=True)
    for name in (EXE_NAME, "ui-current.txt", "ui-runtime.json"):
        copy_file(source / name, target / name)
    for record in verify_ui(source)["files"]:
        copy_file(safe_path(source, record["path"]), safe_path(target, record["path"]))
    cursor_root = ROOT / "assets/cursors"
    themes = json.loads((cursor_root / "manifest.json").read_text(encoding="utf-8"))["themes"]
    for theme in themes:
        if not (safe_path(cursor_root, theme) / "normal.cur").is_file():
            raise ValueError(f"Missing cursor artwork for {theme}")

    def obsolete_themes(directory, names):
        if Path(directory) == cursor_root:
            return [name for name in names if (cursor_root / name).is_dir() and name not in themes]
        return []

    shutil.copytree(ROOT / "assets", target / "assets", ignore=obsolete_themes, dirs_exist_ok=True)
    shutil.copytree(ROOT / "licenses", target / "licenses", dirs_exist_ok=True)
    runtime = source / "runtime"
    layout = (runtime / "taskbar-current.txt").read_text(encoding="ascii").strip()
    if not re.fullmatch(r"LightBlockNativeTaskbar-[a-f0-9]{12}\.dll", layout):
        raise ValueError("Invalid taskbar component identity")
    for name in (layout, "taskbar-current.txt", "QingkuaiUpdate.exe"):
        copy_file(runtime / name, target / "runtime" / name)
    for name in ("ffmpeg.exe", "build-manifest.json", "FFmpeg-GPL-3.0.txt", "FFmpeg-Native-runtime.txt"):
        copy_file(runtime / "recording" / name, target / "runtime/recording" / name)
    recording = json.loads((target / "runtime/recording/build-manifest.json").read_text(encoding="utf-8-sig"))
    if digest(target / "runtime/recording/ffmpeg.exe") != recording["binary_sha256"]:
        raise ValueError("Recording binary integrity mismatch")
    for name in ("LICENSE", "README.md", "README.en.md", "CHANGES.md", "THIRD_PARTY_NOTICES.md", "TEST_REPORT.md"):
        if (ROOT / name).is_file():
            copy_file(ROOT / name, target / name)
    info = dict(version=version, source_commit=git("rev-parse", "HEAD"), source_dirty=bool(git("status", "--porcelain")), executable_sha256=digest(target / EXE_NAME),
                ui_dll=verify_ui(target)["ui_dll"], ffmpeg_sha256=recording["binary_sha256"], runtime_language="C++", channel="stable", installer_app_id=APP_ID)
    (target / "build-info.json").write_text(json.dumps(info, indent=2), encoding="utf-8")
    if (target / "data").exists() or (target / "data-preview").exists() or any(target.rglob("*.pyc")) or any(target.rglob("python3*.dll")):
        raise ValueError("User data or Python runtime leaked into staging")
    for line in (ROOT / "installer/retired-files.sha256").read_text(encoding="ascii").splitlines():
        if line.startswith("#"):
            continue
        checksum, path = line.split("  ", 1)
        if not re.fullmatch(r"[0-9a-f]{64}", checksum) or safe_path(target, path).exists():
            raise ValueError(f"Invalid retirement entry or active file marked for deletion: {path}")
    return target


def smoke_at(exe, data):
    exe = Path(exe).resolve()
    app = exe.parent
    verify_ui(app)
    windows = Path(os.environ["SystemRoot"])
    env = {key: value for key, value in os.environ.items() if not key.startswith(("QT_", "PYTHON", "QINGKUAI_"))}
    env["PATH"] = f"{windows / 'System32'};{windows}"
    # Widgets stay hidden through the fixture itself. Native HWND hit tests must
    # use the Windows plugin; the offscreen plugin supplies no valid HWND.
    env["QT_QPA_PLATFORM"] = "windows"
    env["AV_LOG_FORCE_NOCOLOR"] = "1"
    if os.environ.get("LIGHTBLOCK_TEST_SCOPE") == "organizer":
        data = Path(data); data.mkdir(parents=True, exist_ok=True)
        report = data.parent / "organizer-package-checks.json"
        subprocess.run((str(exe), "--organizer-test", "--data-dir", str(data), "--report", str(report)), env=env, check=True, timeout=90)
        result = json.loads(report.read_text(encoding="utf-8-sig"))
        if result.get("failed") or not result.get("passed"):
            raise RuntimeError("Packaged organizer regression failed")
        return result
    updater = app / "runtime/QingkuaiUpdate.exe"
    if subprocess.run((str(updater),), env=env, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW).returncode != 1:
        raise RuntimeError("Updater did not reject missing arguments")
    ffmpeg = app / "runtime/recording/ffmpeg.exe"
    devices = subprocess.check_output((str(ffmpeg), "-hide_banner", "-devices"), env=env, stderr=subprocess.STDOUT, text=True, timeout=20, creationflags=subprocess.CREATE_NO_WINDOW)
    if not re.search(r"(?m)^\s*D\s+gdigrab\s", devices):
        raise RuntimeError(f"Bundled FFmpeg lacks Windows capture: {devices[:2000]!r}")
    data = Path(data)
    data.mkdir(parents=True, exist_ok=True)
    snapshots = data.parent / (data.name + "-snapshots")
    try:
        subprocess.run((str(exe), "--data-dir", str(data), "--ui-snapshots", str(snapshots)), env=env, check=True, timeout=180)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        evidence = ROOT / "build/package-smoke-evidence" / uuid.uuid4().hex
        evidence.mkdir(parents=True)
        if snapshots.is_dir():
            shutil.copytree(snapshots, evidence / "snapshots")
        failures = []
        for report in snapshots.rglob("*checks*.json"):
            try:
                record = json.loads(report.read_text(encoding="utf-8-sig"))
                if record.get("passed") is False:
                    failures.append(report.name + ": " + ", ".join(key for key, value in record.items() if value is False and key not in ("passed", "python_runtime")))
            except (ValueError, OSError):
                failures.append(report.name + ": unreadable")
        (evidence / "failure.txt").write_text(str(error) + "\n" + "\n".join(failures), encoding="utf-8")
        raise RuntimeError(f"Packaged UI smoke failed; evidence: {evidence}; {'; '.join(failures)}") from error
    result = json.loads((snapshots / "ui-checks.json").read_text(encoding="utf-8-sig"))
    if not result.get("passed") or result.get("python_runtime"):
        raise RuntimeError("Packaged C++ frontend failed its smoke tests")
    return result


def smoke(exe):
    with tempfile.TemporaryDirectory(prefix="qingkuai-release-") as folder:
        smoke_at(exe, Path(folder) / "profile")


def archive_files(target, files):
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for source, name in files:
            archive.write(source, name)
    with zipfile.ZipFile(target) as archive:
        if archive.testzip():
            raise ValueError(f"Archive integrity check failed: {target}")


def bundle(version, dist, output):
    name = prefix(version)
    portable = dist / f"{name}-Windows-x64"
    if git("status", "--porcelain"):
        raise ValueError("Commit the release source before bundling")
    verify_source_ref(version)
    setup = output / f"{name}-Setup-x64.exe"
    if not 0 < setup.stat().st_size <= MAX_SETUP_BYTES:
        raise ValueError("Preview Setup exceeds the 128 MiB package budget")
    verify_ui(portable)
    info = json.loads((portable / "build-info.json").read_text(encoding="utf-8"))
    if info.get("source_dirty", True) or info["source_commit"] != git("rev-parse", "HEAD") or info["version"] != version or info.get("channel") != "stable" or info.get("installer_app_id") != APP_ID:
        raise ValueError("Staged binaries and source commit disagree")
    portable_zip = output / f"{portable.name}.zip"
    archive_files(portable_zip, [(file, str(file.relative_to(dist))) for file in sorted(portable.rglob("*")) if file.is_file()])
    source_zip = output / f"{name}-source.zip"
    subprocess.run(("git", "archive", "--format=zip", f"--prefix={name}-source/", "-o", str(source_zip), "HEAD"), cwd=ROOT, check=True)
    sources = ROOT / "build/corresponding-sources"
    ffmpeg = output / f"{name}-ffmpeg-source.zip"
    copy_file(sources / "ffmpeg-source.zip", ffmpeg)
    recording = json.loads((portable / "runtime/recording/build-manifest.json").read_text(encoding="utf-8-sig"))
    if digest(ffmpeg) != recording["source_sha256"]:
        raise ValueError("FFmpeg source archive does not match staged runtime")
    qt = output / f"{name}-qt-source.zip"
    files = [(file, "sources/" + file.name) for file in sorted(sources.glob("qt*.tar.xz"))]
    if len(files) != 3:
        raise ValueError("Qt Base, Image Formats and Multimedia sources are required")
    files.append((sources / "FFmpeg-n7.1.tar.gz", "sources/FFmpeg-n7.1.tar.gz"))
    files += [(ROOT / "tools" / file, "tools/" + file) for file in ("setup_ui_sdk.ps1", "build_ui.ps1", "build_media_ui.ps1", "build_qt_ffmpeg.sh")]
    files += [(file, str(file.relative_to(portable))) for file in sorted((portable / "licenses").rglob("*")) if file.is_file()]
    archive_files(qt, files)
    alias = output / SETUP_ALIAS
    copy_file(setup, alias)
    assets = (setup, alias, portable_zip, source_zip, ffmpeg, qt)
    for file in (source_zip, ffmpeg):
        with zipfile.ZipFile(file) as archive:
            if archive.testzip():
                raise ValueError(f"Invalid source archive {file.name}")
    (output / f"{name}-SHA256SUMS.txt").write_text("".join(f"{digest(file)}  {file.name}\n" for file in assets), encoding="ascii")


def verify_release(version, output, expected_commit=None, require_store=False):
    if (output / "Qingkuai-Preview-Setup-x64.exe").exists():
        raise ValueError("Preview alias is forbidden in stable output")
    names = set()
    for line in (output / f"{prefix(version)}-SHA256SUMS.txt").read_text(encoding="ascii").splitlines():
        expected, name = line.split("  ", 1)
        if Path(name).name != name or name in names or digest(output / name) != expected:
            raise ValueError(f"Invalid release asset: {name}")
        names.add(name)
    required = {f"{prefix(version)}-{suffix}" for suffix in ("Setup-x64.exe", "Windows-x64.zip", "source.zip", "ffmpeg-source.zip", "qt-source.zip")} | {SETUP_ALIAS}
    store_package = f"{prefix(version)}-x64.msix"
    if require_store or (output / store_package).exists():
        required.add(store_package)
    if names != required:
        raise ValueError("Release assets are incomplete")
    portable = f"{prefix(version)}-Windows-x64"
    with zipfile.ZipFile(output / (portable + ".zip")) as archive:
        record = archive.getinfo(portable + "/build-info.json")
        if record.file_size > 64 * 1024:
            raise ValueError("Unexpected packaged build identity size")
        info = json.loads(archive.read(record))
    if info.get("version") != version or info.get("channel") != "stable" or info.get("installer_app_id") != APP_ID or info.get("source_dirty", True):
        raise ValueError("Packaged preview identity is invalid")
    if expected_commit is None and os.environ.get("GITHUB_ACTIONS") == "true":
        expected_commit = os.environ.get("GITHUB_SHA")
        if not expected_commit:
            raise ValueError("CI triggering SHA is missing")
    if expected_commit and info.get("source_commit") != expected_commit:
        raise ValueError("Packaged source commit does not match the release target")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("version", "numeric-version", "tag", "plan", "stage", "smoke", "bundle", "verify"))
    parser.add_argument("--dist", type=Path, default=ROOT / "dist")
    parser.add_argument("--output", type=Path, default=ROOT / "release")
    parser.add_argument("--require-store", action="store_true", help="Require a checksummed Store package in the release")
    parser.add_argument("--app", type=Path)
    args = parser.parse_args()
    version = read_version()
    if args.command == "version": print(version)
    elif args.command == "numeric-version": print(windows_version(version))
    elif args.command == "tag": print(TAG_PREFIX + version)
    elif args.command == "plan": print("true" if should_publish(version, git("tag", "--list", "v*").splitlines()) else "false")
    elif args.command == "stage": print(stage(version, args.dist.resolve()))
    elif args.command == "smoke": smoke(args.app or args.dist / f"{prefix(version)}-Windows-x64" / EXE_NAME)
    elif args.command == "bundle": bundle(version, args.dist.resolve(), args.output.resolve())
    else: verify_release(version, args.output.resolve(), require_store=args.require_store)


if __name__ == "__main__":
    main()
