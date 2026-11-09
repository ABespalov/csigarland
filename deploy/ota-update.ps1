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
#       Send the update file from the last build instead of spending a build number: the last of
#       the profile -BuildProfile names. Without -BuildProfile, the one profile built here; with both
#       built, it asks for -BuildProfile rather than guess which was meant. A build whose firmware is
#       not of that profile and of this checkout's base version, as one left from 1.0.0 in the folder
#       Dev now has, is refused: -File sends it if it is meant.
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
#       back to plain HTTP if HTTPS does not answer, before the update or after it, when the new
#       firmware's HTTPS has failed: its paired route is then asked over plain HTTP with the token, as a
#       firmware on trial needs. Whoever stands between can then read the token and fake the
#       confirmation. An update's signature still guards what gets installed.
#
# The first time, the garland shows a code and asks for POWER and SPEED, like a browser's first
# visit, and the token it gives is kept in deploy/portal-token.local.txt for its 100 days. HTTPS
# is checked against this computer's trusted roots, so install the garland's root first, see
# docs/guides/user-guide.md. Without that nothing is sent, not even the token. The bash twin also takes
# --ca-file, as Apple's curl does not read the Keychain; PowerShell's web calls take no root of their own
# but the system's, so on Windows the root goes in its store, as for a browser.
#
# The script shows what the file holds before sending it, and -WhatIf stops there. The
# garland installs what the file holds and leaves the rest. It refuses anything not signed
# under the root certificate its firmware holds, see deploy/new-signing-certificate.ps1. It
# then restarts, and goes back to the previous firmware by itself if a new one has not
# answered a paired request within 10 minutes. Keep this script running: its requests are what
# let the new firmware confirm.
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
Import-Module (Join-Path $PSScriptRoot 'BuildVersion.psm1') -Force
$BuildDir = Join-Path $FirmwareDir ".pio\build\$(Get-csiGarlandFirmwareEnvironment -BuildProfile $BuildProfile)"

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
# cryptography package, but no build. Made in a folder of its own under temp\, never in the build's
# generated\, whose pack the last build is checked against.
function New-EffectsUpdate {
    $python = Get-csiGarlandSigningPython
    $generated = Join-Path $RepoRoot "temp\effects-update-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
    [System.IO.Directory]::CreateDirectory($generated) | Out-Null
    $path = Join-Path $generated 'effects.ota'
    $made = $false
    try {
        & $python (Join-Path $FirmwareDir 'scripts\effect_pack_tool.py') (Join-Path $FirmwareDir 'effects') $generated | Write-Host
        if ($LASTEXITCODE -ne 0) {
            throw 'The effects pack was not made. Nothing was sent.'
        }
        $certificates = Join-Path $PSScriptRoot '..\cert'
        & $python (Join-Path $FirmwareDir 'scripts\ota_sign_tool.py') sign $certificates (Join-Path $generated 'effects.pack') '-' $path | Write-Host
        if ($LASTEXITCODE -ne 0) {
            throw 'The effects pack was not signed. Nothing was sent.'
        }
        $made = $true
        return $path
    } finally {
        if (-not $made) {
            Remove-Item -LiteralPath $generated -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
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
    $imageVersion = $null
    if ($image -gt 0) {
        $imageStart = $packStart + $pack
        $version = 'of unknown version'
        if ($image -ge $AppElfHashOffset + $AppElfHashBytes -and
            [System.BitConverter]::ToUInt32($bytes, $imageStart + $AppDescOffset) -eq $AppDescMagic) {
            $version = [System.Text.Encoding]::ASCII.GetString($bytes, $imageStart + $AppVersionOffset, $AppVersionBytes).Split([char]0)[0]
            $imageVersion = $version
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
    return [pscustomobject]@{ Text = $parts -join ' and '; Image = $imageId; PackDigest = $digest; Version = $imageVersion }
}

# What is wrong with a firmware of version for a build of BuildProfile from this checkout, or $null: the
# folder may hold a build from before each profile had its own, such as a Release of 1.0.0 left in
# esp32c6, which is now Dev's.
# By the grammar the build spells versions with, see Get-csiGarlandFirmwareIdentity: the build number
# comes before the base's own suffix, as 1.1.0.123-rc.1, and -dev after it for Dev.
function Get-BuildVersionProblem {
    param([string]$Version, [Parameter(Mandatory = $true)][string]$BuildProfile)
    $ini = Join-Path $FirmwareDir 'platformio.ini'
    $number = [regex]::Match("$Version", '^\d+\.\d+\.\d+\.(\d+)(?:-|$)')
    if ($number.Success -and $Version -ceq (Get-csiGarlandFirmwareIdentity -IniPath $ini -BuildNumber ([int]$number.Groups[1].Value) -BuildProfile $BuildProfile)) {
        return $null
    }
    $wanted = Get-csiGarlandFirmwareIdentity -IniPath $ini -BuildNumber 0 -BuildProfile $BuildProfile
    return "firmware $(if ($Version) { $Version } else { 'of unknown version' }), not a $BuildProfile build of this checkout, such as $($wanted -replace '\.0(?=-|$)', '.N')"
}

$UpdateName = if ($FirmwareOnly) { 'firmware-only.ota' } else { 'firmware.ota' }
$UpdatePath = $null
# The firmware build lock of this checkout, when this builds or sends the last build: let go however this
# ends, as a lock left to the end of the PowerShell this runs in, inside another script's, held back every
# build after it.
$BuildLock = $null
try {
    if ($File) {
        $UpdatePath = $File
    } elseif ($EffectsOnly) {
        $UpdatePath = New-EffectsUpdate
    } else {
        # The firmware build lock of this checkout, from the build, or the look at the last one, to the bytes
        # read: another build would replace the file in between, with one this run did not make or check.
        $BuildLock = Lock-csiGarlandBuild -RepoRoot $RepoRoot -Kind firmware
        if (-not $NoBuild) {
            & pwsh -File (Join-Path $PSScriptRoot 'flash-device.ps1') -BuildOnly -BuildProfile $BuildProfile -LockHeld
            if ($LASTEXITCODE -ne 0) {
                throw "Build failed with exit code $LASTEXITCODE. Nothing was sent."
            }
        } elseif (-not $PSBoundParameters.ContainsKey('BuildProfile')) {
            # Each profile builds in its own folder, so "the last build" may be either: a Release left from
            # long ago is no answer for a Dev built since. Only a file of the kind asked for, whose firmware is
            # of its folder's profile and this checkout's base, counts.
            $built = @('Release', 'Dev' | Where-Object {
                    $candidate = Join-Path $FirmwareDir ".pio\build\$(Get-csiGarlandFirmwareEnvironment -BuildProfile $_)\$UpdateName"
                    if (-not (Test-Path -LiteralPath $candidate)) {
                        return $false
                    }
                    try {
                        $held = Get-UpdateContents -Bytes ([System.IO.File]::ReadAllBytes($candidate)) -Path $candidate
                    } catch {
                        return $false
                    }
                    # And signed under this repository's root: a test build's throwaway chain is no candidate.
                    $null -eq (Get-BuildVersionProblem -Version $held.Version -BuildProfile $_) -and
                    (Test-csiGarlandOwnSignature -RepoRoot $RepoRoot -UpdatePath $candidate)
                })
            if ($built.Count -gt 1) {
                throw 'Both a Release and a Dev build of this version are here: name the one to send with -BuildProfile Release or -BuildProfile Dev. Nothing was sent.'
            }
            if ($built.Count -eq 1) {
                $BuildDir = Join-Path $FirmwareDir ".pio\build\$(Get-csiGarlandFirmwareEnvironment -BuildProfile $built[0])"
                Write-Host "Sending the last $($built[0]) build, the only one of this version here."
            }
        }
        $UpdatePath = Join-Path $BuildDir $UpdateName
    }
    if (-not (Test-Path -LiteralPath $UpdatePath)) {
        throw "No update file at $UpdatePath. Build first."
    }
    # Read once, here: a build may replace the file while this waits for the owner, and what goes out
    # has to be what was shown, and what the garland is then held to.
    $UpdateBytes = [System.IO.File]::ReadAllBytes($UpdatePath)
} finally {
    if ($EffectsOnly -and $UpdatePath) {
        $madeFolder = Split-Path -Parent $UpdatePath
        if ((Split-Path -Leaf $madeFolder) -like 'effects-update-*') {
            Remove-Item -LiteralPath $madeFolder -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    if ($BuildLock) {
        $BuildLock.Dispose()
    }
}
$contents = Get-UpdateContents -Bytes $UpdateBytes -Path $UpdatePath
# A file from a build folder is held to its folder's profile and to this checkout's base, by the version
# the image itself holds. A file meant all the same goes by -File.
if (-not $File -and -not $EffectsOnly) {
    $folderProfile = if ((Split-Path -Leaf $BuildDir) -eq (Get-csiGarlandFirmwareEnvironment -BuildProfile 'Release')) { 'Release' } else { 'Dev' }
    $problem = Get-BuildVersionProblem -Version $contents.Version -BuildProfile $folderProfile
    if ($problem) {
        throw "$UpdatePath holds $($problem): a build left from before, it seems. Build again, or send it with -File if it is meant. Nothing was sent."
    }
}
# The bytes to be sent, judged as the garland will judge them, so a refusal costs no restart.
$checkedPath = Join-Path $RepoRoot "temp\csigarland-$([guid]::NewGuid().ToString('N')).ota"
[System.IO.Directory]::CreateDirectory((Split-Path -Parent $checkedPath)) | Out-Null
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

# The garland says how long the pairing code lasts. Only a number from 1 to 600 is used, as wait_seconds in the
# bash version takes it: digits, and a fraction after one point, cut to the whole seconds; anything else waits a
# minute. ASCII digits only: \d would take the digits of other scripts too.
function Get-WaitSeconds {
    param($Value)
    $text = "$Value"
    if ($text -cnotmatch '^[0-9]+(\.[0-9]+)?$') {
        return 60
    }
    $number = [double]::Parse($text, [cultureinfo]::InvariantCulture)
    $whole = [math]::Floor($number)
    if ($whole -lt 1 -or $whole -gt 600) {
        return 60
    }
    return $whole
}

# Asks the owner at the panel, and returns the token the garland gives this script.
function Request-PortalToken {
    $json = @{ ContentType = 'application/json'; Method = 'Post' } + $TlsCheck
    $asked = Invoke-RestMethod @json -Uri "$BaseUri/api/auth/request" -Body (@{ label = 'ota-update.ps1' } | ConvertTo-Json)
    Write-Host "If the garland shows the code $($asked.code), press POWER and SPEED together there."
    $deadline = [DateTime]::UtcNow.AddSeconds((Get-WaitSeconds $asked.seconds))
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

# The token is for this user alone: anyone else who read it could run the garland for its 100 days. On
# Windows the file takes the folder's ACL, as the user's own; elsewhere, under pwsh, it is narrowed as the
# bash twin narrows it.
function Protect-TokenFile {
    if (-not $IsWindows -and (Test-Path -LiteralPath $TokenPath)) {
        & chmod 600 $TokenPath
    }
}
Protect-TokenFile
$token = if (Test-Path -LiteralPath $TokenPath) { (Get-Content -LiteralPath $TokenPath -Raw).Trim() } else { '' }
$AuthHeaders = @{ Cookie = "csig=$token" }
# A check of who answers comes before the token goes anywhere. A firmware whose HTTPS failed keeps
# the portal on plain HTTP and must still take the next update, which is what -Insecure is for:
# the file's signature guards it the same, while the token travels unencrypted.
try {
    # With no token: it goes only to who has answered, as the bash twin sends it.
    $null = Invoke-WebRequest -Uri $Uri @TlsCheck -TimeoutSec 15 -SkipHttpErrorCheck
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
    # Written to a new file, then put in the old one's place, so that no reader sees half a token; on Linux and
    # macOS whoever held the old file open keeps its old bytes. Elsewhere than Windows the new file is made for this user alone from the start, by a shell under
    # umask 077, as .NET 6 makes a file as the umask has it and narrowing it after leaves a moment in which
    # another could open it; the token then goes into that file, whose mode a write keeps.
    $partial = "$TokenPath.$PID.new"
    if (-not $IsWindows) {
        & /bin/sh -c 'umask 077 && : > "$1"' sh $partial
        if ($LASTEXITCODE -ne 0) {
            throw "The token could not be kept in ${TokenPath}. Nothing was sent."
        }
    }
    [System.IO.File]::WriteAllText($partial, "$token`n", [System.Text.UTF8Encoding]::new($false))
    # Windows refuses to replace a file another program holds open, as a sync client or a virus scanner may
    # for a moment: tried again for a few seconds, and a token that cannot be kept leaves no copy behind.
    for ($try = 1; ; $try++) {
        try {
            [System.IO.File]::Move($partial, $TokenPath, $true)
            break
        } catch [System.IO.IOException], [System.UnauthorizedAccessException] {
            if ($try -ge 10) {
                Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue
                throw "The token could not be kept in ${TokenPath}, which another program holds open: $($_.Exception.Message) Nothing was sent; close it and run this again."
            }
            Start-Sleep -Milliseconds 500
        }
    }
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
# A refusal comes with a status, and is the answer. A connection lost with none may have taken the update
# whole, the garland restarting before its answer reached here: what it runs next says which, so the
# watch below goes on, and sending again before it ends could install the same file twice.
$sentUnknown = $null
try {
    $reply = Invoke-RestMethod -Uri $Uri @TlsCheck -Headers $AuthHeaders -Method Post -Body $UpdateBytes -ContentType 'application/octet-stream' -TimeoutSec 600
    Write-Host "Installed: firmware $($reply.firmware), effects $($reply.effects). The garland is restarting."
} catch {
    $answered = $_.Exception.PSObject.Properties['Response'] -and $_.Exception.Response
    if ($answered) {
        throw
    }
    $sentUnknown = $_.Exception.Message
    Write-Warning "No answer to the update: $sentUnknown. It may have been installed: watching what the garland runs..."
}

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
# Whether what runs is shown to be this send's own, where its answer was lost.
$proved = $false
$imageBefore = Get-Field $before 'image'
$packBefore = Get-Field $before 'pack_digest'
$deadline = [DateTime]::UtcNow.AddMinutes(12)
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
        # A firmware whose HTTPS failed says so in its open log, though anyone could. With -Insecure its own paired
        # route is asked over plain HTTP, with the token, by address: an answer of the firmware that runs, held to
        # the boot, image and effects below as any, and the paired request a firmware on trial confirms itself by,
        # which the open log is not. Without -Insecure the token never goes over plain HTTP.
        if ($httpsFailed -and $plainState -match '^version=\S+ on_trial=\d https=0 ') {
            if ($Insecure) {
                try {
                    $after = Invoke-RestMethod -Uri "http://$hostAddress/api/ota" -Headers $AuthHeaders -TimeoutSec 3 -MaximumRedirection 0
                } catch {
                    $after = $null
                }
            }
            if ($said -ne 'plain') {
                $said = 'plain'
                Write-Warning ('The new firmware has no working HTTPS and runs on plain HTTP. Its log above says why. ' +
                    $(if ($Insecure) { 'It is asked over plain HTTP, with the token, as -Insecure allows.' } else { 'Nothing is sent to it without -Insecure.' }))
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
    # With the answer lost, the bytes running say nothing of this send: the garland may have restarted for
    # another reason, already running them, or another program sent an update meanwhile. A trial of the
    # file's image says it took it, and so do effects alone that were not running, with the firmware as it
    # was. Anything else is told as seen, neither installed nor gone back.
    if ($sentUnknown -and -not $proved) {
        $tookImage = $contents.Image -and $after.on_trial -and $runningImage -eq $contents.Image
        $tookEffects = -not $contents.Image -and $contents.PackDigest -and $packBefore -ne $contents.PackDigest -and $runningPack -eq $contents.PackDigest -and $runningImage -eq $imageBefore
        if (-not ($tookImage -or $tookEffects)) {
            throw "Not confirmed: the garland restarted and runs $($after.version)$(if ($after.on_trial) { ', on trial' }), but the update got no answer ($sentUnknown), so whether it took this file cannot be told. Check its Info page before sending it again."
        }
        $proved = $true
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
        Write-Host "Running $($after.version), which confirms itself once it has answered a paired request, within 10 minutes..."
    }
}
if ($httpsFailed -and -not $Insecure) {
    throw "Not confirmed: the new firmware has no working HTTPS, and a plain answer proves nothing. Its Info page and the log in $logPath say more. Its next update needs -Insecure."
}
if ($unproved) {
    throw "Not confirmed: the garland answers with version $unproved but cannot say which firmware or effects it runs. It may have gone back to the previous firmware. What it logged is in $logPath"
}
if ($sentUnknown -and -not $wentDown) {
    throw "Not installed, as far as can be seen: the update got no answer ($sentUnknown), and in 12 minutes the garland did not restart. Check its Info page before sending it again."
}
throw "The garland did not confirm the update within 12 minutes. If it restarted more than once it went back to the previous firmware. What it logged is in $logPath"
