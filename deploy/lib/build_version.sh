# shellcheck shell=bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Read the build counter and check the identity inside a built image, as deploy/BuildVersion.psm1 does.
# Contributors: None
#
# Sourced after common.sh. Each function is its namesake in BuildVersion.psm1, and the digests and
# identities are worked out by deploy/lib/csg_tool.py, the same to the byte as the module's.

# The number a counter file holds, 0 for none; a counter that holds anything else is refused, an empty one
# among them: a write cut short leaves one, and the numbers would start again.
csg_build_number() {
    local raw
    if [ ! -f "$1" ]; then
        printf '0\n'
        return 0
    fi
    raw="$(csg_trim "$(cat "$1")")"
    case "$raw" in
        '' | *[!0-9]*) csg_die "Invalid csiGarland build counter: $1" ;;
    esac
    printf '%s\n' "$((10#$raw))"
}

# Writes a counter whole or not at all, as Set-csiGarlandBuildNumber: the number into a file in the scratch
# folder, on the same disk, which then takes the counter's place. Fails, the counter as it was, if either fails.
csg_set_build_number() {
    local counter="$1" number="$2" next="$3/build-counter.next"
    if printf '%s\n' "$number" >"$next" && mv -f "$next" "$counter"; then
        return 0
    fi
    rm -f "$next"
    return 1
}

# A build that failed spends no number: the counter goes back to before, but only while it holds the one
# number that build took. Under the build's lock, taken here for the look and the write unless this script
# holds it. Succeeds when it went back.
csg_undo_build_number() {
    local counter="$1" before="$2" kind="${3:-}" now held=1 status=1
    if [ -n "$kind" ] && ! csg_lock_held "$kind"; then
        held=0
        csg_lock "$kind" 120
    fi
    if now="$(csg_build_number "$counter" 2>/dev/null)" && [ "$now" -eq $((before + 1)) ]; then
        printf '%s\n' "$before" >"$counter"
        status=0
    fi
    if [ "$held" -eq 0 ]; then
        csg_unlock "$kind"
    fi
    return "$status"
}

# Where PlatformIO keeps its packages and Pythons, as PlatformIO takes it.
csg_platformio_core() { printf '%s\n' "${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"; }
csg_platformio_packages() { printf '%s\n' "${PLATFORMIO_PACKAGES_DIR:-$(csg_platformio_core)/packages}"; }

# The PlatformIO environment of a profile, which is also its folder under firmware/app/.pio/build.
csg_firmware_environment() {
    if [ "$1" = Release ]; then printf 'esp32c6-release\n'; else printf 'esp32c6\n'; fi
}

csg_firmware_identity() { csg_tool firmware-identity "$1" "$2" "$3"; }
csg_bin_identity_problem() { csg_tool bin-identity-problem "$1" "$2"; }
csg_image_id() { csg_tool image-id "$1"; }
csg_source_identity() { csg_tool source-identity "$1"; }
csg_source_format() { csg_tool source-format; }
csg_utility_source_digest() {
    if [ -n "${2:-}" ]; then csg_tool utility-digest "$1" --revision "$2"; else csg_tool utility-digest "$1"; fi
}
csg_release_compiler() { csg_tool const release-compiler "$1" 2>/dev/null || true; }
csg_compiler_build() { csg_tool compiler-build "$1"; }
csg_release_amd64() { csg_tool const release-amd64; }

# Go keeps a cgo package compiled until something in its own folder changes, and the engine's sources are
# in firmware/app/src: a digest of them in the flags, which Go keys its cache on, recompiles it exactly when
# they change. Beside whatever flags were set, and in place of an earlier digest of these.
csg_set_engine_sources() {
    local digest flag kept=''
    digest="$(csg_tool engine-digest "$1/firmware/app/src")"
    for flag in ${CGO_CPPFLAGS:-}; do
        case "$flag" in
            -DCSG_ENGINE_SOURCES=*) ;;
            *) kept="$kept$flag " ;;
        esac
    done
    export CGO_CPPFLAGS="$kept-DCSG_ENGINE_SOURCES=$digest"
}

# A copy of what git would keep of the firmware and the utility, under the destination, for a build that
# leaves this tree as it is: a repository of its own, so its digests are worked out as they are here.
# Prints the copy's firmware/app.
csg_copy_sources() {
    local repo="$1" dest="$2" path top
    mkdir -p "$dest"
    git -C "$repo" ls-files -z --cached --others --exclude-standard -- firmware/app cmd internal deploy go.mod go.sum LICENSE THIRD_PARTY_NOTICES.md |
        while IFS= read -r -d '' path; do
            if [ -f "$repo/$path" ]; then
                mkdir -p "$dest/$(dirname "$path")"
                cp "$repo/$path" "$dest/$path"
            fi
        done
    [ -f "$dest/go.mod" ] || csg_die "git ls-files listed no sources in $repo."
    for path in .gitattributes .gitignore; do
        if [ -f "$repo/$path" ]; then cp "$repo/$path" "$dest/$path"; fi
    done
    # Had init failed, git would look further up for a repository, this one's among them, and add the copy
    # to that: so the copy has to be the root of its own before anything is added.
    git -C "$dest" init --quiet || csg_die "git could not make a repository of the copy in $dest."
    top="$(git -C "$dest" rev-parse --show-toplevel)" || csg_die "The copy in $dest is not a repository of its own."
    [ "$(cd "$top" && pwd -P)" = "$(cd "$dest" && pwd -P)" ] ||
        csg_die "The copy in $dest is not a repository of its own (git says $top): nothing was added."
    git -C "$dest" add --all || csg_die "git could not take the copy in $dest."
    if [ -d "$repo/firmware/app/managed_components" ]; then
        cp -R "$repo/firmware/app/managed_components" "$dest/firmware/app/managed_components"
    fi
    printf '%s\n' "$dest/firmware/app"
}
