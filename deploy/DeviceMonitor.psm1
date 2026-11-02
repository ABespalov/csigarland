# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Read a running csiGarland board's log to the console and a file.
# Contributors: None
#
# This module only reads. It writes no firmware and consumes no build number, so it does
# not insist on the enrolled board the way flash-device.ps1 does. It prefers that board,
# and otherwise takes the only one there is.
#requires -Version 7.2

Set-StrictMode -Version Latest

# Deliberately not -Force: reloading a module a caller already imported would remove it
# from that caller's scope.
Import-Module (Join-Path $PSScriptRoot 'DeviceDetection.psm1')
Import-Module (Join-Path $PSScriptRoot 'BuildVersion.psm1')

# The C6's USB Serial/JTAG: the same identity in the application and in ROM download mode.
$script:BoardVid = '303A'
$script:BoardPid = '1001'

# The boards that fit: the requested port, or else the enrolled board if it is here, or
# else every board on the bus.
function Select-csiGarlandMonitorCandidates {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$Boards,
        [AllowEmptyString()][string]$Port = '',
        [AllowEmptyString()][string]$EnrolledHash = ''
    )

    if ($Port) {
        return @($Boards | Where-Object { $_.Port -ieq $Port })
    }
    $enrolled = @($Boards | Where-Object { $EnrolledHash -and $_.IdentityHash -ieq $EnrolledHash })
    if ($enrolled.Count -gt 0) {
        return $enrolled
    }
    return @($Boards)
}

# Returns the port name, or $null if none appeared before the timeout.
function Wait-csiGarlandDevicePort {
    param(
        [AllowEmptyString()][string]$Port = '',
        [AllowEmptyString()][string]$ProfilePath = '',
        [int]$TimeoutSeconds = 180,
        # A board that reset by itself is usually back well inside this.
        [int]$QuietSeconds = 6,
        # A single line and then silence would read as a hung script.
        [int]$PromptIntervalSeconds = 15
    )

    $enrolledHash = ''
    if ($ProfilePath -and (Test-Path -LiteralPath $ProfilePath)) {
        $enrolledHash = [string](Get-Content -LiteralPath $ProfilePath -Raw | ConvertFrom-Json).identityHash
    }
    $start = Get-Date
    $deadline = $start.AddSeconds($TimeoutSeconds)
    $nextPrompt = $start.AddSeconds($QuietSeconds)
    $boards = @()

    while ($true) {
        $boards = @(Get-csiGarlandSerialCandidates | Where-Object {
                $_.Vid -ieq $script:BoardVid -and $_.Pid -ieq $script:BoardPid
            })
        $fitting = @(Select-csiGarlandMonitorCandidates -Boards $boards -Port $Port -EnrolledHash $enrolledHash)
        if ($fitting.Count -eq 1) {
            return $fitting[0].Port
        }
        if ($fitting.Count -gt 1) {
            $names = ($fitting | ForEach-Object Port) -join ', '
            throw "More than one ESP32-C6 is present ($names). Choose one with -Port."
        }
        if ((Get-Date) -ge $deadline) {
            break
        }
        if ((Get-Date) -ge $nextPrompt) {
            $elapsed = [int]((Get-Date) - $start).TotalSeconds
            Write-Host "Waiting for the device (${elapsed}s of ${TimeoutSeconds}s). Check the USB cable, or press RESET."
            $nextPrompt = (Get-Date).AddSeconds($PromptIntervalSeconds)
        }
        Start-Sleep -Milliseconds 500
    }

    # "Not found" alone cannot tell a missing board from one on another port.
    $wanted = if ($Port) { "$Port with " } else { '' }
    Write-Host "No ${wanted}VID_$($script:BoardVid)&PID_$($script:BoardPid) appeared. USB serial ports seen:"
    $seen = @(Get-csiGarlandSerialCandidates)
    if ($seen.Count -eq 0) {
        Write-Host '  (none)'
    }
    foreach ($entry in $seen) {
        Write-Host "  $($entry.Port)  VID_$($entry.Vid)&PID_$($entry.Pid)"
    }
    return $null
}

# The default name carries the build number the last flash consumed, which is what ties
# a log to an identified image.
function Resolve-csiGarlandLogPath {
    param(
        [AllowEmptyString()][string]$LogFile = '',
        [Parameter(Mandatory = $true)][string]$TempRoot,
        [Parameter(Mandatory = $true)][string]$CounterPath
    )

    if ($LogFile) {
        $resolved = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine((Get-Location).Path, $LogFile))
    } else {
        $build = Get-csiGarlandBuildNumber -CounterPath $CounterPath
        $resolved = Join-Path $TempRoot "serial_monitor.$build.log"
    }
    New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($resolved)) -Force | Out-Null
    return $resolved
}

# Returns whether the port was ever opened: one that attached and then lost the
# device did its job, one that never opened did not.
function Start-csiGarlandMonitorOnce {
    param(
        [Parameter(Mandatory = $true)][string]$Port,
        [Parameter(Mandatory = $true)][string]$LogPath,
        [int]$OpenTimeoutSeconds = 15
    )

    $serial = [System.IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
    $serial.ReadTimeout = 500
    # The firmware logs ASCII, but the ROM and a crash can send anything; the default
    # encoding would turn each byte of a UTF-8 character into its own question mark.
    $serial.Encoding = [System.Text.Encoding]::UTF8
    # On USB Serial/JTAG these two lines are wired to the chip's reset and boot
    # logic, so both stay released: reading the log must not reset the board.
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $opened = $false
    $deadline = (Get-Date).AddSeconds($OpenTimeoutSeconds)
    while ($true) {
        try {
            $serial.Open()
            $opened = $true
            break
        } catch {
            # The port vanishes for a moment while the board re-enumerates.
            if ((Get-Date) -ge $deadline) { break }
            Start-Sleep -Milliseconds 250
        }
    }
    if (-not $opened) {
        $serial.Dispose()
        return $false
    }

    Write-Host "Monitoring $Port, logging to $LogPath. Press Ctrl+C to stop."
    $writer = [System.IO.StreamWriter]::new($LogPath, $true)
    $writer.AutoFlush = $true
    $writer.WriteLine("--- csiGarland monitor $Port opened $(Get-Date -Format o) ---")
    try {
        while ($serial.IsOpen) {
            try {
                $line = $serial.ReadLine()
            } catch [System.TimeoutException] {
                continue
            } catch {
                Write-Host 'Device disconnected.'
                break
            }
            $clean = $line.TrimEnd()
            Write-Host $clean
            $writer.WriteLine($clean)
        }
    } finally {
        $writer.Dispose()
        if ($serial.IsOpen) { $serial.Close() }
        $serial.Dispose()
    }
    return $true
}

# Stays attached across resets until Ctrl+C or until the board stops coming back, so one
# file holds every boot in order. Throws only if it never found a board at all.
function Start-csiGarlandLogSession {
    param(
        [AllowEmptyString()][string]$Port = '',
        [AllowEmptyString()][string]$ProfilePath = '',
        [Parameter(Mandatory = $true)][string]$LogPath,
        [int]$TimeoutSeconds = 180
    )

    $current = Wait-csiGarlandDevicePort -Port $Port -ProfilePath $ProfilePath -TimeoutSeconds $TimeoutSeconds
    if ($null -eq $current) {
        throw 'No board to read. Nothing was changed on any device.'
    }
    # Attaching does not reset the board, so what happened before now is not in this log.
    Write-Host 'Press RESET on the device to capture a boot log.'

    while ($true) {
        # Windows normally gives a reset board the same COM name, and reopening it is
        # much quicker than looking for it again.
        if (Start-csiGarlandMonitorOnce -Port $current -LogPath $LogPath) {
            Write-Host 'Reattaching. Press Ctrl+C to stop.'
            continue
        }
        $current = Wait-csiGarlandDevicePort -Port $Port -ProfilePath $ProfilePath -TimeoutSeconds $TimeoutSeconds
        if ($null -eq $current) {
            Write-Host 'The device did not come back. Stopping the monitor.'
            return
        }
    }
}

Export-ModuleMember -Function `
    Select-csiGarlandMonitorCandidates, `
    Wait-csiGarlandDevicePort, `
    Resolve-csiGarlandLogPath, `
    Start-csiGarlandLogSession
