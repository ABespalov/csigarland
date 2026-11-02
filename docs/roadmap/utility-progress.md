# Utility: Status

Where the public API and the Go + Gio utility stand at 1.0.0, and what is planned. The API is described in [its own page](../stubs/PUBLIC-API.md), and the utility in [its guide](../guides/utility.md).

## In 1.0.0

- **Firmware:** `/api/v1/`, one wildcard per method with its own table; errors as JSON; the token in `Authorization: Bearer`; pairing that answers with the token; `about`, `power`, any effect's file, and the log, kept once it is first read. Host tests and a board build.
- **`internal/api`:** the client, with the root pinned, the token, typed answers, discovery by mDNS checked by `about`, and tests against a stub server, among them one real trip over TLS.
- **`internal/settings` and `internal/secret`:** the address and root in a file, the token in Credential Manager.
- **`internal/engine`:** the firmware's effect engine through cgo, with every shipped effect run in a test.
- **`internal/ui`:** the window in Gio, after the csiPass companion: connect and pair, Effects, Tune with a live preview and an editor of every field, Schedule, Device, Network, Firmware with updates from a file or the online catalog, Info and Log. Every page laid out in a test.
- **Build:** `deploy/build-utility.ps1`, the same file to the byte from the same sources, Go and compiler, as [Building a Release Again](../guides/rebuild.md) says.

## Planned

- **To see on the board,** all of it never yet run against a real garland: pairing, token expiry and forgetting, a new root; every route's shape against `internal/api`, whose test holds only the field names, and its errors; the log, and the heap with the log kept; an update sent from the utility, refused for its signature, cancelled before and after the garland took it, with its answer lost, gone back, and confirmed.
- **Ubuntu:** Secret Service for the token, a file dialog, the window's development headers in CI.
- **macOS:** Keychain, a file dialog.
- **Android and iOS:** the same code, with cgo on phones still to be tried.
