# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Turns the raw frames of tools/screens into PNG files, enlarged by whole pixels.
# Contributors: None
#
# Example:
#   python deploy/screens_to_png.py temp/screens docs/images/screens 240 2

import struct
import sys
import zlib
from pathlib import Path


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def to_png(raw, size, scale):
    rows = bytearray()
    for y in range(size):
        row = raw[y * size * 3 : (y + 1) * size * 3]
        enlarged = b"".join(row[x * 3 : x * 3 + 3] * scale for x in range(size))
        rows += (b"\0" + enlarged) * scale
    header = struct.pack(">IIBBBBB", size * scale, size * scale, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b"")


def main():
    source, target, size, scale = Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
    target.mkdir(parents=True, exist_ok=True)
    frames = sorted(source.glob("*.rgb"))
    if not frames:
        raise SystemExit(f"No frames in {source}")
    for frame in frames:
        raw = frame.read_bytes()
        if len(raw) != size * size * 3:
            raise SystemExit(f"{frame} is {len(raw)} bytes, not a frame of {size} squared")
        (target / f"{frame.stem}.png").write_bytes(to_png(raw, size, scale))
    print(f"{len(frames)} pictures in {target}")


if __name__ == "__main__":
    main()
