# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Make the effects pack at the start of every build.
# Contributors: None
#
# The work happens in effect_pack_tool.py, which also runs without PlatformIO: an update with
# only effects in it needs no firmware build.

Import("env")

import subprocess
import sys
from pathlib import Path

_PROJECT_DIR = Path(env["PROJECT_DIR"])

subprocess.run(
    [
        sys.executable,
        str(_PROJECT_DIR / "scripts" / "effect_pack_tool.py"),
        str(_PROJECT_DIR / "effects"),
        str(_PROJECT_DIR / "generated"),
    ],
    check=True,
)
