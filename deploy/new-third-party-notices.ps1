# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Write THIRD_PARTY_NOTICES.md from the exact versions the project uses.
# Contributors: None
#
# Takes what the firmware links from the link maps of the last build, versions from
# firmware/app/dependencies.lock and go.mod, and each licence text from what the build and Go
# downloaded, so both have to have run on this machine. Fails rather than leave a component out.
#
# Example:
#   pwsh -File deploy\new-third-party-notices.ps1
#
# -Check writes nothing, and fails if the file is not what this would write.
#
# -BuildProfile Release|Dev takes the link maps of that profile's last build. Without it, the
# profile built last: both link the same components.
#
# -UtilityOnly checks the utility's part alone, with no firmware build: each of its components is named
# in the file under its own licence, and the file names none it no longer holds. It writes nothing.
#requires -Version 7.2

param(
    [switch]$Check,
    [switch]$UtilityOnly,
    [ValidateSet('', 'Release', 'Dev')][string]$BuildProfile = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Managed = Join-Path $RepoRoot 'firmware\app\managed_components'
Import-Module (Join-Path $PSScriptRoot 'BuildVersion.psm1') -Force
if (-not $UtilityOnly) {
    $profiles = if ($BuildProfile) { @($BuildProfile) } else { @('Release', 'Dev') }
    $BuildRoot = @($profiles | ForEach-Object { Join-Path $RepoRoot "firmware\app\.pio\build\$(Get-csiGarlandFirmwareEnvironment -BuildProfile $_)" } |
            Where-Object { (Test-Path -LiteralPath $_) -and (Get-ChildItem -LiteralPath $_ -Filter *.map -File) } |
            Sort-Object { (Get-ChildItem -LiteralPath $_ -Filter *.map -File | Select-Object -First 1).LastWriteTimeUtc } -Descending |
            Select-Object -First 1)
    if ($BuildRoot.Count -eq 0) {
        throw "No firmware build to take the link maps of: run pio run -d firmware/app -e esp32c6 first."
    }
    $BuildRoot = $BuildRoot[0]
}
$EspIdf = Join-Path (Get-csiGarlandPlatformIOPackages) 'framework-espidf'
$Toolchain = Join-Path (Get-csiGarlandPlatformIOPackages) 'toolchain-riscv32-esp'
if (-not (Get-Command go -ErrorAction SilentlyContinue)) {
    throw 'Go is not on PATH, and the notices read the licences of its modules from its cache.'
}
$GoCache = (& go env GOMODCACHE)
$OutputPath = Join-Path $RepoRoot 'THIRD_PARTY_NOTICES.md'

function Get-LicenceText {
    param([Parameter(Mandatory = $true)][string]$Folder)

    $file = Get-ChildItem -LiteralPath $Folder -File | Where-Object Name -match '^(LICEN[CS]E|COPYING)(\.|$)' | Select-Object -First 1
    if (-not $file) {
        throw "No licence file in $Folder"
    }
    return (Get-Content -Raw -LiteralPath $file.FullName).Trim()
}

function Format-Section {
    param([string]$Title, [string]$Licence)

    return "## $Title`n`n``````text`n$Licence`n```````n"
}

# Sections by licence text, so that one text under several names is written once.
$sectionTitles = [ordered]@{}

function Add-Licence {
    param([string]$Title, [string]$Licence)

    if (-not $sectionTitles.Contains($Licence)) {
        $sectionTitles[$Licence] = [System.Collections.Generic.List[string]]::new()
    }
    $sectionTitles[$Licence].Add($Title)
}

function Read-Licence {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "No licence at $Path"
    }
    return (Get-Content -Raw -LiteralPath $Path).Trim()
}

# Components of ESP-IDF with licences of their own, and the folder of each below components\.
$OwnLicences = @{
    esp_coex       = @('esp_coex\lib\LICENSE')
    esp_libc       = @('esp_libc\COPYING.NEWLIB', 'esp_libc\COPYING.picolibc')
    esp_netif      = @('lwip\lwip\COPYING')
    esp_phy        = @('esp_phy\lib\LICENSE')
    esp_wifi       = @('esp_wifi\lib\LICENSE')
    freertos       = @('freertos\FreeRTOS-Kernel\LICENSE.md')
    http_parser    = @('http_parser\LICENSE.txt')
    lwip           = @('lwip\lwip\COPYING')
    mbedtls        = @('mbedtls\mbedtls\LICENSE', 'mbedtls\mbedtls\tf-psa-crypto\LICENSE')
    wpa_supplicant = @('wpa_supplicant\COPYING')
}
# Libraries that come built, and whose component or package answers for them.
$BuiltLibraries = @{
    'libc.a'         = 'toolchain libc'
    'libm.a'         = 'toolchain libc'
    'libgcc.a'       = 'toolchain gcc'
    'libstdc++.a'    = 'toolchain gcc'
    'libcoexist.a'   = 'esp_coex'
    'libcore.a'      = 'esp_wifi'
    'libespnow.a'    = 'esp_wifi'
    'libmesh.a'      = 'esp_wifi'
    'libnet80211.a'  = 'esp_wifi'
    'libpp.a'        = 'esp_wifi'
    'libphy.a'       = 'esp_phy'
}
# Single linked files that keep their licence in their own header, with no file for it.
$FileNotices = @{ 'ubsan.c' = 'esp_system\ubsan.c' }

if (-not $UtilityOnly) {
    # What the link really used, of each image a board runs.
    $linkedComponents = [System.Collections.Generic.HashSet[string]]::new()
    $linkedBuilt = [System.Collections.Generic.HashSet[string]]::new()
    $linkedFiles = [System.Collections.Generic.HashSet[string]]::new()
    $checked = 0
    foreach ($image in @($BuildRoot, (Join-Path $BuildRoot 'bootloader'))) {
        $mapFile = Get-ChildItem -LiteralPath $image -Filter *.map -File | Select-Object -First 1
        if (-not $mapFile) {
            throw "No link map in ${image}: build the firmware first."
        }
        $sources = @{}
        foreach ($command in (Get-Content -Raw -LiteralPath (Join-Path $image 'compile_commands.json') | ConvertFrom-Json)) {
            if (($command.output -replace '\\', '/') -match '^esp-idf/([^/]+)/') {
                $key = "$($Matches[1])|$(Split-Path -Leaf $command.file)"
                if (-not $sources.ContainsKey($key)) {
                    $sources[$key] = [System.Collections.Generic.List[string]]::new()
                }
                $sources[$key].Add($command.file)
            }
        }
        foreach ($line in [System.IO.File]::ReadAllLines($mapFile.FullName)) {
            if ($line -match '^(Discarded input|Memory Configuration|Allocating common)') {
                break
            }
            if ($line -notmatch '^(\S.*?\.a)\(([^)]+)\)') {
                continue
            }
            $archive = $Matches[1] -replace '\\', '/'
            $member = $Matches[2]
            $checked += 1
            if ($archive -notmatch '(?:^|/)esp-idf/([^/]+)/(?:[^/]+/)*lib[^/]+\.a$') {
                $leaf = Split-Path -Leaf $archive
                if (-not $BuiltLibraries.ContainsKey($leaf)) {
                    throw "$archive is linked, and nothing here says what licence it is under."
                }
                $null = $linkedBuilt.Add($BuiltLibraries[$leaf])
                continue
            }
            $component = $Matches[1]
            $null = $linkedComponents.Add($component)
            if ($member -like 'x509_crt_bundle*') {
                throw "$archive($member) is linked: Mozilla's root certificates have terms of their own, and sdkconfig.defaults turns them off."
            }
            if ($component.Contains('__') -or $OwnLicences.ContainsKey($component)) {
                continue
            }
            $sourceName = $member -replace '\.o(bj)?$', ''
            if ($FileNotices.ContainsKey($sourceName)) {
                $null = $linkedFiles.Add($sourceName)
                continue
            }
            if (-not $sources.ContainsKey("$component|$sourceName")) {
                throw "$archive($member) is linked, and no source of it was found to read its licence from."
            }
            foreach ($source in $sources["$component|$sourceName"]) {
                $text = [System.IO.File]::ReadAllText($source)
                $header = $text.Substring(0, [Math]::Min(3000, $text.Length))
                if ($header -notmatch 'SPDX-License-Identifier:\s*Apache-2\.0\s' -and $header -notmatch 'Licensed under the Apache License, Version 2\.0') {
                    throw "$source is linked and is not under ESP-IDF's own licence. Name its licence in this script."
                }
            }
        }
    }

    $lock = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot 'firmware\app\dependencies.lock')
    $fromLock = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($match in [regex]::Matches($lock, '(?m)^  ([a-z0-9_-]+)/([a-z0-9_-]+):\r?\n(?:    .*\r?\n|    - .*\r?\n|      .*\r?\n)*?    version: ''?([^''\r\n]+)''?\r?$')) {
        $name = "$($match.Groups[1].Value)__$($match.Groups[2].Value)"
        # Some only help the build.
        if ($linkedComponents.Contains($name)) {
            $null = $fromLock.Add($name)
            Add-Licence "$($match.Groups[1].Value)/$($match.Groups[2].Value) $($match.Groups[3].Value)" (Get-LicenceText (Join-Path $Managed $name))
        }
    }
    foreach ($component in $linkedComponents) {
        if ($component.Contains('__') -and -not $fromLock.Contains($component)) {
            throw "$component is linked and is not in dependencies.lock."
        }
    }

    $idfVersion = [regex]::Match($lock, '(?m)^  idf:\r?\n    source:\r?\n      type: idf\r?\n    version: (\S+)').Groups[1].Value
    $underIdf = @($linkedComponents | Where-Object { -not $_.Contains('__') } | Sort-Object)
    Add-Licence "ESP-IDF $idfVersion, for all of its code unless named below: $($underIdf -join ', ')" (Get-LicenceText $EspIdf)
    foreach ($component in ($OwnLicences.Keys | Sort-Object)) {
        if ($linkedComponents.Contains($component) -or $linkedBuilt.Contains($component)) {
            foreach ($file in $OwnLicences[$component]) {
                Add-Licence "$component in ESP-IDF $idfVersion ($($file -replace '\\', '/'))" (Read-Licence (Join-Path $EspIdf "components\$file"))
            }
        }
    }
    foreach ($name in ($linkedFiles | Sort-Object)) {
        $header = [regex]::Match([System.IO.File]::ReadAllText((Join-Path $EspIdf "components\$($FileNotices[$name])")), '\A(?://.*\r?\n)+').Value
        Add-Licence "$($FileNotices[$name] -replace '\\', '/') in ESP-IDF $idfVersion" ($header -replace '(?m)^// ?', '').Trim()
    }

    $toolchainVersion = (Get-Content -Raw -LiteralPath (Join-Path $Toolchain 'package.json') | ConvertFrom-Json).version
    if ($linkedBuilt.Contains('toolchain libc')) {
        foreach ($file in 'COPYING.NEWLIB', 'COPYING.picolibc') {
            Add-Licence "picolibc in toolchain-riscv32-esp $toolchainVersion ($file)" (Read-Licence (Join-Path $Toolchain "share\licenses\picolibc\$file"))
        }
    }
    if ($linkedBuilt.Contains('toolchain gcc')) {
        foreach ($file in 'COPYING3', 'COPYING.RUNTIME') {
            Add-Licence "libgcc and libstdc++ in toolchain-riscv32-esp $toolchainVersion ($file)" (Read-Licence (Join-Path $Toolchain "share\licenses\gcc\$file"))
        }
    }
}

$goMod = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot 'go.mod')
foreach ($match in [regex]::Matches($goMod, '(?m)^\s*(?:require\s+)?([a-z0-9.-]+\.[a-z]+(?:/\S+)?) (v\S+)')) {
    $folder = Join-Path $GoCache "$($match.Groups[1].Value)@$($match.Groups[2].Value)"
    # A module only another platform builds with may not be in the cache yet, and its licence is owed all the same.
    if (-not (Test-Path -LiteralPath $folder)) {
        # In the module, wherever this was run from, as the bash version runs it.
        Push-Location -LiteralPath $RepoRoot
        try {
            & go mod download "$($match.Groups[1].Value)@$($match.Groups[2].Value)"
        } finally {
            Pop-Location
        }
    }
    Add-Licence "$($match.Groups[1].Value) $($match.Groups[2].Value) (Go)" (Get-LicenceText $folder)
}

# What the utility's own file holds beside its modules: Go's standard library and runtime, of the
# toolchain go.mod names, and the compiler's libraries for Windows that deploy/build-utility.ps1 lets it
# link statically, by the groups of deploy/BuildVersion.psm1. Their texts are kept in deploy\licences,
# as the compiler comes with none.
$goToolchain = [regex]::Match($goMod, '(?m)^toolchain go(\S+)').Groups[1].Value
Add-Licence "Go $goToolchain standard library and runtime, in the utility" (Read-Licence (Join-Path (& go env GOROOT) 'LICENSE'))
# GCC's are kept here too, as the firmware's toolchain has them, so that the utility's part needs no firmware
# package: a full run holds the two to be the same texts.
$Licences = Join-Path $PSScriptRoot 'licences'
foreach ($file in 'COPYING3', 'COPYING.RUNTIME') {
    $kept = Read-Licence (Join-Path $Licences "gcc-$file")
    if (-not $UtilityOnly -and $kept.Replace("`r`n", "`n") -cne (Read-Licence (Join-Path $Toolchain "share\licenses\gcc\$file")).Replace("`r`n", "`n")) {
        throw "deploy\licences\gcc-$file is not the toolchain's ${file}: copy it again from $Toolchain\share\licenses\gcc."
    }
    Add-Licence "libstdc++, libgcc and libgcc_eh of GCC, linked into the utility for Windows ($file)" $kept
}
Add-Licence 'winpthreads of mingw-w64, linked into the utility for Windows' (Read-Licence (Join-Path $Licences 'winpthreads-COPYING'))
Add-Licence "mingw-w64's runtime, linked into the utility for Windows: its start-up code, libmingw32, libmingwex and the import libraries of Windows' DLLs" (Read-Licence (Join-Path $Licences 'mingw-w64-runtime-COPYING'))

# A whole copy for the utility's engine, whose licence is at the head of the file.
$copiedCjson = [regex]::Match([System.IO.File]::ReadAllText((Join-Path $RepoRoot 'internal\engine\cJSON.c')), '(?s)\A\s*/\*(.*?)\*/').Groups[1].Value
Add-Licence 'cJSON, copied into internal/engine for the utility' (($copiedCjson -replace '(?m)^ {2}', '').Replace("`r`n", "`n").Trim())

if ($UtilityOnly) {
    $current = if (Test-Path -LiteralPath $OutputPath) { [System.IO.File]::ReadAllText($OutputPath).Replace("`r`n", "`n") } else { '' }
    $named = @{}
    foreach ($section in [regex]::Matches($current, '(?s)## ([^\n]+)\n\n```text\n(.*?)\n```\n')) {
        foreach ($title in $section.Groups[1].Value -split '; ') {
            $named[$title] = $section.Groups[2].Value
        }
    }
    $wanted = @{}
    foreach ($licence in $sectionTitles.Keys) {
        foreach ($title in $sectionTitles[$licence]) {
            $wanted[$title] = $licence.Replace("`r`n", "`n")
        }
    }
    $wrong = @($wanted.Keys | Where-Object { $named[$_] -cne $wanted[$_] } | Sort-Object)
    $left = @($named.Keys | Where-Object { ($_ -like '* (Go)' -or $_ -like '*utility*') -and -not $wanted.ContainsKey($_) } | Sort-Object)
    if ($wrong.Count -gt 0 -or $left.Count -gt 0) {
        throw "$OutputPath does not name what the utility holds: $((@($wrong) + @($left)) -join '; '). Run deploy\new-third-party-notices.ps1, and look at what changed."
    }
    Write-Host "$OutputPath names what the utility holds, $($wanted.Count) components."
    return
}

$sections = foreach ($licence in $sectionTitles.Keys) {
    Format-Section ($sectionTitles[$licence] -join '; ') $licence
}

$head = @'
# Third-Party Notices

`csiGarland` itself is under the [MIT License](LICENSE). Its firmware and Go program are built from the components below, at these exact versions. Written by `deploy/new-third-party-notices.ps1`: do not edit by hand.

The firmware's list comes from the link maps of the application and the bootloader: every library that reached the image is here, and the script fails on one it cannot place. The utility's comes from its Go modules and, for the compiler's libraries it links statically, from the groups `deploy/build-utility.ps1` holds its link map to: a build that links any other fails. It has had no legal review, and a file inside a component's own licence may name further authors of its own.

'@
$text = ($head + "`n" + ($sections -join "`n")).Replace("`r`n", "`n")
if ($Check) {
    # However this checkout's line endings came out.
    $current = if (Test-Path -LiteralPath $OutputPath) { [System.IO.File]::ReadAllText($OutputPath).Replace("`r`n", "`n") } else { '' }
    if ($current -cne $text) {
        throw "$OutputPath is not what the last build linked. Run deploy\new-third-party-notices.ps1, and look at what changed."
    }
    Write-Host "$OutputPath is up to date, with $($sectionTitles.Count) licences from $checked linked objects."
    return
}
[System.IO.File]::WriteAllText($OutputPath, $text, [System.Text.UTF8Encoding]::new($false))
Write-Host "Wrote $OutputPath with $($sectionTitles.Count) licences, from $checked linked objects."
