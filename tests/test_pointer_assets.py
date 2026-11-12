"""Windows must decode every original asset before a theme can be selected."""
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import struct
import unittest

from still import win32
from still.model import ASSETS
from still.pointer_style import PointerTrail
from PySide6.QtCore import QPoint
from PySide6.QtWidgets import QApplication
from unittest.mock import patch


class PointerAssetTests(unittest.TestCase):
    def test_all_roles_and_animated_cursors_decode_in_windows(self):
        manifest = json.loads((ASSETS/'cursors/manifest.json').read_text())
        load = win32.bind(win32.user32,'LoadImageW',W.HANDLE,W.HINSTANCE,W.LPCWSTR,W.UINT,C.c_int,C.c_int,W.UINT)
        destroy = win32.bind(win32.user32,'DestroyCursor',W.BOOL,W.HANDLE)
        self.assertEqual(len(manifest['themes']),3)
        self.assertEqual(len(manifest['roles']),17)
        for theme in manifest['themes']:
            for role in manifest['roles']:
                path = ASSETS/'cursors'/theme/(role+'.cur')
                data = path.read_bytes()
                self.assertEqual(struct.unpack_from('<HHH',data), (0,2,5))
                for i,size in enumerate(manifest['sizes']):
                    w,h,_,_,hx,hy,length,offset=struct.unpack_from('<BBBBHHII',data,6+i*16)
                    self.assertEqual((w,h),(size,size))
                    self.assertLess(hx,size);self.assertLess(hy,size)
                    self.assertLessEqual(offset+length,len(data))
                    handle = load(None,str(path),2,size,size,0x10)
                    self.assertTrue(handle,f'{theme}/{role} at {size}')
                    self.assertTrue(destroy(handle))
            for role in manifest['animated']:
                for suffix in ['']+['-'+str(size) for size in manifest['sizes']]:
                    path = ASSETS/'cursors'/theme/(role+suffix+'.ani')
                    data=path.read_bytes()
                    self.assertEqual(data[:4],b'RIFF')
                    self.assertEqual(data[8:12],b'ACON')
                    handle=load(None,str(path),2,48,48,0x10)
                    self.assertTrue(handle,f'{theme}/{role}{suffix} animation')
                    self.assertTrue(destroy(handle))

    def test_trail_stops_painting_when_idle_and_unhooks_when_disabled(self):
        app=QApplication.instance() or QApplication([])
        app.setQuitOnLastWindowClosed(False)
        with patch('still.pointer_style.MouseHook') as hook_type, \
                patch('still.pointer_style.monotonic',return_value=10.) as clock, \
                patch('still.pointer_style.QCursor.pos',return_value=QPoint(100,100)) as cursor:
            hook_type.return_value.start.return_value=True
            trail=PointerTrail()
            try:
                trail.set_enabled(True)
                self.assertFalse(trail.timer.isActive())
                trail.begin()
                self.assertTrue(trail.timer.isActive())
                for i in range(40):
                    clock.return_value=10.+i*.005
                    cursor.return_value=QPoint(100+i,100+i)
                    trail.sample()
                self.assertLessEqual(len(trail.points),20)
                self.assertLessEqual(trail.width(),420)
                self.assertLessEqual(trail.height(),420)
                clock.return_value=11.
                trail.sample()
                self.assertFalse(trail.timer.isActive())
                self.assertFalse(trail.isVisible())
                trail.begin()  # A queued, unchanged-position move must stay idle.
                self.assertFalse(trail.timer.isActive())
                self.assertGreaterEqual(hook_type.return_value.start.call_count,2)
                trail.set_enabled(False)
                self.assertFalse(trail.points)
                self.assertFalse(trail.enabled)
                self.assertGreaterEqual(hook_type.return_value.close.call_count,2)
            finally:
                trail.close_effect()


if __name__=='__main__': unittest.main()
