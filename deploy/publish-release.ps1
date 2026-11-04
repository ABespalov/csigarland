# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Publish a staged release: its files on GitHub Releases, and the site's downloads and catalog pointing at them.
# Contributors: None
#
# A release is a base version, such as 1.0.0, with the firmware's update file and the utility as
# deploy/stage-assets.ps1 and deploy/build-utility.ps1 left them under assets/. Each counts its own
# builds, so each has its own version of that base, such as 1.0.0.102 and 1.0.0.27. Their addresses
# on GitHub Releases are known from the tag, v<base>, so the site's site/downloads/manifest.json and
# site/downloads/firmware-catalog-v1.json can be looked at first with -SiteOnly, which writes them to
# temp\ to see; the release, once its tag is pushed, writes them into site\downloads, to commit.
#
# A run publishes the firmware, the utility for one platform, or both. A release already on GitHub
# takes what a later run adds, as the utility for another platform: a file it holds already is never
# replaced, and its notes are written again with every file it then holds.
#
# Parameters
#
#   -Release <a.b.c>
#       The release, such as 1.0.0, and its tag v1.0.0.
#
#   -Only Firmware|Utility
#       Publish only the firmware, with its root certificate, or only the utility. Both by default.
#       What a run does not publish stays on the site as it was.
#
#   -Platform windows-amd64|linux-amd64|linux-arm64|darwin-amd64|darwin-arm64
#       The utility's platform, as Go names it and its file is named: windows-amd64 by default. A
#       platform is released only once deploy\BuildVersion.psm1 names the compiler its releases are
#       built with.
#
#   -FirmwareVersion <a.b.c.n>, -UtilityVersion <a.b.c.n>
#       The staged firmware and utility of that release, such as 1.0.0.102 and 1.0.0.27, each only for
#       what the run publishes. Release builds only: a Dev build's -dev version is refused.
#
#   -BuildNew
#       Build what the run publishes first, the firmware by deploy\stage-assets.ps1 -NewBuild and the
#       utility by deploy\build-utility.ps1 -Force, each spending a build number, and release what
#       they made: -FirmwareVersion and -UtilityVersion are then left out. The utility is built for
#       the computer this runs on, so -Platform must be its platform. Each base must be the release's,
#       which is checked before anything is built, and so must the tag here unless -SiteOnly. A build
#       that fails gives its number back. With -WhatIf it builds nothing, and stops there.
#
#   -SiteFromRelease
#       For a release already made on GitHub, as when the run that made it stopped before the site's
#       files were written: checks the release holds these files, byte for byte by size, and writes
#       site\downloads for it, dated as GitHub dates it. Makes no release.
#
#   -SiteOnly
#       Write the site's files to temp\site-preview-<release>\ and stop: nothing is sent anywhere,
#       and site\downloads stays as it is, as a file there before the release would be listed as out,
#       with links to nothing and the day of the preview for its release.
#
# Either way it checks that every file is what its manifest says, that the root published with the
# update file is the one its manifest names, and that the file is signed under it. Without -SiteOnly
# it also checks that the tag here is the one on GitHub, and that each file was built from what the
# tag holds, by its sources' digest worked out again from a checkout of it in temp\, and makes the
# GitHub Release with them, or adds them to it. Its release notes are the tag's CHANGELOG.md section
# of the release, each item by its first sentence, a link to the whole, and the release's files. The
# site's files are written only once the release holds the files, so -WhatIf, or a release GitHub
# refused, leaves them as they were. It never pushes: commit, tag and push are the owner's to do.
#
# Examples:
#   pwsh -File deploy\publish-release.ps1 -Release 1.0.0 -FirmwareVersion 1.0.0.102 -UtilityVersion 1.0.0.27 -SiteOnly
#   pwsh -File deploy\publish-release.ps1 -Release 1.0.0 -FirmwareVersion 1.0.0.102 -UtilityVersion 1.0.0.27
#   pwsh -File deploy\publish-release.ps1 -Release 1.1.0 -BuildNew
#   pwsh -File deploy\publish-release.ps1 -Release 1.1.0 -Only Firmware -BuildNew
#   pwsh -File deploy\publish-release.ps1 -Release 1.1.0 -Only Utility -Platform linux-amd64 -UtilityVersion 1.1.0.4
#requires -Version 7.2

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Release,
    [ValidateSet('Firmware', 'Utility')][string]$Only = '',
    [string]$Platform = 'windows-amd64',
    [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')][string]$FirmwareVersion = '',
    [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')][string]$UtilityVersion = '',
    [switch]$BuildNew,
    [switch]$SiteOnly,
    [switch]$SiteFromRelease
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Repository = 'ABespalov/csigarland'
$Tag = "v$Release"
Import-Module (Join-Path $PSScriptRoot 'BuildVersion.psm1') -Force

$WithFirmware = $Only -ne 'Utility'
$WithUtility = $Only -ne 'Firmware'
$UtilityPlatforms = Get-csiGarlandUtilityPlatforms
if (-not $UtilityPlatforms.Contains($Platform)) {
    throw "-Platform is one of $(@($UtilityPlatforms.Keys) -join ', '), not $Platform."
}
$PlatformName = $UtilityPlatforms[$Platform]
if (-not $WithUtility -and $PSBoundParameters.ContainsKey('Platform')) {
    throw '-Platform is the utility''s, and -Only Firmware publishes no utility.'
}
if (-not $WithFirmware -and $FirmwareVersion) {
    throw '-Only Utility publishes no firmware: leave out -FirmwareVersion.'
}
if (-not $WithUtility -and $UtilityVersion) {
    throw '-Only Firmware publishes no utility: leave out -UtilityVersion.'
}
$What = @($(if ($WithFirmware) { 'the firmware' }), $(if ($WithUtility) { "the utility for $PlatformName" }) | Where-Object { $_ }) -join ' and '

# One run of this script in this checkout at a time, from reading the site's lists and the release's notes
# to writing them: two at once, the firmware and a utility, would each write the lists without the
# other's. Held until this process ends.
$PublishLock = Lock-csiGarlandBuild -RepoRoot $RepoRoot -Kind publish
# git's answers as the text they are, a changelog's dashes among them.
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)

# An item's first sentence: up to the stop before a space and a capital, a code span or a bracket.
function Get-FirstSentence {
    param([Parameter(Mandatory = $true)][string]$Text)
    $first = [regex]::Match($Text, '^.*?[.!?](?=\s+[A-Z`(\[]|$)')
    return $first.Success ? $first.Value : $Text
}

# The release notes, from the section of Changelog that names the release: its opening words whole, its
# headings, and each item by its first sentence. A release the changelog does not name is refused.
function Get-ReleaseNotes {
    param([Parameter(Mandatory = $true)][string]$Text)
    $lines = @($Text -split '\r?\n')
    $heading = "^## \[$([regex]::Escape($Release))\]"
    $start = @(0..($lines.Count - 1) | Where-Object { $lines[$_] -match $heading } | Select-Object -First 1)
    if ($start.Count -eq 0) {
        throw "CHANGELOG.md at $Tag has no section for ${Release}: write it, and tag again."
    }
    # An item may go on over lines of its own, indented.
    $entries = [System.Collections.Generic.List[string]]::new()
    for ($i = $start[0] + 1; $i -lt $lines.Count -and $lines[$i] -notmatch '^## '; $i++) {
        if ($lines[$i] -match '^\s+\S' -and $entries.Count -gt 0 -and $entries[$entries.Count - 1].StartsWith('- ')) {
            $entries[$entries.Count - 1] += ' ' + $lines[$i].Trim()
        } else {
            $entries.Add($lines[$i])
        }
    }
    $said = foreach ($entry in $entries) {
        $entry.StartsWith('- ') ? '- ' + (Get-FirstSentence -Text $entry.Substring(2).Trim()) : $entry
    }
    return "## Release notes for $Release`n`n" + (($said -join "`n").Trim())
}

# What the tag holds, read from git as it stores the tag, with no checkout: what a checkout of it would
# give, and nothing written to the repository, under -WhatIf too. The tag on GitHub must be the one here.
$TagHolds = $null
$readTag = {
    $remoteTag = & gh api "repos/$Repository/git/ref/tags/$Tag" --jq '.object.sha' 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "The tag $Tag is not on GitHub. Commit, tag and push it first."
    }
    $localTag = & git -C $RepoRoot rev-parse --verify --quiet "refs/tags/$Tag"
    if ($LASTEXITCODE -ne 0 -or "$localTag".Trim() -ne "$remoteTag".Trim()) {
        throw "The tag $Tag here is not the one on GitHub. Fetch it, and build from it."
    }
    $blobOf = {
        param([string]$Path)
        $blob = & git -C $RepoRoot rev-parse --verify --quiet "${Tag}:$Path"
        if ($LASTEXITCODE -ne 0) {
            throw "$Tag holds no $Path."
        }
        return "$blob".Trim()
    }
    $catalogText = (& git -C $RepoRoot show "${Tag}:firmware/app/effects/catalog.json") -join "`n"
    $changelogText = (& git -C $RepoRoot show "${Tag}:CHANGELOG.md") -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw "$Tag holds no CHANGELOG.md."
    }
    return [pscustomobject]@{
        Commit   = "$(& git -C $RepoRoot rev-parse "$Tag^{commit}")".Trim()
        Firmware = Get-csiGarlandTreeDigest -RepoRoot $RepoRoot -Paths @('firmware/app') -Revision $Tag
        Utility  = Get-csiGarlandUtilitySourceDigest -RepoRoot $RepoRoot -Revision $Tag
        Effects  = @(($catalogText | ConvertFrom-Json).effects).Count
        Notices  = & $blobOf 'THIRD_PARTY_NOTICES.md'
        License  = & $blobOf 'LICENSE'
        Notes    = Get-ReleaseNotes -Text $changelogText
    }
}
# Whether a file's bytes are those of the tag's blob: as git would hash it, with no filters.
$isTagBlob = {
    param([string]$File, [string]$Blob)
    $here = & git -C $RepoRoot hash-object --no-filters -- $File
    return $LASTEXITCODE -eq 0 -and "$here".Trim() -eq $Blob
}

if ($BuildNew) {
    if ($SiteFromRelease) {
        throw '-BuildNew makes new builds, and -SiteFromRelease writes the site for files released already: choose one.'
    }
    if ($FirmwareVersion -or $UtilityVersion) {
        throw '-BuildNew releases what it builds: leave out -FirmwareVersion and -UtilityVersion.'
    }
    # Checked before a number is spent: a build of another base could not be released as this one.
    if ($WithFirmware) {
        $firmwareBase = Get-csiGarlandFirmwareIdentity -IniPath (Join-Path $RepoRoot 'firmware\app\platformio.ini') -BuildNumber 0 -BuildProfile Release
        if ($firmwareBase -cne "$Release.0") {
            throw "The firmware's base, CSIGARLAND_FIRMWARE_BASE in firmware\app\platformio.ini, makes $($firmwareBase -replace '\.0(?=-|$)', '.N'), not a build of $Release. Nothing was built."
        }
    }
    if ($WithUtility) {
        $utilityBase = [regex]::Match((Get-Content -Raw -LiteralPath (Join-Path $RepoRoot 'cmd\csigarland\version.go')), 'Version = "([^"]*)"')
        if (-not $utilityBase.Success -or $utilityBase.Groups[1].Value -cne $Release) {
            throw "The utility's base, in cmd\csigarland\version.go, is $($utilityBase.Groups[1].Value), not $Release. Nothing was built."
        }
        # build-utility.ps1 builds for the computer it runs on, as cgo needs that platform's own compiler.
        if (-not (Get-Command go -ErrorAction SilentlyContinue)) {
            throw 'Go is not on PATH, and the utility cannot be built. Nothing was built.'
        }
        # Asked of the Go here as it is: the toolchain go.mod names may be one it would download first.
        $toolchainWas = $env:GOTOOLCHAIN
        $env:GOTOOLCHAIN = 'local'
        try {
            $here = "$(& go env GOOS)-$(& go env GOARCH)"
        } finally {
            $env:GOTOOLCHAIN = $toolchainWas
        }
        if ($here -ne $Platform) {
            throw ("This computer builds the utility for $here, not $Platform. Build it on a computer of that platform with " +
                "deploy\build-utility.ps1, copy the file and its manifest into assets\utility, and publish it with -UtilityVersion. Nothing was built.")
        }
        if (-not (Get-csiGarlandReleaseCompiler -Platform $Platform)) {
            throw "No release compiler is named for $Platform in deploy\BuildVersion.psm1, so no build of it is a release's yet. Nothing was built."
        }
    }
    # The builds are of the sources here, which must be just what the tag holds, as the release checks
    # them later: known now, before a number is spent.
    if (-not $SiteOnly) {
        $TagHolds = & $readTag
        if ($WithFirmware -and (Get-csiGarlandSourceIdentity -RepoRoot $RepoRoot).source_sha256 -ne $TagHolds.Firmware) {
            throw "The firmware's sources here are not what $Tag holds: check out the tag, or tag what is here. Nothing was built."
        }
        if ($WithUtility -and (Get-csiGarlandUtilitySourceDigest -RepoRoot $RepoRoot) -ne $TagHolds.Utility) {
            throw "The utility's sources here are not what $Tag holds: check out the tag, or tag what is here. Nothing was built."
        }
        foreach ($name in 'THIRD_PARTY_NOTICES.md', 'LICENSE') {
            if (-not (& $isTagBlob (Join-Path $RepoRoot $name) $(if ($name -eq 'LICENSE') { $TagHolds.License } else { $TagHolds.Notices }))) {
                throw "$name here is not what $Tag holds: commit it with the tag. Nothing was built."
            }
        }
    }
    if (-not $PSCmdlet.ShouldProcess($What, "Build anew for $Release, spending a build number of each")) {
        return
    }
    # What each child built, as it says it, shown as it goes: the counters are shared, and another build
    # may have moved one since.
    if ($WithFirmware) {
        $said = @(& pwsh -File (Join-Path $PSScriptRoot 'stage-assets.ps1') -NewBuild | ForEach-Object { Write-Host $_; "$_" })
        if ($LASTEXITCODE -ne 0) {
            throw "Building and staging the firmware failed with exit code $LASTEXITCODE. Nothing was released."
        }
        $staged = @($said | ForEach-Object { [regex]::Match($_, '^Staged .*[\\/]csigarland-(\d+\.\d+\.\d+\.\d+)\.ota$') } | Where-Object Success)
        if ($staged.Count -ne 1) {
            throw 'stage-assets.ps1 did not say which update file it staged. Nothing was released.'
        }
        $FirmwareVersion = $staged[0].Groups[1].Value
    }
    if ($WithUtility) {
        $said = @(& pwsh -File (Join-Path $PSScriptRoot 'build-utility.ps1') -Force | ForEach-Object { Write-Host $_; "$_" })
        if ($LASTEXITCODE -ne 0) {
            $staged = if ($WithFirmware) { "; the firmware $FirmwareVersion stays staged, for -FirmwareVersion" } else { '' }
            throw "Building the utility failed with exit code $LASTEXITCODE. Nothing was released$staged."
        }
        $made = [regex]::Match("$($said | Select-Object -Last 1)", "[\\/]csigarland-(\d+\.\d+\.\d+\.\d+)-$([regex]::Escape($Platform))(?:\.exe)?$")
        if (-not $made.Success) {
            throw 'build-utility.ps1 did not say which file it built. Nothing was released.'
        }
        $UtilityVersion = $made.Groups[1].Value
    }
    Write-Host "Built $(@($(if ($WithFirmware) { "firmware $FirmwareVersion" }), $(if ($WithUtility) { "utility $UtilityVersion" }) | Where-Object { $_ }) -join ' and ') for $Release."
} elseif (($WithFirmware -and -not $FirmwareVersion) -or ($WithUtility -and -not $UtilityVersion)) {
    $named = @($(if ($WithFirmware) { '-FirmwareVersion' }), $(if ($WithUtility) { '-UtilityVersion' }) | Where-Object { $_ }) -join ' and '
    throw "Name the staged builds with $named, or build them anew with -BuildNew."
}
$Base = "https://github.com/$Repository/releases/download/$Tag"
$StagedFirmware = Join-Path $RepoRoot 'assets\firmware'
$StagedUtility = Join-Path $RepoRoot 'assets\utility'
$OtaName = "csigarland-$FirmwareVersion.ota"
$UtilityName = "csigarland-$UtilityVersion-$Platform$(if ($Platform.StartsWith('windows-')) { '.exe' })"
$FirmwareNames = @(if ($WithFirmware) { $OtaName, "$OtaName.sha256", "$OtaName.manifest.json", 'csigarland-ota-root.crt', 'THIRD_PARTY_NOTICES.md', 'LICENSE' })
$UtilityNames = @(if ($WithUtility) { $UtilityName, "$UtilityName.manifest.json" })
# What goes out is a copy, made first and checked after: assets/ is shared with every staging, and one
# running meanwhile would replace the notices between their check and the upload.
# Its own for each run, as are the tag's checkout and the notes: two runs at once never share one.
$RunId = [guid]::NewGuid().ToString('N').Substring(0, 8)
$Snapshot = Join-Path $RepoRoot "temp\release-files-$Release-$RunId"
$FirmwareDir = Join-Path $Snapshot 'firmware'
$UtilityDir = Join-Path $Snapshot 'utility'
$Ota = Join-Path $FirmwareDir $OtaName
$Utility = Join-Path $UtilityDir $UtilityName
$Files = @($FirmwareNames | ForEach-Object { Join-Path $FirmwareDir $_ }) + @($UtilityNames | ForEach-Object { Join-Path $UtilityDir $_ })

Import-Module (Join-Path $PSScriptRoot 'SigningPython.psm1') -Force
# A look ahead, -WhatIf, installs nothing into PlatformIO's Python: it says what is missing instead.
if ($WhatIfPreference) {
    $env:CSIGARLAND_NO_INSTALL = '1'
}

foreach ($version in @($(if ($WithFirmware) { $FirmwareVersion }), $(if ($WithUtility) { $UtilityVersion }) | Where-Object { $_ })) {
    if (-not $version.StartsWith("$Release.")) {
        throw "$version is not a build of $Release."
    }
}

$sets = @(@{ From = $StagedFirmware; To = $FirmwareDir; Names = $FirmwareNames }, @{ From = $StagedUtility; To = $UtilityDir; Names = $UtilityNames }) | Where-Object { $_.Names.Count -gt 0 }
foreach ($set in $sets) {
    foreach ($name in $set.Names) {
        if (-not (Test-Path -LiteralPath (Join-Path $set.From $name))) {
            throw "No $(Join-Path $set.From $name). Stage the release first: deploy\stage-assets.ps1 and deploy\build-utility.ps1, or -BuildNew."
        }
    }
}
# Made under -WhatIf too, by .NET, which it does not stop: the checks read it, and it is scratch.
foreach ($set in $sets) {
    [System.IO.Directory]::CreateDirectory($set.To) | Out-Null
    foreach ($name in $set.Names) {
        [System.IO.File]::Copy((Join-Path $set.From $name), (Join-Path $set.To $name), $true)
    }
}

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

$firmwareBuild = $null
$utilityBuild = $null
if ($WithFirmware) {
    $firmwareBuild = Get-Content -Raw -LiteralPath "$Ota.manifest.json" | ConvertFrom-Json
    if ($firmwareBuild.version -ne $FirmwareVersion) {
        throw "The staged firmware is $($firmwareBuild.version), not $FirmwareVersion."
    }
    if ((Get-Sha256 $Ota) -ne $firmwareBuild.ota_sha256) {
        throw "$OtaName is not the file its manifest describes."
    }
    # The checksum file goes out beside it, for anyone to check a download by: it says this file, whole.
    $sidecar = [System.IO.File]::ReadAllText("$Ota.sha256")
    if ($sidecar -cne "$($firmwareBuild.ota_sha256)  $OtaName`n") {
        throw "$OtaName.sha256 does not say $OtaName's SHA-256, $($firmwareBuild.ota_sha256): stage it again."
    }
}
if ($WithUtility) {
    $utilityBuild = Get-Content -Raw -LiteralPath "$Utility.manifest.json" | ConvertFrom-Json
    if ($utilityBuild.version -ne $UtilityVersion) {
        throw "The staged utility is $($utilityBuild.version), not $UtilityVersion."
    }
    if ((Get-Sha256 $Utility) -ne $utilityBuild.sha256) {
        throw "$UtilityName is not the file its manifest describes."
    }
}
if ($WithFirmware) {
    # The notices and the licence beside it are shared by all that was staged, and a later staging writes
    # them again: they go only if they are the ones this firmware was staged with.
    $Notices = Join-Path $FirmwareDir 'THIRD_PARTY_NOTICES.md'
    $License = Join-Path $FirmwareDir 'LICENSE'
    foreach ($field in 'notices_sha256', 'license_sha256', 'notices_checked') {
        if (-not $firmwareBuild.PSObject.Properties[$field]) {
            throw "$FirmwareVersion records no $field, as a staging before it did: stage it again."
        }
    }
    if ($firmwareBuild.notices_checked -ne $true) {
        throw "$FirmwareVersion was staged with THIRD_PARTY_NOTICES.md not checked against what it links, as with no Go: stage it again where Go is."
    }
    if ((Get-Sha256 $Notices) -ne $firmwareBuild.notices_sha256 -or (Get-Sha256 $License) -ne $firmwareBuild.license_sha256) {
        throw "THIRD_PARTY_NOTICES.md or LICENSE in assets\firmware is not what $FirmwareVersion was staged with: stage it again."
    }
    # The root published beside the update file is the one the image holds and the file is signed under,
    # as whoever builds the release again takes it: never a copy left from another build.
    $rootFile = Join-Path $FirmwareDir 'csigarland-ota-root.crt'
    $pem = Get-Content -Raw -LiteralPath $rootFile
    $body = [regex]::Match($pem, '-----BEGIN CERTIFICATE-----(.+?)-----END CERTIFICATE-----', 'Singleline')
    if (-not $body.Success) {
        throw "$rootFile holds no certificate."
    }
    $der = [System.Convert]::FromBase64String(($body.Groups[1].Value -replace '\s', ''))
    $rootSha = [System.Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($der)).ToLowerInvariant()
    if ($rootSha -ne $firmwareBuild.root_sha256) {
        throw "$rootFile is not the root the update file names: its SHA-256 is $rootSha, the manifest's $($firmwareBuild.root_sha256)."
    }
    # And what the signed header names is what the manifest names, which anyone building the release
    # again compares with: a manifest written of another build would hold the other's image and pack.
    $signed = Get-csiGarlandOwnSignature -RepoRoot $RepoRoot -UpdatePath $Ota -Root $rootFile
    if ($null -eq $signed) {
        throw "$Ota is not signed under the root published with it."
    }
    $header = $signed | ConvertFrom-Json
    foreach ($field in 'image_sha256', 'pack_sha256', 'root_sha256') {
        if ("$($header.$field)" -ne "$($firmwareBuild.$field)") {
            throw "$Ota's signed header names $field $($header.$field), its manifest $($firmwareBuild.$field): stage it again."
        }
    }
    # As the staging counted them from the sources the file was built from, not as the working tree has
    # them now: effects added since are not in this file.
    if (-not $firmwareBuild.PSObject.Properties['effects']) {
        throw "$FirmwareVersion records no count of effects, as a staging before it did: stage it again."
    }
    $effects = [int]$firmwareBuild.effects
}
if ($WithUtility) {
    # The file's own word for its platform, where its manifest has one: older ones say it by name only.
    if ($utilityBuild.PSObject.Properties['platform'] -and $utilityBuild.platform -ne $Platform) {
        throw "$UtilityName is a build for $($utilityBuild.platform), not $Platform."
    }
    # The release says its utility builds again to the byte, which holds only for the compiler the guide
    # names for its platform: one built with another is no release's.
    $releaseCompiler = Get-csiGarlandReleaseCompiler -Platform $Platform
    if (-not $releaseCompiler) {
        throw "No release compiler is named for $Platform in deploy\BuildVersion.psm1, so no build of it is a release's yet: name the one its releases are built with, and how to build it again in docs\guides\rebuild.md."
    }
    if ($utilityBuild.compiler -ne $releaseCompiler) {
        throw "The utility was built with $($utilityBuild.compiler), not $releaseCompiler, so no one could build it again to the byte: build it with that one."
    }
    # And cgo with it, CC and CXX both, and with no flags of anyone's but Go's own and the script's.
    foreach ($field in 'cc', 'cxx', 'cgo_flags') {
        if (-not $utilityBuild.PSObject.Properties[$field]) {
            throw "The utility $UtilityVersion records no $field, as a build before it did: build it again."
        }
    }
    $wanted = Get-csiGarlandCompilerBuild -Line $releaseCompiler
    if ((Get-csiGarlandCompilerBuild -Line $utilityBuild.cc) -ne $wanted -or (Get-csiGarlandCompilerBuild -Line $utilityBuild.cxx) -ne $wanted) {
        throw "cgo built the utility with $($utilityBuild.cc) and $($utilityBuild.cxx), not ${releaseCompiler}: build it with that one, CC and CXX unset, with deploy\build-utility.ps1 -Force."
    }
    # And with the bare commands: arguments in CC or CXX reach the compiler past every flag checked here.
    foreach ($field in 'cc_command', 'cxx_command') {
        if (-not $utilityBuild.PSObject.Properties[$field]) {
            throw "The utility $UtilityVersion records no $field, as a build before it did: build it again."
        }
        if (@(Split-csiGarlandCommand -Command $utilityBuild.$field).Count -ne 1) {
            throw "cgo built the utility with '$($utilityBuild.$field)', arguments and all: build it again with CC and CXX unset, with deploy\build-utility.ps1 -Force."
        }
    }
    # With no GOFLAGS of a shell's, and no workspace of a folder above, which change what is built.
    foreach ($field in 'goflags', 'gowork') {
        if (-not $utilityBuild.PSObject.Properties[$field]) {
            throw "The utility $UtilityVersion records no $field, as a build before it did: build it again with -Force."
        }
    }
    if ($utilityBuild.goflags -ne '' -or $utilityBuild.gowork -ne 'off') {
        throw "The utility was built with GOFLAGS '$($utilityBuild.goflags)' and GOWORK '$($utilityBuild.gowork)': build it again with deploy\build-utility.ps1 -Force."
    }
    # Nor with an experiment of Go's turned on or off, which changes the runtime the file holds.
    if (-not $utilityBuild.PSObject.Properties['goexperiment']) {
        throw "The utility $UtilityVersion records no goexperiment, as a build before it did: build it again with deploy\build-utility.ps1 -Force."
    }
    if (-not $utilityBuild.PSObject.Properties['gofips140'] -or $utilityBuild.gofips140 -ne 'off') {
        throw "The utility $UtilityVersion was built with GOFIPS140 '$(if ($utilityBuild.PSObject.Properties['gofips140']) { $utilityBuild.gofips140 })', not off: unset it, go env -u GOFIPS140, and build it again with deploy\build-utility.ps1 -Force."
    }
    if ($utilityBuild.goexperiment -ne '') {
        throw "The utility was built with GOEXPERIMENT '$($utilityBuild.goexperiment)': unset it, go env -u GOEXPERIMENT, and build it again with deploy\build-utility.ps1 -Force."
    }
    # Built for the x86-64 level any 64-bit PC runs: a higher one stops at start on an older processor.
    if ($Platform -like '*-amd64' -and (-not $utilityBuild.PSObject.Properties['goamd64'] -or $utilityBuild.goamd64 -ne (Get-csiGarlandReleaseAmd64))) {
        throw "The utility was built for x86-64 level '$(if ($utilityBuild.PSObject.Properties['goamd64']) { $utilityBuild.goamd64 })', not $(Get-csiGarlandReleaseAmd64): build it again with deploy\build-utility.ps1."
    }
    foreach ($flag in (Get-csiGarlandReleaseCgoFlags).GetEnumerator()) {
        if ("$($utilityBuild.cgo_flags.($flag.Key))" -ne $flag.Value) {
            throw "The utility was built with $($flag.Key) '$($utilityBuild.cgo_flags.($flag.Key))', not Go's own '$($flag.Value)': build it again with it unset, with deploy\build-utility.ps1 -Force."
        }
    }
    if ($utilityBuild.PSObject.Properties['profile'] -and $utilityBuild.profile -ne 'Release') {
        throw "The utility $UtilityVersion is a $($utilityBuild.profile) build: a release takes a Release one."
    }
}
# The owner's day, as the changelog dates a release.
$today = (Get-Date).ToString('yyyy-MM-dd')
$sizeOf = { param($Path) '{0:N1} MB' -f ((Get-Item -LiteralPath $Path).Length / 1MB) }

# What the site lists before this run, kept: a release is added to the lists, newest first, never put in
# place of those before it, and what this run does not publish stays as it was.
$sitePath = Join-Path $RepoRoot 'site\downloads'
$readJson = {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }
    return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
}
$newerFirst = {
    param($Items, [string]$DateField)
    @($Items | Sort-Object -Property @{ Expression = { "$($_.$DateField)" }; Descending = $true }, @{ Expression = {
                $parts = @("$($_.version)" -split '[.-]' | ForEach-Object { $n = 0; [void][int]::TryParse($_, [ref]$n); $n })
                ($parts | ForEach-Object { '{0:D6}' -f $_ }) -join '.'
            }; Descending = $true
        })
}

# What the utility's Firmware page lists: each release's update file, by its address, length and hash.
# Written only by a run that publishes the firmware.
$catalog = $null
if ($WithFirmware) {
    $catalog = [ordered]@{
        format   = 'csigarland.firmware-catalog/1'
        firmware = @([ordered]@{
                version   = $FirmwareVersion
                file      = Split-Path -Leaf $Ota
                url       = "$Base/$(Split-Path -Leaf $Ota)"
                sha256    = $firmwareBuild.ota_sha256
                bytes     = (Get-Item -LiteralPath $Ota).Length
                effects   = $effects
                published = $today
            })
    }
    $catalogBefore = & $readJson (Join-Path $sitePath 'firmware-catalog-v1.json')
    if ($catalogBefore -and $catalogBefore.PSObject.Properties['firmware']) {
        $kept = @($catalogBefore.firmware | Where-Object { $_.version -ne $FirmwareVersion })
        $catalog.firmware = @(& $newerFirst (@($catalog.firmware) + $kept) 'published')
    }
}

# What the Downloads page lists: each product, with a build for each platform, its release, the day it
# came out, and from after 1.0.0 a link to its release notes on GitHub. A platform not built yet is
# listed as not yet, until a build of its family, as Linux for Linux x64, takes its place.
$releaseNotes = if ([version]$Release -gt [version]'1.0.0') { "https://github.com/$Repository/releases/tag/$Tag" } else { $null }
$build = {
    param($Platform, $Version, $File, $Sha256, $Size)
    [ordered]@{ platform = $Platform; available = $true; version = $Version; release = $Release; released = $today; notes = $releaseNotes
        url = "$Base/$File"; sha256 = $Sha256; size = $Size
    }
}
$products = @(
    [ordered]@{ id = 'firmware'; name = 'Firmware and effects update'
        about = "The update file for a garland whose firmware holds the project's root, signed under it: install it from the portal's Firmware page or the utility's catalog."
        new = @(if ($WithFirmware) { & $build 'ESP32-C6-LCD-1.3' $FirmwareVersion (Split-Path -Leaf $Ota) $firmwareBuild.ota_sha256 (& $sizeOf $Ota) })
        waiting = @()
    },
    [ordered]@{ id = 'utility'; name = 'csiGarland utility'
        about = 'The program for your computer: control, tuning with a live preview, updates and the log.'
        new = @(if ($WithUtility) { & $build $PlatformName $UtilityVersion (Split-Path -Leaf $Utility) $utilityBuild.sha256 (& $sizeOf $Utility) })
        waiting = @('Windows x64', 'Linux', 'macOS')
    },
    [ordered]@{ id = 'root-certificate'; name = 'Root certificate of the release'
        about = 'To build the release again and compare it to the byte.'
        new = @(if ($WithFirmware) { & $build 'Any' $FirmwareVersion 'csigarland-ota-root.crt' (Get-Sha256 (Join-Path $FirmwareDir 'csigarland-ota-root.crt')) $null })
        waiting = @()
    }
)
# A product's builds as the page shows them: the platforms in the order Windows, Linux, macOS, and each
# platform's releases newest first, a platform with none at the end of its family as not yet.
$orderBuilds = {
    param($Builds)
    $released = @($Builds | Where-Object { $_.available })
    $waiting = @($Builds | Where-Object { -not $_.available })
    return @(@(& $newerFirst $released 'released') + $waiting | Sort-Object -Stable -Property @{ Expression = { [Math]::Max(0, @('Windows', 'Linux', 'macOS').IndexOf(("$($_.platform)" -split ' ')[0])) } })
}
$before = & $readJson (Join-Path $sitePath 'manifest.json')
$listed = foreach ($product in $products) {
    $earlier = @(if ($before -and $before.PSObject.Properties['products']) { $before.products | Where-Object { $_.id -eq $product.id } | ForEach-Object { $_.builds } })
    $released = @($product.new)
    # A build of this release for a platform takes the place of one listed for it.
    $kept = @($earlier | Where-Object {
            $old = $_
            $old.available -and -not ($released | Where-Object { $_.platform -eq $old.platform -and $_.release -eq $old.release })
        })
    $builds = @(& $newerFirst (@($released) + @($kept)) 'released')
    $out = @($builds | ForEach-Object { $_.platform })
    $waiting = @(@($product.waiting) + @($earlier | Where-Object { -not $_.available } | ForEach-Object { $_.platform }) | Select-Object -Unique | Where-Object {
            $platform = $_
            -not ($out -contains $platform) -and -not ($out | Where-Object { $_.StartsWith("$platform ") })
        })
    foreach ($platform in $waiting) {
        $builds += [ordered]@{ platform = $platform; available = $false }
    }
    $builds = @(& $orderBuilds $builds)
    if ($builds.Count -gt 0) {
        [ordered]@{ id = $product.id; name = $product.name; about = $product.about; builds = $builds }
    }
}
$manifest = [ordered]@{
    format   = 'csigarland.downloads/2'
    updated  = $today
    source   = "https://github.com/$Repository"
    release  = "https://github.com/$Repository/releases/tag/$Tag"
    products = @($listed)
}
$utf8 = [System.Text.UTF8Encoding]::new($false)
# Under internal/catalog's 64 KB, with room for what JSON's spacing may add.
$CatalogBytes = 48 * 1024
# The files written whole beside the others first, then put in their places one after the other, so a
# failure leaves each as it was, or all new. A run stopped before this is taken up by -SiteFromRelease.
$writeSite = {
    param([Parameter(Mandatory = $true)][string]$Downloads)
    [System.IO.Directory]::CreateDirectory($Downloads) | Out-Null
    $files = [ordered]@{ 'manifest.json' = ($manifest | ConvertTo-Json -Depth 6) + "`n" }
    if ($catalog) {
        # The utility of 1.0.0 reads a catalog of 64 KB at most, and refuses one larger whole, newest
        # release and all: the oldest releases go from it first, the Downloads page keeping them.
        $text = ($catalog | ConvertTo-Json -Depth 5) + "`n"
        while ($utf8.GetByteCount($text) -gt $CatalogBytes -and @($catalog.firmware).Count -gt 1) {
            $catalog.firmware = @($catalog.firmware | Select-Object -SkipLast 1)
            $text = ($catalog | ConvertTo-Json -Depth 5) + "`n"
        }
        $files['firmware-catalog-v1.json'] = $text
    }
    foreach ($file in $files.GetEnumerator()) {
        [System.IO.File]::WriteAllText((Join-Path $Downloads "$($file.Key).$RunId.new"), $file.Value, $utf8)
    }
    foreach ($file in $files.GetEnumerator()) {
        [System.IO.File]::Move((Join-Path $Downloads "$($file.Key).$RunId.new"), (Join-Path $Downloads $file.Key), $true)
    }
    Write-Host "Wrote $(@($files.Keys) -join ' and ') in $Downloads for $What of $Tag."
}
# The release as GitHub holds it now, or $null for none: its files with their lengths, SHA-256 and the
# moment each came, and its notes.
$readRelease = {
    $said = & gh api "repos/$Repository/releases/tags/$Tag" 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $said) {
        return $null
    }
    return ($said -join "`n") | ConvertFrom-Json
}
# A file's SHA-256 as GitHub holds it: the digest GitHub keeps of it, or of its bytes downloaded, for a
# file put there before GitHub kept one. Under temp\, this run's own.
$remoteSha256 = {
    param($Asset)
    if ($Asset.PSObject.Properties['digest'] -and "$($Asset.digest)" -match '^sha256:([0-9a-f]{64})$') {
        return $Matches[1]
    }
    $into = Join-Path $RepoRoot "temp\release-download-$Release-$RunId"
    [System.IO.Directory]::CreateDirectory($into) | Out-Null
    & gh release download $Tag --repo $Repository --pattern $Asset.name --dir $into --clobber
    if ($LASTEXITCODE -ne 0) {
        throw "Could not read $($Asset.name) of the release $Tag back from GitHub to compare it."
    }
    return Get-Sha256 (Join-Path $into $Asset.name)
}
# Whether the release holds this file, byte for byte: $true, $false for none of that name, or a refusal
# for one of other bytes, as a released file is never replaced and the site would name another's hash.
$releaseHolds = {
    param($Held, [string]$File)
    $name = Split-Path -Leaf $File
    $asset = @(if ($Held) { $Held.assets | Where-Object { $_.name -eq $name } })
    if ($asset.Count -eq 0) {
        return $false
    }
    $there = & $remoteSha256 $asset[0]
    if ($there -ne (Get-Sha256 $File)) {
        throw "The release $Tag holds another $name already, SHA-256 $there against $(Get-Sha256 $File) here: a released file is never replaced, and the site is left as it is."
    }
    return $true
}
# The day each of this run's files came to the release, as GitHub dates its upload, once the release
# holds every one of them as staged here: a utility added days after the firmware is dated its own day.
$filesDays = {
    $held = & $readRelease
    if (-not $held) {
        throw "GitHub has no release $Tag to write the site for."
    }
    $days = @{}
    foreach ($file in $Files) {
        if (-not (& $releaseHolds $held $file)) {
            throw "The release $Tag on GitHub does not hold $(Split-Path -Leaf $file): the site is left as it is."
        }
        $asset = @($held.assets | Where-Object { $_.name -eq (Split-Path -Leaf $file) })[0]
        $days[$asset.name] = ([datetime]$asset.updated_at).ToUniversalTime().ToString('yyyy-MM-dd')
    }
    return $days
}
# Only this run's builds: those listed before keep their days. Each takes the day of its own file.
$dateSite = {
    param([hashtable]$Days)
    foreach ($product in $manifest.products) {
        foreach ($entry in $product.builds | Where-Object { $_ -is [System.Collections.IDictionary] -and $_.Contains('release') -and $_['release'] -eq $Release }) {
            $file = Split-Path -Leaf ([uri]$entry['url']).AbsolutePath
            $entry['released'] = if ($Days.ContainsKey($file)) { $Days[$file] } elseif ($Days.ContainsKey($OtaName)) { $Days[$OtaName] } else { $entry['released'] }
        }
    }
    if ($catalog) {
        foreach ($entry in $catalog.firmware | Where-Object { $_ -is [System.Collections.IDictionary] -and $_['version'] -eq $FirmwareVersion }) {
            $entry['published'] = $Days[$OtaName]
        }
        # Sorted again by the days GitHub gives, before the catalog is cut to size from its oldest.
        $catalog.firmware = @(& $newerFirst @($catalog.firmware) 'published')
    }
    foreach ($product in $manifest.products) {
        $product['builds'] = @(& $orderBuilds $product['builds'])
    }
    $manifest.updated = @($Days.Values | Sort-Object -Descending)[0]
}

# Before the tag, to see only: the site lists a release once it is made, by the files written then, so a
# release refused, or one made another day, leaves nothing there pointing at nothing or dated wrong.
if ($SiteOnly) {
    $preview = Join-Path $RepoRoot "temp\site-preview-$Release"
    if ($PSCmdlet.ShouldProcess($preview, "Write a preview of the site's downloads for $What of $Tag")) {
        & $writeSite $preview
    }
    [System.IO.Directory]::Delete($Snapshot, $true)
    return
}

# The release says each file builds again from the tag, so each was built from just what the tag
# holds: its sources' digests, worked out from the tag's tree, are the manifests'.
if (-not $TagHolds) {
    $TagHolds = & $readTag
}
$commit = $TagHolds.Commit
$releaseNotesText = $TagHolds.Notes
foreach ($built in @($firmwareBuild, $utilityBuild) | Where-Object { $_ }) {
    if (-not $built.PSObject.Properties['source_format'] -or $built.source_format -ne (Get-csiGarlandSourceFormat)) {
        throw "$($built.version) records its sources in another form than this script works out: build it again from the tag."
    }
}
if ($WithFirmware) {
    if ($TagHolds.Firmware -ne $firmwareBuild.source_sha256) {
        throw "The firmware was built from other sources than $Tag holds ($($firmwareBuild.source_sha256) against $($TagHolds.Firmware)): build it again from the tag."
    }
    if (-not (& $isTagBlob $Notices $TagHolds.Notices) -or -not (& $isTagBlob $License $TagHolds.License)) {
        throw "THIRD_PARTY_NOTICES.md or LICENSE staged with $FirmwareVersion is not what $Tag holds: commit them with the tag, or stage it again."
    }
    if ($TagHolds.Effects -ne $effects) {
        throw "$FirmwareVersion holds $effects effects, and $Tag's catalog $($TagHolds.Effects): build it again from the tag."
    }
}
if ($WithUtility -and $TagHolds.Utility -ne $utilityBuild.source_sha256) {
    throw "The utility was built from other sources than $Tag holds ($($utilityBuild.source_sha256) against $($TagHolds.Utility)): build it again from the tag."
}
# The utility owes the notices of what it holds, as its build checked them, whether it goes out alone or with
# the firmware: the texts it goes out with are those its build checked. Alone, it takes them and the licence
# as the tag holds them; with the firmware, it shares the firmware's.
if ($WithUtility) {
    foreach ($field in 'notices_sha256', 'license_sha256', 'notices_checked') {
        if (-not $utilityBuild.PSObject.Properties[$field]) {
            throw "$UtilityVersion records no $field, as a build before it did: build it again."
        }
    }
    if ($utilityBuild.notices_checked -ne $true) {
        throw "$UtilityVersion was built with THIRD_PARTY_NOTICES.md not found to name what it holds: bring the notices up to date, commit them, and build it again."
    }
}
if ($WithUtility -and $WithFirmware) {
    if ((Get-Sha256 $Notices) -ne $utilityBuild.notices_sha256 -or (Get-Sha256 $License) -ne $utilityBuild.license_sha256) {
        throw "THIRD_PARTY_NOTICES.md or LICENSE going out with $FirmwareVersion is not what $UtilityVersion was built with: build them from one tag."
    }
}
if (-not $WithFirmware) {
    foreach ($name in 'THIRD_PARTY_NOTICES.md', 'LICENSE') {
        $copy = Join-Path $UtilityDir $name
        [System.IO.File]::Copy((Join-Path $RepoRoot $name), $copy, $true)
        if (-not (& $isTagBlob $copy $(if ($name -eq 'LICENSE') { $TagHolds.License } else { $TagHolds.Notices }))) {
            throw "$name here is not what $Tag holds: commit it with the tag."
        }
        if ((Get-Sha256 $copy) -ne $(if ($name -eq 'LICENSE') { $utilityBuild.license_sha256 } else { $utilityBuild.notices_sha256 })) {
            throw "$name here is not the one $UtilityVersion was built with: build it again."
        }
        $Files += $copy
    }
}

# What GitHub holds of the release already, if anything: a later run adds to it. A file it holds already,
# byte for byte, as a run stopped halfway would have left it, is not sent again.
$existing = & $readRelease
$upload = @(foreach ($file in $Files) {
        if (-not (& $releaseHolds $existing $file)) {
            $file
        }
    })

# The notes' table of files: those the release holds, as its notes list them, and this run's, each said
# by its name, as the notes of 1.0.0 have no column for it. Read again from GitHub right before they are
# written, so that a run that added a file since keeps its line.
$describe = {
    param([string]$File)
    $ota = [regex]::Match($File, '^csigarland-(\d+\.\d+\.\d+\.\d+)\.ota$')
    if ($ota.Success) {
        return "Firmware $($ota.Groups[1].Value) and its effects, the update file"
    }
    $program = [regex]::Match($File, '^csigarland-(\d+\.\d+\.\d+\.\d+)-([a-z0-9]+-[a-z0-9]+)(?:\.exe)?$')
    if ($program.Success -and $UtilityPlatforms.Contains($program.Groups[2].Value)) {
        return "The csiGarland utility $($program.Groups[1].Value) for $($UtilityPlatforms[$program.Groups[2].Value])"
    }
    return ''
}
$notes = Join-Path $RepoRoot "temp\release-notes-$Release-$RunId.md"
$writeNotes = {
    param($Held)
    $rows = [ordered]@{}
    foreach ($asset in @(if ($Held) { $Held.assets })) {
        $what = & $describe $asset.name
        if ($what) {
            $rows[$asset.name] = "| ``$($asset.name)`` | $what | ``$(& $remoteSha256 $asset)`` |"
        }
    }
    if ($WithFirmware) {
        $rows[$OtaName] = "| ``$OtaName`` | $(& $describe $OtaName) | ``$($firmwareBuild.ota_sha256)`` |"
    }
    if ($WithUtility) {
        $rows[$UtilityName] = "| ``$UtilityName`` | $(& $describe $UtilityName) | ``$($utilityBuild.sha256)`` |"
    }
    $signedSaid = if (@($rows.Keys | Where-Object { $_ -like '*.ota' }).Count -gt 0) { 'The update file is signed under the root in `csigarland-ota-root.crt`. ' } else { '' }
    New-Item -ItemType Directory -Path (Split-Path -Parent $notes) -Force | Out-Null
    [System.IO.File]::WriteAllText($notes, @"
$releaseNotesText

The full list: [CHANGELOG.md](https://github.com/$Repository/blob/$Tag/CHANGELOG.md).

## Files

csiGarland $Release, built from commit $commit.

| File | What | SHA-256 |
|---|---|---|
$(@($rows.Values) -join "`n")

${signedSaid}Each file builds again to the byte from this tag's sources: see [Building a Release Again](https://github.com/$Repository/blob/$Tag/docs/guides/rebuild.md).
"@, $utf8)
    Write-Host "The release's notes are in $notes."
}
& $writeNotes $existing

if ($SiteFromRelease) {
    if ($PSCmdlet.ShouldProcess((Join-Path $RepoRoot 'site\downloads'), "Write the site's downloads for $What of the release $Tag made already")) {
        & $dateSite (& $filesDays)
        & $writeSite (Join-Path $RepoRoot 'site\downloads')
    }
    [System.IO.Directory]::Delete($Snapshot, $true)
    return
}
if ($existing) {
    if ($PSCmdlet.ShouldProcess($Repository, "Add $($upload.Count) files of $What to the release $Tag, and write its notes again")) {
        if ($upload.Count -gt 0) {
            & gh release upload $Tag @upload --repo $Repository
            if ($LASTEXITCODE -ne 0) {
                throw "gh release upload failed with exit code $LASTEXITCODE. Run this again: what was sent whole is not sent twice."
            }
        } else {
            Write-Host "The release $Tag holds all these files already."
        }
        # Written from what the release holds then, and read back: a run elsewhere that wrote its own in
        # between, of the files it held before this one's came, is written over with all of them.
        $whole = $false
        for ($try = 0; $try -lt 3 -and -not $whole; $try++) {
            & $writeNotes (& $readRelease)
            & gh release edit $Tag --repo $Repository --notes-file $notes
            if ($LASTEXITCODE -ne 0) {
                throw "gh release edit failed with exit code ${LASTEXITCODE}: the files are added, the notes are as they were. Run this again to write them."
            }
            $now = & $readRelease
            $whole = $now -and @($now.assets | Where-Object { & $describe $_.name } | Where-Object { -not "$($now.body)".Contains("``$($_.name)``") }).Count -eq 0
        }
        if (-not $whole) {
            Write-Warning "The notes of $Tag do not list every file it holds: another run writes them too. Run this again once it is done."
        }
        Write-Host "Added to https://github.com/$Repository/releases/tag/$Tag"
        & $dateSite (& $filesDays)
        & $writeSite (Join-Path $RepoRoot 'site\downloads')
    }
} elseif ($PSCmdlet.ShouldProcess($Repository, "Create the release $Tag with $($Files.Count) files of $What")) {
    & gh release create $Tag @Files --repo $Repository --title "csiGarland $Release" --notes-file $notes --verify-tag
    if ($LASTEXITCODE -ne 0) {
        throw "gh release create failed with exit code $LASTEXITCODE."
    }
    Write-Host "Released https://github.com/$Repository/releases/tag/$Tag"
    & $dateSite (& $filesDays)
    & $writeSite (Join-Path $RepoRoot 'site\downloads')
}
# The copies are this run's own, and done with: the staged files stay in assets\.
[System.IO.Directory]::Delete($Snapshot, $true)
$downloaded = Join-Path $RepoRoot "temp\release-download-$Release-$RunId"
if (Test-Path -LiteralPath $downloaded) {
    [System.IO.Directory]::Delete($downloaded, $true)
}