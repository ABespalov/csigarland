# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Make the site's Russian pages under site/ru from its English pages and deploy/site-ru.json.
# Contributors: None
#
# First every file of the site a page names, its stylesheet, scripts and pictures, gets the start of its
# SHA-256 in its address, as styles.css?v=0123456789, written into the English pages: a browser that kept
# the one before asks for the new one as soon as it changes, rather than show an old stylesheet beside a
# new page. A text file is hashed with its lines ended as git keeps them, so the version is the same in
# any checkout.
#
# The English pages are the source: every element with data-i18n, and every attribute named by
# data-i18n-alt, data-i18n-content or data-i18n-aria, takes its Russian text from deploy/site-ru.json.
# The Russian pages are static, so search engines and visitors without scripts see them, and git
# holds them so Cloudflare Pages serves them with no build step. Run this after either changes;
# -Check, which the source rules run, only says whether site/ru is what this would write.
#
# Example:
#   pwsh -File deploy\site-pages.ps1
#   pwsh -File deploy\site-pages.ps1 -Check
#requires -Version 7.2

[CmdletBinding()]
param(
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Site = Join-Path $RepoRoot 'site'
$Target = Join-Path $Site 'ru'
$Pages = @('index.html', 'roadmap.html', 'documents.html', 'downloads.html')
$Origin = 'https://garland.csilab.am/'
$Utf8 = [System.Text.UTF8Encoding]::new($false)

$dictionary = Get-Content -Raw -Encoding utf8 -LiteralPath (Join-Path $PSScriptRoot 'site-ru.json') | ConvertFrom-Json -AsHashtable
$used = [System.Collections.Generic.HashSet[string]]::new()
$problems = [System.Collections.Generic.List[string]]::new()

function ConvertTo-HtmlText {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Text)
    return $Text.Replace('&', '&amp;').Replace('<', '&lt;').Replace('>', '&gt;').Replace('"', '&quot;')
}

function Get-Russian {
    param(
        [Parameter(Mandatory = $true)][string]$Key,
        [Parameter(Mandatory = $true)][string]$Page
    )
    if (-not $dictionary.ContainsKey($Key)) {
        $problems.Add("deploy/site-ru.json: no text for $Key, used by site/$Page")
        return $null
    }
    $null = $used.Add($Key)
    return ConvertTo-HtmlText -Text $dictionary[$Key]
}

# The start of a file's SHA-256, of its text with lines ended by LF for a text file.
function Get-FileVersion {
    param([Parameter(Mandatory = $true)][string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ([System.IO.Path]::GetExtension($Path) -in '.css', '.js', '.svg', '.json', '.txt', '.webmanifest') {
        $bytes = $Utf8.GetBytes($Utf8.GetString($bytes).Replace("`r`n", "`n"))
    }
    return [System.Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($bytes)).Substring(0, 10).ToLowerInvariant()
}

# The page with every file of the site it names in its version. Pages, folders and other sites are left.
function Add-FileVersions {
    param([Parameter(Mandatory = $true)][string]$Html)
    return [regex]::Replace($Html, '\s(href|src)="([^"?#]+)(?:\?v=[0-9a-f]+)?"', {
            param($match)
            $url = $match.Groups[2].Value
            $file = Join-Path $Site $url
            if ($url -match '^([a-z]+:|/|\.\./)' -or $url.EndsWith('.html') -or -not (Test-Path -LiteralPath $file -PathType Leaf)) {
                return $match.Value
            }
            return " $($match.Groups[1].Value)=`"${url}?v=$(Get-FileVersion -Path $file)`""
        })
}

function ConvertTo-RussianPage {
    param(
        [Parameter(Mandatory = $true)][string]$Page,
        [Parameter(Mandatory = $true)][string]$Html
    )

    $html = $Html
    if (-not $html.Contains('<html lang="en">')) {
        $problems.Add("site/${Page}: no <html lang=`"en`">")
        return $null
    }
    $html = $html.Replace('<html lang="en">', '<html lang="ru">')
    $html = $html.Replace('<!DOCTYPE html>', "<!DOCTYPE html>`n<!-- Made by deploy/site-pages.ps1 from site/$Page and deploy/site-ru.json: edit those, not this. -->")

    # The toggle leads back to the English page.
    $toggle = '<a class="lang-switch" id="lang-switch" href="ru/([^"]*)" hreflang="ru" lang="ru" aria-label="Русская версия" title="Русская версия">RU</a>'
    if ([regex]::Matches($html, $toggle).Count -ne 1) {
        $problems.Add("site/${Page}: not one language toggle leading to ru/")
        return $null
    }
    $html = [regex]::Replace($html, $toggle, '<a class="lang-switch" id="lang-switch" href="../$1" hreflang="en" lang="en" aria-label="English version" title="English version">EN</a>')

    # The page's own address is its Russian one; the alternates name both and stay as they are.
    $html = [regex]::Replace($html, '(rel="canonical" href="|property="og:url" content=")' + [regex]::Escape($Origin), "`$1${Origin}ru/")

    # What lies beside the English pages is one folder up, but for the pages themselves.
    $pageNames = ($Pages | ForEach-Object { [regex]::Escape($_) }) -join '|'
    $html = [regex]::Replace($html, '\s(href|src)="([^"]*)"', {
            param($match)
            $url = $match.Groups[2].Value
            if ($url -match '^([a-z]+:|#|/|\.\./)' -or $url -match "^($pageNames)(#.*)?$") {
                return $match.Value
            }
            return " $($match.Groups[1].Value)=`"../$url`""
        })

    # Texts: an element with data-i18n holds text only, as a script never filled one with markup.
    $expected = [regex]::Matches($html, '\sdata-i18n="').Count
    $done = @{ Count = 0 }
    $html = [regex]::Replace($html, '(<(\w+)\b[^>]*\sdata-i18n="([^"]+)"[^>]*>)([^<]*)(</\2>)', {
            param($match)
            $done.Count++
            $text = Get-Russian -Key $match.Groups[3].Value -Page $Page
            if ($null -eq $text) {
                return $match.Value
            }
            return $match.Groups[1].Value + $text + $match.Groups[5].Value
        })
    if ($done.Count -ne $expected) {
        $problems.Add("site/${Page}: $($expected - $done.Count) elements with data-i18n hold markup, not text")
    }

    # Attributes.
    $attributes = [ordered]@{ 'data-i18n-alt' = 'alt'; 'data-i18n-content' = 'content'; 'data-i18n-aria' = 'aria-label' }
    $html = [regex]::Replace($html, '<[a-zA-Z][^>]*\sdata-i18n-(alt|content|aria)="[^>]*>', {
            param($match)
            $tag = $match.Value
            foreach ($marker in $attributes.Keys) {
                $key = [regex]::Match($tag, "\s$marker=`"([^`"]+)`"")
                if (-not $key.Success) {
                    continue
                }
                $name = $attributes[$marker]
                $text = Get-Russian -Key $key.Groups[1].Value -Page $Page
                if ($null -eq $text) {
                    continue
                }
                if ($tag -notmatch "\s$([regex]::Escape($name))=`"[^`"]*`"") {
                    $problems.Add("site/${Page}: $marker=`"$($key.Groups[1].Value)`" on a tag with no $name")
                    continue
                }
                $tag = [regex]::Replace($tag, "(\s$([regex]::Escape($name))=`")[^`"]*(`")", { param($m) $m.Groups[1].Value + $text + $m.Groups[2].Value })
            }
            return $tag
        })
    return $html
}

$versioned = [ordered]@{}
$made = [ordered]@{}
foreach ($page in $Pages) {
    $versioned[$page] = Add-FileVersions -Html ([System.IO.File]::ReadAllText((Join-Path $Site $page)))
    $made[$page] = ConvertTo-RussianPage -Page $page -Html $versioned[$page]
}
foreach ($key in $dictionary.Keys) {
    if (-not $used.Contains($key)) {
        $problems.Add("deploy/site-ru.json: $key is used by no page")
    }
}
if ($problems.Count -gt 0) {
    $problems | ForEach-Object { Write-Host $_ }
    throw "$($problems.Count) problems with the site's Russian pages."
}

if ($Check) {
    $stale = [System.Collections.Generic.List[string]]::new()
    foreach ($page in $Pages) {
        if ([System.IO.File]::ReadAllText((Join-Path $Site $page)) -cne $versioned[$page]) {
            $stale.Add("site/${page}: versions of the files it names")
        }
    }
    foreach ($page in $Pages) {
        $path = Join-Path $Target $page
        if (-not (Test-Path -LiteralPath $path) -or [System.IO.File]::ReadAllText($path) -cne $made[$page]) {
            $stale.Add("site/ru/${page}")
        }
    }
    if (Test-Path -LiteralPath $Target) {
        foreach ($extra in Get-ChildItem -LiteralPath $Target -File | Where-Object { $_.Name -notin $Pages }) {
            $stale.Add("site/ru/$($extra.Name): made from no page")
        }
    }
    if ($stale.Count -gt 0) {
        throw "Not what deploy/site-pages.ps1 makes, run it: $($stale -join ', ')"
    }
    Write-Host "The site's Russian pages are current."
    return
}

foreach ($page in $Pages) {
    [System.IO.File]::WriteAllText((Join-Path $Site $page), $versioned[$page], $Utf8)
}
New-Item -ItemType Directory -Path $Target -Force | Out-Null
foreach ($page in $Pages) {
    [System.IO.File]::WriteAllText((Join-Path $Target $page), $made[$page], $Utf8)
}
Write-Host "$($Pages.Count) Russian pages written to $Target."
