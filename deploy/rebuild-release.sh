#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Build a published release again from its sources, and show it is the same to the byte.
# Contributors: None
#
# The bash twin of deploy/rebuild-release.ps1, for Linux and macOS. Run in a checkout of the release's
# sources; docs/guides/rebuild.md says how to get them and the tools. The firmware is built with the
# release's own root certificate, which every image holds and a release publishes beside its update file,
# so no key is needed: the image and the effects pack come out unsigned, and are compared with the digests
# the release's manifest holds. The utility is built with the build number its manifest names, on this
# computer's platform: a utility of another platform is built again on one of that platform. Builds in a
# copy of the checkout's sources under temp/rebuild-release, which is kept for a look at what was built.
#
# Options
#
#   --firmware-manifest <path>   A release's csigarland-<version>.ota.manifest.json.
#   --root <path>                The release's root certificate, by default the one beside that manifest.
#   --utility-manifest <path>    A release's csigarland-<version>-<platform>.manifest.json.
#
# Example:
#   bash deploy/rebuild-release.sh --firmware-manifest ~/Downloads/csigarland-1.1.0.113.ota.manifest.json
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/lib/common.sh"
. "$CSG_LIB_DIR/build_version.sh"

firmware_manifest='' root='' utility_manifest=''
while [ "$#" -gt 0 ]; do
    case "$1" in
        --firmware-manifest) csg_need_value "$1" "$#"; firmware_manifest="$2"; shift ;;
        --root) csg_need_value "$1" "$#"; root="$2"; shift ;;
        --utility-manifest) csg_need_value "$1" "$#"; utility_manifest="$2"; shift ;;
        *) csg_die "rebuild-release.sh does not take $1." ;;
    esac
    shift
done
[ -n "$firmware_manifest$utility_manifest" ] || csg_die 'Name a release: --firmware-manifest, --utility-manifest, or both.'
repo="$CSG_REPO_ROOT"
copy="$repo/temp/rebuild-release"
rm -rf "$copy"
firmware_dir="$(csg_copy_sources "$repo" "$copy")"
csg_say "Building in a copy of the sources, $copy."

# A version's base, build number and profile: 1.0.0.27 is 27 of 1.0.0, a Release; 1.0.0.27-dev a Dev.
split_version() {
    local version="$1" plain kind=Release
    case "$version" in *-dev) kind=Dev; plain="${version%-dev}" ;; *) plain="$version" ;; esac
    printf '%s' "$plain" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$' || csg_die "$version is not a csiGarland version."
    version_base="$(printf '%s' "$plain" | sed -E 's/^([0-9]+\.[0-9]+\.[0-9]+)\.[0-9]+(-[0-9A-Za-z.]+)?$/\1\2/')"
    version_number="$(printf '%s' "$plain" | sed -E 's/^[0-9]+\.[0-9]+\.[0-9]+\.([0-9]+).*$/\1/')"
    version_profile="$kind"
}

all=1
verdict() {
    if [ "$2" = "$3" ]; then
        printf '%-6s %-22s made %s\n' same "$1" "$2" >&2
    else
        printf '%-6s %-22s made %s\n' DIFFER "$1" "$2" >&2
        printf '       %-22s released %s\n' '' "$3" >&2
        all=0
    fi
}

if [ -n "$firmware_manifest" ]; then
    csg_require pio 'PlatformIO (pio) is not on PATH.'
    if [ -z "$root" ]; then root="$(dirname "$firmware_manifest")/csigarland-ota-root.crt"; fi
    [ -f "$root" ] || csg_die "No root certificate at $root. A release publishes csigarland-ota-root.crt beside its update file."
    root_sha="$(csg_tool pem-sha256 "$root")"
    wanted_root="$(csg_tool json-get "$firmware_manifest" root_sha256 '')"
    [ "$root_sha" = "$wanted_root" ] || csg_die "$root is not the root of this release: its SHA-256 is $root_sha, the manifest's $wanted_root."
    split_version "$(csg_tool json-get "$firmware_manifest" version)"
    ini_base="$(sed -n 's/^CSIGARLAND_FIRMWARE_BASE[[:space:]]*=[[:space:]]*\([^[:space:]]*\)[[:space:]]*$/\1/p' "$repo/firmware/app/platformio.ini")"
    [ "$ini_base" = "$version_base" ] || csg_die "This checkout is firmware $ini_base, and the release $version_base: check out the release's sources first."
    source="$(csg_source_identity "$repo")"
    # A manifest of 1.0.0 records its digest in an older form, which says nothing here: the files decide.
    if [ "$(csg_tool json-get "$firmware_manifest" source_format '')" = "$(printf '%s' "$source" | csg_tool json-get - source_format)" ] &&
        [ "$(csg_tool json-get "$firmware_manifest" source_sha256 '')" != "$(printf '%s' "$source" | csg_tool json-get - source_sha256)" ]; then
        csg_warn "The firmware sources here are not the ones the release records: expect them to differ."
    fi
    environment="$(csg_firmware_environment "$version_profile")"
    csg_say "Building firmware $(csg_tool json-get "$firmware_manifest" version) again."
    # Only the lines that say how it went, from the whole build's output kept beside the copy.
    status=0
    (
        unset CSIGARLAND_THROWAWAY_SIGNING
        CSIGARLAND_RELEASE_ROOT="$(csg_abs "$root")"
        export CSIGARLAND_RELEASE_ROOT CSIGARLAND_BUILD_NUMBER="$version_number"
        pio run -d "$firmware_dir" -e "$environment" >"$copy/firmware-build.log" 2>&1
    ) || status=$?
    grep -E 'error|SUCCESS|FAILED|CSIGARLAND_VERSION' "$copy/firmware-build.log" >&2 || true
    [ "$status" -eq 0 ] || csg_die "The firmware's build failed with exit code $status: $copy/firmware-build.log has all it said."
    # The image holds the portal's pages gzipped by the zlib of the Python that built it: another zlib than the
    # release's, as the manifest or the tag's platformio.ini names it, packs them into other bytes.
    made_zlib="$(csg_trim "$(cat "$firmware_dir/generated/portal_zlib.txt" 2>/dev/null || true)")"
    release_zlib="$(csg_tool json-get "$firmware_manifest" zlib '')"
    if [ -z "$release_zlib" ]; then
        release_zlib="$(sed -n 's/^CSIGARLAND_RELEASE_ZLIB[[:space:]]*=[[:space:]]*\([^[:space:]]*\)[[:space:]]*$/\1/p' "$firmware_dir/platformio.ini")"
        release_zlib="${release_zlib%%$'\n'*}"
    fi
    if [ -n "$release_zlib" ] && [ "$made_zlib" != "$release_zlib" ]; then
        csg_warn "The portal's pages were gzipped with zlib $made_zlib here, the release's with $release_zlib: expect the image to differ."
    fi
    verdict 'firmware image' "$(csg_tool sha256 "$firmware_dir/.pio/build/$environment/firmware.bin")" "$(csg_tool json-get "$firmware_manifest" image_sha256 '')"
    verdict 'effects pack' "$(csg_tool sha256 "$firmware_dir/generated/effects.pack")" "$(csg_tool json-get "$firmware_manifest" pack_sha256 '')"
fi

if [ -n "$utility_manifest" ]; then
    csg_require go 'Go is not on PATH.'
    split_version "$(csg_tool json-get "$utility_manifest" version)"
    compiler="$(csg_trim "$(csg_first_line gcc --version 2>/dev/null || true)")"
    released_compiler="$(csg_tool json-get "$utility_manifest" compiler '')"
    if [ "$compiler" != "$released_compiler" ]; then
        csg_warn "The C++ compiler here is $compiler, the release's $released_compiler: expect the files to differ."
    fi
    [ -f "$copy/deploy/build-utility.sh" ] ||
        csg_die "The release's sources hold no deploy/build-utility.sh: build it again with pwsh -File deploy/rebuild-release.ps1."
    csg_say "Building the utility $(csg_tool json-get "$utility_manifest" version) again."
    made="$(bash "$copy/deploy/build-utility.sh" --build-number "$version_number" --build-profile "$version_profile")" ||
        csg_die "The utility's build failed."
    made="${made##*$'\n'}"
    if [ "$(csg_tool json-get "$made.manifest.json" go '')" != "$(csg_tool json-get "$utility_manifest" go '')" ]; then
        csg_warn "Go was $(csg_tool json-get "$made.manifest.json" go ''), the release's $(csg_tool json-get "$utility_manifest" go '')."
    fi
    verdict utility "$(csg_tool sha256 "$made")" "$(csg_tool json-get "$utility_manifest" sha256 '')"
fi

[ "$all" -eq 1 ] || csg_die 'What was built again is not the release, as above.'
csg_say 'Built again, the same to the byte as the release.'
