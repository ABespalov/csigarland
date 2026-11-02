# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Stage the signed OTA package of the current firmware under assets/.
# Contributors: None
#
# Takes the last build when firmware.bin names the current build number in both of
# its identity strings, firmware.ota holds exactly that image, and it was made from the
# firmware sources as they stand, whose notices go with it. Otherwise it builds
# through flash-device.ps1, which spends one build number. A build that fails to match
# stops here, and assets/ keeps what it had.
#
# Parameters
#
#   -NewBuild
#       Spend a build number even though the last build is usable.
#
#   -BuildProfile Release|Dev
#       Release, the default, is what goes into a release: no debug information, and the plain
#       version, such as 1.0.0.102. Dev keeps the debug information and is staged as 1.0.0.102-dev.
#       A last build of the other profile is not used.
#
# Writes assets/firmware/csigarland-<version>.ota and a .sha256 beside it. The file is
# what the portal's Firmware update and ota-update.ps1 send to the garland. Its
# .manifest.json says what the build was made from, as recorded when it was made.
#
# Examples:
#   pwsh -File deploy\stage-assets.ps1
#   pwsh -File deploy\stage-assets.ps1 -NewBuild
#   pwsh -File deploy\stage-assets.ps1 -NewBuild -BuildProfile Dev
#requires -Version 7.2

[CmdletBinding()]
param(
    [switch]$NewBuild,
    [ValidateSet('Release', 'Dev')][string]$BuildProfile = 'Release'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# The header firmware/app/src/ota_logic.h reads: magic, format, the sizes of certificate,
# effects pack and image, their digests, signature. The three parts follow it in that order.
$HeaderBytes = 148

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$FirmwareDir = Join-Path $RepoRoot 'firmware\app'
$IniPath = Join-Path $FirmwareDir 'platformio.ini'
$CounterPath = Join-Path $FirmwareDir '.csigarland_build_counter'
$BinPath = Join-Path $FirmwareDir '.pio\build\esp32c6\firmware.bin'
$OtaPath = Join-Path $FirmwareDir '.pio\build\esp32c6\firmware.ota'
$StampPath = Join-Path $FirmwareDir '.pio\build\esp32c6\firmware.source.json'
$PackPath = Join-Path $FirmwareDir 'generated\effects.pack'
$OutputDir = Join-Path $RepoRoot 'assets\firmware'

Import-Module (Join-Path $PSScriptRoot 'BuildVersion.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'SigningPython.psm1') -Force

# Without it the last build may carry a throwaway chain, which no garland should be sent.
if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot 'cert\ota-signing.local.crt'))) {
    throw 'No update certificate. Make one: pwsh -File deploy\new-signing-certificate.ps1'
}

function Get-CurrentVersion {
    $build = Get-csiGarlandBuildNumber -CounterPath $CounterPath
    return Get-csiGarlandFirmwareIdentity -IniPath $IniPath -BuildNumber $build -BuildProfile $BuildProfile
}

# $null when the update file is the signed form of firmware.bin, else what is wrong.
function Get-PackageProblem {
    if (-not (Test-Path -LiteralPath $OtaPath)) {
        return "$OtaPath does not exist"
    }
    $package = [System.IO.File]::ReadAllBytes($OtaPath)
    $image = [System.IO.File]::ReadAllBytes($BinPath)
    if ($package.Length -lt $HeaderBytes -or [System.Text.Encoding]::ASCII.GetString($package, 0, 4) -cne 'CGOT') {
        return "$OtaPath has no update header"
    }
    $effects = [System.IO.File]::ReadAllBytes($PackPath)
    $packStart = $HeaderBytes + [System.BitConverter]::ToUInt32($package, 8)
    $imageStart = $packStart + $effects.Length
    if ($package.Length -ne $imageStart + $image.Length) {
        return "$OtaPath is $($package.Length) bytes for effects of $($effects.Length) and an image of $($image.Length)"
    }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    if ([System.Convert]::ToHexString($sha.ComputeHash($package, $imageStart, $image.Length)) -cne
        [System.Convert]::ToHexString($sha.ComputeHash($image))) {
        return "$OtaPath holds a different image than $BinPath"
    }
    if ([System.Convert]::ToHexString($sha.ComputeHash($package, $packStart, $effects.Length)) -cne
        [System.Convert]::ToHexString($sha.ComputeHash($effects))) {
        return "$OtaPath holds different effects than $PackPath"
    }
    return $null
}

function Get-BuildProblem {
    $problem = Get-csiGarlandBinIdentityProblem -BinPath $BinPath -Version (Get-CurrentVersion)
    if ($null -ne $problem) {
        return $problem
    }
    if (-not (Test-Path -LiteralPath $StampPath)) {
        return "nothing records what the last build was made from"
    }
    $stamp = Get-Content -Raw -LiteralPath $StampPath | ConvertFrom-Json
    if ($stamp.image -cne (Get-csiGarlandImageId -BinPath $BinPath)) {
        return "$StampPath is about another image"
    }
    # The notices staged beside it are this checkout's, so they describe it only if it was made here.
    if ($stamp.source_sha256 -cne (Get-csiGarlandSourceIdentity -RepoRoot $RepoRoot).source_sha256) {
        return 'the firmware sources changed since the last build, and the notices staged with it would describe these'
    }
    return Get-PackageProblem
}

$reason = if ($NewBuild) { 'a new build was asked for' } else { Get-BuildProblem }
if ($null -eq $reason) {
    Write-Host "Using the last build, $(Get-CurrentVersion)."
} else {
    Write-Host "Building, as $reason."
    & pwsh -File (Join-Path $PSScriptRoot 'flash-device.ps1') -BuildOnly -BuildProfile $BuildProfile
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE. Nothing was staged."
    }
    $problem = Get-BuildProblem
    if ($null -ne $problem) {
        throw "The build does not match: $problem. Nothing was staged."
    }
}

# Judged as a garland would judge it: signed under the owner's root, whatever made the build.
$checked = Get-csiGarlandOwnSignature -RepoRoot $RepoRoot -UpdatePath $OtaPath
if ($null -eq $checked) {
    throw "The update file is not signed under this repository's root, as a throwaway build would not be. Nothing was staged."
}

# A binary passed on owes the notices of what it holds, and the file cannot be judged with no Go.
if (Get-Command go -ErrorAction SilentlyContinue) {
    & pwsh -File (Join-Path $PSScriptRoot 'new-third-party-notices.ps1') -Check
    if ($LASTEXITCODE -ne 0) {
        throw 'THIRD_PARTY_NOTICES.md is not what this build linked. Nothing was staged.'
    }
} else {
    Write-Warning 'No Go here, so THIRD_PARTY_NOTICES.md was not checked against this build.'
}

$version = Get-CurrentVersion
$name = "csigarland-$version.ota"
$destination = Join-Path $OutputDir $name
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
Copy-Item -LiteralPath $OtaPath -Destination $destination -Force
# A binary passed on owes these with it.
Copy-Item -LiteralPath (Join-Path $RepoRoot 'THIRD_PARTY_NOTICES.md') -Destination $OutputDir -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot 'LICENSE') -Destination $OutputDir -Force
# The root the image trusts, which it holds anyway, for anyone to build the release again with.
Copy-Item -LiteralPath (Join-Path $RepoRoot 'cert\ota-root-ca.local.crt') -Destination (Join-Path $OutputDir 'csigarland-ota-root.crt') -Force

$hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText("$destination.sha256", "$hash  $name`n", [System.Text.UTF8Encoding]::new($false))

# What this file is, so that it can be told from another of the same version number.
$manifest = $checked | ConvertFrom-Json
$manifest | Add-Member version $version
$source = Get-Content -Raw -LiteralPath $StampPath | ConvertFrom-Json
$manifest | Add-Member commit $source.commit
$manifest | Add-Member dirty $source.dirty
$manifest | Add-Member source_sha256 $source.source_sha256
$manifest | Add-Member image_id $source.image
$manifest | Add-Member ota_sha256 $hash
[System.IO.File]::WriteAllText("$destination.manifest.json", (($manifest | ConvertTo-Json) + "`n"), [System.Text.UTF8Encoding]::new($false))

Write-Host "Staged $destination"
Write-Host "Staged $destination.sha256"
Write-Host "Staged $destination.manifest.json"
Write-Host "Staged $(Join-Path $OutputDir 'csigarland-ota-root.crt')"
