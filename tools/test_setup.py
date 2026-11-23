"""Isolated Inno installs; never registers, replaces or uninstalls the real AppId."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import uuid
import winreg
from release import ROOT, prefix, read_version, windows_version, smoke_at, EXE_NAME, MAX_SETUP_BYTES, digest


def display_name(app_id):
    key = rf"Software\Microsoft\Windows\CurrentVersion\Uninstall\{app_id}_is1"
    for flag in (winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY):
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key, access=winreg.KEY_READ | flag) as entry:
                return winreg.QueryValueEx(entry, "DisplayName")[0]
        except FileNotFoundError:
            pass
    return None


def seed_storage_upgrade(app, version):
    """Model a former release, a modified install and a partially downloaded update."""
    files = {}

    def put(path, data):
        path = app / path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        files[path] = data
        return path

    retired = put("assets/cursors/frost/normal.cur", (ROOT / "assets/cursors/frost/normal.cur").read_bytes())
    put("assets/cursors/frost/help.cur", b"user-modified cursor")
    put("_internal/user-created.txt", b"user-owned")
    stale_module = b"old media module fixture"
    module_hash = hashlib.sha256(stale_module).hexdigest()
    old_module = put(f"LightBlockMedia-{module_hash[:12]}.dll", stale_module)
    put("LightBlockUi-0123456789ab.dll", b"modified DLL")
    updates = "data/profile-v2/updates/"
    old_updates = "isolated-legacy/updates/"
    for tag in ("0.1.1", "0.1.2", "0.1.3"):
        data = b"previously installed " + tag.encode()
        put("runtime/setup-receipts/" + tag + ".sha256", hashlib.sha256(data).hexdigest().encode())
        prefix = old_updates if tag == "0.1.2" else updates
        put(prefix + f"Qingkuai-v{tag}-Setup-x64.exe", data if tag != "0.1.3" else b"modified")
    put(updates + f"Qingkuai-v{version}-Setup-x64.exe", b"current")
    put(updates + "Qingkuai-v0.1.1-Setup-x64.exe.partial", b"partial")
    put(updates + "Qingkuai-v1.0.0-Setup-x64.exe", b"wrong checksum for official release")
    put(updates + "Qingkuai-v99.0.0-Setup-x64.exe", b"future")
    removed = {retired, old_module, app / (updates + "Qingkuai-v0.1.1-Setup-x64.exe")}
    return removed, {path: value for path, value in files.items() if path not in removed}


def check_storage_upgrade(app, fixture):
    removed, kept = fixture
    if any(path.exists() for path in removed):
        raise RuntimeError(f"Verified retired files still present: {[p for p in removed if p.exists()]}")
    if any(path.read_bytes() != value for path, value in kept.items()):
        raise RuntimeError("Disk cleanup modified a protected file")
    manifest = json.loads((app / "assets/cursors/manifest.json").read_text(encoding="utf-8"))
    for theme in manifest["themes"]:
        if not (app / "assets/cursors" / theme / "normal.cur").is_file():
            raise RuntimeError("Active cursor artwork missing after upgrade")


def verify_setup(setup, app_id, folder, language, title):
    app = folder / "安装测试 app"
    config = folder / "isolated-profile"
    quiet = ("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOFORCECLOSEAPPLICATIONS")
    lang_arg = (f"/LANG={language}",) if language else ()
    def install():
        subprocess.run((str(setup), *quiet, *lang_arg, "/CURRENTUSER", "/NOICONS", f"/DIR={app}",
                        f"/LOG={folder / 'setup.log'}"), check=True, timeout=180)
    app.mkdir(parents=True)
    (app / EXE_NAME).write_bytes(b"isolated old executable fixture")
    try:
        install()
        if display_name(app_id) != title:
            raise RuntimeError(f"Unexpected installer language: {display_name(app_id)!r}")
        # Existing installation data must survive both an in-place upgrade and uninstall.
        data = app / "data/profile-v2"
        data.mkdir(parents=True)
        stable_data = app / "data/settings.json"
        stable_data.parent.mkdir(exist_ok=True)
        stable_data.write_bytes(b'{"version":"1.0.0","keep":"stable fixture"}')
        preserved = {"user-kept.txt": b"user-owned data", "wallpapers/original.jpg": b"unchanged media bytes", "notes/preserved.txt": "旧便签".encode()}
        for name, value in preserved.items():
            target = data / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(value)
        storage = seed_storage_upgrade(app, read_version())
        install()
        check_storage_upgrade(app, storage)
        if any((data / name).read_bytes() != value for name, value in preserved.items()):
            raise RuntimeError("Upgrade changed user data")
        if stable_data.read_bytes() != b'{"version":"1.0.0","keep":"stable fixture"}':
            raise RuntimeError("Upgrade changed stable data")
        smoke_at(app / EXE_NAME, config)
    finally:
        uninstaller = next(app.glob("unins*.exe"), None)
        if uninstaller:
            subprocess.run((str(uninstaller), *quiet), check=True, timeout=180)
    if display_name(app_id) is not None or (app / EXE_NAME).exists():
        raise RuntimeError("Isolated uninstall failed")
    if any((data / name).read_bytes() != value for name, value in preserved.items()):
        raise RuntimeError("Uninstall removed user data")
    if stable_data.read_bytes() != b'{"version":"1.0.0","keep":"stable fixture"}':
        raise RuntimeError("Uninstall changed stable data")
    return dict(language=language or "system", install=True, upgrade_preserves_data=True, startup=True, uninstall_preserves_data=True, stable_data_unchanged=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iscc", type=Path, required=True)
    args = parser.parse_args()
    version = read_version()
    portable = ROOT / "dist" / f"{prefix(version)}-Windows-x64"
    (ROOT / "build").mkdir(exist_ok=True)
    results = []
    with tempfile.TemporaryDirectory(prefix="isolated-setup-", dir=ROOT / "build") as temporary:
        folder = Path(temporary).resolve()
        app_id = "QingkuaiSetupQA-" + uuid.uuid4().hex
        subprocess.run((str(args.iscc.resolve()), "/Qp", f"/DAppVersion={version}", f"/DAppNumericVersion={windows_version(version)}", f"/DAppDir={portable}",
                        f"/DOutputDir={folder}", f"/DTestAppId={app_id}", str(ROOT / "installer/LightBlockDesktop.iss")), check=True)
        setup = folder / f"{prefix(version)}-Setup-x64.exe"
        setup_bytes = setup.stat().st_size
        if not 0 < setup_bytes <= MAX_SETUP_BYTES:
            raise RuntimeError("Preview Setup exceeds the 128 MiB package budget")
        setup_hash = digest(setup)
        langid = ctypes.windll.kernel32.GetUserDefaultUILanguage()
        title = ("轻块桌面" if langid in (0x0804, 0x1004) else "輕塊桌面") if langid & 0x03FF == 4 else "Light Block Desktop"
        for language, display in ((None, title), ("chinesesimplified", "轻块桌面"), ("chinesetraditional", "輕塊桌面"), ("english", "Light Block Desktop")):
            case = folder / (language or "system")
            case.mkdir()
            results.append(verify_setup(setup, app_id, case, language, display))
            print(f"PASS isolated install/upgrade/uninstall {language or 'system'}", flush=True)
    (ROOT / "build/setup-checks.json").write_text(json.dumps(dict(
        passed=True, version=version, executable_sha256=digest(portable / EXE_NAME),
        ui_dll=(portable / "ui-current.txt").read_text(encoding="ascii").strip(),
        setup_bytes=setup_bytes, setup_sha256=setup_hash, isolated_app_id=True,
        stable_upgrade_identity=True, checks=results), ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
