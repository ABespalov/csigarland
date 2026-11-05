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

# The lock of one kind of build in this checkout, firmware or utility, as a file under temp\ held open
# for no one else: a build takes its number, builds, checks, and gives the number back if it failed, all
# under it, so two never take one number, and one never gives back another's. Held until disposed, or
# until the process ends. One held by another waits up to WaitSeconds, then fails.
function Lock-csiGarlandBuild {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][ValidateSet('firmware', 'utility', 'publish')][string]$Kind,
        [int]$WaitSeconds = 0
    )

    $path = Join-Path $RepoRoot "temp\build-$Kind.lock"
    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $path)) | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds($WaitSeconds)
    while ($true) {
        try {
            return [System.IO.File]::Open($path, [System.IO.FileMode]::OpenOrCreate, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
        } catch [System.IO.IOException] {
            if ([DateTime]::UtcNow -ge $deadline) {
                throw "Another $(if ($Kind -eq 'publish') { 'publish-release.ps1' } else { "$Kind build" }) of this checkout runs, as $path is held: wait for it to end, then run this again."
            }
            Start-Sleep -Milliseconds 500
        }
    }
}

# Where PlatformIO keeps its packages and Pythons: PLATFORMIO_CORE_DIR if it is set, as PlatformIO takes it,
# else .platformio in the home folder.
function Get-csiGarlandPlatformIOCore {
    if ($env:PLATFORMIO_CORE_DIR) {
        return $env:PLATFORMIO_CORE_DIR
    }
    return Join-Path $HOME '.platformio'
}

# Where PlatformIO keeps its packages: in its core, unless PLATFORMIO_PACKAGES_DIR names another folder.
function Get-csiGarlandPlatformIOPackages {
    if ($env:PLATFORMIO_PACKAGES_DIR) {
        return $env:PLATFORMIO_PACKAGES_DIR
    }
    return Join-Path (Get-csiGarlandPlatformIOCore) 'packages'
}

# A build that failed spends no number: the counter goes back to Before, but only while it holds the
# one number that build took, so a counter another build moved meanwhile is left as it is. Written as
# firmware_build_version.py and build-utility.ps1 write it. Under the build's lock: the caller's, when it
# holds it, else taken here for the look and the write. $true when it went back.
function Undo-csiGarlandBuildNumber {
    param(
        [Parameter(Mandatory = $true)][string]$CounterPath,
        [Parameter(Mandatory = $true)][int]$Before,
        [string]$RepoRoot = '',
        [ValidateSet('', 'firmware', 'utility')][string]$Kind = ''
    )

    $lock = if ($RepoRoot -and $Kind) { Lock-csiGarlandBuild -RepoRoot $RepoRoot -Kind $Kind -WaitSeconds 120 } else { $null }
    try {
        try {
            $now = Get-csiGarlandBuildNumber -CounterPath $CounterPath
        } catch {
            return $false
        }
        if ($now -ne $Before + 1) {
            return $false
        }
        [System.IO.File]::WriteAllText($CounterPath, "$Before`n", [System.Text.UTF8Encoding]::new($false))
        return $true
    } finally {
        if ($lock) {
            $lock.Dispose()
        }
    }
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
    # A git that failed would say clean and list nothing, and stamp a build with an empty digest. A build
    # counter moved by the build before is no change to the sources.
    $changes = @(& git -C $RepoRoot status --porcelain -- . @($BuildCounters | ForEach-Object { ":(exclude)$_" }))
    if ($LASTEXITCODE -ne 0) {
        throw "git status failed with exit code $LASTEXITCODE in $RepoRoot."
    }
    return [pscustomobject]@{
        commit        = if ($commitKnown) { $commit } else { $null }
        dirty         = $changes.Count -gt 0
        source_sha256 = Get-csiGarlandTreeDigest -RepoRoot $RepoRoot -Paths @('firmware/app')
        source_format = $SourceFormat
    }
}

# How source_sha256 is worked out, which a manifest records beside it. 1.0.0's manifests have none,
# and their digest is of the files' bytes as they lay, which the build itself changes.
$SourceFormat = 'raw-blobs/1'

# What the firmware's build writes out again, with the line endings of the machine it runs on: taken as
# git would store it, as nothing is built from it.
$RewrittenByBuild = @('firmware/app/dependencies.lock')

# The build counters, which git keeps so that a clone on another machine goes on from the last number:
# each build moves its own, so neither is a source of what is built. The number a build takes is in
# its version, and a release built again is given its number outright.
$BuildCounters = @('firmware/app/.csigarland_build_counter', 'cmd/csigarland/.csigarland_build_counter')

# The files git would keep under Paths, each by the blob of its bytes as they lie: what a build
# compiles or embeds, as go:embed takes an effect's file, is those bytes, so a file saved with other
# line endings is another source. A checkout of a tag lies as git stores it (.gitattributes asks for LF),
# so it gives the digest of a build made from what the tag holds. Only RewrittenByBuild is taken as git
# would store it. BuildCounters are left out, so a digest is what it was when git ignored them.
function Get-csiGarlandTreeDigest {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string[]]$Paths,
        # A commit or tag, whose tree is read as git holds it, with no checkout: the digest a checkout of it
        # gives, as it lies as git stores it. Empty for the files as they lie here.
        [string]$Revision = ''
    )

    if ($Revision) {
        $listed = (& git -C $RepoRoot ls-tree -r -z --full-tree $Revision -- @Paths) -join ''
        if ($LASTEXITCODE -ne 0) {
            throw "git ls-tree failed with exit code $LASTEXITCODE for $Revision in $RepoRoot."
        }
        $held = @($listed -split "`0" | Where-Object { $_ } | ForEach-Object {
                $meta, $path = $_ -split "`t", 2
                $mode, $type, $blob = $meta -split ' '
                if ($type -eq 'blob' -and $path -cnotin $BuildCounters) { [pscustomobject]@{ Path = $path; Blob = $blob } }
            } | Sort-Object -Property Path -Culture '' -CaseSensitive)
        if ($held.Count -eq 0) {
            throw "$Revision holds no files of $($Paths -join ', ')."
        }
        $tree = [System.Security.Cryptography.IncrementalHash]::CreateHash('SHA256')
        try {
            foreach ($file in $held) {
                $tree.AppendData([System.Text.Encoding]::UTF8.GetBytes("$($file.Path) $($file.Blob)`n"))
            }
            return [System.Convert]::ToHexString($tree.GetHashAndReset()).ToLowerInvariant()
        } finally {
            $tree.Dispose()
        }
    }
    $files = @(& git -C $RepoRoot ls-files --cached --others --exclude-standard -- @Paths | Sort-Object -Culture '' -CaseSensitive |
            Where-Object { $_ -cnotin $BuildCounters -and (Test-Path -LiteralPath (Join-Path $RepoRoot $_) -PathType Leaf) })
    if ($LASTEXITCODE -ne 0 -or $files.Count -eq 0) {
        throw "git ls-files listed no files of $($Paths -join ', ') in $RepoRoot (exit code $LASTEXITCODE)."
    }
    $blobs = @($files | & git -C $RepoRoot hash-object --no-filters --stdin-paths)
    if ($LASTEXITCODE -ne 0 -or $blobs.Count -ne $files.Count) {
        throw "git hash-object failed with exit code $LASTEXITCODE in $RepoRoot."
    }
    for ($i = 0; $i -lt $files.Count; $i++) {
        if ($files[$i] -cin $RewrittenByBuild) {
            $blobs[$i] = & git -C $RepoRoot hash-object -- $files[$i]
            if ($LASTEXITCODE -ne 0) {
                throw "git hash-object failed with exit code $LASTEXITCODE for $($files[$i])."
            }
        }
    }
    $sha = [System.Security.Cryptography.IncrementalHash]::CreateHash('SHA256')
    try {
        for ($i = 0; $i -lt $files.Count; $i++) {
            $sha.AppendData([System.Text.Encoding]::UTF8.GetBytes("$($files[$i]) $($blobs[$i])`n"))
        }
        return [System.Convert]::ToHexString($sha.GetHashAndReset()).ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

function Get-csiGarlandSourceFormat { return $SourceFormat }

# The C++ compiler the utility of a release is built with, for each platform it is released for, as
# docs/guides/rebuild.md names it: another builds, but not the same file, and a release says its file
# builds again to the byte. A platform with none named here has no release yet: $null. deploy/lib/csg_tool.py
# names the same, for the bash scripts.
$ReleaseCompilers = @{
    'windows-amd64' = 'gcc.exe (MinGW-W64 x86_64-ucrt-posix-seh, built by Brecht Sanders, r2) 16.1.0'
    # Ubuntu 24.04's, as the container of docs/guides/utility.md has it on Windows and Ubuntu itself on Linux.
    'linux-amd64'   = 'gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0'
}
function Get-csiGarlandReleaseCompiler {
    param([string]$Platform = 'windows-amd64')
    return $ReleaseCompilers[$Platform]
}

# The platforms a release's utility may be built for, as Go names them and the file is named, and as
# the site lists them: each a family, the site's heading, and its processor.
$UtilityPlatforms = [ordered]@{
    'windows-amd64' = 'Windows x64'
    'linux-amd64'   = 'Linux x64'
    'linux-arm64'   = 'Linux ARM64'
    'darwin-amd64'  = 'macOS Intel'
    'darwin-arm64'  = 'macOS Apple silicon'
}
function Get-csiGarlandUtilityPlatforms { return $UtilityPlatforms }

# A compiler's first line of --version, less the name it was called by, gcc.exe or g++.exe: both of one
# toolchain then say the same.
function Get-csiGarlandCompilerBuild {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Line)
    return ($Line.Trim() -replace '^\S+\s+', '')
}

# The cgo flags Go itself gives when none are set, which a release is built with, beside the ones
# deploy/build-utility.ps1 adds of its own: any other changes the file.
$ReleaseCgoFlags = [ordered]@{ CGO_CFLAGS = '-O2 -g'; CGO_CPPFLAGS = ''; CGO_CXXFLAGS = '-O2 -g'; CGO_LDFLAGS = '-O2 -g' }
function Get-csiGarlandReleaseCgoFlags { return $ReleaseCgoFlags }

# The x86-64 level a release's utility is built for: v1 runs on any 64-bit PC, where a higher one stops
# at start on a processor without its instructions, under the same file name.
$ReleaseAmd64 = 'v1'
function Get-csiGarlandReleaseAmd64 { return $ReleaseAmd64 }

# A command as Go reads CC and CXX: a program and its arguments, split at spaces, quotes keeping theirs.
function Split-csiGarlandCommand {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Command)
    return @([regex]::Matches($Command, '"[^"]*"|''[^'']*''|\S+') | ForEach-Object { $_.Value.Trim('"', "'") })
}

# The PlatformIO environment of a profile, which is also its folder under firmware/app/.pio/build.
function Get-csiGarlandFirmwareEnvironment {
    param([Parameter(Mandatory = $true)][ValidateSet('Release', 'Dev')][string]$BuildProfile)
    return ($BuildProfile -eq 'Release') ? 'esp32c6-release' : 'esp32c6'
}

# Everything that goes into the utility, the firmware's engine and effects among it: the paths git would
# keep and their bytes. deploy/build-utility.ps1 records it, and deploy/publish-release.ps1 holds a
# release's tag to it.
function Get-csiGarlandUtilitySourceDigest {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [string]$Revision = ''
    )

    # This module with the script, as it sets the engine's flags and what the script checks the build by; and
    # what checks its notices, the script and the licence texts it reads, as its manifest says they held. The
    # bash twins of all of these too, as deploy/lib/csg_tool.py lists them the same: a path a tag does not
    # hold adds nothing, so the digests of releases before them stay as they were.
    return Get-csiGarlandTreeDigest -RepoRoot $RepoRoot -Revision $Revision -Paths @('cmd', 'internal', 'firmware/app/effects', 'firmware/app/src', 'go.mod', 'go.sum', 'deploy/build-utility.ps1', 'deploy/BuildVersion.psm1', 'deploy/new-third-party-notices.ps1', 'deploy/licences', 'deploy/build-utility.sh', 'deploy/new-third-party-notices.sh', 'deploy/lib/common.sh', 'deploy/lib/build_version.sh', 'deploy/lib/csg_tool.py', 'deploy/lib/csg_notices.py')
}

# The utility for Windows links these of the compiler's libraries statically, each under one of three
# notices: GCC's runtime, mingw-w64's winpthreads, and the rest of mingw-w64's runtime, its import
# libraries for Windows' own DLLs among them. deploy/build-utility.ps1 refuses a file that links any
# other, and deploy/new-third-party-notices.ps1 writes the three.
$UtilityLibraryGroups = @{
    'libstdc++.a'     = 'gcc'
    'libgcc.a'        = 'gcc'
    'libgcc_eh.a'     = 'gcc'
    'libpthread.a'    = 'winpthreads'
    'libwinpthread.a' = 'winpthreads'
    'libmingw32.a'    = 'mingw-w64'
    'libmingwex.a'    = 'mingw-w64'
    'libmoldname.a'   = 'mingw-w64'
    'libmsvcrt.a'     = 'mingw-w64'
    'libucrt.a'       = 'mingw-w64'
    'libucrtbase.a'   = 'mingw-w64'
    'libkernel32.a'   = 'mingw-w64'
    'libuser32.a'     = 'mingw-w64'
    'libadvapi32.a'   = 'mingw-w64'
    'libshell32.a'    = 'mingw-w64'
    'libws2_32.a'     = 'mingw-w64'
}

function Get-csiGarlandUtilityLibraryGroup {
    param([Parameter(Mandatory = $true)][string]$Library)
    return $UtilityLibraryGroups[$Library]
}

# Go keeps a cgo package compiled until something in its own folder changes, and the engine's sources
# are in firmware/app/src, so a build could carry the engine as it was. A digest of them in the flags,
# which are part of what Go keys its cache on, recompiles it exactly when they change.
function Set-csiGarlandEngineSources {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)

    $folder = Join-Path $RepoRoot 'firmware/app/src'
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

    $sources = @(& git -C $RepoRoot ls-files --cached --others --exclude-standard -- firmware/app cmd internal deploy go.mod go.sum LICENSE THIRD_PARTY_NOTICES.md)
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
    # A tree of git's own, with this one's attributes, so that the digests of its sources are worked out
    # as they are here and from a checkout of a tag: Get-csiGarlandTreeDigest asks git.
    foreach ($name in '.gitattributes', '.gitignore') {
        $from = Join-Path $RepoRoot $name
        if (Test-Path -LiteralPath $from) {
            Copy-Item -LiteralPath $from -Destination (Join-Path $Destination $name)
        }
    }
    # Had init failed, git would look further up for a repository, this one's own under temp\ among them,
    # and add the copy to that: so the copy has to be the root of its own before anything is added.
    & git -C $Destination init --quiet
    if ($LASTEXITCODE -ne 0) {
        throw "git could not make a repository of the copy in $Destination (exit code $LASTEXITCODE)."
    }
    $top = & git -C $Destination rev-parse --show-toplevel
    if ($LASTEXITCODE -ne 0 -or [System.IO.Path]::GetFullPath("$top".Trim()).TrimEnd('\', '/') -ne [System.IO.Path]::GetFullPath($Destination).TrimEnd('\', '/')) {
        throw "The copy in $Destination is not a repository of its own (git says $top): nothing was added."
    }
    & git -C $Destination add --all
    if ($LASTEXITCODE -ne 0) {
        throw "git could not take the copy in $Destination (exit code $LASTEXITCODE)."
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
    Undo-csiGarlandBuildNumber, `
    Get-csiGarlandPlatformIOCore, `
    Get-csiGarlandPlatformIOPackages, `
    Lock-csiGarlandBuild, `
    Get-csiGarlandFirmwareEnvironment, `
    Get-csiGarlandFirmwareIdentity, `
    Get-csiGarlandReleaseCompiler, `
    Get-csiGarlandUtilityPlatforms, `
    Get-csiGarlandCompilerBuild, `
    Get-csiGarlandReleaseCgoFlags, `
    Get-csiGarlandReleaseAmd64, `
    Split-csiGarlandCommand, `
    Get-csiGarlandAppDescVersion, `
    Get-csiGarlandBinIdentityProblem, `
    Get-csiGarlandImageId, `
    Get-csiGarlandSourceIdentity, `
    Get-csiGarlandSourceFormat, `
    Get-csiGarlandTreeDigest, `
    Get-csiGarlandUtilityLibraryGroup, `
    Get-csiGarlandUtilitySourceDigest
