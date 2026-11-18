"""Export web assets from the v2 native artwork. Run from any directory with Pillow."""
from pathlib import Path
import json
import re
import struct
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "site/public/desktop/art"

def cursor_frame(data, size=64):
    count = struct.unpack_from("<H", data, 4)[0]
    for index in range(count):
        w, h, _, _, x, y, length, offset = struct.unpack_from("<BBBBHHII", data, 6 + 16 * index)
        if (w, h) == (size, size):
            header = struct.unpack_from("<I", data, offset)[0]
            bgra = data[offset + header:offset + header + w*h*4]
            return Image.frombytes("RGBA", (w,h), bgra, "raw", "BGRA", 0, -1), [x,y]
    raise ValueError("Missing native cursor size")

def frames(data):
    pos = data.index(b"fram") + 4
    while data[pos:pos+4] == b"icon":
        count = struct.unpack_from("<I", data, pos+4)[0]
        yield data[pos+8:pos+8+count]
        pos += 8 + count + (count % 2)

def cursors():
    base = ROOT / "assets/cursors"
    manifest = json.loads((base / "manifest.json").read_text())
    for theme in manifest["themes"]:
        images, roles = [], {}
        for role in manifest["roles"]:
            source = base/theme
            blobs = list(frames((source/(role+"-64.ani")).read_bytes())) if role in manifest["animated"] else [(source/(role+".cur")).read_bytes()]
            for idx, blob in enumerate(blobs):
                frame, hot = cursor_frame(blob)
                if idx == 0:
                    roles[role] = [len(images), len(blobs), *hot]
                images.append(frame)
        sheet = Image.new("RGBA", (8*64, ((len(images)+7)//8)*64))
        for i, im in enumerate(images):
            sheet.paste(im, ((i%8)*64,(i//8)*64))
        sheet.save(OUT/(theme+".png"), optimize=True)
        (OUT/(theme+".json")).write_text(json.dumps(roles, separators=(",",":"))+"\n")
        (OUT/(theme+"-preview.png")).write_bytes((base/theme/"preview.png").read_bytes())

def icons():
    source = (ROOT/"ui/capture_style.hpp").read_text(encoding="utf8")
    source = source[source.index('if (key == "play")'):source.index("inline QIcon icon")]
    groups = re.split(r'(?:else )?if \(key == "([^"]+)"(?: \|\| key == "[^"]+")?\)',source)
    shapes = {}
    for key, body in zip(groups[1::2], groups[2::2]):
        pieces = []
        for coords in re.findall(r'line\(\{(.*?)\}\);',body,re.S):
            pairs = re.findall(r'\{([\d.]+),([\d.]+)\}',coords)
            pieces.append('<polyline points="'+' '.join(','.join(pair) for pair in pairs)+'"/>')
        for x,y,w,h,rx,ry in re.findall(r'drawRoundedRect\(QRectF\(([\d.]+),([\d.]+),([\d.]+),([\d.]+)\),([\d.]+),([\d.]+)\)',body):
            pieces.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" ry="{ry}"/>')
        for x,y,w,h in re.findall(r'drawEllipse\(QRectF\(([\d.]+),([\d.]+),([\d.]+),([\d.]+)\)\)',body):
            pieces.append(f'<ellipse cx="{float(x)+float(w)/2:g}" cy="{float(y)+float(h)/2:g}" rx="{float(w)/2:g}" ry="{float(h)/2:g}"/>')
        if pieces:
            shapes[key]=''.join(pieces)
    squares=''.join(f'<rect x="{x}" y="{y}" width="4" height="4"/>' for x,y in [(4,4),(12,4),(8,8),(16,8),(4,12),(12,12),(8,16),(16,16)])
    shapes["mosaic"]='<g fill="currentColor" stroke="none">'+squares+'</g>'
    shapes["mosaic_brush"]=shapes["mosaic"]+'<circle cx="6" cy="17" r="3" fill="currentColor"/>'
    shapes["stop"]='<rect x="6" y="6" width="12" height="12" rx="2" fill="currentColor"/>'
    (OUT/"capture-icons.mjs").write_text("// Generated from ui/capture_style.hpp by tools/web_demo_art.py\nexport const paths = "+json.dumps(shapes,indent=2)+";\n",encoding="utf8")

if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    cursors()
    icons()
