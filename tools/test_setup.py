"""Verify localized installations, startup, uninstall and config preservation."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import winreg

from release import ROOT, prefix, read_version, smoke_at

UNINSTALL_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\{3CD16B31-077E-4CE9-BB01-578642D94876}_is1"


def installed_display_name():
    for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
        for flags in (winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY):
            try:
                with winreg.OpenKey(hive, UNINSTALL_KEY, access=winreg.KEY_READ | flags) as key:
                    return winreg.QueryValueEx(key, "DisplayName")[0]
            except FileNotFoundError:
                continue
    return None


def verify_setup(setup, language, title):
    with tempfile.TemporaryDirectory(prefix="test-setup-", dir=ROOT / "build") as folder:
        test = Path(folder)
        app = test / "app"
        config = test / "config"
        exe = app / "轻块桌面.exe"
        quiet = ("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART")
        lang_arg = (f"/LANG={language}",) if language else ()
        log = ROOT / "build" / f"setup-{language or 'auto'}.log"
        try:
            subprocess.run((str(setup), *quiet, *lang_arg, "/CURRENTUSER", "/NOICONS", f"/DIR={app}",
                            f"/LOG={log}"),
                           check=True, timeout=150)
            display = installed_display_name()
            if not display or not display.startswith(title):
                raise RuntimeError(f"Unexpected Setup display name for {language or 'auto'}: {display!r}")
            smoke_at(exe, config)
        finally:
            uninstall = next(app.glob("unins*.exe"), None)
            if uninstall:
                subprocess.run((str(uninstall), *quiet), check=True, timeout=120)
        if installed_display_name() is not None or exe.exists() or not (config / "settings.json").is_file():
            raise RuntimeError("Uninstall did not remove the app and preserve the test configuration.")
    print(f"Setup ({language or 'system language'}), app startup, and uninstall passed.", flush=True)


def main():
    if installed_display_name() is not None:
        raise SystemExit("The test requires a machine without an existing Light Block Desktop installation.")
    setup = ROOT / "release" / f"{prefix(read_version())}-Setup-x64.exe"
    (ROOT / "build").mkdir(exist_ok=True)
    langid = ctypes.windll.kernel32.GetUserDefaultUILanguage()
    if langid & 0x03FF == 0x04:
        system_title = "轻块桌面" if langid in (0x0804, 0x1004) else "輕塊桌面"
    else:
        system_title = "Light Block Desktop"
    for language, title in ((None, system_title), ("chinesesimplified", "轻块桌面"),
                            ("chinesetraditional", "輕塊桌面"), ("english", "Light Block Desktop")):
        verify_setup(setup, language, title)


if __name__ == "__main__":
    main()
