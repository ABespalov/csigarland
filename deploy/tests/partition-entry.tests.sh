#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Holds the check before writing effects alone to what a partition table has to say.
# Contributors: None
#
# The bash twin of deploy/tests/partition-entry.tests.ps1, for Linux and macOS: the same cases, put to
# the partition-entry command of deploy/lib/csg_tool.py that deploy/flash-device.sh asks.
#
# Example:
#   bash deploy/tests/partition-entry.tests.sh
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)/lib/common.sh"

csg_temp_dir work partition-entry
# A table of the entries given, each label type subtype offset size, as the board keeps one: 32 bytes
# an entry, the last four its flags.
table() {
    local name="$1"
    shift
    csg_python -c '
import struct, sys
entries = sys.argv[2:]
table = b""
for at in range(0, len(entries), 5):
    label, kind, subtype, offset, size = entries[at:at + 5]
    table += struct.pack("<BBBBII16sI", 0xAA, 0x50, int(kind, 0), int(subtype, 0), int(offset, 0), int(size, 0), label.encode(), 0)
open(sys.argv[1], "wb").write(table or bytes(64))
' "$work/$name" "$@"
}
failed=0
cases=0
expect() {
    local name="$1" wanted="$2" result=no
    cases=$((cases + 1))
    if csg_tool partition-entry "$work/$name" pack_a 1 0x40 0x3E0000 0x8000; then result=yes; fi
    if [ "$result" != "$wanted" ]; then
        csg_say "FAILED: $name"
        failed=$((failed + 1))
    fi
}
table laid-out pack_a 1 0x40 0x3E0000 0x8000 && expect laid-out yes
table too-small pack_a 1 0x40 0x3E0000 0x4000 && expect too-small no
table subtype pack_a 1 0x81 0x3E0000 0x8000 && expect subtype no
table application pack_a 0 0x40 0x3E0000 0x8000 && expect application no
table place pack_a 1 0x40 0x3E8000 0x8000 && expect place no
table label pack_b 1 0x40 0x3E0000 0x8000 && expect label no
table second nvs 1 2 0x9000 0x6000 pack_a 1 0x40 0x3E0000 0x8000 && expect second yes
table none && expect none no
[ "$failed" -eq 0 ] || csg_die "$failed partition entry cases went wrong."
csg_say "Partition entries are judged as they should be in $cases cases."
