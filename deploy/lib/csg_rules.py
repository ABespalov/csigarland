# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: The source rules of AGENTS.md that a machine can check, for deploy/tests/source-rules.tests.sh.
# Contributors: None
#
# The checks of deploy/tests/source-rules.tests.ps1, each said the same way, and those of the bash
# scripts' own rules. PowerShell's syntax is read only where pwsh is: bash and Python cannot parse it.
#
#   csg_rules.py <repo>
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import unicodedata
from pathlib import Path

import csg_site
from csg_tool import Refusal

MAX_LINES = 1000
HEADER_CHARS = 400
LICENSE_LINE = "Licensed under the terms specified by the MIT License"
POWERSHELL_GUARD = "#requires -Version 7.2"
SOURCE_PATTERN = re.compile(r"\.(c|cc|cpp|h|hpp|go|py|ps1|psm1|sh|html|css|js)$")
# Every source of the project's own carries the header, Python and PowerShell as # comments: AGENTS.md section 1.
NATIVE_PATTERN = re.compile(r"\.(c|cc|cpp|h|hpp|go|sh|py|ps1|psm1)$")
FIRMWARE_PATTERN = re.compile(r"^firmware/app/(src|test|scripts)/")
# A third party's file, copied whole with its own licence header, so cgo can build it.
COPIED_PATTERN = re.compile(r"^internal/engine/cJSON\.(c|h)$")
# What bash 3.2 or the BSD tools of macOS lack, outside comments: a script that uses one works on Linux alone.
NOT_PORTABLE = (
    (re.compile(r"\b(declare|local|typeset)\s+-[a-zA-Z]*[An]"), "an associative array or a nameref, which bash 3.2 lacks"),
    (re.compile(r"\b(mapfile|readarray)\b"), "mapfile, which bash 3.2 lacks"),
    (re.compile(r"\$\{[A-Za-z_][A-Za-z0-9_]*(\[[^\]]*\])?(,,|\^\^|,|\^)\}"), "a change of case by ${x,,}, which bash 3.2 lacks"),
    (re.compile(r"\|&|&>>"), "|& or &>>, which bash 3.2 lacks"),
    (re.compile(r"\bsed\s+(-[a-zA-Z]*\s+)*-i\b"), "sed -i, which differs on macOS"),
    (re.compile(r"\breadlink\s+-f\b"), "readlink -f, which older macOS lacks"),
    (re.compile(r"\bgrep\s+(-[a-zA-Z]*\s+)*-[a-zA-Z]*P"), "grep -P, which macOS lacks"),
    (re.compile(r"(?:^|[\s;&|(])(?:flock|timeout|sha256sum)(?:\s|$)"), "flock, timeout or sha256sum, which macOS lacks"),
    (re.compile(r"\b(stat\s+-c|date\s+-d|find\s.*-printf|xargs\s+-r)\b"), "a GNU-only option, which macOS lacks"),
    (re.compile(r"\$\{[A-Za-z_][A-Za-z0-9_]*\[-1\]\}"), "a negative index, which bash 3.2 lacks"),
    (re.compile(r"\$\([^)]*\bcase\b"), "a case inside $( ), whose patterns' ) bash 3.2 takes for the end of it"),
)


def read_text(path: Path) -> str:
    """As File.ReadAllText reads it: UTF-8, its BOM dropped, its line endings kept."""
    with open(path, encoding="utf-8-sig", errors="replace", newline="") as stream:
        return stream.read()


def listed(repo: Path) -> list[str]:
    done = subprocess.run(["git", "-C", str(repo), "ls-files", "-z", "--cached", "--others", "--exclude-standard"], capture_output=True)
    if done.returncode != 0:
        raise Refusal(f"git ls-files failed with exit code {done.returncode}.")
    return [name.decode("utf-8") for name in done.stdout.split(b"\0") if name]


def check_files(repo: Path, files: list[str], problems: list[str]) -> list[str]:
    sources = [name for name in files if SOURCE_PATTERN.search(name)]
    shells = []
    for file in sources:
        path = repo / file
        if COPIED_PATTERN.match(file) or not path.exists():
            continue
        text = read_text(path)
        lines = len(text.split("\n")) - (1 if text.endswith("\n") else 0)
        if lines > MAX_LINES:
            problems.append(f"{file}: {lines} lines, the limit is {MAX_LINES}")
        if NATIVE_PATTERN.search(file) and LICENSE_LINE not in text[:HEADER_CHARS]:
            problems.append(f"{file}: no licence header")
        if re.search(r"\.(ps1|psm1)$", file):
            guard = re.search(rf"(?m)^{re.escape(POWERSHELL_GUARD)}\s*$", text)
            first_code = re.search(r"(?m)^\s*(\[CmdletBinding|param\s*\()", text)
            if not guard:
                problems.append(f"{file}: no '{POWERSHELL_GUARD}' on a line of its own")
            elif first_code and guard.start() > first_code.start():
                problems.append(f"{file}: '{POWERSHELL_GUARD}' comes after the param block")
        if file.endswith(".sh"):
            shells.append(file)
            check_shell(file, text, problems)
        if FIRMWARE_PATTERN.match(file) and re.search(r"\.(c|cc|cpp|h|hpp)$", file) and re.search(r"[^\x00-\x7F]", text):
            problems.append(f"{file}: not plain ASCII")
    check_syntax(repo, sources, shells, problems)
    return sources


def check_shell(file: str, text: str, problems: list[str]) -> None:
    lines = text.split("\n")
    sourced = file.startswith("deploy/lib/")
    if sourced:
        if lines[0] != "# shellcheck shell=bash":
            problems.append(f"{file}: a sourced script starts with '# shellcheck shell=bash'")
    else:
        if lines[0] != "#!/usr/bin/env bash":
            problems.append(f"{file}: no '#!/usr/bin/env bash' on its first line")
        if not re.search(r"(?m)^set -euo pipefail$", text):
            problems.append(f"{file}: no 'set -euo pipefail' on a line of its own")
    if "\r" in text:
        problems.append(f"{file}: lines ended with CR, which bash reads as part of each command")
    for number, line in enumerate(lines, 1):
        if re.match(r"^\s*#", line):
            continue
        for pattern, said in NOT_PORTABLE:
            if pattern.search(line):
                problems.append(f"{file}:{number}: {said}")


def check_syntax(repo: Path, sources: list[str], shells: list[str], problems: list[str]) -> None:
    """Those for a real board or garland cannot be run here, but they can be read."""
    bash = shutil.which("bash")
    for file in shells:
        if bash:
            done = subprocess.run([bash, "-n", str(repo / file)], capture_output=True, text=True)
            for line in done.stderr.splitlines():
                problems.append(f"{file}: {line.strip()}")
    scripts = [file for file in sources if re.search(r"\.(ps1|psm1)$", file) and (repo / file).exists()]
    pwsh = shutil.which("pwsh")
    if not pwsh:
        sys.stderr.write(f"No pwsh here: the syntax of {len(scripts)} PowerShell scripts was not read.\n")
        return
    reader = ("$failed = 0; foreach ($f in $input) { $e = $null; $null = [System.Management.Automation.Language.Parser]::ParseFile($f, [ref]$null, [ref]$e); "
              "foreach ($x in $e) { Write-Output \"$($f):$($x.Extent.StartLineNumber): $($x.Message)\" } }")
    done = subprocess.run([pwsh, "-NoProfile", "-Command", reader], input="\n".join(str(repo / file) for file in scripts), capture_output=True, text=True)
    for line in done.stdout.splitlines():
        problems.append(line.replace(str(repo) + os.sep, "").strip())


def resolve_stub(path: Path, stubs: Path) -> Path:
    """Where a link leads: a stub on a checkout with no symlinks is a file holding the target's path."""
    full = Path(os.path.abspath(path))
    if full.parent != stubs or full.name == "README.md" or full.is_symlink():
        return full
    return Path(os.path.abspath(stubs / read_text(full).strip()))


def slug(heading: str) -> str:
    """The anchor GitHub makes of a heading, as the PowerShell rules work it out."""
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", heading).lower()
    kept = []
    for ch in text:
        category = unicodedata.category(ch)
        if category.startswith("L") or category in ("Nd", "Mn") or ch.isspace() or ch in "_-":
            kept.append("-" if ch.isspace() else ch)
    return "".join(kept)


def heading_anchors(path: Path, stubs: Path, cache: dict) -> set:
    path = resolve_stub(path, stubs)
    if path in cache:
        return cache[path]
    anchors: set = set()
    in_fence = False
    for line in re.split(r"\r\n|\n|\r", read_text(path)):
        if re.match(r"^\s*```", line):
            in_fence = not in_fence
        found = re.match(r"^#{1,6}\s+(.+?)\s*$", line)
        if in_fence or not found:
            continue
        base = slug(found.group(1))
        candidate, repeat = base, 1
        while candidate in anchors:
            candidate = f"{base}-{repeat}"
            repeat += 1
        anchors.add(candidate)
    cache[path] = anchors
    return anchors


def check_documents(repo: Path, files: list[str], problems: list[str]) -> None:
    stubs = Path(os.path.abspath(repo / "docs/stubs"))
    cache: dict = {}
    for file in (name for name in files if name.endswith(".md")):
        path = repo / file
        if not path.exists() or (file.startswith("docs/stubs/") and file != "docs/stubs/README.md"):
            continue
        text = read_text(path)
        if "file:///" in text:
            problems.append(f"{file}: a file:/// link")
        for link in re.finditer(r"\]\(([^)#\s]+)(#[^)]*)?\)", text):
            target = link.group(1)
            if re.match(r"^[a-z]+:", target, re.I):
                continue
            resolved = path.parent / target
            if not resolved.exists():
                problems.append(f"{file}: broken link to {target}")
            elif link.group(2) and target.endswith(".md") and link.group(2)[1:] not in heading_anchors(resolved, stubs, cache):
                problems.append(f"{file}: no heading for {target}{link.group(2)}")
        for image in re.finditer(r"<img\s[^>]*src=\"([^\"#]+)\"", text):
            target = image.group(1)
            if not re.match(r"^[a-z]+:", target, re.I) and not (path.parent / target).exists():
                problems.append(f"{file}: broken picture {target}")
        for link in re.finditer(r"\]\((#[^)\s]+)\)", text):
            if link.group(1)[1:] not in heading_anchors(path, stubs, cache):
                problems.append(f"{file}: no heading for {link.group(1)}")

    readme = read_text(stubs / "README.md")
    for entry in sorted(os.listdir(stubs)):
        if entry == "README.md":
            continue
        stub = stubs / entry
        file = f"docs/stubs/{entry}"
        if stub.is_dir() and not stub.is_symlink():
            problems.append(f"{file}: a folder among the stubs")
            continue
        if not stub.is_symlink():
            held = read_text(stub).strip()
            if "\n" in held or not held.startswith("../"):
                problems.append(f"{file}: a stub is a link to a file elsewhere under docs/, and this holds content")
                continue
        if not resolve_stub(stub, stubs).is_file():
            problems.append(f"{file}: leads to no file")
        if f"`{file}`" not in readme:
            problems.append(f"docs/stubs/README.md: does not list {file}")
        mode = subprocess.run(["git", "-C", str(repo), "ls-files", "-s", "--", file], capture_output=True, text=True).stdout
        if not mode.startswith("120000 "):
            problems.append(f"{file}: git does not hold it as a link, see docs/stubs/README.md")


class SchemaError(Exception):
    pass


def validate(value, schema, root) -> None:
    """The part of JSON Schema 2020-12 that effect.schema.json uses; a keyword not known here is refused."""
    if schema is True:
        return
    if schema is False:
        raise SchemaError("nothing is allowed here")
    known = {"$schema", "$id", "title", "description", "$defs", "format", "$ref", "type", "enum", "const", "pattern",
             "minimum", "maximum", "minItems", "maxItems", "uniqueItems", "items", "prefixItems", "properties",
             "required", "additionalProperties", "oneOf", "if", "then", "else"}
    unknown = set(schema) - known
    if unknown:
        raise Refusal(f"effect.schema.json uses {', '.join(sorted(unknown))}, which deploy/lib/csg_rules.py does not check")
    if "$ref" in schema:
        target = root
        for part in schema["$ref"].lstrip("#/").split("/"):
            target = target[part]
        validate(value, target, root)
    if "type" in schema:
        kinds = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
        if not any(is_type(value, kind) for kind in kinds):
            raise SchemaError(f"{value!r} is not {schema['type']}")
    if "enum" in schema and not any(same(value, option) for option in schema["enum"]):
        raise SchemaError(f"{value!r} is none of {schema['enum']}")
    if "const" in schema and not same(value, schema["const"]):
        raise SchemaError(f"{value!r} is not {schema['const']!r}")
    if isinstance(value, str) and "pattern" in schema and not re.search(schema["pattern"], value):
        raise SchemaError(f"{value!r} does not match {schema['pattern']}")
    if is_type(value, "number"):
        if "minimum" in schema and value < schema["minimum"]:
            raise SchemaError(f"{value} is below {schema['minimum']}")
        if "maximum" in schema and value > schema["maximum"]:
            raise SchemaError(f"{value} is above {schema['maximum']}")
    if isinstance(value, list):
        if len(value) < schema.get("minItems", 0) or len(value) > schema.get("maxItems", len(value)):
            raise SchemaError(f"a list of {len(value)} items")
        if schema.get("uniqueItems") and any(same(a, b) for i, a in enumerate(value) for b in value[i + 1:]):
            raise SchemaError("a list with an item twice")
        prefix = schema.get("prefixItems", [])
        for item, item_schema in zip(value, prefix):
            validate(item, item_schema, root)
        if "items" in schema:
            for item in value[len(prefix):]:
                validate(item, schema["items"], root)
    if isinstance(value, dict):
        for name in schema.get("required", []):
            if name not in value:
                raise SchemaError(f"no {name}")
        properties = schema.get("properties", {})
        for name, item in value.items():
            if name in properties:
                validate(item, properties[name], root)
            elif "additionalProperties" in schema:
                validate(item, schema["additionalProperties"], root)
    if "oneOf" in schema:
        passed = 0
        for option in schema["oneOf"]:
            try:
                validate(value, option, root)
                passed += 1
            except SchemaError:
                pass
        if passed != 1:
            raise SchemaError(f"{passed} of the oneOf choices fit")
    if "if" in schema:
        try:
            validate(value, schema["if"], root)
            branch = schema.get("then", True)
        except SchemaError:
            branch = schema.get("else", True)
        validate(value, branch, root)


def is_type(value, kind: str) -> bool:
    if kind == "integer":
        return not isinstance(value, bool) and (isinstance(value, int) or (isinstance(value, float) and value.is_integer()))
    if kind == "number":
        return not isinstance(value, bool) and isinstance(value, (int, float))
    return {"string": str, "boolean": bool, "array": list, "object": dict}.get(kind, type(None)) is type(value) or (kind == "null" and value is None)


def same(a, b) -> bool:
    if is_type(a, "number") and is_type(b, "number"):
        return a == b
    return type(a) is type(b) and a == b


def table_names(guide: str, section: str, cell: int, problems: list[str], guide_path: str) -> list[str]:
    """The names in one cell of the rows of one section, as a name in passing elsewhere describes nothing."""
    start = guide.find(f"\n{section}\n")
    if start < 0:
        problems.append(f"{guide_path}: no heading '{section}'")
        return []
    following = re.search(r"(?m)^#{1,3} ", guide[start + len(section) + 2:])
    body = guide[start:start + len(section) + 2 + following.start()] if following else guide[start:]
    names = []
    for row in re.finditer(r"(?m)^\|.*$", body):
        cells = row.group(0).split("|")
        if len(cells) > cell:
            names.extend(re.findall(r"`([a-z0-9-]+)`", cells[cell]))
    return names


def check_content(repo: Path, problems: list[str]) -> None:
    partitions = read_text(repo / "firmware/app/partitions.csv")
    sizes = []
    for pattern, text in (
        (r"(?m)^pack_a\s*,[^,]*,[^,]*,[^,]*,\s*(0x[0-9A-Fa-f]+)", partitions),
        (r"(?m)^pack_b\s*,[^,]*,[^,]*,[^,]*,\s*(0x[0-9A-Fa-f]+)", partitions),
        (r"(?m)^SLOT_BYTES = (0x[0-9A-Fa-f]+)", read_text(repo / "firmware/app/scripts/effect_pack_tool.py")),
        (r"kPackSlotBytes = (0x[0-9A-Fa-f]+)", read_text(repo / "firmware/app/src/effect_pack.h")),
    ):
        found = re.search(pattern, text)
        sizes.append(found.group(1) if found else "")
    if len({size.lower() for size in sizes}) != 1 or not sizes[0]:
        problems.append(f"firmware/app/partitions.csv: the pack's slot differs from effect_pack_tool.py or effect_pack.h: {', '.join(sizes)}")

    guide_path = "docs/guides/effects.md"
    guide = read_text(repo / guide_path)
    catalog = json.loads(read_text(repo / "firmware/app/effects/catalog.json"))
    listed_ids = [entry["id"] for entry in catalog["effects"]]
    algorithms = re.findall(r"\{\"([a-z0-9-]+)\", Algorithm::", read_text(repo / "firmware/app/src/effect_parser.cpp"))
    if not algorithms:
        problems.append("firmware/app/src/effect_parser.cpp: no algorithms found to check")
    in_guide = table_names(guide, "### The effects, in catalog order", 2, problems, guide_path)
    for name in listed_ids:
        if name not in in_guide:
            problems.append(f"{guide_path}: no row in the catalog for `{name}`")
    algorithms_in_guide = table_names(guide, "## 8. Algorithms", 1, problems, guide_path)
    for name in algorithms:
        if name not in algorithms_in_guide:
            problems.append(f"{guide_path}: no row in the algorithms for `{name}`")

    schema_path = "firmware/app/effects/effect.schema.json"
    schema = json.loads(read_text(repo / schema_path))
    named = schema["$defs"]["layer"]["properties"]["fx"]["enum"]
    for name in [n for n in algorithms if n.lower() not in {x.lower() for x in named}] + [n for n in named if n.lower() not in {x.lower() for x in algorithms}]:
        problems.append(f"{schema_path}: differs from the parser about {name}")
    for entry in catalog["effects"]:
        effect = repo / f"firmware/app/effects/{entry['id']}.json"
        if entry.get("todo"):
            if effect.exists():
                problems.append(f"firmware/app/effects/{entry['id']}.json: its catalog entry is marked todo")
        elif not effect.exists():
            problems.append(f"firmware/app/effects/{entry['id']}.json: missing, and its catalog entry is not marked todo")
        else:
            try:
                validate(json.loads(read_text(effect)), schema, schema)
            except (SchemaError, ValueError):
                problems.append(f"firmware/app/effects/{entry['id']}.json: does not pass the schema")

    # The utility's engine builds with a copy of the cJSON the firmware's component lock brings in.
    component = repo / "firmware/app/managed_components/espressif__cjson/cJSON"
    for name in ("cJSON.c", "cJSON.h"):
        locked = component / name
        if locked.exists() and locked.read_bytes() != (repo / "internal/engine" / name).read_bytes():
            problems.append(f"internal/engine/{name}: differs from the firmware's locked cJSON; copy it from {locked}")

    # The site's Russian pages are made from its English ones, and are held to what they would make now.
    try:
        csg_site.run(repo, check=True, quiet=True)
    except Refusal as refusal:
        problems.append(f"site/ru: {refusal}")


def main(argv: list[str]) -> int:
    repo = Path(argv[0])
    problems: list[str] = []
    files = listed(repo)
    sources = check_files(repo, files, problems)
    check_documents(repo, files, problems)
    check_content(repo, problems)
    if problems:
        for problem in problems:
            sys.stdout.write(f"{problem}\n")
        raise Refusal(f"{len(problems)} source rule problems.")
    sys.stderr.write(f"Source rules hold for {len(sources)} files.\n")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Refusal as refusal:
        sys.stderr.write(f"{refusal}\n")
        sys.exit(1)
