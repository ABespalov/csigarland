# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Renders the figurines from their OpenSCAD models: each one on its mount, and the eight on the
#          cable in the order of the firmware's layout, for docs/images/diffusers.
# Contributors: None

"""Usage: model_renders.py <openscad.exe> <repository root>"""

# Under any python3 from 3.8 on, as the bash scripts may run it: its hints are not evaluated.
from __future__ import annotations

import re
import struct
import subprocess
import sys
import zlib
from pathlib import Path

# The module that draws each figurine's cap, by the name the layout gives it.
CAPS = {
    "tree": "tree_diffuser_cap",
    "star": "magen_david_cap",
    "icicle": "icicle_cluster_cap",
    "pinecone": "pinecone_diffuser_cap",
    "bell": "bell_diffuser_cap",
    "snowman": "snowman_diffuser_cap",
    "gingerbread": "gingerbread_diffuser_cap",
    "lantern": "lantern_diffuser_cap",
}
# The tree from a little lower, so that its trunk shows below the tiers.
CAMERAS = {"tree": ["--camera=0,0,-26,75,0,25,180"]}
BACKGROUND = (248, 248, 248)  # The Tomorrow colour scheme's.
PITCH = 52  # mm between figurines on the cable in the picture.


def layout(root: Path) -> list[str]:
    """The figurines in the order the firmware's zones count them along the cable."""
    source = (root / "firmware" / "app" / "src" / "effect_parser.cpp").read_text(encoding="utf-8")
    found = re.search(r"kFigurines\[kFigurineCount\] = \{(.*?)\};", source, re.S)
    if not found:
        raise SystemExit("The layout kFigurines was not found in effect_parser.cpp.")
    names = re.findall(r'"(\w+)"', found.group(1))
    if sorted(names) != sorted(CAPS):
        raise SystemExit(f"The layout names {names}, not the eight figurines.")
    return names


def read_png(path: Path):
    data = path.read_bytes()
    at, idat, width, height, kind = 8, b"", 0, 0, 0
    while at < len(data):
        length, chunk_type = struct.unpack(">I4s", data[at : at + 8])
        chunk = data[at + 8 : at + 8 + length]
        if chunk_type == b"IHDR":
            width, height, _, kind = struct.unpack(">IIBB", chunk[:10])
        elif chunk_type == b"IDAT":
            idat += chunk
        at += 12 + length
    raw = zlib.decompress(idat)
    bpp = 3 if kind == 2 else 4
    stride = 1 + width * bpp
    previous = bytearray(width * bpp)
    rows = []
    for y in range(height):
        kind_of_filter = raw[y * stride]
        row = bytearray(raw[y * stride + 1 : (y + 1) * stride])
        for i in range(len(row)):
            a = row[i - bpp] if i >= bpp else 0
            b = previous[i]
            c = previous[i - bpp] if i >= bpp else 0
            if kind_of_filter == 1:
                row[i] = (row[i] + a) & 0xFF
            elif kind_of_filter == 2:
                row[i] = (row[i] + b) & 0xFF
            elif kind_of_filter == 3:
                row[i] = (row[i] + ((a + b) >> 1)) & 0xFF
            elif kind_of_filter == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                row[i] = (row[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        previous = row
        rows.append(bytes(row))
    return width, height, bpp, rows


def write_png(path: Path, width: int, height: int, bpp: int, rows: list[bytes]) -> None:
    def chunk(chunk_type: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + chunk_type + body + struct.pack(">I", zlib.crc32(chunk_type + body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 2 if bpp == 3 else 6, 0, 0, 0)
    body = zlib.compress(b"".join(b"\x00" + row for row in rows), 9)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", body) + chunk(b"IEND", b""))


def crop(source: Path, target: Path, margin: int = 40) -> None:
    """The picture cut to what is drawn, with a margin of background around it."""
    width, height, bpp, rows = read_png(source)
    background = bytes(BACKGROUND)
    left, top, right, bottom = width, height, -1, -1
    for y, row in enumerate(rows):
        for x in range(width):
            if row[x * bpp : x * bpp + 3] != background:
                left, right = min(left, x), max(right, x)
                top, bottom = min(top, y), max(bottom, y)
    if right < 0:
        raise SystemExit(f"{source.name} has nothing drawn on it.")
    x0, y0 = max(0, left - margin), max(0, top - margin)
    x1, y1 = min(width - 1, right + margin), min(height - 1, bottom + margin)
    write_png(target, x1 - x0 + 1, y1 - y0 + 1, bpp, [row[x0 * bpp : (x1 + 1) * bpp] for row in rows[y0 : y1 + 1]])


def render(openscad: str, scad: Path, picture: Path, size: str, view: list[str]) -> None:
    run = subprocess.run(
        [openscad, "-o", str(picture), f"--imgsize={size}", "--colorscheme=Tomorrow", "--projection=p", *view, str(scad)],
        capture_output=True,
        text=True,
    )
    if run.returncode != 0 or not picture.exists():
        raise SystemExit(f"OpenSCAD failed on {scad.name}:\n{run.stderr}")


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    openscad, root = sys.argv[1], Path(sys.argv[2]).resolve()
    models = root / "3d-model"
    scratch = root / "temp" / "model-renders"
    target = root / "docs" / "images" / "diffusers"
    scratch.mkdir(parents=True, exist_ok=True)
    model = lambda name: (models / f"figurine_{name}.scad").as_posix()

    for name in CAPS:
        scad = scratch / f"{name}.scad"
        scad.write_text(f"use <{model(name)}>\nrotate([0, 180, 0]) assembled_view();\n", encoding="utf-8")
        render(openscad, scad, target / f"diffuser_{name}.png", "900,900", CAMERAS.get(name, ["--autocenter", "--viewall"]))
        print(f"diffuser_{name}.png")

    order = layout(root)
    uses = "".join(f"use <{model(name)}>\n" for name in CAPS)
    places = "".join(
        f"translate([{(i - 3.5) * PITCH}, 0, 0]) rotate([0, 180, 0]) figurine() {CAPS[name]}();\n"
        for i, name in enumerate(order)
    )
    scad = scratch / "hanging.scad"
    scad.write_text(
        uses
        + f"""
// A figurine on its mount, with its LED board, hanging from the cable.
module figurine() {{
    base_mount();
    translate([0, 0, 4.40 + 5.00 - 1.40]) led_pcb_dummy();
    translate([0, 0, 4.40]) children();
}}
rotate([0, 180, 0]) translate([0, 0, 1.20 + 2.00 / 2]) wire_harness_dummy(len = 7 * {PITCH} + 36);
"""
        + places,
        encoding="utf-8",
    )
    raw = scratch / "hanging-raw.png"
    render(openscad, scad, raw, "1660,900", ["--autocenter", "--viewall"])
    crop(raw, target / "garland_octet_hanging.png")
    print("garland_octet_hanging.png: " + ", ".join(order))


if __name__ == "__main__":
    main()
