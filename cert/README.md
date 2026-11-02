# Update signing keys

This folder holds the keys and certificates that sign firmware and effects updates for your garland. Git tracks this file and nothing else here: see [`.gitignore`](../.gitignore), and `deploy/tests/sensitive-data.tests.ps1`, which fails if anything else in this folder would be committed.

A fresh clone has no keys. A build fails until you make them, and it does not make them by itself:

```bash
pwsh -File deploy\new-signing-certificate.ps1
```

That needs ESP-IDF's Python. On a new machine the script first makes a test build, which installs it, as a build for real stops at the missing keys before it gets that far. See sections 3 and 4 of [Building](../docs/guides/build-and-flash.md).

| File | What it is | Where it belongs |
|---|---|---|
| `ota-root-ca.local.key` | The root key, under your passphrase. It issues signing certificates | Off this machine, apart from the passphrase |
| `ota-root-ca.local.crt` | The root certificate. Each firmware built here trusts it | Here, a build needs it |
| `ota-signing.local.key` | The key that signs each update | Here, a build needs it |
| `ota-signing.local.crt` | The signing certificate. It goes into every update file, so it is public once you publish one | Here, a build needs it |

**Nothing recovers the root key or its passphrase.** Without them, a board trusting this root takes new firmware over USB only. Back them up, separately from each other.

A board has its own keys for the portal's HTTPS, made on the board. They do not come from this folder.
