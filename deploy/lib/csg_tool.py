# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: What the bash scripts of deploy/ cannot do alike on Linux and macOS: digests, JSON, binary formats.
# Contributors: None
#
# Called by deploy/lib/common.sh as `csg_tool <command> [args]`, under any python3 from 3.8 on, with
# nothing installed. Each command does what a function of the PowerShell modules does, and gives the same
# answer to the byte: the bash and PowerShell scripts of one checkout record the same digests. A command
# that fails says why on stderr and exits non-zero; one that answers "no" exits 1 with nothing said.
from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from collections import OrderedDict
from pathlib import Path

# How source_sha256 is worked out, which a manifest records beside it: as BuildVersion.psm1 has it.
SOURCE_FORMAT = "raw-blobs/1"
# What the firmware's build writes out again with this machine's line endings: taken as git stores it.
REWRITTEN_BY_BUILD = ("firmware/app/dependencies.lock",)
# The build counters, which git keeps so that a clone on another machine goes on from the last number:
# each build moves its own, so neither is a source of what is built, in a digest or in a manifest's dirty.
BUILD_COUNTERS = ("firmware/app/.csigarland_build_counter", "cmd/csigarland/.csigarland_build_counter")
# Everything that goes into the utility, the scripts that build it among it. A path not in a tag adds
# nothing to its digest, so the bash scripts named here leave the digests of earlier releases as they were.
UTILITY_PATHS = (
    "cmd", "internal", "firmware/app/effects", "firmware/app/src", "go.mod", "go.sum",
    "deploy/build-utility.ps1", "deploy/BuildVersion.psm1", "deploy/new-third-party-notices.ps1", "deploy/licences",
    "deploy/build-utility.sh", "deploy/new-third-party-notices.sh", "deploy/lib/common.sh",
    "deploy/lib/build_version.sh", "deploy/lib/csg_tool.py", "deploy/lib/csg_notices.py",
)
RELEASE_COMPILERS = {
    "windows-amd64": "gcc.exe (MinGW-W64 x86_64-ucrt-posix-seh, built by Brecht Sanders, r2) 16.1.0",
    # Ubuntu 24.04's, as the container of docs/guides/utility.md has it on Windows and Ubuntu itself on Linux.
    "linux-amd64": "gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0",
}
UTILITY_PLATFORMS = OrderedDict([
    ("windows-amd64", "Windows x64"),
    ("linux-amd64", "Linux x64"),
    ("linux-arm64", "Linux ARM64"),
    ("darwin-amd64", "macOS Intel"),
    ("darwin-arm64", "macOS Apple silicon"),
])
RELEASE_CGO_FLAGS = OrderedDict([
    ("CGO_CFLAGS", "-O2 -g"), ("CGO_CPPFLAGS", ""), ("CGO_CXXFLAGS", "-O2 -g"), ("CGO_LDFLAGS", "-O2 -g"),
])
RELEASE_AMD64 = "v1"
UTILITY_LIBRARY_GROUPS = {
    "libstdc++.a": "gcc", "libgcc.a": "gcc", "libgcc_eh.a": "gcc",
    "libpthread.a": "winpthreads", "libwinpthread.a": "winpthreads",
    "libmingw32.a": "mingw-w64", "libmingwex.a": "mingw-w64", "libmoldname.a": "mingw-w64",
    "libmsvcrt.a": "mingw-w64", "libucrt.a": "mingw-w64", "libucrtbase.a": "mingw-w64",
    "libkernel32.a": "mingw-w64", "libuser32.a": "mingw-w64", "libadvapi32.a": "mingw-w64",
    "libshell32.a": "mingw-w64", "libws2_32.a": "mingw-w64",
}

# An ESP32 app image: 24-byte file header, 8-byte segment header, then esp_app_desc_t.
APP_DESC_MAGIC = bytes((0x32, 0x54, 0xCD, 0xAB))
# The update file's header, as firmware/app/src/ota_logic.h reads it.
OTA_HEADER_BYTES = 148
OTA_FORMAT = 3


class Refusal(Exception):
    """A command's answer that something is wrong, said on stderr."""


# PowerShell sorts with the invariant culture, which .NET takes from ICU's root collation: punctuation
# before digits before letters, letters by their kind first and their case last, lower before upper.
# Only what a path here holds is placed, and anything else is refused rather than placed wrong.
_PUNCTUATION = "_-,;:!?.'\"()[]{}@*/\\&#%`^+<=>|~$"


def _primary(ch: str) -> tuple:
    if ch == " ":
        return (0, 0)
    if ch in _PUNCTUATION:
        return (1, _PUNCTUATION.index(ch))
    if "0" <= ch <= "9":
        return (2, ord(ch))
    if "a" <= ch.lower() <= "z":
        return (3, ord(ch.lower()))
    raise Refusal(f"{ch!r} has no place in the order PowerShell sorts by, as this tool knows it")


def culture_key(text: str, case_sensitive: bool = True) -> tuple:
    """The key Sort-Object -Culture '' sorts by: -CaseSensitive orders by case after all else."""
    primary = tuple(_primary(ch) for ch in text)
    if not case_sensitive:
        return (primary,)
    return (primary, tuple(1 if ch.isupper() else 0 for ch in text))


def sha256_file(path: str | Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def _git(repo: str, *args: str, data: bytes | None = None) -> bytes:
    done = subprocess.run(["git", "-C", repo, *args], input=data, capture_output=True)
    if done.returncode != 0:
        raise Refusal(f"git {args[0]} failed with exit code {done.returncode} in {repo}: {done.stderr.decode(errors='replace').strip()}")
    return done.stdout


def tree_digest(repo: str, paths: list[str], revision: str = "") -> str:
    """Get-csiGarlandTreeDigest: each file git would keep under paths, by the blob of its bytes, but the
    build counters, so a digest is what it was when git ignored them."""
    if revision:
        listed = _git(repo, "ls-tree", "-r", "-z", "--full-tree", revision, "--", *paths)
        held = []
        for record in listed.split(b"\0"):
            if not record:
                continue
            meta, path = record.split(b"\t", 1)
            _mode, kind, blob = meta.split(b" ")
            if kind == b"blob" and path.decode("utf-8") not in BUILD_COUNTERS:
                held.append((path.decode("utf-8"), blob.decode("ascii")))
        if not held:
            raise Refusal(f"{revision} holds no files of {', '.join(paths)}.")
        held.sort(key=lambda item: culture_key(item[0]))
        lines = [f"{path} {blob}\n" for path, blob in held]
        return hashlib.sha256("".join(lines).encode("utf-8")).hexdigest()
    listed = _git(repo, "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", *paths)
    files = sorted((name.decode("utf-8") for name in listed.split(b"\0") if name), key=culture_key)
    files = [name for name in files if name not in BUILD_COUNTERS and (Path(repo) / name).is_file()]
    if not files:
        raise Refusal(f"git ls-files listed no files of {', '.join(paths)} in {repo}.")
    blobs = _git(repo, "hash-object", "--no-filters", "--stdin-paths", data=("\n".join(files) + "\n").encode("utf-8")).decode().split()
    if len(blobs) != len(files):
        raise Refusal(f"git hash-object gave {len(blobs)} blobs for {len(files)} files in {repo}.")
    for index, name in enumerate(files):
        if name in REWRITTEN_BY_BUILD:
            blobs[index] = _git(repo, "hash-object", "--", name).decode().strip()
    lines = [f"{name} {blob}\n" for name, blob in zip(files, blobs)]
    return hashlib.sha256("".join(lines).encode("utf-8")).hexdigest()


def engine_digest(folder: str) -> str:
    """Set-csiGarlandEngineSources: the engine's sources, by name and bytes, in the order PowerShell lists them.

    Go keys its cache, and the utility's build ID, on the flags this goes into, so a build here and one by
    the PowerShell script of the same sources make the same file only if both work it out alike.
    """
    root = Path(folder)
    files = [path for path in root.rglob("*") if path.is_file()]
    # By the path below the folder: PowerShell sorts the whole paths, whose common start, wherever the clone
    # is and whatever letters it holds, orders nothing.
    files.sort(key=lambda path: culture_key(path.relative_to(root).as_posix(), case_sensitive=False))
    digest = hashlib.sha256()
    for path in files:
        digest.update(f"{path.name}\n".encode("utf-8"))
        digest.update(path.read_bytes())
    return digest.hexdigest()


def source_identity(repo: str) -> OrderedDict:
    """Get-csiGarlandSourceIdentity: what a firmware build is made from, taken before it starts."""
    commit = subprocess.run(["git", "-C", repo, "rev-parse", "HEAD"], capture_output=True)
    # A build counter moved by the build before is no change to the sources.
    changes = _git(repo, "status", "--porcelain", "--", ".", *(f":(exclude){counter}" for counter in BUILD_COUNTERS))
    return OrderedDict([
        ("commit", commit.stdout.decode().strip() if commit.returncode == 0 else None),
        ("dirty", bool(changes.strip())),
        ("source_sha256", tree_digest(repo, ["firmware/app"])),
        ("source_format", SOURCE_FORMAT),
    ])


def firmware_identity(ini: str, build: int, profile: str) -> str:
    """Get-csiGarlandFirmwareIdentity, spelled as firmware_build_version.py spells it."""
    pattern = re.compile(r"^\s*CSIGARLAND_FIRMWARE_BASE\s*=\s*(\d+\.\d+\.\d+)(?:-([0-9A-Za-z][0-9A-Za-z.-]*))?\s*$", re.I)
    for line in Path(ini).read_text(encoding="utf-8").splitlines():
        found = pattern.match(line)
        if found:
            version = f"{found.group(1)}.{build}"
            if found.group(2):
                version += f"-{found.group(2)}"
            if profile == "Dev":
                version += "-dev"
            return version
    raise Refusal(f"CSIGARLAND_FIRMWARE_BASE not found in {ini}")


def app_desc_version(data: bytes) -> str | None:
    if len(data) < 80 or data[32:36] != APP_DESC_MAGIC:
        return None
    end = 48
    while end < 80 and data[end] != 0:
        end += 1
    return data[48:end].decode("ascii", errors="replace")


def bin_identity_problem(path: str, version: str) -> str | None:
    if not Path(path).exists():
        return f"{path} does not exist"
    data = Path(path).read_bytes()
    described = app_desc_version(data)
    display = []
    for found in re.finditer(rb"FW (\d+\.\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?)\x00", data):
        value = found.group(1).decode("ascii")
        if value not in display:
            display.append(value)
    if described == version and display == [version]:
        return None
    return f"{path} is not {version}: esp_app_desc '{described or ''}', display FW {', '.join(display) or '(none)'}"


def image_id(path: str) -> str | None:
    data = Path(path).read_bytes()
    if app_desc_version(data) is None or len(data) < 208:
        return None
    return data[176:208].hex()


def ota_contents(data: bytes, path: str) -> OrderedDict:
    """Get-UpdateContents of ota-update.ps1: what an update file says it holds."""
    if len(data) < OTA_HEADER_BYTES or data[0:4] != b"CGOT":
        raise Refusal(f"{path} is not a csiGarland update file. Nothing was sent.")
    if int.from_bytes(data[4:8], "little") != OTA_FORMAT:
        raise Refusal(f"{path} is in a format this script does not read. Nothing was sent.")
    certificate, pack, image = (int.from_bytes(data[at:at + 4], "little") for at in (8, 12, 16))
    if len(data) != OTA_HEADER_BYTES + certificate + pack + image or pack + image == 0:
        raise Refusal(f"{path} is incomplete. Nothing was sent.")
    pack_start = OTA_HEADER_BYTES + certificate
    parts, image_hash, image_version = [], None, None
    if image > 0:
        start = pack_start + pack
        version = "of unknown version"
        if image >= 208 and data[start + 32:start + 36] == APP_DESC_MAGIC:
            version = data[start + 48:start + 80].split(b"\0")[0].decode("ascii", errors="replace")
            image_version = version
            image_hash = data[start + 176:start + 208].hex()
        parts.append(f"firmware {version}")
    if pack > 0:
        # Records after the pack's 12-byte header: a kind letter, then four NUL-ended strings.
        known = int.from_bytes(data[pack_start + 4:pack_start + 8], "little") == 2
        effects: int | str = 0 if known else "a newer set of"
        at, end = pack_start + 12, pack_start + pack
        while known and at < end:
            if data[at] != ord("p"):
                effects += 1  # type: ignore[operator]
            at += 1
            ended = 0
            while ended < 4 and at < end:
                if data[at] == 0:
                    ended += 1
                at += 1
        parts.append(f"{effects} effects")
    return OrderedDict([
        ("text", " and ".join(parts)),
        ("image", image_hash),
        ("pack_digest", data[20:52].hex() if pack > 0 else None),
        ("version", image_version),
    ])


def ota_package_problem(ota: str, binary: str, pack: str) -> str | None:
    """Get-PackageProblem of stage-assets.ps1: whether the update file is the signed form of the image."""
    if not Path(ota).exists():
        return f"{ota} does not exist"
    package, image, effects = Path(ota).read_bytes(), Path(binary).read_bytes(), Path(pack).read_bytes()
    if len(package) < OTA_HEADER_BYTES or package[0:4] != b"CGOT":
        return f"{ota} has no update header"
    pack_start = OTA_HEADER_BYTES + int.from_bytes(package[8:12], "little")
    image_start = pack_start + len(effects)
    if len(package) != image_start + len(image):
        return f"{ota} is {len(package)} bytes for effects of {len(effects)} and an image of {len(image)}"
    if hashlib.sha256(package[image_start:]).digest() != hashlib.sha256(image).digest():
        return f"{ota} holds a different image than {binary}"
    if hashlib.sha256(package[pack_start:image_start]).digest() != hashlib.sha256(effects).digest():
        return f"{ota} holds different effects than {pack}"
    return None


def partition_entry(table: bytes, label: str, kind: int, subtype: int, offset: int, size: int) -> bool:
    """Test-csiGarlandPartitionEntry: whether a partition table holds this entry exactly."""
    at = 0
    while at + 32 <= len(table) and table[at] == 0xAA and table[at + 1] == 0x50:
        if table[at + 12:at + 28].decode("ascii", errors="replace").rstrip("\0") == label:
            return (table[at + 2] == kind and table[at + 3] == subtype
                    and int.from_bytes(table[at + 4:at + 8], "little") == offset
                    and int.from_bytes(table[at + 8:at + 12], "little") == size)
        at += 32
    return False


def split_command(command: str) -> list[str]:
    """Split-csiGarlandCommand: a command as Go reads CC and CXX."""
    return [part.strip("\"'") for part in re.findall(r"\"[^\"]*\"|'[^']*'|\S+", command)]


def compiler_build(line: str) -> str:
    """Get-csiGarlandCompilerBuild: a compiler's --version line, less the name it was called by."""
    return re.sub(r"^\S+\s+", "", line.strip(), count=1)


def pem_sha256(path: str) -> str:
    import base64
    found = re.search(r"-----BEGIN CERTIFICATE-----(.+?)-----END CERTIFICATE-----", Path(path).read_text(encoding="utf-8"), re.S)
    if not found:
        raise Refusal(f"{path} holds no certificate.")
    return hashlib.sha256(base64.b64decode(re.sub(r"\s", "", found.group(1)))).hexdigest()


def dumps(value) -> str:
    """JSON as PowerShell's ConvertTo-Json writes it: two spaces, and the text as it is."""
    return json.dumps(value, indent=2, ensure_ascii=False) + "\n"


def _load(path: str):
    text = sys.stdin.read() if path == "-" else Path(path).read_text(encoding="utf-8-sig")
    return json.loads(text, object_pairs_hook=OrderedDict)


def _lookup(value, dotted: str):
    for key in dotted.split(".") if dotted else []:
        if isinstance(value, list):
            value = value[int(key)]
        elif isinstance(value, dict) and key in value:
            value = value[key]
        else:
            raise KeyError(dotted)
    return value


def _text(value) -> str:
    if value is None:
        return ""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (dict, list)):
        return json.dumps(value, ensure_ascii=False)
    return str(value)


def json_build(args: list[str]) -> OrderedDict:
    """An object from --base FILE, then --str KEY VALUE, --raw KEY JSON, --bool KEY true|false, --null KEY
    and --copy KEY FILE PATH, which takes a value of another file's as it is, null and true among them."""
    result: OrderedDict = OrderedDict()
    at = 0
    while at < len(args):
        flag = args[at]
        if flag == "--base":
            result.update(_load(args[at + 1]))
            at += 2
        elif flag == "--str":
            result[args[at + 1]] = args[at + 2]
            at += 3
        elif flag == "--raw":
            result[args[at + 1]] = json.loads(args[at + 2], object_pairs_hook=OrderedDict)
            at += 3
        elif flag == "--bool":
            result[args[at + 1]] = args[at + 2] == "true"
            at += 3
        elif flag == "--copy":
            try:
                result[args[at + 1]] = _lookup(_load(args[at + 2]), args[at + 3])
            except (KeyError, IndexError, ValueError):
                result[args[at + 1]] = None
            at += 4
        elif flag == "--null":
            result[args[at + 1]] = None
            at += 2
        else:
            raise Refusal(f"json-build does not take {flag}")
    return result


def hold_lock(path: str, wait: float, status: str, owner: int) -> int:
    """Holds an exclusive flock on path for as long as the shell owner that started this process lives, which
    the kernel lets go of when this process ends, however it ends: no lock outlives its holder, so none is
    ever taken over. Waits up to wait seconds for another holder, and says held or busy in the status file,
    written whole.

    The owner is this process's parent, named by the shell before it started this: a parent that is not it,
    now or later, as when the shell ended and init took this over, is the shell gone, and nothing is held for
    it. A signal to the whole group, a Ctrl+C or a stop, leaves this holding while the shell gives back what
    it took under the lock; the shell lets go by SIGUSR1, or by ending."""
    import fcntl
    import os
    import signal
    import time

    for passed_by in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(passed_by, signal.SIG_IGN)

    def let_go(_number, _frame) -> None:
        raise SystemExit(0)
    signal.signal(signal.SIGUSR1, let_go)
    if os.getppid() != owner:
        return 2

    def say(word: str) -> None:
        partial = f"{status}.new"
        with open(partial, "w", encoding="utf-8") as stream:
            stream.write(word + "\n")
        os.replace(partial, status)

    stream = open(path, "a+b")
    deadline = time.monotonic() + wait
    while True:
        try:
            fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            break
        except BlockingIOError:
            if time.monotonic() >= deadline:
                say("busy")
                return 1
            time.sleep(0.25)
    say("held")
    # Until the shell that asked is gone, or it lets go.
    while os.getppid() == owner:
        time.sleep(0.5)
    return 0


def _say(value) -> None:
    if value is not None:
        sys.stdout.write(f"{value}\n")


def main(argv: list[str]) -> int:
    if not argv:
        raise Refusal("csg_tool.py needs a command")
    command, args = argv[0], argv[1:]
    if command == "sha256":
        upper = args[:1] == ["--upper"]
        for path in args[1:] if upper else args:
            digest = sha256_file(path)
            _say(digest.upper() if upper else digest)
    elif command == "sha256-text":
        _say(hashlib.sha256(sys.stdin.buffer.read()).hexdigest())
    elif command == "file-size":
        _say(Path(args[0]).stat().st_size)
    elif command == "tree-digest":
        revision = ""
        if args[:1] == ["--revision"]:
            revision, args = args[1], args[2:]
        _say(tree_digest(args[0], args[1:], revision))
    elif command == "utility-digest":
        _say(tree_digest(args[0], list(UTILITY_PATHS), args[2] if args[1:2] == ["--revision"] else ""))
    elif command == "engine-digest":
        _say(engine_digest(args[0]))
    elif command == "source-identity":
        sys.stdout.write(dumps(source_identity(args[0])))
    elif command == "source-format":
        _say(SOURCE_FORMAT)
    elif command == "firmware-identity":
        _say(firmware_identity(args[0], int(args[1]), args[2]))
    elif command == "app-desc-version":
        _say(app_desc_version(Path(args[0]).read_bytes()))
    elif command == "image-id":
        _say(image_id(args[0]))
    elif command == "bin-identity-problem":
        _say(bin_identity_problem(args[0], args[1]))
    elif command == "ota-contents":
        data = sys.stdin.buffer.read() if args[0] == "-" else Path(args[0]).read_bytes()
        sys.stdout.write(dumps(ota_contents(data, args[1] if len(args) > 1 else args[0])))
    elif command == "ota-package-problem":
        _say(ota_package_problem(args[0], args[1], args[2]))
    elif command == "partition-entry":
        fits = partition_entry(Path(args[0]).read_bytes(), args[1], int(args[2], 0), int(args[3], 0), int(args[4], 0), int(args[5], 0))
        return 0 if fits else 1
    elif command == "identity-hash":
        _say(hashlib.sha256(args[0].strip().upper().encode("utf-8")).hexdigest().upper())
    elif command == "split-command":
        for part in split_command(args[0]):
            _say(part)
    elif command == "compiler-build":
        _say(compiler_build(args[0]))
    elif command == "pem-sha256":
        _say(pem_sha256(args[0]))
    elif command == "json-get":
        try:
            _say(_text(_lookup(_load(args[0]), args[1])))
        except (KeyError, IndexError, ValueError):
            if len(args) > 2:
                _say(args[2])
            else:
                return 1
    elif command == "json-len":
        value = _lookup(_load(args[0]), args[1]) if len(args) > 1 else _load(args[0])
        _say(len(value))
    elif command == "json-build":
        sys.stdout.write(dumps(json_build(args)))
    elif command == "mod-replaces":
        # go mod edit -json on stdin: each replacement, as build-utility.ps1 says it.
        for item in _load("-").get("Replace") or []:
            new = item["New"]
            _say(f"{item['Old']['Path']} => {new['Path']}" + (f" {new['Version']}" if new.get("Version") else ""))
    elif command == "link-map-libraries":
        # The compiler's libraries a GNU ld map says the link took members of, each once.
        found = re.findall(r"(?m)^(\S[^\r\n(]*?\.a)\(", Path(args[0]).read_text(encoding="utf-8", errors="replace"))
        for name in sorted({re.split(r"[\\/]", path)[-1] for path in found}):
            _say(name)
    elif command == "lock-hold":
        return hold_lock(args[0], float(args[1]), args[2], int(args[3]))
    elif command == "json-check":
        _load(args[0])
    elif command == "const":
        tables = {"release-compiler": RELEASE_COMPILERS, "platform-name": UTILITY_PLATFORMS,
                  "library-group": UTILITY_LIBRARY_GROUPS, "cgo-flag": RELEASE_CGO_FLAGS}
        if args[0] == "release-amd64":
            _say(RELEASE_AMD64)
        elif args[0] == "platforms":
            for key in UTILITY_PLATFORMS:
                _say(key)
        elif args[0] == "cgo-flags":
            for key in RELEASE_CGO_FLAGS:
                _say(key)
        elif args[0] in tables:
            if args[1] not in tables[args[0]]:
                return 1
            _say(tables[args[0]][args[1]])
        else:
            raise Refusal(f"no constant {args[0]}")
    else:
        raise Refusal(f"csg_tool.py has no command {command}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Refusal as refusal:
        sys.stderr.write(f"{refusal}\n")
        sys.exit(2)
