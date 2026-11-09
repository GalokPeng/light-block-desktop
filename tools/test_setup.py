"""Install into a temporary directory, start the app, and verify uninstall."""
from pathlib import Path
import subprocess
import tempfile
import winreg

from release import ROOT, prefix, read_version, smoke_at

UNINSTALL_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\{3CD16B31-077E-4CE9-BB01-578642D94876}_is1"


def has_existing_install():
    for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
        for flags in (winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY):
            try:
                with winreg.OpenKey(hive, UNINSTALL_KEY, access=winreg.KEY_READ | flags):
                    return True
            except FileNotFoundError:
                continue
    return False


def main():
    if has_existing_install():
        raise SystemExit("The test requires a machine without an existing Light Block Desktop installation.")
    setup = ROOT / "release" / f"{prefix(read_version())}-Setup-x64.exe"
    (ROOT / "build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="test-setup-", dir=ROOT / "build") as folder:
        test = Path(folder)
        app = test / "app"
        config = test / "config"
        quiet = ("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART")
        try:
            subprocess.run((str(setup), *quiet, "/CURRENTUSER", "/NOICONS", f"/DIR={app}",
                            f"/LOG={ROOT / 'build' / 'setup-smoke.log'}"), check=True, timeout=150)
            smoke_at(app / "轻块桌面.exe", config)
        finally:
            uninstall = next(app.glob("unins*.exe"), None)
            if uninstall:
                subprocess.run((str(uninstall), *quiet), check=True, timeout=120)
        if has_existing_install() or (app / "轻块桌面.exe").exists() or not (config / "settings.json").is_file():
            raise RuntimeError("Uninstall did not remove the installed app and preserve the test configuration.")
    print("Setup install, installed app startup, and uninstall passed.")


if __name__ == "__main__":
    main()
