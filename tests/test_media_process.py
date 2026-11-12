"""Media lifecycle fault injection, isolated from the real Windows desktop."""
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from PySide6.QtCore import QEventLoop, QProcess, QTimer
from PySide6.QtWidgets import QApplication, QMessageBox
from still import win32
from still.app import Controller
from still.engine import WallpaperEngine
from still.media_process import MediaProcess
from still.model import Store

FIXTURE='''import json,sys,time
request=json.loads(sys.stdin.readline())
mode=sys.argv[1]
hold=open(request['path'],'rb')
if mode=='hang-load':time.sleep(60)
if mode=='slow':time.sleep(.25)
if mode=='crash-load':sys.exit(7)
if mode=='invalid':print('invalid protocol',flush=True);time.sleep(60)
if mode=='bad-ready':print('{"phase":"ready"}',flush=True);time.sleep(60)
print(json.dumps(dict(phase='ready',handle=123,worker=456,width=1920,height=1080)),flush=True)
if mode=='crash-play':time.sleep(.15);sys.exit(9)
for line in sys.stdin:
 message=json.loads(line)
 if message['op']=='quit':
  if mode=='hang-quit':time.sleep(60)
  break
 print(json.dumps(dict(phase='ack',op=message['op'])),flush=True)
'''


def until(predicate,timeout=5):
    end=time.monotonic()+timeout
    while not predicate() and time.monotonic()<end:
        loop=QEventLoop();QTimer.singleShot(10,loop.quit);loop.exec()
    if not predicate():raise AssertionError('Media operation did not settle')


def running(pid):
    handle=win32.bind(win32.kernel32,'OpenProcess',W.HANDLE,W.DWORD,W.BOOL,W.DWORD)(0x1000,False,pid)
    if not handle:return False
    try:
        code=W.DWORD()
        return bool(win32.bind(win32.kernel32,'GetExitCodeProcess',W.BOOL,W.HANDLE,C.POINTER(W.DWORD))(handle,C.byref(code)) and code.value==259)
    finally:win32.bind(win32.kernel32,'CloseHandle',W.BOOL,W.HANDLE)(handle)


class MediaProcessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app=QApplication.instance() or QApplication([])
        cls.app.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
        self.store=Store(self.root/'profile')
        self.script=self.root/'worker.py';self.script.write_text(FIXTURE,encoding='utf-8')
        self.path=self.root/'source.mp4';self.path.write_bytes(b'original media fixture')
        self.item={'id':'fixture','name':'Fixture','path':str(self.path),'kind':'video','builtin':False}
        self.engine=WallpaperEngine(self.store,self.app)
        self.errors=[];self.applied=[];self.restored=[]
        self.engine.error.connect(self.errors.append)
        self.engine.applied.connect(self.applied.append)
        self.engine.restored.connect(lambda:self.restored.append(True))
        self.patches=[patch('still.engine.win32.target_monitor',return_value={'rect':(0,0,1920,1080)}),
                      patch('still.engine.win32.IsWindow',return_value=True),
                      patch('still.engine.win32.desktop_locked',return_value=False),
                      patch('still.engine.win32.fullscreen_on',return_value=False),
                      patch('still.engine.win32.on_battery',return_value=False)]
        for p in self.patches:p.start()

    def tearDown(self):
        self.engine.cleanup()
        for p in reversed(self.patches):p.stop()
        self.temp.cleanup()

    def command(self,mode):
        return patch.object(self.engine.media,'command',return_value=(sys.executable,[str(self.script),mode]))

    def test_first_frame_ack_precedes_persistence_and_busy_blocks_duplicate_apply(self):
        old=self.store.items[0];self.engine.active=old;self.store.settings.active_id=old['id']
        with self.command('slow'):
            self.engine.apply(self.item)
            self.assertTrue(self.engine.busy)
            self.assertEqual(self.store.settings.active_id,old['id'])
            self.assertFalse(self.applied)
            with self.assertRaises(RuntimeError):self.engine.apply(self.item)
            until(lambda:not self.engine.busy)
        self.assertEqual(self.store.settings.active_id,self.item['id'])
        self.assertEqual(self.applied,[self.item])
        self.assertFalse(self.errors)

    def test_loading_timeout_keeps_event_loop_alive_and_reaps_process(self):
        beats=[];timer=QTimer(interval=10);timer.timeout.connect(lambda:beats.append(time.monotonic()));timer.start()
        try:
            self.engine.media.deadline.setInterval(180)
            self.engine.media.stop_timer.setInterval(80)
            with self.command('hang-load'):
                self.engine.apply(self.item)
                until(lambda:bool(self.errors) and self.engine.media.process is None)
            self.assertGreater(len(beats),8)
            self.assertLess(max(b-a for a,b in zip(beats,beats[1:])),.15)
            self.assertFalse(self.store.settings.active_id)
            self.assertIsNone(self.engine.surface)
        finally:timer.stop()

    def test_stuck_old_decoder_is_terminated_before_next_one_starts(self):
        self.engine.media.stop_timer.setInterval(80)
        with self.command('hang-quit'):
            self.engine.apply(self.item);until(lambda:not self.engine.busy)
            first=int(self.engine.media.process.processId())
        with self.command('normal'):
            second=dict(self.item,id='second')
            self.engine.apply(second)
            until(lambda:not self.engine.busy)
        self.assertFalse(running(first))
        self.assertEqual(self.applied[-1]['id'],'second')
        self.assertFalse(self.errors)

    def test_stop_during_load_ignores_late_ready(self):
        with self.command('slow'):
            self.engine.apply(self.item)
            self.engine.stop()
            until(lambda:self.engine.media.process is None)
        self.assertFalse(self.applied)
        self.assertFalse(self.store.settings.active_id)

    def test_cancel_from_ready_state_callback_does_not_commit_a_stale_success(self):
        self.engine.changed.connect(lambda:self.engine.stop() if self.engine.surface is not None else None)
        with self.command('normal'):
            self.engine.apply(self.item)
            until(lambda:self.engine.media.process is None)
        self.assertFalse(self.applied)
        self.assertFalse(self.store.settings.active_id)

    def test_restore_signal_waits_until_file_handle_is_closed(self):
        self.engine.media.stop_timer.setInterval(100)
        with self.command('hang-quit'):
            self.engine.apply(self.item);until(lambda:not self.engine.busy)
            self.engine.restore_original()
            self.assertFalse(self.restored)
            until(lambda:bool(self.restored))
        self.assertIsNone(self.engine.media.process)
        self.path.unlink()
        self.assertFalse(self.path.exists())

    def test_unexpected_exit_clears_active_wallpaper_and_allows_retry(self):
        with self.command('crash-play'):
            self.engine.apply(self.item)
            until(lambda:bool(self.errors) and self.engine.media.process is None)
        self.assertIsNone(self.engine.surface)
        self.assertFalse(self.store.settings.active_id)
        with self.command('normal'):
            self.engine.apply(self.item);until(lambda:not self.engine.busy)
        self.assertIsNotNone(self.engine.surface)

    def test_invalid_protocol_is_bounded_and_preserves_previous_static_selection(self):
        old=self.store.items[0];self.engine.active=old;self.store.settings.active_id=old['id']
        self.engine.media.stop_timer.setInterval(80)
        for mode in ('invalid','bad-ready'):
            self.errors.clear()
            with self.command(mode):
                self.engine.apply(self.item)
                until(lambda:bool(self.errors) and self.engine.media.process is None)
        self.assertEqual(self.store.settings.active_id,old['id'])
        self.assertIsNone(self.engine.surface)

    def test_failed_start_is_retryable_and_does_not_leak_a_job(self):
        with patch.object(self.engine.media,'command',return_value=(str(self.root/'missing.exe'),[])):
            self.engine.apply(self.item)
            until(lambda:bool(self.errors) and self.engine.media.process is None)
        self.assertIsNone(self.engine.media.job)
        with self.command('normal'):
            self.engine.apply(self.item);until(lambda:not self.engine.busy)

    def test_audio_enable_requires_confirmation_and_cancel_keeps_silent(self):
        controller=SimpleNamespace(store=self.store,engine=Mock(),window=Mock(),report_error=Mock())
        with patch('still.app.QMessageBox.question',return_value=QMessageBox.StandardButton.No) as ask:
            Controller.set_wallpaper_sound(controller,True)
            ask.assert_called_once()
        self.assertFalse(self.store.settings.wallpaper_sound)
        controller.engine.sync_sound.assert_not_called()
        with patch('still.app.QMessageBox.question',return_value=QMessageBox.StandardButton.Yes):
            Controller.set_wallpaper_sound(controller,True)
        self.assertTrue(self.store.settings.wallpaper_sound)
        with patch('still.app.QMessageBox.question') as ask:
            Controller.set_wallpaper_sound(controller,False)
            ask.assert_not_called()

    def test_audio_changes_retire_old_worker_before_reopening_with_new_track_policy(self):
        with self.command('normal'):
            self.engine.apply(self.item);until(lambda:not self.engine.busy)
            old=int(self.engine.media.process.processId())
            self.store.settings.wallpaper_sound=True
            self.engine.sync_sound();until(lambda:not self.engine.busy)
            self.assertTrue(self.engine.media.request['sound'])
            self.assertFalse(running(old))
            old=int(self.engine.media.process.processId())
            self.store.settings.wallpaper_sound=False
            self.engine.sync_sound();until(lambda:not self.engine.busy)
            self.assertFalse(self.engine.media.request['sound'])
            self.assertFalse(running(old))

    def test_windows_job_reaps_worker_when_controller_crashes(self):
        parent_code='''import sys,json
from PySide6.QtCore import QCoreApplication,QTimer
from still.media_process import MediaProcess
app=QCoreApplication([]);worker=MediaProcess()
worker.command=lambda:(sys.executable,[sys.argv[1],'hang-load'])
worker.start({'mode':'desktop','path':sys.argv[2]})
def report():
 if worker.job:print(worker.process.processId(),flush=True)
 else:QTimer.singleShot(10,report)
QTimer.singleShot(10,report);app.exec()
'''
        parent=QProcess()
        parent.start(sys.executable,['-c',parent_code,str(self.script),str(self.path)])
        try:
            until(parent.canReadLine)
            pid=int(bytes(parent.readLine()).strip())
            self.assertTrue(running(pid))
            parent.kill();parent.waitForFinished(1000)
            until(lambda:not running(pid))
        finally:
            if parent.state()!=QProcess.ProcessState.NotRunning:parent.kill();parent.waitForFinished(1000)

    def test_applying_waits_for_preview_retirement_before_starting_desktop_decoder(self):
        preview=MediaProcess()
        try:
            preview.stop_timer.setInterval(100)
            with patch.object(preview,'command',return_value=(sys.executable,[str(self.script),'hang-quit'])):
                preview.start({'mode':'preview','path':str(self.path)})
                until(lambda:preview.loaded)
            old=int(preview.process.processId())
            with self.command('normal'):
                self.engine.apply(self.item,wait_for=preview)
                self.assertTrue(self.engine.busy)
                self.assertIsNone(self.engine.media.process)
                until(lambda:not self.engine.busy)
            self.assertFalse(running(old))
            self.assertEqual(self.applied,[self.item])
        finally:preview.shutdown()

    def test_cancel_queued_apply_does_not_restart_after_preview_exits(self):
        preview=MediaProcess()
        try:
            preview.stop_timer.setInterval(100)
            with patch.object(preview,'command',return_value=(sys.executable,[str(self.script),'hang-quit'])):
                preview.start({'mode':'preview','path':str(self.path)})
                until(lambda:preview.loaded)
            self.engine.apply(self.item,wait_for=preview)
            self.engine.stop()
            until(lambda:preview.process is None)
            self.assertFalse(self.applied)
            self.assertIsNone(self.engine.media.process)
            self.assertFalse(self.engine.busy)
        finally:preview.shutdown()


if __name__=='__main__':unittest.main()
