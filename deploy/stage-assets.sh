#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Stage the signed OTA package of the current firmware under assets/.
# Contributors: None
#
# The bash twin of deploy/stage-assets.ps1, for Linux and macOS, staging the same files and manifest.
# Takes the last build when firmware.bin names the current build number in both of its identity strings,
# firmware.ota holds exactly that image, and it was made from the firmware sources as they stand, whose
# notices go with it. Otherwise it builds through flash-device.sh, which spends one build number. A build
# that fails, or fails to match, stops here, gives its number back, and assets/ keeps what it had.
#
# Options
#
#   --new-build
#       Spend a build number even though the last build is usable.
#
#   --build-profile Release|Dev
#       Release, the default, is what goes into a release. Dev keeps the debug information and is staged
#       as 1.0.0.102-dev. Each profile builds in its own folder, so the last build of the other is never taken.
#
# Writes assets/firmware/csigarland-<version>.ota, a .sha256 and a .manifest.json beside it, and says each
# file it staged on stdout as "Staged <path>".
#
# Examples:
#   bash deploy/stage-assets.sh
#   bash deploy/stage-assets.sh --new-build --build-profile Dev
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/lib/common.sh"
. "$CSG_LIB_DIR/build_version.sh"
. "$CSG_LIB_DIR/signing_python.sh"

new_build=0
profile=Release
while [ "$#" -gt 0 ]; do
    case "$1" in
        --new-build) new_build=1 ;;
        --build-profile) csg_need_value "$1" "$#"; profile="$(csg_profile "$2")"; shift ;;
        *) csg_die "stage-assets.sh does not take $1." ;;
    esac
    shift
done

repo="$CSG_REPO_ROOT"
firmware_dir="$repo/firmware/app"
ini_path="$firmware_dir/platformio.ini"
counter_path="$firmware_dir/.csigarland_build_counter"
output_dir="$repo/assets/firmware"
build_dir="$firmware_dir/.pio/build/$(csg_firmware_environment "$profile")"
bin_path="$build_dir/firmware.bin"
ota_path="$build_dir/firmware.ota"
stamp_path="$build_dir/firmware.source.json"
pack_path="$firmware_dir/generated/effects.pack"

# Without it the last build may carry a throwaway chain, which no garland should be sent.
[ -f "$repo/cert/ota-signing.local.crt" ] || csg_die 'No update certificate. Make one: bash deploy/new-signing-certificate.sh'

current_version() { csg_firmware_identity "$ini_path" "$(csg_build_number "$counter_path")" "$profile"; }

# Nothing when the build is usable, else what is wrong with it.
build_problem() {
    local version problem image source
    version="$(current_version)" || return 1
    problem="$(csg_bin_identity_problem "$bin_path" "$version")" || return 1
    if [ -n "$problem" ]; then printf '%s\n' "$problem"; return 0; fi
    if [ ! -f "$stamp_path" ]; then printf 'nothing records what the last build was made from\n'; return 0; fi
    image="$(csg_image_id "$bin_path")" || return 1
    if [ "$(csg_tool json-get "$stamp_path" image '')" != "$image" ]; then printf '%s is about another image\n' "$stamp_path"; return 0; fi
    # A build before the zlib was recorded says nothing of the bytes of its portal's pages.
    if [ -z "$(csg_tool json-get "$stamp_path" zlib '')" ]; then printf 'the last build records no zlib, as one made before builds did\n'; return 0; fi
    # The notices staged beside it are this checkout's, so they describe it only if it was made here.
    source="$(csg_source_identity "$repo")" || return 1
    if [ "$(csg_tool json-get "$stamp_path" source_sha256 '')" != "$(printf '%s' "$source" | csg_tool json-get - source_sha256)" ]; then
        printf 'the firmware sources changed since the last build, and the notices staged with it would describe these\n'
        return 0
    fi
    csg_tool ota-package-problem "$ota_path" "$bin_path" "$pack_path"
}

# A build made here and then refused by any check, or one whose staging failed, gives its number back: the
# next build takes it. A build that was here before keeps its number, whatever happens to its staging.
before=-1
staged=0
destination=''
stage_finally() {
    if [ "$staged" -eq 0 ]; then
        if [ -n "$destination" ] && [ "$before" -ge 0 ]; then
            rm -f "$destination" "$destination.sha256" "$destination.manifest.json"
        fi
        if [ "$before" -ge 0 ] && csg_undo_build_number "$counter_path" "$before"; then
            csg_warn "Nothing was staged, so build number $((before + 1)) is not spent: the next build takes it."
        fi
    fi
}
# The firmware build lock of this checkout, held from the look at the last build, through a build of its
# own, to the set it stages. flash-device.sh runs under it, told so.
csg_lock firmware
csg_on_exit stage_finally

if [ "$new_build" -eq 1 ]; then reason='a new build was asked for'; else reason="$(build_problem)"; fi
if [ -z "$reason" ]; then
    csg_say "Using the last build, $(current_version)."
else
    csg_say "Building, as $reason."
    before="$(csg_build_number "$counter_path")"
    if ! bash "$CSG_DEPLOY_DIR/flash-device.sh" --build-only --build-profile "$profile" --lock-held >&2; then
        # flash-device.sh gave it back already, if it was taken.
        before=-1
        csg_die 'Build failed. Nothing was staged.'
    fi
    problem="$(build_problem)"
    [ -z "$problem" ] || csg_die "The build does not match: $problem. Nothing was staged."
fi

# One set, copied at once and checked as a set, from which all that is staged is taken: another build of
# this profile may replace the build folder's files meanwhile.
version="$(current_version)"
csg_temp_dir set stage
for file in "$bin_path" "$ota_path" "$stamp_path" "$pack_path"; do
    cp -f "$file" "$set/$(basename "$file")"
done
bin_path="$set/firmware.bin"
ota_path="$set/firmware.ota"
stamp_path="$set/firmware.source.json"
pack_path="$set/effects.pack"
problem="$(build_problem)"
if [ -n "$problem" ] || [ "$version" != "$(current_version)" ]; then
    csg_die "The build changed while it was staged${problem:+: $problem}. Nothing was staged: stage it again."
fi

# Judged as a garland would judge it: signed under the owner's root, whatever made the build. One copy of
# the root, taken once, is both what it is judged by and what goes out beside it.
root_path="$set/csigarland-ota-root.crt"
cp -f "$repo/cert/ota-root-ca.local.crt" "$root_path"
checked="$(csg_own_signature "$ota_path" "$root_path")" ||
    csg_die "The update file is not signed under this repository's root, as a throwaway build would not be. Nothing was staged."
printf '%s' "$checked" >"$set/checked.json"

# A binary passed on owes the notices of what it holds, and the file cannot be judged with no Go.
if command -v go >/dev/null 2>&1; then
    go -C "$repo" mod download || csg_die "Go could not fetch the utility's modules, whose licences the notices hold. Nothing was staged."
    bash "$CSG_DEPLOY_DIR/new-third-party-notices.sh" --check --build-profile "$profile" ||
        csg_die 'THIRD_PARTY_NOTICES.md is not what this build linked. Nothing was staged.'
    notices_checked=true
else
    csg_warn 'No Go here, so THIRD_PARTY_NOTICES.md was not checked against this build: no release takes it.'
    notices_checked=false
fi

name="csigarland-$version.ota"
destination="$output_dir/$name"
mkdir -p "$output_dir"
cp -f "$ota_path" "$destination"
# A binary passed on owes these with it, and the root the image trusts goes beside it for anyone to build
# the release again with.
cp -f "$repo/THIRD_PARTY_NOTICES.md" "$repo/LICENSE" "$output_dir/"
cp -f "$root_path" "$output_dir/csigarland-ota-root.crt"
hash="$(csg_tool sha256 "$destination")"
printf '%s  %s\n' "$hash" "$name" >"$destination.sha256"

# What this file is, so that it can be told from another of the same version number.
csg_tool json-build --base "$set/checked.json" --str version "$version" \
    --copy commit "$stamp_path" commit --copy dirty "$stamp_path" dirty \
    --copy source_sha256 "$stamp_path" source_sha256 --copy source_format "$stamp_path" source_format \
    --copy image_id "$stamp_path" image --copy zlib "$stamp_path" zlib --str ota_sha256 "$hash" \
    --str notices_sha256 "$(csg_tool sha256 "$output_dir/THIRD_PARTY_NOTICES.md")" --bool notices_checked "$notices_checked" \
    --str license_sha256 "$(csg_tool sha256 "$output_dir/LICENSE")" \
    --raw effects "$(csg_tool json-len "$firmware_dir/effects/catalog.json" effects)" >"$destination.manifest.json"
staged=1
printf 'Staged %s\n' "$destination" "$destination.sha256" "$destination.manifest.json" "$output_dir/csigarland-ota-root.crt"
