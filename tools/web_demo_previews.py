"""Build small browser previews of the licensed catalog. Originals stay at their source."""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import urllib.request

from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "site/public/desktop"
CATALOG = ROOT / "assets/online-wallpapers-v2.json"
FIELDS = ("id","title","kind","download_url","bytes","sha1","author","source_url","license","license_url","thumbnail_url","width","height","tags")


def build(entry, caches, output, fetch):
    key = entry["id"]
    cover = output / (key + ".webp")
    movie = output / (key + ".mp4")
    motion = entry["kind"] != "image"
    if not cover.exists() or motion and not movie.exists():
        with tempfile.TemporaryDirectory() as temporary:
            original = None
            for path in caches.get(entry["bytes"], []):
                if hashlib.sha1(path.read_bytes()).hexdigest() == entry["sha1"]:
                    original = path
                    break
            if original is None:
                if not fetch:
                    return False
                original = Path(temporary) / "source"
                request = urllib.request.Request(entry["download_url"], headers={"User-Agent": "LightBlockDesktop-Preview/2.0.6 (github.com/GalokPeng/light-block-desktop)"})
                with urllib.request.urlopen(request, timeout=15) as response:
                    data = response.read(entry["bytes"]+1)
                if len(data)!=entry["bytes"] or hashlib.sha1(data).hexdigest()!=entry["sha1"]:
                    raise ValueError("Source did not match reviewed catalog")
                original.write_bytes(data)
            if motion:
                # Keep audio out of autoplaying previews. Even WMV sources play in browsers.
                temp_video = Path(temporary)/"preview.mp4"
                subprocess.run(["ffmpeg","-hide_banner","-loglevel","error","-nostdin","-i",str(original),"-t","5","-an","-vf","fps=14,scale=480:270:force_original_aspect_ratio=decrease:force_divisible_by=2","-c:v","libx264","-threads","2","-pix_fmt","yuv420p","-crf","27","-movflags","+faststart","-y",str(temp_video)],check=True,timeout=55)
                subprocess.run(["ffmpeg","-hide_banner","-loglevel","error","-nostdin","-i",str(temp_video),"-frames:v","1","-threads","1","-y",str(cover)],check=True,timeout=12)
                movie.write_bytes(temp_video.read_bytes())
            else:
                with Image.open(original) as frame:
                    image=ImageOps.exif_transpose(frame).convert("RGB")
                    image.thumbnail((800,480),Image.Resampling.LANCZOS)
                    image.save(cover,quality=80,method=5)
    return True


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache",action="append",type=Path,default=[])
    parser.add_argument("--fetch",action="store_true",help="Fetch missing approved originals from the catalog")
    args=parser.parse_args()
    entries=json.loads(CATALOG.read_text(encoding="utf-8"))["entries"]
    sizes={e["bytes"] for e in entries}
    lookup={}
    for directory in args.cache:
        for path in directory.rglob("*"):
            if path.is_file() and path.stat().st_size in sizes:
                lookup.setdefault(path.stat().st_size,[]).append(path)
    output=PUBLIC/"previews"
    output.mkdir(exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        jobs={executor.submit(build,e,lookup,output,args.fetch):e for e in entries}
        for job in concurrent.futures.as_completed(jobs):
            try:
                job.result()
            except Exception as err:
                print(jobs[job]["id"], str(err), flush=True)
    result=[]
    for e in entries:
        item={k:e[k] for k in FIELDS}
        key=e["id"]
        if (output/(key+".webp")).is_file():
            item["cover"]="./previews/"+key+".webp"
            if e["kind"]=="image":
                item["preview"]=item["cover"]
            elif (output/(key+".mp4")).is_file():
                item["preview"]="./previews/"+key+".mp4"
        result.append(item)
    (PUBLIC/"catalog.json").write_text(json.dumps(result,ensure_ascii=False,separators=(",",":"))+"\n",encoding="utf-8")
    print(f"{sum('preview' in e for e in result)}/{len(result)} local preview renditions",flush=True)


if __name__=="__main__":
    main()
