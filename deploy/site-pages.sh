#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Make the site's Russian pages under site/ru from its English pages and deploy/site-ru.json.
# Contributors: None
#
# The bash twin of deploy/site-pages.ps1, for Linux and macOS, writing the same pages by
# deploy/lib/csg_site.py. First every file of the site a page names, its stylesheet, scripts and
# pictures, gets the start of its SHA-256 in its address, written into the English pages. Then every
# element with data-i18n, and every attribute named by data-i18n-alt, data-i18n-content or
# data-i18n-aria, takes its Russian text from deploy/site-ru.json. Run this after either changes;
# --check, which the source rules run, only says whether site/ru is what this would write.
#
# Examples:
#   bash deploy/site-pages.sh
#   bash deploy/site-pages.sh --check
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/lib/common.sh"

check=()
while [ "$#" -gt 0 ]; do
    case "$1" in
        --check) check=(--check) ;;
        *) csg_die "site-pages.sh does not take $1." ;;
    esac
    shift
done
csg_python "$CSG_LIB_DIR/csg_site.py" "$CSG_REPO_ROOT" ${check[@]+"${check[@]}"}
