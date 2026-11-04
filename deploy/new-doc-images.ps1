# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Makes the pictures of the garland's screens, portal pages and utility in docs/images.
# Contributors: None
#
# Touches no device. The screens come from the firmware's own drawing code on this computer, through
# firmware/app/tools/screens, and the portal pages are its real pages in a browser with no window,
# answered by deploy/portal_stub.py. The utility's pages are drawn by Gio with no window, by a test
# in internal/ui. Run it after a change to how a screen or page looks. It needs one firmware build
# first, for LVGL in managed_components, and Chrome or Edge.
#
# Parameters
#
#   -Version <text>
#       The firmware version the pictures show. A fixed one, so that a new build does not change
#       every picture.
#
#   -ScreensOnly, -PortalOnly, -UtilityOnly
#       Make only those.
#
# Example:
#   pwsh -File deploy\new-doc-images.ps1
#requires -Version 7.2

param(
    [string]$Version = '1.1.0.113',
    [switch]$ScreensOnly,
    [switch]$PortalOnly,
    [switch]$UtilityOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$FirmwareDir = Join-Path $RepoRoot 'firmware\app'
$ScratchDir = Join-Path $RepoRoot 'temp\doc-images'
$ScreenSize = 240
$ScreenScale = 1
$PortalWidth = 480
# A port nothing else holds, as a site served for a look, say, may hold any fixed one.
$StubPort = & {
    $probe = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $probe.Start()
    try { $probe.LocalEndpoint.Port } finally { $probe.Stop() }
}
# Picture name, and the page with what to press on it first.
$PortalPages = @(
    @('effects', '/'),
    @('effects-short', '/?rows=3'),
    @('schedule', '/schedule'),
    @('device', '/device'),
    @('network', '/network'),
    @('firmware', '/firmware'),
    @('info', '/info'),
    @('pair', '/pair?press=pair-start')
)

function Find-Python {
    $python = Get-ChildItem (Join-Path $env:USERPROFILE '.platformio\penv\.espidf-*\Scripts\python.exe') -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
    if (-not $python) {
        throw 'The Python environment of ESP-IDF was not found under .platformio: build the firmware once.'
    }
    return $python
}

function Find-Browser {
    foreach ($browser in @(
            (Join-Path $env:ProgramFiles 'Google\Chrome\Application\chrome.exe'),
            (Join-Path ${env:ProgramFiles(x86)} 'Google\Chrome\Application\chrome.exe'),
            (Join-Path ${env:ProgramFiles(x86)} 'Microsoft\Edge\Application\msedge.exe'),
            (Join-Path $env:ProgramFiles 'Microsoft\Edge\Application\msedge.exe'))) {
        if (Test-Path -LiteralPath $browser) {
            return $browser
        }
    }
    throw 'Neither Chrome nor Edge was found.'
}

function New-ScreenPictures {
    if (-not (Test-Path -LiteralPath (Join-Path $FirmwareDir 'managed_components\lvgl__lvgl\lvgl.h'))) {
        throw 'LVGL is not in managed_components: build the firmware once.'
    }
    & pio run -d $FirmwareDir -e screens 2>&1 | Select-Object -Last 3 | ForEach-Object { Write-Host "$_" }
    if ($LASTEXITCODE -ne 0) {
        throw "Building the screens tool failed with exit code $LASTEXITCODE."
    }
    $frames = Join-Path $ScratchDir 'screens'
    if (Test-Path -LiteralPath $frames) {
        Remove-Item -LiteralPath $frames -Recurse -Force
    }
    New-Item -ItemType Directory -Path $frames | Out-Null
    & (Join-Path $FirmwareDir '.pio\build\screens\program.exe') $frames $Version
    if ($LASTEXITCODE -ne 0) {
        throw "The screens tool failed with exit code $LASTEXITCODE."
    }
    & $Python (Join-Path $PSScriptRoot 'screens_to_png.py') $frames (Join-Path $RepoRoot 'docs\images\screens') $ScreenSize $ScreenScale
    if ($LASTEXITCODE -ne 0) {
        throw "Making the PNG files failed with exit code $LASTEXITCODE."
    }
}

# One command line from separate arguments, quoted as a Windows program reads them back, so that a
# path with a space stays one argument: Start-Process passes its list on joined as it stands.
function ConvertTo-NativeCommandLine {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    return ($Arguments | ForEach-Object {
            if ($_ -ne '' -and $_ -notmatch '[\s"]') {
                $_
            } else {
                '"' + ($_ -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
            }
        }) -join ' '
}

function Invoke-Browser {
    param(
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][string]$Picture,
        [Parameter(Mandatory = $true)][int]$Height
    )

    $arguments = @('--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run', '--no-default-browser-check', '--lang=en-US',
        "--user-data-dir=$(Join-Path $ScratchDir 'browser')", '--force-device-scale-factor=2',
        "--window-size=$PortalWidth,$Height", '--virtual-time-budget=4000', "--screenshot=$Picture", $Url)
    $run = Start-Process -FilePath $Browser -ArgumentList (ConvertTo-NativeCommandLine -Arguments $arguments) -PassThru -WindowStyle Hidden
    if (-not $run.WaitForExit(60000)) {
        $run.Kill($true)
        throw "The browser did not finish with $Url."
    }
    if (-not (Test-Path -LiteralPath $Picture)) {
        throw "The browser made no picture of $Url."
    }
}

function New-PortalPictures {
    $target = Join-Path $RepoRoot 'docs\images\portal'
    New-Item -ItemType Directory -Path $target, $ScratchDir -Force | Out-Null
    $env:CSIGARLAND_STUB_SHOTS = '1'
    $stub = Start-Process -FilePath $Python -ArgumentList (ConvertTo-NativeCommandLine -Arguments @((Join-Path $PSScriptRoot 'portal_stub.py'), $StubPort, $Version)) -PassThru -WindowStyle Hidden
    try {
        # Until it answers, rather than a guess at how long it takes to start.
        $deadline = [DateTime]::UtcNow.AddSeconds(20)
        while ($true) {
            try {
                $null = Invoke-RestMethod -Uri "http://127.0.0.1:$StubPort/stub/heights" -TimeoutSec 2
                break
            } catch {
                if ($stub.HasExited -or [DateTime]::UtcNow -gt $deadline) {
                    throw "The portal stub did not start on port $StubPort."
                }
                Start-Sleep -Milliseconds 200
            }
        }
        foreach ($entry in $PortalPages) {
            $name, $page = $entry
            $picture = Join-Path $target "$name.png"
            $url = "http://127.0.0.1:$StubPort$page"
            $path = $page -replace '\?press=[^&]*$', ''
            # Once to learn its height, and once at that height.
            Invoke-Browser -Url $url -Picture $picture -Height 900
            $heights = Invoke-RestMethod -Uri "http://127.0.0.1:$StubPort/stub/heights"
            $height = $heights.PSObject.Properties[$path]
            if ($null -eq $height) {
                throw "The page $path did not say how tall it is."
            }
            Invoke-Browser -Url $url -Picture $picture -Height $height.Value
            Write-Host "$name.png, $($height.Value) px tall"
        }
    } finally {
        $stub.Kill($true)
    }
}

function New-UtilityPictures {
    $target = Join-Path $RepoRoot 'docs\images\utility'
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    $env:CSIGARLAND_SHOTS = $target
    Import-Module (Join-Path $PSScriptRoot 'BuildVersion.psm1') -Force
    Set-csiGarlandEngineSources -RepoRoot $RepoRoot
    & go -C $RepoRoot test -tags utilityshot -count=1 -run TestRenderShots ./internal/ui
    if ($LASTEXITCODE -ne 0) {
        throw "Drawing the utility's pages failed with exit code $LASTEXITCODE."
    }
}

if (-not $UtilityOnly) {
    $Python = Find-Python
    if (-not $PortalOnly) {
        New-ScreenPictures
    }
    if (-not $ScreensOnly) {
        $Browser = Find-Browser
        New-PortalPictures
    }
}
if ($UtilityOnly -or -not ($ScreensOnly -or $PortalOnly)) {
    New-UtilityPictures
}
