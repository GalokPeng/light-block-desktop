"""Interactive Windows switching test with isolated data and original desktop restoration.

Optional --extra-file paths exercise licensed media already downloaded locally.
No network requests and no screenshots of the user's desktop are made.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import time

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from PySide6.QtCore import QEventLoop,QTimer
from PySide6.QtWidgets import QApplication,QWidget
from still import win32
from still.engine import WallpaperEngine
from still.media_preview import MediaPreview
from still.media_process import MediaProcess
from still.model import Store
from still.static_wallpaper import StaticWallpaperTask
from still.video_thumbnails import VideoThumbnails


def pump(seconds):
    loop=QEventLoop();QTimer.singleShot(round(seconds*1000),loop.quit);loop.exec()


def until(predicate,seconds=45):
    end=time.monotonic()+seconds
    while not predicate() and time.monotonic()<end:pump(.02)
    assert predicate(),'Media did not settle before deadline'


def snapshot(worker):
    values=[]
    def arrived(value):
        if value.get('phase')=='snapshot':values.append(value)
    worker.message.connect(arrived)
    try:
        worker.control('snapshot');until(lambda:bool(values),5)
        return values[-1]
    finally:worker.message.disconnect(arrived)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extra-file',action='append',type=Path,default=[])
    parser.add_argument('--app',type=Path,help='Use a packaged executable for both media and static helpers')
    parser.add_argument('--cycles',type=int,choices=range(1,11),default=2)
    args=parser.parse_args()
    if args.app:
        executable=str(args.app.resolve(strict=True))
        MediaProcess.command=lambda self:(executable,['--media-player-helper'])
        StaticWallpaperTask.command=lambda self:(executable,['--static-wallpaper-helper'])
    app=QApplication([]);app.setQuitOnLastWindowClosed(False)
    original=win32.current_wallpaper()
    beats=[];timer=QTimer(interval=20);timer.timeout.connect(lambda:beats.append(time.perf_counter()));timer.start()
    report={'switches':0,'checks':[]}
    with tempfile.TemporaryDirectory(prefix='qingkuai-media-') as folder:
        store=Store(Path(folder));store.settings.fullscreen_pause=store.settings.battery_pause=False
        # Use the exact original path for restoration, not a temporary backup.
        store.settings.original_wallpaper=original
        engine=WallpaperEngine(store,app);errors=[];engine.error.connect(errors.append)
        covers=VideoThumbnails(store.directory)
        preview=MediaPreview()
        hashes={}
        items=[next(i for i in store.items if i['kind']==kind) for kind in ('video','gif')]
        for path in args.extra_file:
            path=path.resolve(strict=True)
            hashes[path]=hashlib.sha256(path.read_bytes()).hexdigest()
            items.append({'id':path.name,'name':path.name,'kind':'video','path':str(path),'builtin':False})
        try:
            for sound in (False,True):
                store.settings.wallpaper_sound=sound
                for item in items*args.cycles:
                    started=time.perf_counter();engine.apply(item)
                    assert time.perf_counter()-started<.15,'Apply blocked the controller'
                    until(lambda:not engine.busy)
                    assert engine.surface is not None and not errors,errors
                    assert win32.GetParent(engine.surface)==engine.worker
                    first=snapshot(engine.media);pump(.12);last=snapshot(engine.media)
                    assert last['frames']>first['frames']
                    if item['kind']=='gif' or not sound:
                        assert not last['audio_output'] and last['audio_track']==-1,last
                    engine.pause();pump(.15);paused=snapshot(engine.media)['position']
                    pump(.12);assert snapshot(engine.media)['position']==paused
                    engine.pause();pump(.1)
                    report['switches']+=1
                    # Dynamic -> Windows static -> dynamic, including audio teardown.
                    engine.apply(store.items[0]);until(lambda:not engine.busy)
                    assert engine.media.process is None and engine.state=='static'
                    report['switches']+=1
            report['checks'].append('Silent and audio-enabled video/GIF/static transitions, first frames and pause/resume')
            # Preview is also a native child with a separate decoder.
            preview.resize(480,270);preview.show();preview.play(store.path(items[0]))
            until(lambda:preview.worker.loaded)
            frame=snapshot(preview.worker)
            assert not frame['audio_output']
            preview.resize(640,360);pump(.15)
            preview.stop();until(lambda:preview.worker.process is None)
            report['checks'].append('Silent embedded preview, resize and asynchronous close')
            results=[];covers.ready.connect(lambda path,_image:results.append(path))
            for item in items:
                if item['kind']=='video':covers.get(store.path(item))
            until(lambda:not covers.pending and covers.active is None and covers.worker.process is None)
            assert results
            report['checks'].append('Serialized cached thumbnails without a controller-thread decoder')
            bad=Path(folder)/'broken.mp4';bad.write_bytes(b'not a video')
            engine.apply({'id':'broken','name':'broken','kind':'video','path':str(bad),'builtin':False})
            until(lambda:not engine.busy)
            assert errors and engine.surface is None
            report['checks'].append('Corrupt media falls back without leaving a process')
            assert all(hashlib.sha256(path.read_bytes()).hexdigest()==digest for path,digest in hashes.items())
        finally:
            preview.shutdown();preview.close();covers.close()
            until(lambda:not engine.busy)
            engine.restore_original();until(lambda:not engine.busy)
            engine.cleanup();timer.stop()
    assert win32.current_wallpaper()==original
    report['controller_heartbeat_max_ms']=round(max(b-a for a,b in zip(beats,beats[1:]))*1000,1)
    report['checks'].append('Original desktop restored; original media bytes unchanged')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
