# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Enroll, identify, build, and safely flash one exact csiGarland board.
# Contributors: None
#
# Flashes over the board's USB-C port. The ESP32-C6 keeps its USB Serial/JTAG
# port while the application runs and esptool resets it through that port, so no
# button is needed. A board whose image crashes before USB comes up needs ROM
# download mode: hold BOOT, tap RESET, release BOOT.
#
# Nothing is written to a device this repository has not been told about. The
# first flash of a new board needs -Enroll or an explicit -Port; every flash after
# that checks that the device on the port is still that same physical board.
#
# Parameters
#
#   -Port <COMn>
#       Which serial port to use. Naming one is deliberate targeting, so it does
#       not ask for confirmation, but it still has to match the enrolled board.
#       Without it the script finds the enrolled board itself and asks first.
#
#   -ListCandidates
#       Print the ESP32-C6 USB devices that are present and exit. Touches nothing.
#
#   -Enroll
#       Record the board on -Port as the one this repository may flash, then exit.
#       Only a hashed identity is stored, never a serial number.
#
#   -DetectOnly
#       Resolve and check the target exactly as a flash would, then exit without
#       opening the port or spending a build number.
#
#   -BuildOnly
#       Build and check the image, spending a build number, and write nothing.
#       Needs no board.
#
#   -EffectsOnly
#       Write only the effects of firmware/app/effects, with no build and no build number.
#       The firmware, settings and uploaded effects stay. Refused by a board whose
#       partition table has no slots for them.
#
#   -FullClean
#       Run `pio run -t clean` before building, rebuilding every component.
#
#   -NoMonitor
#       Stop after flashing. Without it the script stays attached to the board's
#       log and keeps a copy in temp/serial_monitor.<build>.log. monitor-device.ps1
#       attaches later, without flashing.
#
#   -LogFile <path>
#       Write the log here instead.
#
#   -LockHeld
#       For deploy\stage-assets.ps1 alone, which holds the firmware build lock of this checkout for all its
#       staging and runs this under it: no lock is taken here. Never given by hand.
#
#   -BuildProfile Release|Dev
#       Release, the default, builds with no debug information and the plain version, such as
#       1.0.0.102, as the environment esp32c6-release. Dev, as esp32c6, keeps the debug
#       information and marks the version 1.0.0.102-dev.
#
#   -WhatIf, -Confirm
#       Standard PowerShell. -WhatIf reports what would be flashed and stops.
#
# A flash writes the bootloader, partition table, update state, application and effects
# pack. Settings in NVS and uploaded effects stay, as long as the partition table keeps
# them where they are.
#
# Every flash builds first and checks the image before writing anything: the
# build counter must have moved by exactly one, and the .bin must name that
# identity both in esp_app_desc.version and in the display's `FW ` string. Any
# disagreement stops the flash with the device untouched. A build that fails, or fails a
# check, gives its number back, so the next build takes it; one that passed keeps it.
#
# Examples:
#   pwsh -File deploy\flash-device.ps1 -ListCandidates
#   pwsh -File deploy\flash-device.ps1 -Port COM5
#   pwsh -File deploy\flash-device.ps1
#   pwsh -File deploy\flash-device.ps1 -Port COM5 -NoMonitor
#   pwsh -File deploy\flash-device.ps1 -BuildOnly
#   pwsh -File deploy\flash-device.ps1 -EffectsOnly -Port COM5
#requires -Version 7.2

[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [string]$Port = '',
    [switch]$ListCandidates,
    [switch]$Enroll,
    [switch]$DetectOnly,
    [switch]$BuildOnly,
    [switch]$EffectsOnly,
    [switch]$FullClean,
    [switch]$NoMonitor,
    [string]$LogFile = '',
    [ValidateSet('Release', 'Dev')][string]$BuildProfile = 'Release',
    [switch]$LockHeld
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# The firmware build lock, once a write over USB holds it for the rest of the run.
$script:FirmwareLock = $null

$SupportedVid = '303A'
$SupportedPid = '1001'  # USB Serial/JTAG, in the application and in ROM download mode.

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$FirmwareDir = Join-Path $RepoRoot 'firmware\app'
$TempRoot = Join-Path $RepoRoot 'temp'
$ProfilePath = Join-Path $PSScriptRoot 'device-profile.local.json'
$CounterPath = Join-Path $FirmwareDir '.csigarland_build_counter'
Import-Module (Join-Path $PSScriptRoot 'BuildVersion.psm1') -Force
# Each profile in its own folder, so a change of profile compiles nothing again that has not changed.
$EnvironmentName = Get-csiGarlandFirmwareEnvironment -BuildProfile $BuildProfile
$BinPath = Join-Path $FirmwareDir ".pio\build\$EnvironmentName\firmware.bin"
$PackSlotPath = Join-Path $FirmwareDir 'generated\effects_slot.bin'
$PackBlankPath = Join-Path $FirmwareDir 'generated\effects_blank.bin'
$PartitionTableOffset = '0x8000'
$PartitionTableBytes = '0xC00'
$PySerialVersion = '3.5'

Import-Module (Join-Path $PSScriptRoot 'DeviceDetection.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'DeviceMonitor.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'SigningPython.psm1') -Force

# What it builds goes to a board, or to a garland by ota-update.ps1: never a throwaway chain.
Assert-csiGarlandOwnSigning -RepoRoot $RepoRoot

function Get-SupportedCandidates {
    return @(Get-csiGarlandSerialCandidates | Where-Object {
            $_.Vid -ieq $SupportedVid -and $_.Pid -ieq $SupportedPid
        })
}

function Read-DeviceProfile {
    if (-not (Test-Path -LiteralPath $ProfilePath)) {
        throw ('Device profile not found. Run -ListCandidates, then enroll the intended ' +
            'board with -Enroll -Port COMx. No port was touched.')
    }
    $deviceProfile = Get-Content -LiteralPath $ProfilePath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($deviceProfile.schemaVersion -ne 1 -or
        [string]::IsNullOrWhiteSpace($deviceProfile.vid) -or
        [string]::IsNullOrWhiteSpace($deviceProfile.pid) -or
        [string]::IsNullOrWhiteSpace($deviceProfile.identityHash)) {
        throw "Invalid device profile: $ProfilePath"
    }
    return $deviceProfile
}

function Save-DeviceProfile {
    param([Parameter(Mandatory = $true)][psobject]$Candidate)

    $json = [ordered]@{
        schemaVersion = 1
        vid           = $Candidate.Vid
        pid           = $Candidate.Pid
        identityHash  = $Candidate.IdentityHash
        enrolledAtUtc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json
    [System.IO.File]::WriteAllText($ProfilePath, "$json`n", [System.Text.UTF8Encoding]::new($false))
}

function Get-ExplicitPortCandidate {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$Candidates,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$PortName
    )

    $requested = $PortName.Trim().ToUpperInvariant()
    if (-not $requested) {
        throw 'An explicit -Port is required to enroll a device.'
    }
    $found = @($Candidates | Where-Object { $_.Port -eq $requested })
    if ($found.Count -ne 1) {
        throw "Expected one ESP32-C6 USB device on $requested; found $($found.Count)."
    }
    return $found[0]
}

# Drops what must be rebuilt for a new build number, keeping every other object.
function Clear-FirmwareBuildOutputs {
    $buildRoot = Join-Path $FirmwareDir ".pio\build\$EnvironmentName"
    foreach ($relative in 'firmware.bin', 'firmware.ota', 'firmware-only.ota', 'effects.ota', 'firmware.elf', 'firmware.map', 'firmware.source.json', 'esp-idf\esp_app_format', 'esp_app_format') {
        $target = Join-Path $buildRoot $relative
        if (Test-Path -LiteralPath $target) {
            Remove-Item -LiteralPath $target -Recurse -Force
        }
    }
}

function Invoke-Pio {
    param([Parameter(Mandatory = $true)][string[]]$PioArgs)

    Push-Location $FirmwareDir
    try {
        & pio @PioArgs 2>&1 | ForEach-Object { Write-Host $_.ToString() }
        return $LASTEXITCODE
    } finally {
        Pop-Location
    }
}

function Get-EspPython {
    $python = Get-ChildItem (Join-Path (Get-csiGarlandPlatformIOCore) 'penv\.espidf-*\Scripts\python.exe') | Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $python) {
        throw "The Python environment of ESP-IDF was not found under $(Get-csiGarlandPlatformIOCore). Nothing was written."
    }
    return $python
}

function Get-EsptoolPath {
    $esptool = Join-Path (Get-csiGarlandPlatformIOPackages) 'tool-esptoolpy\esptool.py'
    if (-not (Test-Path -LiteralPath $esptool)) {
        throw "esptool was not found under $(Get-csiGarlandPlatformIOPackages). Nothing was written."
    }
    return $esptool
}

# A new machine's ESP-IDF environment has no pyserial, which esptool needs and building does not.
function Confirm-EsptoolRuns {
    $python = Get-EspPython
    & $python -c 'import serial' 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Installing pyserial $PySerialVersion into ESP-IDF's Python, for esptool."
        & $python -m pip install --quiet "pyserial==$PySerialVersion" 2>&1 | ForEach-Object { Write-Host $_.ToString() }
        if ($LASTEXITCODE -ne 0) {
            throw "Could not install pyserial. Try: & '$python' -m pip install pyserial==$PySerialVersion. Nothing was written."
        }
    }
    $null = & $python (Get-EsptoolPath) version 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "esptool does not run under $python. Nothing was written."
    }
}

# One numbered build, checked before anything is written. The counter moves as the compile starts; a
# build that fails, or one that fails a check after it, gives its number back and leaves no image of it,
# so the next build takes the same number. An image that passed every check keeps it, written or not.
function Invoke-NumberedFirmwareBuild {
    if (-not (Get-Command pio -ErrorAction SilentlyContinue)) {
        throw 'PlatformIO (pio) is not on PATH.'
    }
    # One firmware build of this checkout at a time, from its outputs cleared to its number given back:
    # another would take the same number, or clear what this one is checking. Taken here unless the script
    # holds it already, for a write over USB, or the staging that runs this does.
    $lock = if ($LockHeld -or $script:FirmwareLock) { $null } else { Lock-csiGarlandBuild -RepoRoot $RepoRoot -Kind firmware }
    $built = $false
    $before = -1
    # A stop by Ctrl+C runs this finally too.
    try {
        if ($FullClean) {
            $cleanExit = Invoke-Pio -PioArgs @('run', '-e', $EnvironmentName, '-t', 'clean')
            if ($cleanExit -ne 0) {
                throw "PlatformIO clean failed with exit code $cleanExit"
            }
        }
        Clear-FirmwareBuildOutputs

        $source = Get-csiGarlandSourceIdentity -RepoRoot ([System.IO.Path]::GetFullPath((Join-Path $FirmwareDir '..\..')))
        $before = Get-csiGarlandBuildNumber -CounterPath $CounterPath
        $env:CSIGARLAND_INCREMENT_BUILD = '1'
        try {
            # What PlatformIO would delete on a change of platformio.ini, firmware_build_version.py keeps.
            $buildExit = Invoke-Pio -PioArgs @('run', '-e', $EnvironmentName, '--disable-auto-clean')
        } finally {
            Remove-Item Env:CSIGARLAND_INCREMENT_BUILD -ErrorAction SilentlyContinue
        }
        if ($buildExit -ne 0) {
            throw "PlatformIO build failed with exit code $buildExit. Nothing was written to the device."
        }

        $after = Get-csiGarlandBuildNumber -CounterPath $CounterPath
        if ($after -ne $before + 1) {
            throw ("Build counter went from $before to $after; a build consumes exactly one " +
                'number. Nothing was written to the device.')
        }
        $version = Get-csiGarlandFirmwareIdentity `
            -IniPath (Join-Path $FirmwareDir 'platformio.ini') `
            -BuildNumber $after `
            -BuildProfile $BuildProfile
        $problem = Get-csiGarlandBinIdentityProblem -BinPath $BinPath -Version $version
        if ($null -ne $problem) {
            throw ("Firmware identity mismatch: $problem. Nothing was written to the device. " +
                '-FullClean rebuilds every component.')
        }
        # Checked as a garland would check it, so that an image that trusts any other root never starts.
        if (-not (Test-csiGarlandOwnSignature -RepoRoot $RepoRoot -UpdatePath (Join-Path (Split-Path -Parent $BinPath) 'firmware.ota'))) {
            throw "The build is not signed under this repository's root, as a throwaway build would not be. Nothing was written to the device."
        }
        Write-Host "Image identity verified: $version in esp_app_desc and the display string."
        # Beside the image, so that whoever passes it on later says what it was made from.
        $source | Add-Member version $version
        $source | Add-Member image (Get-csiGarlandImageId -BinPath $BinPath)
        # The zlib the portal's pages were gzipped with, which the image's bytes depend on, as portal_pack.py
        # recorded it: a release is held to the one platformio.ini names.
        $source | Add-Member zlib (Get-Content -Raw -LiteralPath (Join-Path $FirmwareDir 'generated\portal_zlib.txt')).Trim()
        $stampPath = Join-Path (Split-Path -Parent $BinPath) 'firmware.source.json'
        [System.IO.File]::WriteAllText($stampPath, (($source | ConvertTo-Json) + "`n"), [System.Text.UTF8Encoding]::new($false))
        $image = [pscustomobject]@{
            Version    = $version
            Sha256     = (Get-FileHash -LiteralPath $BinPath -Algorithm SHA256).Hash
            PackSha256 = (Get-FileHash -LiteralPath $PackSlotPath -Algorithm SHA256).Hash
        }
        $built = $true
        return $image
    } finally {
        if (-not $built -and $before -ge 0) {
            Clear-FirmwareBuildOutputs
            if (Undo-csiGarlandBuildNumber -CounterPath $CounterPath -Before $before) {
                Write-Warning "The build failed, so build number $($before + 1) is not spent: the next build takes it."
            }
        }
        if ($lock) {
            $lock.Dispose()
        }
    }
}

if ($EffectsOnly -and ($BuildOnly -or $FullClean)) {
    throw '-EffectsOnly builds nothing, so it does not go with -BuildOnly or -FullClean.'
}

if ($BuildOnly) {
    $image = Invoke-NumberedFirmwareBuild
    Write-Host "Built $($image.Version). Nothing was written to any device."
    return
}

$candidates = @(Get-SupportedCandidates)
if ($ListCandidates) {
    if ($candidates.Count -eq 0) {
        Write-Host 'No ESP32-C6 USB devices are present.'
    } else {
        $candidates | Select-Object Port, Vid, Pid | Format-Table -AutoSize
    }
    return
}

if ($Enroll) {
    $candidate = Get-ExplicitPortCandidate -Candidates $candidates -PortName $Port
    if ($WhatIfPreference) {
        Write-Host "What if: would enroll the board on $($candidate.Port). Nothing was changed."
        return
    }
    Save-DeviceProfile -Candidate $candidate
    Write-Host 'Enrolled the detected csiGarland identity. No firmware was flashed.'
    return
}

# An explicit port is enough enrollment intent on first use.
$hasExplicitPort = -not [string]::IsNullOrWhiteSpace($Port)
if (-not (Test-Path -LiteralPath $ProfilePath) -and $hasExplicitPort) {
    $candidate = Get-ExplicitPortCandidate -Candidates $candidates -PortName $Port
    if ($WhatIfPreference) {
        Write-Host "What if: would enroll the board on $($candidate.Port), then build and flash it. Nothing was changed."
        return
    }
    Save-DeviceProfile -Candidate $candidate
    Write-Host "Enrolled the csiGarland identity currently attached to $($candidate.Port)."
}

$deviceProfile = Read-DeviceProfile
$target = Select-csiGarlandFlashTarget -Candidates $candidates -DeviceProfile $deviceProfile -RequestedPort $Port
Write-Host "Verified csiGarland target: port=$($target.Port)"
if ($DetectOnly) {
    Write-Host 'Detection succeeded. No firmware was flashed.'
    return
}

$action = if ($EffectsOnly) { 'Flash only the effects' } else { "Build and flash PlatformIO environment $EnvironmentName" }
if ($hasExplicitPort) {
    # Deliberate target selection must not cause an interactive question.
    if ($WhatIfPreference) {
        [void]$PSCmdlet.ShouldProcess($target.Port, $action)
        return
    }
} elseif (-not $PSCmdlet.ShouldProcess($target.Port, $action)) {
    return
}

# Where partitions.csv puts a partition. flasher_args.json names only what ESP-IDF flashes.
function Get-PartitionOffset {
    param([Parameter(Mandatory = $true)][string]$Label)

    $table = Get-Content -LiteralPath (Join-Path $FirmwareDir 'partitions.csv')
    $found = @($table | Select-String -Pattern "^\s*$Label\s*,[^,]*,[^,]*,\s*(0x[0-9A-Fa-f]+)\s*,")
    if ($found.Count -ne 1) {
        throw "partitions.csv has no single $Label. Nothing was written to the device."
    }
    return $found[0].Matches[0].Groups[1].Value
}

# The bootloader, partition table, update state, application and effects at the addresses
# this build's partition table gives them. esptool is called directly because PlatformIO offers
# no safe way to write them: a plain upload can rebuild first, and `-t nobuild` silently
# writes the application alone, at a fixed address. The pack goes to its first slot, and
# erasing the second's commit record makes sure no pack from an earlier update outranks it.
function Get-FlashImageArgs {
    $buildRoot = Join-Path $FirmwareDir ".pio\build\$EnvironmentName"
    $parts = @(
        @{ Offset = (Get-PartitionOffset -Label 'pack_a'); File = $PackSlotPath },
        @{ Offset = (Get-PartitionOffset -Label 'pack_b'); File = $PackBlankPath })
    if (-not $EffectsOnly) {
        $flasher = Get-Content -LiteralPath (Join-Path $buildRoot 'flasher_args.json') -Raw | ConvertFrom-Json
        $parts = @(
            @{ Offset = $flasher.bootloader.offset; File = 'bootloader.bin' },
            @{ Offset = $flasher.'partition-table'.offset; File = 'partitions.bin' },
            @{ Offset = $flasher.otadata.offset; File = 'ota_data_initial.bin' },
            @{ Offset = $flasher.app.offset; File = 'firmware.bin' }) + $parts
    }
    $imageArgs = foreach ($part in $parts) {
        $path = [System.IO.Path]::Combine($buildRoot, $part.File)
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Missing $path. Nothing was written to the device."
        }
        $part.Offset
        $path
    }
    return @($imageArgs)
}

function Invoke-Esptool {
    param([Parameter(Mandatory = $true)][string[]]$ToolArgs)

    & (Get-EspPython) (Get-EsptoolPath) @ToolArgs 2>&1 | ForEach-Object { Write-Host $_.ToString() }
    return $LASTEXITCODE
}

# The effects as they are in the repository now, with no build and no build number.
function New-EffectsOnlyImage {
    & (Get-EspPython) (Join-Path $FirmwareDir 'scripts\effect_pack_tool.py') (Join-Path $FirmwareDir 'effects') (Join-Path $FirmwareDir 'generated') |
        ForEach-Object { Write-Host $_.ToString() }
    if ($LASTEXITCODE -ne 0) {
        throw 'The effects pack was not made. Nothing was written to the device.'
    }
    return [pscustomobject]@{
        Version    = 'effects only'
        Sha256     = $null
        PackSha256 = (Get-FileHash -LiteralPath $PackSlotPath -Algorithm SHA256).Hash
    }
}

# With the firmware left as it is, so is the partition table: on an older layout these offsets
# belong to something else. Reads the table on the board and fails closed.
function Assert-BoardHasPackSlots {
    param([Parameter(Mandatory = $true)][string]$PortName)

    New-Item -ItemType Directory -Path $TempRoot -Force | Out-Null
    $tablePath = Join-Path $TempRoot 'partition-table.read.bin'
    $exit = Invoke-Esptool -ToolArgs @('--chip', 'esp32c6', '--port', $PortName, '--before', 'default_reset',
        '--after', 'no_reset', 'read_flash', $PartitionTableOffset, $PartitionTableBytes, $tablePath)
    if ($exit -ne 0) {
        throw "Could not read the board's partition table: esptool exit code $exit. Nothing was written."
    }
    $table = [System.IO.File]::ReadAllBytes($tablePath)
    Remove-Item -LiteralPath $tablePath -Force
    foreach ($label in 'pack_a', 'pack_b') {
        $line = @(Get-Content -LiteralPath (Join-Path $FirmwareDir 'partitions.csv') | Select-String -Pattern "^\s*$label\s*,\s*data\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)")
        if ($line.Count -ne 1) {
            throw "partitions.csv has no single $label. Nothing was written to the device."
        }
        $fields = $line[0].Matches[0].Groups
        $size = [System.Convert]::ToUInt32($fields[3].Value, 16)
        $fits = Test-csiGarlandPartitionEntry -Table $table -Label $label -Type 1 -SubType ([System.Convert]::ToByte($fields[1].Value, 16)) `
            -Offset ([System.Convert]::ToUInt32($fields[2].Value, 16)) -Size $size
        if (-not $fits) {
            throw "The board has no $label as this repository lays it out, so its firmware is too old for effects alone. Flash without -EffectsOnly. Nothing was written."
        }
        if ($label -ceq 'pack_a' -and (Get-Item -LiteralPath $PackSlotPath).Length -gt $size) {
            throw "The effects are larger than the $size bytes of $label. Nothing was written."
        }
    }
    Write-Host 'The board has the effects slots.'
}

# Every attempt is checked again first: a reconnect, a COM reassignment or another board
# during the build or the retry's pause must fail closed, with nothing written.
function Invoke-VerifiedUpload {
    $current = Select-csiGarlandFlashTarget `
        -Candidates @(Get-SupportedCandidates) `
        -DeviceProfile $deviceProfile `
        -RequestedPort $target.Port
    if ((-not $EffectsOnly -and (Get-FileHash -LiteralPath $BinPath -Algorithm SHA256).Hash -ne $image.Sha256) -or
        (Get-FileHash -LiteralPath $PackSlotPath -Algorithm SHA256).Hash -ne $image.PackSha256) {
        throw 'The image changed after it was verified. Nothing was written by this attempt. Flash again.'
    }
    $toolArgs = @(
        '--chip', 'esp32c6', '--port', $current.Port, '--baud', '921600',
        '--before', 'default_reset', '--after', 'hard_reset',
        'write_flash', '-z', '--flash_mode', 'keep', '--flash_freq', 'keep', '--flash_size', 'keep'
    ) + (Get-FlashImageArgs)
    return [pscustomobject]@{ Exit = (Invoke-Esptool -ToolArgs $toolArgs); Target = $current }
}

# A write over USB takes the files of the build folder after their checks: held until this script ends,
# the lock keeps another build of this checkout from replacing them in between.
$script:FirmwareLock = if ($LockHeld) { $null } else { Lock-csiGarlandBuild -RepoRoot $RepoRoot -Kind firmware }
if ($EffectsOnly) {
    Confirm-EsptoolRuns
    $image = New-EffectsOnlyImage
    Assert-BoardHasPackSlots -PortName $target.Port
} else {
    $image = Invoke-NumberedFirmwareBuild
    # Asked only of a write over USB, which runs it: a build alone, for staging or OTA, needs no esptool.
    Confirm-EsptoolRuns
}

$upload = Invoke-VerifiedUpload
if ($upload.Exit -ne 0) {
    # The image is built and verified; a failed upload is the port or the board.
    Write-Host ''
    Write-Host 'Upload failed; retrying once with the same verified image.'
    Start-Sleep -Seconds 2
    $upload = Invoke-VerifiedUpload
}
# The build folder's files are no longer needed: the log that follows holds no lock, so a staging or a
# build elsewhere need not wait for it to close.
if ($script:FirmwareLock) {
    $script:FirmwareLock.Dispose()
    $script:FirmwareLock = $null
}
if ($upload.Exit -ne 0) {
    throw ("Upload failed with exit code $($upload.Exit). If the board is not responding, " +
        'hold BOOT, tap RESET, release BOOT and run again.')
}
$verifiedTarget = $upload.Target
Write-Host "Flash succeeded: $($image.Version) on $($verifiedTarget.Port)"

if ($NoMonitor) {
    return
}
$logPath = Resolve-csiGarlandLogPath -LogFile $LogFile -TempRoot $TempRoot -CounterPath $CounterPath
Start-csiGarlandLogSession -Port $verifiedTarget.Port -ProfilePath $ProfilePath -LogPath $logPath
