# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Build a published release again from its sources, and show it is the same to the byte.
# Contributors: None
#
# Run in a checkout of the release's sources. docs/guides/rebuild.md says how to get them and the
# tools. The firmware is built with the release's own root certificate, which every image holds
# and a release publishes beside its update file, so no key is needed: the image and the effects
# pack come out unsigned, and are compared with the digests the release's signed header holds,
# which its manifest repeats. The utility is built with the build number its manifest names.
#
# Parameters
#
#   -FirmwareManifest <path>
#       A release's csigarland-<version>.ota.manifest.json.
#
#   -Root <path>
#       The release's root certificate, csigarland-ota-root.crt. By default the one beside the manifest.
#
#   -UtilityManifest <path>
#       A release's csigarland-<version>-windows-amd64.exe.manifest.json.
#
# Builds in a copy of the checkout's sources under temp/rebuild-release, so this tree, its build and
# its counters are left as they are. The copy is kept for a look at what was built.
#
# Examples:
#   pwsh -File deploy\rebuild-release.ps1 -FirmwareManifest C:\Downloads\csigarland-1.0.0.102.ota.manifest.json
#   pwsh -File deploy\rebuild-release.ps1 -UtilityManifest C:\Downloads\csigarland-1.0.0.27-windows-amd64.exe.manifest.json
#requires -Version 7.2

[CmdletBinding()]
param(
    [string]$FirmwareManifest = '',
    [string]$Root = '',
    [string]$UtilityManifest = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Import-Module (Join-Path $PSScriptRoot 'BuildVersion.psm1') -Force

if (-not $FirmwareManifest -and -not $UtilityManifest) {
    throw 'Name a release: -FirmwareManifest, -UtilityManifest, or both.'
}
$Copy = Join-Path $RepoRoot 'temp\rebuild-release'
if (Test-Path -LiteralPath $Copy) {
    Remove-Item -LiteralPath $Copy -Recurse -Force
}
$FirmwareDir = Copy-csiGarlandSources -RepoRoot $RepoRoot -Destination $Copy
Write-Host "Building in a copy of the sources, $Copy."

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# A version's build number, its profile, and what it is without them: 1.0.0.27 is 27 of 1.0.0, a
# Release, and 1.0.0.27-dev the same number of a Dev build.
function Split-Version {
    param([Parameter(Mandatory = $true)][string]$Version)
    $profileName = if ($Version.EndsWith('-dev')) { 'Dev' } else { 'Release' }
    $plain = if ($profileName -eq 'Dev') { $Version.Substring(0, $Version.Length - 4) } else { $Version }
    $parts = [regex]::Match($plain, '^(\d+\.\d+\.\d+)\.(\d+)(-[0-9A-Za-z.]+)?$')
    if (-not $parts.Success) {
        throw "$Version is not a csiGarland version."
    }
    return [pscustomobject]@{
        Base    = "$($parts.Groups[1].Value)$($parts.Groups[3].Value)"
        Number  = [int]$parts.Groups[2].Value
        Profile = $profileName
    }
}

function Show-Verdict {
    param([string]$What, [string]$Made, [string]$Released)
    $same = $Made -eq $Released
    Write-Host ('{0,-6} {1,-22} made {2}' -f $(if ($same) { 'same' } else { 'DIFFER' }), $What, $Made)
    if (-not $same) {
        Write-Host ('       {0,-22} released {1}' -f '', $Released)
    }
    return $same
}

$all = $true

if ($FirmwareManifest) {
    $release = Get-Content -Raw -LiteralPath $FirmwareManifest | ConvertFrom-Json
    if (-not $Root) {
        $Root = Join-Path (Split-Path -Parent $FirmwareManifest) 'csigarland-ota-root.crt'
    }
    if (-not (Test-Path -LiteralPath $Root)) {
        throw "No root certificate at $Root. A release publishes csigarland-ota-root.crt beside its update file."
    }
    # The root the release's image holds, by the hash of its certificate, as the manifest says it.
    $pem = Get-Content -Raw -LiteralPath $Root
    $body = [regex]::Match($pem, '-----BEGIN CERTIFICATE-----(.+?)-----END CERTIFICATE-----', 'Singleline')
    if (-not $body.Success) {
        throw "$Root holds no certificate."
    }
    $der = [System.Convert]::FromBase64String(($body.Groups[1].Value -replace '\s', ''))
    $rootSha = [System.Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($der)).ToLowerInvariant()
    if ($rootSha -ne $release.root_sha256) {
        throw "$Root is not the root of this release: its SHA-256 is $rootSha, the manifest's $($release.root_sha256)."
    }
    $version = Split-Version -Version $release.version
    $iniBase = [regex]::Match((Get-Content -Raw -LiteralPath (Join-Path $RepoRoot 'firmware\app\platformio.ini')), '(?m)^CSIGARLAND_FIRMWARE_BASE\s*=\s*(\S+)\s*$').Groups[1].Value
    if ($iniBase -ne $version.Base) {
        throw "This checkout is firmware $iniBase, and the release $($version.Base): check out the release's sources first."
    }
    $source = Get-csiGarlandSourceIdentity -RepoRoot $RepoRoot
    # A manifest of 1.0.0 records its digest in an older form, which says nothing here: the files decide.
    $comparable = $release.PSObject.Properties['source_format'] -and $release.source_format -eq $source.source_format
    if ($comparable -and $source.source_sha256 -ne $release.source_sha256) {
        Write-Warning "The firmware sources here are not the ones the release records ($($source.source_sha256) against $($release.source_sha256)): expect them to differ."
    }

    $saved = @{ Root = $env:CSIGARLAND_RELEASE_ROOT; Number = $env:CSIGARLAND_BUILD_NUMBER; Throwaway = $env:CSIGARLAND_THROWAWAY_SIGNING }
    $environment = Get-csiGarlandFirmwareEnvironment -BuildProfile $version.Profile
    try {
        $env:CSIGARLAND_RELEASE_ROOT = [System.IO.Path]::GetFullPath($Root)
        $env:CSIGARLAND_BUILD_NUMBER = "$($version.Number)"
        Remove-Item Env:CSIGARLAND_THROWAWAY_SIGNING -ErrorAction SilentlyContinue
        Write-Host "Building firmware $($release.version) again."
        & pio run -d $FirmwareDir -e $environment 2>&1 | ForEach-Object { $line = $_.ToString(); if ($line -match 'error|SUCCESS|FAILED|CSIGARLAND_VERSION') { Write-Host $line } }
        if ($LASTEXITCODE -ne 0) {
            throw "The firmware's build failed with exit code $LASTEXITCODE."
        }
    } finally {
        $env:CSIGARLAND_RELEASE_ROOT = $saved.Root
        $env:CSIGARLAND_BUILD_NUMBER = $saved.Number
        $env:CSIGARLAND_THROWAWAY_SIGNING = $saved.Throwaway
    }
    # The image holds the portal's pages gzipped by the zlib of the Python that built it: another zlib than the
    # release's, as the manifest or the tag's platformio.ini names it, packs them into other bytes.
    $madeZlib = (Get-Content -Raw -LiteralPath (Join-Path $FirmwareDir 'generated\portal_zlib.txt')).Trim()
    $releaseZlib = if ($release.PSObject.Properties['zlib']) { $release.zlib } else { [regex]::Match((Get-Content -Raw -LiteralPath (Join-Path $FirmwareDir 'platformio.ini')), '(?m)^CSIGARLAND_RELEASE_ZLIB\s*=\s*(\S+)\s*$').Groups[1].Value }
    if ($releaseZlib -and $madeZlib -cne $releaseZlib) {
        Write-Warning "The portal's pages were gzipped with zlib $madeZlib here, the release's with ${releaseZlib}: expect the image to differ."
    }
    $all = (Show-Verdict -What 'firmware image' -Made (Get-Sha256 (Join-Path $FirmwareDir ".pio\build\$environment\firmware.bin")) -Released $release.image_sha256) -and $all
    $all = (Show-Verdict -What 'effects pack' -Made (Get-Sha256 (Join-Path $FirmwareDir 'generated\effects.pack')) -Released $release.pack_sha256) -and $all
}

if ($UtilityManifest) {
    $release = Get-Content -Raw -LiteralPath $UtilityManifest | ConvertFrom-Json
    $version = Split-Version -Version $release.version
    $go = "$(& go version)".Trim()
    $compiler = "$((& gcc --version 2>$null) | Select-Object -First 1)".Trim()
    if ($compiler -ne $release.compiler) {
        Write-Warning "The C++ compiler here is $compiler, the release's $($release.compiler): expect the files to differ."
    }
    Write-Host "Building the utility $($release.version) again."
    $made = & pwsh -File (Join-Path $Copy 'deploy\build-utility.ps1') -BuildNumber $version.Number -BuildProfile $version.Profile | Select-Object -Last 1
    if ($LASTEXITCODE -ne 0 -or -not $made) {
        throw "The utility's build failed with exit code $LASTEXITCODE."
    }
    $built = Get-Content -Raw -LiteralPath "$made.manifest.json" | ConvertFrom-Json
    if ($built.go -ne $release.go) {
        Write-Warning "Go was $($built.go), the release's $($release.go)."
    }
    $all = (Show-Verdict -What 'utility' -Made (Get-Sha256 $made) -Released $release.sha256) -and $all
}

if (-not $all) {
    throw 'What was built again is not the release, as above.'
}
Write-Host 'Built again, the same to the byte as the release.'
