# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Refuses keys and certificates in the working tree's tracked files and in history.
# Contributors: None
#
# A key that reached history is out, even if a later commit removes it: replace it. Works
# before a first commit too, on what git would track.
#requires -Version 7.2

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# By name: every kind of file a key or certificate lives in, and this project's local files.
$NamePattern = '(\.(pem|key|crt|cer|der|p12|pfx)$)|(\.local\.)'
# By content: a private key under any name. Split so this file does not match itself.
$KeyMarker = '-----BEGIN ' + '([A-Z0-9 ]+ )?PRIVATE KEY-----'

$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$problems = [System.Collections.Generic.List[string]]::new()

$present = @(git -C $repository ls-files --cached --others --exclude-standard)
if ($LASTEXITCODE -ne 0) {
    throw "git ls-files failed with exit code $LASTEXITCODE."
}
$named = @($present)
$hasCommits = $null -ne (git -C $repository rev-parse --verify --quiet HEAD 2>$null)
if ($hasCommits) {
    $named += @(git -C $repository log --all --format= --name-only | Where-Object { $_ })
}
foreach ($file in @($named | Select-Object -Unique)) {
    if ($file -match $NamePattern) {
        $problems.Add("${file}: its name says key or certificate")
    }
    # Whatever it is called, as a passphrase's file says nothing by its name.
    if ($file -match '^cert/' -and $file -cne 'cert/README.md') {
        $problems.Add("${file}: only the README of cert/ may be committed")
    }
}

foreach ($file in $present) {
    $path = Join-Path $repository $file
    if ((Test-Path -LiteralPath $path -PathType Leaf) -and (Select-String -LiteralPath $path -Pattern $KeyMarker -Quiet)) {
        $problems.Add("${file}: holds a private key")
    }
}
if ($hasCommits) {
    $commits = @(git -C $repository log --all --format=%H -G $KeyMarker --extended-regexp)
    foreach ($commit in $commits) {
        $problems.Add("commit ${commit}: adds or removes a private key")
    }
}

if ($problems.Count -gt 0) {
    $problems | ForEach-Object { Write-Host $_ }
    throw "$($problems.Count) sensitive data problems."
}
Write-Host "No keys or certificates in $($present.Count) files."
