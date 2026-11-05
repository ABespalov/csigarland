#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Refuses keys and certificates in the working tree's tracked files and in history.
# Contributors: None
#
# The bash twin of deploy/tests/sensitive-data.tests.ps1, for Linux and macOS. A key that reached history
# is out, even if a later commit removes it: replace it. Works before a first commit too, on what git
# would track.
#
# Example:
#   bash deploy/tests/sensitive-data.tests.sh
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)/lib/common.sh"

[ "$#" -eq 0 ] || csg_die "sensitive-data.tests.sh takes no arguments."
# By name: every kind of file a key or certificate lives in, and this project's local files.
name_pattern='(\.(pem|key|crt|cer|der|p12|pfx)$)|(\.local\.)'
# By content: a private key under any name. Split so this file does not match itself.
key_marker='-----BEGIN ''([A-Z0-9 ]+ )?PRIVATE KEY-----'
repo="$CSG_REPO_ROOT"
problems=''
problem() { problems="$problems$1"$'\n'; }

present="$(git -C "$repo" ls-files --cached --others --exclude-standard)" || csg_die 'git ls-files failed.'
named="$present"
has_commits=0
if git -C "$repo" rev-parse --verify --quiet HEAD >/dev/null 2>&1; then
    has_commits=1
    named="$named"$'\n'"$(git -C "$repo" log --all --format= --name-only)"
fi
while IFS= read -r file; do
    [ -n "$file" ] || continue
    if printf '%s\n' "$file" | grep -Eiq -- "$name_pattern"; then
        problem "$file: its name says key or certificate"
    fi
    # Whatever it is called, as a passphrase's file says nothing by its name.
    case "$file" in
        cert/README.md) ;;
        cert/*) problem "$file: only the README of cert/ may be committed" ;;
    esac
done < <(printf '%s\n' "$named" | awk '!seen[$0]++')

count=0
while IFS= read -r file; do
    [ -n "$file" ] || continue
    count=$((count + 1))
    if [ -f "$repo/$file" ] && grep -Eiq -- "$key_marker" "$repo/$file"; then
        problem "$file: holds a private key"
    fi
done <<<"$present"
if [ "$has_commits" -eq 1 ]; then
    while IFS= read -r commit; do
        [ -n "$commit" ] && problem "commit $commit: adds or removes a private key"
    done < <(git -C "$repo" log --all --format=%H -G "$key_marker" --extended-regexp)
fi

if [ -n "$problems" ]; then
    printf '%s' "$problems"
    csg_die "$(csg_count_lines "${problems%$'\n'}") sensitive data problems."
fi
csg_say "No keys or certificates in $count files."
