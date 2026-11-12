"""Asynchronous, supervised media workers. No decoder lives on the settings thread."""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
import json
import logging
from pathlib import Path
import sys
from shiboken6 import isValid

from PySide6.QtCore import QObject, QProcess, QProcessEnvironment, QTimer, Signal
from PySide6.QtWidgets import QApplication

from . import win32
from .i18n import display_language, tr

close_handle=win32.bind(win32.kernel32,'CloseHandle',W.BOOL,W.HANDLE)


class ProcessJob:
    """Windows closes this job on parent death, including a stuck decoder."""
    def __init__(self, pid):
        class Basic(C.Structure):
            _fields_=[('user',C.c_int64),('job',C.c_int64),('flags',W.DWORD),
                ('minimum',C.c_size_t),('maximum',C.c_size_t),('active',W.DWORD),
                ('affinity',C.c_size_t),('priority',W.DWORD),('scheduling',W.DWORD)]
        class IO(C.Structure):
            _fields_=[(name,C.c_uint64) for name in ('read','write','other','read_bytes','write_bytes','other_bytes')]
        class Extended(C.Structure):
            _fields_=[('basic',Basic),('io',IO),('process_memory',C.c_size_t),('job_memory',C.c_size_t),
                      ('peak_process',C.c_size_t),('peak_job',C.c_size_t)]
        self.handle=None
        job=win32.bind(win32.kernel32,'CreateJobObjectW',W.HANDLE,C.c_void_p,W.LPCWSTR)(None,None)
        if not job:raise C.WinError(C.get_last_error())
        self.handle=job
        process=None
        try:
            limits=Extended();limits.basic.flags=0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE.
            if not win32.bind(win32.kernel32,'SetInformationJobObject',W.BOOL,W.HANDLE,C.c_int,C.c_void_p,W.DWORD)(job,9,C.byref(limits),C.sizeof(limits)):
                raise C.WinError(C.get_last_error())
            process=win32.bind(win32.kernel32,'OpenProcess',W.HANDLE,W.DWORD,W.BOOL,W.DWORD)(0x101,False,pid)
            if not process or not win32.bind(win32.kernel32,'AssignProcessToJobObject',W.BOOL,W.HANDLE,W.HANDLE)(job,process):
                raise C.WinError(C.get_last_error())
        except OSError:
            self.close();raise
        finally:
            if process:close_handle(process)

    def close(self):
        if self.handle:
            close_handle(self.handle);self.handle=None


class MediaProcess(QObject):
    ready=Signal(dict)
    message=Signal(dict)
    error=Signal(str)
    finished=Signal()

    def __init__(self,parent=None):
        super().__init__(parent)
        self.process=self.job=None
        self.request=self.pending=None
        self.buffer=b''
        self.loaded=self.stopping=self.failed=False
        self.deadline=QTimer(self,singleShot=True,interval=15000)
        self.deadline.timeout.connect(lambda:self.fail(tr('媒体播放器响应超时，已停止该壁纸。')))
        self.stop_timer=QTimer(self,singleShot=True,interval=2000)
        self.stop_timer.timeout.connect(self.force_stop)
        self.watch=QTimer(self,interval=5000)
        self.watch.timeout.connect(self.ping)
        if QApplication.instance():QApplication.instance().aboutToQuit.connect(self.shutdown)

    @property
    def busy(self):
        return self.pending is not None or (self.process is not None and (self.stopping or not self.loaded))

    def command(self):
        args=['--media-player-helper']
        if not getattr(sys,'frozen',False):args.insert(0,str(Path(__file__).resolve().parents[1]/'main.py'))
        return sys.executable,args

    def start(self,request):
        self.pending=dict(request,language=display_language())
        if self.process is not None:
            self.stop(cancel_pending=False)
        else:self.launch()

    def launch(self):
        if self.pending is None:return
        self.request,self.pending=self.pending,None
        self.buffer=b''
        self.loaded=self.stopping=self.failed=False
        process=QProcess(self);self.process=process
        environment=QProcessEnvironment.systemEnvironment()
        environment.insert('PYINSTALLER_RESET_ENVIRONMENT','1')
        process.setProcessEnvironment(environment)
        process.started.connect(lambda p=process:self.started(p))
        process.readyReadStandardOutput.connect(lambda p=process:self.read(p))
        process.readyReadStandardError.connect(lambda p=process:logging.info('Media worker: %s',
            bytes(p.readAllStandardError()).decode('utf-8',errors='replace')[-2048:].strip()))
        process.finished.connect(lambda code,_status,p=process:self.ended(p,code))
        process.errorOccurred.connect(lambda error,p=process:self.process_error(p,error))
        self.deadline.start()
        logging.info('Media worker starting mode=%s',self.request['mode'])
        process.start(*self.command())

    def started(self,process):
        if process is not self.process:return
        try:self.job=ProcessJob(int(process.processId()))
        except OSError as exc:
            logging.error('Cannot supervise media worker: %s',exc)
            self.fail(tr('无法创建受保护的媒体进程。'));return
        self.send(dict(self.request,op='load'))

    def send(self,message):
        if self.process is not None and self.process.state()==QProcess.ProcessState.Running:
            self.process.write(json.dumps(message,ensure_ascii=True).encode('ascii')+b'\n')

    def control(self,op,**values):
        if self.process is None or self.stopping:return
        if not self.deadline.isActive():self.deadline.start()
        self.send(dict(op=op,**values))

    def ping(self):
        self.control('ping')

    def read(self,process):
        if process is not self.process:return
        self.buffer+=bytes(process.readAllStandardOutput())
        if len(self.buffer)>262144:
            self.fail(tr('媒体进程返回了无效数据。'));return
        while b'\n' in self.buffer:
            line,self.buffer=self.buffer.split(b'\n',1)
            try:message=json.loads(line)
            except (ValueError,TypeError):
                self.fail(tr('媒体进程返回了无效数据。'));return
            if not isinstance(message,dict):self.fail(tr('媒体进程返回了无效数据。'));return
            if self.stopping:continue
            phase=message.get('phase')
            if phase=='ready':
                if self.loaded:continue
                mode=self.request['mode']
                fields=('width','height') if mode=='thumbnail' else ('handle','worker','width','height')
                if any(type(message.get(key)) is not int or message[key]<=0 for key in fields):
                    self.fail(tr('媒体进程返回了无效数据。'));return
                self.loaded=True;self.deadline.stop()
                if self.request['mode']!='thumbnail':self.watch.start()
                self.ready.emit(message)
                if not isValid(self) or self.process is not process or self.stopping:return
            elif phase=='ack':
                if self.loaded:self.deadline.stop()
            elif phase=='error':self.fail(str(message.get('error',''))[:2048]);return
            self.message.emit(message)

    def fail(self,message):
        if self.failed or self.stopping:return
        self.failed=True
        logging.warning('Media worker failed: %s',message)
        self.stop()
        self.error.emit(message)

    def process_error(self,process,error):
        if process is not self.process or self.stopping:return
        if error==QProcess.ProcessError.FailedToStart:
            self.fail(tr('无法启动媒体播放器。'))
            self.ended(process,-1)

    def stop(self,*,cancel_pending=True):
        if cancel_pending:self.pending=None
        self.deadline.stop();self.watch.stop()
        if self.process is None or self.stopping:return
        self.stopping=True
        self.send({'op':'quit'})
        self.process.closeWriteChannel()
        self.stop_timer.start()

    def force_stop(self):
        if self.process is not None and self.process.state()!=QProcess.ProcessState.NotRunning:
            logging.warning('Media worker did not exit; terminating isolated decoder pid=%s',self.process.processId())
            self.process.kill()
            if self.job:self.job.close()

    def ended(self,process,code):
        if process is not self.process:return
        # QProcess can finish while Qt tears down its owner's children. Python
        # callbacks may still exist after the sibling timers were destroyed.
        if not isValid(self) or not all(isValid(timer) for timer in (self.deadline,self.stop_timer,self.watch)):
            self.process=self.pending=None
            if self.job:self.job.close();self.job=None
            return
        unexpected=not self.stopping and (not self.loaded or self.request['mode']!='thumbnail')
        self.deadline.stop();self.stop_timer.stop();self.watch.stop()
        self.process=None
        if self.job:self.job.close();self.job=None
        self.loaded=self.stopping=False
        logging.info('Media worker exited code=%s',code)
        process.deleteLater()
        if unexpected and not self.failed:
            self.failed=True;self.error.emit(tr('媒体播放器意外退出，已回到 Windows 桌面。'))
        if self.pending is not None:self.launch()
        else:self.finished.emit()

    def shutdown(self):
        self.stop()
        process=self.process
        if process is not None and process.state()!=QProcess.ProcessState.NotRunning:
            if not process.waitForFinished(600):
                self.force_stop();process.waitForFinished(300)
        if self.job:self.job.close();self.job=None
