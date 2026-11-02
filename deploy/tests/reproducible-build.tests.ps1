# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Build the firmware and the utility twice from copies of the sources, and show they are the same to the byte.
# Contributors: None
#
# Each copy holds only what git would keep, under temp/, at paths of different lengths, so that
# nothing of this tree and no path of this machine can carry over. The firmware of both is signed
# by one throwaway chain made for the run, as the owner's keys are not copied anywhere, and both
# take the same build number. What they make is compared file by file: the images, their ELF, the
# effects pack, the three update files, and the utility.
#
# Parameters
#
#   -BuildNumber <n>
#       The number both builds take, 0 unless given.
#
#   -Keep
#       Leave the copies under temp/reproducible for a look at what differs.
#
# It takes two full builds. The components dependencies.lock names come from this tree's
# managed_components where it has them, as from a cache: they are what the lock says either way.
#
# Example:
#   pwsh -File deploy\tests\reproducible-build.tests.ps1
#requires -Version 7.2

[CmdletBinding()]
param(
    [int]$BuildNumber = 0,
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
# Short, as a long path changes the debug info: see the length the build warns of.
$Work = Join-Path $RepoRoot 'temp\r'
$Made = @(
    '.pio\build\esp32c6\firmware.elf',
    '.pio\build\esp32c6\firmware.bin',
    '.pio\build\esp32c6\bootloader.bin',
    '.pio\build\esp32c6\partitions.bin',
    'generated\effects.pack',
    '.pio\build\esp32c6\firmware.ota',
    '.pio\build\esp32c6\firmware-only.ota',
    '.pio\build\esp32c6\effects.ota'
)

Import-Module (Join-Path $PSScriptRoot '..\SigningPython.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '..\BuildVersion.psm1') -Force

if (Test-Path -LiteralPath $Work) {
    Remove-Item -LiteralPath $Work -Recurse -Force
}
$copies = @((Join-Path $Work 'a'), (Join-Path $Work 'bcd'))


# One chain for both, made once: a chain made per build would sign each with its own key.
$chain = Join-Path $Work 'chain'
$python = Get-csiGarlandSigningPython
& $python (Join-Path $RepoRoot 'firmware\app\scripts\ota_sign_tool.py') throwaway $chain
if ($LASTEXITCODE -ne 0) {
    throw 'The throwaway chain was not made.'
}

$results = @()
$saved = @{ Throwaway = $env:CSIGARLAND_THROWAWAY_SIGNING; Number = $env:CSIGARLAND_BUILD_NUMBER; Profile = $env:CSIGARLAND_PROFILE }
try {
    $env:CSIGARLAND_THROWAWAY_SIGNING = '1'
    $env:CSIGARLAND_BUILD_NUMBER = "$BuildNumber"
    # A release is what has to build again to the byte.
    $env:CSIGARLAND_PROFILE = 'release'
    # Both copies first, so that both hold the sources as they were at one moment.
    $apps = @($copies | ForEach-Object { Copy-csiGarlandSources -RepoRoot $RepoRoot -Destination $_ })
    for ($n = 0; $n -lt $copies.Count; $n++) {
        $copy = $copies[$n]
        $app = $apps[$n]
        $signing = Join-Path $app 'generated\throwaway-signing'
        New-Item -ItemType Directory -Path $signing -Force | Out-Null
        Copy-Item -Path (Join-Path $chain '*') -Destination $signing
        Write-Host "Building in $copy"
        & pio run -d $app -e esp32c6 2>&1 | ForEach-Object { $line = $_.ToString(); if ($line -match 'error|warning|SUCCESS|FAILED|CSIGARLAND_VERSION') { Write-Host $line } }
        if ($LASTEXITCODE -ne 0) {
            throw "The build in $copy failed with exit code $LASTEXITCODE."
        }
        Write-Host "Building the utility in $copy"
        $utility = & pwsh -File (Join-Path $copy 'deploy\build-utility.ps1') -BuildNumber $BuildNumber | Select-Object -Last 1
        if ($LASTEXITCODE -ne 0 -or -not $utility) {
            throw "The utility's build in $copy failed with exit code $LASTEXITCODE."
        }
        $files = @($Made | ForEach-Object { Join-Path $app $_ }) + @("$utility")
        $results += , @($files | ForEach-Object {
                if (Test-Path -LiteralPath $_) { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash } else { 'missing' }
            })
    }
} finally {
    $env:CSIGARLAND_THROWAWAY_SIGNING = $saved.Throwaway
    $env:CSIGARLAND_BUILD_NUMBER = $saved.Number
    $env:CSIGARLAND_PROFILE = $saved.Profile
}

$Made += 'the utility'
$differ = 0
for ($i = 0; $i -lt $Made.Count; $i++) {
    $same = $results[0][$i] -eq $results[1][$i] -and $results[0][$i] -ne 'missing'
    if (-not $same) {
        $differ++
    }
    Write-Host ('{0,-6} {1,-40} {2}' -f $(if ($same) { 'same' } else { 'DIFFER' }), $Made[$i], $results[0][$i].ToLowerInvariant())
}
if ($differ -gt 0) {
    # Where the first image's bytes part, for a start at finding why.
    $first = [System.IO.File]::ReadAllBytes((Join-Path $copies[0] "firmware\app\$($Made[1])"))
    $second = [System.IO.File]::ReadAllBytes((Join-Path $copies[1] "firmware\app\$($Made[1])"))
    for ($at = 0; $at -lt [Math]::Min($first.Length, $second.Length); $at++) {
        if ($first[$at] -ne $second[$at]) {
            Write-Host ('firmware.bin first differs at 0x{0:X} of {1} and {2} bytes.' -f $at, $first.Length, $second.Length)
            break
        }
    }
    Write-Host "The copies are kept in $Work."
    throw "$differ of $($Made.Count) files differ between two builds of the same sources."
}
if (-not $Keep) {
    Remove-Item -LiteralPath $Work -Recurse -Force
}
Write-Host "Two builds of the same sources are the same to the byte, in $($Made.Count) files."
