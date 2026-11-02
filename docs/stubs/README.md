<!--
  Copyright (c) 2026 Anton Bespalov. All rights reserved.
  Licensed under the terms specified by the MIT License
  Part of the csiGarland project https://github.com/ABespalov/csigarland

  Purpose: Policy for the short names under docs/stubs/
  Contributors: None
-->

# Documentation stubs

Short names for documents live here as **symbolic links** to the real files elsewhere under `docs/`. This README is the only file here with content of its own.

Cross-references in tracked documents, comments and the code cite **`docs/stubs/<name>`**, not the real path, so that a document can move by changing its one link.

Do not edit through a stub: open the target and change that. Do not put "Moved" redirect pages beside the real documents.

| Stub | Target |
|---|---|
| `docs/stubs/PUBLIC-API.md` | `docs/roadmap/public-api.md` |
| `docs/stubs/ROADMAP.md` | `docs/roadmap/ROADMAP.md` |
| `docs/stubs/TODO.md` | `docs/roadmap/TODO.md` |

On Windows, a checkout shows real symlinks only when Git can create them (`core.symlinks=true` and a privilege that allows it, such as Developer Mode). Otherwise Git writes each stub as a one-line text file holding the relative target path, which is still the stub and not a second copy of the document.

**Adding one with `core.symlinks=false`:** `git add` alone would record a plain file, so write the one line with no line ending, and give it the link's mode:

```bash
git update-index --add --cacheinfo "120000,$(git hash-object -w --no-filters docs/stubs/NAME.md),docs/stubs/NAME.md"
```

`deploy/tests/source-rules.tests.ps1` checks that each stub is one line that leads to a file, and that git holds it as a link.
