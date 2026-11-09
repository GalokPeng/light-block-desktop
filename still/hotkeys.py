from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W

from PySide6.QtCore import QAbstractNativeEventFilter, QObject, Qt, Signal
from PySide6.QtGui import QKeySequence

from .i18n import tr
from . import win32


MOD_ALT, MOD_CTRL, MOD_SHIFT, MOD_WIN = 1, 2, 4, 8
MOD_NOREPEAT = 0x4000
CAPTURE_IDS = (0xB11, 0xB15)
OTHER_KEYS = {0xB12: (82, "record", "Ctrl+Alt+R"), 0xB13: (32, "launcher", "Ctrl+Alt+Space"),
              0xB14: (86, "clipboard", "Ctrl+Alt+V")}
SPECIAL_KEYS = {
    Qt.Key.Key_Escape: 0x1B, Qt.Key.Key_Tab: 0x09, Qt.Key.Key_Backspace: 0x08,
    Qt.Key.Key_Return: 0x0D, Qt.Key.Key_Enter: 0x0D, Qt.Key.Key_Space: 0x20,
    Qt.Key.Key_Insert: 0x2D, Qt.Key.Key_Delete: 0x2E, Qt.Key.Key_Home: 0x24, Qt.Key.Key_End: 0x23,
    Qt.Key.Key_Left: 0x25, Qt.Key.Key_Up: 0x26, Qt.Key.Key_Right: 0x27, Qt.Key.Key_Down: 0x28,
    Qt.Key.Key_PageUp: 0x21, Qt.Key.Key_PageDown: 0x22, Qt.Key.Key_Print: 0x2C,
}


def shortcut_label(portable):
    return portable.replace("Meta+", "Win+")


def capture_binding(portable):
    sequence = QKeySequence.fromString(portable, QKeySequence.SequenceFormat.PortableText)
    if sequence.count() != 1:
        raise ValueError(tr("请按下一组截屏快捷键。"))
    combination = sequence[0]
    key, modifiers = combination.key(), combination.keyboardModifiers()
    flags = 0
    for modifier, flag in ((Qt.KeyboardModifier.AltModifier, MOD_ALT),
                           (Qt.KeyboardModifier.ControlModifier, MOD_CTRL),
                           (Qt.KeyboardModifier.ShiftModifier, MOD_SHIFT),
                           (Qt.KeyboardModifier.MetaModifier, MOD_WIN)):
        if modifiers & modifier:
            flags |= flag
    function = Qt.Key.Key_F1 <= key <= Qt.Key.Key_F24
    if key == Qt.Key.Key_F12:
        raise ValueError(tr("F12 由 Windows 保留，请选择其他快捷键。"))
    if not flags & (MOD_CTRL | MOD_ALT | MOD_WIN) and not (function or key == Qt.Key.Key_Print):
        raise ValueError(tr("请使用带 Ctrl、Alt 或 Win 的组合键，或选用功能键。"))
    if function:
        vk = 0x70 + key - Qt.Key.Key_F1
    elif modifiers & Qt.KeyboardModifier.KeypadModifier:
        vk = 0x60 + key - Qt.Key.Key_0 if Qt.Key.Key_0 <= key <= Qt.Key.Key_9 else {
            Qt.Key.Key_Plus: 0x6B, Qt.Key.Key_Minus: 0x6D, Qt.Key.Key_Asterisk: 0x6A,
            Qt.Key.Key_Slash: 0x6F, Qt.Key.Key_Period: 0x6E,
        }.get(key)
    elif Qt.Key.Key_A <= key <= Qt.Key.Key_Z or Qt.Key.Key_0 <= key <= Qt.Key.Key_9:
        vk = int(key)
    elif key in SPECIAL_KEYS:
        vk = SPECIAL_KEYS[key]
    elif 32 < key < 127:
        translated = win32.bind(win32.user32, "VkKeyScanW", C.c_short, W.WCHAR)(chr(key))
        vk = translated & 0xff if translated != -1 else None
        if vk is not None:
            shift_state = translated >> 8
            flags |= (MOD_SHIFT if shift_state & 1 else 0) | (MOD_CTRL if shift_state & 2 else 0) | (MOD_ALT if shift_state & 4 else 0)
    else:
        vk = None
    if vk is None:
        raise ValueError(tr("这个按键无法注册为 Windows 全局快捷键。"))
    if flags == (MOD_CTRL | MOD_ALT) and any(vk == item[0] for item in OTHER_KEYS.values()):
        raise ValueError(tr("该组合键已用于轻块桌面的其他功能，请选择另一组。"))
    return flags, vk


class HotkeyEvents(QAbstractNativeEventFilter):
    def __init__(self, owner):
        super().__init__()
        self.owner = owner

    def nativeEventFilter(self, event_type, message):
        msg = W.MSG.from_address(int(message))
        if msg.message == 0x0312 and msg.wParam in self.owner.registered:
            self.owner.triggered.emit(self.owner.registered[msg.wParam])
            return True, 0
        return False, 0


class Hotkeys(QObject):
    triggered = Signal(str)

    def __init__(self, controller):
        super().__init__()
        self.controller = controller
        self.registered = {}
        self.capture_id = None
        self.capture = None
        self.hwnd = int(controller.engine.notifier.winId())
        self.unregister = win32.bind(win32.user32, "UnregisterHotKey", W.BOOL, W.HWND, C.c_int)
        self.register = win32.bind(win32.user32, "RegisterHotKey", W.BOOL, W.HWND, C.c_int, W.UINT, W.UINT)
        self.events = HotkeyEvents(self)
        controller.app.installNativeEventFilter(self.events)

    def replace_capture(self, portable):
        binding = capture_binding(portable) if portable is not None else None
        if binding == self.capture:
            return True
        new_id = None
        if binding is not None:
            new_id = CAPTURE_IDS[1] if self.capture_id == CAPTURE_IDS[0] else CAPTURE_IDS[0]
            flags, key = binding
            if not self.register(self.hwnd, new_id, flags | MOD_NOREPEAT, key):
                return False
            self.registered[new_id] = "capture"
        if self.capture_id is not None:
            self.unregister(self.hwnd, self.capture_id)
            self.registered.pop(self.capture_id)
        self.capture_id, self.capture = new_id, binding
        return True

    def configure(self, enabled, recording=False, capture=None):
        desired = set(OTHER_KEYS) if enabled else set()
        if recording:
            desired.add(0xB12)
        for ident in (set(self.registered) & set(OTHER_KEYS)) - desired:
            self.unregister(self.hwnd, ident)
            self.registered.pop(ident)
        failed = []
        for ident in desired-set(self.registered):
            key, action, label = OTHER_KEYS[ident]
            if self.register(self.hwnd, ident, MOD_ALT | MOD_CTRL | MOD_NOREPEAT, key):
                self.registered[ident] = action
            else:
                failed.append(label)
        try:
            if not self.replace_capture(capture):
                failed.append(shortcut_label(capture))
        except ValueError:
            failed.append(shortcut_label(capture))
        return failed

    def cleanup(self):
        self.configure(False)
        self.controller.app.removeNativeEventFilter(self.events)
