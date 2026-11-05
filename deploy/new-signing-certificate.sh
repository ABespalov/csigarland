#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Make the root certificate and the signing certificate for firmware updates.
# Contributors: None
#
# The bash twin of deploy/new-signing-certificate.ps1, for Linux and macOS. A first run makes both. It
# asks for a passphrase that protects the root key, and nothing recovers either: without the root key and
# passphrase, boards can only be updated over USB. Everything goes to cert/ at the repository root, which
# git ignores but for its README. Keep the root key and the passphrase apart, off this machine. The
# signing certificate, with its name and email, is inside every update file, so it is public as soon as
# a file is published.
#
# Options
#
#   --country <XX>                Two-letter country for the certificates, none unless given.
#   --organization <name>, --person <name>, --email <address>
#                                 Who the certificates name.
#   --renew-signing               Keep the root and issue a new signing key and certificate. Firmware
#                                 already on a board accepts it without an update, as it trusts the root.
#   --force                       Replace existing certificates with a new root and signing certificate.
#                                 A board whose firmware trusts the old root takes the next firmware over
#                                 USB only.
#
# Examples:
#   bash deploy/new-signing-certificate.sh
#   bash deploy/new-signing-certificate.sh --renew-signing
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/lib/common.sh"
. "$CSG_LIB_DIR/build_version.sh"
. "$CSG_LIB_DIR/signing_python.sh"

country=''
organization=''
person=''
email=''
renew=0
force=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --country)
            csg_need_value "$1" "$#"
            case "$2" in [A-Z][A-Z] | '') country="$2" ;; *) csg_die "--country is two capital letters, not $2." ;; esac
            shift
            ;;
        --organization) csg_need_value "$1" "$#"; organization="$2"; shift ;;
        --person) csg_need_value "$1" "$#"; person="$2"; shift ;;
        --email) csg_need_value "$1" "$#"; email="$2"; shift ;;
        --renew-signing) renew=1 ;;
        --force) force=1 ;;
        *) csg_die "new-signing-certificate.sh does not take $1." ;;
    esac
    shift
done

tool="$CSG_REPO_ROOT/firmware/app/scripts/ota_sign_tool.py"
cert_folder="$CSG_REPO_ROOT/cert"
root_key="$cert_folder/ota-root-ca.local.key"
signing_key="$cert_folder/ota-signing.local.key"

if [ "$renew" -eq 1 ] && [ "$force" -eq 1 ]; then
    csg_die '--renew-signing keeps the root and --force replaces it. Choose one.'
fi
if [ "$renew" -eq 1 ]; then
    [ -f "$root_key" ] || csg_die "No root key at $root_key. Bring it back from where you keep it."
elif [ "$force" -eq 0 ] && { [ -f "$root_key" ] || [ -f "$signing_key" ]; }; then
    csg_die 'Certificates exist already, and a new root would lock out every board that trusts this one. --renew-signing replaces the signing key only, --force replaces everything.'
fi
[ -t 0 ] || csg_die 'The passphrase is asked at a terminal, and this one has none.'

python="$(csg_signing_python)"
csg_say 'Nothing recovers the root key or its passphrase. Keep both, apart from each other.'
# Read with no echo, and never put on a command line, where others could read it.
read -r -s -p 'Passphrase for the root key: ' passphrase
printf '\n' >&2
[ "${#passphrase}" -ge 12 ] || csg_die 'The passphrase needs at least 12 characters.'
if [ "$renew" -eq 0 ]; then
    read -r -s -p 'Once more: ' again
    printf '\n' >&2
    [ "$passphrase" = "$again" ] || csg_die 'The passphrases differ.'
    unset again
fi

mkdir -p "$cert_folder"
# For the tool alone: given to its process and to nothing after it. A name not given is left unset, as
# the tool falls back to its own only for one that is not there.
names=()
if [ -n "$organization" ]; then names[${#names[@]}]="CSIGARLAND_CERT_ORGANIZATION=$organization"; fi
if [ -n "$person" ]; then names[${#names[@]}]="CSIGARLAND_CERT_PERSON=$person"; fi
if [ -n "$email" ]; then names[${#names[@]}]="CSIGARLAND_CERT_EMAIL=$email"; fi
# Exported in a subshell, never put on a command line, where ps would show the passphrase to others.
run_tool() {
    (
        export CSIGARLAND_ROOT_PASSPHRASE="$passphrase"
        for name in ${names[@]+"${names[@]}"}; do export "$name"; done
        exec "$python" "$tool" "$@"
    ) || csg_die 'ota_sign_tool.py failed.'
}
if [ "$renew" -eq 0 ]; then
    run_tool new-root "$cert_folder" "$country"
fi
run_tool new-signing "$cert_folder" "$country"
unset passphrase
# Every key here for this user alone: the tool writes its own so, and a root key kept from before by
# --renew-signing is narrowed as well.
for key in "$cert_folder"/*.key; do
    if [ -f "$key" ]; then chmod 600 "$key"; fi
done

csg_say "Done. Keep $root_key and its passphrase apart, off this machine."
if [ "$renew" -eq 0 ]; then
    csg_say 'Firmware built now trusts this root; a board on older firmware takes it over USB once.'
fi
