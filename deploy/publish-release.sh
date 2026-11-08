#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Publish a staged release: its files on GitHub Releases, and the site's downloads and catalog pointing at them.
# Contributors: None
#
# The bash twin of deploy/publish-release.ps1, for Linux and macOS, with the same checks, notes and site
# files, by gh and deploy/lib/csg_release.py. A release is a base version, such as 1.0.0, with the
# firmware's update file and the utility as stage-assets and build-utility left them under assets/. A
# run publishes the firmware, the utility for one platform, or both; a release already on GitHub takes
# what a later run adds, and a file it holds already is never replaced. It never pushes: commit, tag and
# push are the owner's to do.
#
# Options
#
#   --release <a.b.c>          The release, such as 1.0.0, and its tag v1.0.0.
#   --only firmware|utility    Publish only the firmware, with its root certificate, or only the utility.
#   --platform <goos-goarch>   The utility's platform, as Go names it: windows-amd64 by default.
#                              macOS's file is its bundle zipped, csigarland-<version>-darwin-<arch>.zip.
#   --firmware-version <a.b.c.n>, --utility-version <a.b.c.n>
#                              The staged builds of that release, each only for what the run publishes.
#   --build-new                Build what the run publishes first, each spending a build number. The
#                              utility is built for this computer, so --platform must be its platform.
#   --site-from-release        For a release already made on GitHub: write site/downloads for it.
#   --site-only                Write the site's files to temp/site-preview-<release>/ and stop.
#   --what-if                  Check everything, and say what would be done instead of doing it.
#
# Examples:
#   bash deploy/publish-release.sh --release 1.1.0 --only utility --platform linux-amd64 --utility-version 1.1.0.4
#   bash deploy/publish-release.sh --release 1.1.0 --build-new --site-only
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/lib/common.sh"
. "$CSG_LIB_DIR/build_version.sh"
. "$CSG_LIB_DIR/signing_python.sh"

release='' only='' platform=windows-amd64 platform_given=0 firmware_version='' utility_version=''
build_new=0 site_only=0 site_from_release=0 what_if=0
version_arg() {
    printf '%s' "$2" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$' || csg_die "$1 is a version such as 1.0.0.27, not $2."
}
while [ "$#" -gt 0 ]; do
    case "$1" in
        --release) csg_need_value "$1" "$#"; release="$2"; shift ;;
        --only)
            csg_need_value "$1" "$#"
            case "$(csg_lower "$2")" in firmware) only=Firmware ;; utility) only=Utility ;; *) csg_die "--only is firmware or utility, not $2." ;; esac
            shift
            ;;
        --platform) csg_need_value "$1" "$#"; platform="$2"; platform_given=1; shift ;;
        --firmware-version) csg_need_value "$1" "$#"; version_arg "$1" "$2"; firmware_version="$2"; shift ;;
        --utility-version) csg_need_value "$1" "$#"; version_arg "$1" "$2"; utility_version="$2"; shift ;;
        --build-new) build_new=1 ;;
        --site-only) site_only=1 ;;
        --site-from-release) site_from_release=1 ;;
        --what-if) what_if=1 ;;
        *) csg_die "publish-release.sh does not take $1." ;;
    esac
    shift
done
printf '%s' "$release" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || csg_die "--release is a base such as 1.0.0, not '$release'."
# A preview asks nothing of GitHub.
if [ "$site_only" -eq 0 ]; then csg_require gh 'gh, the GitHub CLI, is not on PATH.'; fi
repo="$CSG_REPO_ROOT"
repository='ABespalov/csigarland'
tag="v$release"
with_firmware=1 with_utility=1
if [ "$only" = Utility ]; then with_firmware=0; fi
if [ "$only" = Firmware ]; then with_utility=0; fi
platform_name="$(csg_tool const platform-name "$platform" 2>/dev/null)" ||
    csg_die "--platform is one of $(csg_tool const platforms | tr '\n' ' '), not $platform."
[ "$with_utility" -eq 1 ] || [ "$platform_given" -eq 0 ] || csg_die '--platform is the utility'"'"'s, and --only firmware publishes no utility.'
[ "$with_firmware" -eq 1 ] || [ -z "$firmware_version" ] || csg_die '--only utility publishes no firmware: leave out --firmware-version.'
[ "$with_utility" -eq 1 ] || [ -z "$utility_version" ] || csg_die '--only firmware publishes no utility: leave out --utility-version.'
what=''
if [ "$with_firmware" -eq 1 ]; then what='the firmware'; fi
if [ "$with_utility" -eq 1 ]; then what="${what:+$what and }the utility for $platform_name"; fi

# One run of this script in this checkout at a time, from reading the site's lists to writing them.
csg_lock publish
run_id="$(csg_run_id)"
csg_temp_dir scratch "release-$release"

# What the tag holds, read from git with no checkout. The tag on GitHub must be the one here.
tag_read=0
read_tag() {
    local remote local_tag
    remote="$(gh api "repos/$repository/git/ref/tags/$tag" --jq '.object.sha' 2>/dev/null)" || csg_die "The tag $tag is not on GitHub. Commit, tag and push it first."
    local_tag="$(git -C "$repo" rev-parse --verify --quiet "refs/tags/$tag" || true)"
    [ -n "$local_tag" ] && [ "$local_tag" = "$remote" ] || csg_die "The tag $tag here is not the one on GitHub. Fetch it, and build from it."
    tag_commit="$(git -C "$repo" rev-parse "$tag^{commit}")"
    tag_firmware="$(csg_tool tree-digest --revision "$tag" "$repo" firmware/app)"
    tag_utility="$(csg_utility_source_digest "$repo" "$tag")"
    git -C "$repo" show "$tag:firmware/app/effects/catalog.json" >"$scratch/tag-catalog.json"
    tag_effects="$(csg_tool json-len "$scratch/tag-catalog.json" effects)"
    tag_notices="$(git -C "$repo" rev-parse --verify --quiet "$tag:THIRD_PARTY_NOTICES.md")" || csg_die "$tag holds no THIRD_PARTY_NOTICES.md."
    tag_license="$(git -C "$repo" rev-parse --verify --quiet "$tag:LICENSE")" || csg_die "$tag holds no LICENSE."
    git -C "$repo" show "$tag:CHANGELOG.md" >"$scratch/tag-changelog.md" || csg_die "$tag holds no CHANGELOG.md."
    tag_notes="$(csg_python "$CSG_LIB_DIR/csg_release.py" notes "$scratch/tag-changelog.md" "$release")"
    # The zlib the tag says a release is built with; a tag from before the zlib was named says none.
    tag_zlib="$(git -C "$repo" show "$tag:firmware/app/platformio.ini" | sed -n 's/^CSIGARLAND_RELEASE_ZLIB[[:space:]]*=[[:space:]]*\([^[:space:]]*\)[[:space:]]*$/\1/p')"
    tag_zlib="${tag_zlib%%$'\n'*}"
    tag_read=1
}
# Whether a file's bytes are those of the tag's blob, as git would hash it, with no filters.
is_tag_blob() { [ "$(git -C "$repo" hash-object --no-filters -- "$1" 2>/dev/null)" = "$2" ]; }
blob_of() { if [ "$1" = LICENSE ]; then printf '%s\n' "$tag_license"; else printf '%s\n' "$tag_notices"; fi; }

if [ "$build_new" -eq 1 ]; then
    [ "$site_from_release" -eq 0 ] || csg_die '--build-new makes new builds, and --site-from-release writes the site for files released already: choose one.'
    [ -z "$firmware_version$utility_version" ] || csg_die '--build-new releases what it builds: leave out --firmware-version and --utility-version.'
    if [ "$with_firmware" -eq 1 ]; then
        firmware_base="$(csg_firmware_identity "$repo/firmware/app/platformio.ini" 0 Release)"
        [ "$firmware_base" = "$release.0" ] || csg_die "The firmware's base, CSIGARLAND_FIRMWARE_BASE in firmware/app/platformio.ini, makes $(printf '%s' "$firmware_base" | sed -E 's/\.0(-|$)/.N\1/'), not a build of $release. Nothing was built."
    fi
    if [ "$with_utility" -eq 1 ]; then
        utility_base="$(sed -n 's/.*Version = "\([^"]*\)".*/\1/p' "$repo/cmd/csigarland/version.go")"
        [ "${utility_base%%$'\n'*}" = "$release" ] || csg_die "The utility's base, in cmd/csigarland/version.go, is ${utility_base%%$'\n'*}, not $release. Nothing was built."
        csg_require go 'Go is not on PATH, and the utility cannot be built. Nothing was built.'
        here="$(GOTOOLCHAIN=local go env GOOS)-$(GOTOOLCHAIN=local go env GOARCH)"
        [ "$here" = "$platform" ] || csg_die "This computer builds the utility for $here, not $platform. Build it on a computer of that platform, copy the file and its manifest into assets/utility, and publish it with --utility-version. Nothing was built."
        [ -n "$(csg_release_compiler "$platform")" ] || csg_die "No release compiler is named for $platform in deploy/lib/csg_tool.py and deploy/BuildVersion.psm1, so no build of it is a release's yet. Nothing was built."
    fi
    if [ "$site_only" -eq 0 ]; then
        read_tag
        if [ "$with_firmware" -eq 1 ] && [ "$(csg_source_identity "$repo" | csg_tool json-get - source_sha256)" != "$tag_firmware" ]; then
            csg_die "The firmware's sources here are not what $tag holds: check out the tag, or tag what is here. Nothing was built."
        fi
        if [ "$with_utility" -eq 1 ] && [ "$(csg_utility_source_digest "$repo")" != "$tag_utility" ]; then
            csg_die "The utility's sources here are not what $tag holds: check out the tag, or tag what is here. Nothing was built."
        fi
        for name in THIRD_PARTY_NOTICES.md LICENSE; do
            is_tag_blob "$repo/$name" "$(blob_of "$name")" || csg_die "$name here is not what $tag holds: commit it with the tag. Nothing was built."
        done
    fi
    if [ "$what_if" -eq 1 ]; then
        csg_say "What if: build $what anew for $release, spending a build number of each."
        exit 0
    fi
    if [ "$with_firmware" -eq 1 ]; then
        said="$(bash "$CSG_DEPLOY_DIR/stage-assets.sh" --new-build)" || csg_die 'Building and staging the firmware failed. Nothing was released.'
        printf '%s\n' "$said" >&2
        firmware_version="$(printf '%s\n' "$said" | sed -n 's|^Staged .*/csigarland-\([0-9]*\.[0-9]*\.[0-9]*\.[0-9]*\)\.ota$|\1|p')"
        [ "$(csg_count_lines "$firmware_version")" -eq 1 ] || csg_die 'stage-assets.sh did not say which update file it staged. Nothing was released.'
    fi
    if [ "$with_utility" -eq 1 ]; then
        if ! said="$(bash "$CSG_DEPLOY_DIR/build-utility.sh" --force)"; then
            staged=''
            if [ "$with_firmware" -eq 1 ]; then staged="; the firmware $firmware_version stays staged, for --firmware-version"; fi
            csg_die "Building the utility failed. Nothing was released$staged."
        fi
        utility_version="$(printf '%s' "${said##*$'\n'}" | sed -n -E "s#.*/csigarland-([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)-$platform(\.exe|\.zip)?\$#\1#p")"
        [ -n "$utility_version" ] || csg_die 'build-utility.sh did not say which file it built. Nothing was released.'
    fi
    csg_say "Built ${firmware_version:+firmware $firmware_version}${firmware_version:+${utility_version:+ and }}${utility_version:+utility $utility_version} for $release."
elif { [ "$with_firmware" -eq 1 ] && [ -z "$firmware_version" ]; } || { [ "$with_utility" -eq 1 ] && [ -z "$utility_version" ]; }; then
    csg_die 'Name the staged builds with --firmware-version and --utility-version, as the run publishes them, or build them anew with --build-new.'
fi

base_url="https://github.com/$repository/releases/download/$tag"
ota_name="csigarland-$firmware_version.ota"
utility_name="csigarland-$utility_version-$platform"
# On Windows a program, and on macOS its bundle zipped, csiGarland.app in it, which build-utility.sh makes there.
case "$platform" in windows-*) utility_name="$utility_name.exe" ;; darwin-*) utility_name="$utility_name.zip" ;; esac
# What goes out is a copy, made first and checked after: assets/ is shared with every staging.
snapshot="$scratch/files"
firmware_dir="$snapshot/firmware"
utility_dir="$snapshot/utility"
ota="$firmware_dir/$ota_name"
utility="$utility_dir/$utility_name"
files=()
if [ "$what_if" -eq 1 ]; then export CSIGARLAND_NO_INSTALL=1; fi
for version in "$firmware_version" "$utility_version"; do
    [ -z "$version" ] || case "$version" in "$release".*) ;; *) csg_die "$version is not a build of $release." ;; esac
done
if [ "$with_firmware" -eq 1 ]; then
    mkdir -p "$firmware_dir"
    for name in "$ota_name" "$ota_name.sha256" "$ota_name.manifest.json" csigarland-ota-root.crt THIRD_PARTY_NOTICES.md LICENSE; do
        [ -f "$repo/assets/firmware/$name" ] || csg_die "No $repo/assets/firmware/$name. Stage the release first: deploy/stage-assets.sh, or --build-new."
        cp -f "$repo/assets/firmware/$name" "$firmware_dir/$name"
        files[${#files[@]}]="$firmware_dir/$name"
    done
fi
if [ "$with_utility" -eq 1 ]; then
    mkdir -p "$utility_dir"
    for name in "$utility_name" "$utility_name.manifest.json"; do
        [ -f "$repo/assets/utility/$name" ] || csg_die "No $repo/assets/utility/$name. Build it first: deploy/build-utility.sh, or --build-new."
        cp -f "$repo/assets/utility/$name" "$utility_dir/$name"
        files[${#files[@]}]="$utility_dir/$name"
    done
fi

fw() { csg_tool json-get "$ota.manifest.json" "$1" "${2-}"; }
ut() { csg_tool json-get "$utility.manifest.json" "$1" "${2-}"; }
has() { csg_tool json-get "$1" "$2" >/dev/null 2>&1; }
if [ "$with_firmware" -eq 1 ]; then
    [ "$(fw version)" = "$firmware_version" ] || csg_die "The staged firmware is $(fw version), not $firmware_version."
    [ "$(csg_tool sha256 "$ota")" = "$(fw ota_sha256)" ] || csg_die "$ota_name is not the file its manifest describes."
    # Byte for byte, its line's end too, as $(cat) would drop it: compared by digest.
    said_sha="$(printf '%s  %s\n' "$(fw ota_sha256)" "$ota_name" | csg_tool sha256-text)"
    [ "$(csg_tool sha256 "$ota.sha256")" = "$said_sha" ] || csg_die "$ota_name.sha256 does not say $ota_name's SHA-256, $(fw ota_sha256): stage it again."
    for field in notices_sha256 license_sha256 notices_checked; do
        has "$ota.manifest.json" "$field" || csg_die "$firmware_version records no $field, as a staging before it did: stage it again."
    done
    [ "$(fw notices_checked)" = true ] || csg_die "$firmware_version was staged with THIRD_PARTY_NOTICES.md not checked against what it links, as with no Go: stage it again where Go is."
    if [ "$(csg_tool sha256 "$firmware_dir/THIRD_PARTY_NOTICES.md")" != "$(fw notices_sha256)" ] || [ "$(csg_tool sha256 "$firmware_dir/LICENSE")" != "$(fw license_sha256)" ]; then
        csg_die "THIRD_PARTY_NOTICES.md or LICENSE in assets/firmware is not what $firmware_version was staged with: stage it again."
    fi
    # The root published beside the update file is the one the image holds and the file is signed under.
    root_file="$firmware_dir/csigarland-ota-root.crt"
    root_sha="$(csg_tool pem-sha256 "$root_file")"
    [ "$root_sha" = "$(fw root_sha256)" ] || csg_die "$root_file is not the root the update file names: its SHA-256 is $root_sha, the manifest's $(fw root_sha256)."
    csg_own_signature "$ota" "$root_file" >"$scratch/signed.json" || csg_die "$ota is not signed under the root published with it."
    for field in image_sha256 pack_sha256 root_sha256; do
        [ "$(csg_tool json-get "$scratch/signed.json" "$field" '')" = "$(fw "$field" '')" ] ||
            csg_die "$ota's signed header names $field $(csg_tool json-get "$scratch/signed.json" "$field" ''), its manifest $(fw "$field" ''): stage it again."
    done
    has "$ota.manifest.json" effects || csg_die "$firmware_version records no count of effects, as a staging before it did: stage it again."
    effects="$(fw effects)"
fi
if [ "$with_utility" -eq 1 ]; then
    [ "$(ut version)" = "$utility_version" ] || csg_die "The staged utility is $(ut version), not $utility_version."
    [ "$(csg_tool sha256 "$utility")" = "$(ut sha256)" ] || csg_die "$utility_name is not the file its manifest describes."
    if has "$utility.manifest.json" platform && [ "$(ut platform)" != "$platform" ]; then csg_die "$utility_name is a build for $(ut platform), not $platform."; fi
    release_compiler="$(csg_release_compiler "$platform")"
    [ -n "$release_compiler" ] || csg_die "No release compiler is named for $platform in deploy/lib/csg_tool.py and deploy/BuildVersion.psm1, so no build of it is a release's yet: name the one its releases are built with, and how to build it again in docs/guides/rebuild.md."
    [ "$(ut compiler)" = "$release_compiler" ] || csg_die "The utility was built with $(ut compiler), not $release_compiler, so no one could build it again to the byte: build it with that one."
    for field in cc cxx cgo_flags cc_command cxx_command goflags gowork goexperiment; do
        has "$utility.manifest.json" "$field" || csg_die "The utility $utility_version records no $field, as a build before it did: build it again."
    done
    wanted="$(csg_compiler_build "$release_compiler")"
    if [ "$(csg_compiler_build "$(ut cc)")" != "$wanted" ] || [ "$(csg_compiler_build "$(ut cxx)")" != "$wanted" ]; then
        csg_die "cgo built the utility with $(ut cc) and $(ut cxx), not $release_compiler: build it with that one, CC and CXX unset, with deploy/build-utility.sh --force."
    fi
    for field in cc_command cxx_command; do
        [ "$(csg_count_lines "$(csg_tool split-command "$(ut "$field")")")" -eq 1 ] ||
            csg_die "cgo built the utility with '$(ut "$field")', arguments and all: build it again with CC and CXX unset, with deploy/build-utility.sh --force."
    done
    [ -z "$(ut goflags)" ] && [ "$(ut gowork)" = off ] || csg_die "The utility was built with GOFLAGS '$(ut goflags)' and GOWORK '$(ut gowork)': build it again with deploy/build-utility.sh --force."
    [ "$(ut gofips140 '')" = off ] || csg_die "The utility $utility_version was built with GOFIPS140 '$(ut gofips140 '')', not off: unset it, go env -u GOFIPS140, and build it again with deploy/build-utility.sh --force."
    [ -z "$(ut goexperiment)" ] || csg_die "The utility was built with GOEXPERIMENT '$(ut goexperiment)': unset it, go env -u GOEXPERIMENT, and build it again with deploy/build-utility.sh --force."
    case "$platform" in
        *-amd64) [ "$(ut goamd64 '')" = "$(csg_release_amd64)" ] || csg_die "The utility was built for x86-64 level '$(ut goamd64 '')', not $(csg_release_amd64): build it again with deploy/build-utility.sh." ;;
    esac
    while IFS= read -r flag; do
        [ "$(ut "cgo_flags.$flag" '')" = "$(csg_tool const cgo-flag "$flag")" ] ||
            csg_die "The utility was built with $flag '$(ut "cgo_flags.$flag" '')', not Go's own '$(csg_tool const cgo-flag "$flag")': build it again with it unset, with deploy/build-utility.sh --force."
    done < <(csg_tool const cgo-flags)
    if has "$utility.manifest.json" profile && [ "$(ut profile)" != Release ]; then csg_die "The utility $utility_version is a $(ut profile) build: a release takes a Release one."; fi
fi

# What the site gets: worked out by csg_release.py from the lists before this run and this run's builds.
today="$(date +%Y-%m-%d)"
# Each value taken alone first: a command substitution inside another's arguments fails unseen.
write_site() {
    local out="$1" days="${2:-}" state="$scratch/site-state.json" firmware_json=null utility_json=null sha bytes root
    if [ -z "$days" ]; then days='{}'; fi
    if [ "$with_firmware" -eq 1 ]; then
        sha="$(fw ota_sha256)"
        bytes="$(csg_tool file-size "$ota")"
        root="$(csg_tool sha256 "$firmware_dir/csigarland-ota-root.crt")"
        firmware_json="$(csg_tool json-build --str version "$firmware_version" --str file "$ota_name" --str sha256 "$sha" \
            --raw bytes "$bytes" --raw effects "$effects" --str root_sha256 "$root")"
    fi
    if [ "$with_utility" -eq 1 ]; then
        sha="$(ut sha256)"
        bytes="$(csg_tool file-size "$utility")"
        utility_json="$(csg_tool json-build --str version "$utility_version" --str file "$utility_name" --str sha256 "$sha" \
            --raw bytes "$bytes" --str platform_name "$platform_name")"
    fi
    local before=null catalog=null
    if [ -f "$repo/site/downloads/manifest.json" ]; then before="$(cat "$repo/site/downloads/manifest.json")"; fi
    if [ -f "$repo/site/downloads/firmware-catalog-v1.json" ]; then catalog="$(cat "$repo/site/downloads/firmware-catalog-v1.json")"; fi
    csg_tool json-build --str release "$release" --str today "$today" --str what "$what" --str ota_name "$ota_name" \
        --raw firmware "$firmware_json" --raw utility "$utility_json" --raw days "$days" \
        --raw manifest_before "$before" --raw catalog_before "$catalog" >"$state"
    csg_python "$CSG_LIB_DIR/csg_release.py" site "$state" "$out" "$run_id"
}

if [ "$site_only" -eq 1 ]; then
    preview="$repo/temp/site-preview-$release"
    if [ "$what_if" -eq 1 ]; then
        csg_say "What if: write a preview of the site's downloads for $what of $tag in $preview."
    else
        write_site "$preview"
    fi
    exit 0
fi

# The release says each file builds again from the tag, so each was built from just what the tag holds.
if [ "$tag_read" -eq 0 ]; then read_tag; fi
source_format="$(csg_source_format)"
if [ "$with_firmware" -eq 1 ]; then
    [ "$(fw source_format '')" = "$source_format" ] || csg_die "$firmware_version records its sources in another form than this script works out: build it again from the tag."
    [ "$tag_firmware" = "$(fw source_sha256)" ] || csg_die "The firmware was built from other sources than $tag holds ($(fw source_sha256) against $tag_firmware): build it again from the tag."
    is_tag_blob "$firmware_dir/THIRD_PARTY_NOTICES.md" "$tag_notices" && is_tag_blob "$firmware_dir/LICENSE" "$tag_license" ||
        csg_die "THIRD_PARTY_NOTICES.md or LICENSE staged with $firmware_version is not what $tag holds: commit them with the tag, or stage it again."
    [ "$tag_effects" = "$effects" ] || csg_die "$firmware_version holds $effects effects, and $tag's catalog $tag_effects: build it again from the tag."
    # The portal's pages are bytes of the image, gzipped by the zlib of the Python that built it: a release
    # holds the one its tag names, so that whoever builds it again with that zlib makes the same image.
    if [ -n "$tag_zlib" ]; then
        [ -n "$(fw zlib '')" ] || csg_die "$firmware_version records no zlib, as a staging before it did: stage it again."
        [ "$(fw zlib '')" = "$tag_zlib" ] ||
            csg_die "$firmware_version was built with zlib $(fw zlib ''), not $tag_zlib as $tag names in CSIGARLAND_RELEASE_ZLIB: its portal's pages are other bytes than a release's. Build it under a Python with that zlib."
    fi
fi
if [ "$with_utility" -eq 1 ]; then
    [ "$(ut source_format '')" = "$source_format" ] || csg_die "$utility_version records its sources in another form than this script works out: build it again from the tag."
    [ "$tag_utility" = "$(ut source_sha256)" ] || csg_die "The utility was built from other sources than $tag holds ($(ut source_sha256) against $tag_utility): build it again from the tag."
    for field in notices_sha256 license_sha256 notices_checked; do
        has "$utility.manifest.json" "$field" || csg_die "$utility_version records no $field, as a build before it did: build it again."
    done
    [ "$(ut notices_checked)" = true ] || csg_die "$utility_version was built with THIRD_PARTY_NOTICES.md not found to name what it holds: bring the notices up to date, commit them, and build it again."
    if [ "$with_firmware" -eq 1 ]; then
        if [ "$(csg_tool sha256 "$firmware_dir/THIRD_PARTY_NOTICES.md")" != "$(ut notices_sha256)" ] || [ "$(csg_tool sha256 "$firmware_dir/LICENSE")" != "$(ut license_sha256)" ]; then
            csg_die "THIRD_PARTY_NOTICES.md or LICENSE going out with $firmware_version is not what $utility_version was built with: build them from one tag."
        fi
    else
        for name in THIRD_PARTY_NOTICES.md LICENSE; do
            cp -f "$repo/$name" "$utility_dir/$name"
            is_tag_blob "$utility_dir/$name" "$(blob_of "$name")" || csg_die "$name here is not what $tag holds: commit it with the tag."
            if [ "$name" = LICENSE ]; then wanted="$(ut license_sha256)"; else wanted="$(ut notices_sha256)"; fi
            [ "$(csg_tool sha256 "$utility_dir/$name")" = "$wanted" ] || csg_die "$name here is not the one $utility_version was built with: build it again."
            files[${#files[@]}]="$utility_dir/$name"
        done
    fi
fi

# The release as GitHub holds it now, in a file, or a failure for none.
read_release() { gh api "repos/$repository/releases/tags/$tag" >"$1" 2>/dev/null; }
# A file's SHA-256 as GitHub holds it: its digest, or that of its bytes downloaded into a folder of their own.
remote_sha256() {
    local held="$1" name="$2" digest into
    digest="$(csg_python -c 'import json, sys
for asset in json.load(open(sys.argv[1]))["assets"]:
    if asset["name"] == sys.argv[2]:
        print(asset.get("digest") or "")' "$held" "$name")"
    case "$digest" in sha256:*) printf '%s\n' "${digest#sha256:}"; return 0 ;; esac
    into="$scratch/download-$(csg_run_id)"
    mkdir -p "$into"
    gh release download "$tag" --repo "$repository" --pattern "$name" --dir "$into" || csg_die "Could not read $name of the release $tag back from GitHub to compare it."
    csg_tool sha256 "$into/$name"
}
asset_names() { csg_python -c 'import json, sys; [print(a["name"]) for a in json.load(open(sys.argv[1]))["assets"]]' "$1"; }
# Whether the release holds this file, byte for byte; a refusal for one of other bytes.
release_holds() {
    local held="$1" file="$2" name there
    name="$(basename "$file")"
    [ -f "$held" ] && asset_names "$held" | grep -Fxq -- "$name" || return 1
    there="$(remote_sha256 "$held" "$name")"
    [ "$there" = "$(csg_tool sha256 "$file")" ] ||
        csg_die "The release $tag holds another $name already, SHA-256 $there against $(csg_tool sha256 "$file") here: a released file is never replaced, and the site is left as it is."
}
# The notes, with the table of the files the release holds and this run's: kept under temp/ to read after,
# where the copies and the downloads in scratch go when the script ends.
notes="$CSG_TEMP/release-notes-$release-$run_id.md"
write_notes() {
    local held="$1" rows='' name described sha signed_said='' placed_ota=0 placed_utility=0
    local names=()
    if [ -f "$held" ]; then while IFS= read -r name; do names[${#names[@]}]="$name"; done < <(asset_names "$held"); fi
    # A file of this run's that the release holds keeps its place in the table, with this run's hash, as
    # an ordered table keeps a key it is given again.
    for name in ${names[@]+"${names[@]}"}; do
        described="$(csg_python "$CSG_LIB_DIR/csg_release.py" describe "$name")"
        [ -n "$described" ] || continue
        if [ "$with_firmware" -eq 1 ] && [ "$name" = "$ota_name" ]; then
            sha="$(fw ota_sha256)"
            placed_ota=1
        elif [ "$with_utility" -eq 1 ] && [ "$name" = "$utility_name" ]; then
            sha="$(ut sha256)"
            placed_utility=1
        else
            sha="$(remote_sha256 "$held" "$name")"
        fi
        rows="$rows| \`$name\` | $described | \`$sha\` |"$'\n'
    done
    if [ "$with_firmware" -eq 1 ] && [ "$placed_ota" -eq 0 ]; then
        described="$(csg_python "$CSG_LIB_DIR/csg_release.py" describe "$ota_name")"
        sha="$(fw ota_sha256)"
        rows="$rows| \`$ota_name\` | $described | \`$sha\` |"$'\n'
    fi
    if [ "$with_utility" -eq 1 ] && [ "$placed_utility" -eq 0 ]; then
        described="$(csg_python "$CSG_LIB_DIR/csg_release.py" describe "$utility_name")"
        sha="$(ut sha256)"
        rows="$rows| \`$utility_name\` | $described | \`$sha\` |"$'\n'
    fi
    case "$rows" in *'.ota` |'*) signed_said='The update file is signed under the root in `csigarland-ota-root.crt`. ' ;; esac
    printf '%s\n\nThe full list: [CHANGELOG.md](https://github.com/%s/blob/%s/CHANGELOG.md).\n\n## Files\n\ncsiGarland %s, built from commit %s.\n\n| File | What | SHA-256 |\n|---|---|---|\n%s\n%sEach file builds again to the byte from this tag'"'"'s sources: see [Building a Release Again](https://github.com/%s/blob/%s/docs/guides/rebuild.md).' \
        "$tag_notes" "$repository" "$tag" "$release" "$tag_commit" "${rows%$'\n'}" "$signed_said" "$repository" "$tag" >"$notes"
    csg_say "The release's notes are in $notes."
}
# The day each of this run's files came to the release, as GitHub dates its upload, once it holds them all.
files_days() {
    local held="$scratch/held-days.json" file
    read_release "$held" || csg_die "GitHub has no release $tag to write the site for."
    for file in "${files[@]}"; do
        release_holds "$held" "$file" || csg_die "The release $tag on GitHub does not hold $(basename "$file"): the site is left as it is."
    done
    csg_python -c 'import json, sys, datetime
held = json.load(open(sys.argv[1]))
names = set(sys.argv[2:])
days = {}
for asset in held["assets"]:
    if asset["name"] in names:
        moment = datetime.datetime.strptime(asset["updated_at"], "%Y-%m-%dT%H:%M:%SZ")
        days[asset["name"]] = moment.strftime("%Y-%m-%d")
print(json.dumps(days))' "$held" $(for file in "${files[@]}"; do basename "$file"; done)
}

existing="$scratch/existing.json"
read_release "$existing" || rm -f "$existing"
upload=()
for file in "${files[@]}"; do
    release_holds "$existing" "$file" || upload[${#upload[@]}]="$file"
done
write_notes "$existing"
if [ "$site_from_release" -eq 1 ]; then
    if [ "$what_if" -eq 1 ]; then
        csg_say "What if: write the site's downloads for $what of the release $tag made already."
    else
        days="$(files_days)" || exit 1
        write_site "$repo/site/downloads" "$days"
    fi
    exit 0
fi
if [ -f "$existing" ]; then
    if [ "$what_if" -eq 1 ]; then
        csg_say "What if: add ${#upload[@]} files of $what to the release $tag, and write its notes again."
        exit 0
    fi
    if [ "${#upload[@]}" -gt 0 ]; then
        gh release upload "$tag" "${upload[@]}" --repo "$repository" || csg_die 'gh release upload failed. Run this again: what was sent whole is not sent twice.'
    else
        csg_say "The release $tag holds all these files already."
    fi
    # Written from what the release holds then, and read back: a run elsewhere may write its own between.
    whole=0
    for try in 1 2 3; do
        read_release "$scratch/now.json" || true
        write_notes "$scratch/now.json"
        gh release edit "$tag" --repo "$repository" --notes-file "$notes" ||
            csg_die 'gh release edit failed: the files are added, the notes are as they were. Run this again to write them.'
        read_release "$scratch/now.json" || continue
        missing="$(csg_python -c 'import json, sys
sys.path.insert(0, sys.argv[2])
from csg_release import describe
now = json.load(open(sys.argv[1]))
print(sum(1 for a in now["assets"] if describe(a["name"]) and "`%s`" % a["name"] not in (now.get("body") or "")))' "$scratch/now.json" "$CSG_LIB_DIR")"
        if [ "$missing" -eq 0 ]; then whole=1; break; fi
    done
    [ "$whole" -eq 1 ] || csg_warn "The notes of $tag do not list every file it holds: another run writes them too. Run this again once it is done."
    csg_say "Added to https://github.com/$repository/releases/tag/$tag"
else
    if [ "$what_if" -eq 1 ]; then
        csg_say "What if: create the release $tag with ${#files[@]} files of $what."
        exit 0
    fi
    gh release create "$tag" "${files[@]}" --repo "$repository" --title "csiGarland $release" --notes-file "$notes" --verify-tag ||
        csg_die 'gh release create failed.'
    csg_say "Released https://github.com/$repository/releases/tag/$tag"
fi
days="$(files_days)" || exit 1
write_site "$repo/site/downloads" "$days"
