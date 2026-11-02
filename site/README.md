<!--
  Copyright (c) 2026 Anton Bespalov. All rights reserved.
  Licensed under the terms specified by the MIT License
  Part of the csiGarland project https://github.com/ABespalov/csigarland

  Purpose: How the public site at garland.csilab.am is made and published
  Contributors: None
-->

# csiGarland public site

Static English site for **https://garland.csilab.am**, served by Cloudflare Pages from this folder, with no build step.

| File | Role |
|---|---|
| `index.html` | The product: what it does, the figurines, the control unit, the portal and utility |
| `roadmap.html` | What is released, the milestones to the tree, and what is planned, after `docs/roadmap/ROADMAP.md` |
| `documents.html` | Links to the documents, for owners, and for builders and contributors |
| `downloads.html`, `downloads.js` | The latest release's files, drawn from `downloads/manifest.json` |
| `downloads/manifest.json` | What the Downloads page lists |
| `downloads/firmware-catalog-v1.json` | The catalog of firmware releases the utility reads |
| `assets/` | The wordmark, the icon, and pictures copied from `docs/images` |
| `_headers`, `_redirects` | Security and cache headers, and short links |

Everything is served from this origin alone, as the `Content-Security-Policy` in `_headers` asks: no external scripts, styles or fonts.

## Pictures

The site shows what the documents show, never a mock: the renders of the 3D models and the pictures `deploy/new-doc-images.ps1` makes from the firmware's and utility's own code. After either changes:

```powershell
pwsh -File deploy\site-assets.ps1
```

## Cloudflare Pages

1. Create a Pages project linked to the GitHub repository `ABespalov/csigarland`.
2. Set **Production branch** to `main`, **Root directory** to `site`, leave **Build command** empty, and set **Build output directory** to `/`.
3. Attach the custom domain `garland.csilab.am`. Cloudflare adds its CNAME to the `csilab.am` zone.

A garland reached from the internet needs a name of its own, not this one: a DNS-only record, such as `home.garland.csilab.am`, pointing at the router that forwards to it, and the same name in the domain field of its Network page. A proxied name would end TLS at Cloudflare, under a certificate that is not the garland's.

## Releases and the catalog

Binaries stay out of git: GitHub Releases of the repository are the file host. `deploy/publish-release.ps1` attaches a staged release to a GitHub Release and writes `downloads/manifest.json` and `downloads/firmware-catalog-v1.json` to point at it, which a push to `main` then publishes here.

The catalog lists each update file with its address, length and SHA-256. The utility downloads a file only as the catalog names it. The catalog is not what decides whether a file is good: the garland checks the file's signature under its own root before it writes a byte.

## Local preview

```powershell
cd site
python -m http.server 8080
```

Open `http://127.0.0.1:8080/`.
