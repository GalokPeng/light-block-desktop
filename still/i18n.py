"""Select app messages from the Windows display language, independent of regional formats."""
from functools import lru_cache
from pathlib import Path
import ctypes
import json
import sys

CATALOG = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent.parent)) / "locales" / "en.json"
_preference = "system"


def language_for_id(langid):
    # PRIMARYLANGID: all Chinese variants use our Simplified Chinese messages.
    return "zh" if langid & 0x03FF == 0x04 else "en"


@lru_cache(maxsize=1)
def system_language():
    if sys.platform == "win32":
        return language_for_id(ctypes.windll.kernel32.GetUserDefaultUILanguage())
    return "en"


def set_language(preference):
    global _preference
    _preference = preference if preference in {"zh", "en"} else "system"


def display_language():
    return system_language() if _preference == "system" else _preference


@lru_cache(maxsize=1)
def english_messages():
    with CATALOG.open(encoding="utf-8") as file:
        return json.load(file)


def tr(text):
    return english_messages().get(text, text) if display_language() == "en" else text


def install_qt_translation(app):
    """Localize built-in Qt buttons and dialogs in Chinese."""
    previous = getattr(app, "_still_translator", None)
    if previous is not None:
        app.removeTranslator(previous)
        previous.deleteLater()
        app._still_translator = None
    if display_language() != "zh":
        return
    from PySide6.QtCore import QLibraryInfo, QTranslator

    translator = QTranslator(app)
    if translator.load("qtbase_zh_CN", QLibraryInfo.path(QLibraryInfo.LibraryPath.TranslationsPath)):
        app.installTranslator(translator)
        app._still_translator = translator
    else:
        translator.deleteLater()
