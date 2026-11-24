# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Copy the renders and screen pictures the public site shows from docs/images into site/assets.
# Contributors: None
#
# The site shows what the documents show: the renders of the 3D models, and the pictures of the
# screen, portal and utility that deploy/new-doc-images.ps1 makes from the real code. Run this after
# either changes, so the site is never drawn apart from them. The photographs of the built garland
# come from docs/images/photos. The effect animations of the Gallery, site/assets/effect-*.gif, are the
# site's own and this leaves them as they are: see site/README.md.
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
    'garland-octet.png'     = 'diffusers\garland_octet.png'
    'figurine-tree.png'     = 'diffusers\diffuser_tree.png'
    'figurine-star.png'     = 'diffusers\diffuser_star.png'
    'figurine-icicle.png'   = 'diffusers\diffuser_icicle.png'
    'figurine-pinecone.png' = 'diffusers\diffuser_pinecone.png'
    'figurine-bell.png'     = 'diffusers\diffuser_bell.png'
    'figurine-snowman.png'  = 'diffusers\diffuser_snowman.png'
    'figurine-gingerbread.png' = 'diffusers\diffuser_gingerbread.png'
    'figurine-lantern.png'  = 'diffusers\diffuser_lantern.png'
    'enclosure.png'         = 'hardware\enclosure_exploded_preview.png'
    'enclosure-exploded.png' = 'hardware\enclosure_exploded_preview.png'
    'screen-playing.png'    = 'screens\playing.png'
    'screen-standby.png'    = 'screens\standby.png'
    'screen-access.png'     = 'screens\access.png'
    'screen-update.png'     = 'screens\update-receiving.png'
    'portal-effects.png'    = 'portal\effects-short.png'
    'portal-schedule.png'   = 'portal\schedule.png'
    'utility-effects.png'   = 'utility\effects-short.png'
    'utility-tune.png'      = 'utility\tune.png'
    'utility-firmware.png'  = 'utility\firmware.png'
    'schematic.svg'         = 'schematic.svg'
    'photo-garland-lit.jpg' = 'photos\garland-lit.jpg'
    'photo-harness-tinned.jpg' = 'photos\harness-tinned.jpg'
    'photo-garland-current.jpg' = 'photos\garland-current.jpg'
    'photo-controller-closed.jpg' = 'photos\controller-closed.jpg'
    'photo-controller-inside.jpg' = 'photos\controller-inside.jpg'
    'photo-figurine-star-lit.jpg' = 'photos\figurine-star-lit.jpg'
    'photo-figurine-gingerbread-lit.jpg' = 'photos\figurine-gingerbread-lit.jpg'
    'photo-figurine-pinecone-lit.jpg' = 'photos\figurine-pinecone-lit.jpg'
    'photo-figurine-icicle-lit.jpg' = 'photos\figurine-icicle-lit.jpg'
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
