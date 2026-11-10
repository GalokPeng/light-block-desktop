"""Per-user verbs for the native Windows desktop background menu."""
from pathlib import Path
import subprocess
import sys
import winreg

from .i18n import tr

SHELL_KEY = r"Software\Classes\DesktopBackground\Shell"
ACTIONS = ("note", "sound")


def register():
    executable = str(Path(sys.executable).resolve())
    for action, name in zip(ACTIONS, (tr("轻块桌面 · 新建便签"), tr("切换轻块桌面声音"))):
        path = SHELL_KEY + "\\LightBlockDesktop." + action
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, path) as key:
            winreg.SetValueEx(key, "MUIVerb", 0, winreg.REG_SZ, name)
            winreg.SetValueEx(key, "Icon", 0, winreg.REG_SZ, f'"{executable}",0')
            with winreg.CreateKey(key, "command") as command:
                value = f'"{executable}" ' + subprocess.list2cmdline(["--desktop-action", action])
                winreg.SetValueEx(command, "", 0, winreg.REG_SZ, value)
