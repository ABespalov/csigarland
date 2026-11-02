# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: The one Python the update signing tool runs under, with the cryptography version the build pins.
# Contributors: None
#
# The build's own script, firmware/app/scripts/ota_signing.py, picks the same way and pins the
# version, so every script signs and checks with what the build signed with. A Python that merely
# imports some cryptography is not taken: an older one lacks what the tool calls.
#requires -Version 7.2

$script:SigningScript = Join-Path $PSScriptRoot '..\firmware\app\scripts\ota_signing.py'

# The version ota_signing.py pins, read from it, so there is one place to change it.
function Get-csiGarlandPinnedCryptography {
    $line = Select-String -LiteralPath $script:SigningScript -Pattern '^_CRYPTOGRAPHY = "cryptography==([0-9.]+)"' | Select-Object -First 1
    if (-not $line) {
        throw "No pinned cryptography version in $script:SigningScript."
    }
    return $line.Matches[0].Groups[1].Value
}

function Get-CryptographyVersion {
    param([Parameter(Mandatory = $true)][string]$Python)
    $version = & $Python -c 'import cryptography, sys; sys.stdout.write(cryptography.__version__)' 2>$null
    if ($LASTEXITCODE -ne 0) {
        return $null
    }
    return "$version".Trim()
}

# PlatformIO's own Python first, which the build runs under, then ESP-IDF's, newest first: the first
# that holds the pinned version. With none, the version goes into PlatformIO's, as the build does.
function Get-csiGarlandSigningPython {
    $pinned = Get-csiGarlandPinnedCryptography
    $environments = Join-Path $HOME '.platformio\penv'
    $own = Join-Path $environments 'Scripts\python.exe'
    $candidates = @($own)
    if (Test-Path -LiteralPath $environments) {
        $candidates += @(Get-ChildItem -LiteralPath $environments -Directory -Force -Filter '.espidf-*' | Sort-Object Name -Descending |
                ForEach-Object { Join-Path $_.FullName 'Scripts\python.exe' })
    }
    foreach ($python in $candidates) {
        if ((Test-Path -LiteralPath $python) -and (Get-CryptographyVersion -Python $python) -eq $pinned) {
            return $python
        }
    }
    if (-not (Test-Path -LiteralPath $own)) {
        throw "PlatformIO's Python is not at $own. Install PlatformIO, as docs/guides/build-and-flash.md says."
    }
    Write-Host "No Python of PlatformIO's holds cryptography ${pinned}: installing it into $own, as the build does."
    & $own -m pip install --quiet "cryptography==$pinned" 2>&1 | ForEach-Object { Write-Host $_.ToString() }
    if ($LASTEXITCODE -ne 0 -or (Get-CryptographyVersion -Python $own) -ne $pinned) {
        throw "Could not install cryptography $pinned. Try: & '$own' -m pip install cryptography==$pinned"
    }
    return $own
}

# A build signed by a throwaway chain trusts a root nobody keeps: a board that started on it would
# burn that root's hash into its eFuses and take no real update ever after. Scripts that write to a
# board or send to a garland refuse one.
function Assert-csiGarlandOwnSigning {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)
    if ($env:CSIGARLAND_RELEASE_ROOT) {
        throw 'CSIGARLAND_RELEASE_ROOT is set in this shell, which makes unsigned builds to compare with a release. Remove it: Remove-Item Env:CSIGARLAND_RELEASE_ROOT. Nothing was written or sent.'
    }
    if ($env:CSIGARLAND_THROWAWAY_SIGNING) {
        throw 'CSIGARLAND_THROWAWAY_SIGNING is set in this shell, which makes test builds that no board should be given. Remove it: Remove-Item Env:CSIGARLAND_THROWAWAY_SIGNING. Nothing was written or sent.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot 'cert\ota-root-ca.local.crt'))) {
        throw 'No update root certificate in cert/. Make one: pwsh -File deploy\new-signing-certificate.ps1. Nothing was written or sent.'
    }
}

# What the signing tool says of an update file signed under this repository's root, as a garland
# would judge it: what it holds, as JSON. $null for one that is not.
function Get-csiGarlandOwnSignature {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$UpdatePath
    )
    $python = Get-csiGarlandSigningPython
    $said = & $python (Join-Path $RepoRoot 'firmware\app\scripts\ota_sign_tool.py') verify (Join-Path $RepoRoot 'cert\ota-root-ca.local.crt') $UpdatePath 2>$null
    if ($LASTEXITCODE -ne 0) {
        return $null
    }
    return ($said -join "`n")
}

function Test-csiGarlandOwnSignature {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$UpdatePath
    )
    return $null -ne (Get-csiGarlandOwnSignature -RepoRoot $RepoRoot -UpdatePath $UpdatePath)
}

Export-ModuleMember -Function Get-csiGarlandPinnedCryptography, Get-csiGarlandSigningPython, Assert-csiGarlandOwnSigning, Get-csiGarlandOwnSignature, Test-csiGarlandOwnSignature
