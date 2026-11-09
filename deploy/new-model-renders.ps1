# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Renders the figurines and the enclosure from their OpenSCAD models into docs/images.
# Contributors: None
#
# Each figurine on its mount, in the colour its model gives its filament, and the eight hanging from
# the cable in the order of the firmware's layout, read from firmware/app/src/effect_parser.cpp, into
# docs/images/diffusers; the control unit's enclosure, exploded and as its parts print, into
# docs/images/hardware. Run it after a change to a model, a colour or the layout, then deploy\site-assets.ps1 and
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
# ESP-IDF's Python, under PlatformIO's core as PLATFORMIO_CORE_DIR or the home folder gives it, else the Python
# on the PATH, as the bash twin takes: the renders need nothing installed.
Import-Module (Join-Path $PSScriptRoot 'BuildVersion.psm1') -Force
$python = Get-ChildItem (Join-Path (Get-csiGarlandPlatformIOCore) 'penv\.espidf-*\Scripts\python.exe') -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $python) {
    $onPath = Get-Command python -ErrorAction SilentlyContinue
    if (-not $onPath) {
        throw 'Neither the Python environment of ESP-IDF under .platformio nor a Python on the PATH was found: build the firmware once, or install Python.'
    }
    $python = $onPath.Source
}
& $python (Join-Path $PSScriptRoot 'model_renders.py') $OpenScad $RepoRoot
if ($LASTEXITCODE -ne 0) {
    throw "Rendering the models failed with exit code $LASTEXITCODE."
}
