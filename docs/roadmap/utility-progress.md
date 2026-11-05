# Utility: Status

Where the public API and the Go + Gio utility stand, and what is planned. The API is described in [its own page](../stubs/PUBLIC-API.md), and the utility in [its guide](../guides/utility.md).

## In 1.0.0

- **Firmware:** `/api/v1/`, one wildcard per method with its own table; errors as JSON; the token in `Authorization: Bearer`; pairing that answers with the token; `about`, `power`, any effect's file, and the log, kept once it is first read. Host tests and a board build.
- **`internal/api`:** the client, with the root pinned, the token, typed answers, discovery by mDNS checked by `about`, and tests against a stub server, among them one real trip over TLS.
- **`internal/settings` and `internal/secret`:** the address and root in a file, the token in Credential Manager.
- **`internal/engine`:** the firmware's effect engine through cgo, with every shipped effect run in a test.
- **`internal/ui`:** the window in Gio, after the csiPass companion: connect and pair, Effects, Tune with a live preview and an editor of every field, Schedule, Device, Network, Firmware with updates from a file or the online catalog, Info and Log. Every page laid out in a test.
- **Build:** `deploy/build-utility.ps1`, the same file to the byte from the same sources, Go and compiler, as [Building a Release Again](../guides/rebuild.md) says.

## In 1.1.0

- **API:** the playlist, its place in the state, the schedule's next 24 hours, the rise and the playlist as steps. `CheckRoutes` reads every route that changes nothing and holds each answer to the types here.
- **`internal/backup`:** the schedule, favourites, playlist, screen and uploads to a file and back, with a test that puts an erased garland back.
- **`internal/ui`:** the offer of a newer release, the playlist card, the next 24 hours, Tune's current by the firmware's model, the backup card, the API check after an update, the playlist in the rail, and the countdown of a trial after a start.

## Seen on the Board

The 1.0.0 utility against the owner's garland: pairing, the pages, updates and the log. What 1.1.0 adds is yet to be seen on it.

- **Push updates:** `Events` reads the garland's stream on a connection of its own, and the window asks for no state while it is open.
- **Notices for the `.exe`:** Go's own library and the C++ runtime it links statically are in `THIRD_PARTY_NOTICES.md`, and `deploy/build-utility.ps1` fails on a library of the compiler's that has no notice.

## Planned

- **Ubuntu:** written, with Secret Service for the token, the desktop portal's file dialog or zenity's, one window at a time over a socket, and the window's development headers in CI. Host tests pass on Ubuntu 22.04 with GNOME on Wayland, the token against GNOME Keyring; still to see against a garland, and to release.
- **macOS:** Keychain, a file dialog.
- **Android and iOS:** the same code, with cgo on phones still to be tried.
