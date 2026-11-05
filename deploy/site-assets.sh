#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Copy the renders and screen pictures the public site shows from docs/images into site/assets.
# Contributors: None
#
# The bash twin of deploy/site-assets.ps1, for Linux and macOS. The site shows what the documents show:
# the renders of the 3D models, and the pictures of the screen, portal and utility that
# deploy/new-doc-images.sh makes from the real code. Run this after either changes.
#
# Example:
#   bash deploy/site-assets.sh
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/lib/common.sh"

[ "$#" -eq 0 ] || csg_die "site-assets.sh takes no arguments."
images="$CSG_REPO_ROOT/docs/images"
target="$CSG_REPO_ROOT/site/assets"
# The site's name for each, and where it comes from under docs/images.
pictures=(
    garland-hanging.png=diffusers/garland_octet_hanging.png
    garland-octet.png=diffusers/garland_octet.png
    figurine-tree.png=diffusers/diffuser_tree.png
    figurine-star.png=diffusers/diffuser_star.png
    figurine-icicle.png=diffusers/diffuser_icicle.png
    figurine-pinecone.png=diffusers/diffuser_pinecone.png
    figurine-bell.png=diffusers/diffuser_bell.png
    figurine-snowman.png=diffusers/diffuser_snowman.png
    figurine-gingerbread.png=diffusers/diffuser_gingerbread.png
    figurine-lantern.png=diffusers/diffuser_lantern.png
    enclosure.png=hardware/enclosure_exploded_preview.png
    enclosure-exploded.png=hardware/enclosure_exploded_preview.png
    screen-playing.png=screens/playing.png
    screen-standby.png=screens/standby.png
    screen-access.png=screens/access.png
    screen-update.png=screens/update-receiving.png
    portal-effects.png=portal/effects-short.png
    portal-schedule.png=portal/schedule.png
    utility-effects.png=utility/effects-short.png
    utility-tune.png=utility/tune.png
    utility-firmware.png=utility/firmware.png
    schematic.svg=schematic.svg
)
mkdir -p "$target"
for picture in "${pictures[@]}"; do
    from="$images/${picture#*=}"
    [ -f "$from" ] || csg_die "No picture at $from."
    cp -f "$from" "$target/${picture%%=*}"
done
csg_say "${#pictures[@]} pictures copied to $target."
