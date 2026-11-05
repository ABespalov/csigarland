#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: The checks of the Go code, with the firmware's engine as it stands in the tree.
# Contributors: None
#
# The bash twin of deploy/test-utility.ps1, for Linux and macOS. Plain go test could use an engine
# compiled before the firmware last changed: see csg_set_engine_sources in deploy/lib/build_version.sh.
# Needs Go and a C++20 compiler on the PATH, and on Linux the window's headers, as
# docs/guides/utility.md lists.
#
# Example:
#   bash deploy/test-utility.sh
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/lib/common.sh"
. "$CSG_LIB_DIR/build_version.sh"

[ "$#" -eq 0 ] || csg_die "test-utility.sh takes no arguments."
csg_require go 'Go is not on PATH.'
csg_set_engine_sources "$CSG_REPO_ROOT"
export CGO_ENABLED=1

unformatted="$(gofmt -l "$CSG_REPO_ROOT/cmd" "$CSG_REPO_ROOT/internal" "$CSG_REPO_ROOT/firmware/app/effects")"
[ -z "$unformatted" ] || csg_die "gofmt would change: $(printf '%s' "$unformatted" | tr '\n' ' ')"
# The module's own packages, named: ./... would take in any copy of the sources left under temp/.
packages=(./cmd/... ./internal/... ./firmware/app/effects/...)
for check in build vet 'test -race'; do
    # shellcheck disable=SC2086 # The check's words are meant to split.
    go -C "$CSG_REPO_ROOT" $check "${packages[@]}" || csg_die "go $check failed."
done
csg_say 'The Go checks hold.'
