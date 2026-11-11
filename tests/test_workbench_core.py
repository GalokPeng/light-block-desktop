import json
from pathlib import Path
import tempfile
import unittest

from PySide6.QtGui import QColor, QImage, QPainter
from PySide6.QtCore import Qt

from still.capture import render_operations
from still.media_tools import parse_media_info, video_commands
from still.productivity_model import ToolsDB, RenameJournal, calculate, rename_preview
from still.scroll_capture import append_frame, estimate_scroll
from still.scroll_match import match_scroll


class WorkbenchCoreTests(unittest.TestCase):
    def test_annotation_is_lossless_and_source_is_unchanged(self):
        source = QImage(120, 80, QImage.Format.Format_RGB32)
        source.fill(QColor("#214365"))
        result = render_operations(source, [{"kind":"rect","color":"#ff0000","width":3,"points":[(10,10),(70,50)]},
                                             {"kind":"crop","rect":[5,5,90,60]}])
        self.assertEqual((result.width(),result.height()),(90,60))
        self.assertEqual(source.pixelColor(10,10),QColor("#214365"))
        self.assertNotEqual(result.pixelColor(5,5),source.pixelColor(10,10))

    def test_calculator_rejects_code_and_unbounded_exponents(self):
        self.assertEqual(calculate("24*60+3"),1443)
        self.assertEqual(calculate("-(9+3)/2"),-6)
        for text in ("__import__('os').system('x')", "9**999999", "[1]*9", "(1).__class__"):
            with self.assertRaises((ValueError,SyntaxError)):
                calculate(text)

    def test_rename_preview_collision_commit_and_undo(self):
        with tempfile.TemporaryDirectory(prefix="still-files-") as temp:
            root=Path(temp)
            a,b=root/"一.txt",root/"二.txt"
            a.write_bytes(b"A");b.write_bytes(b"B")
            pairs=rename_preview([a,b],"{n:03}_{name}{ext}")
            journal=RenameJournal(root/"config")
            record=journal.apply(pairs)
            self.assertFalse(a.exists())
            self.assertEqual((root/"001_一.txt").read_bytes(),b"A")
            journal.undo(record)
            self.assertEqual(a.read_bytes(),b"A")
            self.assertEqual(b.read_bytes(),b"B")
            with self.assertRaises(ValueError):rename_preview([a],"../oops")
            with self.assertRaises(FileExistsError):rename_preview([a,b],"same.txt")

    def test_clipboard_dedup_and_pins(self):
        with tempfile.TemporaryDirectory() as temp:
            database=ToolsDB(Path(temp))
            database.add_clip("text","hello")
            database.add_clip("text","hello")
            self.assertEqual(len(database.query("SELECT * FROM clipboard")),1)
            ident=database.query("SELECT id FROM clipboard")[0]["id"]
            database.pin_clip(ident)
            for n in range(100):database.add_clip("text",str(n))
            self.assertTrue(database.query("SELECT id FROM clipboard WHERE id=?",(ident,)))
            self.assertLessEqual(len(database.query("SELECT id FROM clipboard")),81)
            database.connection.close()

    def test_media_commands_are_two_pass_and_never_overwrite_source(self):
        commands=video_commands(Path("原素材.mp4"),Path("输出.gif"),1,3,(0,0,100,80),"gif",palette=Path("palette.png"))
        self.assertEqual(len(commands),2)
        self.assertIn("-n",commands[0])
        self.assertIn("palettegen", " ".join(commands[0]))
        self.assertIn("paletteuse", " ".join(commands[1]))
        self.assertNotIn("split", " ".join(commands[0]))
        self.assertIn("diff_mode=rectangle", " ".join(commands[1]))
        self.assertIn("fps=8:round=up", " ".join(commands[0]))
        marked = video_commands(Path("source.mp4"), Path("marked.gif"), 0, 1, (0,0,100,80), "gif",
                                palette=Path("palette.png"), overlay=Path("annotation.png"))
        self.assertIn("[marked]null", " ".join(marked[0]))
        self.assertNotIn("[marked],", " ".join(marked[0]))
        fine = video_commands(Path("source.mp4"), Path("fine.gif"), 0, 1, (0,0,100,80), "gif",
                              palette=Path("palette.png"), gif_fast=False)
        self.assertNotIn("round=up", " ".join(fine[0]))
        self.assertIn("dither=sierra2_4a", " ".join(fine[1]))
        frame = video_commands(Path("source.mp4"), Path("one.gif"), 1/24, 1/24, (0,0,100,80), "gif",
                               fps=24, palette=Path("palette.png"), frame_limit=1)
        self.assertEqual(frame[1][frame[1].index("-ss")+1], "0.041666")
        self.assertEqual(frame[1][frame[1].index("-frames:v")+1], "1")
        with self.assertRaises(ValueError):video_commands(Path("same.mp4"),Path("same.mp4"),0,2,(0,0,0,0),"mp4")
        info=parse_media_info("Duration: 00:01:02.50, start: 0\nStream #0:0: Video: h264, yuv420p, 1920x1080, 24fps")
        self.assertEqual(info,{"duration":62.5,"width":1920,"height":1080})

    def test_longshot_overlap_synthetic_content(self):
        source=QImage(180,600,QImage.Format.Format_RGB32)
        # Non-periodic deterministic row/text-like variation.
        for y in range(600):
            for x in range(180):
                value=(y*37+(y//17)*91+x*13+(x*y)%157)%256
                source.setPixelColor(x,y,QColor(value,(value*3)%256,(value*7)%256))
        first=source.copy(0,0,180,300)
        second=source.copy(0,140,180,300)
        shift=estimate_scroll(first,second)
        self.assertIsNotNone(shift)
        self.assertEqual(shift,140)
        result,added=append_frame(first,first,second,overlap=160)
        self.assertEqual(added,140)
        self.assertEqual(result.convertToFormat(QImage.Format.Format_RGB32),source.copy(0,0,180,440))
        self.assertEqual(estimate_scroll(first,first),0)

    def test_longshot_matches_with_sticky_chrome_and_a_repainting_column(self):
        width, height, header, footer = 420, 440, 35, 42
        content = QImage(width, 1250, QImage.Format.Format_RGB32)
        painter = QPainter(content)
        for row in range(content.height()):
            hue = (row * 17 + row * row // 107) % 359
            for column in range(4):
                painter.setPen(QColor.fromHsv(hue + column % 2, 180 + column * 8, 180))
                painter.drawLine(column * 105, row, (column + 1) * 105 - 1, row)
        painter.end()

        def frame(offset):
            image = QImage(width, height, QImage.Format.Format_RGB32)
            image.fill(QColor("#31415c"))
            painter = QPainter(image)
            painter.drawImage(0, header, content.copy(0, offset, width, height - header - footer))
            painter.fillRect(0, 0, width, 12, QColor("#4974a8"))
            painter.fillRect(0, height - 26, width, 26, QColor("#16222f"))
            painter.fillRect(365, 104, 36, 175, QColor.fromHsv(offset % 359, 245, 190))
            painter.end()
            return image

        first, second, third = frame(0), frame(130), frame(207)
        match = match_scroll(first, second)
        self.assertEqual((match.shift, match.footer), (130, footer))
        result, _ = append_frame(first, first, second)
        result, _ = append_frame(result, second, third)
        self.assertEqual(result.height(), height + 207)
        self.assertEqual(result.pixelColor(0, height - 8), content.pixelColor(0, height - 8 - header))
        self.assertEqual(result.copy(0, result.height() - footer, width, footer).convertToFormat(first.format()),
                         first.copy(0, height - footer, width, footer))
        self.assertIsNone(match_scroll(first, frame(760)))


if __name__=="__main__":unittest.main()
