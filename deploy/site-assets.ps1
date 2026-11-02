# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Copy the renders and screen pictures the public site shows from docs/images into site/assets.
# Contributors: None
#
# The site shows what the documents show: the renders of the 3D models, and the pictures of the
# screen, portal and utility that deploy/new-doc-images.ps1 makes from the real code. Run this after
# either changes, so the site is never drawn apart from them.
#
# Example:
#   pwsh -File deploy\site-assets.ps1
#requires -Version 7.2

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Images = Join-Path $RepoRoot 'docs\images'
$Target = Join-Path $RepoRoot 'site\assets'

# The site's name for each, and where it comes from under docs/images.
$Pictures = [ordered]@{
    'garland-hanging.png'   = 'diffusers\garland_octet_hanging.png'
    'garland-octet.png'     = 'diffusers\garland_octet.png'
    'figurine-tree.png'     = 'diffusers\diffuser_tree.png'
    'figurine-star.png'     = 'diffusers\diffuser_star.png'
    'figurine-icicle.png'   = 'diffusers\diffuser_icicle.png'
    'figurine-pinecone.png' = 'diffusers\diffuser_pinecone.png'
    'figurine-bell.png'     = 'diffusers\diffuser_bell.png'
    'figurine-snowman.png'  = 'diffusers\diffuser_snowman.png'
    'figurine-gingerbread.png' = 'diffusers\diffuser_gingerbread.png'
    'figurine-lantern.png'  = 'diffusers\diffuser_lantern.png'
    'enclosure.png'         = 'hardware\enclosure_assembled_iso.png'
    'enclosure-exploded.png' = 'hardware\enclosure_exploded_preview.png'
    'screen-playing.png'    = 'screens\playing.png'
    'screen-standby.png'    = 'screens\standby.png'
    'screen-access.png'     = 'screens\access.png'
    'screen-update.png'     = 'screens\update-receiving.png'
    'portal-effects.png'    = 'portal\effects.png'
    'portal-schedule.png'   = 'portal\schedule.png'
    'utility-effects.png'   = 'utility\effects.png'
    'utility-tune.png'      = 'utility\tune.png'
    'utility-firmware.png'  = 'utility\firmware.png'
    'schematic.svg'         = 'schematic.svg'
}

New-Item -ItemType Directory -Path $Target -Force | Out-Null
foreach ($name in $Pictures.Keys) {
    $from = Join-Path $Images $Pictures[$name]
    if (-not (Test-Path -LiteralPath $from)) {
        throw "No picture at $from."
    }
    Copy-Item -LiteralPath $from -Destination (Join-Path $Target $name) -Force
}
Write-Host "$($Pictures.Count) pictures copied to $Target."
