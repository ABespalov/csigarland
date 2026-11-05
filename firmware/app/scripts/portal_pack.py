# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Gzip the portal's pages, stylesheet and script into a C++ header.
# Contributors: None
#
# The stylesheet and script are served under names holding a hash of their content, and
# browsers may keep those for good: a change gives a new name, which the pages link to. The
# pages themselves are checked with the garland each time. The sources are src/portal_assets.h
# and src/portal_page_*.h.
#
# The pages are gzipped by the zlib of the Python that runs the build, and another zlib packs them into
# other bytes: zlib-ng, as Python 3.14 has on Windows, and the classic zlib of Linux and macOS differ. So
# the zlib is recorded in generated/portal_zlib.txt, which the deploy scripts put in a build's manifest, and
# a Release build warns of one other than CSIGARLAND_RELEASE_ZLIB in platformio.ini, whose image no release
# takes.

Import("env")

import gzip
import hashlib
import re
import zlib
from pathlib import Path

_DEFINITION = re.compile(r'(k\w+)\[\] = R"(\w+)\((.*?)\)\2"', re.S)
_PAGES = (
    ("/", "kPageEffects", "portal_page_effects.h"),
    ("/schedule", "kPageSchedule", "portal_page_schedule.h"),
    ("/device", "kPageDevice", "portal_page_device.h"),
    ("/network", "kPageNetwork", "portal_page_network.h"),
    ("/firmware", "kPageFirmware", "portal_page_firmware.h"),
    ("/info", "kPageInfo", "portal_page_info.h"),
    ("/pair", "kPagePair", "portal_page_pair.h"),
)
_REVALIDATE = "no-cache"
_FOREVER = "public, max-age=31536000, immutable"
_BYTES_PER_LINE = 16
_RELEASE_SUFFIX = "-release"
_RELEASE_ZLIB = re.compile(r"^CSIGARLAND_RELEASE_ZLIB\s*=\s*(\S+)\s*$", re.MULTILINE)

# The wordmark on the glass and in the portal is one set of letters, so the build compares them.
_GLYPH = re.compile(r"\{(\d+), (\d+), (\d+), (\d+), (\d+)\},\s*// \w")
_WORDMARK_WHITE_LETTERS = 3
_WORDMARK_PITCH = 6
_WORDMARK_ROWS = 7


def _wordmark_path(glyphs: list) -> str:
    """One stroke along each row's run of lit cells, as an SVG path."""
    path = ""
    columns = [column for glyph in glyphs for column in (*glyph, 0)]
    for row in range(_WORDMARK_ROWS):
        x = 0
        while x < len(columns):
            if not columns[x] >> row & 1:
                x += 1
                continue
            start = x
            while x < len(columns) and columns[x] >> row & 1:
                x += 1
            path += f"M{start} {row}.5h{x - start}"
    return path


def _check_wordmark(src: Path, top: str) -> None:
    glyphs = [tuple(int(value) for value in match) for match in _GLYPH.findall((src / "wordmark.h").read_text(encoding="utf-8"))]
    if len(glyphs) != 10:
        raise RuntimeError("wordmark.h does not hold the ten letters of the wordmark")
    white = _wordmark_path(glyphs[:_WORDMARK_WHITE_LETTERS])
    blue = _wordmark_path(glyphs[_WORDMARK_WHITE_LETTERS:])
    if f'd="{white}"' not in top or f'd="{blue}"' not in top:
        raise RuntimeError(f'kPortalPageTop\'s wordmark is not that of wordmark.h. White: {white} Blue: {blue}')


def _texts(path: Path) -> dict:
    return {name: body for name, _, body in _DEFINITION.findall(path.read_text(encoding="utf-8"))}


def _versioned_uri(prefix: str, extension: str, text: str) -> str:
    return f"/{prefix}.{hashlib.sha256(text.encode()).hexdigest()[:8]}.{extension}"


def _array(name: str, data: bytes) -> list:
    lines = [
        ", ".join(f"0x{byte:02X}" for byte in data[i : i + _BYTES_PER_LINE])
        for i in range(0, len(data), _BYTES_PER_LINE)
    ]
    return [f"inline constexpr unsigned char {name}[] = {{", *(f"    {line}," for line in lines), "};", ""]


def _record_zlib(project_dir: Path) -> None:
    """The zlib the pages are packed with, beside them, and a word where a release would take another."""
    used = zlib.ZLIB_RUNTIME_VERSION
    record = project_dir / "generated" / "portal_zlib.txt"
    record.parent.mkdir(parents=True, exist_ok=True)
    if not record.is_file() or record.read_text(encoding="utf-8") != f"{used}\n":
        record.write_text(f"{used}\n", encoding="utf-8", newline="\n")
    pinned = _RELEASE_ZLIB.search((project_dir / "platformio.ini").read_text(encoding="utf-8"))
    if not pinned:
        raise RuntimeError("CSIGARLAND_RELEASE_ZLIB not found in platformio.ini")
    if env.subst("$PIOENV").endswith(_RELEASE_SUFFIX) and used != pinned.group(1):
        print(f"WARNING: the portal's pages are gzipped with zlib {used}, not {pinned.group(1)} as a release's are: "
              "this builds, but not the image of a release, and no release takes it.")


def portal_pack() -> None:
    project_dir = Path(env["PROJECT_DIR"])
    _record_zlib(project_dir)
    src = project_dir / "src"
    assets = _texts(src / "portal_assets.h")
    _check_wordmark(src, assets["kPortalPageTop"])
    style_uri = _versioned_uri("s", "css", assets["kPortalStyle"])
    script_uri = _versioned_uri("p", "js", assets["kPortalScript"])
    top = assets["kPortalPageTop"].replace("/portal.css", style_uri).replace("/portal.js", script_uri)
    if style_uri not in top or script_uri not in top:
        raise RuntimeError("kPortalPageTop does not link the stylesheet and the script")

    files = [(style_uri, "text/css", _FOREVER, assets["kPortalStyle"]), (script_uri, "text/javascript", _FOREVER, assets["kPortalScript"])]
    for uri, name, header in _PAGES:
        files.append((uri, "text/html", _REVALIDATE, top + _texts(src / header)[name] + assets["kPortalPageBottom"]))

    arrays = []
    rows = []
    raw_total = 0
    packed_total = 0
    for index, (uri, kind, cache, text) in enumerate(files):
        raw = text.encode()
        # No time in the header, so the same text always gives the same image.
        packed = gzip.compress(raw, 9, mtime=0)
        raw_total += len(raw)
        packed_total += len(packed)
        arrays += _array(f"kPortalData{index}", packed)
        rows.append(f'    {{"{uri}", "{kind}", "{cache}", kPortalData{index}, sizeof(kPortalData{index})}},')

    header = "\n".join(
        [
            "// Generated by portal_pack.py from src/portal_*.h. Do not edit.",
            "#pragma once",
            "",
            "#include <cstddef>",
            "",
            "namespace garland {",
            "",
            "struct PortalFile {",
            "    const char* uri;",
            "    const char* type;",
            "    const char* cache;",
            "    const unsigned char* data;",
            "    size_t bytes;",
            "};",
            "",
            *arrays,
            "inline constexpr PortalFile kPortalFiles[] = {",
            *rows,
            "};",
            "",
            "}  // namespace garland",
            "",
        ]
    )
    target = project_dir / "generated" / "portal_web.h"
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.is_file() or target.read_text(encoding="utf-8") != header:
        target.write_text(header, encoding="utf-8")
    print(f"portal: {raw_total} bytes, {packed_total} gzipped with zlib {zlib.ZLIB_RUNTIME_VERSION}")


portal_pack()
