"""Validate the reviewed URL catalog; optionally verify original files online."""
from hashlib import new as new_hash
import argparse
from collections import Counter
from pathlib import Path
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from still.model import VERSION
from still.online_catalog import (SEED, checksum_for, checksum_key,
    https_url, MEDIA_HOSTS, parse_catalog, media_signature)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-downloads", action="store_true", help="Stream originals to verify size and their recorded checksum; no files are stored.")
    parser.add_argument("--limit", type=int, help="Limit optional network checks; metadata checks always cover the whole catalog.")
    parser.add_argument("--minimum-per-kind", type=int, default=0)
    args = parser.parse_args()
    catalog = parse_catalog(SEED.read_bytes())
    counts = Counter(entry["kind"] for entry in catalog["entries"])
    if any(counts[kind] < args.minimum_per_kind for kind in ("image", "gif", "video")):
        raise ValueError("Catalog is below the required number per media type")
    if catalog["schema_version"] >= 3 and (counts["image"] != 20 or counts["video"] != 20 or counts["gif"] > 20):
        raise ValueError("Curated catalog requires 20 images, 20 videos and at most 20 qualified GIFs")
    keys = [checksum_key(e) for e in catalog["entries"]]
    urls = [e["download_url"] for e in catalog["entries"]]
    if len(set(keys)) != len(keys) or len(set(urls)) != len(urls):
        raise ValueError("Duplicate original files in catalog")
    for index, entry in enumerate(catalog["entries"]):
        algorithm, expected = checksum_for(entry)
        if args.verify_downloads and (args.limit is None or index < args.limit):
            request = urllib.request.Request(entry["download_url"], headers={
                "User-Agent": f"LightBlockDesktop/{VERSION} (https://github.com/GalokPeng/light-block-desktop; catalog verification)",
                "Accept-Encoding": "identity"})
            digest, received, prefix = new_hash(algorithm), 0, b""
            with urllib.request.urlopen(request, timeout=30) as response:
                if not https_url(response.url, MEDIA_HOSTS):
                    raise ValueError("Unreviewed redirect")
                for chunk in iter(lambda: response.read(256 * 1024), b""):
                    prefix = (prefix + chunk)[:32]
                    received += len(chunk)
                    if received > entry["bytes"]:
                        raise ValueError("File grew beyond catalog size")
                    digest.update(chunk)
            if received != entry["bytes"] or digest.hexdigest() != expected or not media_signature(prefix, entry["kind"]):
                raise ValueError("Original file no longer matches catalog: " + entry["id"])
    print("Media types:", dict(counts))
    print("Minimum original resolution: 1920 x 1080; maximum original size: 20,000,000 bytes; no content categories.")
    print("Images/videos record positive community feedback. GIFs require verified looping 16:9 frames; unavailable metrics remain null.")
    print(f'{len(catalog["entries"])} source/license records validated. No remote files are bundled.')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
