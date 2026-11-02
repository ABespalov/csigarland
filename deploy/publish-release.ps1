# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Publish a staged release: its files on GitHub Releases, and the site's downloads and catalog pointing at them.
# Contributors: None
#
# A release is a base version, such as 1.0.0, with the firmware's update file and the utility as
# deploy/stage-assets.ps1 and deploy/build-utility.ps1 left them under assets/. Each counts its own
# builds, so each has its own version of that base, such as 1.0.0.102 and 1.0.0.27. Their addresses
# on GitHub Releases are known from the tag, v<base>, so the site's site/downloads/manifest.json and
# site/downloads/firmware-catalog-v1.json can be written first, and committed with the sources the
# tag names; the release is made once that tag is pushed.
#
# Parameters
#
#   -Release <a.b.c>
#       The release, such as 1.0.0, and its tag v1.0.0.
#
#   -FirmwareVersion <a.b.c.n>, -UtilityVersion <a.b.c.n>
#       The staged firmware and utility of that release, such as 1.0.0.102 and 1.0.0.27. Release
#       builds only: a Dev build's -dev version is refused.
#
#   -SiteOnly
#       Write the site's two files and stop: nothing is sent anywhere.
#
# Without -SiteOnly it checks that the tag is on GitHub, that every file is what its manifest says
# and the update file is signed under this repository's root, and makes the GitHub Release with
# them. It never pushes: commit, tag and push are the owner's to do.
#
# Examples:
#   pwsh -File deploy\publish-release.ps1 -Release 1.0.0 -FirmwareVersion 1.0.0.102 -UtilityVersion 1.0.0.27 -SiteOnly
#   pwsh -File deploy\publish-release.ps1 -Release 1.0.0 -FirmwareVersion 1.0.0.102 -UtilityVersion 1.0.0.27
#requires -Version 7.2

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Release,
    [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+\.\d+$')][string]$FirmwareVersion,
    [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+\.\d+$')][string]$UtilityVersion,
    [switch]$SiteOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Repository = 'ABespalov/csigarland'
$Tag = "v$Release"
$Base = "https://github.com/$Repository/releases/download/$Tag"
$FirmwareDir = Join-Path $RepoRoot 'assets\firmware'
$UtilityDir = Join-Path $RepoRoot 'assets\utility'
$Ota = Join-Path $FirmwareDir "csigarland-$FirmwareVersion.ota"
$Utility = Join-Path $UtilityDir "csigarland-$UtilityVersion-windows-amd64.exe"
$Files = @(
    $Ota, "$Ota.sha256", "$Ota.manifest.json", (Join-Path $FirmwareDir 'csigarland-ota-root.crt'),
    $Utility, "$Utility.manifest.json",
    (Join-Path $FirmwareDir 'THIRD_PARTY_NOTICES.md'), (Join-Path $FirmwareDir 'LICENSE')
)

Import-Module (Join-Path $PSScriptRoot 'SigningPython.psm1') -Force

foreach ($version in @($FirmwareVersion, $UtilityVersion)) {
    if (-not $version.StartsWith("$Release.")) {
        throw "$version is not a build of $Release."
    }
}

foreach ($file in $Files) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "No $file. Stage the release first: deploy\stage-assets.ps1 and deploy\build-utility.ps1."
    }
}

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

$firmwareBuild = Get-Content -Raw -LiteralPath "$Ota.manifest.json" | ConvertFrom-Json
$utilityBuild = Get-Content -Raw -LiteralPath "$Utility.manifest.json" | ConvertFrom-Json
if ($firmwareBuild.version -ne $FirmwareVersion -or $utilityBuild.version -ne $UtilityVersion) {
    throw "The staged firmware is $($firmwareBuild.version) and the utility $($utilityBuild.version), not $FirmwareVersion and $UtilityVersion."
}
if ((Get-Sha256 $Ota) -ne $firmwareBuild.ota_sha256 -or (Get-Sha256 $Utility) -ne $utilityBuild.sha256) {
    throw 'A staged file is not the one its manifest describes.'
}
if (-not (Test-csiGarlandOwnSignature -RepoRoot $RepoRoot -UpdatePath $Ota)) {
    throw "$Ota is not signed under this repository's root."
}
$effects = @((Get-Content -Raw -LiteralPath (Join-Path $RepoRoot 'firmware\app\effects\catalog.json') | ConvertFrom-Json).effects).Count
# The owner's day, as the changelog dates a release.
$today = (Get-Date).ToString('yyyy-MM-dd')
$sizeOf = { param($Path) '{0:N1} MB' -f ((Get-Item -LiteralPath $Path).Length / 1MB) }

# What the utility's Firmware page lists: each release's update file, by its address, length and hash.
$catalog = [ordered]@{
    format   = 'csigarland.firmware-catalog/1'
    firmware = @([ordered]@{
            version   = $FirmwareVersion
            file      = Split-Path -Leaf $Ota
            url       = "$Base/$(Split-Path -Leaf $Ota)"
            sha256    = $firmwareBuild.ota_sha256
            bytes     = (Get-Item -LiteralPath $Ota).Length
            effects   = $effects
            published = $today
        })
}
$manifest = [ordered]@{
    updated   = $today
    source    = "https://github.com/$Repository"
    release   = "https://github.com/$Repository/releases/tag/$Tag"
    artifacts = @(
        [ordered]@{ id = 'firmware'; name = 'Firmware and effects update'; platform = 'ESP32-C6-LCD-1.3'; available = $true
            version = $FirmwareVersion; url = "$Base/$(Split-Path -Leaf $Ota)"; sha256 = $firmwareBuild.ota_sha256; size = (& $sizeOf $Ota)
        },
        [ordered]@{ id = 'utility-windows'; name = 'csiGarland utility'; platform = 'Windows x64'; available = $true
            version = $UtilityVersion; url = "$Base/$(Split-Path -Leaf $Utility)"; sha256 = $utilityBuild.sha256; size = (& $sizeOf $Utility)
        },
        [ordered]@{ id = 'root-certificate'; name = 'Root certificate of the release, to build it again'; platform = 'Any'; available = $true
            version = $FirmwareVersion; url = "$Base/csigarland-ota-root.crt"; sha256 = (Get-Sha256 (Join-Path $FirmwareDir 'csigarland-ota-root.crt')); size = $null
        },
        [ordered]@{ id = 'utility-linux'; name = 'csiGarland utility'; platform = 'Linux'; available = $false; version = $null; url = $null; sha256 = $null; size = $null },
        [ordered]@{ id = 'utility-macos'; name = 'csiGarland utility'; platform = 'macOS'; available = $false; version = $null; url = $null; sha256 = $null; size = $null }
    )
}
$downloads = Join-Path $RepoRoot 'site\downloads'
New-Item -ItemType Directory -Path $downloads -Force | Out-Null
$utf8 = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText((Join-Path $downloads 'firmware-catalog-v1.json'), (($catalog | ConvertTo-Json -Depth 5) + "`n"), $utf8)
[System.IO.File]::WriteAllText((Join-Path $downloads 'manifest.json'), (($manifest | ConvertTo-Json -Depth 5) + "`n"), $utf8)
Write-Host "Wrote site\downloads\manifest.json and site\downloads\firmware-catalog-v1.json for $Tag."
if ($SiteOnly) {
    return
}

& gh api "repos/$Repository/git/ref/tags/$Tag" --silent 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "The tag $Tag is not on GitHub. Commit, tag and push it first."
}
$notes = Join-Path $RepoRoot "temp\release-notes-$Release.md"
New-Item -ItemType Directory -Path (Split-Path -Parent $notes) -Force | Out-Null
[System.IO.File]::WriteAllText($notes, @"
csiGarland ${Release}: firmware $FirmwareVersion, as an update file, and the csiGarland utility $UtilityVersion for Windows.

| File | SHA-256 |
|---|---|
| ``$(Split-Path -Leaf $Ota)`` | ``$($firmwareBuild.ota_sha256)`` |
| ``$(Split-Path -Leaf $Utility)`` | ``$($utilityBuild.sha256)`` |

The update file is signed under the root in ``csigarland-ota-root.crt``. Each file builds again to the byte from this tag's sources: see [Building a Release Again](https://github.com/$Repository/blob/$Tag/docs/guides/rebuild.md).
"@, $utf8)
if ($PSCmdlet.ShouldProcess($Repository, "Create the release $Tag with $($Files.Count) files")) {
    & gh release create $Tag @Files --repo $Repository --title "csiGarland $Release" --notes-file $notes --verify-tag
    if ($LASTEXITCODE -ne 0) {
        throw "gh release create failed with exit code $LASTEXITCODE."
    }
    Write-Host "Released https://github.com/$Repository/releases/tag/$Tag"
}
