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

# The Python a pio installed with pip runs from, as pio itself says it; else by where pio.exe sits: in a
# virtual environment's Scripts folder beside python.exe, in a Python installed for the system one folder
# below it. A pip install for the user puts pio.exe in a folder of its own, which only pio can say of.
function Get-PioPython {
    $pio = Get-Command pio -ErrorAction SilentlyContinue
    if (-not $pio -or -not $pio.Source) {
        return $null
    }
    try {
        $said = & $pio.Source system info --json-output 2>$null | ConvertFrom-Json
        $python = "$($said.python_exe.value)"
        if ($LASTEXITCODE -eq 0 -and $python -and (Test-Path -LiteralPath $python)) {
            return $python
        }
    } catch {
        Write-Verbose "pio did not say its Python: $_"
    }
    $scripts = Split-Path -Parent $pio.Source
    foreach ($python in (Join-Path $scripts 'python.exe'), (Join-Path (Split-Path -Parent $scripts) 'python.exe')) {
        if (Test-Path -LiteralPath $python) {
            return $python
        }
    }
    return $null
}

# As the build chooses: the Python pio runs from, PlatformIO's own, then ESP-IDF's, newest first, the
# first that holds the pinned version. With none, the version goes into PlatformIO's own, as the build
# does, never into another Python, which the guides install it beside pio for.
function Get-csiGarlandSigningPython {
    $pinned = Get-csiGarlandPinnedCryptography
    # As PlatformIO takes it: PLATFORMIO_CORE_DIR, or .platformio in the home folder.
    $environments = Join-Path $(if ($env:PLATFORMIO_CORE_DIR) { $env:PLATFORMIO_CORE_DIR } else { Join-Path $HOME '.platformio' }) 'penv'
    $own = Join-Path $environments 'Scripts\python.exe'
    $pioPython = Get-PioPython
    $candidates = @($pioPython, $own) | Where-Object { $_ }
    if (Test-Path -LiteralPath $environments) {
        $candidates += @(Get-ChildItem -LiteralPath $environments -Directory -Force -Filter '.espidf-*' | Sort-Object Name -Descending |
                ForEach-Object { Join-Path $_.FullName 'Scripts\python.exe' })
    }
    foreach ($python in $candidates) {
        if ((Test-Path -LiteralPath $python) -and (Get-CryptographyVersion -Python $python) -eq $pinned) {
            return $python
        }
    }
    # A look ahead, as publish-release.ps1 -WhatIf, changes nothing: it says what is missing.
    if ($env:CSIGARLAND_NO_INSTALL) {
        throw "No Python of PlatformIO's holds cryptography ${pinned}, and nothing is installed now. Install it: & '$(if ($pioPython) { $pioPython } else { $own })' -m pip install cryptography==$pinned"
    }
    if (-not (Test-Path -LiteralPath $own)) {
        $beside = if ($pioPython) { " Install it beside pio: & '$pioPython' -m pip install cryptography==$pinned" } else { '' }
        throw "No Python of PlatformIO's holds cryptography ${pinned}, and PlatformIO's own is not at $own.$beside As docs/guides/build-and-flash.md says, pip installs both together."
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
# Under the repository's root in cert/, or under Root, such as the copy of it a release publishes.
function Get-csiGarlandOwnSignature {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$UpdatePath,
        [string]$Root = ''
    )
    $python = Get-csiGarlandSigningPython
    $trusted = if ($Root) { $Root } else { Join-Path $RepoRoot 'cert\ota-root-ca.local.crt' }
    $said = & $python (Join-Path $RepoRoot 'firmware\app\scripts\ota_sign_tool.py') verify $trusted $UpdatePath 2>$null
    if ($LASTEXITCODE -ne 0) {
        return $null
    }
    return ($said -join "`n")
}

function Test-csiGarlandOwnSignature {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$UpdatePath,
        [string]$Root = ''
    )
    return $null -ne (Get-csiGarlandOwnSignature -RepoRoot $RepoRoot -UpdatePath $UpdatePath -Root $Root)
}

Export-ModuleMember -Function Get-csiGarlandPinnedCryptography, Get-csiGarlandSigningPython, Assert-csiGarlandOwnSigning, Get-csiGarlandOwnSignature, Test-csiGarlandOwnSignature
