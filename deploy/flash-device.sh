#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Enroll, identify, build, and safely flash one exact csiGarland board.
# Contributors: None
#
# The bash twin of deploy/flash-device.ps1, for Linux and macOS, with the same enrolment file, counter and
# checks. Flashes over the board's USB-C port, which the ESP32-C6 keeps while the application runs:
# esptool resets it through that port, so no button is needed. A board whose image crashes before USB
# comes up needs ROM download mode: hold BOOT, tap RESET, release BOOT. On Linux your user needs the
# dialout group (uucp on Arch) to open the port.
#
# Nothing is written to a device this repository has not been told about. The first flash of a new board
# needs --enroll or an explicit --port; every flash after that checks that the device on the port is still
# that same physical board.
#
# Options
#
#   --port <device>
#       Which serial port to use: /dev/ttyACM0 on Linux, /dev/cu.usbmodem1101 on macOS, or the name alone.
#       Naming one is deliberate targeting, so it does not ask for confirmation, but it still has to match
#       the enrolled board. Without it the script finds the enrolled board itself and asks first.
#   --list-candidates   Print the ESP32-C6 USB devices that are present and exit. Touches nothing.
#   --enroll            Record the board on --port as the one this repository may flash, then exit. Only a
#                       hashed identity is stored, never a serial number.
#   --detect-only       Resolve and check the target exactly as a flash would, then exit.
#   --build-only        Build and check the image, spending a build number, and write nothing.
#   --effects-only      Write only the effects of firmware/app/effects, with no build and no build number.
#   --full-clean        Run pio run -t clean before building, rebuilding every component.
#   --no-monitor        Stop after flashing. Without it the script stays attached to the board's log and
#                       keeps a copy in temp/serial_monitor.<build>.log.
#   --log-file <path>   Write the log here instead.
#   --lock-held         For deploy/stage-assets.sh and deploy/ota-update.sh alone, which hold the firmware
#                       build lock of this checkout and run this under it. Never given by hand.
#   --build-profile Release|Dev
#                       Release, the default, as the environment esp32c6-release; Dev, as esp32c6, keeps
#                       the debug information and marks the version -dev.
#   --what-if           Report what would be flashed and stop.
#   --yes               Flash the board found without asking, as PowerShell's -Confirm:$false.
#
# Every flash builds first and checks the image before writing anything: the build counter must have moved
# by exactly one, and the .bin must name that identity both in esp_app_desc.version and in the display's
# FW string. A build that fails, or fails a check, gives its number back.
#
# Examples:
#   bash deploy/flash-device.sh --list-candidates
#   bash deploy/flash-device.sh --port /dev/ttyACM0
#   bash deploy/flash-device.sh --build-only
#   bash deploy/flash-device.sh --effects-only --port /dev/cu.usbmodem1101
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/lib/common.sh"
. "$CSG_LIB_DIR/build_version.sh"
. "$CSG_LIB_DIR/signing_python.sh"
. "$CSG_LIB_DIR/device_detection.sh"
. "$CSG_LIB_DIR/device_monitor.sh"

port=''
list_candidates=0 enroll=0 detect_only=0 build_only=0 effects_only=0 full_clean=0 no_monitor=0 lock_held=0 what_if=0 yes=0
log_file=''
profile=Release
while [ "$#" -gt 0 ]; do
    case "$1" in
        --port) csg_need_value "$1" "$#"; port="$(csg_port_path "$2")"; shift ;;
        --list-candidates) list_candidates=1 ;;
        --enroll) enroll=1 ;;
        --detect-only) detect_only=1 ;;
        --build-only) build_only=1 ;;
        --effects-only) effects_only=1 ;;
        --full-clean) full_clean=1 ;;
        --no-monitor) no_monitor=1 ;;
        --log-file) csg_need_value "$1" "$#"; log_file="$2"; shift ;;
        --lock-held) lock_held=1 ;;
        --build-profile) csg_need_value "$1" "$#"; profile="$(csg_profile "$2")"; shift ;;
        --what-if) what_if=1 ;;
        --yes) yes=1 ;;
        *) csg_die "flash-device.sh does not take $1." ;;
    esac
    shift
done

supported_vid='303A'
supported_pid='1001' # USB Serial/JTAG, in the application and in ROM download mode.
firmware_dir="$CSG_REPO_ROOT/firmware/app"
profile_path="$CSG_DEPLOY_DIR/device-profile.local.json"
counter_path="$firmware_dir/.csigarland_build_counter"
environment="$(csg_firmware_environment "$profile")"
build_root="$firmware_dir/.pio/build/$environment"
bin_path="$build_root/firmware.bin"
pack_slot="$firmware_dir/generated/effects_slot.bin"
pack_blank="$firmware_dir/generated/effects_blank.bin"
partition_table_offset='0x8000'
partition_table_bytes='0xC00'

# What it builds goes to a board, or to a garland by ota-update.sh: never a throwaway chain.
csg_assert_own_signing

supported_candidates() {
    local all port vid pid identity
    all="$(csg_serial_candidates)" || return 1
    while IFS="$(printf '\t')" read -r port vid pid identity; do
        [ -n "$port" ] || continue
        if [ "$(csg_upper "$vid")" = "$supported_vid" ] && [ "$(csg_upper "$pid")" = "$supported_pid" ]; then
            printf '%s\t%s\t%s\t%s\n' "$port" "$vid" "$pid" "$identity"
        fi
    done <<<"$all"
}

profile_field() { csg_tool json-get "$profile_path" "$1" ''; }

read_device_profile() {
    [ -f "$profile_path" ] ||
        csg_die 'Device profile not found. Run --list-candidates, then enroll the intended board with --enroll --port <device>. No port was touched.'
    if [ "$(profile_field schemaVersion)" != 1 ] || [ -z "$(profile_field vid)" ] || [ -z "$(profile_field pid)" ] || [ -z "$(profile_field identityHash)" ]; then
        csg_die "Invalid device profile: $profile_path"
    fi
}

# The candidate on the port named, as port, VID, PID and identity hash by tabs.
explicit_port_candidate() {
    local candidates="$1" found='' count=0 line
    [ -n "$port" ] || csg_die 'An explicit --port is required to enroll a device.'
    while IFS= read -r line; do
        if [ -n "$line" ] && [ "${line%%$(printf '\t')*}" = "$port" ]; then
            found="$line"
            count=$((count + 1))
        fi
    done <<<"$candidates"
    [ "$count" -eq 1 ] || csg_die "Expected one ESP32-C6 USB device on $port; found $count."
    printf '%s\n' "$found"
}

save_device_profile() {
    local vid pid identity
    IFS="$(printf '\t')" read -r _ vid pid identity <<<"$1"
    [ -n "$identity" ] || csg_die "The device on $port says no USB serial number, so it cannot be told from another. Nothing was changed."
    csg_tool json-build --raw schemaVersion 1 --str vid "$vid" --str pid "$pid" --str identityHash "$identity" \
        --str enrolledAtUtc "$(date -u +%Y-%m-%dT%H:%M:%SZ)" >"$profile_path"
}

# Drops what must be rebuilt for a new build number, keeping every other object.
clear_build_outputs() {
    local relative
    for relative in firmware.bin firmware.ota firmware-only.ota effects.ota firmware.elf firmware.map firmware.source.json esp-idf/esp_app_format esp_app_format; do
        rm -rf "${build_root:?}/$relative"
    done
}

run_pio() { (cd "$firmware_dir" && pio "$@"); }

# ESP-IDF's Python and esptool, found once, and checked to run: a new machine's ESP-IDF environment has no
# pyserial, which esptool needs and building does not.
esp_py=''
esptool=''
confirm_esptool_runs() {
    esp_py="$(csg_esp_python)"
    [ -n "$esp_py" ] || csg_die "The Python environment of ESP-IDF was not found under $(csg_platformio_core). Nothing was written."
    esptool="$(csg_platformio_packages)/tool-esptoolpy/esptool.py"
    [ -f "$esptool" ] || csg_die "esptool was not found under $(csg_platformio_packages). Nothing was written."
    csg_serial_python >/dev/null
    "$esp_py" "$esptool" version >/dev/null 2>&1 || csg_die "esptool does not run under $esp_py. Nothing was written."
}

run_esptool() {
    [ -n "$esp_py" ] && [ -n "$esptool" ] || csg_die 'esptool was not checked before use. Nothing was written.'
    "$esp_py" "$esptool" "$@"
}

# One numbered build, checked before anything is written. A build that fails, or one that fails a check
# after it, gives its number back and leaves no image of it, so the next build takes the same number.
build_state=idle
build_before=-1
build_finally() {
    if [ "$build_state" = building ] && [ "$build_before" -ge 0 ]; then
        clear_build_outputs
        if csg_undo_build_number "$counter_path" "$build_before"; then
            csg_warn "The build failed, so build number $((build_before + 1)) is not spent: the next build takes it."
        fi
    fi
}

numbered_build() {
    local source after stamp zlib
    csg_require pio 'PlatformIO (pio) is not on PATH.'
    if [ "$lock_held" -eq 0 ]; then csg_lock firmware; fi
    # After the lock, so that the number goes back while it is still held: cleanups run last added first.
    csg_on_exit build_finally
    build_state=building
    if [ "$full_clean" -eq 1 ]; then
        run_pio run -e "$environment" -t clean >&2 || csg_die 'PlatformIO clean failed.'
    fi
    clear_build_outputs
    source="$(csg_source_identity "$CSG_REPO_ROOT")"
    build_before="$(csg_build_number "$counter_path")"
    # What PlatformIO would delete on a change of platformio.ini, firmware_build_version.py keeps.
    CSIGARLAND_INCREMENT_BUILD=1 run_pio run -e "$environment" --disable-auto-clean >&2 ||
        csg_die 'PlatformIO build failed. Nothing was written to the device.'
    after="$(csg_build_number "$counter_path")"
    [ "$after" -eq $((build_before + 1)) ] ||
        csg_die "Build counter went from $build_before to $after; a build consumes exactly one number. Nothing was written to the device."
    image_version="$(csg_firmware_identity "$firmware_dir/platformio.ini" "$after" "$profile")"
    problem="$(csg_bin_identity_problem "$bin_path" "$image_version")"
    [ -z "$problem" ] || csg_die "Firmware identity mismatch: $problem. Nothing was written to the device. --full-clean rebuilds every component."
    # Checked as a garland would check it, so that an image that trusts any other root never starts.
    csg_test_own_signature "$build_root/firmware.ota" ||
        csg_die "The build is not signed under this repository's root, as a throwaway build would not be. Nothing was written to the device."
    csg_say "Image identity verified: $image_version in esp_app_desc and the display string."
    # Beside the image, so that whoever passes it on later says what it was made from, with the zlib its
    # portal's pages were gzipped with, as portal_pack.py recorded it: a release is held to the one named.
    zlib="$(csg_trim "$(cat "$firmware_dir/generated/portal_zlib.txt")")"
    [ -n "$zlib" ] || csg_die 'The build recorded no zlib in generated/portal_zlib.txt. Nothing was written to the device.'
    stamp="$(printf '%s' "$source" | csg_tool json-build --base - --str version "$image_version" --str image "$(csg_image_id "$bin_path")" --str zlib "$zlib")"
    printf '%s' "$stamp" >"$build_root/firmware.source.json"
    image_sha="$(csg_tool sha256 "$bin_path")"
    pack_sha="$(csg_tool sha256 "$pack_slot")"
    build_state=built
    if [ "$lock_held" -eq 0 ] && [ "$usb_lock" -eq 0 ]; then csg_unlock firmware; fi
}

[ "$effects_only" -eq 0 ] || { [ "$build_only" -eq 0 ] && [ "$full_clean" -eq 0 ]; } ||
    csg_die '--effects-only builds nothing, so it does not go with --build-only or --full-clean.'
usb_lock=0

if [ "$build_only" -eq 1 ]; then
    numbered_build
    csg_say "Built $image_version. Nothing was written to any device."
    exit 0
fi

candidates="$(supported_candidates)"
if [ "$list_candidates" -eq 1 ]; then
    if [ -z "$candidates" ]; then
        csg_say 'No ESP32-C6 USB devices are present.'
    else
        printf 'Port\tVid\tPid\n'
        printf '%s\n' "$candidates" | cut -f1-3
    fi
    exit 0
fi

if [ "$enroll" -eq 1 ]; then
    candidate="$(explicit_port_candidate "$candidates")"
    if [ "$what_if" -eq 1 ]; then
        csg_say "What if: would enroll the board on $port. Nothing was changed."
        exit 0
    fi
    save_device_profile "$candidate"
    csg_say 'Enrolled the detected csiGarland identity. No firmware was flashed.'
    exit 0
fi

# An explicit port is enough enrollment intent on first use.
if [ ! -f "$profile_path" ] && [ -n "$port" ]; then
    candidate="$(explicit_port_candidate "$candidates")"
    if [ "$what_if" -eq 1 ]; then
        csg_say "What if: would enroll the board on $port, then build and flash it. Nothing was changed."
        exit 0
    fi
    save_device_profile "$candidate"
    csg_say "Enrolled the csiGarland identity currently attached to $port."
fi

read_device_profile
target="$(csg_select_flash_target "$candidates" "$(profile_field vid)" "$(profile_field pid)" "$(profile_field identityHash)" "$port")"
csg_say "Verified csiGarland target: port=$target"
if [ "$detect_only" -eq 1 ]; then
    csg_say 'Detection succeeded. No firmware was flashed.'
    exit 0
fi

if [ "$effects_only" -eq 1 ]; then action='Flash only the effects'; else action="Build and flash PlatformIO environment $environment"; fi
if [ "$what_if" -eq 1 ]; then
    csg_say "What if: $action on $target."
    exit 0
fi
# Deliberate target selection must not cause an interactive question.
if [ -z "$port" ] && [ "$yes" -eq 0 ] && ! csg_confirm "$action on $target?"; then
    csg_say 'Nothing was flashed.'
    exit 0
fi

# Where partitions.csv puts a partition. flasher_args.json names only what ESP-IDF flashes.
partition_fields() {
    local found
    found="$(grep -Ei "^[[:space:]]*$1[[:space:]]*," "$firmware_dir/partitions.csv" || true)"
    [ "$(csg_count_lines "$found")" -eq 1 ] || csg_die "partitions.csv has no single $1. Nothing was written to the device."
    # Its type, subtype, offset and size, with the spaces taken out.
    printf '%s\n' "$found" | tr -d '[:space:]' | cut -d, -f2-5 | tr ',' ' '
}

# The bootloader, partition table, update state, application and effects at the addresses this build's
# partition table gives them. The pack goes to its first slot, and erasing the second's commit record makes
# sure no pack from an earlier update outranks it.
flash_image_args() {
    local offset file part flasher fields
    image_args=()
    if [ "$effects_only" -eq 0 ]; then
        flasher="$build_root/flasher_args.json"
        for part in bootloader:bootloader.bin partition-table:partitions.bin otadata:ota_data_initial.bin app:firmware.bin; do
            offset="$(csg_tool json-get "$flasher" "${part%%:*}.offset")" || csg_die "$flasher names no ${part%%:*}. Nothing was written to the device."
            image_args[${#image_args[@]}]="$offset"
            image_args[${#image_args[@]}]="$build_root/${part#*:}"
        done
    fi
    for part in "pack_a:$pack_slot" "pack_b:$pack_blank"; do
        fields="$(partition_fields "${part%%:*}")" || exit 1
        read -r _ _ offset _ <<<"$fields"
        image_args[${#image_args[@]}]="$offset"
        image_args[${#image_args[@]}]="${part#*:}"
    done
    for file in "${image_args[@]}"; do
        case "$file" in
            0x* | 0X*) ;;
            *) [ -f "$file" ] || csg_die "Missing $file. Nothing was written to the device." ;;
        esac
    done
}

# With the firmware left as it is, so is the partition table: on an older layout these offsets belong to
# something else. Reads the table on the board and fails closed.
assert_board_has_pack_slots() {
    local table label kind subtype offset size fields
    mkdir -p "$CSG_TEMP"
    table="$CSG_TEMP/partition-table.read.bin"
    run_esptool --chip esp32c6 --port "$1" --before default_reset --after no_reset read_flash "$partition_table_offset" "$partition_table_bytes" "$table" >&2 ||
        csg_die "Could not read the board's partition table. Nothing was written."
    for label in pack_a pack_b; do
        fields="$(partition_fields "$label")" || exit 1
        read -r kind subtype offset size <<<"$fields"
        [ "$(csg_lower "$kind")" = data ] || csg_die "partitions.csv has no single $label. Nothing was written to the device."
        csg_tool partition-entry "$table" "$label" 1 "$subtype" "$offset" "$size" ||
            csg_die "The board has no $label as this repository lays it out, so its firmware is too old for effects alone. Flash without --effects-only. Nothing was written."
        if [ "$label" = pack_a ] && [ "$(csg_tool file-size "$pack_slot")" -gt $((size)) ]; then
            csg_die "The effects are larger than the $((size)) bytes of $label. Nothing was written."
        fi
    done
    rm -f "$table"
    csg_say 'The board has the effects slots.'
}

# Every attempt is checked again first: a reconnect, a renamed port or another board during the build or
# the retry's pause must fail closed, with nothing written.
# Run as the condition of an if, where bash leaves set -e aside: every step that decides where to write
# ends the script itself when it fails, as PowerShell's throw would, rather than let esptool choose a port.
verified_upload() {
    local now
    now="$(supported_candidates)" || exit 1
    current="$(csg_select_flash_target "$now" "$(profile_field vid)" "$(profile_field pid)" "$(profile_field identityHash)" "$target")" || exit 1
    [ -n "$current" ] || csg_die 'No verified target to write to. Nothing was written.'
    if { [ "$effects_only" -eq 0 ] && [ "$(csg_tool sha256 "$bin_path")" != "$image_sha" ]; } || [ "$(csg_tool sha256 "$pack_slot")" != "$pack_sha" ]; then
        csg_die 'The image changed after it was verified. Nothing was written by this attempt. Flash again.'
    fi
    flash_image_args
    run_esptool --chip esp32c6 --port "$current" --baud 921600 --before default_reset --after hard_reset \
        write_flash -z --flash_mode keep --flash_freq keep --flash_size keep "${image_args[@]}" >&2
}

# A write over USB takes the files of the build folder after their checks: held until the write ends, the
# lock keeps another build of this checkout from replacing them in between.
if [ "$lock_held" -eq 0 ]; then
    csg_lock firmware
    usb_lock=1
fi
if [ "$effects_only" -eq 1 ]; then
    confirm_esptool_runs
    "$esp_py" "$firmware_dir/scripts/effect_pack_tool.py" "$firmware_dir/effects" "$firmware_dir/generated" >&2 ||
        csg_die 'The effects pack was not made. Nothing was written to the device.'
    image_version='effects only'
    pack_sha="$(csg_tool sha256 "$pack_slot")"
    assert_board_has_pack_slots "$target"
else
    numbered_build
    # Asked only of a write over USB, which runs it: a build alone, for staging or OTA, needs no esptool.
    confirm_esptool_runs
fi

if ! verified_upload; then
    # The image is built and verified; a failed upload is the port or the board.
    csg_say ''
    csg_say 'Upload failed; retrying once with the same verified image.'
    sleep 2
    verified_upload || csg_die 'Upload failed. If the board is not responding, hold BOOT, tap RESET, release BOOT and run again.'
fi
# The log that follows holds no lock, so a staging or a build elsewhere need not wait for it to close.
if [ "$usb_lock" -eq 1 ]; then csg_unlock firmware; fi
csg_say "Flash succeeded: $image_version on $current"
if [ "$no_monitor" -eq 1 ]; then
    exit 0
fi
csg_start_log_session "$current" "$profile_path" "$(csg_resolve_log_path "$log_file" "$counter_path")"
