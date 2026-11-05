# shellcheck shell=bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Discover and select the exact enrolled ESP32-C6 USB Serial/JTAG device, on Linux and macOS.
# Contributors: None
#
# Sourced after common.sh and build_version.sh: each function is its namesake in
# deploy/DeviceDetection.psm1. A port is a device's path, /dev/ttyACM0 on Linux or
# /dev/cu.usbmodem1101 on macOS, and a name given without /dev/ is taken to be under it. The identity
# hash is of the USB serial, the chip's MAC address, as Windows takes it too, so a board enrolled there
# is the same board here. The ports come from deploy/lib/csg_serial.py, under a Python with pyserial.

CSG_PYSERIAL_VERSION='3.5'

# ESP-IDF's own Python under PlatformIO, the newest, which esptool runs under: nothing when there is none.
csg_esp_python() {
    local python found=''
    for python in "$(csg_platformio_core)"/penv/.espidf-*/bin/python; do
        if [ -x "$python" ]; then found="$python"; fi
    done
    if [ -n "$found" ]; then printf '%s\n' "$found"; fi
}

# A Python that can list the ports: ESP-IDF's, given pyserial as esptool needs it, else one that has it.
csg_serial_python() {
    local python
    python="$(csg_esp_python)"
    if [ -n "$python" ]; then
        if ! "$python" -c 'import serial' 2>/dev/null; then
            csg_say "Installing pyserial $CSG_PYSERIAL_VERSION into ESP-IDF's Python, for esptool and the ports."
            "$python" -m pip install --quiet "pyserial==$CSG_PYSERIAL_VERSION" >&2 ||
                csg_die "Could not install pyserial. Try: '$python' -m pip install pyserial==$CSG_PYSERIAL_VERSION. Nothing was written."
        fi
        printf '%s\n' "$python"
        return 0
    fi
    if "$CSG_PYTHON" -c 'import serial' 2>/dev/null; then
        printf '%s\n' "$CSG_PYTHON"
        return 0
    fi
    csg_die "No Python here has pyserial to list the serial ports: build the firmware once, for ESP-IDF's, or install pyserial. Nothing was touched."
}

# A port as named: /dev/ttyACM0, or ttyACM0 for it.
csg_port_path() {
    case "$1" in
        '') ;;
        /*) printf '%s\n' "$1" ;;
        *) printf '/dev/%s\n' "$1" ;;
    esac
}

csg_identity_hash() { csg_tool identity-hash "$1"; }

# Every USB serial port present, one a line: port, VID, PID and identity hash, by tabs.
csg_serial_candidates() {
    local python
    python="$(csg_serial_python)" || return 1
    "$python" -B -E -s "$CSG_LIB_DIR/csg_serial.py" candidates
}

# The one candidate that is the enrolled board, on the port asked for if one was: a refusal for none, or
# for more than one.
csg_select_flash_target() {
    local candidates="$1" vid="$2" pid="$3" identity="$4" requested="${5:-}" port cvid cpid chash found='' count=0 hint=''
    while IFS="$(printf '\t')" read -r port cvid cpid chash; do
        [ -n "$port" ] || continue
        if [ "$(csg_upper "$cvid")" = "$(csg_upper "$vid")" ] && [ "$(csg_upper "$cpid")" = "$(csg_upper "$pid")" ] &&
            [ -n "$chash" ] && [ "$(csg_upper "$chash")" = "$(csg_upper "$identity")" ] && { [ -z "$requested" ] || [ "$port" = "$requested" ]; }; then
            found="$port"
            count=$((count + 1))
        fi
    done <<<"$candidates"
    if [ "$count" -eq 0 ]; then
        if [ -n "$requested" ]; then hint=" on $requested"; fi
        # The C6 keeps this port while its application runs, so a missing port means an unplugged board,
        # or an image that stopped USB by crashing.
        csg_die "The enrolled csiGarland device was not found$hint. No port was touched. Check the USB cable. If the board is plugged in, hold BOOT, tap RESET, release BOOT and run again."
    fi
    [ "$count" -eq 1 ] || csg_die "The enrolled identity matched $count ports. Refusing an ambiguous flash."
    printf '%s\n' "$found"
}
