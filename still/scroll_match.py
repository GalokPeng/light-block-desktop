"""Estimate vertical motion without treating stationary chrome as scroll content."""

from collections import Counter, defaultdict
from dataclasses import dataclass

from PySide6.QtCore import QRect, Qt
from PySide6.QtGui import QImage


@dataclass(frozen=True)
class ScrollMatch:
    shift: int
    footer: int = 0


_QUANTIZE = bytes(value >> 3 for value in range(256))


def _signatures(image):
    # Inspect independent columns, leaving the native scrollbar out of the vote.
    width = image.width() - min(16, image.width() // 20)
    height = image.height()
    sample = image.copy(QRect(0, 0, width, height))
    if width > 192:
        sample = sample.scaled(192, height, Qt.AspectRatioMode.IgnoreAspectRatio,
                               Qt.TransformationMode.SmoothTransformation)
    sample = sample.convertToFormat(QImage.Format.Format_RGB888)
    bits = sample.constBits()
    pixels = memoryview(bits)
    stride = sample.bytesPerLine()
    width = sample.width()
    columns = min(6, width)
    lanes = []
    for column in range(columns):
        start = 3 * (column * width // columns)
        end = 3 * ((column + 1) * width // columns)
        lanes.append([
            hash(bytes(pixels[y * stride + start:y * stride + end]).translate(_QUANTIZE))
            for y in range(height)
        ])
    return lanes


def _fixed_edge(stationary_rows):
    # A selection edge can include a pixel or two of the adjacent content.
    for start in range(4):
        if all(stationary_rows[start:start + 4]):
            end = next((index for index in range(start + 4, len(stationary_rows))
                        if not stationary_rows[index]), len(stationary_rows))
            return end
    return 0


def match_scroll(previous, current):
    height = previous.height()
    if previous.size() != current.size() or height < 60 or previous.width() < 4:
        return None
    if previous == current:
        return ScrollMatch(0)
    old, new = _signatures(previous), _signatures(current)
    if old == new:
        return ScrollMatch(0)

    columns = len(old)
    minimum_lanes = (columns + 1) // 2
    active = [(a, b) for a, b in zip(old, new) if sum(x != y for x, y in zip(a, b)) >= 4]
    edge_agreement = max(1, len(active) - len(active) // 5)
    stationary = [sum(a[y] == b[y] for a, b in active) >= edge_agreement for y in range(height)]
    header = _fixed_edge(stationary[:height // 3])
    footer = _fixed_edge(stationary[::-1][:height // 3])
    start, end = header, height - footer
    votes = defaultdict(set)
    for lane, (before, after) in enumerate(zip(old, new)):
        # A short vertical neighborhood distinguishes repeated individual rows.
        before_keys = list(zip(before[:-4], before[2:-2], before[4:]))
        after_keys = list(zip(after[:-4], after[2:-2], after[4:]))
        locations = defaultdict(list)
        for row, signature in enumerate(before_keys[start:end - 4], start + 2):
            locations[signature].append(row)
        samples = after_keys[start:end - 4]
        counts = Counter(samples)
        for row, signature in enumerate(samples, start + 2):
            hits = locations.get(signature, ())
            if counts[signature] > 6 or len(hits) > 6:
                continue
            for old_row in hits:
                shift = old_row - row
                if 0 < shift <= end - start - 24:
                    votes[shift].add((row, lane))

    # A footer can begin with blank rows. Those rows belong to the moving
    # content unless the edge itself contains a visible, stationary transition.
    if footer and all(len(set(lane[end:])) == 1 for lane in old):
        footer = 0

    candidates = []
    for shift, anchors in sorted(votes.items(), key=lambda item: len(item[1]), reverse=True)[:16]:
        rows = {row for row, _ in anchors}
        lanes = {lane for _, lane in anchors}
        minimum_rows = max(7, height // 100) * (1 if len(lanes) >= 2 else 2)
        if len(rows) < minimum_rows or max(rows) - min(rows) < minimum_rows - 1:
            continue
        # Confirm the alignment across the overlap, tolerating localized repaints.
        comparisons = [sum(a[y + shift] == b[y] for a, b in zip(old, new))
                       for y in range(start, end - shift)]
        coverage = sum(count >= minimum_lanes for count in comparisons) / len(comparisons)
        agreement = sum(comparisons) / (len(comparisons) * columns)
        if coverage >= .83 and agreement >= .67:
            candidates.append((len(anchors) * coverage, shift))
    if not candidates:
        return None
    candidates.sort(reverse=True)
    score, shift = candidates[0]
    if len(candidates) > 1 and candidates[1][0] >= score * .88:
        return None
    return ScrollMatch(shift, footer)
