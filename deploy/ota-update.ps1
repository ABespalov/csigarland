# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Install over Wi-Fi an update file with firmware, effects, or both.
# Contributors: None
#
# Parameters
#
#   -Address <host>
#       The garland, as its certificate has it: csigarland.local, or your domain or fixed address.
#
#   -NoBuild
#       Send the update file from the last build instead of spending a build number.
#
#   -FirmwareOnly
#       Build and send only the firmware. The effects on the garland stay.
#
#   -EffectsOnly
#       Send only the effects of firmware/app/effects, with no build and no new firmware.
#
#   -File <path>
#       Send this update file as it is, with no build.
#
#   -BuildProfile Release|Dev
#       What to build: Release, the default, or Dev, with debug information and a -dev version.
#
#   -Insecure
#       For recovery, or reaching the garland by its address: do not check who answers, and fall
#       back to plain HTTP if HTTPS does not answer. Whoever stands between can then read the
#       token and fake the confirmation. An update's signature still guards what gets installed.
#
# The first time, the garland shows a code and asks for POWER and SPEED, like a browser's first
# visit, and the token it gives is kept in deploy/portal-token.local.txt for its 100 days. HTTPS
# is checked against this computer's trusted roots, so install the garland's root first, see
# docs/guides/user-guide.md. Without that nothing is sent, not even the token.
#
# The script shows what the file holds before sending it, and -WhatIf stops there. The
# garland installs what the file holds and leaves the rest. It refuses anything not signed
# under the root certificate its firmware holds, see deploy/new-signing-certificate.ps1. It
# then restarts, and goes back to the previous firmware by itself if a new one does not
# survive its first minute.
#
# Examples:
#   pwsh -File deploy\ota-update.ps1 -Address csigarland.local
#   pwsh -File deploy\ota-update.ps1 -Address csigarland.local -NoBuild
#   pwsh -File deploy\ota-update.ps1 -Address 192.168.1.57 -NoBuild -Insecure
#   pwsh -File deploy\ota-update.ps1 -Address csigarland.local -FirmwareOnly
#   pwsh -File deploy\ota-update.ps1 -Address csigarland.local -EffectsOnly
#   pwsh -File deploy\ota-update.ps1 -Address csigarland.local -File C:\Downloads\effects.ota -WhatIf
#requires -Version 7.2

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)][string]$Address,
    [switch]$NoBuild,
    [switch]$FirmwareOnly,
    [switch]$EffectsOnly,
    [string]$File = '',
    [switch]$Insecure,
    [ValidateSet('Release', 'Dev')][string]$BuildProfile = 'Release'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# The header firmware/app/src/ota_logic.h reads, and where an image keeps its version: past
# the image and segment headers, then 16 bytes into esp_app_desc_t.
$HeaderBytes = 148
$UpdateFormat = 3
$AppDescOffset = 32
$AppDescMagic = 0xABCD5432u
$AppVersionOffset = 48
$AppVersionBytes = 32
$AppElfHashOffset = 176
$AppElfHashBytes = 32

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$FirmwareDir = Join-Path $RepoRoot 'firmware\app'
$BuildDir = Join-Path $FirmwareDir '.pio\build\esp32c6'

Import-Module (Join-Path $PSScriptRoot 'SigningPython.psm1') -Force
# A garland is sent only what its own root signed: never a test build's throwaway chain.
Assert-csiGarlandOwnSigning -RepoRoot $RepoRoot
$BaseUri = "https://$Address"
$Uri = "$BaseUri/api/ota"
$TokenPath = Join-Path $PSScriptRoot 'portal-token.local.txt'
$TlsCheck = if ($Insecure) { @{ SkipCertificateCheck = $true } } else { @{} }

if (([int][bool]$FirmwareOnly + [int][bool]$EffectsOnly + [int][bool]$File) -gt 1) {
    throw 'Use one of -FirmwareOnly, -EffectsOnly and -File.'
}

# The effects as they are in the repository now, signed. Needs ESP-IDF's Python, for its
# cryptography package, but no build.
function New-EffectsUpdate {
    $python = Get-csiGarlandSigningPython
    $generated = Join-Path $FirmwareDir 'generated'
    $path = Join-Path $generated 'effects.ota'
    & $python (Join-Path $FirmwareDir 'scripts\effect_pack_tool.py') (Join-Path $FirmwareDir 'effects') $generated | Write-Host
    if ($LASTEXITCODE -ne 0) {
        throw 'The effects pack was not made. Nothing was sent.'
    }
    $certificates = Join-Path $PSScriptRoot '..\cert'
    & $python (Join-Path $FirmwareDir 'scripts\ota_sign_tool.py') sign $certificates (Join-Path $generated 'effects.pack') '-' $path | Write-Host
    if ($LASTEXITCODE -ne 0) {
        throw 'The effects pack was not signed. Nothing was sent.'
    }
    return $path
}

# What the file says it holds, to show before sending. Only the garland checks the signature.
function Get-UpdateContents {
    param([Parameter(Mandatory = $true)][byte[]]$Bytes, [Parameter(Mandatory = $true)][string]$Path)

    if ($bytes.Length -lt $HeaderBytes -or [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4) -cne 'CGOT') {
        throw "$Path is not a csiGarland update file. Nothing was sent."
    }
    if ([System.BitConverter]::ToUInt32($bytes, 4) -ne $UpdateFormat) {
        throw "$Path is in a format this script does not read. Nothing was sent."
    }
    $certificate = [long][System.BitConverter]::ToUInt32($bytes, 8)
    $pack = [long][System.BitConverter]::ToUInt32($bytes, 12)
    $image = [long][System.BitConverter]::ToUInt32($bytes, 16)
    if ($bytes.Length -ne $HeaderBytes + $certificate + $pack + $image -or ($pack + $image) -eq 0) {
        throw "$Path is incomplete. Nothing was sent."
    }
    $packStart = $HeaderBytes + $certificate
    $parts = @()
    $imageId = $null
    if ($image -gt 0) {
        $imageStart = $packStart + $pack
        $version = 'of unknown version'
        if ($image -ge $AppElfHashOffset + $AppElfHashBytes -and
            [System.BitConverter]::ToUInt32($bytes, $imageStart + $AppDescOffset) -eq $AppDescMagic) {
            $version = [System.Text.Encoding]::ASCII.GetString($bytes, $imageStart + $AppVersionOffset, $AppVersionBytes).Split([char]0)[0]
            $imageId = [System.Convert]::ToHexString($bytes, $imageStart + $AppElfHashOffset, $AppElfHashBytes).ToLowerInvariant()
        }
        $parts += "firmware $version"
    }
    if ($pack -gt 0) {
        # Records after the pack's 12-byte header: a kind letter, then four NUL-ended strings.
        $known = [System.BitConverter]::ToUInt32($bytes, $packStart + 4) -eq 2
        $effects = if ($known) { 0 } else { 'a newer set of' }
        $at = $packStart + 12
        while ($known -and $at -lt $packStart + $pack) {
            if ([char]$bytes[$at] -ne 'p') {
                $effects++
            }
            $at++
            for ($ended = 0; $ended -lt 4 -and $at -lt $packStart + $pack; $at++) {
                if ($bytes[$at] -eq 0) {
                    $ended++
                }
            }
        }
        $parts += "$effects effects"
    }
    $digest = if ($pack -gt 0) { [System.Convert]::ToHexString($bytes, 20, 32).ToLowerInvariant() } else { $null }
    return [pscustomobject]@{ Text = $parts -join ' and '; Image = $imageId; PackDigest = $digest }
}

if ($File) {
    $UpdatePath = $File
} elseif ($EffectsOnly) {
    $UpdatePath = New-EffectsUpdate
} else {
    if (-not $NoBuild) {
        & pwsh -File (Join-Path $PSScriptRoot 'flash-device.ps1') -BuildOnly -BuildProfile $BuildProfile
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed with exit code $LASTEXITCODE. Nothing was sent."
        }
    }
    $UpdatePath = Join-Path $BuildDir $(if ($FirmwareOnly) { 'firmware-only.ota' } else { 'firmware.ota' })
}
if (-not (Test-Path -LiteralPath $UpdatePath)) {
    throw "No update file at $UpdatePath. Build first."
}
# Read once, here: a build may replace the file while this waits for the owner, and what goes out
# has to be what was shown, and what the garland is then held to.
$UpdateBytes = [System.IO.File]::ReadAllBytes($UpdatePath)
$contents = Get-UpdateContents -Bytes $UpdateBytes -Path $UpdatePath
# The bytes to be sent, judged as the garland will judge them, so a refusal costs no restart.
$checkedPath = Join-Path ([System.IO.Path]::GetTempPath()) "csigarland-$([guid]::NewGuid().ToString('N')).ota"
try {
    [System.IO.File]::WriteAllBytes($checkedPath, $UpdateBytes)
    if (-not (Test-csiGarlandOwnSignature -RepoRoot $RepoRoot -UpdatePath $checkedPath)) {
        throw "$UpdatePath is not signed under this repository's root. Nothing was sent."
    }
} finally {
    [System.IO.File]::Delete($checkedPath)
}
Write-Host "This file holds $($contents.Text)."
if (-not $PSCmdlet.ShouldProcess($Address, "Install $($contents.Text)")) {
    return
}

# A field of an answer, or nothing from a firmware too old to give it.
function Get-Field {
    param($Object, [Parameter(Mandatory = $true)][string]$Name)

    $property = $Object.PSObject.Properties[$Name]
    if ($property) {
        return $property.Value
    }
    return $null
}

# Asks the owner at the panel, and returns the token the garland gives this script.
function Request-PortalToken {
    $json = @{ ContentType = 'application/json'; Method = 'Post' } + $TlsCheck
    $asked = Invoke-RestMethod @json -Uri "$BaseUri/api/auth/request" -Body (@{ label = 'ota-update.ps1' } | ConvertTo-Json)
    Write-Host "If the garland shows the code $($asked.code), press POWER and SPEED together there."
    $deadline = [DateTime]::UtcNow.AddSeconds($asked.seconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Seconds 2
        $headers = $null
        $answer = Invoke-RestMethod @json -Uri "$BaseUri/api/auth/wait" -Body (@{ request = $asked.request } | ConvertTo-Json) -ResponseHeadersVariable headers
        if ($answer.state -eq 'approved') {
            $cookie = @($headers['Set-Cookie'])[0]
            if ($cookie -notmatch '^csigh?=([0-9a-f]+);') {
                throw 'The garland answered without a token. Nothing was sent.'
            }
            return $Matches[1]
        }
        if ($answer.state -ne 'waiting') {
            break
        }
    }
    throw 'The request was not approved in time. Nothing was sent.'
}

$token = if (Test-Path -LiteralPath $TokenPath) { (Get-Content -LiteralPath $TokenPath -Raw).Trim() } else { '' }
$AuthHeaders = @{ Cookie = "csig=$token" }
# A check of who answers comes before the token goes anywhere. A firmware whose HTTPS failed keeps
# the portal on plain HTTP and must still take the next update, which is what -Insecure is for:
# the file's signature guards it the same, while the token travels unencrypted.
try {
    $null = Invoke-WebRequest -Uri $Uri @TlsCheck -Headers $AuthHeaders -TimeoutSec 15 -SkipHttpErrorCheck
} catch {
    if (-not $Insecure) {
        throw ("No trusted HTTPS at ${Address}: $($_.Exception.Message) Nothing was sent, not even the token. " +
            "Use a name or address the garland's certificate holds, and install its root on this computer " +
            "(docs/guides/user-guide.md, Stopping the Certificate Warning). If its HTTPS has failed, or to go on " +
            'without knowing who answers, add -Insecure.')
    }
    Write-Warning "No answer over HTTPS: $($_.Exception.Message). Trying plain HTTP."
    $BaseUri = "http://$Address"
    $Uri = "$BaseUri/api/ota"
}
try {
    $before = Invoke-RestMethod -Uri $Uri @TlsCheck -Headers $AuthHeaders
} catch {
    # A connection that broke has no response to ask, and what it said is what is to be told.
    $refused = $_.Exception.PSObject.Properties['Response'] -and $_.Exception.Response -and $_.Exception.Response.StatusCode.value__ -eq 401
    if (-not $refused) {
        throw
    }
    $token = Request-PortalToken
    [System.IO.File]::WriteAllText($TokenPath, "$token`n", [System.Text.UTF8Encoding]::new($false))
    $AuthHeaders = @{ Cookie = "csig=$token" }
    $before = Invoke-RestMethod -Uri $Uri @TlsCheck -Headers $AuthHeaders
}
# By number, as mDNS can be slow to find a garland that has just restarted.
$hostAddress = $Address
try {
    $hostAddress = ([System.Net.Dns]::GetHostAddresses($Address) | Where-Object { $_.AddressFamily -eq 'InterNetwork' } | Select-Object -First 1).IPAddressToString
} catch {
    Write-Verbose "Keeping the name: $_"
}
$beforeBoot = Get-Field $before 'boot'
Write-Host "Running now: $($before.version) with $($before.effects) built-in effects. Sending the update..."
$reply = Invoke-RestMethod -Uri $Uri @TlsCheck -Headers $AuthHeaders -Method Post -Body $UpdateBytes -ContentType 'application/octet-stream' -TimeoutSec 600
Write-Host "Installed: firmware $($reply.firmware), effects $($reply.effects). The garland is restarting."

# Over only when the garland has restarted, runs what was sent and has confirmed it: a first
# answer may come from before the restart, from a firmware still on trial, or from the old one
# after going back. A version may repeat, so the image says which one runs. Meanwhile the firmware
# on trial's log comes over plain HTTP, which is all there is to see of it on a closed unit.
$logPath = Join-Path $PSScriptRoot "..\temp\trial-log.$([DateTime]::Now.ToString('yyyyMMdd-HHmmss')).txt"
New-Item -ItemType Directory -Path (Split-Path -Parent $logPath) -Force | Out-Null
$seen = [System.Collections.Generic.HashSet[string]]::new()
$wentDown = $false
$httpsFailed = $false
$lastConfirmCheck = [DateTime]::MinValue
$said = ''
$unproved = $null
$deadline = [DateTime]::UtcNow.AddSeconds(180)
while ([DateTime]::UtcNow -lt $deadline) {
    Start-Sleep -Milliseconds 500
    $plainState = $null
    try {
        $log = Invoke-WebRequest -Uri "http://$hostAddress/trial-log" -TimeoutSec 2 -MaximumRedirection 0 -SkipHttpErrorCheck
        if ($log.StatusCode -eq 200) {
            $lines = ([string]$log.Content) -split "`n"
            $plainState = $lines[0]
            foreach ($line in $lines | Select-Object -Skip 1) {
                $clean = $line.TrimEnd()
                if ($clean -and $seen.Add($clean)) {
                    # On trial plain HTTP starts first, so https=0 alone says only "not yet".
                    $httpsFailed = $httpsFailed -or $clean -match 'no HTTPS, the portal stays'
                    Write-Host "  | $clean"
                    Add-Content -LiteralPath $logPath -Value $clean
                }
            }
        }
    } catch {
        $wentDown = $true
    }
    if (([DateTime]::UtcNow - $lastConfirmCheck).TotalSeconds -lt 3) {
        continue
    }
    $lastConfirmCheck = [DateTime]::UtcNow
    try {
        $after = Invoke-RestMethod -Uri $Uri @TlsCheck -Headers $AuthHeaders -TimeoutSec 3
    } catch {
        $after = $null
        # A firmware whose HTTPS failed still says so here, though anyone could.
        if ($httpsFailed -and $plainState -match '^version=(\S+) on_trial=(\d) https=0 boot=(\S*) image=(\S*) pack=(\S*)') {
            if ($Insecure) {
                $after = [pscustomobject]@{ version = $Matches[1]; on_trial = $Matches[2] -eq '1'; boot = $Matches[3]; image = $Matches[4]; pack_digest = $Matches[5]; effects = '?' }
            }
            if ($said -ne 'plain') {
                $said = 'plain'
                Write-Warning 'The new firmware has no working HTTPS and runs on plain HTTP. Its log above says why.'
            }
        } else {
            $wentDown = $true
        }
    }
    if ($null -eq $after) {
        continue
    }
    # Before the restart the old firmware still answers, which says nothing yet. One too old to
    # give its boot id shows the restart only by having gone away.
    if ((Get-Field $after 'boot') -eq $beforeBoot -and ($beforeBoot -or -not $wentDown)) {
        continue
    }
    # An answer that cannot say what runs proves nothing either way, and a version may repeat. It
    # may be the old firmware, before its restart or after going back, so only the time runs out.
    $runningImage = Get-Field $after 'image'
    $runningPack = Get-Field $after 'pack_digest'
    if (($contents.Image -and -not $runningImage) -or ($contents.PackDigest -and -not $runningPack)) {
        $unproved = $after.version
        continue
    }
    if ($contents.Image -and $runningImage -ne $contents.Image) {
        throw "Not installed: running $($after.version), not the firmware in this file. It went back to the previous firmware. What it logged is in $logPath"
    }
    if ($contents.PackDigest -and $runningPack -ne $contents.PackDigest) {
        throw 'Not installed: the garland is not running the effects in this file.'
    }
    if (-not $after.on_trial) {
        Write-Host "Confirmed: running $($after.version) with $($after.effects) built-in effects."
        return
    }
    if ($said -eq '') {
        $said = 'trial'
        Write-Host "Running $($after.version), which confirms itself after its first minute..."
    }
}
if ($httpsFailed -and -not $Insecure) {
    throw "Not confirmed: the new firmware has no working HTTPS, and a plain answer proves nothing. Its Info page and the log in $logPath say more. Its next update needs -Insecure."
}
if ($unproved) {
    throw "Not confirmed: the garland answers with version $unproved but cannot say which firmware or effects it runs. It may have gone back to the previous firmware. What it logged is in $logPath"
}
throw "The garland did not confirm the update within 180 s. If it restarted more than once it went back to the previous firmware. What it logged is in $logPath"
