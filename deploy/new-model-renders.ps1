# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Renders the figurines from their OpenSCAD models into docs/images/diffusers.
# Contributors: None
#
# Each figurine on its mount, in the colour its model gives its filament, and the eight hanging from
# the cable in the order of the firmware's layout, read from firmware/app/src/effect_parser.cpp.
# Run it after a change to a model, a colour or the layout, then deploy\site-assets.ps1 and
# deploy\site-pages.ps1 for the site. It needs OpenSCAD.
#
# Parameters
#
#   -OpenScad <path>
#       openscad.exe, when it is not in Program Files.
#
# Example:
#   pwsh -File deploy\new-model-renders.ps1
#requires -Version 7.2

param(
    [string]$OpenScad = (Join-Path $env:ProgramFiles 'OpenSCAD\openscad.exe')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not (Test-Path -LiteralPath $OpenScad -PathType Leaf)) {
    throw "OpenSCAD was not found at $OpenScad. Install it, or name it with -OpenScad."
}
$python = Get-ChildItem (Join-Path $env:USERPROFILE '.platformio\penv\.espidf-*\Scripts\python.exe') -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $python) {
    throw 'The Python environment of ESP-IDF was not found under .platformio: build the firmware once.'
}
& $python (Join-Path $PSScriptRoot 'model_renders.py') $OpenScad $RepoRoot
if ($LASTEXITCODE -ne 0) {
    throw "Rendering the figurines failed with exit code $LASTEXITCODE."
}
