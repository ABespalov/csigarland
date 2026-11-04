# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Record the Release sdkconfig as made, in the same build whose configure made it.
# Contributors: None
#
# firmware_build_version.py, run before the build, removes a Release sdkconfig.<env> that is not what the
# defaults made, and marks one to be made. ESP-IDF makes it in the configure PlatformIO runs as it reads
# its builder, before this script, which runs after it: the file here is the one made from the defaults,
# and nothing else, not even menuconfig, can have come in between. Its digest is kept, and every later
# build holds the file to it. A mark still there at the next build says no configure took it, and the
# file is made again.

Import("env")

import hashlib
from pathlib import Path

_RELEASE_SUFFIX = "-release"
# The mark's format, which firmware_build_version.py reads: a mark of another, as of a build before the
# environment's own defaults were refused, is no mark, and the config is made again.
MADE_FORMAT = "made-from-defaults/2"


def record_made_sdkconfig() -> None:
    name = env.subst("$PIOENV")
    if not name.endswith(_RELEASE_SUFFIX):
        return
    project_dir = Path(env["PROJECT_DIR"])
    effective = project_dir / f"sdkconfig.{name}"
    pending = project_dir / "generated" / f"sdkconfig.{name}.pending"
    made = project_dir / "generated" / f"sdkconfig.{name}.made.sha256"
    if pending.is_file() and effective.is_file():
        made.write_text(f"{MADE_FORMAT} {hashlib.sha256(effective.read_bytes()).hexdigest()}\n", encoding="utf-8")
        pending.unlink()
        print(f"{effective.name} made from sdkconfig.defaults, and kept as made.")


record_made_sdkconfig()
