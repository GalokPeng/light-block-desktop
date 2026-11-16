"""Isolated Inno installs; never registers, replaces or uninstalls the real AppId."""
import argparse
import ctypes
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


def verify_setup(setup, app_id, folder, language, title):
    app = folder / "安装测试 app"
    config = folder / "isolated-profile"
    quiet = ("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOFORCECLOSEAPPLICATIONS")
    lang_arg = (f"/LANG={language}",) if language else ()
    def install():
        subprocess.run((str(setup), *quiet, *lang_arg, "/CURRENTUSER", "/NOICONS", f"/DIR={app}",
                        f"/LOG={folder / 'setup.log'}"), check=True, timeout=180)
    try:
        install()
        if display_name(app_id) != title:
            raise RuntimeError(f"Unexpected installer language: {display_name(app_id)!r}")
        # Existing installation data must survive both an in-place upgrade and uninstall.
        data = app / "data-preview"
        data.mkdir()
        stable_data = app / "data/settings.json"
        stable_data.parent.mkdir()
        stable_data.write_bytes(b'{"version":"1.0.0","keep":"stable fixture"}')
        preserved = {"user-kept.txt": b"user-owned data", "wallpapers/original.jpg": b"unchanged media bytes", "notes/preserved.txt": "旧便签".encode()}
        for name, value in preserved.items():
            target = data / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(value)
        install()
        if any((data / name).read_bytes() != value for name, value in preserved.items()):
            raise RuntimeError("Upgrade changed user data")
        if stable_data.read_bytes() != b'{"version":"1.0.0","keep":"stable fixture"}':
            raise RuntimeError("Preview upgrade changed stable data")
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
        raise RuntimeError("Preview uninstall changed stable data")
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
        title = ("轻块桌面测试版（极度优化）" if langid in (0x0804, 0x1004) else "輕塊桌面測試版（極度優化）") if langid & 0x03FF == 4 else "Light Block Desktop Preview (Extensive optimization)"
        stable = folder / "stable-installation"
        stable.mkdir()
        sentinel = stable / "轻块桌面.exe"
        sentinel.write_bytes(b"stable application must not be replaced")
        for forbidden in (stable, stable / "data/nested-preview"):
            try:
                rejected = subprocess.run((str(setup), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOICONS", f"/DIR={forbidden}"), timeout=120)
                if rejected.returncode == 0 or sentinel.read_bytes() != b"stable application must not be replaced" or (forbidden / EXE_NAME).exists():
                    raise RuntimeError("Preview installer did not reject a stable installation folder or descendant")
            finally:
                # A failing test must still clean its isolated uninstall registration.
                accidental = next(forbidden.glob("unins*.exe"), None)
                if accidental:
                    subprocess.run((str(accidental), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"), check=True, timeout=120)
        for language, display in ((None, title), ("chinesesimplified", "轻块桌面测试版（极度优化）"), ("chinesetraditional", "輕塊桌面測試版（極度優化）"), ("english", "Light Block Desktop Preview (Extensive optimization)")):
            case = folder / (language or "system")
            case.mkdir()
            results.append(verify_setup(setup, app_id, case, language, display))
            print(f"PASS isolated install/upgrade/uninstall {language or 'system'}", flush=True)
    (ROOT / "build/setup-checks.json").write_text(json.dumps(dict(
        passed=True, version=version, executable_sha256=digest(portable / EXE_NAME),
        ui_dll=(portable / "ui-current.txt").read_text(encoding="ascii").strip(),
        setup_bytes=setup_bytes, setup_sha256=setup_hash, isolated_app_id=True,
        stable_installation_rejected=True, checks=results), ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
