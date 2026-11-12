"""Render isolated appearance UI and original cursor illustrations, never a desktop."""
import os
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
os.environ['QT_QPA_PLATFORM']='windows:fontengine=freetype'
from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QFont, QImage, QPainter
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication
from still.app import Controller
from still.cursor_art import render
from still.cursor_outline import make_outline, paint_outline
from still.i18n import set_language
from still.model import Store
from still.ui import MainWindow


def detail(language,theme):
    dark=theme=='dark';zh=language=='zh'
    image=QImage(1200,460,QImage.Format.Format_ARGB32)
    image.fill(QColor('#132332' if dark else '#edf4f1'))
    painter=QPainter(image);painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    ink=QColor('#e6f4f1' if dark else '#203b3b')
    painter.setPen(ink);painter.setFont(QFont('Microsoft YaHei UI',22,QFont.Weight.DemiBold))
    painter.drawText(QRectF(42,28,1116,55),'光芒，沿着指针的轮廓流动。' if zh else 'A shimmer that follows the cursor.')
    painter.setFont(QFont('Microsoft YaHei UI',11))
    painter.drawText(QRectF(44,85,1116,35),'轮廓光示意放大 · 原创指针 · 不读取桌面画面' if zh else 'Enlarged outline illustration · Original cursors · No desktop capture')
    for n,(role,name) in enumerate((('normal','箭头' if zh else 'Arrow'),('text','文本' if zh else 'Text'),('hand','链接' if zh else 'Link'))):
        x=42+n*372
        painter.setPen(QColor('#3a5260' if dark else '#d1dfd9'))
        painter.setBrush(QColor('#203442' if dark else '#ffffff'))
        painter.drawRoundedRect(QRectF(x,145,346,258),20,20)
        cursor,hotspot=render('stream',role,112,color='#c4f3e3' if dark else '#3b6e62')
        outline=make_outline(cursor,QPointF(*hotspot),1.,'#73ebc5' if dark else '#16a17d')
        origin=QPointF(x+117,188)
        paint_outline(painter,outline,origin+QPointF(*hotspot),'#73ebc5' if dark else '#16a17d',1.16+n*.13,1.)
        painter.drawImage(origin,cursor)
        painter.setPen(ink);painter.setFont(QFont('Microsoft YaHei UI',14,QFont.Weight.DemiBold))
        painter.drawText(QRectF(x,343,346,36),Qt.AlignmentFlag.AlignCenter,name)
    painter.end()
    return image


def main():
    app=QApplication([]);app.setFont(QFont('Microsoft YaHei UI',9));app.setQuitOnLastWindowClosed(False)
    output=ROOT/'site/public/media'
    with patch('PySide6.QtWidgets.QSystemTrayIcon.show'),patch('PySide6.QtGui.QScreen.grabWindow',side_effect=AssertionError('No host desktop reads')):
        for language in ('zh','en'):
            set_language(language)
            for theme in ('light','dark'):
                with tempfile.TemporaryDirectory(prefix='qingkuai-appearance-demo-') as directory:
                    store=Store(Path(directory));store.settings.settings_language=language;store.settings.settings_theme=theme
                    store.settings.capture_hotkey=store.settings.restore_on_start=False
                    with patch('still.app.Controller.show_window'):
                        controller=Controller(app,store,check_updates=False)
                    window=controller.window=MainWindow(controller)
                    window.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen)
                    window.resize(1280,1050);window.show();window.pages.setCurrentIndex(1)
                    page=window.pages.widget(1)
                    page.pointer_section.set_expanded(True,animate=False)
                    app.processEvents();QTest.qWait(120)
                    assert window.grab().save(str(output/f'appearance-{language}-{theme}.webp'),'WEBP',90)
                    assert detail(language,theme).save(str(output/f'cursor-glow-{language}-{theme}.webp'),'WEBP',93)
                    controller.cleanup();window.close();app.processEvents()
    print('Rendered bilingual light/dark appearance settings and original outline illustrations.')


if __name__=='__main__':main()
