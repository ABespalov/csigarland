#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Makes the pictures of the garland's screens, portal pages and utility in docs/images.
# Contributors: None
#
# The bash twin of deploy/new-doc-images.ps1, for Linux and macOS. Touches no device. The screens come
# from the firmware's own drawing code on this computer, through firmware/app/tools/screens, and the portal
# pages are its real pages in a browser with no window, answered by deploy/portal_stub.py. The utility's
# pages are drawn by Gio with no window, by a test in internal/ui. It needs one firmware build first, for
# LVGL in managed_components, and Chrome, Chromium or Edge.
#
# Options
#
#   --version <text>                 The firmware version the pictures show, fixed so a build changes none.
#   --screens-only, --portal-only, --utility-only
#                                    Make only those.
#
# Example:
#   bash deploy/new-doc-images.sh
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/lib/common.sh"
. "$CSG_LIB_DIR/build_version.sh"
. "$CSG_LIB_DIR/device_detection.sh"

version='1.4.0.119'
screens_only=0 portal_only=0 utility_only=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --version) csg_need_value "$1" "$#"; version="$2"; shift ;;
        --screens-only) screens_only=1 ;;
        --portal-only) portal_only=1 ;;
        --utility-only) utility_only=1 ;;
        *) csg_die "new-doc-images.sh does not take $1." ;;
    esac
    shift
done
repo="$CSG_REPO_ROOT"
firmware_dir="$repo/firmware/app"
scratch="$repo/temp/doc-images"
screen_size=240
screen_scale=1
portal_width=480
# Picture name, and the page with what to press on it first.
portal_pages=('effects /' 'effects-short /?rows=3' 'schedule /schedule' 'device /device' 'network /network'
    'firmware /firmware' 'info /info' 'pair /pair?press=pair-start')

# ESP-IDF's Python, as the PowerShell script takes. The screens need a firmware build anyway; the portal's
# stub needs nothing installed, so without one it runs under this Python.
find_python() {
    python="$(csg_esp_python)"
    if [ -z "$python" ]; then
        [ "$portal_only" -eq 1 ] || csg_die 'The Python environment of ESP-IDF was not found under .platformio: build the firmware once.'
        python="$CSG_PYTHON"
    fi
}

find_browser() {
    local candidate
    for candidate in google-chrome google-chrome-stable chromium chromium-browser microsoft-edge microsoft-edge-stable \
        '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' '/Applications/Chromium.app/Contents/MacOS/Chromium' \
        '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge'; do
        if command -v "$candidate" >/dev/null 2>&1; then
            browser="$(command -v "$candidate")"
            return 0
        fi
    done
    csg_die 'Neither Chrome, Chromium nor Edge was found.'
}

new_screen_pictures() {
    [ -f "$firmware_dir/managed_components/lvgl__lvgl/lvgl.h" ] || csg_die 'LVGL is not in managed_components: build the firmware once.'
    csg_require pio 'PlatformIO (pio) is not on PATH.'
    pio run -d "$firmware_dir" -e screens >"$scratch/screens-build.log" 2>&1 || {
        tail -n 3 "$scratch/screens-build.log" >&2
        csg_die 'Building the screens tool failed.'
    }
    rm -rf "$scratch/screens"
    mkdir -p "$scratch/screens"
    "$firmware_dir/.pio/build/screens/program" "$scratch/screens" "$version" || csg_die 'The screens tool failed.'
    "$python" "$CSG_DEPLOY_DIR/screens_to_png.py" "$scratch/screens" "$repo/docs/images/screens" "$screen_size" "$screen_scale" ||
        csg_die 'Making the PNG files failed.'
}

# The browser draws a page, and is stopped if it has not after a minute: macOS has no timeout command.
run_browser() {
    local url="$1" picture="$2" height="$3" pid waited=0
    rm -f "$picture"
    "$browser" --headless=new --disable-gpu --hide-scrollbars --no-first-run --no-default-browser-check --lang=en-US \
        "--user-data-dir=$scratch/browser" --force-device-scale-factor=2 "--window-size=$portal_width,$height" \
        --virtual-time-budget=4000 "--screenshot=$picture" "$url" >/dev/null 2>&1 &
    pid=$!
    while kill -0 "$pid" 2>/dev/null; do
        if [ "$waited" -ge 120 ]; then
            kill "$pid" 2>/dev/null || true
            csg_die "The browser did not finish with $url."
        fi
        sleep 0.5
        waited=$((waited + 1))
    done
    wait "$pid" || true
    [ -f "$picture" ] || csg_die "The browser made no picture of $url."
}

new_portal_pictures() {
    local target="$repo/docs/images/portal" port stub entry name page url path height deadline
    mkdir -p "$target"
    # A port nothing else holds, as a site served for a look, say, may hold any fixed one.
    port="$(csg_python -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')"
    CSIGARLAND_STUB_SHOTS=1 "$python" "$CSG_DEPLOY_DIR/portal_stub.py" "$port" "$version" >"$scratch/stub.log" 2>&1 &
    stub=$!
    csg_on_exit "kill $stub 2>/dev/null || true"
    # Until it answers, rather than a guess at how long it takes to start.
    deadline=$(($(date +%s) + 20))
    until curl -s --max-time 2 -o /dev/null "http://127.0.0.1:$port/stub/heights"; do
        if ! kill -0 "$stub" 2>/dev/null || [ "$(date +%s)" -gt "$deadline" ]; then
            csg_die "The portal stub did not start on port $port."
        fi
        sleep 0.2
    done
    for entry in "${portal_pages[@]}"; do
        name="${entry%% *}"
        page="${entry#* }"
        url="http://127.0.0.1:$port$page"
        path="${page%%\?press=*}"
        # Once to learn its height, and once at that height.
        run_browser "$url" "$target/$name.png" 900
        curl -s --max-time 5 -o "$scratch/heights.json" "http://127.0.0.1:$port/stub/heights" || csg_die 'The portal stub stopped answering.'
        height="$(csg_tool json-get "$scratch/heights.json" "$path")" || csg_die "The page $path did not say how tall it is."
        run_browser "$url" "$target/$name.png" "$height"
        csg_say "$name.png, $height px tall"
    done
}

new_utility_pictures() {
    local target="$repo/docs/images/utility"
    csg_require go 'Go is not on PATH.'
    mkdir -p "$target"
    csg_set_engine_sources "$repo"
    CSIGARLAND_SHOTS="$target" go -C "$repo" test -tags utilityshot -count=1 -run TestRenderShots ./internal/ui ||
        csg_die "Drawing the utility's pages failed."
}

mkdir -p "$scratch"
if [ "$utility_only" -eq 0 ]; then
    find_python
    if [ "$portal_only" -eq 0 ]; then new_screen_pictures; fi
    if [ "$screens_only" -eq 0 ]; then
        csg_require curl 'curl is not on PATH.'
        find_browser
        new_portal_pictures
    fi
fi
if [ "$utility_only" -eq 1 ] || { [ "$screens_only" -eq 0 ] && [ "$portal_only" -eq 0 ]; }; then
    new_utility_pictures
fi
