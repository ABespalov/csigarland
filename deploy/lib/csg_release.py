# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: What deploy/publish-release.sh works out: the release's notes, and the site's downloads and catalog.
# Contributors: None
#
# Each command does what a part of deploy/publish-release.ps1 does, and writes what it writes, so the two
# scripts keep one site/downloads between them.
#
#   csg_release.py notes <changelog> <release>                       the notes from the changelog's section
#   csg_release.py site <state.json> <out-folder> <run-id>           writes manifest.json and the catalog
#   csg_release.py describe <file-name>                              what a release's file is, or nothing
from __future__ import annotations

import json
import re
import sys
from collections import OrderedDict
from pathlib import Path
from urllib.parse import urlparse

from csg_tool import UTILITY_PLATFORMS, Refusal, dumps

REPOSITORY = "ABespalov/csigarland"
# Under internal/catalog's 64 KB, with room for what JSON's spacing may add.
CATALOG_BYTES = 48 * 1024
FAMILIES = ["Windows", "Linux", "macOS"]


def first_sentence(text: str) -> str:
    """An item's first sentence: up to the stop before a space and a capital, a code span or a bracket."""
    found = re.match(r"^.*?[.!?](?=\s+[A-Z`(\[]|$)", text)
    return found.group(0) if found else text


def release_notes(text: str, release: str) -> str:
    lines = re.split(r"\r?\n", text)
    heading = re.compile(rf"^## \[{re.escape(release)}\]")
    starts = [index for index, line in enumerate(lines) if heading.match(line)]
    if not starts:
        raise Refusal(f"CHANGELOG.md at v{release} has no section for {release}: write it, and tag again.")
    entries: list[str] = []
    index = starts[0] + 1
    while index < len(lines) and not lines[index].startswith("## "):
        line = lines[index]
        if re.match(r"^\s+\S", line) and entries and entries[-1].startswith("- "):
            entries[-1] += " " + line.strip()
        else:
            entries.append(line)
        index += 1
    said = ["- " + first_sentence(entry[2:].strip()) if entry.startswith("- ") else entry for entry in entries]
    return f"## Release notes for {release}\n\n" + "\n".join(said).strip()


def describe(name: str) -> str:
    ota = re.match(r"^csigarland-(\d+\.\d+\.\d+\.\d+)\.ota$", name)
    if ota:
        return f"Firmware {ota.group(1)} and its effects, the update file"
    program = re.match(r"^csigarland-(\d+\.\d+\.\d+\.\d+)-([a-z0-9]+-[a-z0-9]+)(?:\.exe)?$", name)
    if program and program.group(2) in UTILITY_PLATFORMS:
        return f"The csiGarland utility {program.group(1)} for {UTILITY_PLATFORMS[program.group(2)]}"
    return ""


def _version_key(item) -> str:
    parts = []
    for part in re.split(r"[.-]", str(item.get("version", ""))):
        parts.append(int(part) if part.isdigit() else 0)
    return ".".join(f"{part:06d}" for part in parts)


def newer_first(items: list, date_field: str) -> list:
    """By the day descending, then by the version descending, as PowerShell's Sort-Object takes them."""
    return sorted(items, key=lambda item: (str(item.get(date_field, "") or ""), _version_key(item)), reverse=True)


def order_builds(builds: list) -> list:
    """The platforms in the order Windows, Linux, macOS, each newest first, one with none last as not yet."""
    released = [build for build in builds if build.get("available")]
    waiting = [build for build in builds if not build.get("available")]

    def family(build) -> int:
        name = str(build.get("platform", "")).split(" ")[0]
        return FAMILIES.index(name) if name in FAMILIES else 0
    return sorted(newer_first(released, "released") + waiting, key=family)


def megabytes(size: int) -> str:
    """A file's size as PowerShell's '{0:N1} MB' says it in the invariant culture."""
    return f"{size / 1048576:,.1f} MB"


def site(state: dict, out: Path, run_id: str) -> None:
    """The site's manifest and catalog from what was published before, and this run's builds."""
    release, today = state["release"], state["today"]
    tag = f"v{release}"
    base = f"https://github.com/{REPOSITORY}/releases/download/{tag}"
    days = state.get("days") or {}
    before = state.get("manifest_before") or {}
    catalog_before = state.get("catalog_before") or {}
    notes = f"https://github.com/{REPOSITORY}/releases/tag/{tag}" if tuple(int(p) for p in release.split(".")) > (1, 0, 0) else None

    def build(platform, version, file, sha256, size):
        released = days.get(file) or days.get(state.get("ota_name", "")) or today
        return OrderedDict([("platform", platform), ("available", True), ("version", version), ("release", release),
                            ("released", released), ("notes", notes), ("url", f"{base}/{file}"), ("sha256", sha256), ("size", size)])

    firmware, utility = state.get("firmware"), state.get("utility")
    products = [
        ("firmware", "Firmware and effects update",
         "The update file for a garland whose firmware holds the project's root, signed under it: install it from the portal's Firmware page or the utility's catalog.",
         [build("ESP32-C6-LCD-1.3", firmware["version"], firmware["file"], firmware["sha256"], megabytes(firmware["bytes"]))] if firmware else [], []),
        ("utility", "csiGarland utility", "The program for your computer: control, tuning with a live preview, updates and the log.",
         [build(utility["platform_name"], utility["version"], utility["file"], utility["sha256"], megabytes(utility["bytes"]))] if utility else [],
         ["Windows x64", "Linux", "macOS"]),
        ("root-certificate", "Root certificate of the release", "To build the release again and compare it to the byte.",
         [build("Any", firmware["version"], "csigarland-ota-root.crt", firmware["root_sha256"], None)] if firmware else [], []),
    ]
    listed = []
    for product_id, name, about, new, waiting in products:
        earlier = []
        for product in before.get("products", []):
            if product.get("id") == product_id:
                earlier.extend(product.get("builds", []))
        kept = [old for old in earlier if old.get("available") and not any(
            item["platform"] == old.get("platform") and item["release"] == old.get("release") for item in new)]
        builds = newer_first(new + kept, "released")
        out_platforms = [item["platform"] for item in builds]
        waiting_all = []
        for platform in waiting + [old["platform"] for old in earlier if not old.get("available")]:
            if platform not in waiting_all:
                waiting_all.append(platform)
        for platform in waiting_all:
            if platform not in out_platforms and not any(p.startswith(platform + " ") for p in out_platforms):
                builds.append(OrderedDict([("platform", platform), ("available", False)]))
        builds = order_builds(builds)
        if builds:
            listed.append(OrderedDict([("id", product_id), ("name", name), ("about", about), ("builds", builds)]))
    updated = max(days.values()) if days else today
    manifest = OrderedDict([("format", "csigarland.downloads/2"), ("updated", updated), ("source", f"https://github.com/{REPOSITORY}"),
                            ("release", f"https://github.com/{REPOSITORY}/releases/tag/{tag}"), ("products", listed)])
    files = OrderedDict([("manifest.json", dumps(manifest))])
    if firmware:
        entry = OrderedDict([("version", firmware["version"]), ("file", firmware["file"]), ("url", f"{base}/{firmware['file']}"),
                             ("sha256", firmware["sha256"]), ("bytes", firmware["bytes"]), ("effects", firmware["effects"]),
                             ("published", days.get(firmware["file"], today))])
        entries = [entry] + [item for item in catalog_before.get("firmware", []) if item.get("version") != firmware["version"]]
        entries = newer_first(entries, "published")
        catalog = OrderedDict([("format", "csigarland.firmware-catalog/1"), ("firmware", entries)])
        # The utility of 1.0.0 reads a catalog of 64 KB at most: the oldest releases go from it first.
        text = dumps(catalog)
        while len(text.encode("utf-8")) > CATALOG_BYTES and len(catalog["firmware"]) > 1:
            catalog["firmware"] = catalog["firmware"][:-1]
            text = dumps(catalog)
        files["firmware-catalog-v1.json"] = text
    out.mkdir(parents=True, exist_ok=True)
    # Written whole beside the others first, then put in their places, so a failure leaves each as it was.
    for name, text in files.items():
        with open(out / f"{name}.{run_id}.new", "w", encoding="utf-8", newline="") as stream:
            stream.write(text)
    for name in files:
        (out / f"{name}.{run_id}.new").replace(out / name)
    sys.stderr.write(f"Wrote {' and '.join(files)} in {out} for {state['what']} of {tag}.\n")


def main(argv: list[str]) -> int:
    if argv[0] == "notes":
        sys.stdout.write(release_notes(Path(argv[1]).read_text(encoding="utf-8"), argv[2]) + "\n")
    elif argv[0] == "site":
        site(json.loads(Path(argv[1]).read_text(encoding="utf-8"), object_pairs_hook=OrderedDict), Path(argv[2]), argv[3])
    elif argv[0] == "describe":
        said = describe(argv[1])
        if said:
            sys.stdout.write(said + "\n")
    elif argv[0] == "file-of-url":
        sys.stdout.write(urlparse(argv[1]).path.rsplit("/", 1)[-1] + "\n")
    else:
        raise Refusal(f"csg_release.py has no command {argv[0]}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Refusal as refusal:
        sys.stderr.write(f"{refusal}\n")
        sys.exit(2)
