#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Holds the bash scripts' library and Python helper to what the PowerShell modules work out.
# Contributors: None
#
# The digests of release 1.1.0, as the PowerShell scripts recorded them in its manifests, worked out again
# from its tag; the order PowerShell sorts paths in; an update file's contents; JSON as ConvertTo-Json
# writes it; and the library's own functions: locks, the choice of a board, counting and trimming. Needs
# no board, no network and no build. A clone without the tag v1.1.0 skips the digests. It has no PowerShell
# twin, as it holds the bash library itself to the PowerShell modules, whose recorded digests it checks.
#
# Example:
#   bash deploy/tests/shell-lib.tests.sh
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)/lib/common.sh"
. "$CSG_LIB_DIR/build_version.sh"
. "$CSG_LIB_DIR/device_detection.sh"
. "$CSG_LIB_DIR/device_monitor.sh"

[ "$#" -eq 0 ] || csg_die "shell-lib.tests.sh takes no arguments."
failed=0
cases=0
check() {
    cases=$((cases + 1))
    if [ "$2" != "$3" ]; then
        csg_say "FAILED: $1"
        csg_say "  got:  $2"
        csg_say "  want: $3"
        failed=$((failed + 1))
    fi
}
csg_temp_dir work shell-lib

# What the PowerShell scripts recorded for release 1.1.0, in its manifests on GitHub.
if git -C "$CSG_REPO_ROOT" rev-parse --verify --quiet 'refs/tags/v1.1.0' >/dev/null; then
    check 'the firmware digest of 1.1.0' "$(csg_tool tree-digest --revision v1.1.0 "$CSG_REPO_ROOT" firmware/app)" \
        da153d5b04ff9faab07928a91327a3377e2357f40a847382fe4e5fcc33c1158b
    check 'the utility digest of 1.1.0' "$(csg_utility_source_digest "$CSG_REPO_ROOT" v1.1.0)" \
        2633a2a0889f4441d3c7a5acef463817e5fd6f30d69ff00bb05452f15d8c7173
else
    csg_say 'No tag v1.1.0 here: the digests of a release were not checked.'
fi

# The order of Sort-Object -Culture '' -CaseSensitive, and of a plain Sort-Object for the engine's sources.
order="$(csg_python -c 'import sys
sys.path.insert(0, sys.argv[1])
from csg_tool import culture_key
names = ["secret_linux.go", "secret.go", "CMakeLists.txt", "buttons.h", "effect_pack.h", "effect_pack_store.cpp", "a-b", "a_b", "ab", "Ab", "a1"]
print(" ".join(sorted(names, key=culture_key)))' "$CSG_LIB_DIR")"
check 'the culture order' "$order" 'a_b a-b a1 ab Ab buttons.h CMakeLists.txt effect_pack_store.cpp effect_pack.h secret_linux.go secret.go'

# An update file of a firmware and three effects, one of them a palette, as the header lays it out.
csg_python -c 'import struct, sys
image = bytearray(256)
image[32:36] = bytes((0x32, 0x54, 0xCD, 0xAB))
image[48:57] = b"1.1.0.200"
image[176:208] = bytes(range(32))
pack = struct.pack("<4sII", b"PACK", 2, 0) + b"e" + b"a\0b\0c\0d\0" + b"p" + b"a\0b\0c\0d\0" + b"e" + b"a\0b\0c\0d\0"
header = b"CGOT" + struct.pack("<IIII", 3, 0, len(pack), len(image)) + bytes(range(32)) + bytes(96)
open(sys.argv[1], "wb").write(header + pack + bytes(image))' "$work/update.ota"
contents="$(csg_tool ota-contents "$work/update.ota")"
check 'what an update file holds' "$(printf '%s' "$contents" | csg_tool json-get - text)" 'firmware 1.1.0.200 and 2 effects'
check 'the image an update file holds' "$(printf '%s' "$contents" | csg_tool json-get - image)" 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
printf 'XXXX' >"$work/not.ota"
check 'a file that is no update' "$(csg_tool ota-contents "$work/not.ota" 2>&1 || true)" "$work/not.ota is not a csiGarland update file. Nothing was sent."

# JSON as ConvertTo-Json writes it, and a value taken back with its type.
built="$(csg_tool json-build --str a 'x "y"' --raw b '[1, 2]' --bool c true --null d --raw e '{}')"
check 'JSON as PowerShell writes it' "$built" "$(printf '{\n  "a": "x \\"y\\"",\n  "b": [\n    1,\n    2\n  ],\n  "c": true,\n  "d": null,\n  "e": {}\n}')"
check 'a field of JSON' "$(printf '%s' "$built" | csg_tool json-get - b.1)" 2
check 'a missing field with a default' "$(printf '%s' "$built" | csg_tool json-get - nope fallback)" fallback

check 'a firmware identity' "$(csg_firmware_identity "$CSG_REPO_ROOT/firmware/app/platformio.ini" 7 Dev | sed 's/^[0-9]*\.[0-9]*\.[0-9]*//')" '.7-dev'
check 'a command as Go splits it' "$(csg_tool split-command 'gcc -m64 "a b"' | tr '\n' '|')" 'gcc|-m64|a b|'
check 'a compiler build' "$(csg_compiler_build 'gcc.exe (MinGW-W64) 16.1.0')" '(MinGW-W64) 16.1.0'
check 'an identity hash' "$(csg_identity_hash ' 40:4c:ca:01:02:03 ')" "$(printf '40:4C:CA:01:02:03' | csg_tool sha256-text | tr '[:lower:]' '[:upper:]')"
check 'lines counted' "$(csg_count_lines "$(printf 'a\nb\nc')")" 3
check 'no lines counted' "$(csg_count_lines '')" 0
check 'a first line' "$(csg_first_line printf 'one\ntwo\n')" one
check 'a trim' "[$(csg_trim $'  a b \n')]" '[a b]'
check 'a profile in any case' "$(csg_profile dEv)" Dev
check 'a port by name' "$(csg_port_path ttyACM0)" /dev/ttyACM0

# The board to write to: the enrolled one alone, and on the port asked for.
tab="$(printf '\t')"
boards="/dev/ttyACM0${tab}303A${tab}1001${tab}AAA"$'\n'"/dev/ttyACM1${tab}303A${tab}1001${tab}BBB"
check 'the enrolled board' "$(csg_select_flash_target "$boards" 303a 1001 bbb)" /dev/ttyACM1
check 'no board on the port asked for' "$( (csg_select_flash_target "$boards" 303A 1001 BBB /dev/ttyACM0 2>/dev/null) || printf refused)" refused
twice="$boards"$'\n'"/dev/ttyACM2${tab}303A${tab}1001${tab}BBB"
check 'two boards of one identity' "$( (csg_select_flash_target "$twice" 303A 1001 BBB 2>/dev/null) || printf refused)" refused
check 'the enrolled board to read' "$(csg_select_monitor_candidates "$boards" '' BBB)" /dev/ttyACM1
check 'every board to read' "$(csg_select_monitor_candidates "$boards" '' CCC | tr '\n' ' ')" '/dev/ttyACM0 /dev/ttyACM1 '

# A lock: taken and let go, refused while another holds it, free again as soon as a holder is killed, and
# waited for while it is held. Held, here, by a script of its own, as another build would hold it.
CSG_TEMP="$work/locks"
csg_lock test
check 'a lock taken' "$(if csg_lock_held test; then printf held; fi)" held
csg_unlock test
check 'a lock let go' "$(if csg_lock_held test; then printf held; else printf free; fi)" free
# A script that takes the lock, waiting a moment for one let go, and holds it for the seconds given. Waited
# for a while, and given up on, rather than hung on, should it never hold it.
hold() {
    local waited=0
    bash -c '. "$1/common.sh"; CSG_TEMP="$2"; csg_lock test 5; printf held >"$2/holding"; sleep "$3"' _ "$CSG_LIB_DIR" "$CSG_TEMP" "$1" &
    holder=$!
    while [ ! -f "$CSG_TEMP/holding" ] && [ "$waited" -lt 100 ]; do
        sleep 0.1
        waited=$((waited + 1))
    done
    rm -f "$CSG_TEMP/holding"
}
hold 30
check 'a lock another holds' "$( (csg_lock test 0 2>/dev/null) || printf refused)" refused
kill -9 "$holder" 2>/dev/null || true
wait "$holder" 2>/dev/null || true
check 'a lock free once its holder is killed' "$( (csg_lock test 5 2>/dev/null && printf taken && csg_unlock test) || printf refused)" taken
hold 1
check 'a lock waited for' "$( (csg_lock test 10 2>/dev/null && printf taken && csg_unlock test) || printf refused)" taken
wait "$holder" 2>/dev/null || true

# The build counters, which git keeps, are no source: a build that moves one changes neither the digest
# nor dirty, and any other file does.
sample="$work/sample"
mkdir -p "$sample/firmware/app" "$sample/cmd/csigarland"
printf 'int main;\n' >"$sample/firmware/app/main.c"
printf '7\n' >"$sample/firmware/app/.csigarland_build_counter"
printf '3\n' >"$sample/cmd/csigarland/.csigarland_build_counter"
git -C "$sample" init --quiet
git -C "$sample" add --all
git -C "$sample" -c user.name=test -c user.email=test@example.com commit --quiet -m sample
clean="$(csg_tool tree-digest "$sample" firmware/app cmd)"
check 'a digest of the tag leaves the counters out' "$(csg_tool tree-digest --revision HEAD "$sample" firmware/app cmd)" "$clean"
printf '8\n' >"$sample/firmware/app/.csigarland_build_counter"
printf '4\n' >"$sample/cmd/csigarland/.csigarland_build_counter"
check 'a moved counter changes no digest' "$(csg_tool tree-digest "$sample" firmware/app cmd)" "$clean"
check 'a moved counter is not dirty' "$(csg_source_identity "$sample" | csg_tool json-get - dirty)" false
printf 'int main2;\n' >"$sample/firmware/app/main.c"
check 'a changed source is dirty' "$(csg_source_identity "$sample" | csg_tool json-get - dirty)" true

# The engine's digest is the same wherever the clone is, whatever letters its path holds.
for place in plain 'проект с пробелом'; do
    mkdir -p "$work/$place/src"
    printf 'int a;\n' >"$work/$place/src/b.cpp"
    printf 'int b;\n' >"$work/$place/src/A.h"
done
check 'an engine digest under any path' "$(csg_tool engine-digest "$work/проект с пробелом/src")" "$(csg_tool engine-digest "$work/plain/src")"

# A folder of this run's own goes when the script ends, wherever the clone is, a quote in its path and all.
quoted="$work/John's folder"
mkdir -p "$quoted"
left="$(bash -c '. "$1/common.sh"; CSG_TEMP="$2"; csg_temp_dir made scratch; printf "%s\n" "$made"' _ "$CSG_LIB_DIR" "$quoted" 2>&1)"
made=other
case "$left" in "$quoted"/scratch-*) made=made ;; esac
check 'a folder under a quote is made' "$made" made
check 'a folder under a quote is cleaned up' "$(if [ -e "$left" ]; then printf left; else printf gone; fi)" gone

# A shell killed before its holder has started leaves nothing held: the holder, started a moment late here
# by a Python that waits first, finds its owner gone and holds nothing for it.
printf '#!/bin/sh\nsleep 1\nexec "%s" "$@"\n' "$CSG_PYTHON" >"$work/late-python"
chmod +x "$work/late-python"
CSG_PYTHON="$work/late-python" bash -c '. "$1/common.sh"; CSG_TEMP="$2"; csg_lock early 10' _ "$CSG_LIB_DIR" "$CSG_TEMP" 2>/dev/null &
early=$!
sleep 0.3
kill -9 "$early" 2>/dev/null || true
wait "$early" 2>/dev/null || true
sleep 2
check 'a shell killed before its holder started holds nothing' "$( (csg_lock early 0 2>/dev/null && printf taken && csg_unlock early) || printf refused)" taken

# A stop sent to the whole group, a cancelled job's, leaves the lock held while the shell gives back what it
# took, and lets it go once that is done: the holder lets the stop pass, and goes when the shell lets it go.
bash -c '. "$1/common.sh"; CSG_TEMP="$2"; csg_lock term
    eval "printf %s \$CSG_LOCK_term" >"$2/term-holder"
    csg_on_exit "sleep 1; printf done >\"$2/term-cleaned\""
    sleep 30 & printf %s "$!" >"$2/term-sleep"; printf held >"$2/term-holding"; wait' _ "$CSG_LIB_DIR" "$CSG_TEMP" 2>/dev/null &
stopped=$!
waited=0
while [ ! -f "$CSG_TEMP/term-holding" ] && [ "$waited" -lt 100 ]; do sleep 0.1; waited=$((waited + 1)); done
kill -TERM "$stopped" "$(cat "$CSG_TEMP/term-holder")" "$(cat "$CSG_TEMP/term-sleep")" 2>/dev/null || true
sleep 0.3
check 'a lock still held through the cleanup after a stop' "$( (csg_lock term 0 2>/dev/null && printf taken && csg_unlock term) || printf refused)" refused
wait "$stopped" 2>/dev/null || true
check 'the cleanup after a stop ran' "$(cat "$CSG_TEMP/term-cleaned" 2>/dev/null || true)" done
check 'a lock let go once the cleanup is done' "$( (csg_lock term 2 2>/dev/null && printf taken && csg_unlock term) || printf refused)" taken

# A lock taken by several at once is held by one at a time: each that holds it marks so for a moment, and
# finds no other mark beside its own; each waits its turn.
for round in 1 2 3; do
    race="$work/race-$round"
    mkdir -p "$race"
    for worker in 1 2 3 4 5 6 7 8; do
        bash -c '. "$1/common.sh"; CSG_TEMP="$2"; csg_lock race 30 2>/dev/null || { printf "refused\n"; exit 0; }
            : >"$CSG_TEMP/held.$$"; sleep 0.05; set -- "$CSG_TEMP"/held.*
            if [ "$#" -gt 1 ]; then printf "overlap\n"; fi; rm -f "$CSG_TEMP/held.$$"' _ "$CSG_LIB_DIR" "$race" >>"$work/race.out" &
    done
    wait
done
check 'a lock taken by several at once is held by one at a time' "$(csg_count_lines "$(cat "$work/race.out" 2>/dev/null || true)")" 0

# A counter that goes back only from the number its build took.
printf '5\n' >"$work/counter"
if csg_undo_build_number "$work/counter" 4; then back=yes; else back=no; fi
check 'a number given back' "$back:$(cat "$work/counter")" yes:4
if csg_undo_build_number "$work/counter" 1; then back=yes; else back=no; fi
check 'a number another build moved' "$back:$(cat "$work/counter")" no:4

[ "$failed" -eq 0 ] || csg_die "$failed of $cases cases of the shell library went wrong."
csg_say "The shell library holds in $cases cases."
