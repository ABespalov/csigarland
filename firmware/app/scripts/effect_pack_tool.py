# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Turn effects/catalog.json and its effect files into the effects pack.
# Contributors: None
#
# Runs as a plain program under any Python 3:
#
#   effect_pack_tool.py <effects dir> <output dir>
#
# and writes, in the output directory:
#
#   effects.pack        the pack, as an update file carries it
#   effects_slot.bin    the pack with its commit record, for a flash over USB
#   effects_blank.bin   one erased sector, which voids the other slot's commit record
#   effects_data.h      the pack as C++, for the host tests only
#
# An entry with "todo": true is a planned effect: it has no file and no algorithm yet, so
# the pack carries only its name, to show on the portal. Every other entry must have an
# effect file. The layouts are the ones src/effect_pack.h reads.

import hashlib
import json
import re
import struct
import sys
from pathlib import Path

_ID = re.compile(r"^[a-z0-9-]{1,32}$")
_SAFE_NAME = re.compile(r'^[\x20-\x7E]{1,32}$')

PACK_MAGIC = b"CGPK"
PACK_FORMAT = 2
PACK_HEADER_BYTES = 12
SLOT_MAGIC = b"CGPS"
SLOT_BYTES = 0x18000
SLOT_HEADER_BYTES = 4096
MAX_EFFECTS = 256
MAX_PLANNED = 32
BYTES_PER_LINE = 16


def _safe_name(name: str) -> bool:
    return bool(_SAFE_NAME.match(name)) and '"' not in name and "\\" not in name


def _read_entries(effects_dir: Path):
    catalog = json.loads((effects_dir / "catalog.json").read_text(encoding="utf-8"))
    if catalog.get("format") != "csigarland.catalog/1":
        raise SystemExit("catalog.json: unexpected format")
    groups = {group["id"]: group["name"] for group in catalog["groups"]}
    for name in groups.values():
        if not _safe_name(name):
            raise SystemExit(f"catalog.json: unsafe group name {name!r}")
    seen = set()
    for entry in catalog["effects"]:
        effect_id = entry["id"]
        if not _ID.match(effect_id) or effect_id == "preview":
            raise SystemExit(f"catalog.json: invalid id {effect_id!r}")
        if entry.get("group") not in groups:
            raise SystemExit(f"catalog.json: {effect_id} has no known group")
        if entry.get("favorite", False) and entry.get("todo", False):
            raise SystemExit(f"catalog.json: {effect_id} is todo, so it cannot be a favourite")
        if effect_id in seen:
            raise SystemExit(f"catalog.json: duplicate id {effect_id!r}")
        seen.add(effect_id)
        has_file = (effects_dir / f"{effect_id}.json").is_file()
        if entry.get("todo", False) and has_file:
            raise SystemExit(f"catalog.json: {effect_id} is todo but has a file; remove the flag")
        if not entry.get("todo", False) and not has_file:
            raise SystemExit(f"catalog.json: {effect_id} has no file; add one or mark it todo")
    if not any(entry.get("favorite", False) for entry in catalog["effects"]):
        raise SystemExit("catalog.json: the EFFECT button needs at least one default favourite")
    return catalog["effects"], groups


def _record(kind: str, effect_id: str, group: str, name: str, text: str) -> bytes:
    return kind.encode() + b"\0".join(part.encode("ascii") for part in (effect_id, group, name, text)) + b"\0"


def build_pack(effects_dir: Path) -> tuple[bytes, int, int]:
    entries, groups = _read_entries(effects_dir)
    records = []
    ready = 0
    planned = 0
    for entry in entries:
        effect_id = entry["id"]
        group = groups[entry["group"]]
        if entry.get("todo", False):
            if not _safe_name(entry["name"]):
                raise SystemExit(f"catalog.json: unsafe name for {effect_id}")
            records.append(_record("p", effect_id, group, entry["name"], ""))
            planned += 1
            continue
        # Parsed and re-dumped: this drops the formatting and fails the build on a file that
        # is not JSON at all. Its default escaping leaves plain ASCII, with no NUL to end early.
        document = json.loads((effects_dir / f"{effect_id}.json").read_text(encoding="utf-8"))
        text = json.dumps(document, separators=(",", ":"))
        if not _safe_name(document.get("name", "")):
            raise SystemExit(f"{effect_id}.json: unsafe name")
        records.append(_record("f" if entry.get("favorite", False) else "e", effect_id, group, document["name"], text))
        ready += 1
    if ready > MAX_EFFECTS or planned > MAX_PLANNED:
        raise SystemExit(f"{ready} effects and {planned} planned; a pack holds {MAX_EFFECTS} and {MAX_PLANNED}")
    body = b"".join(records)
    pack = PACK_MAGIC + struct.pack("<II", PACK_FORMAT, PACK_HEADER_BYTES + len(body)) + body
    if len(pack) > SLOT_BYTES - SLOT_HEADER_BYTES:
        raise SystemExit(f"The pack is {len(pack)} bytes; a slot holds {SLOT_BYTES - SLOT_HEADER_BYTES}.")
    return pack, ready, planned


def slot_image(pack: bytes) -> bytes:
    # Sequence 1, as a flash over USB also voids the other slot.
    record = SLOT_MAGIC + struct.pack("<II", 1, len(pack)) + hashlib.sha256(pack).digest()
    return record.ljust(SLOT_HEADER_BYTES, b"\xFF") + pack


def test_header(pack: bytes, ready: int, planned: int) -> str:
    lines = [
        ", ".join(f"0x{byte:02X}" for byte in pack[i : i + BYTES_PER_LINE])
        for i in range(0, len(pack), BYTES_PER_LINE)
    ]
    return "\n".join(
        [
            "// Generated by effect_pack_tool.py from effects/catalog.json. Do not edit.",
            "#pragma once",
            "",
            "#include <cstddef>",
            "",
            "namespace garland {",
            "",
            f"inline constexpr size_t kShippedEffectCount = {ready};",
            f"inline constexpr size_t kShippedPlannedCount = {planned};",
            "inline constexpr unsigned char kShippedPack[] = {",
            *(f"    {line}," for line in lines),
            "};",
            "",
            "}  // namespace garland",
            "",
        ]
    )


def write_if_changed(path: Path, content: bytes) -> None:
    if not path.is_file() or path.read_bytes() != content:
        path.write_bytes(content)


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: see the top of this file")
    effects_dir = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    pack, ready, planned = build_pack(effects_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    write_if_changed(out_dir / "effects.pack", pack)
    write_if_changed(out_dir / "effects_slot.bin", slot_image(pack))
    write_if_changed(out_dir / "effects_blank.bin", b"\xFF" * SLOT_HEADER_BYTES)
    write_if_changed(out_dir / "effects_data.h", test_header(pack, ready, planned).encode())
    print(f"effects pack: {ready} effects, {planned} planned, {len(pack)} bytes")


main()
