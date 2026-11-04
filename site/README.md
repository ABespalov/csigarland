<!--
  Copyright (c) 2026 Anton Bespalov. All rights reserved.
  Licensed under the terms specified by the MIT License
  Part of the csiGarland project https://github.com/ABespalov/csigarland

  Purpose: How the public site at garland.csilab.am is made and published
  Contributors: None
-->

# csiGarland public site

Static English and Russian site for **https://garland.csilab.am**, served by Cloudflare Pages from this folder, with no build step. The English pages are at the root and the Russian ones under `ru/`, each naming the other with `hreflang`. A visitor whose browser prefers Russian over English is sent from an English page to its Russian twin; a toggle in the header leads to the other language and the browser remembers that choice. Every picture opens in a zoomable preview that pages through its gallery. The portal's Effects picture is `docs/images/portal/effects-short.png`, its first three effects, while the documents keep the whole page.

| File | Role |
|---|---|
| `index.html` | The product: what it does, the figurines, the control unit, the portal and utility |
| `roadmap.html` | What is released, the milestones to the tree, and what is planned, after `docs/roadmap/ROADMAP.md` |
| `documents.html` | Links to the documents, for owners, and for builders and contributors, and the control unit's schematic |
| `downloads.html`, `downloads.js` | Every release's files, newest first under each product and platform, drawn from `downloads/manifest.json` |
| `ru/` | The Russian pages, made by `deploy/site-pages.ps1`: edit the English page or `deploy/site-ru.json`, never these |
| `lang.js` | Sends a visitor who prefers Russian to the page under `ru/`, and remembers the toggle's choice |
| `site.js` | The zoomable picture preview |
| `downloads/manifest.json` | What the Downloads page lists, format `csigarland.downloads/2`: each product, the firmware, the utility and the root certificate, with a build for each platform, its version, release, the day it was released and, after 1.0.0, a link to its release notes; a platform not built yet as not yet |
| `downloads/firmware-catalog-v1.json` | The catalog of firmware releases the utility reads |
| `assets/` | The wordmark, the icon, and pictures copied from `docs/images` |
| `_headers`, `_redirects` | Security and cache headers, and short links |

Everything is served from this origin alone, as the `Content-Security-Policy` in `_headers` asks: no external scripts, styles or fonts.

## Pictures

The site shows what the documents show, never a mock: the renders of the 3D models and the pictures `deploy/new-doc-images.ps1` makes from the firmware's and utility's own code. After either changes:

```powershell
pwsh -File deploy\site-assets.ps1
```

## Russian pages

Every text of an English page that has a Russian one carries `data-i18n`, or `data-i18n-alt`, `data-i18n-content` or `data-i18n-aria` for an attribute, with a key in `deploy/site-ru.json`. After changing a page or a translation:

```powershell
pwsh -File deploy\site-pages.ps1
```

The source rules fail while `ru/` is not what it would make, when a key has no Russian text, or when a Russian text is used by no page. What scripts draw, the downloads and the preview's buttons, keeps its Russian in `downloads.js` and `site.js`, chosen by the page's `lang`.

## Cloudflare Pages

1. Create a Pages project linked to the GitHub repository `ABespalov/csigarland`.
2. Set **Production branch** to `main`, **Root directory** to `site`, leave **Build command** empty, and set **Build output directory** to `/`.
3. Attach the custom domain `garland.csilab.am`. Cloudflare adds its CNAME to the `csilab.am` zone.

A garland reached from the internet needs a name of its own, not this one: a DNS-only record, such as `home.garland.csilab.am`, pointing at the router that forwards to it, and the same name in the domain field of its Network page. A proxied name would end TLS at Cloudflare, under a certificate that is not the garland's.

## Releases and the catalog

Binaries stay out of git: GitHub Releases of the repository are the file host. `deploy/publish-release.ps1` attaches a staged release to a GitHub Release and only then writes `downloads/manifest.json` and `downloads/firmware-catalog-v1.json` to point at it, with the day GitHub dates it, keeping the releases before in both, newest first, and what a run does not publish, as with `-Only Firmware` or `-Only Utility`, as it was, which a push to `main` then publishes here. A run that stopped after making the release writes them with `-SiteFromRelease`. Its `-SiteOnly`, before the tag, writes them to `temp\` to look at, never here: they would list files not yet there.

The catalog lists each update file with its address, length and SHA-256. The utility downloads a file only as the catalog names it. The catalog is not what decides whether a file is good: the garland checks the file's signature under its own root before it writes a byte.

## Local preview

```powershell
cd site
python -m http.server 8080
```

Open `http://127.0.0.1:8080/`.
