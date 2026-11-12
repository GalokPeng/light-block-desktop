"""Opt-in Windows cursor integration; always restores the registered scheme.

Run explicitly on an interactive Windows desktop after build_pointer_host.ps1.
This is separate from the default tests because it temporarily changes cursors.
"""
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from PySide6.QtCore import QEventLoop, QProcess, QTimer
from PySide6.QtGui import QCursor
from PySide6.QtWidgets import QApplication
from still import win32
from still.model import Store
from still.pointer_style import PointerStyle, pointer_host


def signature():
    class IconInfo(C.Structure):
        _fields_=[('icon',W.BOOL),('x',W.DWORD),('y',W.DWORD),('mask',W.HBITMAP),('color',W.HBITMAP)]
    class Bitmap(C.Structure):
        _fields_=[('type',W.LONG),('width',W.LONG),('height',W.LONG),('stride',W.LONG),
                  ('planes',W.WORD),('bits',W.WORD),('data',C.c_void_p)]
    load=win32.bind(win32.user32,'LoadCursorW',W.HANDLE,W.HINSTANCE,C.c_void_p)
    info=IconInfo()
    assert win32.bind(win32.user32,'GetIconInfo',W.BOOL,W.HANDLE,C.POINTER(IconInfo))(load(None,32512),C.byref(info))
    gdi=C.WinDLL('gdi32')
    get=win32.bind(gdi,'GetObjectW',C.c_int,W.HANDLE,C.c_int,C.c_void_p)
    bits=win32.bind(gdi,'GetBitmapBits',W.LONG,W.HBITMAP,W.LONG,C.c_void_p)
    delete=win32.bind(gdi,'DeleteObject',W.BOOL,W.HANDLE)
    try:
        bitmap=Bitmap();handle=info.color or info.mask
        assert get(handle,C.sizeof(bitmap),C.byref(bitmap))
        data=C.create_string_buffer(bitmap.stride*bitmap.height)
        assert bits(handle,len(data),data)
        return hashlib.sha256(data.raw).hexdigest()
    finally:
        if info.color:delete(info.color)
        if info.mask:delete(info.mask)


def read(process):
    deadline=time.monotonic()+5
    while not process.canReadLine() and time.monotonic()<deadline:
        process.waitForReadyRead(100)
    assert process.canReadLine(), 'Cursor helper did not reply'
    return bytes(process.readLine()).strip()


def idle_measurement(pid):
    """Measure the separate native process without adding a runtime dependency."""
    class Memory(C.Structure):
        _fields_=[('cb',W.DWORD),('faults',W.DWORD)]+[(key,C.c_size_t) for key in
            ('peak_working_set','working_set','peak_paged','paged','peak_nonpaged','nonpaged','pagefile','peak_pagefile')]
    handle=win32.bind(win32.kernel32,'OpenProcess',W.HANDLE,W.DWORD,W.BOOL,W.DWORD)(0x410,False,pid)
    assert handle
    times=win32.bind(win32.kernel32,'GetProcessTimes',W.BOOL,W.HANDLE,*([C.POINTER(W.FILETIME)]*4))
    def cpu():
        fields=[W.FILETIME() for _ in range(4)]
        assert times(handle,*[C.byref(value) for value in fields])
        return sum((value.dwHighDateTime<<32)+value.dwLowDateTime for value in fields[2:])/1e7
    try:
        memory=Memory();memory.cb=C.sizeof(memory)
        assert win32.bind(win32.kernel32,'K32GetProcessMemoryInfo',W.BOOL,W.HANDLE,C.POINTER(Memory),W.DWORD)(handle,C.byref(memory),memory.cb)
        start=time.perf_counter();before=cpu()
        time.sleep(2)
        return {'idle_working_set_mb':round(memory.working_set/1024**2,2),
                'idle_cpu_one_core_percent':round((cpu()-before)/(time.perf_counter()-start)*100,3)}
    finally:
        win32.bind(win32.kernel32,'CloseHandle',W.BOOL,W.HANDLE)(handle)


def check_qt_service(app, before):
    def pump(seconds):
        loop=QEventLoop();QTimer.singleShot(round(seconds*1000),loop.quit);loop.exec()
    def wait(condition):
        deadline=time.monotonic()+8
        while not condition() and time.monotonic()<deadline:pump(.02)
        assert condition(), 'Cursor service did not settle'
    position=QCursor.pos()
    with tempfile.TemporaryDirectory(prefix='qingkuai-pointer-') as directory:
        store=Store(Path(directory))
        service=PointerStyle(SimpleNamespace(app=app,store=store))
        errors=[];service.error.connect(errors.append)
        try:
            for theme in ('stream','outline','pixel','system','stream'):
                service.set_theme(theme)
                wait(lambda:service.pending is None and service.actual_theme==theme)
                if theme=='system':wait(lambda:service.process is None)
                assert Store(store.directory).settings.cursor_theme==theme
            original_particle=store.settings.particle_color
            service.set_color('#f092b3')
            wait(lambda:service.pending is None and store.settings.cursor_color=='#f092b3')
            assert store.settings.particle_color==original_particle
            for percent in (75,150,200,100):
                service.set_size(percent)
                wait(lambda:service.pending is None and store.settings.cursor_scale==percent)
                assert Store(store.directory).settings.cursor_scale==percent
            assert not errors,errors
            service.process.kill()  # Simulate unexpected native helper termination.
            wait(lambda:service.process is None)
            wait(lambda:signature()==before)
            assert errors and Store(store.directory).settings.cursor_theme=='system'
            for effect in ('dots','fall','glow'):
                service.set_particle_options(style=effect,color='#76adff')
                service.set_particles(True)
                assert service.trail.hook.handle and not service.trail.timer.isActive()
                win32.bind(win32.user32,'mouse_event',None,W.DWORD,W.DWORD,W.DWORD,W.DWORD,C.c_size_t)(1,20,0,0,0)
                pump(.15)
                assert service.trail.timer.isActive()
                assert win32.GetWindowLong(int(service.trail.winId()),-20)&0x20
                with patch('still.pointer_style.QCursor.pos',return_value=QCursor.pos()):
                    pump(1.4)
                    assert not service.trail.timer.isActive() and not service.trail.isVisible(),effect
            service.set_particles(False);pump(.02)
            assert service.trail is None and not service.active
        finally:
            service.cleanup();QCursor.setPos(position);pump(.15)
    assert signature()==before


def main():
    app=QApplication([])
    app.setQuitOnLastWindowClosed(False)
    helper=pointer_host()
    assert helper, 'Build the cursor helper first'
    assert subprocess.run([str(helper),'--probe'],capture_output=True,timeout=5).stdout==b'READY\n'
    before=signature()
    process=QProcess()
    process.start(str(helper),[])
    assert process.waitForStarted(3000)
    report={'checks':[], 'native_helper_bytes':helper.stat().st_size}
    try:
        fingerprints=set()
        for theme in ('stream','outline','pixel'):
            process.write(('APPLY\t'+str(ROOT/'assets/cursors'/theme)+'\n').encode('utf-8'))
            assert read(process)==b'OK', theme
            fingerprints.add(signature())
        assert len(fingerprints)==3
        report['checks'].append('Three distinct themes apply through native Windows cursors')
        sizes=set()
        for percent in (75,100,150,200):
            process.write(('APPLY\t'+str(ROOT/'assets/cursors/stream')+'\t'+str(percent)+'\n').encode('utf-8'))
            assert read(process)==b'OK'
            sizes.add(signature())
        assert len(sizes)==4,'Cursor scale did not alter the native cursor'
        owned=signature()
        for invalid in ('74','201','abc','100x'):
            process.write(('APPLY\t'+str(ROOT/'assets/cursors/stream')+'\t'+invalid+'\n').encode('utf-8'))
            assert read(process).startswith(b'ERROR') and signature()==owned
        report['checks'].append('Four cursor sizes change native output; invalid sizes preserve the applied scheme')
        report.update(idle_measurement(int(process.processId())))
        owned=signature()
        conflict=subprocess.run([str(helper)],input=b'QUIT\n',capture_output=True,timeout=5)
        assert conflict.stdout==b'BUSY\n' and signature()==owned
        report['checks'].append('Second instance cannot replace the active theme')
        process.write(b'APPLY\tC:\\missing-theme\n')
        assert read(process).startswith(b'ERROR') and signature()==owned
        report['checks'].append('Missing assets leave the active theme unchanged')
        process.write(b'RESTORE\n')
        assert read(process)==b'OK' and signature()==before
        process.write(('APPLY\t'+str(ROOT/'assets/cursors/stream')+'\n').encode('utf-8'))
        assert read(process)==b'OK'
        process.closeWriteChannel()  # Same EOF path as a parent process crash.
        assert process.waitForFinished(5000) and signature()==before
        report['checks'].append('Explicit restore and parent pipe EOF both restore the original scheme')
    finally:
        if process.state()!=QProcess.ProcessState.NotRunning:
            process.write(b'QUIT\n');process.closeWriteChannel()
            if not process.waitForFinished(5000):process.kill();process.waitForFinished(1000)
        subprocess.run([str(helper),'--restore'],capture_output=True,timeout=5)
    assert signature()==before
    check_qt_service(app,before)
    report['checks'].append('Qt theme persistence, helper crash recovery and real mouse trail wake/idle/release')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
