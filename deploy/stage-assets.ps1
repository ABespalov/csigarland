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
# through flash-device.ps1, which spends one build number. A build that fails, or fails to
# match, stops here, gives its number back, and assets/ keeps what it had.
#
# Parameters
#
#   -NewBuild
#       Spend a build number even though the last build is usable.
#
#   -BuildProfile Release|Dev
#       Release, the default, is what goes into a release: no debug information, and the plain
#       version, such as 1.0.0.102. Dev keeps the debug information and is staged as 1.0.0.102-dev.
#       Each profile builds in its own folder, so the last build of the other is never taken.
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
$PackPath = Join-Path $FirmwareDir 'generated\effects.pack'
$OutputDir = Join-Path $RepoRoot 'assets\firmware'

Import-Module (Join-Path $PSScriptRoot 'BuildVersion.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'SigningPython.psm1') -Force

# The profile's own folder: the last build of the other is in another.
$BuildDir = Join-Path $FirmwareDir ".pio\build\$(Get-csiGarlandFirmwareEnvironment -BuildProfile $BuildProfile)"
$BinPath = Join-Path $BuildDir 'firmware.bin'
$OtaPath = Join-Path $BuildDir 'firmware.ota'
$StampPath = Join-Path $BuildDir 'firmware.source.json'

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
    # A build before the zlib was recorded says nothing of the bytes of its portal's pages.
    if (-not $stamp.PSObject.Properties['zlib'] -or -not $stamp.zlib) {
        return 'the last build records no zlib, as one made before builds did'
    }
    # The notices staged beside it are this checkout's, so they describe it only if it was made here.
    if ($stamp.source_sha256 -cne (Get-csiGarlandSourceIdentity -RepoRoot $RepoRoot).source_sha256) {
        return 'the firmware sources changed since the last build, and the notices staged with it would describe these'
    }
    return Get-PackageProblem
}

# A build made here and then refused by any check of this script, or one whose staging failed, gives its
# number back, as a failed build does in flash-device.ps1: nothing of it was staged, and the next build
# takes the number. A build that was here before keeps its number, whatever happens to its staging.
$before = -1
$staged = $false
$destination = $null
# The firmware build lock of this checkout, held from the look at the last build, through a build of its
# own, to the set it stages: another build would replace the files staged, or take the number given back.
# flash-device.ps1 runs under it, told so.
$lock = Lock-csiGarlandBuild -RepoRoot $RepoRoot -Kind firmware
try {
    $reason = if ($NewBuild) { 'a new build was asked for' } else { Get-BuildProblem }
    if ($null -eq $reason) {
        Write-Host "Using the last build, $(Get-CurrentVersion)."
    } else {
        Write-Host "Building, as $reason."
        $before = Get-csiGarlandBuildNumber -CounterPath $CounterPath
        & pwsh -File (Join-Path $PSScriptRoot 'flash-device.ps1') -BuildOnly -BuildProfile $BuildProfile -LockHeld
        if ($LASTEXITCODE -ne 0) {
            # flash-device.ps1 gave it back already, if it was taken.
            $before = -1
            throw "Build failed with exit code $LASTEXITCODE. Nothing was staged."
        }
        $problem = Get-BuildProblem
        if ($null -ne $problem) {
            throw "The build does not match: $problem. Nothing was staged."
        }
    }

    # One set, copied at once and checked as a set, from which all that is staged is taken: another build of
    # this profile may replace the build folder's files meanwhile, and the manifest of one with the file of
    # another would describe neither.
    $version = Get-CurrentVersion
    $set = Join-Path $RepoRoot "temp\stage-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
    [System.IO.Directory]::CreateDirectory($set) | Out-Null
    foreach ($file in @($BinPath, $OtaPath, $StampPath, $PackPath)) {
        [System.IO.File]::Copy($file, (Join-Path $set (Split-Path -Leaf $file)), $true)
    }
    $BinPath = Join-Path $set (Split-Path -Leaf $BinPath)
    $OtaPath = Join-Path $set (Split-Path -Leaf $OtaPath)
    $StampPath = Join-Path $set (Split-Path -Leaf $StampPath)
    $PackPath = Join-Path $set (Split-Path -Leaf $PackPath)
    $problem = Get-BuildProblem
    if ($null -ne $problem -or $version -ne (Get-CurrentVersion)) {
        throw "The build changed while it was staged$(if ($problem) { ": $problem" }). Nothing was staged: stage it again."
    }

    # Judged as a garland would judge it: signed under the owner's root, whatever made the build. One copy of
    # the root, taken once, is both what it is judged by and what goes out beside it.
    $RootPath = Join-Path $set 'csigarland-ota-root.crt'
    [System.IO.File]::Copy((Join-Path $RepoRoot 'cert\ota-root-ca.local.crt'), $RootPath, $true)
    $checked = Get-csiGarlandOwnSignature -RepoRoot $RepoRoot -UpdatePath $OtaPath -Root $RootPath
    if ($null -eq $checked) {
        throw "The update file is not signed under this repository's root, as a throwaway build would not be. Nothing was staged."
    }

    # A binary passed on owes the notices of what it holds, and the file cannot be judged with no Go. The
    # notices read the licences of the Go modules from Go's cache, which a clean clone has yet to fill.
    if (Get-Command go -ErrorAction SilentlyContinue) {
        & go -C $RepoRoot mod download
        if ($LASTEXITCODE -ne 0) {
            throw "Go could not fetch the utility's modules, whose licences the notices hold (exit code $LASTEXITCODE). Nothing was staged."
        }
        & pwsh -File (Join-Path $PSScriptRoot 'new-third-party-notices.ps1') -Check -BuildProfile $BuildProfile
        if ($LASTEXITCODE -ne 0) {
            throw 'THIRD_PARTY_NOTICES.md is not what this build linked. Nothing was staged.'
        }
        $noticesChecked = $true
    } else {
        Write-Warning 'No Go here, so THIRD_PARTY_NOTICES.md was not checked against this build: no release takes it.'
        $noticesChecked = $false
    }

    $name = "csigarland-$version.ota"
    $destination = Join-Path $OutputDir $name
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    Copy-Item -LiteralPath $OtaPath -Destination $destination -Force
    # A binary passed on owes these with it.
    Copy-Item -LiteralPath (Join-Path $RepoRoot 'THIRD_PARTY_NOTICES.md') -Destination $OutputDir -Force
    Copy-Item -LiteralPath (Join-Path $RepoRoot 'LICENSE') -Destination $OutputDir -Force
    # The root the image trusts, which it holds anyway, for anyone to build the release again with.
    Copy-Item -LiteralPath $RootPath -Destination (Join-Path $OutputDir 'csigarland-ota-root.crt') -Force

    $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    [System.IO.File]::WriteAllText("$destination.sha256", "$hash  $name`n", [System.Text.UTF8Encoding]::new($false))

    # What this file is, so that it can be told from another of the same version number.
    $manifest = $checked | ConvertFrom-Json
    $manifest | Add-Member version $version
    $source = Get-Content -Raw -LiteralPath $StampPath | ConvertFrom-Json
    $manifest | Add-Member commit $source.commit
    $manifest | Add-Member dirty $source.dirty
    $manifest | Add-Member source_sha256 $source.source_sha256
    $manifest | Add-Member source_format $source.source_format
    $manifest | Add-Member image_id $source.image
    $manifest | Add-Member zlib $source.zlib
    $manifest | Add-Member ota_sha256 $hash
    # The notices and the licence staged with it are shared by every version in assets/firmware, and the next
    # staging writes them again: the manifest names the ones of this build, which a release is held to.
    $manifest | Add-Member notices_sha256 (Get-FileHash -LiteralPath (Join-Path $OutputDir 'THIRD_PARTY_NOTICES.md') -Algorithm SHA256).Hash.ToLowerInvariant()
    # Whether the notices were checked against what this build links, which a release asks for: a file of
    # the tag's bytes could still be of another set of libraries.
    $manifest | Add-Member notices_checked $noticesChecked
    $manifest | Add-Member license_sha256 (Get-FileHash -LiteralPath (Join-Path $OutputDir 'LICENSE') -Algorithm SHA256).Hash.ToLowerInvariant()
    # As the build made its pack: the sources are the build's, checked above, catalog among them.
    $manifest | Add-Member effects @((Get-Content -Raw -LiteralPath (Join-Path $FirmwareDir 'effects\catalog.json') | ConvertFrom-Json).effects).Count
    [System.IO.File]::WriteAllText("$destination.manifest.json", (($manifest | ConvertTo-Json) + "`n"), [System.Text.UTF8Encoding]::new($false))

    [System.IO.Directory]::Delete($set, $true)
    $staged = $true
} finally {
    if (-not $staged) {
        # Nothing half staged is left under the name of a version built here, which no staging held before.
        if ($destination -and $before -ge 0) {
            foreach ($left in $destination, "$destination.sha256", "$destination.manifest.json") {
                if (Test-Path -LiteralPath $left) {
                    Remove-Item -LiteralPath $left -Force
                }
            }
        }
        if ($before -ge 0 -and (Undo-csiGarlandBuildNumber -CounterPath $CounterPath -Before $before)) {
            Write-Warning "Nothing was staged, so build number $($before + 1) is not spent: the next build takes it."
        }
    }
    $lock.Dispose()
}
Write-Host "Staged $destination"
Write-Host "Staged $destination.sha256"
Write-Host "Staged $destination.manifest.json"
Write-Host "Staged $(Join-Path $OutputDir 'csigarland-ota-root.crt')"
