# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Builds the csiGarland utility for this computer, into assets/utility.
# Contributors: None
#
# Needs Go, and a C++20 compiler on the PATH for cgo, as the effect engine is the firmware's own
# code: MinGW-w64 on Windows. The result is one file with nothing to install beside it.
#
# Its bash twin, deploy/build-utility.sh, on macOS makes the file a bundle signed ad hoc and zipped, as
# deploy/lib/macos_bundle.sh says. cgo builds only for the system it runs on, and only a Mac signs a
# bundle, so this one refuses to build on macOS, and names that one.
#
# The utility and the firmware are built apart and each counts its own builds. The base version is
# in cmd/csigarland/version.go, and a build is it with its number, such as 1.0.0.27. Sources the
# same as the last build's, which the fingerprint tells, keep its number and its file. A build that
# fails spends no number.
#
# The same sources, build number, Go and C++ compiler make the same file to the byte: Go is the
# toolchain go.mod names, which GOTOOLCHAIN is set to, the paths of this machine are mapped away,
# and the linker writes no time. A .manifest.json beside the file says what made it, and
# docs/guides/rebuild.md how to make it again.
#
# Parameters
#
#   -Force
#       Build and spend a number even though nothing changed.
#
#   -BuildNumber <n>
#       Build that number outright, as a release's manifest names it, and leave the counter as it is.
#
#   -BuildProfile Release|Dev
#       Release, the default, links with no symbols or debug information and gives the plain
#       version, such as 1.0.0.27. Dev keeps them, for a debugger, and is 1.0.0.27-dev.
#
# Writes the file's path as its output. To build and start it, use deploy\run.ps1.
#
# Example:
#   pwsh -File deploy\build-utility.ps1
#requires -Version 7.2

param(
    [switch]$Force,
    [int]$BuildNumber = -1,
    [ValidateSet('Release', 'Dev')][string]$BuildProfile = 'Release'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Target = Join-Path $RepoRoot 'assets\utility'
$VersionFile = Join-Path $RepoRoot 'cmd\csigarland\version.go'
$CounterPath = Join-Path $RepoRoot 'cmd\csigarland\.csigarland_build_counter'
$FingerprintPath = Join-Path $RepoRoot 'cmd\csigarland\.csigarland_build_fingerprint'
Import-Module (Join-Path $PSScriptRoot 'BuildVersion.psm1') -Force

function Get-StoredText {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (Test-Path -LiteralPath $Path) {
        return "$(Get-Content -Raw -LiteralPath $Path)".Trim()
    }
    return ''
}


$toolchain = [regex]::Match((Get-Content -Raw -LiteralPath (Join-Path $RepoRoot 'go.mod')), '(?m)^toolchain (go[0-9.]+)\s*$')
if (-not $toolchain.Success) {
    throw 'No toolchain line in go.mod.'
}
$env:GOTOOLCHAIN = $toolchain.Groups[1].Value

$base = [regex]::Match((Get-Content -Raw -LiteralPath $VersionFile), 'Version = "(\d+\.\d+\.\d+)(-[0-9A-Za-z.]+)?"')
if (-not $base.Success) {
    throw "No base version in $VersionFile."
}
# One utility build of this checkout at a time, from reading the counter to writing it: two would take
# one number, and share the link map's folder. Let go however this script ends: run inside another script's
# PowerShell, as deploy/run.ps1 runs it, a lock left for that PowerShell to end held back every build after it.
$BuildLock = Lock-csiGarlandBuild -RepoRoot $RepoRoot -Kind utility
try {
    # 0 with none; one there that holds no number is refused, as starting again would give numbers out twice.
    $number = Get-csiGarlandBuildNumber -CounterPath $CounterPath
    $system = (& go env GOOS)
    $machine = (& go env GOARCH)
    if ($system -eq 'darwin') {
        throw 'On macOS the utility is a bundle zipped, which only deploy/build-utility.sh makes: bash deploy/build-utility.sh. Nothing was built.'
    }
    $extension = if ($system -eq 'windows') { '.exe' } else { '' }
    # The compiler the releases for this platform are made with. Another one builds as well, and makes
    # another file; a platform with none named has no release yet, and this build is no release's.
    $ReleaseCompiler = Get-csiGarlandReleaseCompiler -Platform "$system-$machine"

    function Get-OutputPath {
        param([Parameter(Mandatory = $true)][int]$Build)

        $version = "$($base.Groups[1].Value).$Build$($base.Groups[2].Value)$(if ($BuildProfile -eq 'Dev') { '-dev' })"
        return @($version, (Join-Path $Target "csigarland-$version-$system-$machine$extension"))
    }

    $compiler = "$((& gcc --version 2>$null) | Select-Object -First 1)".Trim()
    # What cgo really compiles with, which CC and CXX may set apart from the gcc first in PATH, and the flags
    # it is given before this script adds its own: the manifest records them, and a release is held to them.
    # On x86-64, the level any 64-bit PC runs, whatever GOAMD64 says in this shell. No workspace of a folder
    # above, and no GOFLAGS of the shell's: either would change what is built from the sources here.
    if ($machine -eq 'amd64') {
        $env:GOAMD64 = Get-csiGarlandReleaseAmd64
    }
    $env:GOWORK = 'off'
    $env:GOFLAGS = ''
    $env:GOEXPERIMENT = ''
    $env:GOFIPS140 = ''
    # An empty variable gives way to what go env -w wrote, so what Go will really use is asked, and recorded:
    # GOFLAGS, GOEXPERIMENT or a GOFIPS140 other than off from there would build another file, which no
    # release takes.
    $goEnv = & go env -json CC CXX CGO_CFLAGS CGO_CPPFLAGS CGO_CXXFLAGS CGO_LDFLAGS GOAMD64 GOFLAGS GOWORK GOEXPERIMENT GOFIPS140 | ConvertFrom-Json
    foreach ($name in 'GOFLAGS', 'GOEXPERIMENT') {
        if ("$($goEnv.$name)" -ne '') {
            Write-Warning "Go takes $name '$($goEnv.$name)' from its own settings, go env -w: this builds, but not the same file as a release. Unset it: go env -u $name"
        }
    }
    if ("$($goEnv.GOFIPS140)" -ne 'off') {
        Write-Warning "Go takes GOFIPS140 '$($goEnv.GOFIPS140)' from its own settings, go env -w: this builds, but not the same file as a release. Unset it: go env -u GOFIPS140"
    }
    # As Go runs them, a program and its arguments: one it cannot be asked its version builds all the same,
    # and is recorded as unknown, which no release takes.
    $versionOf = {
        param([string]$Command)
        # An array, one token or more: a lone gcc would come back a string, and its first "part" a letter.
        $parts = @(Split-csiGarlandCommand -Command $Command)
        try {
            $first = if ($parts.Count -gt 1) { & $parts[0] @($parts[1..($parts.Count - 1)]) --version 2>$null } else { & $parts[0] --version 2>$null }
            return "$($first | Select-Object -First 1)".Trim()
        } catch {
            Write-Warning "$Command could not be asked its version: $($_.Exception.Message)"
            return 'unknown'
        }
    }
    $cc = & $versionOf $goEnv.CC
    $cxx = & $versionOf $goEnv.CXX
    $goVersion = "$(& go version)".Trim()
    # Arguments in CC or CXX go to the compiler past any flag Go is given, as -march would: a release takes
    # the bare commands.
    foreach ($command in $goEnv.CC, $goEnv.CXX) {
        if (@(Split-csiGarlandCommand -Command $command).Count -gt 1) {
            Write-Warning "cgo runs '$command', with arguments of its own: this builds, but not the same file as a release."
        }
    }
    $cgoFlags = [ordered]@{}
    foreach ($name in (Get-csiGarlandReleaseCgoFlags).Keys) {
        $cgoFlags[$name] = "$($goEnv.$name)"
    }
    $flagsDiffer = @((Get-csiGarlandReleaseCgoFlags).Keys | Where-Object { $cgoFlags[$_] -ne (Get-csiGarlandReleaseCgoFlags)[$_] })
    if (-not $ReleaseCompiler) {
        Write-Warning "No release compiler is named for $system-$machine in deploy\BuildVersion.psm1: this builds, but no release takes it yet."
    } elseif ((Get-csiGarlandCompilerBuild -Line $cc) -ne (Get-csiGarlandCompilerBuild -Line $ReleaseCompiler) -or (Get-csiGarlandCompilerBuild -Line $cxx) -ne (Get-csiGarlandCompilerBuild -Line $ReleaseCompiler) -or $flagsDiffer.Count -gt 0) {
        Write-Warning "cgo compiles with $cc and $cxx$(if ($flagsDiffer.Count -gt 0) { ", and $($flagsDiffer -join ', ') set" }), not ${ReleaseCompiler} as Go sets it: this builds, but not the same file as a release."
    }

    # A module go.mod replaces is built from other bytes than the notices read the licence of, by go.mod's
    # requirements, and one replaced with a folder from bytes no digest, tag or copy for building again holds:
    # a Release refuses either, and a Dev build says so.
    $replaces = @((& go -C $RepoRoot mod edit -json | ConvertFrom-Json).Replace | Where-Object { $_ } | ForEach-Object { "$($_.Old.Path) => $($_.New.Path)$(if ($_.New.Version) { " $($_.New.Version)" })" })
    if ($replaces.Count -gt 0) {
        $said = "go.mod replaces a module ($($replaces -join '; ')), which the notices and a tag do not follow"
        if ($BuildProfile -eq 'Release') {
            throw "${said}: a Release takes modules as go.sum holds them. Nothing was built."
        }
        Write-Warning "${said}: this builds, but no release takes it."
    }
    # The manifest names the notices and the licence the file is passed on with, so a tree without them,
    # as a copy that left them out, builds nothing.
    foreach ($name in 'LICENSE', 'THIRD_PARTY_NOTICES.md') {
        if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot $name) -PathType Leaf)) {
            throw "$name is not in $RepoRoot, and the manifest names it: nothing was built."
        }
    }
    # The sources and every tool and flag the file is made with, and the notices it is passed on with: a build
    # kept is taken again only if all of them are the same, so one fixed only in its tools is built again.
    $sourceDigest = Get-csiGarlandUtilitySourceDigest -RepoRoot $RepoRoot
    $noticesDigests = @('THIRD_PARTY_NOTICES.md', 'LICENSE' | ForEach-Object { (Get-FileHash -LiteralPath (Join-Path $RepoRoot $_) -Algorithm SHA256).Hash })
    $fingerprint = $sourceDigest + '|' + [System.Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData(
            [System.Text.Encoding]::UTF8.GetBytes((@($BuildProfile, $goVersion, $compiler, $cc, $cxx, $goEnv.CC, $goEnv.CXX, $goEnv.GOAMD64, $goEnv.GOFLAGS, $goEnv.GOWORK, $goEnv.GOEXPERIMENT, $goEnv.GOFIPS140) +
                    @($cgoFlags.Values) + $noticesDigests) -join "`n"))).ToLowerInvariant()
    $named = $BuildNumber -ge 0
    $kept = (Get-OutputPath -Build $number)[1]
    # A build whose notices were not found to hold is not one to keep: the check runs again.
    $keptChecked = $false
    if (Test-Path -LiteralPath "$kept.manifest.json") {
        $keptManifest = Get-Content -Raw -LiteralPath "$kept.manifest.json" | ConvertFrom-Json
        $keptChecked = $keptManifest.PSObject.Properties['notices_checked'] -and $keptManifest.notices_checked -eq $true
    }
    if (-not $named -and -not $Force -and $number -gt 0 -and $keptChecked -and $fingerprint -eq (Get-StoredText -Path $FingerprintPath) -and (Test-Path -LiteralPath $kept)) {
        Write-Host "Sources and tools unchanged, so $kept stands."
        Write-Output $kept
        return
    }

    $number = if ($named) { $BuildNumber } else { $number + 1 }
    $version, $output = Get-OutputPath -Build $number

    # On Windows, with no console behind the window. The linker writes no time into the PE header.
    $linkFlags = @("-X main.Version=$version")
    if ($BuildProfile -eq 'Release') {
        $linkFlags = @('-s', '-w') + $linkFlags
    }
    if ($system -eq 'windows') {
        $linkFlags += @('-H=windowsgui', '-extldflags=-Wl,--no-insert-timestamp')
    }
    New-Item -ItemType Directory -Path $Target -Force | Out-Null
    # Made and checked in a folder of this run's own, and only then put in place: a build of a number named
    # again, whose file is there already, leaves that file as it was if it fails. The counter is written
    # once the pair is in place: a build that fails, or is stopped, spends no number, and leaves nothing.
    $final = $output
    $work = Join-Path $RepoRoot "temp\utility-build-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
    [System.IO.Directory]::CreateDirectory($work) | Out-Null
    $output = Join-Path $work (Split-Path -Leaf $final)
    $placed = $false
    $built = $false
    # A pair there already, of a number named again, moved aside into this run's folder until the build stands.
    $previous = @()
    try {
        $env:CGO_ENABLED = '1'
        # -mod=readonly, in both links: a vendor folder Go would otherwise take is no source the digest or a copy
        # for building again holds, and the notices are checked against the modules go.mod names.
        Set-csiGarlandEngineSources -RepoRoot $RepoRoot
        # The engine's C++ names its files by where this clone is; mapped to the repository's root, it does not.
        # Quoted, as Go reads these flags: a path may hold a space.
        $env:CGO_CPPFLAGS = "$env:CGO_CPPFLAGS `"-ffile-prefix-map=$($RepoRoot.TrimEnd('\'))=.`""
        & go -C $RepoRoot build -mod=readonly -trimpath -buildvcs=false "-ldflags=$($linkFlags -join ' ')" -o $output ./cmd/csigarland
        if ($LASTEXITCODE -ne 0) {
            throw "Building the utility failed with exit code $LASTEXITCODE."
        }
        # The digest the manifest records is of the sources Go compiled: a file saved while it built, which
        # the file may hold and the digest not, is found here, and the build spends no number.
        if ((Get-csiGarlandUtilitySourceDigest -RepoRoot $RepoRoot) -ne $sourceDigest) {
            throw 'The sources changed while the utility was built: build it again, once they are left alone.'
        }
        # Every library of the compiler's the file holds is one THIRD_PARTY_NOTICES.md names, by the groups
        # deploy/new-third-party-notices.ps1 lists: anything else has no notice yet, and the file is not passed on.
        # The link map comes from a link of its own into temp\, as its path in the flags would change the file
        # itself, by the build ID Go keys on them. The path is relative, as the linker runs where go -C puts it:
        # one with this clone's place in it could hold a space, which the linker's flags would split.
        if ($system -eq 'windows') {
            $check = Join-Path $RepoRoot 'temp\utility-link'
            New-Item -ItemType Directory -Path $check -Force | Out-Null
            $linkMap = Join-Path $check 'csigarland.map'
            # Both, as Go takes an output already there with the same build ID for done, and links nothing:
            # the map would then not be written at all.
            foreach ($left in $linkMap, (Join-Path $check 'csigarland.exe')) {
                if (Test-Path -LiteralPath $left) {
                    Remove-Item -LiteralPath $left
                }
            }
            $mapFlags = @($linkFlags | Where-Object { $_ -notlike '-extldflags=*' }) + '-extldflags=-Wl,--no-insert-timestamp,-Map=temp/utility-link/csigarland.map'
            & go -C $RepoRoot build -mod=readonly -trimpath -buildvcs=false "-ldflags=$($mapFlags -join ' ')" -o (Join-Path $check 'csigarland.exe') ./cmd/csigarland
            if ($LASTEXITCODE -ne 0) {
                throw "Linking the utility for its map failed with exit code $LASTEXITCODE."
            }
            if (-not (Test-Path -LiteralPath $linkMap)) {
                throw "The linker wrote no map to $linkMap, so what the utility links cannot be checked."
            }
            $linked = @([regex]::Matches([System.IO.File]::ReadAllText($linkMap), '(?m)^(\S[^\r\n(]*?\.a)\(') | ForEach-Object { Split-Path -Leaf $_.Groups[1].Value } | Sort-Object -Unique)
            $unknown = @($linked | Where-Object { -not (Get-csiGarlandUtilityLibraryGroup -Library $_) })
            if ($unknown.Count -gt 0) {
                throw "The utility links $($unknown -join ', '), which THIRD_PARTY_NOTICES.md has no notice for: name it in deploy\BuildVersion.psm1."
            }
        }
        # Whether THIRD_PARTY_NOTICES.md names what this build holds, its Go modules among them, which a release
        # of the utility alone asks, as one with the firmware asks its staging. The utility's part alone, by its
        # own inputs, with no firmware build: notices out of date leave the build standing, and say so.
        $noticesSaid = & pwsh -File (Join-Path $PSScriptRoot 'new-third-party-notices.ps1') -UtilityOnly 2>&1
        $noticesChecked = $LASTEXITCODE -eq 0
        if (-not $noticesChecked) {
            Write-Warning "THIRD_PARTY_NOTICES.md was not found to name what this build holds, so no release takes it: $(@($noticesSaid)[-1])"
        }
        $hash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()
        # What made it, for whoever makes it again.
        $manifest = [ordered]@{
            version       = $version
            platform      = "$system-$machine"
            profile       = $BuildProfile
            sha256        = $hash
            go            = $goVersion
            compiler      = $compiler
            cc            = $cc
            cxx           = $cxx
            cc_command    = "$($goEnv.CC)"
            cxx_command   = "$($goEnv.CXX)"
            cgo_flags     = $cgoFlags
            goamd64       = "$($goEnv.GOAMD64)"
            goflags       = "$($goEnv.GOFLAGS)"
            gowork        = "$($goEnv.GOWORK)"
            goexperiment  = "$($goEnv.GOEXPERIMENT)"
            gofips140     = "$($goEnv.GOFIPS140)"
            source_sha256 = $sourceDigest
            source_format = Get-csiGarlandSourceFormat
            notices_checked = $noticesChecked
            notices_sha256 = (Get-FileHash -LiteralPath (Join-Path $RepoRoot 'THIRD_PARTY_NOTICES.md') -Algorithm SHA256).Hash.ToLowerInvariant()
            license_sha256 = (Get-FileHash -LiteralPath (Join-Path $RepoRoot 'LICENSE') -Algorithm SHA256).Hash.ToLowerInvariant()
            commit        = $(try { $c = & git -C $RepoRoot rev-parse HEAD 2>$null; if ($LASTEXITCODE -eq 0) { "$c".Trim() } else { $null } } catch { $null })
        }
        [System.IO.File]::WriteAllText("$output.manifest.json", (($manifest | ConvertTo-Json) + "`n"), [System.Text.UTF8Encoding]::new($false))
        # The file first, then its manifest, each replacing any of that name: a manifest never names a file
        # that is not in place.
        # A pair there already, of a number named again, is moved aside first, and the finally below puts it
        # back if anything fails or stops the run before the build stands, so the file and its manifest are
        # always of one build. Named before it is moved, so a stop in between leaves nothing out of the count.
        foreach ($there in $final, "$final.manifest.json") {
            if (Test-Path -LiteralPath $there) {
                $aside = Join-Path $work ('previous-' + (Split-Path -Leaf $there))
                $previous += , @($there, $aside)
                [System.IO.File]::Move($there, $aside)
            }
        }
        $placed = $true
        [System.IO.File]::Move($output, $final)
        [System.IO.File]::Move("$output.manifest.json", "$final.manifest.json")
        $output = $final
        # The counter is what spends the number. Once it is written the build stands, and the fingerprint
        # only lets a later build keep it: one that fails to be written means only that the next builds again.
        # Whole or not at all: a write that fails keeps the number before, and the finally the pair before.
        if (-not $named) {
            Set-csiGarlandBuildNumber -CounterPath $CounterPath -Number $number -Scratch $work
        }
        $built = $true
        if (-not $named) {
            try {
                [System.IO.File]::WriteAllText($FingerprintPath, "$fingerprint`n")
            } catch {
                Write-Warning "The fingerprint could not be written, so the next build builds again: $($_.Exception.Message)"
                Remove-Item -LiteralPath $FingerprintPath -Force -ErrorAction SilentlyContinue
            }
        }
    } finally {
        $keepWork = $false
        if (-not $built) {
            # A file put in place, under a number not spent or with no manifest beside it, is no build: it goes,
            # and its manifest, before what was there comes back.
            if ($placed) {
                foreach ($left in $final, "$final.manifest.json") {
                    if (Test-Path -LiteralPath $left) {
                        Remove-Item -LiteralPath $left -Force -ErrorAction SilentlyContinue
                    }
                }
            }
            foreach ($pair in $previous) {
                if (Test-Path -LiteralPath $pair[1]) {
                    try {
                        [System.IO.File]::Move($pair[1], $pair[0], $true)
                    } catch {
                        $keepWork = $true
                    }
                }
            }
            if ($keepWork) {
                Write-Warning "The build that was there could not all be put back: what is left of it is in $work."
            }
            if (-not $named) {
                Write-Warning "The build failed, so build number $number is not spent: the next build takes it."
            }
        }
        if (-not $keepWork) {
            [System.IO.Directory]::Delete($work, $true)
        }
    }
    Write-Host "$output, $([int]((Get-Item -LiteralPath $output).Length / 1KB)) KB, SHA-256 $hash"
    # For deploy/run.ps1.
    Write-Output $output
} finally {
    $BuildLock.Dispose()
}
