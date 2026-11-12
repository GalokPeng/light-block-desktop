"""Original Qt renderer hosted outside the controller; stdin EOF exits the worker."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import sys
import threading

from PySide6.QtCore import QObject, Qt, QTimer, QUrl, Signal
from PySide6.QtWidgets import QApplication

from . import win32
from .i18n import set_language, tr
from .taskbar_style_native import pipe_stream


class Worker(QObject):
    command=Signal(object)

    def __init__(self,app,writer):
        super().__init__(app)
        self.app,self.writer=app,writer
        self.player=self.surface=self.audio=self.sink=None
        self.request=None
        self.ready=self.quitting=False
        self.frame_count=0
        self.frame_pending=False
        self.command.connect(self.handle,Qt.ConnectionType.QueuedConnection)

    def reply(self,message):
        try:
            self.writer.write(json.dumps(message,ensure_ascii=True).encode('ascii')+b'\n')
            self.writer.flush()
        except OSError:self.app.quit()

    def handle(self,message):
        if self.quitting:return
        try:
            op=message['op']
            if op=='load':self.load(message);return
            if op=='quit':
                self.quitting=True;QTimer.singleShot(0,self.dispose);return
            if self.player is None:raise ValueError('Player not loaded')
            if op=='pause':self.player.pause() if message['paused'] else self.player.play()
            elif op=='sound':self.set_sound(bool(message['enabled']))
            elif op=='geometry':self.position(message['rect'])
            elif op=='snapshot':
                frame=self.sink.videoFrame()
                image=frame.toImage().scaled(32,18)
                self.reply({'phase':'snapshot','frames':self.frame_count,'position':self.player.position(),
                    'width':frame.width(),'height':frame.height(),
                    'audio_output':self.audio is not None,'audio_track':self.player.activeAudioTrack(),
                    'digest':hashlib.sha256(bytes(image.constBits())).hexdigest() if not image.isNull() else ''})
            elif op!='ping':raise ValueError('Invalid media command')
            self.reply({'phase':'ack','op':op})
        except Exception as exc:self.reply({'phase':'error','error':str(exc)[:2048]})

    def load(self,request):
        if self.player is not None:raise ValueError('One source per media worker')
        set_language(request.get('language','system'))
        path=Path(request['path'])
        if not path.is_file():raise FileNotFoundError(tr('原文件已移动或删除，请重新导入。'))
        if request.get('mode') not in ('desktop','preview','thumbnail'):raise ValueError('Invalid media mode')
        self.request=request
        from PySide6.QtMultimedia import QMediaPlayer,QVideoSink
        self.player=QMediaPlayer(self)
        if request['mode']=='thumbnail':
            self.sink=QVideoSink(self)
            self.player.setVideoSink(self.sink)
        else:
            from PySide6.QtMultimediaWidgets import QVideoWidget
            self.surface=QVideoWidget(None)
            self.surface.setWindowFlags(Qt.WindowType.FramelessWindowHint|Qt.WindowType.Tool|Qt.WindowType.WindowDoesNotAcceptFocus)
            self.surface.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
            self.surface.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
            self.surface.setAspectRatioMode({'fill':Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                'fit':Qt.AspectRatioMode.KeepAspectRatio,'center':Qt.AspectRatioMode.KeepAspectRatio,
                'stretch':Qt.AspectRatioMode.IgnoreAspectRatio}[request.get('fit','fit')])
            self.parent_hwnd=win32.desktop_worker() if request['mode']=='desktop' else int(request['parent'])
            if not win32.IsWindow(self.parent_hwnd):raise OSError(tr('无法挂接桌面层。'))
            self.surface.resize(request['rect'][2],request['rect'][3])
            win32.attach(int(self.surface.winId()),self.parent_hwnd,tuple(request['rect']))
            self.surface.hide()
            self.player.setVideoOutput(self.surface)
            self.player.setLoops(QMediaPlayer.Loops.Infinite)
            self.sink=self.surface.videoSink()
        self.sink.videoFrameChanged.connect(self.frame)
        self.player.errorOccurred.connect(lambda _code,text:self.reply({'phase':'error','error':text}))
        self.player.mediaStatusChanged.connect(self.status)
        self.player.tracksChanged.connect(self.tracks_changed)
        self.set_sound(bool(request.get('sound')) and request['mode']=='desktop')
        self.player.setSource(QUrl.fromLocalFile(str(path)))
        self.player.play()

    def status(self,status):
        if status==self.player.MediaStatus.LoadedMedia and not self.player.hasVideo():
            self.reply({'phase':'error','error':tr('所选文件没有视频轨道。')})

    def set_sound(self,enabled):
        self.sound=enabled
        if enabled:
            if self.audio is None:
                from PySide6.QtMultimedia import QAudioOutput
                self.audio=QAudioOutput(self)
                self.player.setAudioOutput(self.audio)
            self.audio.setMuted(False)
        elif self.audio is not None:
            # Muting avoids hot-unplugging a live audio sink. The entire worker
            # is later retired, so audio teardown can never block the controller.
            self.audio.setMuted(True)
        self.tracks_changed()

    def tracks_changed(self):
        # Leave the backend's native signal stack before changing active tracks.
        QTimer.singleShot(0,self.select_tracks)

    def select_tracks(self):
        if self.quitting or self.player is None:return
        desired=0 if self.sound and self.player.audioTracks() else -1
        if self.player.activeAudioTrack()!=desired:self.player.setActiveAudioTrack(desired)

    def frame(self,frame):
        if self.quitting or not frame.isValid():return
        self.frame_count+=1
        if not self.ready and not self.frame_pending:
            self.frame_pending=True
            QTimer.singleShot(0,self.first_frame)

    def first_frame(self):
        self.frame_pending=False
        if self.quitting or self.ready:return
        frame=self.sink.videoFrame()
        if not frame.isValid():return
        mode=self.request['mode']
        if mode=='thumbnail':
            target=min(400,self.player.duration()//8)*1000
            if 0<=frame.startTime()<target:return
            image=frame.toImage()
            if image.isNull():return
            image=image.scaled(720,480,Qt.AspectRatioMode.KeepAspectRatio,Qt.TransformationMode.SmoothTransformation)
            target=Path(self.request['output']);target.parent.mkdir(parents=True,exist_ok=True)
            temporary=target.with_suffix('.tmp')
            try:
                if not image.save(str(temporary),'JPEG',86):raise OSError('Cannot save video cover')
                temporary.replace(target)
            finally:temporary.unlink(missing_ok=True)
            self.ready=True
            self.reply({'phase':'ready','output':str(target),'width':frame.width(),'height':frame.height()})
            QTimer.singleShot(0,lambda:self.handle({'op':'quit'}))
            return
        if not win32.IsWindow(self.parent_hwnd):
            self.reply({'phase':'error','error':tr('无法挂接桌面层。')})
            return
        self.surface.show()
        self.position(self.request['rect'])
        self.ready=True
        self.reply({'phase':'ready','handle':int(self.surface.winId()),'worker':self.parent_hwnd,
                    'width':frame.width(),'height':frame.height(),'has_audio':bool(self.player.audioTracks()),
                    'audio_output':self.audio is not None})

    def position(self,rect):
        if self.surface:
            if not win32.IsWindow(self.parent_hwnd):raise OSError(tr('无法挂接桌面层。'))
            win32.position(int(self.surface.winId()),self.parent_hwnd,tuple(rect))

    def dispose(self):
        if self.surface:self.surface.hide()
        if self.player:
            self.player.blockSignals(True)
            self.player.stop()
            self.player.setSource(QUrl())
            self.player.setVideoOutput(None)
            self.player.setAudioOutput(None)
        self.app.quit()


def run_helper():
    os.environ.setdefault('QT_QPA_PLATFORM','windows:fontengine=freetype')
    app=QApplication(sys.argv[:1]);app.setQuitOnLastWindowClosed(False)
    reader,writer=pipe_stream(sys.stdin,-10,'rb'),pipe_stream(sys.stdout,-11,'wb')
    worker=Worker(app,writer)
    def read():
        try:
            while True:
                line=reader.readline(262145)
                if not line:break
                if len(line)>262144:break
                message=json.loads(line)
                if not isinstance(message,dict):break
                worker.command.emit(message)
        except (OSError,ValueError):pass
        worker.command.emit({'op':'quit'})
    threading.Thread(target=read,daemon=True,name='media-commands').start()
    return app.exec()
