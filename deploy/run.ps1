# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Builds the csiGarland utility and runs it, refusing to run a build that failed.
# Contributors: None
#
# The build belongs to deploy/build-utility.ps1 and stays there. A failed build leaves the previous
# file in assets/utility, and running that would open a window that works and is not the code just
# written, which looks like success. So the run is reached only through a build that returned.
#
# Touches no garland, and needs none to start. The script ends once the utility has started.
#
# Its bash twin, deploy/run.sh, on macOS starts the bundle deploy/build-utility.sh made beside the
# file. On Windows there is none, and this starts the file.
#
# Parameters
#
#   -Force
#       Build and spend a build number even though nothing changed.
#
#   -NoRun
#       Build and stop.
#
#   -BuildProfile Release|Dev
#       As deploy/build-utility.ps1 takes it: Release by default, Dev with debug information.
#
# Example:
#   pwsh -File deploy\run.ps1
#requires -Version 7.2

[CmdletBinding()]
param(
    [switch]$Force,
    [switch]$NoRun,
    [ValidateSet('Release', 'Dev')][string]$BuildProfile = 'Release'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$BuildScript = Join-Path $PSScriptRoot 'build-utility.ps1'

Write-Host 'Building the utility...'
try {
    $output = & $BuildScript -Force:$Force -BuildProfile $BuildProfile | Select-Object -Last 1
} catch {
    Write-Host ''
    Write-Host "Build failed: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host 'Any earlier build in assets\utility was not started.' -ForegroundColor Yellow
    exit 1
}

# Checked anyway: a build that reports success and leaves nothing is the case this script is for.
if (-not $output -or -not (Test-Path -LiteralPath $output)) {
    Write-Host "The build reported success, but left no file: '$output'" -ForegroundColor Red
    exit 1
}

if ($NoRun) {
    exit 0
}

# It only brings that one to the front, and a different build would not be what is shown.
$earlier = @(Get-Process -Name 'csigarland-*' -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -ne $output })
if ($earlier.Count -gt 0) {
    Write-Host "An earlier build is running, from $($earlier[0].Path). Close its window to see this one." -ForegroundColor Yellow
}
# On its own from here: it has no console to keep, and logs nothing to one.
Start-Process -FilePath $output
