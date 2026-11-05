#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Write THIRD_PARTY_NOTICES.md from the exact versions the project uses.
# Contributors: None
#
# The bash twin of deploy/new-third-party-notices.ps1, for Linux and macOS: it writes the same file to
# the byte, by deploy/lib/csg_notices.py. Takes what the firmware links from the link maps of the last
# build, versions from firmware/app/dependencies.lock and go.mod, and each licence text from what the
# build and Go downloaded, so both have to have run on this machine. Fails rather than leave one out.
#
# Options
#
#   --check
#       Write nothing, and fail if the file is not what this would write.
#
#   --build-profile Release|Dev
#       Take the link maps of that profile's last build. Without it, the profile built last.
#
#   --utility-only
#       Check the utility's part alone, with no firmware build: each of its components is named in the
#       file under its own licence, and the file names none it no longer holds. It writes nothing.
#
# Example:
#   bash deploy/new-third-party-notices.sh
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/lib/common.sh"
. "$CSG_LIB_DIR/build_version.sh"

mode=write
profiles=(Release Dev)
utility_only=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --check) mode=check ;;
        --utility-only) utility_only=1 ;;
        --build-profile)
            csg_need_value "$1" "$#"
            profiles=("$(csg_profile "$2")")
            shift
            ;;
        *) csg_die "new-third-party-notices.sh does not take $1." ;;
    esac
    shift
done
if [ "$utility_only" -eq 1 ]; then mode=utility-only; fi
csg_require go 'Go is not on PATH, and the notices read the licences of its modules from its cache.'

build_root=-
if [ "$mode" != utility-only ]; then
    # The profile built last, by its link map: both link the same components.
    newest=''
    for each in "${profiles[@]}"; do
        folder="$CSG_REPO_ROOT/firmware/app/.pio/build/$(csg_firmware_environment "$each")"
        for map in "$folder"/*.map; do
            [ -f "$map" ] || continue
            if [ -z "$newest" ] || [ "$map" -nt "$newest" ]; then
                newest="$map"
                build_root="$folder"
            fi
            break
        done
    done
    [ "$build_root" != - ] || csg_die 'No firmware build to take the link maps of: run pio run -d firmware/app -e esp32c6 first.'
fi
csg_python "$CSG_LIB_DIR/csg_notices.py" "$CSG_REPO_ROOT" "$(csg_platformio_packages)" "$(go env GOMODCACHE)" "$(go env GOROOT)" "$build_root" "$mode"
