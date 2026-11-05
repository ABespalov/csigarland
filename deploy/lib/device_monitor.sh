# shellcheck shell=bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Read a running csiGarland board's log to the terminal and a file, on Linux and macOS.
# Contributors: None
#
# Sourced after common.sh, build_version.sh and device_detection.sh: each function is its namesake in
# deploy/DeviceMonitor.psm1. It only reads. It writes no firmware and consumes no build number, so it does
# not insist on the enrolled board the way flash-device.sh does. It prefers that board, and otherwise
# takes the only one there is.

# The C6's USB Serial/JTAG: the same identity in the application and in ROM download mode.
CSG_BOARD_VID='303A'
CSG_BOARD_PID='1001'

# The boards that fit, one port a line: the requested port, or else the enrolled board if it is here, or
# else every board on the bus.
csg_select_monitor_candidates() {
    local boards="$1" requested="${2:-}" enrolled="${3:-}" port vid pid identity mine='' all=''
    while IFS="$(printf '\t')" read -r port vid pid identity; do
        [ -n "$port" ] || continue
        [ "$(csg_upper "$vid")" = "$CSG_BOARD_VID" ] && [ "$(csg_upper "$pid")" = "$CSG_BOARD_PID" ] || continue
        if [ -n "$requested" ]; then
            if [ "$port" = "$requested" ]; then all="$all$port"$'\n'; fi
            continue
        fi
        all="$all$port"$'\n'
        if [ -n "$enrolled" ] && [ "$(csg_upper "$identity")" = "$(csg_upper "$enrolled")" ]; then mine="$mine$port"$'\n'; fi
    done <<<"$boards"
    if [ -n "$mine" ]; then printf '%s' "$mine"; else printf '%s' "$all"; fi
}

# The port of the board, or nothing if none appeared before the timeout.
csg_wait_device_port() {
    local requested="${1:-}" profile="${2:-}" timeout="${3:-180}" quiet=6 every=15 enrolled='' start now next boards fitting count
    if [ -n "$profile" ] && [ -f "$profile" ]; then
        enrolled="$(csg_tool json-get "$profile" identityHash '')"
    fi
    start="$(date +%s)"
    next=$((start + quiet))
    while true; do
        boards="$(csg_serial_candidates)" || return 1
        fitting="$(csg_select_monitor_candidates "$boards" "$requested" "$enrolled")"
        count="$(csg_count_lines "${fitting%$'\n'}")"
        if [ "$count" -eq 1 ]; then
            printf '%s\n' "${fitting%$'\n'}"
            return 0
        fi
        if [ "$count" -gt 1 ]; then
            csg_die "More than one ESP32-C6 is present ($(printf '%s' "${fitting%$'\n'}" | tr '\n' ',' | sed 's/,/, /g')). Choose one with --port."
        fi
        now="$(date +%s)"
        if [ "$now" -ge $((start + timeout)) ]; then
            break
        fi
        if [ "$now" -ge "$next" ]; then
            csg_say "Waiting for the device ($((now - start))s of ${timeout}s). Check the USB cable, or press RESET."
            next=$((now + every))
        fi
        sleep 0.5
    done
    # "Not found" alone cannot tell a missing board from one on another port.
    csg_say "No ${requested:+$requested with }VID_${CSG_BOARD_VID}&PID_${CSG_BOARD_PID} appeared. USB serial ports seen:"
    if [ -z "$boards" ]; then csg_say '  (none)'; fi
    printf '%s\n' "$boards" | while IFS="$(printf '\t')" read -r port vid pid _; do
        [ -n "$port" ] && csg_say "  $port  VID_$vid&PID_$pid"
    done
    return 0
}

# The default name carries the build number the last flash consumed, which ties a log to an image.
csg_resolve_log_path() {
    local log_file="$1" counter="$2" path
    if [ -n "$log_file" ]; then
        path="$(csg_abs "$log_file")"
    else
        path="$CSG_TEMP/serial_monitor.$(csg_build_number "$counter").log"
    fi
    mkdir -p "$(dirname "$path")"
    printf '%s\n' "$path"
}

# Stays attached across resets until Ctrl+C or until the board stops coming back, so one file holds every
# boot in order. Fails only if it never found a board at all.
csg_start_log_session() {
    local requested="$1" profile="$2" log="$3" timeout="${4:-180}" current python status
    current="$(csg_wait_device_port "$requested" "$profile" "$timeout")"
    [ -n "$current" ] || csg_die 'No board to read. Nothing was changed on any device.'
    python="$(csg_serial_python)"
    # Attaching does not reset the board, so what happened before now is not in this log.
    csg_say 'Press RESET on the device to capture a boot log.'
    while true; do
        status=0
        "$python" -B -E -s "$CSG_LIB_DIR/csg_serial.py" monitor "$current" "$log" 15 || status=$?
        case "$status" in
            0)
                # The device name of a reset board is usually the same, and reopening it is much quicker
                # than looking for it again.
                csg_say 'Reattaching. Press Ctrl+C to stop.'
                continue
                ;;
            130) return 0 ;;
            3) ;;
            *) csg_die "Reading $current failed with exit code $status. On Linux, your user needs the dialout group, or uucp on Arch." ;;
        esac
        current="$(csg_wait_device_port "$requested" "$profile" "$timeout")"
        if [ -z "$current" ]; then
            csg_say 'The device did not come back. Stopping the monitor.'
            return 0
        fi
    done
}
