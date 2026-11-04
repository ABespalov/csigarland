# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Checks the source rules of AGENTS.md that a machine can check.
# Contributors: None
#
# Looks at the files git tracks, and at new ones it would track, so it also works before a
# commit: pwsh -File deploy\tests\source-rules.tests.ps1
#requires -Version 7.2

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$MaxLines = 1000
$HeaderChars = 400
$LicenseLine = 'Licensed under the terms specified by the MIT License'
$PowerShellGuard = '#requires -Version 7.2'
$SourcePattern = '\.(c|cc|cpp|h|hpp|go|py|ps1|psm1|html|css|js)$'
$NativePattern = '\.(c|cc|cpp|h|hpp|go)$'
$FirmwarePattern = '^firmware/app/(src|test|scripts)/'
# A third party's file, copied whole with its own licence header, so cgo can build it.
$CopiedPattern = '^internal/engine/cJSON\.(c|h)$'

$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$stubFolder = Join-Path $repository 'docs\stubs'
$problems = [System.Collections.Generic.List[string]]::new()

$files = @(git -C $repository ls-files --cached --others --exclude-standard | Where-Object { $_ -match $SourcePattern })
if ($LASTEXITCODE -ne 0) {
    throw "git ls-files failed with exit code $LASTEXITCODE."
}

foreach ($file in $files) {
    $path = Join-Path $repository $file
    if ($file -match $CopiedPattern -or -not (Test-Path -LiteralPath $path)) {
        continue
    }
    $text = [System.IO.File]::ReadAllText($path)
    $lines = ($text -split "`n").Count
    if ($text.EndsWith("`n")) {
        $lines -= 1
    }
    if ($lines -gt $MaxLines) {
        $problems.Add("${file}: $lines lines, the limit is $MaxLines")
    }
    # In the header, rather than anywhere in the file.
    if ($file -match $NativePattern -and -not $text.Substring(0, [Math]::Min($HeaderChars, $text.Length)).Contains($LicenseLine)) {
        $problems.Add("${file}: no licence header")
    }
    if ($file -match '\.(ps1|psm1)$') {
        # PowerShell heeds it only on a line of its own, ahead of the param block.
        $guard = [regex]::Match($text, "(?m)^$([regex]::Escape($PowerShellGuard))\s*$")
        $firstCode = [regex]::Match($text, '(?m)^\s*(\[CmdletBinding|param\s*\()')
        if (-not $guard.Success) {
            $problems.Add("${file}: no '$PowerShellGuard' on a line of its own")
        } elseif ($firstCode.Success -and $guard.Index -gt $firstCode.Index) {
            $problems.Add("${file}: '$PowerShellGuard' comes after the param block")
        }
    }
    # Those for a real board or garland cannot be run here, but they can be read.
    if ($file -match '\.(ps1|psm1)$') {
        $syntaxErrors = $null
        $null = [System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$null, [ref]$syntaxErrors)
        foreach ($syntaxError in $syntaxErrors) {
            $problems.Add("${file}:$($syntaxError.Extent.StartLineNumber): $($syntaxError.Message)")
        }
    }
    # What these print reaches the serial log, which is ASCII only.
    if ($file -match $FirmwarePattern -and $file -match '\.(c|cc|cpp|h|hpp)$' -and $text -match '[^\x00-\x7F]') {
        $problems.Add("${file}: not plain ASCII")
    }
}

# Where a link leads: a stub on a checkout with no symlinks is a file holding the target's path.
function Resolve-Stub {
    param([Parameter(Mandatory = $true)][string]$Path)

    $full = [System.IO.Path]::GetFullPath($Path)
    if ([System.IO.Path]::GetDirectoryName($full) -ne $stubFolder -or [System.IO.Path]::GetFileName($full) -eq 'README.md' -or
        (Get-Item -LiteralPath $full).LinkType) {
        return $full
    }
    return [System.IO.Path]::GetFullPath((Join-Path $stubFolder ([System.IO.File]::ReadAllText($full).Trim())))
}

# The anchors a document's headings make on GitHub.
function Get-HeadingAnchors {
    param([Parameter(Mandatory = $true)][string]$Path)

    $Path = Resolve-Stub -Path $Path
    $anchors = [System.Collections.Generic.HashSet[string]]::new()
    $inFence = $false
    foreach ($line in [System.IO.File]::ReadAllLines($Path)) {
        if ($line -match '^\s*```') {
            $inFence = -not $inFence
        }
        if ($inFence -or $line -notmatch '^#{1,6}\s+(.+?)\s*$') {
            continue
        }
        $slug = ($Matches[1] -replace '\[([^\]]*)\]\([^)]*\)', '$1').ToLowerInvariant() -replace '[^\p{L}\p{Nd}\p{Mn}\s_-]', '' -replace '\s', '-'
        $candidate = $slug
        for ($repeat = 1; -not $anchors.Add($candidate); $repeat++) {
            $candidate = "$slug-$repeat"
        }
    }
    return , $anchors
}

# Links in the documents have to work for anyone who has the repository.
$documents = @(git -C $repository ls-files --cached --others --exclude-standard | Where-Object { $_ -match '\.md$' })
foreach ($file in $documents) {
    $path = Join-Path $repository $file
    if (-not (Test-Path -LiteralPath $path)) {
        continue
    }
    # A stub is checked below, and its target here as itself: read through a symlink, the target's
    # links would be taken from the stubs' folder.
    if ($file -like 'docs/stubs/*' -and $file -ne 'docs/stubs/README.md') {
        continue
    }
    $text = [System.IO.File]::ReadAllText($path)
    if ($text -match 'file:///') {
        $problems.Add("${file}: a file:/// link")
    }
    foreach ($link in [regex]::Matches($text, '\]\(([^)#\s]+)(#[^)]*)?\)')) {
        $target = $link.Groups[1].Value
        if ($target -match '^[a-z]+:') {
            continue
        }
        $resolved = Join-Path (Split-Path -Parent $path) $target
        if (-not (Test-Path -LiteralPath $resolved)) {
            $problems.Add("${file}: broken link to $target")
        } elseif ($link.Groups[2].Success -and $target -match '\.md$' -and -not (Get-HeadingAnchors -Path $resolved).Contains($link.Groups[2].Value.Substring(1))) {
            $problems.Add("${file}: no heading for $target$($link.Groups[2].Value)")
        }
    }
    foreach ($image in [regex]::Matches($text, '<img\s[^>]*src="([^"#]+)"')) {
        $target = $image.Groups[1].Value
        if ($target -notmatch '^[a-z]+:' -and -not (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $path) $target))) {
            $problems.Add("${file}: broken picture $target")
        }
    }
    foreach ($link in [regex]::Matches($text, '\]\((#[^)\s]+)\)')) {
        if (-not (Get-HeadingAnchors -Path $path).Contains($link.Groups[1].Value.Substring(1))) {
            $problems.Add("${file}: no heading for $($link.Groups[1].Value)")
        }
    }
}

# Stubs hold no documents: a link git knows as one, to a file that is there, and named in the README.
$stubReadme = [System.IO.File]::ReadAllText((Join-Path $stubFolder 'README.md'))
foreach ($stub in Get-ChildItem -LiteralPath $stubFolder -Force | Where-Object { $_.Name -ne 'README.md' }) {
    $file = "docs/stubs/$($stub.Name)"
    if ($stub.PSIsContainer) {
        $problems.Add("${file}: a folder among the stubs")
        continue
    }
    if (-not $stub.LinkType) {
        $text = [System.IO.File]::ReadAllText($stub.FullName).Trim()
        if ($text -match '\n' -or $text -notmatch '^\.\./') {
            $problems.Add("${file}: a stub is a link to a file elsewhere under docs/, and this holds content")
            continue
        }
    }
    if (-not (Test-Path -LiteralPath (Resolve-Stub -Path $stub.FullName) -PathType Leaf)) {
        $problems.Add("${file}: leads to no file")
    }
    if (-not $stubReadme.Contains("``$file``")) {
        $problems.Add("docs/stubs/README.md: does not list $file")
    }
    if ((@(git -C $repository ls-files -s -- $file) -join '') -notmatch '^120000 ') {
        $problems.Add("${file}: git does not hold it as a link, see docs/stubs/README.md")
    }
}

# One size of the effects pack's slots, as nothing else ties these three together.
$partitions = [System.IO.File]::ReadAllText((Join-Path $repository 'firmware/app/partitions.csv'))
$slotSizes = @(
    [regex]::Match($partitions, '(?m)^pack_a\s*,[^,]*,[^,]*,[^,]*,\s*(0x[0-9A-Fa-f]+)').Groups[1].Value,
    [regex]::Match($partitions, '(?m)^pack_b\s*,[^,]*,[^,]*,[^,]*,\s*(0x[0-9A-Fa-f]+)').Groups[1].Value,
    [regex]::Match([System.IO.File]::ReadAllText((Join-Path $repository 'firmware/app/scripts/effect_pack_tool.py')), '(?m)^SLOT_BYTES = (0x[0-9A-Fa-f]+)').Groups[1].Value,
    [regex]::Match([System.IO.File]::ReadAllText((Join-Path $repository 'firmware/app/src/effect_pack.h')), 'kPackSlotBytes = (0x[0-9A-Fa-f]+)').Groups[1].Value
)
if (@($slotSizes | ForEach-Object { $_.ToLowerInvariant() } | Select-Object -Unique).Count -ne 1 -or -not $slotSizes[0]) {
    $problems.Add("firmware/app/partitions.csv: the pack's slot differs from effect_pack_tool.py or effect_pack.h: $($slotSizes -join ', ')")
}

# The effects guide lists every effect and every algorithm, as nothing else would make it.
$guidePath = 'docs/guides/effects.md'
$guide = [System.IO.File]::ReadAllText((Join-Path $repository $guidePath))
$catalog = Get-Content -Raw -LiteralPath (Join-Path $repository 'firmware/app/effects/catalog.json') | ConvertFrom-Json
$listed = @($catalog.effects | ForEach-Object { $_.id })
$parser = [System.IO.File]::ReadAllText((Join-Path $repository 'firmware/app/src/effect_parser.cpp'))
$algorithms = @([regex]::Matches($parser, '\{"([a-z0-9-]+)", Algorithm::') | ForEach-Object { $_.Groups[1].Value })
if ($algorithms.Count -eq 0) {
    $problems.Add('firmware/app/src/effect_parser.cpp: no algorithms found to check')
}

# The names in one cell of the rows of one section, as a name in passing elsewhere describes nothing.
function Get-TableNames {
    param(
        [Parameter(Mandatory = $true)][string]$Section,
        [Parameter(Mandatory = $true)][int]$Cell
    )

    $start = $guide.IndexOf("`n$Section`n")
    if ($start -lt 0) {
        $problems.Add("${guidePath}: no heading '$Section'")
        return @()
    }
    $next = [regex]::Match($guide.Substring($start + $Section.Length + 2), '(?m)^#{1,3} ')
    $body = if ($next.Success) { $guide.Substring($start, $Section.Length + 2 + $next.Index) } else { $guide.Substring($start) }
    $names = foreach ($row in [regex]::Matches($body, '(?m)^\|.*$')) {
        $cells = $row.Value -split '\|'
        if ($cells.Count -gt $Cell) {
            [regex]::Matches($cells[$Cell], '`([a-z0-9-]+)`') | ForEach-Object { $_.Groups[1].Value }
        }
    }
    return @($names)
}

$listedInGuide = Get-TableNames -Section '### The effects, in catalog order' -Cell 2
foreach ($name in $listed) {
    if ($name -cnotin $listedInGuide) {
        $problems.Add("${guidePath}: no row in the catalog for ``$name``")
    }
}
$algorithmsInGuide = Get-TableNames -Section '## 8. Algorithms' -Cell 1
foreach ($name in $algorithms) {
    if ($name -cnotin $algorithmsInGuide) {
        $problems.Add("${guidePath}: no row in the algorithms for ``$name``")
    }
}

# The schema offered to editors names what the parser takes, and every effect that ships passes it.
$schemaPath = 'firmware/app/effects/effect.schema.json'
$schemaFile = Join-Path $repository $schemaPath
$schema = Get-Content -Raw -LiteralPath $schemaFile | ConvertFrom-Json -AsHashtable
$named = @($schema['$defs']['layer']['properties']['fx']['enum'])
Compare-Object $algorithms $named | ForEach-Object {
    $problems.Add("${schemaPath}: differs from the parser about $($_.InputObject)")
}
# A planned effect is named in the catalog and has no file yet, as the pack tool also demands.
foreach ($entry in $catalog.effects) {
    $id = $entry.id
    $effectFile = Join-Path $repository "firmware/app/effects/$id.json"
    if ($entry.PSObject.Properties['todo'] -and $entry.todo) {
        if (Test-Path -LiteralPath $effectFile) {
            $problems.Add("firmware/app/effects/${id}.json: its catalog entry is marked todo")
        }
    } elseif (-not (Test-Path -LiteralPath $effectFile)) {
        $problems.Add("firmware/app/effects/${id}.json: missing, and its catalog entry is not marked todo")
    } elseif (-not (Test-Json -LiteralPath $effectFile -SchemaFile $schemaFile -ErrorAction SilentlyContinue)) {
        $problems.Add("firmware/app/effects/${id}.json: does not pass the schema")
    }
}

# The utility's engine builds with a copy of the cJSON the firmware's component lock brings in, so a
# clone builds the utility with no firmware build. Where that build has run, the copy is held to it.
$component = Join-Path $repository 'firmware/app/managed_components/espressif__cjson/cJSON'
foreach ($name in @('cJSON.c', 'cJSON.h')) {
    $locked = Join-Path $component $name
    $copy = Join-Path $repository "internal/engine/$name"
    if ((Test-Path -LiteralPath $locked) -and (Get-FileHash -LiteralPath $locked).Hash -ne (Get-FileHash -LiteralPath $copy).Hash) {
        $problems.Add("internal/engine/${name}: differs from the firmware's locked cJSON; copy it from $locked")
    }
}

# The site's Russian pages are made from its English ones, and are held to what they would make now.
try {
    & (Join-Path $PSScriptRoot '..\site-pages.ps1') -Check 6>$null | Out-Null
} catch {
    $problems.Add("site/ru: $($_.Exception.Message)")
}

if ($problems.Count -gt 0) {
    $problems | ForEach-Object { Write-Host $_ }
    throw "$($problems.Count) source rule problems."
}
Write-Host "Source rules hold for $($files.Count) files."
