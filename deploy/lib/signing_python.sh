# shellcheck shell=bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: The one Python the update signing tool runs under, with the cryptography version the build pins.
# Contributors: None
#
# Sourced after common.sh and build_version.sh: each function is its namesake in deploy/SigningPython.psm1.
# The build's own script, firmware/app/scripts/ota_signing.py, picks the same way and pins the version,
# so every script signs and checks with what the build signed with. On Linux and macOS a Python of a
# virtual environment is bin/python, where Windows has Scripts\python.exe.

CSG_SIGNING_SCRIPT="$CSG_REPO_ROOT/firmware/app/scripts/ota_signing.py"

# The version ota_signing.py pins, read from it, so there is one place to change it.
csg_pinned_cryptography() {
    local pinned
    pinned="$(sed -n 's/^_CRYPTOGRAPHY = "cryptography==\([0-9.]*\)".*/\1/p' "$CSG_SIGNING_SCRIPT")"
    [ -n "$pinned" ] || csg_die "No pinned cryptography version in $CSG_SIGNING_SCRIPT."
    printf '%s\n' "${pinned%%$'\n'*}"
}

csg_cryptography_version() {
    "$1" -c 'import cryptography, sys; sys.stdout.write(cryptography.__version__)' 2>/dev/null || true
}

# The Python a pio installed with pip runs from, as pio itself says it; else the one beside pio, as in a
# virtual environment's bin folder.
csg_pio_python() {
    local pio said python
    pio="$(command -v pio 2>/dev/null || true)"
    [ -n "$pio" ] || return 0
    if said="$(pio system info --json-output 2>/dev/null)"; then
        python="$(printf '%s' "$said" | csg_tool json-get - python_exe.value '' 2>/dev/null || true)"
        if [ -n "$python" ] && [ -x "$python" ]; then
            printf '%s\n' "$python"
            return 0
        fi
    fi
    for python in "$(dirname "$pio")/python" "$(dirname "$pio")/python3"; do
        if [ -x "$python" ]; then
            printf '%s\n' "$python"
            return 0
        fi
    done
}

# As the build chooses: the Python pio runs from, PlatformIO's own, then ESP-IDF's, newest first, the first
# that holds the pinned version. With none, the version goes into PlatformIO's own, as the build does.
csg_signing_python() {
    local pinned environments own pio_python candidates=() espidf=() python index beside=''
    pinned="$(csg_pinned_cryptography)"
    environments="$(csg_platformio_core)/penv"
    own="$environments/bin/python"
    pio_python="$(csg_pio_python)"
    if [ -n "$pio_python" ]; then candidates[${#candidates[@]}]="$pio_python"; fi
    candidates[${#candidates[@]}]="$own"
    for python in "$environments"/.espidf-*/bin/python; do
        if [ -x "$python" ]; then espidf[${#espidf[@]}]="$python"; fi
    done
    # Newest first: the glob sorts them oldest first.
    index=${#espidf[@]}
    while [ "$index" -gt 0 ]; do
        index=$((index - 1))
        candidates[${#candidates[@]}]="${espidf[$index]}"
    done
    for python in "${candidates[@]}"; do
        if [ -x "$python" ] && [ "$(csg_cryptography_version "$python")" = "$pinned" ]; then
            printf '%s\n' "$python"
            return 0
        fi
    done
    # A look ahead, as publish-release.sh --what-if, changes nothing: it says what is missing.
    if [ -n "${CSIGARLAND_NO_INSTALL:-}" ]; then
        csg_die "No Python of PlatformIO's holds cryptography $pinned, and nothing is installed now. Install it: '${pio_python:-$own}' -m pip install cryptography==$pinned"
    fi
    if [ ! -x "$own" ]; then
        if [ -n "$pio_python" ]; then beside=" Install it beside pio: '$pio_python' -m pip install cryptography==$pinned"; fi
        csg_die "No Python of PlatformIO's holds cryptography $pinned, and PlatformIO's own is not at $own.$beside As docs/guides/build-and-flash.md says, pip installs both together."
    fi
    csg_say "No Python of PlatformIO's holds cryptography $pinned: installing it into $own, as the build does."
    "$own" -m pip install --quiet "cryptography==$pinned" >&2 ||
        csg_die "Could not install cryptography $pinned. Try: '$own' -m pip install cryptography==$pinned"
    [ "$(csg_cryptography_version "$own")" = "$pinned" ] ||
        csg_die "Could not install cryptography $pinned. Try: '$own' -m pip install cryptography==$pinned"
    printf '%s\n' "$own"
}

# A build signed by a throwaway chain trusts a root nobody keeps: scripts that write to a board or send to
# a garland refuse one.
csg_assert_own_signing() {
    if [ -n "${CSIGARLAND_RELEASE_ROOT:-}" ]; then
        csg_die 'CSIGARLAND_RELEASE_ROOT is set in this shell, which makes unsigned builds to compare with a release. Remove it: unset CSIGARLAND_RELEASE_ROOT. Nothing was written or sent.'
    fi
    if [ -n "${CSIGARLAND_THROWAWAY_SIGNING:-}" ]; then
        csg_die 'CSIGARLAND_THROWAWAY_SIGNING is set in this shell, which makes test builds that no board should be given. Remove it: unset CSIGARLAND_THROWAWAY_SIGNING. Nothing was written or sent.'
    fi
    [ -f "$CSG_REPO_ROOT/cert/ota-root-ca.local.crt" ] ||
        csg_die 'No update root certificate in cert/. Make one: bash deploy/new-signing-certificate.sh. Nothing was written or sent.'
}

# What the signing tool says of an update file signed under this repository's root, or under the root
# given, as a garland would judge it: what it holds, as JSON. Fails for one that is not.
csg_own_signature() {
    local update="$1" root="${2:-$CSG_REPO_ROOT/cert/ota-root-ca.local.crt}" python
    python="$(csg_signing_python)" || return 1
    "$python" "$CSG_REPO_ROOT/firmware/app/scripts/ota_sign_tool.py" verify "$root" "$update" 2>/dev/null
}

csg_test_own_signature() { csg_own_signature "$@" >/dev/null; }
