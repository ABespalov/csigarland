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
# Everything that goes into the file, the firmware's engine and effects among it.
$SourceFolders = @('cmd', 'internal', 'firmware/app/effects', 'firmware/app/src', 'go.mod', 'go.sum', 'deploy/build-utility.ps1')

function Get-SourceFingerprint {
    $paths = @(git -C $RepoRoot ls-files --cached --others --exclude-standard -- @SourceFolders | Sort-Object -CaseSensitive)
    if ($LASTEXITCODE -ne 0) {
        throw "git ls-files failed with exit code $LASTEXITCODE."
    }
    $hash = [System.Security.Cryptography.IncrementalHash]::CreateHash([System.Security.Cryptography.HashAlgorithmName]::SHA256)
    try {
        foreach ($path in $paths) {
            $full = Join-Path $RepoRoot $path
            if (Test-Path -LiteralPath $full -PathType Leaf) {
                $hash.AppendData([System.Text.Encoding]::UTF8.GetBytes("$path`n"))
                $hash.AppendData([System.IO.File]::ReadAllBytes($full))
            }
        }
        return [System.Convert]::ToHexString($hash.GetHashAndReset())
    } finally {
        $hash.Dispose()
    }
}

function Get-StoredText {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (Test-Path -LiteralPath $Path) {
        return (Get-Content -Raw -LiteralPath $Path).Trim()
    }
    return ''
}

# The compiler the releases are made with. Another one builds as well, and makes another file.
$ReleaseCompiler = 'gcc.exe (MinGW-W64 x86_64-ucrt-posix-seh, built by Brecht Sanders, r2) 16.1.0'

$toolchain = [regex]::Match((Get-Content -Raw -LiteralPath (Join-Path $RepoRoot 'go.mod')), '(?m)^toolchain (go[0-9.]+)\s*$')
if (-not $toolchain.Success) {
    throw 'No toolchain line in go.mod.'
}
$env:GOTOOLCHAIN = $toolchain.Groups[1].Value

$base = [regex]::Match((Get-Content -Raw -LiteralPath $VersionFile), 'Version = "(\d+\.\d+\.\d+)(-[0-9A-Za-z.]+)?"')
if (-not $base.Success) {
    throw "No base version in $VersionFile."
}
$stored = Get-StoredText -Path $CounterPath
$number = if ($stored -match '^\d+$') { [int]$stored } else { 0 }
$system = (& go env GOOS)
$machine = (& go env GOARCH)
$extension = if ($system -eq 'windows') { '.exe' } else { '' }

function Get-OutputPath {
    param([Parameter(Mandatory = $true)][int]$Build)

    $version = "$($base.Groups[1].Value).$Build$($base.Groups[2].Value)$(if ($BuildProfile -eq 'Dev') { '-dev' })"
    return @($version, (Join-Path $Target "csigarland-$version-$system-$machine$extension"))
}

$fingerprint = Get-SourceFingerprint
$named = $BuildNumber -ge 0
$kept = (Get-OutputPath -Build $number)[1]
if (-not $named -and -not $Force -and $number -gt 0 -and $fingerprint -eq (Get-StoredText -Path $FingerprintPath) -and (Test-Path -LiteralPath $kept)) {
    Write-Host "Sources unchanged, so $kept stands."
    Write-Output $kept
    return
}

$number = if ($named) { $BuildNumber } else { $number + 1 }
$version, $output = Get-OutputPath -Build $number
$compiler = "$((& gcc --version 2>$null) | Select-Object -First 1)".Trim()
if ($compiler -ne $ReleaseCompiler) {
    Write-Warning "The C++ compiler is $compiler, not ${ReleaseCompiler}: this builds, but not the same file as a release."
}
# On Windows, with no console behind the window. The linker writes no time into the PE header.
$linkFlags = @("-X main.Version=$version")
if ($BuildProfile -eq 'Release') {
    $linkFlags = @('-s', '-w') + $linkFlags
}
if ($system -eq 'windows') {
    $linkFlags += @('-H=windowsgui', '-extldflags=-Wl,--no-insert-timestamp')
}
New-Item -ItemType Directory -Path $Target -Force | Out-Null
$env:CGO_ENABLED = '1'
Import-Module (Join-Path $PSScriptRoot 'BuildVersion.psm1') -Force
Set-csiGarlandEngineSources -RepoRoot $RepoRoot
# The engine's C++ names its files by where this clone is; mapped to the repository's root, it does not.
# Quoted, as Go reads these flags: a path may hold a space.
$env:CGO_CPPFLAGS = "$env:CGO_CPPFLAGS `"-ffile-prefix-map=$($RepoRoot.TrimEnd('\'))=.`""
& go -C $RepoRoot build -trimpath -buildvcs=false "-ldflags=$($linkFlags -join ' ')" -o $output ./cmd/csigarland
if ($LASTEXITCODE -ne 0) {
    throw "Building the utility failed with exit code $LASTEXITCODE."
}
if (-not $named) {
    [System.IO.File]::WriteAllText($CounterPath, "$number`n")
    [System.IO.File]::WriteAllText($FingerprintPath, "$fingerprint`n")
}
$hash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()
# What made it, for whoever makes it again.
$manifest = [ordered]@{
    version       = $version
    profile       = $BuildProfile
    sha256        = $hash
    go            = "$(& go version)".Trim()
    compiler      = $compiler
    source_sha256 = $fingerprint.ToLowerInvariant()
    commit        = $(try { $c = & git -C $RepoRoot rev-parse HEAD 2>$null; if ($LASTEXITCODE -eq 0) { "$c".Trim() } else { $null } } catch { $null })
}
[System.IO.File]::WriteAllText("$output.manifest.json", (($manifest | ConvertTo-Json) + "`n"), [System.Text.UTF8Encoding]::new($false))
Write-Host "$output, $([int]((Get-Item -LiteralPath $output).Length / 1KB)) KB, SHA-256 $hash"
# For deploy/run.ps1.
Write-Output $output
