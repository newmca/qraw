#!/usr/bin/env python3
"""Packs the hicolor PNGs into qraw.ico (Windows) and qraw.icns (macOS).

    pack_icons.py <icons-dir> <app-id>

Reads <icons-dir>/hicolor/<N>x<N>/apps/<app-id>.png (written by
qraw-render-icons) and writes <icons-dir>/qraw.ico and qraw.icns. Both
containers hold the PNG files unchanged, so the output is reproducible.
Python standard library only.
"""
import struct
import sys
from pathlib import Path

ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
# ICNS PNG entry types: icp4 16, icp5 32, icp6 64, ic07 128, ic08 256, ic09 512.
ICNS_TYPES = [(16, b"icp4"), (32, b"icp5"), (64, b"icp6"), (128, b"ic07"), (256, b"ic08"), (512, b"ic09")]


def load(icons: Path, app_id: str, size: int) -> bytes:
    path = icons / "hicolor" / f"{size}x{size}" / "apps" / f"{app_id}.png"
    return path.read_bytes()


def pack_ico(icons: Path, app_id: str) -> bytes:
    images = [(size, load(icons, app_id, size)) for size in ICO_SIZES]
    out = bytearray(struct.pack("<HHH", 0, 1, len(images)))  # reserved, type = icon, count
    offset = 6 + 16 * len(images)
    for size, png in images:
        dimension = 0 if size >= 256 else size  # 0 means 256
        out += struct.pack("<BBBBHHII", dimension, dimension, 0, 0, 1, 32, len(png), offset)
        offset += len(png)
    for _, png in images:
        out += png
    return bytes(out)


def pack_icns(icons: Path, app_id: str) -> bytes:
    body = bytearray()
    for size, kind in ICNS_TYPES:
        png = load(icons, app_id, size)
        body += kind + struct.pack(">I", 8 + len(png)) + png
    return b"icns" + struct.pack(">I", 8 + len(body)) + bytes(body)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    icons = Path(sys.argv[1])
    app_id = sys.argv[2]
    (icons / "qraw.ico").write_bytes(pack_ico(icons, app_id))
    (icons / "qraw.icns").write_bytes(pack_icns(icons, app_id))
    return 0


if __name__ == "__main__":
    sys.exit(main())
