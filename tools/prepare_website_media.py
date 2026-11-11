"""Publish local demo media; never read a user's desktop or profile."""
from hashlib import sha256
import json
from pathlib import Path
import shutil

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
MEDIA = ROOT / "site/public/media"
WALLPAPERS = (
    ("night-drive", "01_夜驰_城郊高速_1920x1080.jpg"),
    ("milky-way", "02_银河_静夜_1920x1080.jpg"),
    ("mountain-bay", "03_山海_晨光_1920x1080.jpg"),
    ("rain-coast", "04_海岸_雨夜弯道_1920x1080.jpg"),
)


def taskbar_diagram(lang, theme):
    dark = theme == "dark"
    ink, muted = ("#e8eef9", "#a9b7ce") if dark else ("#25354c", "#64748b")
    bg, panel, line = ("#101824", "#202d42", "#42546e") if dark else ("#f0f4fa", "#ffffff", "#cdd8e7")
    title = "原生任务栏 · 三段居中" if lang == "zh" else "Native Windows taskbar · Three centered sections"
    labels = ("电源 · 设置 · 任务管理器 · 搜索", "应用", "系统托盘") if lang == "zh" else ("Power · Settings · Task Manager · Search", "Apps", "System tray")
    search = "搜索" if lang == "zh" else "Search"
    parts = [f'''<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="212" viewBox="0 0 1200 212" role="img" aria-label="{title}">
<rect width="1200" height="212" rx="18" fill="{bg}"/>
<g font-family="Segoe UI,Microsoft YaHei UI,Arial,sans-serif" fill="{ink}">
<text x="36" y="40" font-size="18" font-weight="600">{title}</text>''']
    for x, width in ((130,280),(418,360),(786,286)):
        parts.append(f'<rect x="{x}" y="76" width="{width}" height="70" rx="20" fill="{panel}" stroke="{line}"/>')
    parts.append(f'''<g stroke="{ink}" stroke-width="2" fill="none" stroke-linecap="round" stroke-linejoin="round">
<path d="M151 103a11 11 0 1 0 14 0M158 97v12"/>
<circle cx="194" cy="111" r="8"/><circle cx="194" cy="111" r="3"/>
<path d="M194 99v3m0 18v3m-12-12h3m18 0h3m-21-9 2 2m14 14 2 2m-18 0 2-2m14-14 2-2"/>
<circle cx="270" cy="109" r="7"/><path d="m275 114 5 5"/>
</g><g fill="{ink}"><rect x="222" y="111" width="3" height="10" rx="1"/><rect x="228" y="101" width="3" height="20" rx="1"/><rect x="234" y="106" width="3" height="15" rx="1"/></g>
<text x="290" y="117" font-size="17" fill="{muted}">{search}</text>''')
    for x, color in zip((450,508,566,624,682,740),("#e6ad43","#3d8ed9","#6d7bce","#48a997","#5c9bd8","#c0809e")):
        parts.append(f'<rect x="{x-16}" y="94" width="32" height="32" rx="9" fill="{color}"/><circle cx="{x}" cy="135" r="2" fill="{ink}"/>')
    parts.append(f'''<g stroke="{ink}" stroke-width="1.8" fill="none" stroke-linecap="round"><path d="m806 112 5-5 5 5"/><circle cx="840" cy="111" r="7"/><rect x="861" y="104" width="13" height="13" rx="2"/><rect x="902" y="103" width="17" height="12" rx="2"/><path d="M910 115v5m-4 0h8m23-12h4l5-4v15l-5-4h-4Z"/></g>
<text x="883" y="117" font-size="15">{'中' if lang=='zh' else 'EN'}</text>
<text x="1049" y="107" text-anchor="end" font-size="15">09:41</text><text x="1049" y="128" text-anchor="end" font-size="12" fill="{muted}">2026/11/11</text>
<g fill="{muted}" font-size="13"><text x="130" y="181">{labels[0]}</text><text x="418" y="181">{labels[1]}</text><text x="786" y="181">{labels[2]}</text></g></g></svg>''')
    return "\n".join(parts)+"\n"


def main():
    target = MEDIA / "wallpapers"
    target.mkdir(parents=True, exist_ok=True)
    origins=[]
    for name, original in WALLPAPERS:
        source=ROOT/"assets"/(name+".jpg")
        with Image.open(source) as image:
            assert image.size==(1920,1080)
            metadata = image.getexif()
            assert set(metadata).issubset({270, 305, 37510}), "Review unexpected image metadata before publishing"
            assert not metadata or str(metadata.get(305, "")).startswith("OpenAI image_gen"), "Review image provenance"
            image.resize((960,540),Image.Resampling.LANCZOS).save(target/(name+".webp"),"WEBP",quality=88,method=6)
        shutil.copyfile(source,target/(name+".jpg"))
        origins.append({"filename":source.name,"provided_filename":original,"width":1920,"height":1080,
                        "bytes":source.stat().st_size,"sha256":sha256(source.read_bytes()).hexdigest()})
    (ROOT/"assets/wallpaper-origins.json").write_text(json.dumps({"source":"Project maintainer supplied these files for distribution.",
        "app_files":"Original JPEG bytes preserved.","wallpapers":origins},ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    for lang in ("zh","en"):
        for theme in ("light","dark"):
            (MEDIA/f"taskbar-layout-{lang}-{theme}.svg").write_text(taskbar_diagram(lang,theme),encoding="utf-8")
        for name in ("wallpaper","online-wallpaper","organizer","capture","recording","notes"):
            source=MEDIA/f"{name}-{lang}-light.webp"
            if source.exists(): shutil.copyfile(source,MEDIA/f"{name}-{lang}.webp")
    shutil.copyfile(MEDIA/"taskbar-layout-zh-light.svg",MEDIA/"taskbar-layout.svg")
    print("Prepared original wallpapers, web previews, source hashes and taskbar diagrams.")


if __name__=="__main__":
    main()
