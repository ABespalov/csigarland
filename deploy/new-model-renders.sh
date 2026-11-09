#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Renders the figurines and the enclosure from their OpenSCAD models into docs/images.
# Contributors: None
#
# The bash twin of deploy/new-model-renders.ps1, for Linux and macOS, by deploy/model_renders.py. Each
# figurine on its mount, in the colour its model gives its filament, and the eight hanging from the cable
# in the order of the firmware's layout, into docs/images/diffusers; the control unit's enclosure, exploded
# and as its parts print, into docs/images/hardware. Run it after a change to a model, a colour or the
# layout, then deploy/site-assets.sh and deploy/site-pages.sh for the site. It needs OpenSCAD.
#
# Options
#
#   --open-scad <path>
#       OpenSCAD's program, when it is neither on PATH nor in /Applications, as -OpenScad of the PowerShell
#       twin. --openscad is taken too.
#
# Example:
#   bash deploy/new-model-renders.sh
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/lib/common.sh"
. "$CSG_LIB_DIR/build_version.sh"
. "$CSG_LIB_DIR/device_detection.sh"

openscad=''
while [ "$#" -gt 0 ]; do
    case "$1" in
        --open-scad | --openscad) csg_need_value "$1" "$#"; openscad="$2"; shift ;;
        *) csg_die "new-model-renders.sh does not take $1." ;;
    esac
    shift
done
if [ -z "$openscad" ]; then
    openscad="$(command -v openscad 2>/dev/null || true)"
    if [ -z "$openscad" ] && [ -x /Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD ]; then
        openscad=/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD
    fi
fi
[ -n "$openscad" ] && [ -x "$openscad" ] || csg_die "OpenSCAD was not found${openscad:+ at $openscad}. Install it, or name it with --open-scad."
# ESP-IDF's Python, as the PowerShell script takes, else this one: the renders need nothing installed.
python="$(csg_esp_python)"
python="${python:-$CSG_PYTHON}"
"$python" "$CSG_DEPLOY_DIR/model_renders.py" "$openscad" "$CSG_REPO_ROOT" || csg_die 'Rendering the models failed.'
