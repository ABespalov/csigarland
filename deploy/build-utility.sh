#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Builds the csiGarland utility for this computer, into assets/utility.
# Contributors: None
#
# The bash twin of deploy/build-utility.ps1, for Linux and macOS, with the same counter, fingerprint and
# manifest: the two can take turns in one checkout. Needs Go, and a C++20 compiler on the PATH for cgo, as
# the effect engine is the firmware's own code: GCC on Linux, the Xcode command line tools on macOS. On
# Linux the result uses the system's Wayland, X11 and EGL libraries, which a desktop has.
#
# The utility and the firmware are built apart and each counts its own builds. The base version is in
# cmd/csigarland/version.go, and a build is it with its number, such as 1.0.0.27. Sources the same as the
# last build's, which the fingerprint tells, keep its number and its file. A build that fails spends no
# number. The same sources, build number, Go and C++ compiler make the same file to the byte: Go is the
# toolchain go.mod names, which GOTOOLCHAIN is set to, and the paths of this machine are mapped away. A
# .manifest.json beside the file says what made it.
#
# Options
#
#   --force
#       Build and spend a number even though nothing changed.
#
#   --build-number <n>
#       Build that number outright, as a release's manifest names it, and leave the counter as it is.
#
#   --build-profile Release|Dev
#       Release, the default, links with no symbols or debug information and gives the plain version,
#       such as 1.0.0.27. Dev keeps them, for a debugger, and is 1.0.0.27-dev.
#
# Writes the file's path to stdout. To build and start it, use deploy/run.sh.
#
# Example:
#   bash deploy/build-utility.sh
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/lib/common.sh"
. "$CSG_LIB_DIR/build_version.sh"

force=0
named_number=-1
profile=Release
while [ "$#" -gt 0 ]; do
    case "$1" in
        --force) force=1 ;;
        --build-number)
            csg_need_value "$1" "$#"
            case "$2" in '' | *[!0-9]*) csg_die "--build-number is a number, not $2." ;; esac
            named_number="$2"
            shift
            ;;
        --build-profile)
            csg_need_value "$1" "$#"
            profile="$(csg_profile "$2")"
            shift
            ;;
        *) csg_die "build-utility.sh does not take $1." ;;
    esac
    shift
done

csg_require go 'Go is not on PATH.'
repo="$CSG_REPO_ROOT"
target="$repo/assets/utility"
version_file="$repo/cmd/csigarland/version.go"
counter_path="$repo/cmd/csigarland/.csigarland_build_counter"
fingerprint_path="$repo/cmd/csigarland/.csigarland_build_fingerprint"

stored_text() {
    if [ -f "$1" ]; then csg_trim "$(cat "$1")"; fi
}

toolchain="$(sed -n 's/^toolchain \(go[0-9.]*\)[[:space:]]*$/\1/p' "$repo/go.mod")"
toolchain="${toolchain%%$'\n'*}"
[ -n "$toolchain" ] || csg_die 'No toolchain line in go.mod.'
export GOTOOLCHAIN="$toolchain"

base_line="$(grep -E 'Version = "[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?"' "$version_file" || true)"
base_line="${base_line%%$'\n'*}"
[ -n "$base_line" ] || csg_die "No base version in $version_file."
base_number="$(printf '%s' "$base_line" | sed -E 's/.*Version = "([0-9]+\.[0-9]+\.[0-9]+)(-[0-9A-Za-z.]+)?".*/\1/')"
base_suffix="$(printf '%s' "$base_line" | sed -E 's/.*Version = "([0-9]+\.[0-9]+\.[0-9]+)(-[0-9A-Za-z.]+)?".*/\2/')"

# One utility build of this checkout at a time, from reading the counter to writing it.
csg_lock utility
# 0 with none; one there that holds no number is refused, as starting again would give numbers out twice.
number="$(csg_build_number "$counter_path")"
system="$(go env GOOS)"
machine="$(go env GOARCH)"
extension=''
if [ "$system" = windows ]; then extension='.exe'; fi
# The compiler the releases for this platform are made with; a platform with none named has no release yet.
release_compiler="$(csg_release_compiler "$system-$machine")"

output_version() {
    local version="$base_number.$1$base_suffix"
    if [ "$profile" = Dev ]; then version="$version-dev"; fi
    printf '%s\n' "$version"
}
output_path() { printf '%s/csigarland-%s-%s-%s%s\n' "$target" "$(output_version "$1")" "$system" "$machine" "$extension"; }

compiler="$(csg_trim "$(csg_first_line gcc --version 2>/dev/null || true)")"
# On x86-64, the level any 64-bit PC runs. No workspace of a folder above, and no GOFLAGS of the shell's:
# either would change what is built from the sources here.
if [ "$machine" = amd64 ]; then
    GOAMD64="$(csg_release_amd64)"
    export GOAMD64
fi
export GOWORK=off
unset GOFLAGS GOEXPERIMENT GOFIPS140
# What Go will really use is asked, and recorded: go env -w may set what the shell does not.
go_env="$(go env -json CC CXX CGO_CFLAGS CGO_CPPFLAGS CGO_CXXFLAGS CGO_LDFLAGS GOAMD64 GOFLAGS GOWORK GOEXPERIMENT GOFIPS140)"
env_of() { printf '%s' "$go_env" | csg_tool json-get - "$1" ''; }
for name in GOFLAGS GOEXPERIMENT; do
    if [ -n "$(env_of "$name")" ]; then
        csg_warn "Go takes $name '$(env_of "$name")' from its own settings, go env -w: this builds, but not the same file as a release. Unset it: go env -u $name"
    fi
done
if [ "$(env_of GOFIPS140)" != off ]; then
    csg_warn "Go takes GOFIPS140 '$(env_of GOFIPS140)' from its own settings, go env -w: this builds, but not the same file as a release. Unset it: go env -u GOFIPS140"
fi
go_cc="$(env_of CC)"
go_cxx="$(env_of CXX)"
# As Go runs them, a program and its arguments: one that cannot be asked its version is recorded as unknown.
version_of() {
    local parts=() part said
    while IFS= read -r part; do parts[${#parts[@]}]="$part"; done < <(csg_tool split-command "$1")
    if [ "${#parts[@]}" -eq 0 ] || ! said="$(csg_first_line "${parts[@]}" --version 2>/dev/null)"; then
        csg_warn "$1 could not be asked its version."
        printf 'unknown\n'
        return 0
    fi
    csg_trim "$said"
    printf '\n'
}
cc="$(version_of "$go_cc")"
cxx="$(version_of "$go_cxx")"
go_version="$(csg_trim "$(go version)")"
for command in "$go_cc" "$go_cxx"; do
    if [ "$(csg_count_lines "$(csg_tool split-command "$command")")" -gt 1 ]; then
        csg_warn "cgo runs '$command', with arguments of its own: this builds, but not the same file as a release."
    fi
done
cgo_flags_json='{'
cgo_values=''
flags_differ=''
separator=''
while IFS= read -r name; do
    value="$(env_of "$name")"
    cgo_flags_json="$cgo_flags_json$separator\"$name\": $(printf '%s' "$value" | csg_python -c 'import json, sys; print(json.dumps(sys.stdin.read()))')"
    separator=', '
    cgo_values="$cgo_values$value"$'\n'
    if [ "$value" != "$(csg_tool const cgo-flag "$name")" ]; then
        flags_differ="${flags_differ:+$flags_differ, }$name"
    fi
done < <(csg_tool const cgo-flags)
cgo_flags_json="$cgo_flags_json}"
if [ -z "$release_compiler" ]; then
    csg_warn "No release compiler is named for $system-$machine in deploy/lib/csg_tool.py and deploy/BuildVersion.psm1: this builds, but no release takes it yet."
else
    wanted="$(csg_compiler_build "$release_compiler")"
    if [ "$(csg_compiler_build "$cc")" != "$wanted" ] || [ "$(csg_compiler_build "$cxx")" != "$wanted" ] || [ -n "$flags_differ" ]; then
        csg_warn "cgo compiles with $cc and $cxx${flags_differ:+, and $flags_differ set}, not $release_compiler as Go sets it: this builds, but not the same file as a release."
    fi
fi

# A module go.mod replaces is built from other bytes than the notices read the licence of: a Release
# refuses it, and a Dev build says so.
replaces="$(go -C "$repo" mod edit -json | csg_tool mod-replaces | tr '\n' ';' | sed 's/;$//; s/;/; /g')"
if [ -n "$replaces" ]; then
    said="go.mod replaces a module ($replaces), which the notices and a tag do not follow"
    if [ "$profile" = Release ]; then csg_die "$said: a Release takes modules as go.sum holds them. Nothing was built."; fi
    csg_warn "$said: this builds, but no release takes it."
fi
for name in LICENSE THIRD_PARTY_NOTICES.md; do
    [ -f "$repo/$name" ] || csg_die "$name is not in $repo, and the manifest names it: nothing was built."
done
# The sources and every tool and flag the file is made with, and the notices it is passed on with.
source_digest="$(csg_utility_source_digest "$repo")"
notices_digests="$(csg_tool sha256 --upper "$repo/THIRD_PARTY_NOTICES.md" "$repo/LICENSE")"
fingerprint_text="$profile"$'\n'"$go_version"$'\n'"$compiler"$'\n'"$cc"$'\n'"$cxx"$'\n'"$go_cc"$'\n'"$go_cxx"$'\n'"$(env_of GOAMD64)"$'\n'"$(env_of GOFLAGS)"$'\n'"$(env_of GOWORK)"$'\n'"$(env_of GOEXPERIMENT)"$'\n'"$(env_of GOFIPS140)"$'\n'"$cgo_values$notices_digests"
fingerprint="$source_digest|$(printf '%s' "$fingerprint_text" | csg_tool sha256-text)"
kept="$(output_path "$number")"
kept_checked=false
if [ -f "$kept.manifest.json" ]; then
    kept_checked="$(csg_tool json-get "$kept.manifest.json" notices_checked false)"
fi
if [ "$named_number" -lt 0 ] && [ "$force" -eq 0 ] && [ "$number" -gt 0 ] && [ "$kept_checked" = true ] &&
    [ "$fingerprint" = "$(stored_text "$fingerprint_path")" ] && [ -f "$kept" ]; then
    csg_say "Sources and tools unchanged, so $kept stands."
    printf '%s\n' "$kept"
    exit 0
fi

if [ "$named_number" -ge 0 ]; then number="$named_number"; else number=$((number + 1)); fi
version="$(output_version "$number")"
final="$(output_path "$number")"

link_flags="-X main.Version=$version"
if [ "$profile" = Release ]; then link_flags="-s -w $link_flags"; fi
if [ "$system" = windows ]; then link_flags="$link_flags -H=windowsgui -extldflags=-Wl,--no-insert-timestamp"; fi
mkdir -p "$target"
# Made and checked in a folder of this run's own, and only then put in place: a build that fails, or is
# stopped, spends no number, and leaves nothing.
csg_temp_dir work utility-build keep
output="$work/$(basename "$final")"
placed=0
built=0
# A pair there already, of a number named again, moved aside into this run's folder until the build stands.
aside=()
# What was moved aside comes back, so the file and its manifest stay of one build. False if any of it
# could not, which then stays in this run's folder.
put_back_aside() {
    local there previous failed=0
    for there in ${aside[@]+"${aside[@]}"}; do
        previous="$work/previous-$(basename "$there")"
        if [ -e "$previous" ] && ! mv -f "$previous" "$there"; then
            failed=1
        fi
    done
    return "$failed"
}
finish() {
    local keep_work=0
    if [ "$built" -eq 0 ]; then
        # A file put in place, under a number not spent or with no manifest beside it, is no build: it goes,
        # and its manifest, before what was there comes back.
        if [ "$placed" -eq 1 ]; then
            rm -f "$final" "$final.manifest.json"
        fi
        if ! put_back_aside; then
            keep_work=1
            csg_warn "The build that was there could not all be put back: what is left of it is in $work."
        fi
        if [ "$named_number" -lt 0 ]; then
            csg_warn "The build failed, so build number $number is not spent: the next build takes it."
        fi
    fi
    if [ "$keep_work" -eq 0 ]; then
        rm -rf "$work"
    fi
}
csg_on_exit finish

export CGO_ENABLED=1
csg_set_engine_sources "$repo"
# The engine's C++ names its files by where this clone is; mapped to the repository's root, it does not.
# Quoted, as Go reads these flags: a path may hold a space.
export CGO_CPPFLAGS="$CGO_CPPFLAGS \"-ffile-prefix-map=$repo=.\""
go -C "$repo" build -mod=readonly -trimpath -buildvcs=false "-ldflags=$link_flags" -o "$output" ./cmd/csigarland ||
    csg_die 'Building the utility failed.'
# A file saved while it built, which the file may hold and the digest not, is found here.
[ "$(csg_utility_source_digest "$repo")" = "$source_digest" ] ||
    csg_die 'The sources changed while the utility was built: build it again, once they are left alone.'
# Every library of the compiler's a Windows build holds is one THIRD_PARTY_NOTICES.md names. On Linux
# and macOS the C++ runtime is the system's own, linked as a shared library, and passed on with nothing.
if [ "$system" = windows ]; then
    check="$repo/temp/utility-link"
    mkdir -p "$check"
    rm -f "$check/csigarland.map" "$check/csigarland.exe"
    map_flags="${link_flags% -extldflags=*} -extldflags=-Wl,--no-insert-timestamp,-Map=temp/utility-link/csigarland.map"
    go -C "$repo" build -mod=readonly -trimpath -buildvcs=false "-ldflags=$map_flags" -o "$check/csigarland.exe" ./cmd/csigarland ||
        csg_die 'Linking the utility for its map failed.'
    [ -f "$check/csigarland.map" ] || csg_die "The linker wrote no map to $check/csigarland.map, so what the utility links cannot be checked."
    unknown=''
    while IFS= read -r library; do
        csg_tool const library-group "$library" >/dev/null || unknown="${unknown:+$unknown, }$library"
    done < <(csg_tool link-map-libraries "$check/csigarland.map")
    [ -z "$unknown" ] || csg_die "The utility links $unknown, which THIRD_PARTY_NOTICES.md has no notice for: name it in deploy/lib/csg_tool.py and deploy/BuildVersion.psm1."
fi
# Whether THIRD_PARTY_NOTICES.md names what this build holds, its Go modules among them: notices out of
# date leave the build standing, and say so.
notices_checked=true
if ! notices_said="$(bash "$CSG_DEPLOY_DIR/new-third-party-notices.sh" --utility-only 2>&1)"; then
    notices_checked=false
    csg_warn "THIRD_PARTY_NOTICES.md was not found to name what this build holds, so no release takes it: ${notices_said##*$'\n'}"
fi
hash="$(csg_tool sha256 "$output")"
commit="$(git -C "$repo" rev-parse HEAD 2>/dev/null || true)"
commit_args=(--null commit)
if [ -n "$commit" ]; then commit_args=(--str commit "$commit"); fi
csg_tool json-build \
    --str version "$version" --str platform "$system-$machine" --str profile "$profile" --str sha256 "$hash" \
    --str go "$go_version" --str compiler "$compiler" --str cc "$cc" --str cxx "$cxx" \
    --str cc_command "$go_cc" --str cxx_command "$go_cxx" --raw cgo_flags "$cgo_flags_json" \
    --str goamd64 "$(env_of GOAMD64)" --str goflags "$(env_of GOFLAGS)" --str gowork "$(env_of GOWORK)" \
    --str goexperiment "$(env_of GOEXPERIMENT)" --str gofips140 "$(env_of GOFIPS140)" \
    --str source_sha256 "$source_digest" --str source_format "$(csg_source_format)" --bool notices_checked "$notices_checked" \
    --str notices_sha256 "$(csg_tool sha256 "$repo/THIRD_PARTY_NOTICES.md")" --str license_sha256 "$(csg_tool sha256 "$repo/LICENSE")" \
    "${commit_args[@]}" >"$output.manifest.json"

# The file first, then its manifest, each replacing any of that name: a pair there already, of a number
# named again, is moved aside first, and finish puts it back if anything fails or stops the run before the
# build stands. Named before it is moved, so a stop in between leaves nothing out of the count.
for there in "$final" "$final.manifest.json"; do
    if [ -e "$there" ]; then
        aside[${#aside[@]}]="$there"
        mv -f "$there" "$work/previous-$(basename "$there")" || csg_die "Could not move $there aside."
    fi
done
placed=1
if ! mv -f "$output" "$final" || ! mv -f "$output.manifest.json" "$final.manifest.json"; then
    csg_die "Could not put $final in place."
fi
# The counter is what spends the number. Once it is written the build stands. Whole or not at all: a write
# that fails keeps the number before, and finish the pair before.
if [ "$named_number" -lt 0 ]; then
    csg_set_build_number "$counter_path" "$number" "$work" || csg_die "Could not write the build counter $counter_path."
fi
built=1
if [ "$named_number" -lt 0 ] && ! printf '%s\n' "$fingerprint" >"$fingerprint_path"; then
    csg_warn 'The fingerprint could not be written, so the next build builds again.'
    rm -f "$fingerprint_path"
fi
csg_say "$final, $(($(csg_tool file-size "$final") / 1024)) KB, SHA-256 $hash"
# For deploy/run.sh.
printf '%s\n' "$final"
