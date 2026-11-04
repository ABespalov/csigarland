# Changelog

All notable changes to the `csiGarland` project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versions are not Semantic Versioning: a release is a base, such as `1.0.0`, and the firmware and
the utility each add their own build number to it, such as `1.0.0.102` and `1.0.0.27`. Updates
tell builds apart by the hashes of what they hold.

## [Unreleased]

## [1.1.0] - 2026-11-04

Everything a 1.0.0 garland saved, settings, pairings, uploaded effects, favourites, the schedule and its root certificate, stays as it was after this update. What this one saves reads in 1.0.0 as it means here, should a garland go back to it: a rise as a plain turn on, and the playlist as not set.

### Firmware

- The light fades in over 0.8 s when the garland turns on, and out over 0.8 s before the rail goes off, by the buttons, the portal, the countdown or the schedule.
- A scheduled action can turn the garland on rising from dark over a minute, or start the playlist.
- A playlist: up to 16 effects played in turn, each for 10 s to an hour, started and stopped from the portal or the utility, and remembered across a restart. Choosing a single effect, by any button, page or action, stops it, and the playlist moving on is no choice of the owner's that would outrank the schedule.
- With a playlist set, a long press of EFFECT starts it, or stops it and keeps what plays. Going back to the first favourite is now a double click of EFFECT.
- While the playlist plays, the screen's title says where it stands, as `Playing 3/12`.
- Every change of effect cross-fades from the last frame of the one before, over 0 to 5 s, 1 s unless set otherwise with the playlist.
- Push updates: the garland sends its state as it changes, over a stream of Server-Sent Events, to two pages or programs at a time.
- A schedule with a rise or a playlist step is not replaced by a program that knows neither, such as the 1.0.0 utility: it is told to update.
- A domain setting that could not be read, or a key that could not be read for want of memory, no longer makes the garland replace its HTTPS root: it keeps its keys, and serves HTTP until a start reads them. A portal key that cannot be read is made again under the root kept, which browsers trust already.
- A playlist keeps an effect the garland lacks, as after an update of the effects, by its id, and plays it again once it is back.
- A scheduled action naming an effect the garland lacks, as after an update of the effects, stays, and a change of another action no longer has the whole list refused for it.
- The setup network's exception from pairing holds only while that network is up, and never for a home network of the same addresses.
- A client sending slowly can no longer hold up the portal, an update among it: a request's headers, its body and an update each have a time limit, and a connection past it, or one whose request was answered before its body was read, is closed. A connection left idle is closed after 10 s. A renewal of the certificate waits for an update being received rather than restart in the middle of it.
- An Off by the POWER button's countdown during an update stays after the restart that ends it.
- A list of favourites too long to save whole is refused, and nothing is changed, where it was saved in part and lost the rest at the next start.
- An effect file with a NUL byte in it is refused whole, on upload and on preview.
- A change of the chain's length is drawn whole, and held to the 1400 mA limit, before a frame goes out over it: one sent in between could carry an older frame's tail past the limit, by the firmware's own model up to some 2.6 A for full white from 32 back to 256 LEDs.

### Portal

- The Effects page holds the playlist: its effects, how long each plays, the fade between them, and Play or Stop. A list saved from another page or program since it was read is not written over: the page says so, and reads it again. Its Now says where the playlist stands, and follows the garland as it changes, asking for nothing while the stream is open.
- The Schedule page shows the next 24 hours, by the garland's clock: when the garland is lit, each action due, and where actions meet in one minute, in place of the next action alone. Its form has the rise from dark and the playlist.
- An upload whose connection was lost is no longer called refused at once: the page watches what the garland then runs, and says whether it took the file.

### Public API

- `GET` and `POST /api/v1/playlist`, `POST /api/v1/playlist/play`, `GET /api/v1/schedule/timeline`, and `GET /api/v1/events`, the push updates. `GET /api/v1/state` says how far the scheduler has done its steps, `steps_done_until`, whether the playlist plays and where it stands, and a scheduled action may hold the power step `rise` and `"playlist": true`. Nothing that was there has changed.

### Utility

- The Effects page holds the playlist; the Schedule page shows the next 24 hours in place of the next action alone, and its form has the rise from dark and the playlist. The rail says `Playlist 3/12 (Fireplace)` while it plays.
- The window follows the garland's pushed state, on a connection of its own, and asks for it only when there is no stream.
- Tune shows the current an effect draws, by the firmware's own model, over the 20 s the preview plays in, with a line that moves with the preview, at the brightness chosen, and where the 1400 mA limit holds it back.
- The Info page saves the garland's schedule, favourites, playlist, screen and uploaded effects to a backup file, and puts them back on this garland or another.
- After an update confirms itself, the utility reads every route of the public API and says whether each answer reads as it expects. The Firmware page's Check the API does the same at any time.
- After a start, the footer counts down a firmware's trial until it confirms itself, as it did only during an update sent from here.
- Offers the newest release only when the garland's firmware is older; a Dev build counts as its number.
- A download from the catalog that ends after a file was chosen, or while the file dialog is open, leaves that file chosen, and installs nothing.
- `THIRD_PARTY_NOTICES.md` names what the utility's own file holds: Go's standard library and runtime, and the C++ runtime of GCC, winpthreads and mingw-w64's runtime it links statically. A build that links any other of the compiler's libraries fails.

### Build and release

- `deploy\publish-release.ps1` takes `-Release`, `-FirmwareVersion` and `-UtilityVersion`, publishes only files built from what the tag holds, checked by their sources' digests against a checkout of the tag, and an update file signed under the root published with it, and writes its release notes from this changelog. `-BuildNew` builds the firmware and the utility first, and releases what they made. `-Only Firmware` or `-Only Utility` publishes one of them, the utility for the platform `-Platform` names, and a later run adds to a release already made.
- A manifest's `source_sha256` is worked out from each source file's bytes, as `source_format` `raw-blobs/1` says. The manifests of 1.0.0 record the older form, and are not compared with it.
- The Release profile builds as the PlatformIO environment `esp32c6-release`, in a folder of its own, and the deploy scripts keep the objects through a change of `platformio.ini` or of the list of sources: a build compiles only what changed. `CSIGARLAND_PROFILE` no longer picks the profile, and `ota-update.ps1 -NoBuild` asks for `-BuildProfile` when both profiles are built, and refuses a build left of another profile or version, as one of 1.0.0. A Release build makes its `sdkconfig` again from the defaults when it differs from what they made, as after an edit by hand.
- A build that fails, is stopped, or fails a check after it gives its build number back, for the firmware and the utility, so the next build takes it.
- A firmware build of a checkout runs alone, and refuses to run beside another, so two never take one number; the utility's build does the same, and builds in a folder of its own, so a failed one leaves the file there as it was.
- `publish-release.ps1` checks a file the release already holds by its SHA-256 on GitHub, dates each build on the site by its own file, keeps the firmware catalog within what the utility of 1.0.0 reads, and checks the tag before `-BuildNew` builds anything.
- `ota-update.ps1` goes on watching the garland when an update gets no answer at all, as when the connection drops while it restarts, since it may have been installed, and says what it finds, calling it confirmed only when the garland shows it took this file.
- The guides install PlatformIO with the signing tool's `cryptography`, as a first build needs it before ESP-IDF's environment exists, and the scripts take the Python pio runs from, as the build does.

### Site

- The Downloads page lists each product once, the utility with Windows, Linux and macOS under it, every release newest first, with the day each build was released, and from 1.1.0 on, a link to its release notes. The documents and the roadmap say that released firmware is ready to download and install, and what has been seen working on the owner's garland.

### Hardware

- The star diffuser is an even 1.2 mm hollow shell that follows its facets. The solid rays with an ellipsoid chamber printed floating walls that showed through the translucent PETG near the centre. `csigarland_full_set.3mf` is rebuilt with it: the star plate takes 1 h 09 min and 17 g, where it took 1 h 30 min and 20 g.

## [1.0.0] - 2026-11-03

The first release: firmware 1.0.0.102 and the csiGarland utility 1.0.0.27 for Windows. It is the baseline, and nothing from before it is kept compatible with.

### Firmware

- 32 WS2812B LEDs by default, chains of up to 256 set by hand or estimated by a probe on the gated rail, with the 1400 mA limit applied to every frame and the power sequence of data low, gate, output.
- 154 effects in groups, from 101 algorithms in up to four layers, in an effects pack apart from the firmware with room for 512, favourites for the EFFECT button, and up to 32 effects uploaded or previewed from files.
- Three buttons, POWER, EFFECT and SPEED, with brightness steps, the off countdown and pairing; the screen in the csiPass design, with screensavers.
- Home Wi-Fi, a setup network with its password on the screen, a captive portal and mDNS; NTP time, an optional DS3231 clock module, and a schedule of up to 32 actions in the garland's time zone, where a choice made by hand outranks the steps before it.
- HTTPS with each garland's own root and certificates, made on the board, and browsers and programs paired by a press on the garland.
- Signed updates of firmware, effects or both under the owner's private root, checked before anything is written, with a trial after which the garland confirms itself or goes back, and plain HTTP left as a way back should HTTPS fail.

### Portal

- Effects, Schedule, Device, Network, Firmware and Info pages, self-contained, with glyph and word buttons, and the root certificate to install.

### Public API

- `/api/v1/` for programs: finding a garland by mDNS, pinning its root, pairing for a token, every setting the portal has, update files, and the garland's log for a unit with no USB.

### Utility

- The csiGarland utility for Windows, in Go and Gio: everything the portal does, Tune, an editor of every field of an effect with a preview drawn by the firmware's own engine, updates from a file or the online catalog of releases, an offer of a newer release as soon as it connects, and the log.

### Site

- [garland.csilab.am](https://garland.csilab.am): the product, the roadmap, the documents for owners and for builders, and the downloads, with the catalog of firmware releases the utility reads.

### Build and release

- Two build profiles: Release, with no debug information and the plain version, which is what is published, and Dev, with debug information and `-dev` on its version.
- Built again to the byte from the same sources, firmware and utility, as `docs/guides/rebuild.md` says, with deterministic signatures, a pinned toolchain and a check of two builds side by side.
- Hardware documents, a bill of materials and the 3D models of the enclosure and figurines.
