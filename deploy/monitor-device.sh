#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Read the log of a csiGarland board that is already running.
# Contributors: None
#
# The bash twin of deploy/monitor-device.ps1, for Linux and macOS. This flashes nothing and consumes no
# build number: a flash whose monitor did not attach does not have to be repeated to recover its log. It
# needs no enrolled profile, because it only reads. With several boards plugged in it takes the enrolled
# one if that is there; otherwise it asks for --port. Attaching does not reset the board: the lines that
# could are held released, so a boot log takes pressing RESET after the monitor has attached. It stays
# attached across resets, so one file holds every boot in order. Press Ctrl+C to stop. Only one program
# can hold the port: flash-device.sh and this one cannot run at once. On Linux your user needs the dialout
# group (uucp on Arch).
#
# Options
#
#   --port <device>
#       Read this port instead of finding the board: /dev/ttyACM0, /dev/cu.usbmodem1101, or the name alone.
#
#   --log-file <path>
#       Write the log here instead of temp/serial_monitor.<build>.log, whose name carries the build number
#       the last flash consumed.
#
#   --timeout-seconds <n>
#       How long to wait for the board to appear, default 180.
#
# Examples:
#   bash deploy/monitor-device.sh
#   bash deploy/monitor-device.sh --port /dev/ttyACM0 --log-file temp/session.log
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/lib/common.sh"
. "$CSG_LIB_DIR/build_version.sh"
. "$CSG_LIB_DIR/device_detection.sh"
. "$CSG_LIB_DIR/device_monitor.sh"

port=''
log_file=''
timeout=180
while [ "$#" -gt 0 ]; do
    case "$1" in
        --port) csg_need_value "$1" "$#"; port="$(csg_port_path "$2")"; shift ;;
        --log-file) csg_need_value "$1" "$#"; log_file="$2"; shift ;;
        --timeout-seconds)
            csg_need_value "$1" "$#"
            case "$2" in '' | *[!0-9]*) csg_die "--timeout-seconds is a number, not $2." ;; esac
            timeout="$2"
            shift
            ;;
        *) csg_die "monitor-device.sh does not take $1." ;;
    esac
    shift
done

log_path="$(csg_resolve_log_path "$log_file" "$CSG_REPO_ROOT/firmware/app/.csigarland_build_counter")"
csg_start_log_session "$port" "$CSG_DEPLOY_DIR/device-profile.local.json" "$log_path" "$timeout"
