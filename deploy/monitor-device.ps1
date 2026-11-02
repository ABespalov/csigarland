# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Read the log of a csiGarland board that is already running.
# Contributors: None
#
# This flashes nothing and consumes no build number, which is the point: a flash whose
# monitor did not attach does not have to be repeated to recover its log, and a board
# updated over the air has a log to read too. It needs no enrolled profile, because it
# only reads. With several boards plugged in it takes the enrolled one if that is there;
# otherwise it asks for -Port.
#
# Attaching does not reset the board: the lines that could are held released. So a boot
# log takes pressing RESET after the monitor has attached.
#
# The monitor stays attached across resets. The device re-enumerates, the port drops,
# and the monitor reopens it and keeps appending, so one file holds every boot in order.
# Press Ctrl+C to stop. Only one program can hold the port: flash-device.ps1 and this one
# cannot run at once.
#
# Parameters
#
#   -Port <COMn>
#       Read this port instead of finding the board.
#
#   -LogFile <path>
#       Write the log here instead of temp/serial_monitor.<build>.log. A relative path
#       resolves against the working directory and missing parents are created. The
#       default name carries the build number the last flash consumed, which is what ties
#       a log to an identified image; naming the file gives that up.
#
#   -TimeoutSeconds <n>
#       How long to wait for the board to appear, default 180. The wait repeats with an
#       elapsed count, and on giving up it prints every USB serial port it did see.
#
# Examples:
#   pwsh -File deploy\monitor-device.ps1
#   pwsh -File deploy\monitor-device.ps1 -Port COM17
#   pwsh -File deploy\monitor-device.ps1 -LogFile temp\session.log
#   pwsh -File deploy\monitor-device.ps1 -TimeoutSeconds 30
#requires -Version 7.2

[CmdletBinding()]
param(
    [string]$Port = '',
    [string]$LogFile = '',
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

Import-Module (Join-Path $PSScriptRoot 'DeviceMonitor.psm1') -Force

$logPath = Resolve-csiGarlandLogPath `
    -LogFile $LogFile `
    -TempRoot (Join-Path $RepoRoot 'temp') `
    -CounterPath (Join-Path $RepoRoot 'firmware\app\.csigarland_build_counter')
Start-csiGarlandLogSession `
    -Port $Port.Trim().ToUpperInvariant() `
    -ProfilePath (Join-Path $PSScriptRoot 'device-profile.local.json') `
    -LogPath $logPath `
    -TimeoutSeconds $TimeoutSeconds
