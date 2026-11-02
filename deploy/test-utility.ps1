# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: The checks of the Go code, with the firmware's engine as it stands in the tree.
# Contributors: None
#
# Plain go test could use an engine compiled before the firmware last changed: see
# Set-csiGarlandEngineSources in BuildVersion.psm1. Needs Go and a C++20 compiler on the PATH.
#
# Example:
#   pwsh -File deploy\test-utility.ps1
#requires -Version 7.2

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Import-Module (Join-Path $PSScriptRoot 'BuildVersion.psm1') -Force
Set-csiGarlandEngineSources -RepoRoot $RepoRoot
$env:CGO_ENABLED = '1'

$unformatted = @(& gofmt -l (Join-Path $RepoRoot 'cmd') (Join-Path $RepoRoot 'internal') (Join-Path $RepoRoot 'firmware\app\effects'))
if ($unformatted.Count -gt 0) {
    throw "gofmt would change: $($unformatted -join ', ')"
}
# The module's own packages, named: ./... would take in any copy of the sources left under temp/.
$packages = @('./cmd/...', './internal/...', './firmware/app/effects/...')
$checks = @(
    , (@('build') + $packages)
    , (@('vet') + $packages)
    , (@('test', '-race') + $packages)
)
foreach ($check in $checks) {
    & go -C $RepoRoot @check
    if ($LASTEXITCODE -ne 0) {
        throw "go $($check -join ' ') failed with exit code $LASTEXITCODE."
    }
}
Write-Host 'The Go checks hold.'
