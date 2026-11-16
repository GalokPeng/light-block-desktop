"""Check the reviewed URL-only wallpaper catalog without downloading media."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
MEDIA_HOSTS = {'upload.wikimedia.org', 'commons.wikimedia.org', 'thumb.wikimedia.org', 'archive.org', 'images.unsplash.com'}


def validate(path):
    raw = path.read_bytes()
    if len(raw) > 2 * 1024 * 1024:
        raise ValueError('Catalog exceeds 2 MiB')
    catalog = json.loads(raw)
    if catalog.get('schema_version') != 4:
        raise ValueError('Expected catalog schema 4')
    entries = catalog['entries']
    if not isinstance(entries, list) or not 1 <= len(entries) <= 120:
        raise ValueError('Invalid catalog size')
    ids, names, counts = set(), set(), Counter()
    for entry in entries:
        identity, name, kind = entry['id'], entry['filename'], entry['kind']
        if not re.fullmatch(r'[A-Za-z0-9_-]{1,99}', identity) or identity in ids:
            raise ValueError('Duplicate or invalid identity')
        if not name or len(name) >= 180 or any(ch in name for ch in '/\\:*?"<>|') or name in names or name in {'.', '..'}:
            raise ValueError(f'Invalid file name: {identity}')
        if kind not in {'image', 'gif', 'video'}:
            raise ValueError(f'Invalid kind: {identity}')
        width, height, size = entry['width'], entry['height'], entry['bytes']
        if any(type(value) is not int for value in (width, height, size)) or width < 1920 or height < 1080 or not 0 < size <= 20_000_000:
            raise ValueError(f'Original dimensions/size out of bounds: {identity}')
        if kind == 'gif' and width * 9 != height * 16:
            raise ValueError(f'GIF must be native 16:9: {identity}')
        if not re.fullmatch(r'[a-f0-9]{40}', entry['sha1']):
            raise ValueError(f'Invalid original checksum: {identity}')
        for field in ('download_url', 'thumbnail_url'):
            url = urlsplit(entry[field])
            if url.scheme != 'https' or url.hostname not in MEDIA_HOSTS or url.username or url.password or url.port not in (None, 443):
                raise ValueError(f'Unapproved media URL: {identity}')
        for field in ('author', 'license', 'source_url', 'license_url'):
            if not isinstance(entry.get(field), str) or not entry[field].strip():
                raise ValueError(f'Missing {field}: {identity}')
        for language in ('zh', 'en'):
            if not entry.get('title', {}).get(language):
                raise ValueError(f'Missing title {language}: {identity}')
        ids.add(identity)
        names.add(name)
        counts[kind] += 1
    if any(value > 80 for value in counts.values()):
        raise ValueError('More than 80 entries in one media kind')
    return dict(counts)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog', type=Path, default=ROOT / 'assets/online-wallpapers.json')
    args = parser.parse_args()
    print(json.dumps({'passed': True, 'counts': validate(args.catalog), 'network_requests': 0}, ensure_ascii=False))
