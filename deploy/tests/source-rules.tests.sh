#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Checks the source rules of AGENTS.md that a machine can check.
# Contributors: None
#
# The bash twin of deploy/tests/source-rules.tests.ps1, for Linux and macOS, by deploy/lib/csg_rules.py.
# Looks at the files git tracks, and at new ones it would track, so it also works before a commit. Reads
# the syntax of every bash script with bash -n, and of the PowerShell ones only where pwsh is.
#
# Example:
#   bash deploy/tests/source-rules.tests.sh
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)/lib/common.sh"

[ "$#" -eq 0 ] || csg_die "source-rules.tests.sh takes no arguments."
csg_python "$CSG_LIB_DIR/csg_rules.py" "$CSG_REPO_ROOT"
