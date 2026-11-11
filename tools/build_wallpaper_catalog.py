"""Generate credits from the manually reviewed catalog; never auto-fill a quota."""
import argparse
from collections import Counter
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from still.online_catalog import SEED, parse_catalog


def write_credits(catalog):
    entries = catalog["entries"]
    counts = Counter(e["kind"] for e in entries)
    lines = ["# 在线壁纸来源 / Online wallpaper credits", "",
        f"当前精选：静态 {counts['image']}、GIF {counts['gif']}、视频 {counts['video']}。旧 300 项目录已全部退下，不再按内容题材分组。", "",
        f"Current selection: {counts['image']} images, {counts['gif']} GIFs and {counts['video']} videos. The former 300-item catalog was retired. Only the three media types remain.", "",
        "所有入选原文件宽度至少 1920、高度至少 1080，且不超过 20,000,000 字节（20 MB），不通过放大或软件转码凑数量。GitHub 与安装包只提供 URL 和署名元数据；正式下载保留发布者提供文件的原始字节。", "",
        "Every source file is at least 1920 pixels wide and 1080 pixels high, and no larger than 20,000,000 bytes (20 MB). No upscaling or app-side transcoding is used to meet the quota. GitHub and the installer bundle URL/attribution metadata only; downloads preserve the publisher-provided bytes.", "",
        "## 授权与热度评估 / License and popularity review", "",
        "- 静图以 Wikimedia Commons 2024 年度图片公开投票中的横向作品为候选，先检查授权、像素和画面，再在通过人工观感筛选的候选中按支持票降序选择 20 张。支持票不是点赞数或收藏数。",
        "- Images come from landscape-format candidates in the public Wikimedia Commons Picture of the Year 2024 results. License, dimensions and visual suitability are checked before selecting 20 by support votes within the visually approved pool. Votes are not likes or favorites.",
        "- 视频包含 7 段 Jeffrey Beach / Beachfront Productions 在 Internet Archive 公开上传的 1080p WMV，以及 13 段 Wikimedia Commons 评选视频；均在 20 MB 内。前者核对 CC BY 3.0、公开收藏与浏览，后者核对逐项许可、评选支持票及近 30 天文件页浏览。",
        "- Videos include seven creator-uploaded 1080p WMV files by Jeffrey Beach / Beachfront Productions on Internet Archive, and thirteen Wikimedia Commons reviewed videos, all within 20 MB. The first pool records CC BY 3.0, favorites and views; the second records per-work licenses, closed support votes and 30-day file-page views.",
        "- 视频分别在同来源、同口径候选内按收藏或支持票排序，再结合画面合并，不把收藏数与评选票直接相加。沿用发布者提供的 WMV、WebM、OGV 格式，不自行压缩、裁短或改分辨率。",
        "- Videos are ranked within each source/metric pool, then combined after visual review; favorites and votes are never added together. Publisher-provided WMV, WebM and OGV files are preserved without app-side recompression, trimming or resolution changes.",
        "- 未公开的指标在目录里记为 null，不记成零、不推算点赞、不声称全网绝对排名；不同平台、不同统计期间的数值不能直接比较。热度是核对日期的快照，后续会变化。",
        "- Unavailable metrics are null, not zero or inferred likes. This is a reviewed candidate selection, not a claim of a worldwide top ranking. Different platforms and time windows are not directly comparable. Counts are dated snapshots and can change.",
        f"- GIF 不以点赞／收藏作为入选门槛；维护者确认恢复之前复看的候选，当前录入 {counts['gif']} 项；原始 1080p、16:9、20 MB 和明确开放授权的要求保持不变。",
        f"- GIFs have no likes/favorites threshold. The maintainer restored the previously reviewed batch; {counts['gif']} GIFs are currently admitted. Native 1080p, 16:9, 20 MB and explicit reusable licensing remain required.",
        "- GIF 浏览量为官方 API 在所列日期区间内已公开日期的合计；未公开的日期不作零次处理，点赞／收藏缺失时为 null。应用提示实际有统计数据的天数。",
        "- GIF views sum only dates exposed by the official API within the stated window; unavailable dates are not treated as zero, and unpublished likes/favorites remain null. The app reports the number of dates with available counts.", "",
        "软件代码的 MIT 许可不替代素材自身许可。详情页、下载文件旁的 origin.json 和图库记录均保留作者、来源、许可与热度依据；修改、再分发时遵循对应署名与 ShareAlike 等要求。",
        "The app's MIT license does not replace the media licenses. Details, adjacent origin.json and library records retain author, source, license and popularity evidence. Follow applicable attribution and ShareAlike terms when modifying or redistributing works.", "",
        "[评估范围与排除理由 / Evaluation scope](wallpaper-evaluation.md) · [Wikimedia 复用指南](https://commons.wikimedia.org/wiki/Commons:Reusing_content_outside_Wikimedia) · [Internet Archive](https://archive.org/)", "",
        "## 逐项记录 / Individual records", "",
        "下表由 tools/build_wallpaper_catalog.py --credits-only 从当前目录生成。热度核对日期 / Metrics checked: " + max(e["popularity"]["checked_at"] for e in entries) + ".", "",
        "| 类型 / Type | 作品 / Work | 作者 / Author | 授权 / License | 热度快照 / Popularity snapshot |", "| --- | --- | --- | --- | --- |"]
    clean = lambda value: str(value).replace("|", "&#124;").replace("[", "&#91;").replace("]", "&#93;").replace("\n", " ")
    for entry in entries:
        metrics = entry["popularity"]
        stats = " / ".join(str(metrics[k])+" "+k for k in ("votes", "views", "favorites", "likes") if metrics.get(k) is not None)
        lines.append(f'| {entry["kind"]} | [{clean(entry["title"]["zh"])} / {clean(entry["title"]["en"])}]({entry["source_url"]}) | [{clean(entry["author"])}]({entry["author_url"]}) | [{entry["license"]}]({entry["license_url"]}) | [{stats}]({metrics["evidence_url"]}) |')
    (ROOT/"docs/wallpaper-sources.md").write_text("\n".join(lines)+"\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--credits-only", action="store_true", help="Regenerate credit records without network requests")
    parser.parse_args()
    write_credits(parse_catalog(SEED.read_bytes()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
