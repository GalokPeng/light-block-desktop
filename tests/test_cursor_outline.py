import ctypes as C
from ctypes import wintypes as W
import unittest
from unittest.mock import patch

from PySide6.QtCore import QPointF, Qt
from PySide6.QtGui import QColor, QImage, QPainter
from PySide6.QtWidgets import QApplication

from still import win32
from still.cursor_art import render
from still.cursor_outline import CursorOutlineCache, cursor_mask, make_outline, paint_outline
from still.model import ASSETS


class CursorOutlineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app=QApplication.instance() or QApplication([])

    def test_outer_band_leaves_cursor_body_clear_and_follows_hotspot_and_dpi(self):
        for role in ('normal','text','hand'):
            mask,hotspot=render('stream',role,48)
            outline=make_outline(mask,QPointF(*hotspot),1.5,'#7ee8bc')
            padding=round(outline.hotspot.x()*1.5-hotspot[0])
            self.assertEqual(outline.glow.devicePixelRatio(),1.5)
            self.assertGreater(outline.rim.width(),mask.width())
            visible=0
            for y in range(mask.height()):
                for x in range(mask.width()):
                    if mask.pixelColor(x,y).alpha()==255:
                        self.assertEqual(outline.rim.pixelColor(x+padding,y+padding).alpha(),0)
                        self.assertEqual(outline.glow.pixelColor(x+padding,y+padding).alpha(),0)
                    visible += outline.rim.pixelColor(x+padding,y+padding).alpha()>0
            self.assertGreater(visible,5)
            image=QImage(100,100,QImage.Format.Format_ARGB32);image.fill(Qt.GlobalColor.transparent)
            painter=QPainter(image)
            paint_outline(painter,outline,QPointF(20,20),QColor('#7ee8bc'),.5,1.)
            painter.end()
            self.assertTrue(any(bytes(image.constBits())[3::4]))

    def test_native_cursor_bitmaps_are_copied_and_gdi_resources_are_released(self):
        load=win32.bind(win32.user32,'LoadImageW',W.HANDLE,W.HINSTANCE,W.LPCWSTR,W.UINT,C.c_int,C.c_int,W.UINT)
        destroy=win32.bind(win32.user32,'DestroyCursor',W.BOOL,W.HANDLE)
        resources=win32.bind(win32.user32,'GetGuiResources',W.DWORD,W.HANDLE,W.DWORD)
        process=win32.bind(win32.kernel32,'GetCurrentProcess',W.HANDLE)()
        for role in ('normal','text','hand'):
            handle=load(None,str(ASSETS/'cursors/outline'/(role+'.cur')),2,48,48,16)
            self.assertTrue(handle)
            try:
                before=resources(process,0)
                for _ in range(20):
                    image,hotspot=cursor_mask(handle)
                    self.assertEqual((image.width(),image.height()),(48,48))
                    self.assertLess(hotspot.x(),48);self.assertLess(hotspot.y(),48)
                self.assertLessEqual(resources(process,0),before+1)
                # A different consumer may bind the shared user32 function to
                # its own ctypes structure. Our dedicated DLL remains isolated.
                foreign=win32.user32.GetIconInfo
                previous=foreign.argtypes
                try:
                    foreign.argtypes=[W.HANDLE,C.POINTER(W.DWORD)]
                    self.assertIsNotNone(cursor_mask(handle))
                finally:
                    foreign.argtypes=previous
            finally:
                destroy(handle)
            self.assertTrue(any(bytes(image.constBits())[3::4]))

    def test_cursor_shape_cache_is_bounded_and_avoids_native_bitmap_reads_per_frame(self):
        image,hotspot=render('pixel','normal',32)
        current=[123]
        def read_cursor(pointer):
            pointer._obj.flags=1;pointer._obj.cursor=current[0]
            return True
        cache=CursorOutlineCache()
        with patch('still.cursor_outline.get_cursor',side_effect=read_cursor),patch('still.cursor_outline.cursor_mask',return_value=(image,QPointF(*hotspot))) as read:
            first=cache.current(1.,'#7ee8bc')
            for _ in range(30):self.assertIs(cache.current(1.,'#7ee8bc'),first)
            self.assertEqual(read.call_count,1)
            for n in range(20):
                current[0]=200+n;cache.current(1.,'#7ee8bc')
            self.assertEqual(len(cache.entries),16)
            cache.clear();self.assertFalse(cache.entries)
