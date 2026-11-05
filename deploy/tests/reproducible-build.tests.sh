#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Build the firmware and the utility twice from copies of the sources, and show they are the same to the byte.
# Contributors: None
#
# The bash twin of deploy/tests/reproducible-build.tests.ps1, for Linux and macOS. Each copy holds only
# what git would keep, under temp/, at paths of different lengths, so that nothing of this tree and no
# path of this machine can carry over. The firmware of both is signed by one throwaway chain made for the
# run, and both take the same build number. What they make is compared file by file. It takes two full
# builds.
#
# Options
#
#   --build-number <n>   The number both builds take, 0 unless given.
#   --keep               Leave the copies under temp/r for a look at what differs.
#
# Example:
#   bash deploy/tests/reproducible-build.tests.sh
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)/lib/common.sh"
. "$CSG_LIB_DIR/build_version.sh"
. "$CSG_LIB_DIR/signing_python.sh"

build_number=0
keep=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-number)
            csg_need_value "$1" "$#"
            case "$2" in '' | *[!0-9]*) csg_die "--build-number is a number, not $2." ;; esac
            build_number="$2"
            shift
            ;;
        --keep) keep=1 ;;
        *) csg_die "reproducible-build.tests.sh does not take $1." ;;
    esac
    shift
done
csg_require pio 'PlatformIO (pio) is not on PATH.'
csg_require go 'Go is not on PATH.'
repo="$CSG_REPO_ROOT"
# Short, as a long path changes the debug info.
work="$repo/temp/r"
environment=esp32c6-release
made=(".pio/build/$environment/firmware.elf" ".pio/build/$environment/firmware.bin" ".pio/build/$environment/bootloader.bin"
    ".pio/build/$environment/partitions.bin" generated/effects.pack ".pio/build/$environment/firmware.ota"
    ".pio/build/$environment/firmware-only.ota" ".pio/build/$environment/effects.ota")
rm -rf "$work"
copies=("$work/a" "$work/bcd")

# One chain for both, made once: a chain made per build would sign each with its own key.
chain="$work/chain"
"$(csg_signing_python)" "$repo/firmware/app/scripts/ota_sign_tool.py" throwaway "$chain" >&2 || csg_die 'The throwaway chain was not made.'
export CSIGARLAND_THROWAWAY_SIGNING=1 CSIGARLAND_BUILD_NUMBER="$build_number"
# Both copies first, so that both hold the sources as they were at one moment.
apps=()
for copy in "${copies[@]}"; do apps[${#apps[@]}]="$(csg_copy_sources "$repo" "$copy")"; done
results=()
for n in 0 1; do
    copy="${copies[$n]}"
    app="${apps[$n]}"
    mkdir -p "$app/generated/throwaway-signing"
    cp "$chain"/* "$app/generated/throwaway-signing/"
    csg_say "Building in $copy"
    status=0
    pio run -d "$app" -e "$environment" >"$copy.firmware.log" 2>&1 || status=$?
    grep -E 'error|warning|SUCCESS|FAILED|CSIGARLAND_VERSION' "$copy.firmware.log" >&2 || true
    [ "$status" -eq 0 ] || csg_die "The build in $copy failed with exit code $status."
    csg_say "Building the utility in $copy"
    utility="$(bash "$copy/deploy/build-utility.sh" --build-number "$build_number")" || csg_die "The utility's build in $copy failed."
    utility="${utility##*$'\n'}"
    hashes=''
    for file in "${made[@]}"; do
        if [ -f "$app/$file" ]; then hashes="$hashes$(csg_tool sha256 "$app/$file") "; else hashes="${hashes}missing "; fi
    done
    if [ -f "$utility" ]; then hashes="$hashes$(csg_tool sha256 "$utility")"; else hashes="${hashes}missing"; fi
    results[$n]="$hashes"
done
unset CSIGARLAND_THROWAWAY_SIGNING CSIGARLAND_BUILD_NUMBER

made[${#made[@]}]='the utility'
read -r -a first <<<"${results[0]}"
read -r -a second <<<"${results[1]}"
differ=0
for i in "${!made[@]}"; do
    state=same
    if [ "${first[$i]}" != "${second[$i]}" ] || [ "${first[$i]}" = missing ]; then
        state=DIFFER
        differ=$((differ + 1))
    fi
    printf '%-6s %-40s %s\n' "$state" "${made[$i]}" "${first[$i]}" >&2
done
if [ "$differ" -gt 0 ]; then
    # Where the first image's bytes part, for a start at finding why.
    csg_python -c 'import sys
a, b = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
at = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), None)
if at is not None:
    print("firmware.bin first differs at 0x%X of %d and %d bytes." % (at, len(a), len(b)))' \
        "${copies[0]}/firmware/app/${made[1]}" "${copies[1]}/firmware/app/${made[1]}" >&2 || true
    csg_say "The copies are kept in $work."
    csg_die "$differ of ${#made[@]} files differ between two builds of the same sources."
fi
if [ "$keep" -eq 0 ]; then rm -rf "$work"; fi
csg_say "Two builds of the same sources are the same to the byte, in ${#made[@]} files."
