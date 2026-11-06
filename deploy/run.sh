#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Builds the csiGarland utility and runs it, refusing to run a build that failed.
# Contributors: None
#
# The bash twin of deploy/run.ps1, for Linux and macOS. The build belongs to deploy/build-utility.sh. On
# macOS it starts the bundle the build unzipped beside its zip, which the PowerShell twin, on Windows, has not.
# A failed build leaves the previous file in assets/utility, and running that would open a window that
# works and is not the code just written, which looks like success. So the run is reached only through
# a build that returned. Touches no garland, and needs none to start. The script ends once the utility
# has started.
#
# Options
#
#   --force
#       Build and spend a build number even though nothing changed.
#
#   --no-run
#       Build and stop.
#
#   --build-profile Release|Dev
#       As deploy/build-utility.sh takes it: Release by default, Dev with debug information.
#
# Example:
#   bash deploy/run.sh
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/lib/common.sh"

build_args=()
no_run=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --force) build_args[${#build_args[@]}]=--force ;;
        --no-run) no_run=1 ;;
        --build-profile)
            csg_need_value "$1" "$#"
            build_args[${#build_args[@]}]=--build-profile
            build_args[${#build_args[@]}]="$(csg_profile "$2")"
            shift
            ;;
        *) csg_die "run.sh does not take $1." ;;
    esac
    shift
done

csg_say 'Building the utility...'
if ! said="$(bash "$CSG_DEPLOY_DIR/build-utility.sh" ${build_args[@]+"${build_args[@]}"})"; then
    csg_say ''
    csg_say 'Build failed, as above.'
    csg_say 'Any earlier build in assets/utility was not started.'
    exit 1
fi
output="${said##*$'\n'}"
# Checked anyway: a build that reports success and leaves nothing is the case this script is for.
if [ -z "$output" ] || [ ! -f "$output" ]; then
    csg_die "The build reported success, but left no file: '$output'"
fi
if [ "$no_run" -eq 1 ]; then
    exit 0
fi
# The utility only brings the one already open to the front, and a different build would not be shown.
# On macOS the build's file is a zip, and what runs is the bundle beside it.
started="$output"
if [ "$(csg_os)" = darwin ]; then started="${output%.zip}.app"; fi
earlier="$(ps -A -o args= 2>/dev/null | grep '/csigarland-[0-9]' | grep -v grep | grep -vF "$started" || true)"
if [ -n "$earlier" ]; then
    csg_warn "An earlier build is running, ${earlier%%$'\n'*}. Close its window to see this one."
fi
# On macOS the bundle beside the file, as a Mac starts a program: with its icon, and the questions macOS
# asks before the local network and the keychain asked of it, not of this terminal.
if [ "$(csg_os)" = darwin ]; then
    [ -d "$started" ] || csg_die "The build left no bundle beside $output, at $started."
    open "$started" || csg_die "macOS would not open $started."
    csg_say "Started $started."
    exit 0
fi
# On its own from here: it has no terminal to keep, and logs nothing to one.
nohup "$output" </dev/null >/dev/null 2>&1 &
csg_say "Started $output."
