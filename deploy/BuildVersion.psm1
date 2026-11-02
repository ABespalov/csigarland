# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Read the build counter and check the identity inside a built image.
# Contributors: None
#requires -Version 7.2

Set-StrictMode -Version Latest

function Get-csiGarlandBuildNumber {
    param([Parameter(Mandatory = $true)][string]$CounterPath)

    if (-not (Test-Path -LiteralPath $CounterPath)) {
        return 0
    }
    $raw = (Get-Content -LiteralPath $CounterPath -Raw -Encoding UTF8).Trim()
    $parsed = 0
    if (-not [int]::TryParse($raw, [ref]$parsed) -or $parsed -lt 0) {
        throw "Invalid csiGarland build counter: $CounterPath"
    }
    return $parsed
}

# Spelled the way firmware/app/scripts/firmware_build_version.py spells it.
function Get-csiGarlandFirmwareIdentity {
    param(
        [Parameter(Mandatory = $true)][string]$IniPath,
        [Parameter(Mandatory = $true)][int]$BuildNumber,
        [ValidateSet('Release', 'Dev')][string]$BuildProfile = 'Release'
    )

    $line = Select-String -LiteralPath $IniPath `
        -Pattern '^\s*CSIGARLAND_FIRMWARE_BASE\s*=\s*(\d+\.\d+\.\d+)(?:-([0-9A-Za-z][0-9A-Za-z.-]*))?\s*$' |
        Select-Object -First 1
    if (-not $line) {
        throw "CSIGARLAND_FIRMWARE_BASE not found in $IniPath"
    }
    $groups = $line.Matches[0].Groups
    $version = '{0}.{1}' -f $groups[1].Value, $BuildNumber
    if ($groups[2].Success) {
        $version += "-$($groups[2].Value)"
    }
    if ($BuildProfile -eq 'Dev') {
        $version += '-dev'
    }
    return $version
}

# An ESP32 app image: 24-byte file header, 8-byte segment header, then
# esp_app_desc_t (magic 0xABCD5432, version at +16). $null without a descriptor.
function Get-csiGarlandAppDescVersion {
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)

    if ($Bytes.Length -lt 80 -or
        $Bytes[32] -ne 0x32 -or $Bytes[33] -ne 0x54 -or
        $Bytes[34] -ne 0xCD -or $Bytes[35] -ne 0xAB) {
        return $null
    }
    $end = 48
    while ($end -lt 80 -and $Bytes[$end] -ne 0) {
        $end++
    }
    return [System.Text.Encoding]::ASCII.GetString($Bytes, 48, $end - 48)
}

# $null when esp_app_desc and the display's `FW ` string both name $Version;
# otherwise one line saying what the image holds.
function Get-csiGarlandBinIdentityProblem {
    param(
        [Parameter(Mandatory = $true)][string]$BinPath,
        [Parameter(Mandatory = $true)][string]$Version
    )

    if (-not (Test-Path -LiteralPath $BinPath)) {
        return "$BinPath does not exist"
    }
    $bytes = [System.IO.File]::ReadAllBytes($BinPath)
    $appDesc = Get-csiGarlandAppDescVersion -Bytes $bytes
    $ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
    $display = @([regex]::Matches($ascii, 'FW (\d+\.\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?)\x00') |
            ForEach-Object { $_.Groups[1].Value } |
            Select-Object -Unique)
    if ($appDesc -ceq $Version -and $display.Count -eq 1 -and $display[0] -ceq $Version) {
        return $null
    }
    $displayText = if ($display.Count -eq 0) { '(none)' } else { $display -join ', ' }
    return "$BinPath is not ${Version}: esp_app_desc '$appDesc', display FW $displayText"
}

# The ELF hash in esp_app_desc_t, as a garland reports it, or $null without a descriptor.
function Get-csiGarlandImageId {
    param([Parameter(Mandatory = $true)][string]$BinPath)

    $bytes = [System.IO.File]::ReadAllBytes($BinPath)
    if ($null -eq (Get-csiGarlandAppDescVersion -Bytes $bytes) -or $bytes.Length -lt 208) {
        return $null
    }
    return [System.Convert]::ToHexString($bytes, 176, 32).ToLowerInvariant()
}

# What a build is being made from, taken before it starts: HEAD alone does not say, as the tree
# may differ from it, so a digest of the firmware's files says exactly.
function Get-csiGarlandSourceIdentity {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)

    $commit = & git -C $RepoRoot rev-parse HEAD 2>$null
    $commitKnown = $LASTEXITCODE -eq 0
    # A git that failed would say clean and list nothing, and stamp a build with an empty digest.
    $changes = @(& git -C $RepoRoot status --porcelain)
    if ($LASTEXITCODE -ne 0) {
        throw "git status failed with exit code $LASTEXITCODE in $RepoRoot."
    }
    $paths = @(& git -C $RepoRoot ls-files --cached --others --exclude-standard -- firmware/app | Sort-Object -Culture '' -CaseSensitive)
    if ($LASTEXITCODE -ne 0 -or $paths.Count -eq 0) {
        throw "git ls-files listed no firmware files in $RepoRoot (exit code $LASTEXITCODE)."
    }
    $sha = [System.Security.Cryptography.IncrementalHash]::CreateHash('SHA256')
    try {
        foreach ($path in $paths) {
            $file = Join-Path $RepoRoot $path
            if (Test-Path -LiteralPath $file -PathType Leaf) {
                $line = "$path $((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash)`n"
                $sha.AppendData([System.Text.Encoding]::UTF8.GetBytes($line))
            }
        }
        $digest = [System.Convert]::ToHexString($sha.GetHashAndReset()).ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
    return [pscustomobject]@{
        commit        = if ($commitKnown) { $commit } else { $null }
        dirty         = $changes.Count -gt 0
        source_sha256 = $digest
    }
}

# Go keeps a cgo package compiled until something in its own folder changes, and the engine's sources
# are in firmware/app/src, so a build could carry the engine as it was. A digest of them in the flags,
# which are part of what Go keys its cache on, recompiles it exactly when they change.
function Set-csiGarlandEngineSources {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)

    $folder = Join-Path $RepoRoot 'firmware\app\src'
    $sha = [System.Security.Cryptography.IncrementalHash]::CreateHash('SHA256')
    try {
        foreach ($file in (Get-ChildItem -LiteralPath $folder -Recurse -File | Sort-Object FullName)) {
            $sha.AppendData([System.Text.Encoding]::UTF8.GetBytes("$($file.Name)`n"))
            $sha.AppendData([System.IO.File]::ReadAllBytes($file.FullName))
        }
        $digest = [System.Convert]::ToHexString($sha.GetHashAndReset()).ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
    # Beside whatever flags were already set, and in place of an earlier digest of these.
    $kept = @("$env:CGO_CPPFLAGS".Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries) | Where-Object { $_ -notlike '-DCSG_ENGINE_SOURCES=*' })
    $env:CGO_CPPFLAGS = (@($kept) + "-DCSG_ENGINE_SOURCES=$digest") -join ' '
}

# A copy of what git would keep of the firmware and the utility, under Destination, for a build that
# leaves this tree as it is and takes nothing of it but its sources. The components the lock names
# come from this tree's managed_components where it has them, as from a cache: they are what the
# lock says either way. Returns the copy's firmware/app.
function Copy-csiGarlandSources {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $sources = @(& git -C $RepoRoot ls-files --cached --others --exclude-standard -- firmware/app cmd internal deploy go.mod go.sum)
    if ($LASTEXITCODE -ne 0 -or $sources.Count -eq 0) {
        throw "git ls-files listed no sources (exit code $LASTEXITCODE)."
    }
    foreach ($path in $sources) {
        $from = Join-Path $RepoRoot $path
        if (Test-Path -LiteralPath $from -PathType Leaf) {
            $to = Join-Path $Destination $path
            New-Item -ItemType Directory -Path (Split-Path -Parent $to) -Force | Out-Null
            Copy-Item -LiteralPath $from -Destination $to
        }
    }
    $app = Join-Path $Destination 'firmware\app'
    $components = Join-Path $RepoRoot 'firmware\app\managed_components'
    if (Test-Path -LiteralPath $components) {
        Copy-Item -LiteralPath $components -Destination (Join-Path $app 'managed_components') -Recurse
    }
    return $app
}

Export-ModuleMember -Function `
    Copy-csiGarlandSources, `
    Set-csiGarlandEngineSources, `
    Get-csiGarlandBuildNumber, `
    Get-csiGarlandFirmwareIdentity, `
    Get-csiGarlandAppDescVersion, `
    Get-csiGarlandBinIdentityProblem, `
    Get-csiGarlandImageId, `
    Get-csiGarlandSourceIdentity
