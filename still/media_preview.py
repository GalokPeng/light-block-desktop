"""Native child-window preview whose media backend cannot block its Qt parent."""
from PySide6.QtCore import QPoint, QTimer, Signal
from PySide6.QtWidgets import QApplication, QWidget
from .media_process import MediaProcess


class MediaPreview(QWidget):
    failed=Signal()

    def __init__(self,parent=None):
        super().__init__(parent)
        self.worker=MediaProcess(QApplication.instance())
        self.worker.error.connect(self.on_error)
        self.worker.ready.connect(self.on_ready)
        self.resize_timer=QTimer(self,singleShot=True,interval=50)
        self.resize_timer.timeout.connect(self.geometry_changed)
        self.path=None

    def on_error(self,_text):
        self.failed.emit()

    def on_ready(self,_result):
        self.geometry_changed()

    def rect_global(self):
        origin=self.mapToGlobal(QPoint())
        return [origin.x(),origin.y(),max(1,self.width()),max(1,self.height())]

    def play(self,path):
        self.path=str(path)
        self.worker.start({'mode':'preview','path':self.path,'parent':int(self.winId()),
                           'rect':self.rect_global(),'fit':'fit','sound':False})

    def geometry_changed(self):
        self.worker.control('geometry',rect=self.rect_global())

    def resizeEvent(self,event):
        self.resize_timer.start()
        super().resizeEvent(event)

    def stop(self):
        self.path=None
        self.resize_timer.stop()
        self.worker.stop()

    def closeEvent(self,event):
        self.stop()
        if self.worker.process is not None:
            self.worker.finished.connect(self.worker.deleteLater)
        else:self.worker.deleteLater()
        self.deleteLater()
        super().closeEvent(event)

    def shutdown(self):
        self.stop();self.worker.shutdown()
