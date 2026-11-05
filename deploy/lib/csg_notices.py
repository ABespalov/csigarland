# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Write THIRD_PARTY_NOTICES.md from the exact versions the project uses, for deploy/new-third-party-notices.sh.
# Contributors: None
#
# Does what deploy/new-third-party-notices.ps1 does, and writes the same file to the byte, so the two
# can take turns: the link maps of the last firmware build, firmware/app/dependencies.lock, go.mod, and
# each licence text from what the build and Go downloaded.
#
#   csg_notices.py <repo> <packages> <gomodcache> <goroot> <build-root or -> write|check|utility-only
from __future__ import annotations

import json
import re
import subprocess
import sys
from collections import OrderedDict
from pathlib import Path

from csg_tool import Refusal, culture_key

# Components of ESP-IDF with licences of their own, and the files of each below components/.
OWN_LICENCES = {
    "esp_coex": ["esp_coex/lib/LICENSE"],
    "esp_libc": ["esp_libc/COPYING.NEWLIB", "esp_libc/COPYING.picolibc"],
    "esp_netif": ["lwip/lwip/COPYING"],
    "esp_phy": ["esp_phy/lib/LICENSE"],
    "esp_wifi": ["esp_wifi/lib/LICENSE"],
    "freertos": ["freertos/FreeRTOS-Kernel/LICENSE.md"],
    "http_parser": ["http_parser/LICENSE.txt"],
    "lwip": ["lwip/lwip/COPYING"],
    "mbedtls": ["mbedtls/mbedtls/LICENSE", "mbedtls/mbedtls/tf-psa-crypto/LICENSE"],
    "wpa_supplicant": ["wpa_supplicant/COPYING"],
}
# Libraries that come built, and whose component or package answers for them.
BUILT_LIBRARIES = {
    "libc.a": "toolchain libc", "libm.a": "toolchain libc", "libgcc.a": "toolchain gcc", "libstdc++.a": "toolchain gcc",
    "libcoexist.a": "esp_coex", "libcore.a": "esp_wifi", "libespnow.a": "esp_wifi", "libmesh.a": "esp_wifi",
    "libnet80211.a": "esp_wifi", "libpp.a": "esp_wifi", "libphy.a": "esp_phy",
}
# Single linked files that keep their licence in their own header, with no file for it.
FILE_NOTICES = {"ubsan.c": "esp_system/ubsan.c"}

# As the PowerShell here-string holds it: its last line break, before '@, is not part of it.
HEAD = """# Third-Party Notices

`csiGarland` itself is under the [MIT License](LICENSE). Its firmware and Go program are built from the components below, at these exact versions. Written by `deploy/new-third-party-notices.ps1`: do not edit by hand.

The firmware's list comes from the link maps of the application and the bootloader: every library that reached the image is here, and the script fails on one it cannot place. The utility's comes from its Go modules and, for the compiler's libraries it links statically, from the groups `deploy/build-utility.ps1` holds its link map to: a build that links any other fails. It has had no legal review, and a file inside a component's own licence may name further authors of its own.
"""


def read_text(path: Path) -> str:
    """As Get-Content -Raw reads a file: UTF-8, its BOM dropped, its line endings kept."""
    with open(path, encoding="utf-8-sig", newline="") as stream:
        return stream.read()


def licence_in(folder: Path) -> str:
    """Get-LicenceText: the first licence file of a folder, by name in any case."""
    if not folder.is_dir():
        raise Refusal(f"No licence file in {folder}")
    names = sorted((entry for entry in folder.iterdir() if entry.is_file()), key=lambda entry: entry.name.lower())
    for entry in names:
        if re.match(r"^(LICEN[CS]E|COPYING)(\.|$)", entry.name, re.I):
            return read_text(entry).strip()
    raise Refusal(f"No licence file in {folder}")


def read_licence(path: Path) -> str:
    if not path.exists():
        raise Refusal(f"No licence at {path}")
    return read_text(path).strip()


class Notices:
    def __init__(self) -> None:
        # Sections by licence text, so that one text under several names is written once.
        self.titles: OrderedDict = OrderedDict()

    def add(self, title: str, licence: str) -> None:
        self.titles.setdefault(licence, []).append(title)


def firmware_part(notices: Notices, repo: Path, packages: Path, build_root: Path) -> int:
    esp_idf = packages / "framework-espidf"
    toolchain = packages / "toolchain-riscv32-esp"
    linked_components, linked_built, linked_files = set(), set(), set()
    checked = 0
    for image in (build_root, build_root / "bootloader"):
        maps = sorted(image.glob("*.map"), key=lambda path: path.name.lower())
        if not maps:
            raise Refusal(f"No link map in {image}: build the firmware first.")
        sources: dict = {}
        for command in json.loads(read_text(image / "compile_commands.json")):
            found = re.match(r"^esp-idf/([^/]+)/", command["output"].replace("\\", "/"))
            if found:
                leaf = re.split(r"[\\/]", command["file"])[-1]
                sources.setdefault(f"{found.group(1)}|{leaf}", []).append(command["file"])
        for line in read_text(maps[0]).splitlines():
            if re.match(r"^(Discarded input|Memory Configuration|Allocating common)", line):
                break
            found = re.match(r"^(\S.*?\.a)\(([^)]+)\)", line)
            if not found:
                continue
            archive, member = found.group(1).replace("\\", "/"), found.group(2)
            checked += 1
            component_match = re.search(r"(?:^|/)esp-idf/([^/]+)/(?:[^/]+/)*lib[^/]+\.a$", archive)
            if not component_match:
                leaf = archive.split("/")[-1]
                if leaf not in BUILT_LIBRARIES:
                    raise Refusal(f"{archive} is linked, and nothing here says what licence it is under.")
                linked_built.add(BUILT_LIBRARIES[leaf])
                continue
            component = component_match.group(1)
            linked_components.add(component)
            if member.lower().startswith("x509_crt_bundle"):
                raise Refusal(f"{archive}({member}) is linked: Mozilla's root certificates have terms of their own, and sdkconfig.defaults turns them off.")
            if "__" in component or component in OWN_LICENCES:
                continue
            source_name = re.sub(r"\.o(bj)?$", "", member)
            if source_name in FILE_NOTICES:
                linked_files.add(source_name)
                continue
            if f"{component}|{source_name}" not in sources:
                raise Refusal(f"{archive}({member}) is linked, and no source of it was found to read its licence from.")
            for source in sources[f"{component}|{source_name}"]:
                header = Path(source).read_text(encoding="utf-8", errors="replace")[:3000]
                if not re.search(r"SPDX-License-Identifier:\s*Apache-2\.0\s", header) and "Licensed under the Apache License, Version 2.0" not in header:
                    raise Refusal(f"{source} is linked and is not under ESP-IDF's own licence. Name its licence in this script.")

    lock = read_text(repo / "firmware/app/dependencies.lock")
    from_lock = set()
    pattern = r"(?m)^  ([a-z0-9_-]+)/([a-z0-9_-]+):\r?\n(?:    .*\r?\n|    - .*\r?\n|      .*\r?\n)*?    version: '?([^'\r\n]+)'?\r?$"
    for found in re.finditer(pattern, lock):
        name = f"{found.group(1)}__{found.group(2)}"
        if name in linked_components:
            from_lock.add(name)
            notices.add(f"{found.group(1)}/{found.group(2)} {found.group(3)}", licence_in(repo / "firmware/app/managed_components" / name))
    for component in linked_components:
        if "__" in component and component not in from_lock:
            raise Refusal(f"{component} is linked and is not in dependencies.lock.")

    idf = re.search(r"(?m)^  idf:\r?\n    source:\r?\n      type: idf\r?\n    version: (\S+)", lock)
    idf_version = idf.group(1) if idf else ""
    under_idf = sorted((name for name in linked_components if "__" not in name), key=lambda name: culture_key(name, False))
    notices.add(f"ESP-IDF {idf_version}, for all of its code unless named below: {', '.join(under_idf)}", licence_in(esp_idf))
    for component in sorted(OWN_LICENCES, key=lambda name: culture_key(name, False)):
        if component in linked_components or component in linked_built:
            for file in OWN_LICENCES[component]:
                notices.add(f"{component} in ESP-IDF {idf_version} ({file})", read_licence(esp_idf / "components" / file))
    for name in sorted(linked_files, key=lambda name: culture_key(name, False)):
        text = read_text(esp_idf / "components" / FILE_NOTICES[name])
        header = re.match(r"\A(?://.*\r?\n)+", text)
        notices.add(f"{FILE_NOTICES[name]} in ESP-IDF {idf_version}", re.sub(r"(?m)^// ?", "", header.group(0) if header else "").strip())

    toolchain_version = json.loads(read_text(toolchain / "package.json"))["version"]
    if "toolchain libc" in linked_built:
        for file in ("COPYING.NEWLIB", "COPYING.picolibc"):
            notices.add(f"picolibc in toolchain-riscv32-esp {toolchain_version} ({file})", read_licence(toolchain / "share/licenses/picolibc" / file))
    if "toolchain gcc" in linked_built:
        for file in ("COPYING3", "COPYING.RUNTIME"):
            notices.add(f"libgcc and libstdc++ in toolchain-riscv32-esp {toolchain_version} ({file})", read_licence(toolchain / "share/licenses/gcc" / file))
    return checked


def utility_part(notices: Notices, repo: Path, packages: Path, go_cache: Path, go_root: Path, utility_only: bool) -> None:
    go_mod = read_text(repo / "go.mod")
    for found in re.finditer(r"(?m)^\s*(?:require\s+)?([a-z0-9.-]+\.[a-z]+(?:/\S+)?) (v\S+)", go_mod):
        module, version = found.group(1), found.group(2)
        folder = go_cache / f"{module}@{version}"
        # A module only another platform builds with may not be in the cache yet, and its licence is owed all the same.
        if not folder.exists():
            subprocess.run(["go", "mod", "download", f"{module}@{version}"], cwd=repo, check=False)
        notices.add(f"{module} {version} (Go)", licence_in(folder))
    toolchain = re.search(r"(?m)^toolchain go(\S+)", go_mod)
    notices.add(f"Go {toolchain.group(1) if toolchain else ''} standard library and runtime, in the utility", read_licence(go_root / "LICENSE"))
    kept_folder = repo / "deploy/licences"
    for file in ("COPYING3", "COPYING.RUNTIME"):
        kept = read_licence(kept_folder / f"gcc-{file}")
        if not utility_only:
            theirs = read_licence(packages / "toolchain-riscv32-esp/share/licenses/gcc" / file)
            if kept.replace("\r\n", "\n") != theirs.replace("\r\n", "\n"):
                raise Refusal(f"deploy/licences/gcc-{file} is not the toolchain's {file}: copy it again from {packages}/toolchain-riscv32-esp/share/licenses/gcc.")
        notices.add(f"libstdc++, libgcc and libgcc_eh of GCC, linked into the utility for Windows ({file})", kept)
    notices.add("winpthreads of mingw-w64, linked into the utility for Windows", read_licence(kept_folder / "winpthreads-COPYING"))
    notices.add("mingw-w64's runtime, linked into the utility for Windows: its start-up code, libmingw32, libmingwex and the import libraries of Windows' DLLs",
                read_licence(kept_folder / "mingw-w64-runtime-COPYING"))
    copied = re.match(r"(?s)\A\s*/\*(.*?)\*/", read_text(repo / "internal/engine/cJSON.c"))
    notices.add("cJSON, copied into internal/engine for the utility", re.sub(r"(?m)^ {2}", "", copied.group(1) if copied else "").replace("\r\n", "\n").strip())


def main(argv: list[str]) -> int:
    repo, packages, go_cache, go_root, build_root, mode = (Path(argv[0]), Path(argv[1]), Path(argv[2]), Path(argv[3]), argv[4], argv[5])
    output = repo / "THIRD_PARTY_NOTICES.md"
    notices = Notices()
    checked = 0
    if mode != "utility-only":
        checked = firmware_part(notices, repo, packages, Path(build_root))
    utility_part(notices, repo, packages, go_cache, go_root, mode == "utility-only")
    current = read_text(output).replace("\r\n", "\n") if output.exists() else ""

    if mode == "utility-only":
        named = {}
        for section in re.finditer(r"(?s)## ([^\n]+)\n\n```text\n(.*?)\n```\n", current):
            for title in section.group(1).split("; "):
                named[title] = section.group(2)
        wanted = {}
        for licence, titles in notices.titles.items():
            for title in titles:
                wanted[title] = licence.replace("\r\n", "\n")
        wrong = sorted((title for title in wanted if named.get(title) != wanted[title]), key=lambda t: culture_key(t, False))
        left = sorted((title for title in named if (title.lower().endswith(" (go)") or "utility" in title.lower()) and title not in wanted),
                      key=lambda t: culture_key(t, False))
        if wrong or left:
            raise Refusal(f"{output} does not name what the utility holds: {'; '.join(wrong + left)}. Run deploy/new-third-party-notices.sh, and look at what changed.")
        sys.stderr.write(f"{output} names what the utility holds, {len(wanted)} components.\n")
        return 0

    sections = [f"## {'; '.join(titles)}\n\n```text\n{licence}\n```\n" for licence, titles in notices.titles.items()]
    text = (HEAD + "\n" + "\n".join(sections)).replace("\r\n", "\n")
    if mode == "check":
        if current != text:
            raise Refusal(f"{output} is not what the last build linked. Run deploy/new-third-party-notices.sh, and look at what changed.")
        sys.stderr.write(f"{output} is up to date, with {len(notices.titles)} licences from {checked} linked objects.\n")
        return 0
    with open(output, "w", encoding="utf-8", newline="") as stream:
        stream.write(text)
    sys.stderr.write(f"Wrote {output} with {len(notices.titles)} licences, from {checked} linked objects.\n")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Refusal as refusal:
        sys.stderr.write(f"{refusal}\n")
        sys.exit(2)
