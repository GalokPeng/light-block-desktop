"""Small, reviewed URL catalogs. No third-party media are bundled."""
from __future__ import annotations

import json
from pathlib import Path
import re
from urllib.parse import urlsplit
import uuid

from .i18n import display_language
from .model import ASSETS, media_kind

CATALOG_URL = "https://raw.githubusercontent.com/GalokPeng/light-block-desktop/main/assets/online-wallpapers.json"
SEED = ASSETS / "online-wallpapers.json"
MAX_CATALOG_BYTES = 4 * 1024 * 1024
MAX_MEDIA_BYTES = 1024 * 1024 * 1024
MAX_CURATED_BYTES = 20_000_000
MAX_PREVIEW_BYTES = 20_000_000
MEDIA_HOSTS = {"upload.wikimedia.org", "thumb.wikimedia.org", "archive.org"}
LICENSES = {"CC0", "Public domain", "CC BY 2.0", "CC BY 2.5", "CC BY 3.0", "CC BY 4.0",
            "CC BY-SA 2.0", "CC BY-SA 2.5", "CC BY-SA 3.0", "CC BY-SA 4.0"}


def https_url(value, hosts=None):
    if not isinstance(value, str) or len(value) > 2048 or any(ord(c) < 33 for c in value):
        return False
    try:
        parsed = urlsplit(value)
        return (parsed.scheme == "https" and bool(parsed.hostname) and not parsed.username
                and not parsed.password and parsed.port in (None, 443)
                and (hosts is None or parsed.hostname in hosts
                     or (hosts is MEDIA_HOSTS and re.fullmatch(r"(?:ia[0-9]+\.(?:us\.)?|dn[0-9]+\.(?:us|ca)\.)archive\.org", parsed.hostname))))
    except ValueError:
        return False


def catalog_version(catalog):
    return catalog["schema_version"], catalog.get("catalog_revision", 0)


def parse_catalog(data):
    if len(data) > MAX_CATALOG_BYTES:
        raise ValueError("Catalog too large")
    catalog = json.loads(data)
    if not isinstance(catalog, dict) or type(catalog.get("schema_version")) is not int or catalog["schema_version"] not in (1, 2, 3, 4):
        raise ValueError("Unsupported catalog")
    revision = catalog.get("catalog_revision", 0)
    if type(revision) is not int or not 0 <= revision <= 2147483647:
        raise ValueError("Invalid catalog revision")
    entries = catalog.get("entries")
    if not isinstance(entries, list) or len(entries) > 2000:
        raise ValueError("Invalid catalog entries")
    if catalog["schema_version"] >= 4 and any(sum(e.get("kind") == kind for e in entries if isinstance(e, dict)) > 20 for kind in ("image", "gif", "video")):
        raise ValueError("Curated catalog has more than 20 entries in one type")
    seen = set()
    for item in entries:
        if not isinstance(item, dict):
            raise ValueError("Invalid entry")
        if item.get("kind") not in {"image", "gif", "video"}:
            raise ValueError("Unsupported media type")
        ident = item.get("id", "")
        if not isinstance(ident, str) or not re.fullmatch(r"[a-z0-9][a-z0-9-]{2,95}", ident) or ident in seen:
            raise ValueError("Invalid or duplicate ID")
        seen.add(ident)
        if type(item.get("revision")) is not int or not 1 <= item["revision"] <= 1000000:
            raise ValueError("Invalid revision")
        title = item.get("title")
        if not isinstance(title, dict) or any(not isinstance(title.get(k), str) or not 0 < len(title[k]) <= 100 for k in ("zh", "en")):
            raise ValueError("Missing translated title")
        for key in ("filename", "author", "license", "changes", "reviewed_at"):
            if not isinstance(item.get(key), str) or not 0 < len(item[key]) <= 1000:
                raise ValueError("Missing source metadata")
        filename = item["filename"]
        if (len(filename) > 180 or any(c in filename for c in '/\\:\0<>"|?*')
                or any(ord(c) < 32 for c in filename) or filename.startswith('.') or filename.endswith((' ', '.'))
                or re.fullmatch(r"(?i)(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])", filename.split('.')[0])
                or media_kind(Path(filename)) != item.get("kind")):
            raise ValueError("Invalid media filename")
        if item["license"] not in LICENSES:
            raise ValueError("Unreviewed license")
        for key in ("thumbnail_url", "download_url"):
            if not https_url(item.get(key), MEDIA_HOSTS):
                raise ValueError("Unreviewed media host")
        for key in ("source_url", "author_url", "license_url"):
            if not https_url(item.get(key)):
                raise ValueError("Invalid attribution URL")
        if type(item.get("bytes")) is not int or not 1 <= item["bytes"] <= MAX_MEDIA_BYTES:
            raise ValueError("Invalid file size")
        available = False
        for key, length in (("sha256", 64), ("sha1", 40)):
            if key in item:
                if not isinstance(item[key], str) or not re.fullmatch(r"[0-9a-f]{"+str(length)+"}", item[key]):
                    raise ValueError("Invalid source checksum")
                available = True
        if not available:
            raise ValueError("Missing file checksum")
        if "search_text" in item and (not isinstance(item["search_text"], str) or len(item["search_text"]) > 500):
            raise ValueError("Invalid search metadata")
        for key in ("width", "height"):
            if type(item.get(key)) is not int or not 1 <= item[key] <= 32768:
                raise ValueError("Invalid dimensions")
        if catalog["schema_version"] >= 3:
            if item["width"] < 1920 or item["height"] < 1080:
                raise ValueError("Original media must be at least 1920 by 1080")
            if "categories" in item:
                raise ValueError("Content categories were retired")
            popularity = item.get("popularity")
            if not isinstance(popularity, dict) or not https_url(popularity.get("evidence_url")):
                raise ValueError("Missing popularity evidence")
            for field in ("views", "likes", "favorites", "votes"):
                value = popularity.get(field)
                if value is not None and (type(value) is not int or not 0 <= value <= 10**12):
                    raise ValueError("Invalid popularity count")
            if item["kind"] != "gif" and not any((popularity.get(field) or 0) > 0 for field in ("likes", "favorites", "votes")):
                raise ValueError("Views alone do not establish community approval")
            if not isinstance(popularity.get("checked_at"), str) or not re.fullmatch(r"\d{4}-\d{2}-\d{2}", popularity["checked_at"]):
                raise ValueError("Missing popularity review date")
        if catalog["schema_version"] >= 4 and item["bytes"] > MAX_CURATED_BYTES:
            raise ValueError("Curated originals must not exceed 20 MB")
        if catalog["schema_version"] >= 4 and item["kind"] == "gif":
            # GIFs may have no public likes/favorites; preserve unknown metrics as
            # null while requiring a verified, complete 16:9 looping animation.
            if item["width"] * 9 != item["height"] * 16:
                raise ValueError("Curated GIFs must have a 16:9 aspect ratio")
            if type(item.get("frame_count")) is not int or not 2 <= item["frame_count"] <= 10000:
                raise ValueError("Curated GIFs must contain multiple verified frames")
            if item.get("looped") is not True:
                raise ValueError("Curated GIFs must loop")
            if (type(item.get("duration_seconds")) not in (int, float)
                    or not 0 < item["duration_seconds"] <= 120):
                raise ValueError("Invalid GIF animation duration")
        preview = item.get("preview")
        if preview is not None:
            if (item["kind"] != "video" or not isinstance(preview, dict)
                    or not https_url(preview.get("download_url"), MEDIA_HOSTS)
                    or type(preview.get("bytes")) is not int or not 1 <= preview["bytes"] <= 64 * 1024 * 1024
                    or not isinstance(preview.get("filename"), str) or not re.fullmatch(r"[a-z0-9-]+\.(mp4|webm|ogv)", preview["filename"])
                    or not isinstance(preview.get("sha1"), str) or not re.fullmatch(r"[0-9a-f]{40}", preview["sha1"])):
                raise ValueError("Invalid independent preview")
            if catalog["schema_version"] >= 4 and preview["bytes"] > MAX_CURATED_BYTES:
                raise ValueError("Curated previews must not exceed 20 MB")
        if not isinstance(item.get("tags"), list) or len(item["tags"]) > 40 or any(not isinstance(t, str) or len(t) > 80 for t in item["tags"]):
            raise ValueError("Invalid tags")
    return catalog


def title_for(entry):
    return entry["title"][display_language()]


def checksum_for(entry):
    return ("sha256", entry["sha256"]) if entry.get("sha256") else ("sha1", entry["sha1"])


def checksum_key(entry):
    algorithm, value = checksum_for(entry)
    return value if algorithm == "sha256" else "sha1-" + value


def local_id(entry):
    return uuid.uuid5(uuid.NAMESPACE_URL, "qingkuai:" + entry["id"] + ":" + str(entry["revision"]) + ":" + checksum_key(entry)).hex


def media_signature(prefix, kind):
    image = (prefix.startswith((b"\x89PNG\r\n\x1a\n", b"\xff\xd8\xff", b"BM", b"II*\0", b"MM\0*"))
             or (prefix.startswith(b"RIFF") and prefix[8:12] == b"WEBP"))
    if kind == "cover":
        return image or prefix.startswith((b"GIF87a", b"GIF89a"))
    if kind == "image":
        return image
    if kind == "gif":
        return prefix.startswith((b"GIF87a", b"GIF89a"))
    if kind == "video":
        return (prefix.startswith((b"\x1aE\xdf\xa3", b"OggS")) or prefix[4:8] in (b"ftyp", b"moov", b"mdat", b"wide")
                or prefix.startswith(b"\x30\x26\xb2\x75\x8e\x66\xcf\x11\xa6\xd9\x00\xaa\x00\x62\xce\x6c")
                or (prefix.startswith(b"RIFF") and prefix[8:12] == b"AVI "))
    return False
